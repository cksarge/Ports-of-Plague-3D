// Proves the rules match the web version: replays games recorded from the web
// version's own engine (Tools/golden/make_golden.mjs) and compares the whole
// game state, as JSON, after every single move.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PortsBots.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

namespace
{
	using V = FPortsValue;

	FString Sha(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		uint8 Hash[FSHA1::DigestSize];
		FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash);
		return BytesToHex(Hash, FSHA1::DigestSize).ToLower();
	}

	FString GoldenDir() { return FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools"), TEXT("golden"), TEXT("out")); }

	bool LoadGolden(const TCHAR* Name, FPortsValue& Out)
	{
		FString Text;
		return FFileHelper::LoadFileToString(Text, *FPaths::Combine(GoldenDir(), Name)) && FPortsValue::Parse(Text, Out);
	}

	FPortsSetup SetupFrom(const FPortsValue& In)
	{
		FPortsSetup Setup;
		for (const FPortsValue& P : In.Get(TEXT("players")).GetItems())
		{
			FPortsSetupPlayer Player;
			Player.name = P.Get(TEXT("name")).AsString();
			Player.home = P.Get(TEXT("home")).AsString();
			Player.bot = P.Get(TEXT("bot")).Truthy();
			Player.skill = P.Get(TEXT("skill")).AsString();
			Player.strategy = P.Get(TEXT("strategy")).AsString();
			Setup.players.Add(Player);
		}
		Setup.seed = In.Get(TEXT("seed")).AsString();
		if (In.Get(TEXT("mode")).IsString()) Setup.mode = In.Get(TEXT("mode")).AsString();
		if (In.Get(TEXT("difficulty")).IsString()) Setup.difficulty = In.Get(TEXT("difficulty")).AsString();
		Setup.prePlague = In.Get(TEXT("prePlague")).Truthy();
		Setup.timer = In.Get(TEXT("timer")).Truthy();
		return Setup;
	}

	V Reason(const FString& Why) { return Why.IsEmpty() ? V::Null() : V(Why); }

	// The same questions, in the same order, as battery() in make_golden.mjs.
	FString Battery(const FPortsState& State)
	{
		const FPortsData& D = FPortsData::Get();
		const FPortsPlayer& P = *Ports::CurrentPlayer(State);
		V Out = V::Array();
		const auto Check = [&](const FPortsAction& A) { Out.Add(Reason(Ports::CheckAction(State, A))); };
		for (const FString& From : P.posts)
		{
			for (const int32 Index : D.RoutesFrom(From))
			{
				for (const bool bOffshore : { false, true })
				{
					FPortsAction A;
					A.type = TEXT("ship"); A.from = From; A.route = D.Routes[Index].Id; A.offshore = bOffshore;
					Check(A);
					const FPortsShipQuote Q = Ports::ShipQuote(State, P, A.route, From, bOffshore);
					Out.Add(V::Array({ V(Q.to), Q.parts, V(Q.fixed), V(Q.min), V(Q.max), V(Q.contagionRisk), V(Q.safe), V(Q.fee), Q.partner >= 0 ? V(Q.partner) : V::Null() }));
				}
			}
		}
		for (const FPortsCity& C : D.Cities)
		{
			for (const TCHAR* Type : { TEXT("post"), TEXT("prepare"), TEXT("physician"), TEXT("marry"), TEXT("land"), TEXT("gates") })
			{
				FPortsAction A;
				A.type = Type; A.city = C.Id;
				Check(A);
			}
			Out.Add(V(Ports::IsThreatened(State, C.Id)));
		}
		TArray<FString> Places;
		Places.Add(Ports::ESTATE);
		Places.Append(P.posts);
		for (const FString& From : Places) for (const FString& To : Places) for (const int32 Count : { 1, 2, 3 })
		{
			FPortsAction A;
			A.type = TEXT("move"); A.from = From; A.to = To; A.count = Count;
			Check(A);
		}
		for (const TCHAR* Kind : { TEXT("hospital"), TEXT("confraternity"), TEXT("church"), TEXT("nonsense") })
		{
			FPortsAction A;
			A.type = TEXT("charity"); A.kind = Kind;
			Check(A);
		}
		{ FPortsAction A; A.type = TEXT("loan"); Check(A); }
		for (int32 Partner = 0; Partner <= State.players.Num(); Partner++) { FPortsAction A; A.type = TEXT("deal"); A.partner = Partner; Check(A); }
		{ FPortsAction A; A.type = TEXT("dance"); Check(A); }
		{ FPortsAction A; A.type = TEXT("ship"); A.from = P.home; A.route = TEXT("nope"); Check(A); }
		{ FPortsAction A; A.type = TEXT("ship"); A.from = TEXT("moscow"); A.route = TEXT("novgorod-moscow"); Check(A); }
		Out.Add(V(Ports::CharityCost(State, P)));
		Out.Add(V(Ports::Cost(State, TEXT("openPost"), &P)));
		Out.Add(V(Ports::ActionPointsFor(State, P)));
		Out.Add(V(Ports::LastPlaceId(State)));
		Out.Add(V(Ports::RoundNumber(State)));
		Out.Add(V(Ports::TotalRounds(State)));
		const FPortsRoundInfo Info = Ports::RoundInfo(State);
		Out.Add(V::Array({ V(Info.round), V(Info.pre), V(Info.label), V(Info.months), V(Info.headline), Info.factIds }));
		for (const FPortsPlayer& H : State.players)
		{
			const FPortsScore S = Ports::ScorePlayer(H);
			Out.Add(V::Object({ { TEXT("wealth"), S.wealth }, { TEXT("family"), S.family }, { TEXT("reputation"), S.reputation }, { TEXT("balance"), S.balance }, { TEXT("total"), S.total } }));
		}
		V Ships = V::Array(), Posts = V::Array();
		for (const FPortsAction& A : Ports::LegalShipments(State, P)) Ships.Add(A.ToValue());
		for (const FPortsAction& A : Ports::LegalPosts(State, P)) Posts.Add(A.ToValue());
		Out.Add(Ships);
		Out.Add(Posts);
		return Sha(Out.ToJson());
	}

	// Replays one recorded game. Returns an empty string if every step matched.
	FString Replay(int32 GameIndex, const FPortsValue& Game, int32& OutSteps)
	{
		const FPortsValue& Steps = Game.Get(TEXT("steps"));
		const FPortsSetup Setup = SetupFrom(Game.Get(TEXT("setup")));
		FPortsState State;
		FString Problem;
		if (!Ports::CreateGame(Setup, State, Problem)) return FString::Printf(TEXT("game %d: setup refused: %s"), GameIndex, *Problem);
		int32 Index = 0;
		const bool bRoundTrip = GameIndex % 5 == 0;
		const bool bEveryMove = GameIndex % 6 == 0;
		uint32 Lcg = Ports::SeedFrom(Setup.seed) ^ 0x9e3779b9u;
		const auto Chance = [&Lcg](uint32 OneIn) { Lcg = Lcg * 1103515245u + 12345u; return (Lcg >> 16) % OneIn == 0; };

		// Compares the state (and anything else recorded) with the next recorded step.
		const auto Expect = [&](const TCHAR* Op, const FString& BatteryHash, const FString& Detail, const FString& WantDetail) -> FString
		{
			if (Index >= Steps.Num()) return FString::Printf(TEXT("game %d: the recording ends at step %d but this game goes on (%s)"), GameIndex, Index, Op);
			const FPortsValue& Step = Steps[Index];
			const FString Where = FString::Printf(TEXT("game %d (%s), step %d"), GameIndex, *Setup.seed, Index);
			if (Step.Get(TEXT("op")).AsString() != Op) return FString::Printf(TEXT("%s: expected \"%s\" but did \"%s\" %s"), *Where, *Step.Get(TEXT("op")).AsString(), Op, *Detail);
			if (!WantDetail.IsEmpty() && Detail != Step.Get(*WantDetail).ToJson()) return FString::Printf(TEXT("%s: %s chose %s, the web version chose %s"), *Where, Op, *Detail, *Step.Get(*WantDetail).ToJson());
			if (Step.Get(TEXT("b")).IsString() && !BatteryHash.IsEmpty() && Step.Get(TEXT("b")).AsString() != BatteryHash) return FString::Printf(TEXT("%s: the allowed moves or scores before \"%s\" differ"), *Where, Op);
			const FString Json = State.ToJson();
			if (Sha(Json) != Step.Get(TEXT("h")).AsString())
			{
				const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Golden"), FString::Printf(TEXT("actual_%d_%d.json"), GameIndex, Index));
				FFileHelper::SaveStringToFile(Json, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				return FString::Printf(TEXT("%s: the state after \"%s\" %s differs (saved to %s)"), *Where, Op, *Detail, *File);
			}
			if (bRoundTrip)
			{
				FPortsState Restored;
				if (!FPortsState::FromJson(Json, Restored) || Restored.ToJson() != Json) return FString::Printf(TEXT("%s: the state does not survive saving and loading"), *Where);
				State = Restored;
			}
			Index++;
			return FString();
		};
#define PORTS_EXPECT(...) { const FString Bad = Expect(__VA_ARGS__); if (!Bad.IsEmpty()) { OutSteps += Index; return Bad; } }

		PORTS_EXPECT(TEXT("create"), FString(), FString(), FString());
		for (int32 Guard = 0; State.phase != TEXT("ended") && Guard < 5000; Guard++)
		{
			if (State.phase != TEXT("actions"))
			{
				Ports::Advance(State);
				PORTS_EXPECT(TEXT("advance"), FString(), FString(), FString());
				continue;
			}
			if (GameIndex % 3 == 0 && Chance(9))
			{
				const FString B = Battery(State);
				Ports::TimeUp(State);
				PORTS_EXPECT(TEXT("timeUp"), B, FString(), FString());
				continue;
			}
			for (int32 Moves = 0; Moves < 40; Moves++)
			{
				const FString B = bEveryMove || Moves == 0 ? Battery(State) : FString();
				const FPortsBotMove Move = Ports::BotMove(State);
				if (Move.type == FPortsBotMove::EType::End)
				{
					PORTS_EXPECT(TEXT("botEnd"), B, FString(), FString());
					break;
				}
				if (Move.type == FPortsBotMove::EType::Decide)
				{
					Ports::BotDecide(State, Move.choice);
					PORTS_EXPECT(TEXT("decide"), B, Ports::ChoiceToValue(Move.choice).ToJson(), TEXT("choice"));
				}
				else
				{
					const FPortsResult R = Ports::PerformAction(State, Move.action);
					PORTS_EXPECT(TEXT("act"), B, Move.action.ToValue().ToJson(), TEXT("action"));
					if (!R.ok) break;
				}
			}
			const FPortsResult R = Ports::EndTurn(State);
			PORTS_EXPECT(TEXT("endTurn"), FString(), V(R.next).ToJson(), TEXT("next"));
		}
#undef PORTS_EXPECT
		OutSteps += Index;
		if (Index != Steps.Num()) return FString::Printf(TEXT("game %d: finished after %d steps, the web version took %d"), GameIndex, Index, Steps.Num());
		return FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortsGoldenStepsTest, "Ports.Engine.Golden.EveryMoveMatchesTheWebVersion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPortsGoldenStepsTest::RunTest(const FString&)
{
	if (!TestTrue(TEXT("data files load"), FPortsData::EnsureLoaded())) return false;
	FPortsValue Games;
	if (!TestTrue(TEXT("Tools/golden/out/steps.json loads (run node Tools/golden/make_golden.mjs)"), LoadGolden(TEXT("steps.json"), Games))) return false;
	int32 Steps = 0, Failures = 0;
	for (int32 i = 0; i < Games.Num(); i++)
	{
		const FString Bad = Replay(i, Games[i], Steps);
		if (!Bad.IsEmpty())
		{
			AddError(Bad);
			if (++Failures >= 5) break;
		}
	}
	AddInfo(FString::Printf(TEXT("PORTS-GOLDEN steps: %d games, %d steps compared, %d games differ"), Games.Num(), Steps, Failures));
	UE_LOG(LogTemp, Display, TEXT("PORTS-GOLDEN steps: %d games, %d steps compared, %d games differ"), Games.Num(), Steps, Failures);
	return Failures == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortsGoldenWholeTest, "Ports.Engine.Golden.WholeGamesMatchTheWebVersion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPortsGoldenWholeTest::RunTest(const FString&)
{
	if (!TestTrue(TEXT("data files load"), FPortsData::EnsureLoaded())) return false;
	FPortsValue Games;
	if (!TestTrue(TEXT("Tools/golden/out/whole.json loads"), LoadGolden(TEXT("whole.json"), Games))) return false;
	int32 Failures = 0;
	for (int32 i = 0; i < Games.Num(); i++)
	{
		FPortsSetup Setup = SetupFrom(Games[i].Get(TEXT("setup")));
		FPortsState State;
		const int32 Turns = Ports::PlayBotGame(Setup, State);
		if (Turns != Games[i].Get(TEXT("turns")).AsInt() || Sha(State.ToJson()) != Games[i].Get(TEXT("h")).AsString())
		{
			AddError(FString::Printf(TEXT("whole game %d (%s) ends differently: %d turns here, %d in the web version"), i, *Setup.seed, Turns, Games[i].Get(TEXT("turns")).AsInt()));
			if (++Failures >= 5) break;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PORTS-GOLDEN whole: %d games compared, %d differ"), Games.Num(), Failures);
	return Failures == 0;
}

#endif
