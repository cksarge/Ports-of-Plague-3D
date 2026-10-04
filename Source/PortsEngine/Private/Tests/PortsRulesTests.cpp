// The web version's rules tests (tests/engine.test.js and tests/bots.test.js),
// rewritten case for case. The multi-device case of bots.test.js and the
// rule-book text case belong to later stages.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PortsBots.h"

namespace
{
	using V = FPortsValue;
	using namespace Ports;

	FPortsSetupPlayer House(const TCHAR* Name, const TCHAR* Home, const TCHAR* Strategy = TEXT(""))
	{
		FPortsSetupPlayer P;
		P.name = Name; P.home = Home; P.strategy = Strategy;
		return P;
	}

	FPortsSetupPlayer Bot(const TCHAR* Name, const TCHAR* Home, const TCHAR* Skill)
	{
		FPortsSetupPlayer P;
		P.name = Name; P.home = Home; P.bot = true; P.skill = Skill;
		return P;
	}

	FPortsSetup Four(const FString& Seed)
	{
		FPortsSetup S;
		S.players = { House(TEXT("Ada"), TEXT("venice")), House(TEXT("Bo"), TEXT("london")), House(TEXT("Cy"), TEXT("lubeck")), House(TEXT("Di"), TEXT("genoa")) };
		S.seed = Seed;
		return S;
	}
	FPortsSetup Four(int32 Seed) { return Four(FString::FromInt(Seed)); }

	FPortsState Game(const FPortsSetup& Setup)
	{
		FPortsState State;
		FString Problem;
		CreateGame(Setup, State, Problem);
		return State;
	}

	// Advance to the first player's action phase.
	void ToActions(FPortsState& S) { while (S.phase != TEXT("actions")) Advance(S); }

	FPortsPlayer& ClearPending(FPortsState& S)
	{
		FPortsPlayer& P = *CurrentPlayer(S);
		while (P.pending.Num()) Decide(S, P.pending[0].Get(TEXT("kind")).AsString() == TEXT("wageLaw") ? EPortsChoice::Pay : EPortsChoice::No);
		return P;
	}

	// Ends every remaining turn this round (declining offers), which runs the plague phase.
	void FinishRound(FPortsState& S)
	{
		while (S.phase == TEXT("actions")) { ClearPending(S); EndTurn(S); }
	}

	// Plays on until it is this house's turn in the next round.
	FPortsPlayer& NextTurnOf(FPortsState& S, int32 Id)
	{
		FinishRound(S);
		ToActions(S);
		while (CurrentPlayer(S)->id != Id) { ClearPending(S); EndTurn(S); }
		return ClearPending(S);
	}

	FPortsAction Act(const TCHAR* Type, const FString& City = FString()) { FPortsAction A; A.type = Type; A.city = City; return A; }
	FPortsAction Ship(const FString& From, const FString& Route, bool bOffshore = false) { FPortsAction A; A.type = TEXT("ship"); A.from = From; A.route = Route; A.offshore = bOffshore; return A; }
	FPortsAction Move(const FString& From, const FString& To, int32 Count) { FPortsAction A; A.type = TEXT("move"); A.from = From; A.to = To; A.count = Count; return A; }
	FPortsAction Charity(const TCHAR* Kind) { FPortsAction A; A.type = TEXT("charity"); A.kind = Kind; return A; }
	FPortsAction Deal(int32 Partner) { FPortsAction A; A.type = TEXT("deal"); A.partner = Partner; return A; }

	TArray<FString> Stricken(const FPortsState& S)
	{
		TArray<FString> Out;
		for (int32 i = 0; i < S.cities.Num(); i++) if (S.cities[i].state == TEXT("stricken")) Out.Add(FPortsData::Get().Cities[i].Id);
		Out.Sort();
		return Out;
	}

	TArray<FString> ArrivedBy(int32 Round)
	{
		TArray<FString> Out;
		for (const FPortsCity& C : FPortsData::Get().Cities) if (C.ArrivalRound <= Round) Out.Add(C.Id);
		Out.Sort();
		return Out;
	}

	int32 CountLog(const FPortsState& S, const TCHAR* Type)
	{
		int32 N = 0;
		for (const V& E : S.log) if (E.Get(TEXT("type")).AsString() == Type) N++;
		return N;
	}

	int32 Cfg(const TCHAR* Path) { return FPortsData::Get().Int(Path); }
}

#define PORTS_TEST(Class, Name) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, "Ports.Engine.Rules." Name, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool Class::RunTest(const FString&)
#define CHECK(What, Cond) TestTrue(TEXT(What), (Cond))
#define LOADED() if (!TestTrue(TEXT("data files load"), FPortsData::EnsureLoaded())) return false

PORTS_TEST(FPortsTestSetupValidates, "setup validates player count and home cities")
{
	LOADED();
	FPortsState S;
	FString Why;
	FPortsSetup Solo; Solo.players = { House(TEXT("Solo"), TEXT("venice")) };
	CHECK("one house is refused", !CreateGame(Solo, S, Why) && Why.Contains(TEXT("between 2 and 6")));
	FPortsSetup Same; Same.players = { House(TEXT("A"), TEXT("venice")), House(TEXT("B"), TEXT("venice")) };
	CHECK("a shared home is refused", !CreateGame(Same, S, Why) && Why.Contains(TEXT("cannot share")));
	FPortsSetup NotHome; NotHome.players = { House(TEXT("A"), TEXT("venice")), House(TEXT("B"), TEXT("caffa")) };
	CHECK("a city that is not a home city is refused", !CreateGame(NotHome, S, Why) && Why.Contains(TEXT("not one of the home cities")));
	CHECK("eight home cities", FPortsData::Get().HomeCities.Num() == 8);
	return true;
}

PORTS_TEST(FPortsTestSetupStart, "setup gives starting resources, Caffa stricken, random turn order")
{
	LOADED();
	const FPortsState S = Game(Four(7));
	for (const FPortsPlayer& P : S.players)
	{
		const V& Bonus = FPortsData::Get().FindCity(P.home)->Raw.Get(TEXT("home"));
		CHECK("starting florins", P.florins == Cfg(TEXT("start.florins")) + Bonus.Get(TEXT("startFlorins")).AsInt());
		CHECK("starting family", FamilyTotal(P) == Cfg(TEXT("start.family")));
		CHECK("one post, at home", P.posts.Num() == 1 && P.posts[0] == P.home);
	}
	CHECK("Caffa is stricken", S.City(TEXT("caffa"))->state == TEXT("stricken"));
	TArray<int32> Order = S.order;
	Order.Sort();
	CHECK("every house is in the turn order", Order == TArray<int32>({ 0, 1, 2, 3 }));
	return true;
}

PORTS_TEST(FPortsTestTimelineRound1, "timeline round 1 strikes exactly the cities that fell in Late 1347")
{
	LOADED();
	FPortsState S = Game(Four(3));
	Advance(S);
	CHECK("round 1", S.round == 1);
	CHECK("stricken cities match history", Stricken(S) == ArrivedBy(1));
	CHECK("Messina's chronicle card is in play", S.currentChronicle.Contains(TEXT("CHR-messina")));
	return true;
}

PORTS_TEST(FPortsTestTimelineAll, "timeline every city is struck no later than its historical round")
{
	LOADED();
	for (int32 Seed = 1; Seed <= 20; Seed++)
	{
		FPortsSetup Setup = Four(Seed);
		for (FPortsSetupPlayer& P : Setup.players) P.strategy = TEXT("greedy");
		Setup.prePlague = true;
		FPortsState S;
		PlayBotGame(Setup, S);
		for (const FPortsCityState& C : S.cities) CHECK("every city was struck", C.state != TEXT("safe"));
	}
	return true;
}

PORTS_TEST(FPortsTestTurnOrder, "turn order is rolled once at the start, then fixed for the whole game")
{
	LOADED();
	FPortsState S = Game(Four(11));
	const V* RollEntry = S.log.FindByPredicate([](const V& E) { return E.Get(TEXT("type")).AsString() == TEXT("orderRoll"); });
	if (!CHECK("turn-order dice were rolled", RollEntry && RollEntry->Get(TEXT("rolls")).Num() >= 1)) return false;
	const V& First = RollEntry->Get(TEXT("rolls"))[0];
	int32 Top = 0;
	for (const V& R : First.GetItems()) Top = FMath::Max(Top, R.Get(TEXT("die")).AsInt());
	bool bTopFirst = false;
	for (const V& R : First.GetItems()) if (R.Get(TEXT("die")).AsInt() == Top && R.Get(TEXT("player")).AsInt() == S.order[0]) bTopFirst = true;
	CHECK("a highest roller goes first", bTopFirst);
	const TArray<int32> Order = S.order;
	ToActions(S);
	TArray<int32> Seen;
	for (int32 i = 0; i < 4; i++)
	{
		FPortsPlayer& P = ClearPending(S);
		Seen.Add(P.id);
		const bool bFavor = S.guildFavor.IsSet() && *S.guildFavor == P.id;
		CHECK("action points", P.ap == Cfg(TEXT("modes.standard.actionPoints")) + (bFavor ? Cfg(TEXT("comeback.guildFavorAP")) : 0));
		CHECK("turn ends", EndTurn(S).ok);
	}
	CHECK("houses played in the rolled order", Seen == Order);
	CHECK("plague phase follows", S.phase == TEXT("plague"));
	CHECK("order never changes", S.order == Order);
	return true;
}

PORTS_TEST(FPortsTestIllegal, "illegal moves are blocked with a reason")
{
	LOADED();
	FPortsState S = Game(Four(5));
	ToActions(S);
	FPortsPlayer& P = *CurrentPlayer(S);
	if (P.pending.Num()) CHECK("a waiting card blocks actions", CheckAction(S, Charity(TEXT("church"))).Contains(TEXT("answer the card")));
	ClearPending(S);
	// Stricken city: no new posts.
	P.posts.Add(TEXT("constantinople")); // make it adjacent for the test
	const FString Why = CheckAction(S, Act(TEXT("post"), TEXT("messina")));
	CHECK("no post in a stricken city", Why.Contains(TEXT("Stricken")) || Why.Contains(TEXT("gates")));
	// Too many family moved.
	CHECK("at most 2 family move", CheckAction(S, Move(P.home, ESTATE, 3)).Contains(TEXT("at most 2")));
	// Ship twice from the same post.
	const TArray<FPortsAction> Ships = LegalShipments(S, P);
	if (!CHECK("there is a legal shipment", Ships.Num() > 0)) return false;
	CHECK("first shipment succeeds", PerformAction(S, Ships[0]).ok);
	CHECK("a post ships once per round", CheckAction(S, Ships[0]).Contains(TEXT("already shipped")));
	// Opening a post takes 2 action points; only 1 is left after shipping.
	CHECK("a post takes 2 action points", CheckAction(S, Act(TEXT("post"), TEXT("moscow"))).Contains(TEXT("takes 2 action points")));
	P.ap = 2;
	CHECK("not connected", CheckAction(S, Act(TEXT("post"), TEXT("moscow"))).Contains(TEXT("not connected")));
	P.florins = 0;
	CHECK("out of money", CheckAction(S, Charity(TEXT("church"))).Contains(TEXT("costs")));
	P.ap = 0;
	CHECK("out of action points", CheckAction(S, Act(TEXT("prepare"), P.home)).Contains(TEXT("no action points")));
	return true;
}

PORTS_TEST(FPortsTestShippingFormula, "shipping profit follows the published formula")
{
	LOADED();
	for (int32 Seed = 1; Seed <= 30; Seed++)
	{
		FPortsState S = Game(Four(Seed));
		ToActions(S);
		FPortsPlayer& P = ClearPending(S);
		const TArray<FPortsAction> Ships = LegalShipments(S, P);
		if (Ships.Num() == 0) continue;
		const FPortsShipQuote Q = ShipQuote(S, P, Ships[0].route, Ships[0].from);
		const FPortsResult R = PerformAction(S, Ships[0]);
		CHECK("shipment succeeds", R.ok);
		const int32 Die = R.entry.Get(TEXT("profitDie")).AsInt();
		const int32 Raw = FMath::Max(0, Q.fixed + Die);
		const int32 Want = R.entry.Get(TEXT("infected")).AsBool() ? FMath::FloorToInt32(Raw * FPortsData::Get().Number(TEXT("shipping.infectedProfitFactor"))) : Raw;
		CHECK("profit is the fixed part plus the die", R.entry.Get(TEXT("profit")).AsInt() == Want);
		CHECK("the die is 1 to 6", Die >= 1 && Die <= 6);
	}
	return true;
}

PORTS_TEST(FPortsTestContagion, "contagion infected cargo brings the plague at most one round early")
{
	LOADED();
	FPortsState S = Game(Four(21));
	ToActions(S);
	FPortsPlayer& P = ClearPending(S);
	// Round 1: Messina is Stricken; Genoa historically falls in round 2 (one round later).
	P.posts.Add(TEXT("messina"));
	S.City(TEXT("messina"))->severity = 3;
	S.effects.contagion.all = 6; // guarantee infection for the test
	const FPortsResult R = PerformAction(S, Ship(TEXT("messina"), TEXT("messina-genoa")));
	CHECK("cargo is infected", R.entry.Get(TEXT("infected")).AsBool());
	CHECK("Genoa is stricken", S.City(TEXT("genoa"))->state == TEXT("stricken"));
	CHECK("Genoa was struck early", S.City(TEXT("genoa"))->early);
	// Tunis falls in round 3: two rounds ahead, so no early spread.
	const FPortsResult R2 = PerformAction(S, Ship(P.home == TEXT("genoa") ? TEXT("genoa") : TEXT("messina"), TEXT("messina-tunis")));
	if (R2.ok && R2.entry.Get(TEXT("infected")).AsBool()) CHECK("Tunis stays safe", S.City(TEXT("tunis"))->state == TEXT("safe"));
	return true;
}

PORTS_TEST(FPortsTestSurvival, "survival mortality rolls, inheritance, and the last heir never dies")
{
	LOADED();
	int32 Deaths = 0;
	for (int32 Seed = 1; Seed <= 200; Seed++)
	{
		FPortsState S = Game(Four(Seed));
		FPortsPlayer& P = S.players[0];
		S.City(P.home)->state = TEXT("stricken");
		S.City(P.home)->severity = 3;
		const int32 Before = P.florins;
		MortalityPhase(S);
		const int32 Lost = Cfg(TEXT("start.family")) - FamilyTotal(P);
		Deaths += Lost;
		CHECK("the house inherits for each death", P.florins == Before + Lost * Cfg(TEXT("gains.inheritance")));
		CHECK("at least one survives", FamilyTotal(P) >= 1);
	}
	// Severity 3 kills on 1-3 of a d6: about half of 5 members = 2.5 per trial.
	CHECK("average deaths are about half the family", Deaths / 200.0 > 1.8 && Deaths / 200.0 < 3.2);
	// Last heir alone in a Devastating city always survives.
	for (int32 Seed = 1; Seed <= 100; Seed++)
	{
		FPortsState S = Game(Four(Seed));
		FPortsPlayer& P = S.players[0];
		P.family.Reset();
		P.family.Set(P.home, 1);
		S.City(P.home)->state = TEXT("stricken");
		S.City(P.home)->severity = 3;
		MortalityPhase(S);
		CHECK("the last heir survives", FamilyTotal(P) == 1);
	}
	return true;
}

PORTS_TEST(FPortsTestPreparing, "preparing improves survival odds")
{
	LOADED();
	const auto Trial = [](bool bPrepared)
	{
		int32 Dead = 0;
		for (int32 Seed = 1; Seed <= 300; Seed++)
		{
			FPortsState S = Game(Four(Seed));
			FPortsPlayer& P = S.players[0];
			S.City(P.home)->state = TEXT("stricken");
			S.City(P.home)->severity = 2;
			if (bPrepared) P.prepared.Add(P.home);
			MortalityPhase(S);
			Dead += Cfg(TEXT("start.family")) - FamilyTotal(P);
		}
		return Dead;
	};
	CHECK("fewer deaths when prepared", Trial(true) < Trial(false));
	return true;
}

PORTS_TEST(FPortsTestAftermath, "cities move from Stricken to Aftermath after the set number of rounds")
{
	LOADED();
	FPortsState S = Game(Four(2));
	StrikeCity(S, TEXT("paris"));
	for (int32 i = 0; i < Cfg(TEXT("plague.strickenRounds")) - 1; i++) AdvanceCities(S);
	CHECK("still stricken", S.City(TEXT("paris"))->state == TEXT("stricken"));
	AdvanceCities(S);
	CHECK("then aftermath", S.City(TEXT("paris"))->state == TEXT("aftermath"));
	return true;
}

PORTS_TEST(FPortsTestScoring, "scoring wealth, family, reputation and the balance bonus")
{
	LOADED();
	FPortsState S = Game(Four(1));
	FPortsPlayer& P = S.players[0];
	P.florins = 62;
	P.posts = { TEXT("venice"), TEXT("florence") };
	P.family.Reset();
	P.family.Set(TEXT("venice"), 4);
	P.reputation = 9;
	FPortsScore Sc = ScorePlayer(P);
	CHECK("wealth", Sc.wealth == 62 / Cfg(TEXT("scoring.florinsPerPoint")) + 2);
	CHECK("family", Sc.family == 12);
	CHECK("reputation", Sc.reputation == 9);
	CHECK("balance", Sc.balance == 9);
	CHECK("total", Sc.total == Sc.wealth + 12 + 9 + 9);
	// Reputation above the soft cap counts at a reduced rate.
	P.reputation = 16;
	Sc = ScorePlayer(P);
	CHECK("reputation above the soft cap", Sc.reputation == Cfg(TEXT("scoring.reputationSoftCap")) + (16 - Cfg(TEXT("scoring.reputationSoftCap"))) / Cfg(TEXT("scoring.reputationHighRate")));
	return true;
}

PORTS_TEST(FPortsTestWinning, "winning highest Legacy wins and ties are broken by reputation")
{
	LOADED();
	FPortsState S = Game(Four(1));
	for (FPortsPlayer& P : S.players)
	{
		P.florins = 0;
		P.posts = { P.home };
		P.family.Reset();
		P.family.Set(P.home, 1);
		P.reputation = 1;
	}
	FPortsPlayer& A = S.players[0];
	FPortsPlayer& B = S.players[1];
	A.florins = 3 * Cfg(TEXT("scoring.florinsPerPoint")); A.reputation = 5; // wealth 3+1, family 3, rep 5, balance 3 = 15
	B.florins = 2 * Cfg(TEXT("scoring.florinsPerPoint")); B.reputation = 6; // wealth 2+1, family 3, rep 6, balance 3 = 15
	CHECK("A scores 15", ScorePlayer(A).total == 15);
	CHECK("B scores 15", ScorePlayer(B).total == 15);
	const TArray<FPortsRank> Ranks = RankPlayers(S);
	CHECK("tie goes to higher reputation", Ranks[0].id == B.id);
	CHECK("A is second", Ranks[1].id == A.id && Ranks[1].place == 2);
	return true;
}

PORTS_TEST(FPortsTestFullGames, "full games finish with a winner, for 2 to 6 players, in both modes")
{
	LOADED();
	const TCHAR* Strategies[] = { TEXT("greedy"), TEXT("balanced"), TEXT("cautious"), TEXT("charitable") };
	const TCHAR* Homes[] = { TEXT("venice"), TEXT("london"), TEXT("lubeck"), TEXT("genoa"), TEXT("florence"), TEXT("bruges") };
	for (const TCHAR* Mode : { TEXT("standard"), TEXT("quick") })
	{
		for (int32 N = 2; N <= 6; N++)
		{
			for (int32 Seed = 1; Seed <= 6; Seed++)
			{
				FPortsSetup Setup;
				for (int32 i = 0; i < N; i++) Setup.players.Add(House(*FString::Printf(TEXT("P%d"), i), Homes[i], Strategies[(i + Seed) % 4]));
				Setup.seed = FString::FromInt(Seed);
				Setup.mode = Mode;
				Setup.prePlague = true;
				FPortsState S;
				const int32 Turns = PlayBotGame(Setup, S);
				CHECK("game ended", S.phase == TEXT("ended"));
				CHECK("ended in the last half-year", S.roundEnd == Cfg(TEXT("rounds")));
				const int32 Rounds = Cfg(*FString::Printf(TEXT("prePlague.rounds.%s"), Mode)) + Cfg(TEXT("rounds")) / Cfg(*FString::Printf(TEXT("modes.%s.span"), Mode));
				CHECK("every house took every turn", Turns == Rounds * N);
				CHECK("there is a winner", S.winner.IsSet() && S.winner->Num() >= 1);
				for (const FPortsPlayer& P : S.players) CHECK("no house is ever eliminated", FamilyTotal(P) >= 1);
			}
		}
	}
	return true;
}

PORTS_TEST(FPortsTestQuickPlay, "quick play 4 rounds of a year and a half, three plague rolls per round")
{
	LOADED();
	FPortsSetup Setup = Four(8);
	Setup.mode = TEXT("quick");
	FPortsState S = Game(Setup);
	Advance(S);
	CHECK("round 1", S.round == 1);
	CHECK("covers three half-years", S.roundEnd == 3);
	CHECK("four rounds", TotalRounds(S) == 4);
	CHECK("all three half-years are struck at once", Stricken(S) == ArrivedBy(3));
	CHECK("round label", RoundInfo(S).label == TEXT("Late 1347 \u2013 Late 1348"));
	ToActions(S);
	for (int32 i = 0; i < 4; i++)
	{
		ClearPending(S);
		const FPortsPlayer& P = *CurrentPlayer(S);
		CHECK("three action points", P.ap == Cfg(TEXT("modes.quick.actionPoints")) + (S.guildFavor.IsSet() && *S.guildFavor == P.id ? 1 : 0));
		EndTurn(S);
	}
	int32 Plagues = 0;
	for (const V& E : S.log) if (E.Get(TEXT("type")).AsString() == TEXT("plague") && E.Get(TEXT("round")).AsInt() == 1) Plagues++;
	CHECK("three plague phases in the round", Plagues == 3);
	return true;
}

PORTS_TEST(FPortsTestPrePlague, "pre-plague rounds 2 in Standard, 1 in Quick Play, trade only")
{
	LOADED();
	struct FCase { const TCHAR* Mode; int32 N; };
	for (const FCase& Case : { FCase{ TEXT("standard"), 2 }, FCase{ TEXT("quick"), 1 } })
	{
		FPortsSetup Setup = Four(5);
		Setup.mode = Case.Mode;
		Setup.prePlague = true;
		FPortsState S = Game(Setup);
		CHECK("total rounds", TotalRounds(S) == Case.N + Cfg(TEXT("rounds")) / Cfg(*FString::Printf(TEXT("modes.%s.span"), Case.Mode)));
		ToActions(S);
		CHECK("before the plague", IsPrePlague(S));
		CHECK("round number 1", RoundNumber(S) == 1);
		CHECK("no Event card before the plague", S.currentEvent.IsEmpty());
		CHECK("round info says pre", RoundInfo(S).pre);
		CHECK("posts cost less", Cost(S, TEXT("openPost")) == Cfg(TEXT("costs.openPost")) - Cfg(TEXT("prePlague.postDiscount")));
		CHECK("only the Black Sea is stricken before the plague sails", Stricken(S) == TArray<FString>({ TEXT("caffa"), TEXT("tana") }));
		CurrentPlayer(S)->family.Set(TEXT("caffa"), 2); // even family in Caffa does not roll before the game proper
		for (int32 i = 0; i < Case.N; i++)
		{
			while (S.phase == TEXT("actions")) { ClearPending(S); EndTurn(S); }
			CHECK("no survival rolls", CountLog(S, TEXT("mortality")) == 0);
			CHECK("cities do not age before the plague", S.City(TEXT("caffa"))->state == TEXT("stricken"));
			if (i < Case.N - 1) ToActions(S);
		}
		Advance(S);
		CHECK("the plague years follow", S.round == 1);
		CHECK("no longer pre-plague", !IsPrePlague(S));
		CHECK("round number continues", RoundNumber(S) == Case.N + 1);
		CHECK("posts cost the full price", Cost(S, TEXT("openPost")) == Cfg(TEXT("costs.openPost")));
	}
	FPortsState Off = Game(Four(5));
	Advance(Off);
	CHECK("without pre-plague the game starts in round 1", Off.round == 1);
	CHECK("twelve rounds", TotalRounds(Off) == Cfg(TEXT("rounds")));
	return true;
}

PORTS_TEST(FPortsTestTimer, "turn timer time up declines open cards and passes the turn")
{
	LOADED();
	FPortsSetup Setup = Four(9);
	Setup.timer = true;
	FPortsState S = Game(Setup);
	ToActions(S);
	CHECK("timer seconds", S.turnSeconds == Cfg(TEXT("turnTimer.seconds")));
	FPortsPlayer& P = *CurrentPlayer(S);
	const int32 Id = P.id;
	P.pending.Add(V::Object({ { TEXT("kind"), TEXT("offer") }, { TEXT("offer"), TEXT("test") }, { TEXT("label"), TEXT("Buy it") }, { TEXT("decline"), TEXT("Walk away") },
		{ TEXT("cost"), V::Object({ { TEXT("florins"), 2 } }) }, { TEXT("gain"), V::Object({ { TEXT("reputation"), 1 } }) }, { TEXT("card"), TEXT("EV-wages") } }));
	const int32 Florins = P.florins;
	CHECK("time up succeeds", TimeUp(S).ok);
	CHECK("no cards left waiting", P.pending.Num() == 0);
	CHECK("the offer was declined", P.florins == Florins);
	CHECK("the turn passed on", !CurrentPlayer(S) || CurrentPlayer(S)->id != Id);
	CHECK("time up is in the log", S.log.ContainsByPredicate([Id](const V& E) { return E.Get(TEXT("type")).AsString() == TEXT("timeUp") && E.Get(TEXT("player")).AsInt() == Id; }));
	CHECK("timer off by default in the engine", Game(Four(9)).turnSeconds == 0);
	return true;
}

PORTS_TEST(FPortsTestMortalityDifficulty, "Great Mortality raises severity and contagion")
{
	LOADED();
	for (int32 Seed = 1; Seed <= 30; Seed++)
	{
		FPortsSetup Setup = Four(Seed);
		Setup.difficulty = TEXT("mortality");
		FPortsState S = Game(Setup);
		Advance(S);
		CHECK("no Light outbreaks on Great Mortality", S.City(TEXT("constantinople"))->severity >= 2);
	}
	FPortsSetup Setup = Four(3);
	Setup.difficulty = TEXT("mortality");
	FPortsState S = Game(Setup);
	ToActions(S);
	FPortsPlayer& P = ClearPending(S);
	P.posts.Add(TEXT("messina"));
	CHECK("contagion risk is one higher", ShipQuote(S, P, TEXT("messina-genoa"), TEXT("messina")).contagionRisk == FMath::Min(6, S.City(TEXT("messina"))->severity + 1));
	return true;
}

PORTS_TEST(FPortsTestFortuneDraw, "fortune cards rolling a 6 or opening a post draws a personal card")
{
	LOADED();
	int32 Drawn = 0;
	for (int32 Seed = 1; Seed <= 40; Seed++)
	{
		FPortsState S = Game(Four(Seed));
		ToActions(S);
		FPortsPlayer& P = ClearPending(S);
		const TArray<FPortsAction> Ships = LegalShipments(S, P);
		if (Ships.Num() == 0) continue;
		const FPortsResult R = PerformAction(S, Ships[0]);
		const int32 Fortunes = CountLog(S, TEXT("fortune"));
		if (R.entry.Get(TEXT("profitDie")).AsInt() == 6) { CHECK("a 6 draws one card", Fortunes == 1); Drawn++; }
		else CHECK("no card otherwise", Fortunes == 0);
		for (const V& E : S.log) if (E.Get(TEXT("type")).AsString() == TEXT("fortune")) CHECK("only the active house draws", E.Get(TEXT("player")).AsInt() == P.id);
	}
	CHECK("some cards were drawn", Drawn > 0);
	CHECK("at least 20 fortune cards", FPortsData::Get().Fortune().Num() >= 20);
	return true;
}

PORTS_TEST(FPortsTestFortuneFree, "fortune free post costs nothing and clean hold skips contagion")
{
	LOADED();
	FPortsState S = Game(Four(12));
	ToActions(S);
	FPortsPlayer& P = ClearPending(S);
	P.free.Set(TEXT("post"), true);
	const int32 Ap = P.ap;
	const TArray<FPortsAction> Posts = LegalPosts(S, P);
	if (!CHECK("there is a city to open a post in", Posts.Num() > 0)) return false;
	CHECK("free post opens", PerformAction(S, Posts[0]).ok);
	while (P.pending.Num()) Decide(S, EPortsChoice::No);
	CHECK("no action point spent", P.ap == Ap);
	P.posts.Add(TEXT("messina"));
	P.nextShip = FPortsNextShip{ 0, true };
	S.effects.contagion.all = 6;
	const FPortsResult R = PerformAction(S, Ship(TEXT("messina"), TEXT("messina-genoa")));
	CHECK("no contagion die", R.entry.Get(TEXT("contagionDie")).IsNull());
	CHECK("not infected", !R.entry.Get(TEXT("infected")).AsBool());
	return true;
}

PORTS_TEST(FPortsTestFortuneNextShip, "fortune a next-shipment bonus or penalty changes one shipment's profit, then is used up")
{
	LOADED();
	struct FCase { const TCHAR* Id; int32 Change; };
	for (const FCase& Case : { FCase{ TEXT("FO-galley"), 3 }, FCase{ TEXT("FO-held"), -2 } })
	{
		CHECK("the card's number", FortuneById(Case.Id).Get(TEXT("effect")).Get(TEXT("profit")).AsInt() == Case.Change);
		FPortsState S = Game(Four(12));
		ToActions(S);
		FPortsPlayer& P = ClearPending(S);
		const FPortsAction First = LegalShipments(S, P)[0];
		const FPortsShipQuote Plain = ShipQuote(S, P, First.route, First.from);
		P.nextShip = FPortsNextShip{ Case.Change, false };
		const FPortsShipQuote Q = ShipQuote(S, P, First.route, First.from);
		CHECK("the quote includes the change", Q.fixed == Plain.fixed + Case.Change);
		CHECK("the last line is the Fortune card", Q.parts[Q.parts.Num() - 1] == V::Object({ { TEXT("label"), TEXT("Fortune card") }, { TEXT("value"), Case.Change } }));
		const FPortsResult R = PerformAction(S, First);
		CHECK("the profit includes the change", R.entry.Get(TEXT("profit")).AsInt() == FMath::Max(0, Plain.fixed + Case.Change + R.entry.Get(TEXT("profitDie")).AsInt()));
		CHECK("the bonus is used up", !P.nextShip.IsSet());
		CHECK("the next quote is plain again", ShipQuote(S, P, First.route, First.from).fixed == Plain.fixed);
	}
	return true;
}

PORTS_TEST(FPortsTestCharityLimit, "charity the refusal names the real limit, in Standard and in Quick Play")
{
	LOADED();
	struct FCase { const TCHAR* Mode; int32 Limit; const TCHAR* Text; };
	for (const FCase& Case : { FCase{ TEXT("standard"), 1, TEXT("already given charity this turn") }, FCase{ TEXT("quick"), 3, TEXT("already given charity 3 times this turn") } })
	{
		FPortsSetup Setup = Four(12);
		Setup.mode = Case.Mode;
		FPortsState S = Game(Setup);
		ToActions(S);
		FPortsPlayer& P = ClearPending(S);
		CHECK("the limit", Cfg(TEXT("limits.charityPerTurn")) * S.span == Case.Limit);
		P.charityThisTurn = Case.Limit - 1;
		CHECK("one below the limit is not refused for that", !CheckAction(S, Charity(TEXT("church"))).Contains(TEXT("already given")));
		P.charityThisTurn = Case.Limit;
		CHECK("at the limit the refusal names it", CheckAction(S, Charity(TEXT("church"))).Contains(Case.Text));
	}
	return true;
}

PORTS_TEST(FPortsTestPersecution, "persecution protecting costs money and a turn action")
{
	LOADED();
	FPortsState S = Game(Four(9));
	while (S.round < 4 || S.phase != TEXT("event"))
	{
		Advance(S);
		if (S.phase == TEXT("actions")) for (int32 i = 0; i < 4; i++) { ClearPending(S); EndTurn(S); }
	}
	CHECK("unrest in Strasbourg", S.City(TEXT("strasbourg"))->unrest > 0);
	Advance(S); // to actions
	FPortsPlayer& P = *CurrentPlayer(S);
	const int32 Florins = P.florins, Rep = P.reputation;
	while (P.pending.Num() && P.pending[0].Get(TEXT("kind")).AsString() != TEXT("protect")) Decide(S, P.pending[0].Get(TEXT("kind")).AsString() == TEXT("wageLaw") ? EPortsChoice::Pay : EPortsChoice::No);
	const int32 Ap = P.ap;
	CHECK("protecting is accepted", Decide(S, EPortsChoice::Yes).ok);
	CHECK("it takes an action point", P.ap == Ap - 1);
	CHECK("it costs florins", P.florins <= Florins - Cfg(TEXT("costs.protectCommunity")));
	CHECK("it raises reputation", P.reputation > Rep);
	CHECK("the Strasbourg card is the persecution card", CardById(TEXT("CHR-strasbourg")).Get(TEXT("effect")).Get(TEXT("type")).AsString() == TEXT("persecution"));
	return true;
}

PORTS_TEST(FPortsTestWageLaw, "wage law obeying blocks shipping from English posts this round")
{
	LOADED();
	FPortsState S = Game(Four(4));
	ToActions(S);
	FPortsPlayer& P = ClearPending(S);
	P.posts.Add(TEXT("london"));
	P.pending.Add(V::Object({ { TEXT("kind"), TEXT("wageLaw") }, { TEXT("card"), TEXT("CHR-ordinance") }, { TEXT("cities"), V::Array({ V(TEXT("london")), V(TEXT("melcombe")) }) } }));
	CHECK("obeying is accepted", Decide(S, EPortsChoice::Obey).ok);
	CHECK("English posts cannot ship", CheckAction(S, Ship(TEXT("london"), TEXT("london-bruges"))).Contains(TEXT("wage law")));
	return true;
}

PORTS_TEST(FPortsTestQuack, "remedy seller buying the cure has no benefit")
{
	LOADED();
	FPortsState S = Game(Four(6));
	ToActions(S);
	FPortsPlayer& P = ClearPending(S);
	P.florins = 20;
	ApplyCard(S, CardById(TEXT("EV-quack")));
	const int32 Rep = P.reputation, Family = FamilyTotal(P);
	CHECK("buying is accepted", Decide(S, EPortsChoice::Yes).ok);
	CHECK("3 florins are gone", P.florins == 17);
	CHECK("reputation unchanged", P.reputation == Rep);
	CHECK("family unchanged", FamilyTotal(P) == Family);
	return true;
}

PORTS_TEST(FPortsTestSaveRestore, "save and restore a saved game continues identically")
{
	LOADED();
	FPortsState A = Game(Four(42));
	ToActions(A);
	const FString Saved = A.ToJson();
	FPortsState B;
	if (!CHECK("the saved game loads", FPortsState::FromJson(Saved, B))) return false;
	CHECK("loading and saving again gives the same text", B.ToJson() == Saved);
	const auto Run = [](FPortsState& S)
	{
		ClearPending(S);
		const TArray<FPortsAction> Ships = LegalShipments(S, *CurrentPlayer(S));
		for (int32 i = 0; i < FMath::Min(2, Ships.Num()); i++) PerformAction(S, Ships[i]);
		return S.ToJson();
	};
	CHECK("both continue identically", Run(A) == Run(B));
	return true;
}

// ---------- Merchant's Ledger and Hold Offshore ----------

PORTS_TEST(FPortsTestOffshore, "hold offshore infected cargo costs no reputation and does not spread")
{
	LOADED();
	FPortsState S = Game(Four(21));
	ToActions(S);
	FPortsPlayer& P = ClearPending(S);
	P.posts.Add(TEXT("messina"));
	S.City(TEXT("messina"))->severity = 3;
	S.effects.contagion.all = 6; // guarantee infection
	P.florins = 20;
	const int32 Rep = P.reputation;
	const FPortsResult R = PerformAction(S, Ship(TEXT("messina"), TEXT("messina-genoa"), true));
	CHECK("infected shipment", R.ok && R.entry.Get(TEXT("infected")).AsBool());
	CHECK("the plague did not spread", S.City(TEXT("genoa"))->state == TEXT("safe"));
	CHECK("no reputation lost", P.reputation == Rep);
	CHECK("fee paid, profit earned", P.florins == 20 - Cfg(TEXT("costs.holdOffshore")) + R.entry.Get(TEXT("profit")).AsInt());
	const FString FirstRoute = FPortsData::Get().Routes[FPortsData::Get().RoutesFrom(P.home)[0]].Id;
	CHECK("offshore only from a stricken city", CheckAction(S, Ship(P.home, FirstRoute, true)).Contains(TEXT("Stricken city")));
	return true;
}

PORTS_TEST(FPortsTestMarriage, "arrange a marriage only in Aftermath, and never above the starting family")
{
	LOADED();
	FPortsState S = Game(Four(4));
	ToActions(S);
	FPortsPlayer& P = ClearPending(S);
	P.florins = 20;
	P.family.Set(P.home, 3);
	CHECK("not before Aftermath", CheckAction(S, Act(TEXT("marry"), P.home)).Contains(TEXT("not in Aftermath")));
	S.City(P.home)->state = TEXT("aftermath");
	const int32 Ap = P.ap;
	CHECK("wedding succeeds", PerformAction(S, Act(TEXT("marry"), P.home)).ok);
	CHECK("one more family member", P.family.Get(P.home) == 4);
	CHECK("wedding paid", P.florins == 20 - Cfg(TEXT("costs.marriage")));
	CHECK("one action point", P.ap == Ap - 1);
	CHECK("one wedding per turn", CheckAction(S, Act(TEXT("marry"), P.home)).Contains(TEXT("already arranged a marriage")));
	P.family.Set(P.home, Cfg(TEXT("start.family")));
	P.marriedThisTurn = 0;
	CHECK("never above the starting family", CheckAction(S, Act(TEXT("marry"), P.home)).Contains(TEXT("already has 5 family")));
	return true;
}

PORTS_TEST(FPortsTestLand, "abandoned land adds Wealth points and charges wages each half-year")
{
	LOADED();
	for (const bool bBroke : { false, true })
	{
		FPortsState S = Game(Four(6));
		ToActions(S);
		FPortsPlayer& P = ClearPending(S);
		S.City(P.home)->state = TEXT("aftermath");
		P.florins = 20;
		CHECK("land bought", PerformAction(S, Act(TEXT("land"), P.home)).ok);
		CHECK("land adds Wealth", ScorePlayer(P).wealth == P.florins / Cfg(TEXT("scoring.florinsPerPoint")) + P.posts.Num() + Cfg(TEXT("scoring.pointsPerLand")));
		CHECK("one holding per city", CheckAction(S, Act(TEXT("land"), P.home)).Contains(TEXT("already own land")));
		const int32 Family = FamilyTotal(P);
		P.family.Reset();
		P.family.Set(ESTATE, Family); // keep inheritance out of the sums
		P.florins = bBroke ? 0 : 10;
		const int32 Rep = P.reputation;
		FinishRound(S);
		CHECK("wages paid if possible", P.florins == (bBroke ? 0 : 10 - Cfg(TEXT("costs.landWage"))));
		CHECK("unpaid wages cost reputation", P.reputation == (bBroke ? Rep - 1 : Rep));
	}
	return true;
}

PORTS_TEST(FPortsTestLoan, "loan no action point, repaid next round, default costs reputation, settled at the end")
{
	LOADED();
	FPortsState S = Game(Four(9));
	ToActions(S);
	FPortsPlayer& P = ClearPending(S);
	const int32 Id = P.id;
	const int32 F = P.florins, Ap = P.ap;
	CHECK("loan taken", PerformAction(S, Act(TEXT("loan"))).ok);
	CHECK("florins lent", P.florins == F + Cfg(TEXT("gains.loan")));
	CHECK("no action point", P.ap == Ap);
	CHECK("one loan at a time", CheckAction(S, Act(TEXT("loan"))).Contains(TEXT("already owe")));
	int32 Family = FamilyTotal(P);
	P.family.Reset(); P.family.Set(ESTATE, Family);
	FinishRound(S);
	CHECK("not due yet after the first round", P.loan.IsSet());
	NextTurnOf(S, Id);
	Family = FamilyTotal(P);
	P.family.Reset(); P.family.Set(ESTATE, Family);
	P.florins = 3;
	P.reputation = 10;
	FinishRound(S);
	CHECK("loan settled", !P.loan.IsSet());
	CHECK("all florins taken", P.florins == 0);
	CHECK("default costs reputation", P.reputation == 10 - Cfg(TEXT("penalties.loanDefaultReputation")));

	S.round = S.roundEnd = Cfg(TEXT("rounds"));
	S.phase = TEXT("actions");
	S.turn = S.order.IndexOfByKey(Id);
	CHECK("no loans in the final round", CheckAction(S, Act(TEXT("loan"))).Contains(TEXT("final round")));
	P.loan = FPortsLoan{ Cfg(TEXT("costs.loanRepay")), 99 };
	P.florins = 20;
	S.phase = TEXT("plague");
	Advance(S);
	CHECK("game ended", S.phase == TEXT("ended"));
	CHECK("open debts are paid before final scoring", P.florins == 20 - Cfg(TEXT("costs.loanRepay")));
	return true;
}

PORTS_TEST(FPortsTestPartnership, "partnership the other house decides, both earn on shared cities, then it ends")
{
	LOADED();
	FPortsState S = Game(Four(12));
	ToActions(S);
	FPortsPlayer& A = ClearPending(S);
	FPortsPlayer& B = S.players[S.order[1]];
	CHECK("proposal made", PerformAction(S, Deal(B.id)).ok);
	CHECK("one proposal per turn", CheckAction(S, Deal(S.order[2])).Contains(TEXT("already proposed")));
	CHECK("the other house has the offer", B.pending.ContainsByPredicate([](const V& D) { return D.Get(TEXT("kind")).AsString() == TEXT("deal"); }));
	EndTurn(S);
	while (B.pending.Num())
	{
		const FString Kind = B.pending[0].Get(TEXT("kind")).AsString();
		Decide(S, Kind == TEXT("deal") ? EPortsChoice::Yes : Kind == TEXT("wageLaw") ? EPortsChoice::Pay : EPortsChoice::No);
	}
	if (!CHECK("both are partners", A.deal.IsSet() && B.deal.IsSet() && A.deal->partner == B.id && B.deal->partner == A.id)) return false;
	const FPortsRoute& Route = FPortsData::Get().Routes[FPortsData::Get().RoutesFrom(B.home)[0]];
	const FString Dest = OtherEnd(Route, B.home);
	A.posts.Add(Dest);
	const FPortsShipQuote Q = ShipQuote(S, B, Route.Id, B.home);
	CHECK("the shipper earns the partner bonus", Q.parts.GetItems().ContainsByPredicate([](const V& X) { return X.Get(TEXT("label")).AsString().StartsWith(TEXT("Partner")) && X.Get(TEXT("value")).AsInt() == Cfg(TEXT("gains.dealShipperBonus")); }));
	const int32 Af = A.florins;
	CHECK("shipment succeeds", PerformAction(S, Ship(B.home, Route.Id)).ok);
	CHECK("the partner earns too", A.florins == Af + Cfg(TEXT("gains.dealBonus")));
	FinishRound(S);
	CHECK("still partners after the first round", A.deal.IsSet());
	ToActions(S);
	FinishRound(S);
	CHECK("the partnership has ended", !A.deal.IsSet() && !B.deal.IsSet());
	return true;
}

PORTS_TEST(FPortsTestGates, "close your gates rivals cannot open a post there and earn less shipping in")
{
	LOADED();
	FPortsState S = Game(Four(14));
	ToActions(S);
	FPortsPlayer& A = ClearPending(S);
	const int32 Rep = A.reputation;
	CHECK("gates closed", PerformAction(S, Act(TEXT("gates"), A.home)).ok);
	CHECK("it costs reputation", A.reputation == Rep - Cfg(TEXT("penalties.gatesReputation")));
	EndTurn(S);
	FPortsPlayer& B = ClearPending(S);
	const FPortsRoute& Route = FPortsData::Get().Routes[FPortsData::Get().RoutesFrom(A.home)[0]];
	const FString Near = OtherEnd(Route, A.home);
	B.posts.AddUnique(Near);
	B.florins = 30;
	CHECK("rivals cannot open a post", CheckAction(S, Act(TEXT("post"), A.home)).Contains(TEXT("closed the gates")));
	const FPortsShipQuote Q = ShipQuote(S, B, Route.Id, Near);
	CHECK("rivals earn less shipping in", Q.parts.GetItems().ContainsByPredicate([](const V& X) { return X.Get(TEXT("label")).AsString().StartsWith(TEXT("Gates closed")) && X.Get(TEXT("value")).AsInt() == -Cfg(TEXT("penalties.gatesProfit")); }));
	FinishRound(S);
	CHECK("still closed after the first round", A.gates.IsSet());
	ToActions(S);
	FinishRound(S);
	CHECK("the gates open again after the next round", !A.gates.IsSet());
	return true;
}

// ---------- tests/bots.test.js ----------

PORTS_TEST(FPortsTestBotsSetup, "bots one person can play against bots, but not zero people")
{
	LOADED();
	CHECK("one person and a bot is fine", ValidateSetup({ House(TEXT("Ada"), TEXT("venice")), Bot(TEXT("Bo"), TEXT("london"), TEXT("easy")) }).IsEmpty());
	CHECK("all bots is refused", ValidateSetup({ Bot(TEXT("A"), TEXT("venice"), TEXT("hard")), Bot(TEXT("B"), TEXT("london"), TEXT("hard")) }).Contains(TEXT("person")));
	CHECK("an unknown skill is refused", ValidateSetup({ House(TEXT("Ada"), TEXT("venice")), Bot(TEXT("Bo"), TEXT("london"), TEXT("expert")) }).Contains(TEXT("skill")));
	return true;
}

PORTS_TEST(FPortsTestBotsStyle, "bots each bot gets a playing style from its skill level")
{
	LOADED();
	FPortsSetup Setup;
	Setup.seed = TEXT("3");
	Setup.players = { House(TEXT("Ada"), TEXT("venice")), Bot(TEXT("E"), TEXT("london"), TEXT("easy")), Bot(TEXT("M"), TEXT("genoa"), TEXT("medium")), Bot(TEXT("H"), TEXT("bruges"), TEXT("hard")) };
	const FPortsState S = Game(Setup);
	CHECK("the person is not a bot and has no style", !S.players[0].bot && S.players[0].strategy.IsEmpty());
	for (int32 i = 1; i < 4; i++)
	{
		const FPortsPlayer& P = S.players[i];
		CHECK("a bot's style comes from its skill's list", P.bot && FPortsData::Get().Config().Get(TEXT("bots")).Get(TEXT("skills")).Get(P.skill).Get(TEXT("strategies")).ContainsString(P.strategy));
	}
	CHECK("hard bots are balanced", S.players[3].strategy == TEXT("balanced"));
	CHECK("hard bots never slip", MistakeRate(S.players[3]) == 0);
	CHECK("easy bots slip more than medium", MistakeRate(S.players[1]) > MistakeRate(S.players[2]));
	return true;
}

PORTS_TEST(FPortsTestBotsTurn, "bots a bot turn is a series of legal moves that ends")
{
	LOADED();
	for (const TCHAR* Skill : { TEXT("easy"), TEXT("medium"), TEXT("hard") })
	{
		FPortsSetup Setup;
		Setup.seed = FString::Printf(TEXT("turn-%s"), Skill);
		Setup.players = { Bot(TEXT("B"), TEXT("genoa"), Skill), House(TEXT("Ada"), TEXT("venice")) };
		FPortsState S = Game(Setup);
		ToActions(S);
		// Make the bot go first whatever the dice said.
		S.order = { 0, 1 };
		FPortsPlayer& P = *CurrentPlayer(S);
		for (int32 Moves = 0;; Moves++)
		{
			const FPortsBotMove Move = BotMove(S);
			if (Move.type == FPortsBotMove::EType::End) break;
			if (Move.type == FPortsBotMove::EType::Decide) CHECK("the bot's answer is accepted", BotDecide(S, Move.choice).ok);
			else CHECK("the bot's action is legal", PerformAction(S, Move.action).ok);
			if (!CHECK("the turn must end", Moves < 40)) return false;
		}
		CHECK("no cards left waiting", P.pending.Num() == 0);
	}
	return true;
}

PORTS_TEST(FPortsTestBotsSolo, "bots a one-person game against bots of every skill plays to the end")
{
	LOADED();
	FPortsSetup Setup;
	Setup.seed = TEXT("solo");
	Setup.mode = TEXT("quick");
	Setup.prePlague = true;
	Setup.players = { House(TEXT("Ada"), TEXT("venice")), Bot(TEXT("E"), TEXT("london"), TEXT("easy")), Bot(TEXT("M"), TEXT("genoa"), TEXT("medium")), Bot(TEXT("H"), TEXT("bruges"), TEXT("hard")) };
	FPortsState S;
	PlayBotGame(Setup, S);
	CHECK("game ended", S.phase == TEXT("ended"));
	CHECK("there is a winner", S.winner.IsSet() && S.winner->Num() >= 1);
	return true;
}

PORTS_TEST(FPortsTestBotsSkill, "bots harder bots win more often")
{
	LOADED();
	TMap<FString, double> Wins;
	const TCHAR* Homes[] = { TEXT("venice"), TEXT("london"), TEXT("genoa"), TEXT("bruges") };
	const TCHAR* Skills[] = { TEXT("easy"), TEXT("medium"), TEXT("hard") };
	for (int32 G = 0; G < 60; G++)
	{
		// The person's seat is played by the simulator (at random); only the bots are counted.
		FPortsSetup Setup;
		Setup.players.Add(House(TEXT("Ada"), Homes[3], TEXT("random")));
		for (int32 i = 0; i < 3; i++) Setup.players.Add(Bot(Skills[i], Homes[(i + G) % 3], Skills[i]));
		Setup.seed = FString::Printf(TEXT("skill-%d"), G);
		Setup.mode = G % 2 ? TEXT("quick") : TEXT("standard");
		Setup.prePlague = true;
		FPortsState S;
		PlayBotGame(Setup, S);
		if (!S.winner.IsSet()) continue;
		for (const int32 Id : *S.winner) if (S.players[Id].bot) Wins.FindOrAdd(S.players[Id].skill) += 1.0 / S.winner->Num();
	}
	CHECK("hard beats medium beats easy", Wins.FindRef(TEXT("hard")) > Wins.FindRef(TEXT("medium")) && Wins.FindRef(TEXT("medium")) > Wins.FindRef(TEXT("easy")));
	return true;
}

#undef PORTS_TEST
#undef CHECK
#undef LOADED

#endif
