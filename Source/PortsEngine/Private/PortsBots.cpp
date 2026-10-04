// Computer players: the bot houses in a game (see config.json "bots") and the
// strategies used by the balance simulator. BotMove() chooses ONE move at a
// time (answer a card, take an action, or end the turn). Skill levels: an
// easier bot sometimes makes a "mistake" (a random legal move) and plays with
// a simpler personality; a Hard bot plays the strongest strategy and never slips.
#include "PortsBots.h"

// The bots weigh moves with fractions. To choose the same moves as the web
// version, every sum must round exactly as JavaScript rounds it: do not let
// the compiler merge a multiply and an add into one operation.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace Ports
{
	namespace
	{
		using FMaybeAction = TOptional<FPortsAction>;

		FPortsAction Make(const TCHAR* Type) { FPortsAction A; A.type = Type; return A; }
		FPortsAction MakeCity(const TCHAR* Type, const FString& City) { FPortsAction A; A.type = Type; A.city = City; return A; }
		FPortsAction MakeMove(const FString& From, const FString& To, int32 Count) { FPortsAction A; A.type = TEXT("move"); A.from = From; A.to = To; A.count = Count; return A; }
		FPortsAction MakeCharity(const TCHAR* Kind) { FPortsAction A; A.type = TEXT("charity"); A.kind = Kind; return A; }
		FPortsAction MakeDeal(int32 Partner) { FPortsAction A; A.type = TEXT("deal"); A.partner = Partner; return A; }

		struct FShipEstimate { double Value = 0; double RepCost = 0; };

		FShipEstimate ExpectedShip(const FPortsState& State, const FPortsPlayer& P, const FPortsAction& S)
		{
			const FPortsData& D = FPortsData::Get();
			const FPortsShipQuote Q = ShipQuote(State, P, S.route, S.from, S.offshore);
			const double Mean = Q.fixed + (D.Number(TEXT("shipping.profitDie")) + 1) / 2;
			const double PInf = Q.contagionRisk / 6.0;
			FShipEstimate E;
			E.RepCost = S.offshore ? 0.0 : PInf * D.Number(TEXT("penalties.infectedCargoReputation"));
			E.Value = Mean * (1 - PInf * 0.5) - Q.fee;
			return E;
		}

		// Legal shipments, plus the "hold offshore" version of each one from a Stricken city.
		TArray<FPortsAction> ShipOptions(const FPortsState& State, const FPortsPlayer& P)
		{
			TArray<FPortsAction> Out;
			for (const FPortsAction& S : LegalShipments(State, P))
			{
				Out.Add(S);
				FPortsAction Held = S;
				Held.offshore = true;
				if (CheckAction(State, Held).IsEmpty()) Out.Add(Held);
			}
			return Out;
		}

		FMaybeAction BestShipment(const FPortsState& State, const FPortsPlayer& P, bool bAvoidRisk = false)
		{
			FMaybeAction Best;
			double BestScore = 0;
			for (const FPortsAction& S : ShipOptions(State, P))
			{
				const FShipEstimate E = ExpectedShip(State, P, S);
				const double Score = E.Value - (bAvoidRisk ? E.RepCost * 4 : E.RepCost);
				if (!Best.IsSet() || Score > BestScore) { Best = S; BestScore = Score; }
			}
			return Best;
		}

		double DangerAt(const FPortsState& State, const FString& Loc)
		{
			if (Loc == ESTATE) return 0;
			if (IsStricken(State, Loc)) return State.City(Loc)->severity;
			if (IsThreatened(State, Loc)) return 0.7;
			return 0;
		}

		// Which of Wealth, Family and Reputation is lowest (the first of them on a tie).
		const TCHAR* LowestPart(const FPortsPlayer& P)
		{
			const FPortsScore S = ScorePlayer(P);
			if (S.wealth <= S.family && S.wealth <= S.reputation) return TEXT("wealth");
			return S.family <= S.reputation ? TEXT("family") : TEXT("reputation");
		}
		bool LowestIsReputation(const FPortsPlayer& P) { return FCString::Strcmp(LowestPart(P), TEXT("reputation")) == 0; }

		// The answer a bot gives to the first card waiting for it.
		EPortsChoice ChooseDecision(FPortsState& State, FPortsPlayer& P, const FString& Strategy)
		{
			const FPortsValue D = P.pending[0];
			const FString Kind = D.Get(TEXT("kind")).AsString();
			const auto YesIfAble = [&](bool bWant) { return bWant && CanAccept(State, P, D).IsEmpty() ? EPortsChoice::Yes : EPortsChoice::No; };
			if (Kind == TEXT("wageLaw"))
			{
				if (Strategy == TEXT("random")) return NextRandom(State) < 0.5 ? EPortsChoice::Obey : EPortsChoice::Pay;
				return Strategy == TEXT("charitable") || (Strategy == TEXT("balanced") && LowestIsReputation(P)) ? EPortsChoice::Obey : EPortsChoice::Pay;
			}
			if (Kind == TEXT("deal")) return YesIfAble(Strategy == TEXT("random") ? NextRandom(State) < 0.5 : true);
			if (Kind == TEXT("protect"))
			{
				// The web version works out every style's answer, including the coin toss, whichever style is playing.
				const bool bBalanced = LowestIsReputation(P) || P.florins > 15;
				const bool bRandom = NextRandom(State) < 0.5;
				const bool bWant = Strategy == TEXT("charitable") ? true : Strategy == TEXT("balanced") ? bBalanced : Strategy == TEXT("random") ? bRandom : false;
				return YesIfAble(bWant);
			}
			const bool bTrap = D.Get(TEXT("reveal")).Truthy(); // a careful player knows medieval "cures" did nothing
			const int32 Rep = D.Get(TEXT("gain")).Get(TEXT("reputation")).AsInt(0);
			const int32 Price = D.Get(TEXT("cost")).Get(TEXT("florins")).AsInt(0);
			bool bWant;
			if (Strategy == TEXT("random")) bWant = NextRandom(State) < 0.5;
			else if (bTrap) bWant = false;
			else if (Strategy == TEXT("greedy")) bWant = false;
			else if (Strategy == TEXT("charitable")) bWant = Rep > 0;
			else if (Strategy == TEXT("balanced")) bWant = Rep > 0 && (LowestIsReputation(P) || Price <= 2) && P.florins > Price + 4;
			else bWant = Rep >= 2 && P.florins > Price + 6;
			// Informed players remember that the Pope banned the flagellants.
			if (D.Get(TEXT("offer")).AsString() == TEXT("flagellantAlms") && Strategy != TEXT("random") && Strategy != TEXT("charitable")) bWant = false;
			return YesIfAble(bWant);
		}

		// The action itself if it is allowed right now, otherwise nothing.
		FMaybeAction Legal(const FPortsState& State, const FMaybeAction& Action)
		{
			return Action.IsSet() && CheckAction(State, *Action).IsEmpty() ? Action : FMaybeAction();
		}

		// Protect family: move people out of danger or prepare them.
		FMaybeAction ProtectFamily(const FPortsState& State, const FPortsPlayer& P, bool bWillFlee, double Threshold)
		{
			bool bHave = false;
			FString WorstLoc;
			double WorstDanger = 0;
			for (const FString& Loc : FamilyLocations(P))
			{
				const double Danger = DangerAt(State, Loc);
				if (Danger >= Threshold && (!bHave || Danger > WorstDanger)) { bHave = true; WorstLoc = Loc; WorstDanger = Danger; }
			}
			if (!bHave) return {};
			const int32 N = FMath::Min(FPortsData::Get().Int(TEXT("limits.moveFamilyMax")), FamilyAt(P, WorstLoc));
			// Prefer a safe post (no reputation loss if the city is only threatened), else the estate.
			const FString* SafePost = P.posts.FindByPredicate([&](const FString& C) { return C != WorstLoc && DangerAt(State, C) == 0; });
			const FMaybeAction Flee = bWillFlee ? Legal(State, MakeMove(WorstLoc, SafePost ? *SafePost : FString(ESTATE), N)) : FMaybeAction();
			if (Flee.IsSet()) return Flee;
			return IsStricken(State, WorstLoc) ? Legal(State, MakeCity(TEXT("prepare"), WorstLoc)) : FMaybeAction();
		}

		FMaybeAction ReturnFamily(const FPortsState& State, const FPortsPlayer& P)
		{
			if (FamilyAt(P, ESTATE) == 0) return {};
			const FString* Safe = P.posts.FindByPredicate([&](const FString& C) { return DangerAt(State, C) == 0; });
			if (!Safe) return {};
			return Legal(State, MakeMove(ESTATE, *Safe, FMath::Min(FPortsData::Get().Int(TEXT("limits.moveFamilyMax")), FamilyAt(P, ESTATE))));
		}

		double RoutesValue(const FString& City)
		{
			const FPortsData& D = FPortsData::Get();
			const TArray<int32>& Routes = D.RoutesFrom(City);
			double Sum = 0;
			for (const int32 Index : Routes) Sum += D.Routes[Index].Value;
			return Sum / FMath::Max(1, Routes.Num()) + Routes.Num() * 0.3;
		}

		// How attractive a city is for a new post: good routes, no plague nearby.
		double ValueOf(const FPortsState& State, const FString& City)
		{
			return RoutesValue(City) - DangerAt(State, City) * 2 + (IsAftermath(State, City) ? 1 : 0);
		}

		FMaybeAction Expand(const FPortsState& State, const FPortsPlayer& P, int32 Reserve)
		{
			if (P.florins < Cost(State, TEXT("openPost")) + Reserve) return {};
			TArray<FPortsAction> Options = LegalPosts(State, P);
			if (Options.Num() == 0) return {};
			// Prefer cities with valuable routes and no plague nearby.
			Options.StableSort([&State](const FPortsAction& A, const FPortsAction& B) { return ValueOf(State, A.city) > ValueOf(State, B.city); });
			return Legal(State, Options[0]);
		}

		FMaybeAction GiveCharity(FPortsState& State)
		{
			const TCHAR* const Kinds[] = { TEXT("hospital"), TEXT("confraternity"), TEXT("church") };
			return Legal(State, MakeCharity(Kinds[PickIndex(State, 3)]));
		}

		// Any legal action, picked at random (the 'random' strategy, and a bot's "mistakes").
		FMaybeAction RandomAction(FPortsState& State, const FPortsPlayer& P)
		{
			TArray<FPortsAction> Options;
			Options.Append(LegalShipments(State, P));
			Options.Append(LegalPosts(State, P));
			Options.Add(MakeCharity(TEXT("church")));
			for (const FPortsAction& A : ShipOptions(State, P)) if (A.offshore) Options.Add(A);
			for (const FString& C : P.posts)
			{
				Options.Add(MakeCity(TEXT("marry"), C));
				Options.Add(MakeCity(TEXT("land"), C));
				Options.Add(MakeCity(TEXT("gates"), C));
			}
			Options.Add(Make(TEXT("loan")));
			for (const FPortsPlayer& O : State.players) if (&O != &P) Options.Add(MakeDeal(O.id));
			const TArray<FString> Homes = FamilyLocations(P);
			for (const FString& C : Homes) Options.Add(MakeCity(TEXT("prepare"), C));
			for (const FString& C : Homes) Options.Add(MakeCity(TEXT("physician"), C));
			for (const FString& From : Homes)
			{
				if (From != ESTATE) Options.Add(MakeMove(From, ESTATE, 1));
				for (const FString& To : P.posts) if (To != From) Options.Add(MakeMove(From, To, 1));
			}
			TArray<FPortsAction> Allowed;
			for (const FPortsAction& A : Options) if (CheckAction(State, A).IsEmpty()) Allowed.Add(A);
			if (Allowed.Num() == 0) return {};
			return Allowed[PickIndex(State, Allowed.Num())];
		}

		// ---------- Balanced: pick the action that most improves the Legacy score ----------
		struct FChange { double Florins = 0; double Reputation = 0; double Family = 0; int32 ExtraPosts = 0; int32 ExtraLand = 0; };

		double LegacyOf(const FPortsPlayer& P, const FChange& Change)
		{
			const double Florins = FMath::Max(0.0, P.florins + Change.Florins);
			const double Reputation = FMath::Max(0.0, FMath::Min(FPortsData::Get().Number(TEXT("limits.maxReputation")), P.reputation + Change.Reputation));
			return ScoreTotal(Florins, P.posts.Num() + Change.ExtraPosts, P.land.Num() + Change.ExtraLand, FamilyTotal(P) + Change.Family, Reputation);
		}

		double ExpectedDeaths(const FPortsState& State, int32 Members, const FString& Loc, bool bPrepared = false)
		{
			if (Loc == ESTATE) return 0;
			const FPortsData& D = FPortsData::Get();
			const FPortsCityState& C = *State.City(Loc);
			double Sev = 0;
			double Rounds = 0;
			if (C.state == TEXT("stricken")) { Sev = C.severity; Rounds = D.Int(TEXT("plague.strickenRounds")) - C.strickenFor; }
			else if (IsThreatened(State, Loc)) { Sev = 2; Rounds = 0.4; }
			const double PerRoll = FMath::Max(0.0, Sev - (bPrepared ? D.Number(TEXT("plague.prepareBonus")) : 0.0)) / 6;
			return Members * (1 - FMath::Pow(1 - PerRoll, Rounds));
		}

		TArray<FPortsAction> CandidateActions(const FPortsState& State, const FPortsPlayer& P)
		{
			const int32 MoveMax = FPortsData::Get().Int(TEXT("limits.moveFamilyMax"));
			TArray<FPortsAction> Out = ShipOptions(State, P);
			Out.Append(LegalPosts(State, P));
			Out.Add(MakeCharity(TEXT("hospital")));
			for (const FString& C : P.posts) { Out.Add(MakeCity(TEXT("marry"), C)); Out.Add(MakeCity(TEXT("land"), C)); }
			for (const FString& Loc : FamilyLocations(P))
			{
				Out.Add(MakeCity(TEXT("prepare"), Loc));
				Out.Add(MakeCity(TEXT("physician"), Loc));
				const int32 Count = FMath::Min(MoveMax, FamilyAt(P, Loc));
				if (Loc != ESTATE) Out.Add(MakeMove(Loc, ESTATE, Count));
				for (const FString& To : P.posts) if (To != Loc) Out.Add(MakeMove(Loc, To, Count));
			}
			TArray<FPortsAction> Allowed;
			for (const FPortsAction& A : Out) if (CheckAction(State, A).IsEmpty()) Allowed.Add(A);
			return Allowed;
		}

		FMaybeAction BestByLegacy(const FPortsState& State, const FPortsPlayer& P)
		{
			const FPortsData& D = FPortsData::Get();
			const double Base = LegacyOf(P, FChange());
			const int32 Rounds = D.Int(TEXT("rounds"));
			const int32 RoundsLeft = Rounds - State.round;
			const double StrickenRounds = D.Number(TEXT("plague.strickenRounds"));
			const double PostBonus = D.Number(TEXT("gains.familyAtPostBonus"));
			FMaybeAction Best;
			double BestValue = 0;
			for (const FPortsAction& A : CandidateActions(State, P))
			{
				FChange Change;
				if (A.type == TEXT("ship"))
				{
					const FShipEstimate E = ExpectedShip(State, P, A);
					Change.Florins = E.Value;
					Change.Reputation = -E.RepCost;
				}
				else if (A.type == TEXT("post"))
				{
					const double Useful = P.posts.Num() < ActionPointsFor(State, P) ? 7 : 1.5;
					Change.Florins = -Cost(State, TEXT("openPost")) + Useful * FMath::Max(0, RoundsLeft - 1);
					Change.ExtraPosts = 1;
				}
				else if (A.type == TEXT("charity"))
				{
					Change.Florins = -CharityCost(State, P);
					Change.Reputation = FMath::Max(1, D.Int(TEXT("gains.charityReputation")) + State.effects.charityBonus);
				}
				else if (A.type == TEXT("prepare"))
				{
					const double Saved = ExpectedDeaths(State, FamilyAt(P, A.city), A.city) - ExpectedDeaths(State, FamilyAt(P, A.city), A.city, true);
					Change.Florins = -Cost(State, TEXT("prepareHousehold"));
					Change.Family = Saved / FMath::Max(1.0, StrickenRounds);
				}
				else if (A.type == TEXT("physician"))
				{
					const double Risk = ExpectedDeaths(State, FamilyAt(P, A.city), A.city);
					Change.Florins = -Cost(State, TEXT("physician"));
					Change.Family = FMath::Min(1.0, Risk) * (1.0 / 6) * 0.5;
				}
				else if (A.type == TEXT("move"))
				{
					const double Before = ExpectedDeaths(State, FamilyAt(P, A.from), A.from);
					const double Share = static_cast<double>(A.count) / FamilyAt(P, A.from);
					const double DestRisk = A.to == ESTATE ? 0.0 : ExpectedDeaths(State, A.count, A.to);
					const double Saved = Before * Share - DestRisk;
					const bool bFled = IsStricken(State, A.from);
					const double BonusLoss = A.to == ESTATE && FamilyAt(P, A.from) == A.count ? PostBonus * 2 : 0.0;
					const double BonusGain = A.from == ESTATE ? PostBonus * 2 : 0.0;
					Change.Family = Saved;
					Change.Reputation = bFled ? -D.Number(TEXT("penalties.fleeReputation")) : 0.0;
					Change.Florins = BonusGain - BonusLoss;
				}
				else if (A.type == TEXT("marry"))
				{
					Change.Florins = -Cost(State, TEXT("marriage"));
					Change.Family = D.Number(TEXT("gains.marriageFamily"));
				}
				else if (A.type == TEXT("land"))
				{
					const int32 HalvesLeft = Rounds - State.roundEnd + 1;
					Change.Florins = -Cost(State, TEXT("buyLand")) - HalvesLeft * D.Int(TEXT("costs.landWage"));
					Change.ExtraLand = 1;
				}
				// Compare value per action point, so a 2-AP action must earn twice as much.
				const double Value = (LegacyOf(P, Change) - Base) / ApCost(A.type);
				if (!Best.IsSet() || Value > BestValue) { Best = A; BestValue = Value; }
			}
			return Best.IsSet() && BestValue > 0.05 ? Best : FMaybeAction();
		}

		// Land is worth buying only if its points beat the price plus the wages still to pay.
		bool LandPaysOff(const FPortsState& State)
		{
			const FPortsData& D = FPortsData::Get();
			const int32 HalvesLeft = D.Int(TEXT("rounds")) - State.roundEnd + 1;
			return D.Int(TEXT("costs.buyLand")) + HalvesLeft * D.Int(TEXT("costs.landWage")) < D.Int(TEXT("scoring.pointsPerLand")) * D.Int(TEXT("scoring.florinsPerPoint"));
		}

		// Borrow when a wedding or land purchase is blocked only by a lack of florins.
		FMaybeAction BorrowFor(FPortsState& State, FPortsPlayer& P)
		{
			if (!CheckAction(State, Make(TEXT("loan"))).IsEmpty()) return {};
			const int32 Loan = FPortsData::Get().Int(TEXT("gains.loan"));
			P.florins += Loan;
			bool bHelps = false;
			for (const FString& C : P.posts)
			{
				if (CheckAction(State, MakeCity(TEXT("marry"), C)).IsEmpty() || CheckAction(State, MakeCity(TEXT("land"), C)).IsEmpty()) { bHelps = true; break; }
			}
			P.florins -= Loan;
			return bHelps ? FMaybeAction(Make(TEXT("loan"))) : FMaybeAction();
		}

		// Offer a partnership to the house whose posts sit at the ends of our routes.
		FMaybeAction ProposeDeal(const FPortsState& State, const FPortsPlayer& P)
		{
			TArray<FString> Reach;
			for (const FString& C : P.posts) for (const FString& N : Neighbors(C)) Reach.AddUnique(N);
			int32 BestId = -1, BestOverlap = 0;
			for (const FPortsPlayer& O : State.players)
			{
				int32 Overlap = 0;
				for (const FString& C : O.posts) if (Reach.Contains(C)) Overlap++;
				for (const FString& C : P.posts)
				{
					if (O.posts.ContainsByPredicate([&C](const FString& X) { return Neighbors(X).Contains(C); })) Overlap++;
				}
				if (Overlap && CheckAction(State, MakeDeal(O.id)).IsEmpty() && (BestId < 0 || Overlap > BestOverlap)) { BestId = O.id; BestOverlap = Overlap; }
			}
			return BestId >= 0 ? FMaybeAction(MakeDeal(BestId)) : FMaybeAction();
		}

		// The action a bot's strategy wants next, or nothing when it is done.
		FMaybeAction ChooseAction(FPortsState& State, FPortsPlayer& P, const FString& Strategy)
		{
			const FPortsData& D = FPortsData::Get();
			if (Strategy == TEXT("random")) return RandomAction(State, P);
			// Partnerships are offered once, at the start of the turn.
			if (P.ap == ActionPointsFor(State, P))
			{
				const FMaybeAction Deal = ProposeDeal(State, P);
				if (Deal.IsSet()) return Deal;
			}
			FMaybeAction Out;
			if (Strategy == TEXT("greedy"))
			{
				// Reputation above the soft cap is cheap to spend on shutting rivals out.
				if (!P.gates.IsSet() && P.reputation > D.Int(TEXT("scoring.reputationSoftCap")) + 1 && FamilyAt(P, P.home))
				{
					Out = Legal(State, MakeCity(TEXT("gates"), P.home));
					if (Out.IsSet()) return Out;
				}
				Out = Legal(State, BestShipment(State, P));
				if (Out.IsSet()) return Out;
				if (LandPaysOff(State))
				{
					for (const FString& C : P.posts)
					{
						Out = Legal(State, MakeCity(TEXT("land"), C));
						if (Out.IsSet()) return Out;
					}
				}
				return Expand(State, P, 0);
			}
			if (Strategy == TEXT("cautious"))
			{
				Out = ProtectFamily(State, P, true, 0.7);
				if (Out.IsSet()) return Out;
				Out = Legal(State, BestShipment(State, P, true));
				if (Out.IsSet()) return Out;
				Out = Expand(State, P, 4);
				if (Out.IsSet()) return Out;
				return ReturnFamily(State, P);
			}
			if (Strategy == TEXT("charitable"))
			{
				if (P.charityThisTurn == 0 && P.reputation < D.Int(TEXT("limits.maxReputation")) && P.florins >= CharityCost(State, P))
				{
					Out = GiveCharity(State);
					if (Out.IsSet()) return Out;
				}
				Out = ProtectFamily(State, P, false, 1);
				if (Out.IsSet()) return Out;
				Out = Legal(State, BestShipment(State, P));
				if (Out.IsSet()) return Out;
				return Expand(State, P, 6);
			}
			// balanced, and anything else
			Out = BestByLegacy(State, P);
			if (Out.IsSet()) return Out;
			return BorrowFor(State, P);
		}

		FString StrategyOf(const FPortsPlayer& P) { return P.strategy.IsEmpty() ? FString(TEXT("balanced")) : P.strategy; }
	}

	double MistakeRate(const FPortsPlayer& P)
	{
		double Rate = 0;
		if (!P.skill.IsEmpty()) FPortsData::Get().TryNumber(TEXT("bots.skills.") + P.skill + TEXT(".mistakes"), Rate);
		return Rate;
	}

	EPortsChoice BotChooseDecision(FPortsState& State, FPortsPlayer& P)
	{
		return ChooseDecision(State, P, StrategyOf(P));
	}

	FPortsBotMove BotMove(FPortsState& State)
	{
		FPortsBotMove Move;
		FPortsPlayer* Current = CurrentPlayer(State);
		if (!Current) return Move;
		FPortsPlayer& P = *Current;
		const FString Strategy = StrategyOf(P);
		const auto Slip = [&]() { return NextRandom(State) < MistakeRate(P); };
		if (P.pending.Num())
		{
			Move.type = FPortsBotMove::EType::Decide;
			Move.choice = ChooseDecision(State, P, Slip() ? FString(TEXT("random")) : Strategy);
			return Move;
		}
		if (P.ap <= 0 && !P.free.Values.Contains(true)) return Move;
		FMaybeAction Action = Slip() ? RandomAction(State, P) : FMaybeAction();
		if (!Action.IsSet()) Action = ChooseAction(State, P, Strategy);
		if (Action.IsSet())
		{
			Move.type = FPortsBotMove::EType::Act;
			Move.action = *Action;
		}
		return Move;
	}

	FPortsResult BotDecide(FPortsState& State, EPortsChoice Choice)
	{
		FPortsPlayer* P = CurrentPlayer(State);
		const bool bWageLaw = P && P->pending.Num() && P->pending[0].Get(TEXT("kind")).AsString() == TEXT("wageLaw");
		const FPortsResult R = Decide(State, Choice);
		return R.ok ? R : Decide(State, bWageLaw ? EPortsChoice::Pay : EPortsChoice::No);
	}

	FPortsResult PlayTurn(FPortsState& State)
	{
		for (int32 Guard = 0; Guard < 40; Guard++)
		{
			const FPortsBotMove Move = BotMove(State);
			if (Move.type == FPortsBotMove::EType::End) break;
			if (Move.type == FPortsBotMove::EType::Decide) BotDecide(State, Move.choice);
			else if (!PerformAction(State, Move.action).ok) break;
		}
		FPortsPlayer* P = CurrentPlayer(State);
		while (P && P->pending.Num()) BotDecide(State, BotChooseDecision(State, *P));
		return EndTurn(State);
	}

	int32 PlayBotGame(const FPortsSetup& Setup, FPortsState& State)
	{
		FString Problem;
		if (!CreateGame(Setup, State, Problem)) return -1;
		int32 Turns = 0;
		for (int32 Guard = 0; State.phase != TEXT("ended") && Guard < 1000; Guard++)
		{
			if (State.phase == TEXT("actions")) { PlayTurn(State); Turns++; }
			else Advance(State);
		}
		return State.phase == TEXT("ended") ? Turns : -1;
	}
}
