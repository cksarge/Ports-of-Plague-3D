#include "PortsState.h"

using V = FPortsValue;

namespace Ports
{
	const TCHAR* const ESTATE = TEXT("estate");

	const FPortsPlayerStyle PLAYER_STYLES[6] = {
		{ TEXT("#0072B2"), TEXT("Lapis blue"), TEXT("circle") },
		{ TEXT("#D55E00"), TEXT("Vermilion"), TEXT("square") },
		{ TEXT("#009E73"), TEXT("Verdigris green"), TEXT("triangle") },
		{ TEXT("#CC79A7"), TEXT("Rose madder"), TEXT("diamond") },
		{ TEXT("#56B4E9"), TEXT("Sky blue"), TEXT("hexagon") },
		{ TEXT("#E69F00"), TEXT("Saffron"), TEXT("star") },
	};

	// ---------- rng.js ----------

	uint32 SeedFrom(const FString& Value)
	{
		uint32 H = 2166136261u;
		for (int32 i = 0; i < Value.Len(); i++)
		{
			const uint32 Unit = static_cast<uint16>(Value[i]);
			H = (H ^ Unit) * 16777619u;
			// JavaScript walks the string by whole characters and takes the first
			// half of a two-part one, so the second half is skipped.
			if (Unit >= 0xD800 && Unit <= 0xDBFF && i + 1 < Value.Len())
			{
				const uint32 Next = static_cast<uint16>(Value[i + 1]);
				if (Next >= 0xDC00 && Next <= 0xDFFF) i++;
			}
		}
		return H;
	}

	// mulberry32, on 32-bit whole numbers exactly as the JavaScript does.
	double NextRandom(FPortsState& State)
	{
		State.rng += 0x6d2b79f5u;
		uint32 T = State.rng;
		T = (T ^ (T >> 15)) * (T | 1u);
		T ^= T + (T ^ (T >> 7)) * (T | 61u);
		return static_cast<double>(T ^ (T >> 14)) / 4294967296.0;
	}

	int32 Roll(FPortsState& State, int32 Sides)
	{
		return 1 + static_cast<int32>(FMath::FloorToDouble(NextRandom(State) * Sides));
	}

	TArray<FString> Shuffle(FPortsState& State, const TArray<FString>& List)
	{
		TArray<FString> A = List;
		for (int32 i = A.Num() - 1; i > 0; i--)
		{
			const int32 j = static_cast<int32>(FMath::FloorToDouble(NextRandom(State) * (i + 1)));
			A.Swap(i, j);
		}
		return A;
	}

	int32 PickIndex(FPortsState& State, int32 Length)
	{
		return static_cast<int32>(FMath::FloorToDouble(NextRandom(State) * Length));
	}

	// ---------- state.js ----------

	const FString& OtherEnd(const FPortsRoute& Route, const FString& CityId)
	{
		return Route.A == CityId ? Route.B : Route.A;
	}

	TArray<FString> Neighbors(const FString& CityId)
	{
		const FPortsData& Data = FPortsData::Get();
		TArray<FString> Out;
		for (const int32 Index : Data.RoutesFrom(CityId)) Out.Add(OtherEnd(Data.Routes[Index], CityId));
		return Out;
	}

	FString ValidateSetup(const TArray<FPortsSetupPlayer>& Players)
	{
		const FPortsData& Data = FPortsData::Get();
		const int32 Min = Data.Int(TEXT("players.min")), Max = Data.Int(TEXT("players.max"));
		if (Players.Num() < Min || Players.Num() > Max) return FString::Printf(TEXT("Choose between %d and %d players."), Min, Max);
		TArray<FString> Homes;
		int32 Humans = 0;
		for (const FPortsSetupPlayer& P : Players)
		{
			if (P.name.TrimStartAndEnd().IsEmpty()) return TEXT("Every house needs a name.");
			if (!Data.HomeCities.Contains(P.home)) return FString::Printf(TEXT("%s is not one of the home cities."), *P.home);
			if (Homes.Contains(P.home)) return FString::Printf(TEXT("Two houses cannot share %s as a home city."), *Data.FindCity(P.home)->Name);
			Homes.Add(P.home);
			if (P.bot && !Data.Config().Get(TEXT("bots")).Get(TEXT("skills")).Get(P.skill).IsObject())
			{
				return FString::Printf(TEXT("Choose a skill level for %s."), *P.name.TrimStartAndEnd());
			}
		}
		for (const FPortsSetupPlayer& P : Players) if (!P.bot) Humans++;
		if (Humans < Data.Int(TEXT("bots.minHumans"))) return TEXT("At least one house must be played by a person.");
		return FString();
	}

	FPortsEffects EmptyEffects()
	{
		return FPortsEffects();
	}

	FPortsValue AddLog(FPortsState& State, const FPortsValue& Entry)
	{
		V E = V::Object({ { TEXT("seq"), ++State.logSeq }, { TEXT("round"), State.round } });
		for (int32 i = 0; i < Entry.GetKeys().Num(); i++) E.Set(Entry.GetKeys()[i], Entry.ValueAt(i));
		State.log.Add(E);
		for (const FPortsValue& Id : Entry.Get(TEXT("factIds")).GetItems()) State.journal.AddUnique(Id.AsString());
		return E;
	}

	bool CreateGame(const FPortsSetup& Setup, FPortsState& State, FString& OutProblem)
	{
		if (!FPortsData::EnsureLoaded())
		{
			OutProblem = TEXT("The game's data files could not be loaded.");
			return false;
		}
		const FPortsData& Data = FPortsData::Get();
		OutProblem = ValidateSetup(Setup.players);
		if (!OutProblem.IsEmpty()) return false;

		const FPortsValue& Config = Data.Config();
		const FString& Mode = Setup.mode;
		const int32 PreRounds = Setup.prePlague ? Config.Get(TEXT("prePlague")).Get(TEXT("rounds")).Get(Mode).AsInt(0) : 0;
		const int32 Start = PreRounds ? 1 - Data.Int(TEXT("prePlague.halves")) : 1;

		State = FPortsState();
		State.version = SAVE_VERSION;
		State.title = Config.Get(TEXT("title")).AsString();
		State.seed = Setup.seed;
		State.rng = SeedFrom(Setup.seed);
		State.difficulty = Setup.difficulty;
		State.mode = Mode;
		const FPortsValue& Modes = Config.Get(TEXT("modes"));
		State.span = (Modes.Get(Mode).IsObject() ? Modes.Get(Mode) : Modes.Get(TEXT("standard"))).Get(TEXT("span")).AsInt();
		State.firstHalf = Start;
		State.preRounds = PreRounds;
		State.turnSeconds = !Setup.timer ? 0 : Setup.timerSeconds > 0 ? Setup.timerSeconds : Data.Int(TEXT("turnTimer.seconds"));
		State.round = Start - 1;
		State.roundEnd = Start - 1;
		State.phase = TEXT("roundStart");
		State.effects = EmptyEffects();

		for (int32 i = 0; i < Setup.players.Num(); i++)
		{
			const FPortsSetupPlayer& In = Setup.players[i];
			const FPortsValue& Home = Data.FindCity(In.home)->Raw.Get(TEXT("home"));
			const FPortsPlayerStyle& Style = PLAYER_STYLES[i];
			FPortsPlayer P;
			P.id = i;
			P.name = In.name.TrimStartAndEnd();
			P.color = In.color.IsEmpty() ? FString(Style.color) : In.color;
			P.colorName = In.colorName.IsEmpty() ? FString(Style.colorName) : In.colorName;
			P.crest = In.crest.IsEmpty() ? FString(Style.crest) : In.crest;
			P.home = In.home;
			P.bot = In.bot;
			P.skill = In.bot ? In.skill : FString();
			if (!In.strategy.IsEmpty()) P.strategy = In.strategy;
			else if (In.bot)
			{
				// A bot house's playing style: drawn from its skill level's list.
				const FPortsValue& List = Config.Get(TEXT("bots")).Get(TEXT("skills")).Get(In.skill).Get(TEXT("strategies"));
				P.strategy = List[PickIndex(State, List.Num())].AsString();
			}
			const FPortsValue& StartFlorins = Home.Get(TEXT("startFlorins"));
			const FPortsValue& QuickFlorins = Home.Get(TEXT("quickStartFlorins"));
			const int32 Bonus = Mode == TEXT("quick") ? (!QuickFlorins.IsMissing() ? QuickFlorins.AsInt() : StartFlorins.AsInt(0)) : StartFlorins.AsInt(0);
			P.florins = Data.Int(TEXT("start.florins")) + Bonus;
			P.reputation = Data.Int(TEXT("start.reputation")) + Home.Get(TEXT("startReputation")).AsInt(0);
			P.family.Set(In.home, Data.Int(TEXT("start.family")));
			P.posts.Add(In.home);
			State.players.Add(MoveTemp(P));
		}

		State.cities.SetNum(Data.Cities.Num());
		TArray<FString> Ids;
		for (const FPortsValue& Card : Data.Deck().GetItems()) Ids.Add(Card.Get(TEXT("id")).AsString());
		State.deck = Shuffle(State, Ids);
		Ids.Reset();
		for (const FPortsValue& Card : Data.Fortune().GetItems()) Ids.Add(Card.Get(TEXT("id")).AsString());
		State.fortuneDeck = Shuffle(State, Ids);
		// Caffa is already Stricken when the game begins (the siege of 1346).
		for (int32 i = 0; i < Data.Cities.Num(); i++)
		{
			if (Data.Cities[i].ArrivalRound == 0)
			{
				State.cities[i].state = TEXT("stricken");
				State.cities[i].severity = 3;
				State.cities[i].strickenFor = 0;
			}
		}
		const FPortsValue& Prologue = Data.Timeline().Get(TEXT("prologue"));
		AddLog(State, V::Object({ { TEXT("type"), TEXT("prologue") }, { TEXT("text"), Prologue.Get(TEXT("text")) }, { TEXT("factIds"), Prologue.Get(TEXT("factIds")) } }));
		RollTurnOrder(State);
		return true;
	}

	// Before the game each house rolls a die: highest goes first; tied houses
	// roll again. The order then stays the same for the whole game.
	static TArray<int32> PlaceHouses(FPortsState& State, const TArray<int32>& Ids, FPortsValue& Rounds)
	{
		if (Ids.Num() == 1) return Ids;
		struct FRoll { int32 Player; int32 Die; };
		TArray<FRoll> Rolls;
		V RollsValue = V::Array();
		const int32 Sides = FPortsData::Get().Int(TEXT("turnOrderDie"));
		for (const int32 Id : Ids)
		{
			const int32 Die = Roll(State, Sides);
			Rolls.Add({ Id, Die });
			RollsValue.Add(V::Object({ { TEXT("player"), Id }, { TEXT("die"), Die } }));
		}
		Rounds.Add(RollsValue);
		TArray<int32> Values;
		for (const FRoll& R : Rolls) Values.AddUnique(R.Die);
		Values.Sort([](int32 A, int32 B) { return A > B; });
		TArray<int32> Out;
		for (const int32 Value : Values)
		{
			TArray<int32> Tied;
			for (const FRoll& R : Rolls) if (R.Die == Value) Tied.Add(R.Player);
			Out.Append(PlaceHouses(State, Tied, Rounds));
		}
		return Out;
	}

	void RollTurnOrder(FPortsState& State)
	{
		V Rounds = V::Array();
		TArray<int32> Ids;
		for (const FPortsPlayer& P : State.players) Ids.Add(P.id);
		State.order = PlaceHouses(State, Ids, Rounds);
		TArray<FString> Names;
		for (const int32 Id : State.order) Names.Add(State.players[Id].name);
		AddLog(State, V::Object({
			{ TEXT("type"), TEXT("orderRoll") }, { TEXT("rolls"), Rounds }, { TEXT("order"), V::Ints(State.order) },
			{ TEXT("text"), FString::Printf(TEXT("Turn order (fixed for the whole game): %s."), *FString::Join(Names, TEXT(", "))) } }));
	}

	FPortsPlayer* CurrentPlayer(FPortsState& State)
	{
		if (State.phase != TEXT("actions") || !State.order.IsValidIndex(State.turn)) return nullptr;
		return &State.players[State.order[State.turn]];
	}

	const FPortsPlayer* CurrentPlayer(const FPortsState& State)
	{
		return CurrentPlayer(const_cast<FPortsState&>(State));
	}

	int32 FamilyTotal(const FPortsPlayer& P)
	{
		int32 Total = 0;
		for (const int32 N : P.family.Values) Total += N;
		return Total;
	}

	int32 FamilyAt(const FPortsPlayer& P, const FString& Loc)
	{
		return P.family.Get(Loc, 0);
	}

	TArray<FString> FamilyLocations(const FPortsPlayer& P)
	{
		TArray<FString> Out;
		for (int32 i = 0; i < P.family.Num(); i++) if (P.family.Values[i] > 0) Out.Add(P.family.Keys[i]);
		return Out;
	}

	bool IsStricken(const FPortsState& State, const FString& CityId)
	{
		const FPortsCityState* C = State.City(CityId);
		return C && C->state == TEXT("stricken");
	}

	bool IsAftermath(const FPortsState& State, const FString& CityId)
	{
		const FPortsCityState* C = State.City(CityId);
		return C && C->state == TEXT("aftermath");
	}

	bool IsThreatened(const FPortsState& State, const FString& CityId)
	{
		const FPortsCityState* C = State.City(CityId);
		if (!C || C->state != TEXT("safe")) return false;
		for (const FString& N : Neighbors(CityId)) if (IsStricken(State, N)) return true;
		return false;
	}

	const FPortsPlayer* GatesClosedBy(const FPortsState& State, const FString& CityId, const FPortsPlayer* P)
	{
		for (const FPortsPlayer& O : State.players)
		{
			if (&O != P && O.gates.IsSet() && O.gates->city == CityId) return &O;
		}
		return nullptr;
	}

	int32 UntilRound(const FPortsState& State, int32 Rounds)
	{
		return State.roundEnd + State.span * (Rounds - 1);
	}

	bool IsPrePlague(const FPortsState& State)
	{
		return State.round <= 0 && State.round >= State.firstHalf;
	}

	double PreSpan(const FPortsState& State)
	{
		return State.preRounds ? static_cast<double>(1 - State.firstHalf) / State.preRounds : 1.0;
	}

	const FPortsValue& HalfInfo(int32 Half)
	{
		static const FPortsValue None;
		const FPortsValue& Timeline = FPortsData::Get().Timeline();
		if (Half >= 1) return Timeline.Get(TEXT("rounds"))[Half - 1];
		for (const FPortsValue& R : Timeline.Get(TEXT("prePlague")).GetItems()) if (R.Get(TEXT("round")).AsInt() == Half) return R;
		return None;
	}

	void ClampReputation(FPortsPlayer& P)
	{
		const FPortsData& Data = FPortsData::Get();
		P.reputation = FMath::Max(Data.Int(TEXT("limits.minReputation")), FMath::Min(Data.Int(TEXT("limits.maxReputation")), P.reputation));
	}

	int32 Cost(const FPortsState& State, const FString& Key, const FPortsPlayer* P)
	{
		const FPortsData& Data = FPortsData::Get();
		const int32 Pre = Key == TEXT("openPost") && IsPrePlague(State) ? -Data.Int(TEXT("prePlague.postDiscount")) : 0;
		return FMath::Max(0, Data.Int(*(TEXT("costs.") + Key)) + Pre + State.effects.costs.Get(Key, 0) + (P ? P->personalCosts.Get(Key, 0) : 0));
	}

	int32 ApCost(const FString& Type)
	{
		double Value = 1;
		FPortsData::Get().TryNumber(TEXT("actionPointCosts.") + Type, Value);
		return static_cast<int32>(Value);
	}

	FString DifficultyPath(const FPortsState& State)
	{
		const bool bKnown = FPortsData::Get().Config().Get(TEXT("difficulty")).Get(State.difficulty).IsObject();
		return TEXT("difficulty.") + (bKnown ? State.difficulty : FString(TEXT("chronicler")));
	}

	FString ModePath(const FPortsState& State)
	{
		const bool bKnown = FPortsData::Get().Config().Get(TEXT("modes")).Get(State.mode).IsObject();
		return TEXT("modes.") + (bKnown ? State.mode : FString(TEXT("standard")));
	}

	int32 TotalRounds(const FPortsState& State)
	{
		return State.preRounds + FMath::CeilToInt32(static_cast<double>(FPortsData::Get().Int(TEXT("rounds"))) / State.span);
	}

	int32 RoundNumber(const FPortsState& State)
	{
		if (State.round <= 0) return FMath::Max(1, FMath::CeilToInt32((State.round - State.firstHalf + 1) / PreSpan(State)));
		return State.preRounds + FMath::CeilToInt32(static_cast<double>(State.round) / State.span);
	}

	// Label for the current round. In Quick Play a round covers several half-years.
	FPortsRoundInfo RoundInfo(const FPortsState& State)
	{
		FPortsRoundInfo Info;
		if (State.round < State.firstHalf) return Info;
		TArray<const FPortsValue*> Halves;
		for (int32 H = State.round; H <= FMath::Max(State.round, State.roundEnd); H++) Halves.Add(&HalfInfo(H));
		const FPortsValue& First = *Halves[0];
		const FPortsValue& Last = *Halves.Last();
		if (!First.IsObject()) return Info;
		Info.bValid = true;
		Info.pre = State.round <= 0;
		Info.round = First.Get(TEXT("round")).AsInt();
		if (Halves.Num() == 1)
		{
			Info.label = First.Get(TEXT("label")).AsString();
			Info.months = First.Get(TEXT("months")).AsString();
			Info.season = First.Get(TEXT("season")).AsString();
			Info.headline = First.Get(TEXT("headline")).AsString();
			Info.factIds = First.Get(TEXT("factIds"));
			return Info;
		}
		const auto Split = [](const FString& Text, const TCHAR* By)
		{
			TArray<FString> Parts;
			Text.ParseIntoArray(Parts, By, false);
			return Parts;
		};
		const FString FirstLabel = First.Get(TEXT("label")).AsString(), LastLabel = Last.Get(TEXT("label")).AsString();
		const FString FirstMonths = First.Get(TEXT("months")).AsString(), LastMonths = Last.Get(TEXT("months")).AsString();
		const auto Year = [&Split](const FString& Label) { const TArray<FString> Parts = Split(Label, TEXT(" ")); return Parts.IsValidIndex(1) ? Parts[1] : FString(); };
		Info.label = Year(FirstLabel) == Year(LastLabel) ? Year(FirstLabel) : FString::Printf(TEXT("%s – %s"), *FirstLabel, *LastLabel);
		const TArray<FString> LastParts = Split(LastMonths, TEXT("–"));
		Info.months = FString::Printf(TEXT("%s %s – %s"), *Split(FirstMonths, TEXT("–"))[0], *Split(FirstMonths, TEXT(" ")).Last(), LastParts.IsValidIndex(1) ? *LastParts[1] : TEXT(""));
		TArray<FString> Headlines;
		Info.factIds = V::Array();
		for (const FPortsValue* H : Halves)
		{
			Headlines.Add(H->Get(TEXT("headline")).AsString());
			for (const FPortsValue& Id : H->Get(TEXT("factIds")).GetItems()) Info.factIds.Add(Id);
		}
		Info.headline = FString::Join(Headlines, TEXT(" "));
		return Info;
	}
}

// ---------- The state as JSON ----------

FPortsCityState* FPortsState::City(const FString& Id)
{
	const int32 Index = FPortsData::Get().CityIndexOf(Id);
	return cities.IsValidIndex(Index) ? &cities[Index] : nullptr;
}

const FPortsCityState* FPortsState::City(const FString& Id) const
{
	const int32 Index = FPortsData::Get().CityIndexOf(Id);
	return cities.IsValidIndex(Index) ? &cities[Index] : nullptr;
}

namespace
{
	V StringOrNull(const FString& Text) { return Text.IsEmpty() ? V::Null() : V(Text); }

	template <typename T>
	V MapToValue(const TPortsMap<T>& Map)
	{
		V Out = V::Object();
		for (int32 i = 0; i < Map.Num(); i++) Out.Set(Map.Keys[i], V(Map.Values[i]));
		return Out;
	}

	void MapFromValue(const V& In, TPortsMap<int32>& Map)
	{
		Map.Reset();
		for (int32 i = 0; i < In.GetKeys().Num(); i++) Map.Set(In.GetKeys()[i], In.ValueAt(i).AsInt());
	}

	void MapFromValue(const V& In, TPortsMap<bool>& Map)
	{
		Map.Reset();
		for (int32 i = 0; i < In.GetKeys().Num(); i++) Map.Set(In.GetKeys()[i], In.ValueAt(i).Truthy());
	}

	V RouteTypesToValue(const FPortsByRouteType& In)
	{
		return V::Object({ { TEXT("all"), In.all }, { TEXT("sea"), In.sea }, { TEXT("land"), In.land } });
	}

	FPortsByRouteType RouteTypesFromValue(const V& In)
	{
		FPortsByRouteType Out;
		Out.all = In.Get(TEXT("all")).AsInt();
		Out.sea = In.Get(TEXT("sea")).AsInt();
		Out.land = In.Get(TEXT("land")).AsInt();
		return Out;
	}

	V PlayerToValue(const FPortsPlayer& P)
	{
		const FPortsStats& S = P.stats;
		return V::Object({
			{ TEXT("id"), P.id }, { TEXT("name"), P.name }, { TEXT("color"), P.color }, { TEXT("colorName"), P.colorName }, { TEXT("crest"), P.crest },
			{ TEXT("home"), P.home }, { TEXT("bot"), P.bot }, { TEXT("skill"), StringOrNull(P.skill) }, { TEXT("strategy"), StringOrNull(P.strategy) },
			{ TEXT("florins"), P.florins }, { TEXT("reputation"), P.reputation }, { TEXT("family"), MapToValue(P.family) }, { TEXT("posts"), V::Strings(P.posts) },
			{ TEXT("lostFamily"), P.lostFamily }, { TEXT("offers"), MapToValue(P.offers) },
			{ TEXT("pending"), [&P]() { V A = V::Array(); for (const V& D : P.pending) A.Add(D); return A; }() },
			{ TEXT("ap"), P.ap }, { TEXT("shipped"), V::Strings(P.shipped) }, { TEXT("prepared"), V::Strings(P.prepared) }, { TEXT("physician"), V::Strings(P.physician) },
			{ TEXT("charityThisTurn"), P.charityThisTurn }, { TEXT("free"), MapToValue(P.free) },
			{ TEXT("nextShip"), P.nextShip.IsSet() ? V::Object({ { TEXT("profit"), P.nextShip->profit }, { TEXT("safe"), P.nextShip->safe } }) : V::Null() },
			{ TEXT("personalCosts"), MapToValue(P.personalCosts) }, { TEXT("englishBlocked"), P.englishBlocked },
			{ TEXT("marriedThisTurn"), P.marriedThisTurn }, { TEXT("proposedThisTurn"), P.proposedThisTurn }, { TEXT("land"), V::Strings(P.land) },
			{ TEXT("loan"), P.loan.IsSet() ? V::Object({ { TEXT("owed"), P.loan->owed }, { TEXT("due"), P.loan->due } }) : V::Null() },
			{ TEXT("deal"), P.deal.IsSet() ? V::Object({ { TEXT("partner"), P.deal->partner }, { TEXT("until"), P.deal->until } }) : V::Null() },
			{ TEXT("gates"), P.gates.IsSet() ? V::Object({ { TEXT("city"), P.gates->city }, { TEXT("until"), P.gates->until } }) : V::Null() },
			{ TEXT("stats"), V::Object({
				{ TEXT("shipments"), S.shipments }, { TEXT("infected"), S.infected }, { TEXT("earned"), S.earned }, { TEXT("fled"), S.fled },
				{ TEXT("protected"), S.protectedCount }, { TEXT("charity"), S.charity }, { TEXT("spread"), S.spread }, { TEXT("fortune"), S.fortune },
				{ TEXT("married"), S.married }, { TEXT("land"), S.land }, { TEXT("loans"), S.loans }, { TEXT("defaults"), S.defaults },
				{ TEXT("deals"), S.deals }, { TEXT("gates"), S.gates }, { TEXT("offshore"), S.offshore } }) },
		});
	}

	FPortsPlayer PlayerFromValue(const V& In)
	{
		FPortsPlayer P;
		P.id = In.Get(TEXT("id")).AsInt();
		P.name = In.Get(TEXT("name")).AsString();
		P.color = In.Get(TEXT("color")).AsString();
		P.colorName = In.Get(TEXT("colorName")).AsString();
		P.crest = In.Get(TEXT("crest")).AsString();
		P.home = In.Get(TEXT("home")).AsString();
		P.bot = In.Get(TEXT("bot")).Truthy();
		P.skill = In.Get(TEXT("skill")).AsString();
		P.strategy = In.Get(TEXT("strategy")).AsString();
		P.florins = In.Get(TEXT("florins")).AsInt();
		P.reputation = In.Get(TEXT("reputation")).AsInt();
		MapFromValue(In.Get(TEXT("family")), P.family);
		P.posts = In.Get(TEXT("posts")).ToStrings();
		P.lostFamily = In.Get(TEXT("lostFamily")).AsInt();
		MapFromValue(In.Get(TEXT("offers")), P.offers);
		P.pending = In.Get(TEXT("pending")).GetItems();
		P.ap = In.Get(TEXT("ap")).AsInt();
		P.shipped = In.Get(TEXT("shipped")).ToStrings();
		P.prepared = In.Get(TEXT("prepared")).ToStrings();
		P.physician = In.Get(TEXT("physician")).ToStrings();
		P.charityThisTurn = In.Get(TEXT("charityThisTurn")).AsInt();
		MapFromValue(In.Get(TEXT("free")), P.free);
		const V& Next = In.Get(TEXT("nextShip"));
		if (Next.IsObject()) P.nextShip = FPortsNextShip{ Next.Get(TEXT("profit")).AsInt(), Next.Get(TEXT("safe")).Truthy() };
		MapFromValue(In.Get(TEXT("personalCosts")), P.personalCosts);
		P.englishBlocked = In.Get(TEXT("englishBlocked")).Truthy();
		P.marriedThisTurn = In.Get(TEXT("marriedThisTurn")).AsInt();
		P.proposedThisTurn = In.Get(TEXT("proposedThisTurn")).Truthy();
		P.land = In.Get(TEXT("land")).ToStrings();
		const V& Loan = In.Get(TEXT("loan"));
		if (Loan.IsObject()) P.loan = FPortsLoan{ Loan.Get(TEXT("owed")).AsInt(), Loan.Get(TEXT("due")).AsInt() };
		const V& Deal = In.Get(TEXT("deal"));
		if (Deal.IsObject()) P.deal = FPortsDeal{ Deal.Get(TEXT("partner")).AsInt(), Deal.Get(TEXT("until")).AsInt() };
		const V& Gates = In.Get(TEXT("gates"));
		if (Gates.IsObject()) P.gates = FPortsGates{ Gates.Get(TEXT("city")).AsString(), Gates.Get(TEXT("until")).AsInt() };
		const V& S = In.Get(TEXT("stats"));
		P.stats.shipments = S.Get(TEXT("shipments")).AsInt();
		P.stats.infected = S.Get(TEXT("infected")).AsInt();
		P.stats.earned = S.Get(TEXT("earned")).AsInt();
		P.stats.fled = S.Get(TEXT("fled")).AsInt();
		P.stats.protectedCount = S.Get(TEXT("protected")).AsInt();
		P.stats.charity = S.Get(TEXT("charity")).AsInt();
		P.stats.spread = S.Get(TEXT("spread")).AsInt();
		P.stats.fortune = S.Get(TEXT("fortune")).AsInt();
		P.stats.married = S.Get(TEXT("married")).AsInt();
		P.stats.land = S.Get(TEXT("land")).AsInt();
		P.stats.loans = S.Get(TEXT("loans")).AsInt();
		P.stats.defaults = S.Get(TEXT("defaults")).AsInt();
		P.stats.deals = S.Get(TEXT("deals")).AsInt();
		P.stats.gates = S.Get(TEXT("gates")).AsInt();
		P.stats.offshore = S.Get(TEXT("offshore")).AsInt();
		return P;
	}
}

FPortsValue FPortsState::ToValue() const
{
	const FPortsData& Data = FPortsData::Get();
	V Players = V::Array();
	for (const FPortsPlayer& P : players) Players.Add(PlayerToValue(P));
	V Cities = V::Object();
	for (int32 i = 0; i < cities.Num() && i < Data.Cities.Num(); i++)
	{
		const FPortsCityState& C = cities[i];
		Cities.Set(Data.Cities[i].Id, V::Object({ { TEXT("state"), C.state }, { TEXT("severity"), C.severity }, { TEXT("strickenFor"), C.strickenFor }, { TEXT("early"), C.early }, { TEXT("unrest"), C.unrest } }));
	}
	V CityProfit = V::Array();
	for (const FPortsCityProfit& M : effects.cityProfit)
	{
		CityProfit.Add(V::Object({ { TEXT("cities"), V::Strings(M.cities) }, { TEXT("onlyStricken"), M.onlyStricken }, { TEXT("profit"), M.profit } }));
	}
	V Log = V::Array();
	for (const V& E : log) Log.Add(E);
	V Scores = V::Null();
	if (finalScores.IsSet())
	{
		Scores = V::Array();
		for (const FPortsRank& R : *finalScores)
		{
			Scores.Add(V::Object({ { TEXT("id"), R.id }, { TEXT("name"), R.name }, { TEXT("wealth"), R.wealth }, { TEXT("family"), R.family }, { TEXT("reputation"), R.reputation },
				{ TEXT("balance"), R.balance }, { TEXT("total"), R.total }, { TEXT("familyCount"), R.familyCount }, { TEXT("rep"), R.rep }, { TEXT("place"), R.place } }));
		}
	}
	V Out = V::Object({
		{ TEXT("version"), version }, { TEXT("title"), title }, { TEXT("seed"), seed }, { TEXT("rng"), rng }, { TEXT("difficulty"), difficulty }, { TEXT("mode"), mode },
		{ TEXT("span"), span }, { TEXT("firstHalf"), firstHalf }, { TEXT("preRounds"), preRounds }, { TEXT("turnSeconds"), turnSeconds },
		{ TEXT("round"), round }, { TEXT("roundEnd"), roundEnd }, { TEXT("phase"), phase }, { TEXT("players"), Players }, { TEXT("order"), V::Ints(order) }, { TEXT("turn"), turn },
		{ TEXT("cities"), Cities }, { TEXT("deck"), V::Strings(deck) }, { TEXT("fortuneDeck"), V::Strings(fortuneDeck) },
		{ TEXT("currentEvent"), StringOrNull(currentEvent) }, { TEXT("currentChronicle"), V::Strings(currentChronicle) },
		{ TEXT("effects"), V::Object({
			{ TEXT("profit"), RouteTypesToValue(effects.profit) }, { TEXT("contagion"), RouteTypesToValue(effects.contagion) }, { TEXT("costs"), MapToValue(effects.costs) },
			{ TEXT("charityBonus"), effects.charityBonus }, { TEXT("marketBonus"), effects.marketBonus }, { TEXT("noNewPostsNearPlague"), effects.noNewPostsNearPlague },
			{ TEXT("cityProfit"), CityProfit } }) },
		{ TEXT("persecution"), persecution.IsSet() ? V::Object({ { TEXT("city"), persecution->city }, { TEXT("round"), persecution->round }, { TEXT("protectors"), V::Ints(persecution->protectors) } }) : V::Null() },
		{ TEXT("log"), Log }, { TEXT("logSeq"), logSeq }, { TEXT("journal"), V::Strings(journal) },
		{ TEXT("winner"), winner.IsSet() ? V::Ints(*winner) : V::Null() }, { TEXT("finalScores"), Scores },
	});
	if (bHasGuildFavor) Out.Set(TEXT("guildFavor"), guildFavor.IsSet() ? V(*guildFavor) : V::Null());
	if (bHasMidLast) Out.Set(TEXT("midLast"), midLast);
	return Out;
}

bool FPortsState::FromValue(const FPortsValue& In, FPortsState& Out)
{
	if (!In.IsObject() || !FPortsData::EnsureLoaded()) return false;
	const FPortsData& Data = FPortsData::Get();
	Out = FPortsState();
	Out.version = In.Get(TEXT("version")).AsInt();
	Out.title = In.Get(TEXT("title")).AsString();
	Out.seed = In.Get(TEXT("seed")).AsString();
	Out.rng = static_cast<uint32>(In.Get(TEXT("rng")).AsNumber());
	Out.difficulty = In.Get(TEXT("difficulty")).AsString();
	Out.mode = In.Get(TEXT("mode")).AsString();
	Out.span = In.Get(TEXT("span")).AsInt(1);
	Out.firstHalf = In.Get(TEXT("firstHalf")).AsInt(1);
	Out.preRounds = In.Get(TEXT("preRounds")).AsInt();
	Out.turnSeconds = In.Get(TEXT("turnSeconds")).AsInt();
	Out.round = In.Get(TEXT("round")).AsInt();
	Out.roundEnd = In.Get(TEXT("roundEnd")).AsInt();
	Out.phase = In.Get(TEXT("phase")).AsString();
	for (const V& P : In.Get(TEXT("players")).GetItems()) Out.players.Add(PlayerFromValue(P));
	Out.order = In.Get(TEXT("order")).ToInts();
	Out.turn = In.Get(TEXT("turn")).AsInt();
	Out.cities.SetNum(Data.Cities.Num());
	const V& Cities = In.Get(TEXT("cities"));
	for (int32 i = 0; i < Data.Cities.Num(); i++)
	{
		const V& C = Cities.Get(Data.Cities[i].Id);
		if (!C.IsObject()) continue;
		Out.cities[i].state = C.Get(TEXT("state")).AsString();
		Out.cities[i].severity = C.Get(TEXT("severity")).AsInt();
		Out.cities[i].strickenFor = C.Get(TEXT("strickenFor")).AsInt();
		Out.cities[i].early = C.Get(TEXT("early")).Truthy();
		Out.cities[i].unrest = C.Get(TEXT("unrest")).AsInt();
	}
	Out.deck = In.Get(TEXT("deck")).ToStrings();
	Out.fortuneDeck = In.Get(TEXT("fortuneDeck")).ToStrings();
	Out.currentEvent = In.Get(TEXT("currentEvent")).AsString();
	Out.currentChronicle = In.Get(TEXT("currentChronicle")).ToStrings();
	const V& Fx = In.Get(TEXT("effects"));
	Out.effects.profit = RouteTypesFromValue(Fx.Get(TEXT("profit")));
	Out.effects.contagion = RouteTypesFromValue(Fx.Get(TEXT("contagion")));
	MapFromValue(Fx.Get(TEXT("costs")), Out.effects.costs);
	Out.effects.charityBonus = Fx.Get(TEXT("charityBonus")).AsInt();
	Out.effects.marketBonus = Fx.Get(TEXT("marketBonus")).AsInt();
	Out.effects.noNewPostsNearPlague = Fx.Get(TEXT("noNewPostsNearPlague")).Truthy();
	for (const V& M : Fx.Get(TEXT("cityProfit")).GetItems())
	{
		Out.effects.cityProfit.Add({ M.Get(TEXT("cities")).ToStrings(), M.Get(TEXT("onlyStricken")).Truthy(), M.Get(TEXT("profit")).AsInt() });
	}
	const V& Persecution = In.Get(TEXT("persecution"));
	if (Persecution.IsObject()) Out.persecution = FPortsPersecution{ Persecution.Get(TEXT("city")).AsString(), Persecution.Get(TEXT("round")).AsInt(), Persecution.Get(TEXT("protectors")).ToInts() };
	Out.log = In.Get(TEXT("log")).GetItems();
	Out.logSeq = In.Get(TEXT("logSeq")).AsInt();
	Out.journal = In.Get(TEXT("journal")).ToStrings();
	if (In.Get(TEXT("winner")).IsArray()) Out.winner = In.Get(TEXT("winner")).ToInts();
	if (In.Get(TEXT("finalScores")).IsArray())
	{
		TArray<FPortsRank> Rows;
		for (const V& R : In.Get(TEXT("finalScores")).GetItems())
		{
			FPortsRank Row;
			Row.id = R.Get(TEXT("id")).AsInt();
			Row.name = R.Get(TEXT("name")).AsString();
			Row.wealth = R.Get(TEXT("wealth")).AsInt();
			Row.family = R.Get(TEXT("family")).AsInt();
			Row.reputation = R.Get(TEXT("reputation")).AsInt();
			Row.balance = R.Get(TEXT("balance")).AsInt();
			Row.total = R.Get(TEXT("total")).AsInt();
			Row.familyCount = R.Get(TEXT("familyCount")).AsInt();
			Row.rep = R.Get(TEXT("rep")).AsInt();
			Row.place = R.Get(TEXT("place")).AsInt();
			Rows.Add(Row);
		}
		Out.finalScores = Rows;
	}
	if (In.Has(TEXT("guildFavor")))
	{
		Out.bHasGuildFavor = true;
		if (In.Get(TEXT("guildFavor")).IsNumber()) Out.guildFavor = In.Get(TEXT("guildFavor")).AsInt();
	}
	if (In.Has(TEXT("midLast")))
	{
		Out.bHasMidLast = true;
		Out.midLast = In.Get(TEXT("midLast")).AsInt();
	}
	return true;
}

bool FPortsState::FromJson(const FString& Json, FPortsState& Out)
{
	FPortsValue Value;
	return FPortsValue::Parse(Json, Value) && FromValue(Value, Out);
}
