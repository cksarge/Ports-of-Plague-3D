// The actions. Every action is checked by a Check function that returns a
// plain-language reason when a move is illegal; the action itself only runs
// after a successful check.
#include "PortsRules.h"
#include "Algo/Find.h"

using V = FPortsValue;

namespace Ports
{
	static const TCHAR* const ENGLISH_CITIES[] = { TEXT("london"), TEXT("melcombe"), TEXT("bristol"), TEXT("york") };

	static FString CityName(const FString& Id)
	{
		if (Id == ESTATE) return TEXT("your Country Estate");
		const FPortsCity* City = FPortsData::Get().FindCity(Id);
		return City ? City->Name : Id;
	}

	static bool IsCity(const FString& Id) { return FPortsData::Get().FindCity(Id) != nullptr; }

	static FString BaseChecks(const FPortsState& State, const FPortsPlayer* P, int32 ApNeeded = 1)
	{
		if (State.phase != TEXT("actions")) return TEXT("It is not the action phase.");
		if (!P) return TEXT("No player is taking a turn.");
		if (P->pending.Num()) return TEXT("First answer the card waiting for you.");
		if (P->ap < ApNeeded)
		{
			return P->ap < 1 ? FString(TEXT("You have no action points left. End your turn.")) : FString::Printf(TEXT("This action takes %d action points; you have %d left."), ApNeeded, P->ap);
		}
		return FString();
	}

	FPortsValue FPortsAction_ToValueImpl(const FPortsAction& A)
	{
		V Out = V::Object({ { TEXT("type"), A.type } });
		if (A.type == TEXT("ship")) { Out.Set(TEXT("from"), A.from); Out.Set(TEXT("route"), A.route); if (A.offshore) Out.Set(TEXT("offshore"), true); }
		else if (A.type == TEXT("move")) { Out.Set(TEXT("from"), A.from); Out.Set(TEXT("to"), A.to); Out.Set(TEXT("count"), A.count); }
		else if (A.type == TEXT("charity")) Out.Set(TEXT("kind"), A.kind);
		else if (A.type == TEXT("deal")) Out.Set(TEXT("partner"), A.partner);
		else if (A.type != TEXT("loan")) Out.Set(TEXT("city"), A.city);
		return Out;
	}

	// ---------- Ship Goods ----------
	FPortsShipQuote ShipQuote(const FPortsState& State, const FPortsPlayer& P, const FString& RouteId, const FString& From, bool bOffshore)
	{
		const FPortsData& D = FPortsData::Get();
		FPortsShipQuote Q;
		const FPortsRoute* R = D.FindRoute(RouteId);
		Q.route = R;
		Q.from = From;
		Q.parts = V::Array();
		if (!R) return Q;
		const FString To = OtherEnd(*R, From);
		Q.to = To;
		const FPortsEffects& Fx = State.effects;
		const int32 Bonus = Fx.profit.all + Fx.profit.Of(R->Type);
		const auto Part = [&Q](const FString& Label, int32 Value) { Q.parts.Add(V::Object({ { TEXT("label"), Label }, { TEXT("value"), Value } })); };
		Part(FString::Printf(TEXT("%s route value"), R->Type == TEXT("sea") ? TEXT("Sea") : TEXT("Land")), R->Value);
		if (FamilyAt(P, From) > 0) Part(TEXT("Family runs the post"), D.Int(TEXT("gains.familyAtPostBonus")));
		if (IsAftermath(State, To)) Part(TEXT("High prices at destination (Aftermath)"), D.Int(TEXT("gains.aftermathPriceBonus")) + Fx.marketBonus);
		if (IsAftermath(State, From)) Part(TEXT("Wages at origin (Aftermath)"), -Cost(State, TEXT("wageAftermath")));
		const FPortsCityState* FromState = State.City(From);
		const FPortsCityState* ToState = State.City(To);
		if ((FromState && FromState->unrest > 0) || (ToState && ToState->unrest > 0)) Part(TEXT("Unrest"), -D.Int(TEXT("penalties.unrestProfit")));
		const FPortsPlayer* Partner = P.deal.IsSet() ? &State.players[P.deal->partner] : nullptr;
		const bool bPartnerThere = Partner && Partner->posts.Contains(To);
		if (bPartnerThere && D.Int(TEXT("gains.dealShipperBonus"))) Part(FString::Printf(TEXT("Partner's agent (%s)"), *Partner->name), D.Int(TEXT("gains.dealShipperBonus")));
		if (const FPortsPlayer* Gates = GatesClosedBy(State, To, &P)) Part(FString::Printf(TEXT("Gates closed by %s"), *Gates->name), -D.Int(TEXT("penalties.gatesProfit")));
		for (const FPortsCityProfit& M : Fx.cityProfit)
		{
			for (const FString* C : { &From, &To })
			{
				if (M.cities.Contains(*C) && (!M.onlyStricken || IsStricken(State, *C))) Part(FString::Printf(TEXT("Event: %s"), *CityName(*C)), M.profit);
			}
		}
		if (Bonus) Part(TEXT("This round’s event"), Bonus);
		if (P.nextShip.IsSet() && P.nextShip->profit) Part(TEXT("Fortune card"), P.nextShip->profit);
		int32 Fixed = 0;
		for (const FPortsValue& Line : Q.parts.GetItems()) Fixed += Line.Get(TEXT("value")).AsInt();
		Q.fixed = Fixed;
		const bool bSafe = P.nextShip.IsSet() && P.nextShip->safe;
		double ContagionMod = 0;
		D.TryNumber(DifficultyPath(State) + TEXT(".contagionMod"), ContagionMod);
		Q.contagionRisk = IsStricken(State, From) && !bSafe
			? FMath::Max(0, FMath::Min(6, FromState->severity + Fx.contagion.all + Fx.contagion.Of(R->Type) + static_cast<int32>(ContagionMod)))
			: 0;
		Q.fee = bOffshore ? Cost(State, TEXT("holdOffshore")) : 0;
		Q.min = FMath::Max(0, Fixed + 1);
		Q.max = FMath::Max(0, Fixed + D.Int(TEXT("shipping.profitDie")));
		Q.safe = bSafe && IsStricken(State, From);
		Q.offshore = bOffshore;
		Q.partner = bPartnerThere ? Partner->id : -1;
		return Q;
	}

	FString CheckShip(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		const FString Why = BaseChecks(State, P);
		if (!Why.IsEmpty()) return Why;
		const FPortsRoute* R = FPortsData::Get().FindRoute(A.route);
		if (!R) return TEXT("Choose a route.");
		if (!P->posts.Contains(A.from)) return FString::Printf(TEXT("You have no trading post in %s."), *CityName(A.from));
		if (R->A != A.from && R->B != A.from) return FString::Printf(TEXT("That route does not leave %s."), *CityName(A.from));
		if (P->shipped.Contains(A.from)) return FString::Printf(TEXT("Your post in %s has already shipped this round. Each post ships once per round."), *CityName(A.from));
		if (P->englishBlocked && Algo::Find(ENGLISH_CITIES, A.from)) return FString::Printf(TEXT("You obeyed the wage law: workers in %s refuse to work for the old wages this round."), *CityName(A.from));
		if (A.offshore)
		{
			if (!IsStricken(State, A.from)) return TEXT("Holding a ship offshore only matters when it sails from a Stricken city.");
			const int32 Fee = Cost(State, TEXT("holdOffshore"));
			if (P->florins < Fee) return FString::Printf(TEXT("Holding the ship offshore costs %dƒ; you have %dƒ."), Fee, P->florins);
		}
		return FString();
	}

	static FPortsValue DoShip(FPortsState& State, FPortsPlayer& P, const FPortsAction& A)
	{
		const FPortsData& D = FPortsData::Get();
		const FPortsShipQuote Q = ShipQuote(State, P, A.route, A.from, A.offshore);
		P.florins -= Q.fee;
		if (A.offshore) P.stats.offshore++;
		const int32 ProfitDie = Roll(State, D.Int(TEXT("shipping.profitDie")));
		bool bRolledContagion = false;
		int32 ContagionDie = 0;
		bool bInfected = false;
		P.nextShip.Reset(); // a Fortune bonus applies to one shipment only
		if (IsStricken(State, A.from) && !Q.safe)
		{
			bRolledContagion = true;
			ContagionDie = Roll(State, 6);
			bInfected = ContagionDie <= Q.contagionRisk;
		}
		int32 Profit = FMath::Max(0, Q.fixed + ProfitDie);
		const double InfectedFactor = D.Number(TEXT("shipping.infectedProfitFactor"));
		FString Spread; // empty = null
		int32 WorseSeverity = 0;
		if (bInfected && A.offshore)
		{
			// The ship waits at anchor: the sickness shows before anyone lands.
			Profit = FMath::FloorToInt32(Profit * InfectedFactor);
			P.stats.infected++;
			Spread = TEXT("held");
		}
		else if (bInfected)
		{
			Profit = FMath::FloorToInt32(Profit * InfectedFactor);
			P.reputation -= D.Int(TEXT("penalties.infectedCargoReputation"));
			ClampReputation(P);
			P.stats.infected++;
			FPortsCityState& Dest = *State.City(Q.to);
			const int32 ArrivalRound = D.FindCity(Q.to)->ArrivalRound;
			const int32 End = State.roundEnd;
			if (Dest.state == TEXT("safe") && ArrivalRound > End && ArrivalRound - End <= D.Int(TEXT("plague.earlyArrivalWindow")) * State.span)
			{
				Spread = TEXT("early");
				StrikeCity(State, Q.to, true, P.id);
				P.stats.spread++;
			}
			else if (Dest.state == TEXT("stricken") && Dest.severity < D.Int(TEXT("plague.severityMax")))
			{
				Dest.severity++;
				WorseSeverity = Dest.severity;
				Spread = TEXT("worse");
				P.stats.spread++;
			}
			else
			{
				Spread = TEXT("none");
			}
		}
		P.florins += Profit;
		P.shipped.Add(A.from);
		P.stats.shipments++;
		P.stats.earned += Profit;
		const int32 DealBonus = D.Int(TEXT("gains.dealBonus"));
		if (Q.partner >= 0) State.players[Q.partner].florins += DealBonus;
		FString Text = FString::Printf(TEXT("%s ships from %s to %s: profit die %d, earning %dƒ."), *P.name, *CityName(A.from), *CityName(Q.to), ProfitDie, Profit);
		if (A.offshore) Text += FString::Printf(TEXT(" The ship waited offshore (%dƒ)."), Q.fee);
		if (bRolledContagion) Text += FString::Printf(TEXT(" Contagion die %d (infected on %d or less): %s"), ContagionDie, Q.contagionRisk, bInfected ? TEXT("INFECTED cargo!") : TEXT("clean cargo."));
		if (Spread == TEXT("worse")) Text += FString::Printf(TEXT(" The plague in %s grows worse (%s)."), *CityName(Q.to), *SeverityName(WorseSeverity));
		if (Spread == TEXT("none")) Text += TEXT(" The infection dies out.");
		if (Spread == TEXT("held")) Text += TEXT(" The sickness shows while the ship waits at anchor: no one lands, no reputation is lost and the plague does not spread.");
		if (Q.partner >= 0) Text += FString::Printf(TEXT(" Partner %s earns %dƒ."), *State.players[Q.partner].name, DealBonus);
		V FactIds = V::Array();
		if (bInfected && !A.offshore) { FactIds.Add(V(TEXT("TR-05"))); FactIds.Add(V(TEXT("CI-11"))); }
		if (A.offshore) { FactIds.Add(V(TEXT("ME-12"))); FactIds.Add(V(TEXT("ME-14"))); }
		const FPortsValue Entry = AddLog(State, V::Object({
			{ TEXT("type"), TEXT("ship") }, { TEXT("player"), P.id }, { TEXT("from"), A.from }, { TEXT("to"), Q.to }, { TEXT("route"), A.route },
			{ TEXT("profitDie"), ProfitDie }, { TEXT("contagionDie"), bRolledContagion ? V(ContagionDie) : V::Null() }, { TEXT("contagionRisk"), Q.contagionRisk },
			{ TEXT("infected"), bInfected }, { TEXT("profit"), Profit }, { TEXT("spread"), Spread.IsEmpty() ? V::Null() : V(Spread) }, { TEXT("parts"), Q.parts },
			{ TEXT("safe"), Q.safe }, { TEXT("offshore"), A.offshore }, { TEXT("fee"), Q.fee }, { TEXT("partner"), Q.partner >= 0 ? V(Q.partner) : V::Null() },
			{ TEXT("text"), Text }, { TEXT("factIds"), FactIds } }));
		if (ProfitDie == D.Int(TEXT("fortune.drawOnProfitDie"))) DrawFortune(State, P, FString::Printf(TEXT("rolled a %d on the profit die"), ProfitDie));
		return Entry;
	}

	// ---------- Open Trading Post ----------
	FString CheckPost(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		const FPortsData& D = FPortsData::Get();
		const bool bFree = P && P->free.Get(TEXT("post"), false);
		const FString Why = BaseChecks(State, P, bFree ? 0 : ApCost(TEXT("post")));
		if (!Why.IsEmpty()) return Why;
		if (!IsCity(A.city)) return TEXT("Choose a city.");
		if (P->posts.Contains(A.city)) return FString::Printf(TEXT("You already have a trading post in %s."), *CityName(A.city));
		if (P->posts.Num() >= D.Int(TEXT("limits.maxPosts"))) return FString::Printf(TEXT("You already have the maximum of %d trading posts."), D.Int(TEXT("limits.maxPosts")));
		if (!P->posts.ContainsByPredicate([&A](const FString& Own) { return Neighbors(Own).Contains(A.city); })) return FString::Printf(TEXT("%s is not connected by a route to any of your posts."), *CityName(A.city));
		if (IsStricken(State, A.city)) return FString::Printf(TEXT("%s is Stricken: its gates are closed to new trading posts."), *CityName(A.city));
		if (const FPortsPlayer* Gates = GatesClosedBy(State, A.city, P)) return FString::Printf(TEXT("%s has closed the gates of %s to rival merchants."), *Gates->name, *CityName(A.city));
		if (State.effects.noNewPostsNearPlague && Neighbors(A.city).ContainsByPredicate([&State](const FString& N) { return IsStricken(State, N); }))
		{
			return FString::Printf(TEXT("Guards at the gates: %s is next to a Stricken city and turns strangers away this round."), *CityName(A.city));
		}
		const int32 Price = Cost(State, TEXT("openPost"), P);
		if (!bFree && P->florins < Price) return FString::Printf(TEXT("A trading post costs %dƒ; you have %dƒ."), Price, P->florins);
		return FString();
	}

	// ---------- Move Family ----------
	static FString CheckMove(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		const FPortsData& D = FPortsData::Get();
		const bool bFree = P && (P->free.Get(TEXT("move"), false) || P->free.Get(TEXT("moveNoPenalty"), false));
		const FString Why = BaseChecks(State, P, bFree ? 0 : ApCost(TEXT("move")));
		if (!Why.IsEmpty()) return Why;
		if (A.from == A.to) return TEXT("Choose two different places.");
		const auto ValidPlace = [P](const FString& Loc) { return Loc == ESTATE || P->posts.Contains(Loc); };
		if (!ValidPlace(A.from) || !ValidPlace(A.to)) return TEXT("Family can only live in cities where you have a trading post, or at your Country Estate.");
		if (!A.bCountIsWhole || A.count < 1) return TEXT("Choose how many family members to move.");
		const int32 MoveMax = D.Int(TEXT("limits.moveFamilyMax"));
		if (A.count > MoveMax) return FString::Printf(TEXT("You can move at most %d family members per action."), MoveMax);
		const int32 There = FamilyAt(*P, A.from);
		if (There < A.count) return FString::Printf(TEXT("You have only %d family member%s in %s."), There, There == 1 ? TEXT("") : TEXT("s"), *CityName(A.from));
		return FString();
	}

	// ---------- Prepare Household ----------
	static FString CheckPrepare(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		const bool bFree = P && P->free.Get(TEXT("prepare"), false);
		const FString Why = BaseChecks(State, P, bFree ? 0 : 1);
		if (!Why.IsEmpty()) return Why;
		if (!IsCity(A.city) || FamilyAt(*P, A.city) < 1) return TEXT("Choose a city where your family lives.");
		if (P->prepared.Contains(A.city)) return FString::Printf(TEXT("Your household in %s is already prepared this round."), *CityName(A.city));
		const int32 Price = Cost(State, TEXT("prepareHousehold"));
		if (!bFree && P->florins < Price) return FString::Printf(TEXT("Preparing costs %dƒ; you have %dƒ."), Price, P->florins);
		return FString();
	}

	// ---------- Consult Physician ----------
	static FString CheckPhysician(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		const bool bFree = P && P->free.Get(TEXT("physician"), false);
		const FString Why = BaseChecks(State, P, bFree ? 0 : 1);
		if (!Why.IsEmpty()) return Why;
		if (!IsCity(A.city) || FamilyAt(*P, A.city) < 1) return TEXT("Choose a city where your family lives.");
		if (P->physician.Contains(A.city)) return FString::Printf(TEXT("A physician is already caring for your family in %s this round."), *CityName(A.city));
		const int32 Price = Cost(State, TEXT("physician"));
		if (!bFree && P->florins < Price) return FString::Printf(TEXT("A physician costs %dƒ; you have %dƒ."), Price, P->florins);
		return FString();
	}

	// ---------- Charity & Piety ----------
	struct FCharityKind { const TCHAR* Id; const TCHAR* Label; TArray<const TCHAR*> FactIds; };
	static const FCharityKind* FindCharity(const FString& Kind)
	{
		static const FCharityKind Kinds[] = {
			{ TEXT("hospital"), TEXT("Fund a hospital"), { TEXT("CI-05"), TEXT("CH-03") } },
			{ TEXT("confraternity"), TEXT("Endow a confraternity"), { TEXT("CH-07") } },
			{ TEXT("church"), TEXT("Give to your parish church"), { TEXT("CH-03"), TEXT("CH-06") } },
		};
		for (const FCharityKind& K : Kinds) if (Kind == K.Id) return &K;
		return nullptr;
	}

	int32 CharityCost(const FPortsState& State, const FPortsPlayer& P)
	{
		const int32 Discount = LastPlaceId(State) == P.id ? FPortsData::Get().Int(TEXT("costs.charityDiscountLastPlace")) : 0;
		return FMath::Max(0, Cost(State, TEXT("charity")) - Discount);
	}

	static FString CheckCharity(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		const FPortsData& D = FPortsData::Get();
		const FString Why = BaseChecks(State, P);
		if (!Why.IsEmpty()) return Why;
		if (!FindCharity(A.kind)) return TEXT("Choose where to give.");
		const int32 Gifts = D.Int(TEXT("limits.charityPerTurn")) * State.span;
		if (P->charityThisTurn >= Gifts) return Gifts > 1 ? FString::Printf(TEXT("You have already given charity %d times this turn (once per half-year)."), Gifts) : FString(TEXT("You have already given charity this turn."));
		const int32 Price = CharityCost(State, *P);
		if (P->florins < Price) return FString::Printf(TEXT("Charity costs %dƒ; you have %dƒ."), Price, P->florins);
		const int32 MaxRep = D.Int(TEXT("limits.maxReputation"));
		if (P->reputation >= MaxRep) return FString::Printf(TEXT("Your reputation is already at the maximum (%d)."), MaxRep);
		return FString();
	}

	// ---------- Arrange a Marriage ----------
	static FString CheckMarry(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		const FPortsData& D = FPortsData::Get();
		const FString Why = BaseChecks(State, P);
		if (!Why.IsEmpty()) return Why;
		if (!IsCity(A.city) || !P->posts.Contains(A.city)) return TEXT("Choose a city where you have a trading post.");
		if (!IsAftermath(State, A.city)) return FString::Printf(TEXT("%s is not in Aftermath yet. Weddings wait until the plague has passed."), *CityName(A.city));
		if (FamilyAt(*P, A.city) < 1) return FString::Printf(TEXT("Your family must live in %s to arrange a marriage there."), *CityName(A.city));
		const int32 StartFamily = D.Int(TEXT("start.family"));
		if (FamilyTotal(*P) >= StartFamily) return FString::Printf(TEXT("Your house already has %d family members."), StartFamily);
		if (P->marriedThisTurn >= D.Int(TEXT("limits.marriagePerTurn"))) return TEXT("You have already arranged a marriage this turn.");
		const int32 Price = Cost(State, TEXT("marriage"));
		if (P->florins < Price) return FString::Printf(TEXT("A wedding costs %dƒ; you have %dƒ."), Price, P->florins);
		return FString();
	}

	// ---------- Buy Abandoned Land ----------
	static FString CheckLand(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		const FString Why = BaseChecks(State, P);
		if (!Why.IsEmpty()) return Why;
		if (!IsCity(A.city) || !P->posts.Contains(A.city)) return TEXT("Choose a city where you have a trading post.");
		if (!IsAftermath(State, A.city)) return FString::Printf(TEXT("%s is not in Aftermath yet. Its fields are still being worked."), *CityName(A.city));
		if (P->land.Contains(A.city)) return FString::Printf(TEXT("You already own land near %s."), *CityName(A.city));
		const int32 Price = Cost(State, TEXT("buyLand"));
		if (P->florins < Price) return FString::Printf(TEXT("The land costs %dƒ; you have %dƒ."), Price, P->florins);
		return FString();
	}

	// ---------- Take a Loan (no action point) ----------
	static FString CheckLoan(const FPortsState& State, const FPortsPlayer* P, const FPortsAction&)
	{
		const FString Why = BaseChecks(State, P, 0);
		if (!Why.IsEmpty()) return Why;
		if (P->loan.IsSet()) return FString::Printf(TEXT("You already owe %dƒ. It must be repaid before you borrow again."), P->loan->owed);
		if (UntilRound(State, 2) > FPortsData::Get().Int(TEXT("rounds"))) return TEXT("No banker will lend in the final round.");
		return FString();
	}

	// ---------- Propose a Partnership (no action point) ----------
	static FString CheckDeal(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		const FString Why = BaseChecks(State, P, 0);
		if (!Why.IsEmpty()) return Why;
		const FPortsPlayer* Other = State.players.IsValidIndex(A.partner) ? &State.players[A.partner] : nullptr;
		if (!Other || Other == P) return TEXT("Choose another house.");
		if (P->proposedThisTurn) return TEXT("You have already proposed a partnership this turn.");
		if (P->deal.IsSet()) return FString::Printf(TEXT("You already have a partnership with %s."), *State.players[P->deal->partner].name);
		if (Other->deal.IsSet()) return FString::Printf(TEXT("%s already has a partner."), *Other->name);
		if (Other->pending.ContainsByPredicate([](const FPortsValue& Dec) { return Dec.Get(TEXT("kind")).AsString() == TEXT("deal"); })) return FString::Printf(TEXT("%s is already considering an offer."), *Other->name);
		return FString();
	}

	// ---------- Close Your Gates ----------
	static FString CheckGates(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		const FString Why = BaseChecks(State, P);
		if (!Why.IsEmpty()) return Why;
		if (!IsCity(A.city) || !P->posts.Contains(A.city) || FamilyAt(*P, A.city) < 1) return TEXT("Choose a city where you have a trading post and family.");
		if (IsStricken(State, A.city)) return FString::Printf(TEXT("%s is Stricken: it is too late to close the gates."), *CityName(A.city));
		if (P->gates.IsSet()) return FString::Printf(TEXT("Your gates in %s are already closed."), *CityName(P->gates->city));
		if (const FPortsPlayer* Other = GatesClosedBy(State, A.city, P)) return FString::Printf(TEXT("%s has already closed the gates of %s."), *Other->name, *CityName(A.city));
		return FString();
	}

	// ---------- Dispatcher ----------
	static FString CheckFor(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& A)
	{
		if (A.type == TEXT("ship")) return CheckShip(State, P, A);
		if (A.type == TEXT("post")) return CheckPost(State, P, A);
		if (A.type == TEXT("move")) return CheckMove(State, P, A);
		if (A.type == TEXT("prepare")) return CheckPrepare(State, P, A);
		if (A.type == TEXT("physician")) return CheckPhysician(State, P, A);
		if (A.type == TEXT("charity")) return CheckCharity(State, P, A);
		if (A.type == TEXT("marry")) return CheckMarry(State, P, A);
		if (A.type == TEXT("land")) return CheckLand(State, P, A);
		if (A.type == TEXT("loan")) return CheckLoan(State, P, A);
		if (A.type == TEXT("deal")) return CheckDeal(State, P, A);
		if (A.type == TEXT("gates")) return CheckGates(State, P, A);
		return TEXT("Unknown action.");
	}

	FString CheckAction(const FPortsState& State, const FPortsAction& Action)
	{
		return CheckFor(State, CurrentPlayer(State), Action);
	}

	FPortsResult PerformAction(FPortsState& State, const FPortsAction& A)
	{
		const FPortsData& D = FPortsData::Get();
		FPortsResult Out;
		Out.reason = CheckAction(State, A);
		if (!Out.reason.IsEmpty()) return Out;
		FPortsPlayer& P = *CurrentPlayer(State);
		bool bFree = false;

		if (A.type == TEXT("ship"))
		{
			Out.entry = DoShip(State, P, A);
		}
		else if (A.type == TEXT("post"))
		{
			bFree = P.free.Get(TEXT("post"), false);
			const int32 Price = bFree ? 0 : Cost(State, TEXT("openPost"), &P);
			if (bFree) P.free.Remove(TEXT("post"));
			P.florins -= Price;
			P.posts.Add(A.city);
			Out.entry = AddLog(State, V::Object({
				{ TEXT("type"), TEXT("post") }, { TEXT("player"), P.id }, { TEXT("city"), A.city },
				{ TEXT("text"), FString::Printf(TEXT("%s opens a trading post in %s%s."), *P.name, *CityName(A.city), bFree ? TEXT(" for free (Fortune card)") : *FString::Printf(TEXT(" for %dƒ"), Price)) },
				{ TEXT("factIds"), V::Array() } }));
			if (D.Number(TEXT("fortune.drawOnNewPost")) != 0) DrawFortune(State, P, TEXT("opened a new trading post"));
		}
		else if (A.type == TEXT("move"))
		{
			const bool bNoPenalty = P.free.Get(TEXT("moveNoPenalty"), false);
			bFree = P.free.Get(TEXT("move"), false) || bNoPenalty;
			if (bNoPenalty) P.free.Remove(TEXT("moveNoPenalty"));
			else if (bFree) P.free.Remove(TEXT("move"));
			P.family.Set(A.from, P.family.Get(A.from) - A.count);
			P.family.Set(A.to, P.family.Get(A.to, 0) + A.count);
			if (P.family.Get(A.from) == 0 && A.from != P.home) P.family.Remove(A.from);
			const bool bFled = A.from != ESTATE && IsStricken(State, A.from);
			const int32 FleeRep = D.Int(TEXT("penalties.fleeReputation"));
			if (bFled && !bNoPenalty)
			{
				P.reputation -= FleeRep;
				ClampReputation(P);
				P.stats.fled++;
			}
			FString Text = FString::Printf(TEXT("%s moves %d family member%s from %s to %s."), *P.name, A.count, A.count == 1 ? TEXT("") : TEXT("s"), *CityName(A.from), *CityName(A.to));
			if (bFled && !bNoPenalty) Text += FString::Printf(TEXT(" Fleeing a Stricken city costs %d reputation."), FleeRep);
			else if (bFled) Text += TEXT(" Friends in the countryside take them in: no reputation lost.");
			if (bFree) Text += TEXT(" (Free action from the event card.)");
			Out.entry = AddLog(State, V::Object({
				{ TEXT("type"), TEXT("move") }, { TEXT("player"), P.id }, { TEXT("from"), A.from }, { TEXT("to"), A.to }, { TEXT("count"), A.count }, { TEXT("fled"), bFled },
				{ TEXT("text"), Text }, { TEXT("factIds"), bFled ? V::Array({ V(TEXT("SO-04")) }) : V::Array() } }));
		}
		else if (A.type == TEXT("prepare"))
		{
			bFree = P.free.Get(TEXT("prepare"), false);
			if (bFree) P.free.Remove(TEXT("prepare"));
			else P.florins -= Cost(State, TEXT("prepareHousehold"));
			P.prepared.Add(A.city);
			Out.entry = AddLog(State, V::Object({
				{ TEXT("type"), TEXT("prepare") }, { TEXT("player"), P.id }, { TEXT("city"), A.city },
				{ TEXT("text"), FString::Printf(TEXT("%s's household in %s shuts its doors and stockpiles food (+%d to survival rolls this round).%s"), *P.name, *CityName(A.city), D.Int(TEXT("plague.prepareBonus")), bFree ? TEXT(" (Free from the event card.)") : TEXT("")) },
				{ TEXT("factIds"), V::Array({ V(TEXT("SO-05")), V(TEXT("ME-12")) }) } }));
		}
		else if (A.type == TEXT("physician"))
		{
			bFree = P.free.Get(TEXT("physician"), false);
			if (bFree) P.free.Remove(TEXT("physician"));
			else P.florins -= Cost(State, TEXT("physician"));
			P.physician.Add(A.city);
			const FPortsValue& Remedy = D.Remedies()[PickIndex(State, D.Remedies().Num())];
			const FString Text = FString::Printf(TEXT("%s consults a physician in %s. Remedy: %s. %s It cannot cure the plague, but nursing care gives one family member a slim chance (a %d) if they fall ill this round."),
				*P.name, *CityName(A.city), *Remedy.Get(TEXT("name")).AsString(), *Remedy.Get(TEXT("text")).AsString(), D.Int(TEXT("plague.physicianSaveOn")));
			V FactIds = V::Array();
			for (const FPortsValue& Id : Remedy.Get(TEXT("factIds")).GetItems()) FactIds.Add(Id);
			FactIds.Add(V(TEXT("ME-08")));
			Out.entry = AddLog(State, V::Object({ { TEXT("type"), TEXT("physician") }, { TEXT("player"), P.id }, { TEXT("city"), A.city }, { TEXT("remedy"), Remedy.Get(TEXT("id")) }, { TEXT("text"), Text }, { TEXT("factIds"), FactIds } }));
		}
		else if (A.type == TEXT("charity"))
		{
			const int32 Price = CharityCost(State, P);
			const int32 Gain = FMath::Max(1, D.Int(TEXT("gains.charityReputation")) + State.effects.charityBonus);
			P.florins -= Price;
			P.reputation += Gain;
			ClampReputation(P);
			P.charityThisTurn++;
			P.stats.charity++;
			const FCharityKind& K = *FindCharity(A.kind);
			V FactIds = V::Array();
			for (const TCHAR* Id : K.FactIds) FactIds.Add(V(Id));
			Out.entry = AddLog(State, V::Object({
				{ TEXT("type"), TEXT("charity") }, { TEXT("player"), P.id }, { TEXT("kind"), A.kind },
				{ TEXT("text"), FString::Printf(TEXT("%s: %s (%dƒ). +%d reputation."), *P.name, K.Label, Price, Gain) }, { TEXT("factIds"), FactIds } }));
		}
		else if (A.type == TEXT("marry"))
		{
			const int32 Price = Cost(State, TEXT("marriage"));
			const int32 Gain = D.Int(TEXT("gains.marriageFamily"));
			P.florins -= Price;
			P.family.Set(A.city, P.family.Get(A.city) + Gain);
			P.marriedThisTurn++;
			P.stats.married++;
			Out.entry = AddLog(State, V::Object({
				{ TEXT("type"), TEXT("marry") }, { TEXT("player"), P.id }, { TEXT("city"), A.city },
				{ TEXT("text"), FString::Printf(TEXT("%s celebrates a wedding in %s (%dƒ). +%d family member."), *P.name, *CityName(A.city), Price, Gain) },
				{ TEXT("factIds"), V::Array({ V(TEXT("SO-12")) }) } }));
		}
		else if (A.type == TEXT("land"))
		{
			const int32 Price = Cost(State, TEXT("buyLand"));
			P.florins -= Price;
			P.land.Add(A.city);
			P.stats.land++;
			Out.entry = AddLog(State, V::Object({
				{ TEXT("type"), TEXT("land") }, { TEXT("player"), P.id }, { TEXT("city"), A.city },
				{ TEXT("text"), FString::Printf(TEXT("%s buys abandoned fields near %s for %dƒ. They are worth %d Wealth points at the end, but workers must be paid %dƒ every half-year."),
					*P.name, *CityName(A.city), Price, D.Int(TEXT("scoring.pointsPerLand")), Cost(State, TEXT("landWage"))) },
				{ TEXT("factIds"), V::Array({ V(TEXT("EC-02")), V(TEXT("EC-10")) }) } }));
		}
		else if (A.type == TEXT("loan"))
		{
			bFree = true;
			const int32 Loan = D.Int(TEXT("gains.loan")), Repay = D.Int(TEXT("costs.loanRepay"));
			P.florins += Loan;
			P.loan = FPortsLoan{ Repay, UntilRound(State, 2) };
			P.stats.loans++;
			Out.entry = AddLog(State, V::Object({
				{ TEXT("type"), TEXT("loan") }, { TEXT("player"), P.id },
				{ TEXT("text"), FString::Printf(TEXT("%s borrows %dƒ. %dƒ is due in the plague phase of the next round."), *P.name, Loan, Repay) },
				{ TEXT("factIds"), V::Array({ V(TEXT("EC-01")), V(TEXT("EC-11")) }) } }));
		}
		else if (A.type == TEXT("deal"))
		{
			bFree = true;
			FPortsPlayer& Other = State.players[A.partner];
			P.proposedThisTurn = true;
			Other.pending.Add(V::Object({ { TEXT("kind"), TEXT("deal") }, { TEXT("from"), P.id } }));
			Out.entry = AddLog(State, V::Object({
				{ TEXT("type"), TEXT("deal") }, { TEXT("player"), P.id }, { TEXT("partner"), A.partner },
				{ TEXT("text"), FString::Printf(TEXT("%s proposes a partnership to %s, who will answer at the start of their next turn."), *P.name, *Other.name) },
				{ TEXT("factIds"), V::Array({ V(TEXT("TR-04")) }) } }));
		}
		else if (A.type == TEXT("gates"))
		{
			const int32 Rep = D.Int(TEXT("penalties.gatesReputation"));
			P.reputation -= Rep;
			ClampReputation(P);
			P.gates = FPortsGates{ A.city, UntilRound(State, D.Int(TEXT("limits.gatesRounds"))) };
			P.stats.gates++;
			Out.entry = AddLog(State, V::Object({
				{ TEXT("type"), TEXT("gates") }, { TEXT("player"), P.id }, { TEXT("city"), A.city },
				{ TEXT("text"), FString::Printf(TEXT("%s has guards posted at the gates of %s (−%d reputation). Until the end of next round, rival houses cannot open a post there and earn %dƒ less shipping to it."),
					*P.name, *CityName(A.city), Rep, D.Int(TEXT("penalties.gatesProfit"))) },
				{ TEXT("factIds"), V::Array({ V(TEXT("SO-11")) }) } }));
		}
		if (!bFree) P.ap -= ApCost(A.type);
		Out.ok = true;
		return Out;
	}

	// All legal ship moves for a house (used by the interface and bots).
	TArray<FPortsAction> LegalShipments(const FPortsState& State, const FPortsPlayer& P)
	{
		const FPortsData& D = FPortsData::Get();
		TArray<FPortsAction> Out;
		for (const FString& From : P.posts)
		{
			for (const int32 Index : D.RoutesFrom(From))
			{
				FPortsAction A;
				A.type = TEXT("ship");
				A.from = From;
				A.route = D.Routes[Index].Id;
				if (CheckShip(State, &P, A).IsEmpty()) Out.Add(A);
			}
		}
		return Out;
	}

	TArray<FPortsAction> LegalPosts(const FPortsState& State, const FPortsPlayer& P)
	{
		TArray<FString> Seen;
		TArray<FPortsAction> Out;
		for (const FString& Own : P.posts)
		{
			for (const FString& N : Neighbors(Own))
			{
				if (Seen.Contains(N)) continue;
				Seen.Add(N);
				FPortsAction A;
				A.type = TEXT("post");
				A.city = N;
				if (CheckPost(State, &P, A).IsEmpty()) Out.Add(A);
			}
		}
		return Out;
	}
}

FPortsValue FPortsAction::ToValue() const
{
	return Ports::FPortsAction_ToValueImpl(*this);
}

FPortsAction FPortsAction::FromValue(const FPortsValue& In)
{
	FPortsAction A;
	A.type = In.Get(TEXT("type")).AsString();
	A.from = In.Get(TEXT("from")).AsString();
	A.to = In.Get(TEXT("to")).AsString();
	A.route = In.Get(TEXT("route")).AsString();
	A.city = In.Get(TEXT("city")).AsString();
	A.kind = In.Get(TEXT("kind")).AsString();
	const FPortsValue& Count = In.Get(TEXT("count"));
	A.count = Count.AsInt();
	A.bCountIsWhole = Count.IsNumber() && Count.AsNumber() == FMath::FloorToDouble(Count.AsNumber());
	A.partner = In.Get(TEXT("partner")).IsNumber() ? In.Get(TEXT("partner")).AsInt() : -1;
	A.offshore = In.Get(TEXT("offshore")).Truthy();
	return A;
}
