// The round and turn sequence:
//   roundStart -> chronicle -> event -> actions (each player) -> plague -> next round ... -> ended
// A round is half a year (Standard) or a year and a half (Quick Play: three
// half-years at once). The optional pre-plague rounds come first: no event
// card and no plague phase, only upkeep.
#include "PortsRules.h"

using V = FPortsValue;

namespace Ports
{
	int32 ActionPointsFor(const FPortsState& State, const FPortsPlayer& P)
	{
		const FPortsData& D = FPortsData::Get();
		int32 Ap = D.Int(*(ModePath(State) + TEXT(".actionPoints")));
		if (State.guildFavor.IsSet() && *State.guildFavor == P.id) Ap += D.Int(TEXT("comeback.guildFavorAP"));
		return Ap;
	}

	// Guild's Favor: from the set round, the last-place house gets +1 AP, but
	// only if it trails the leader by at least `guildFavorMinGap` Legacy points.
	static TOptional<int32> GuildFavorFor(const FPortsState& State)
	{
		const FPortsData& D = FPortsData::Get();
		if (State.round < D.Int(TEXT("comeback.guildFavorFromRound"))) return {};
		const int32 Last = LastPlaceId(State);
		int32 Leader = MIN_int32;
		for (const FPortsPlayer& P : State.players) Leader = FMath::Max(Leader, ScorePlayer(P).total);
		double MinGap = 0;
		D.TryNumber(TEXT("comeback.guildFavorMinGap"), MinGap);
		if (Leader - ScorePlayer(State.players[Last]).total >= static_cast<int32>(MinGap)) return Last;
		return {};
	}

	// Half-years covered by the current round.
	TArray<int32> HalvesOfRound(const FPortsState& State)
	{
		TArray<int32> Out;
		for (int32 H = State.round; H <= State.roundEnd; H++) Out.Add(H);
		return Out;
	}

	static void BeginTurn(FPortsState& State)
	{
		FPortsPlayer& P = *CurrentPlayer(State);
		P.ap = ActionPointsFor(State, P);
		P.charityThisTurn = 0;
		P.marriedThisTurn = 0;
		P.proposedThisTurn = false;
		const bool bFavor = State.guildFavor.IsSet() && *State.guildFavor == P.id;
		AddLog(State, V::Object({
			{ TEXT("type"), TEXT("turn") }, { TEXT("player"), P.id },
			{ TEXT("text"), FString::Printf(TEXT("%s's turn (%d action points%s)."), *P.name, P.ap, bFavor ? TEXT(", including Guild’s Favor") : TEXT("")) } }));
	}

	static void StartRound(FPortsState& State)
	{
		const FPortsData& D = FPortsData::Get();
		const int32 Rounds = D.Int(TEXT("rounds"));
		State.round = State.roundEnd + 1;
		const int32 Span = State.round <= 0 ? static_cast<int32>(PreSpan(State)) : State.span;
		State.roundEnd = FMath::Min(Rounds, State.round + Span - 1);
		State.effects = EmptyEffects();
		State.currentEvent.Reset();
		State.persecution.Reset();
		for (FPortsPlayer& P : State.players)
		{
			// Partnership offers wait for their answer across rounds; everything else is per round.
			P.shipped.Reset();
			P.prepared.Reset();
			P.physician.Reset();
			P.free.Reset();
			P.englishBlocked = false;
			P.pending.RemoveAll([](const FPortsValue& Dec) { return Dec.Get(TEXT("kind")).AsString() != TEXT("deal"); });
			P.ap = 0;
			P.nextShip.Reset();
			P.personalCosts.Reset();
		}
		State.guildFavor = GuildFavorFor(State);
		State.bHasGuildFavor = true;
		// Remember who was last at the halfway point (used to measure comebacks).
		if (HalvesOfRound(State).Contains(Rounds / 2 + 1))
		{
			State.midLast = LastPlaceId(State);
			State.bHasMidLast = true;
		}
		const FPortsRoundInfo Info = RoundInfo(State);
		State.phase = TEXT("chronicle");
		AddLog(State, V::Object({
			{ TEXT("type"), TEXT("round") }, { TEXT("text"), FString::Printf(TEXT("%s (%s): %s"), *Info.label, *Info.months, *Info.headline) }, { TEXT("factIds"), Info.factIds } }));
		TArray<const FPortsValue*> Cards;
		for (const int32 H : HalvesOfRound(State))
		{
			if (H >= 1) HistoricalArrivals(State, H);
			for (const FPortsValue& Card : D.Chronicle().GetItems()) if (Card.Get(TEXT("round")).AsInt() == H) Cards.Add(&Card);
		}
		State.currentChronicle.Reset();
		for (const FPortsValue* Card : Cards) State.currentChronicle.Add(Card->Get(TEXT("id")).AsString());
		for (const FPortsValue* Card : Cards) ApplyCard(State, *Card);
	}

	static void EventPhase(FPortsState& State)
	{
		const FPortsData& D = FPortsData::Get();
		State.phase = TEXT("event");
		// No Event card before the plague: the deck is about the plague years.
		const int32 PerRound = IsPrePlague(State) ? 0 : D.Int(*(DifficultyPath(State) + TEXT(".eventsPerRound")));
		for (int32 i = 0; i < PerRound; i++)
		{
			if (State.deck.Num() == 0)
			{
				TArray<FString> Ids;
				for (const FPortsValue& Card : D.Deck().GetItems()) Ids.Add(Card.Get(TEXT("id")).AsString());
				State.deck = Shuffle(State, Ids);
			}
			const FString Id = State.deck[0];
			State.deck.RemoveAt(0);
			State.currentEvent = Id;
			for (const FPortsValue& Card : D.Deck().GetItems())
			{
				if (Card.Get(TEXT("id")).AsString() == Id) { ApplyCard(State, Card); break; }
			}
		}
	}

	// Land holdings need hired workers every half-year; wages were high.
	static void PayLandWages(FPortsState& State)
	{
		const int32 LandWage = FPortsData::Get().Int(TEXT("costs.landWage"));
		for (FPortsPlayer& P : State.players)
		{
			if (P.land.Num() == 0) continue;
			const int32 Wage = P.land.Num() * LandWage;
			if (P.florins >= Wage)
			{
				P.florins -= Wage;
				AddLog(State, V::Object({ { TEXT("type"), TEXT("upkeep") }, { TEXT("player"), P.id }, { TEXT("text"), FString::Printf(TEXT("%s pays %dƒ in wages for its land."), *P.name, Wage) }, { TEXT("factIds"), V::Array({ V(TEXT("EC-03")) }) } }));
			}
			else
			{
				P.reputation -= 1;
				ClampReputation(P);
				AddLog(State, V::Object({ { TEXT("type"), TEXT("upkeep") }, { TEXT("player"), P.id }, { TEXT("text"), FString::Printf(TEXT("%s cannot pay its farm workers. The fields go untended: −1 reputation."), *P.name) }, { TEXT("factIds"), V::Array({ V(TEXT("EC-03")) }) } }));
			}
		}
	}

	// Loans fall due in the plague phase of the round after they were taken.
	static void SettleLoans(FPortsState& State, bool bFinal = false)
	{
		const int32 DefaultRep = FPortsData::Get().Int(TEXT("penalties.loanDefaultReputation"));
		for (FPortsPlayer& P : State.players)
		{
			if (!P.loan.IsSet() || (!bFinal && State.roundEnd < P.loan->due)) continue;
			const int32 Owed = P.loan->owed;
			P.loan.Reset();
			if (P.florins >= Owed)
			{
				P.florins -= Owed;
				AddLog(State, V::Object({ { TEXT("type"), TEXT("loanRepaid") }, { TEXT("player"), P.id }, { TEXT("text"), FString::Printf(TEXT("%s repays its loan: %dƒ."), *P.name, Owed) }, { TEXT("factIds"), V::Array() } }));
			}
			else
			{
				const int32 Paid = P.florins;
				P.florins = 0;
				P.reputation -= DefaultRep;
				ClampReputation(P);
				P.stats.defaults++;
				AddLog(State, V::Object({
					{ TEXT("type"), TEXT("loanDefault") }, { TEXT("player"), P.id },
					{ TEXT("text"), FString::Printf(TEXT("%s can pay only %dƒ of the %dƒ it owes. The banker spreads the news: −%d reputation."), *P.name, Paid, Owed, DefaultRep) },
					{ TEXT("factIds"), V::Array({ V(TEXT("EC-01")) }) } }));
			}
		}
	}

	// Partnerships and closed gates last until the end of the next round.
	static void ExpireAgreements(FPortsState& State)
	{
		for (FPortsPlayer& P : State.players)
		{
			if (P.deal.IsSet() && State.roundEnd >= P.deal->until)
			{
				if (P.id < P.deal->partner)
				{
					AddLog(State, V::Object({ { TEXT("type"), TEXT("dealEnd") }, { TEXT("player"), P.id }, { TEXT("text"), FString::Printf(TEXT("The partnership between %s and %s ends."), *P.name, *State.players[P.deal->partner].name) } }));
				}
				P.deal.Reset();
			}
			if (P.gates.IsSet() && State.roundEnd >= P.gates->until)
			{
				AddLog(State, V::Object({ { TEXT("type"), TEXT("gatesOpen") }, { TEXT("player"), P.id }, { TEXT("text"), FString::Printf(TEXT("%s opens the gates of %s again."), *P.name, *FPortsData::Get().FindCity(P.gates->city)->Name) } }));
				P.gates.Reset();
			}
		}
	}

	// Mortality and ageing happen once per half-year: three times per round in
	// Quick Play. Before the plague there is only upkeep. Turn order never changes.
	static void PlaguePhase(FPortsState& State)
	{
		State.phase = TEXT("plague");
		for (const int32 H : HalvesOfRound(State))
		{
			const FString Label = HalfInfo(H).Get(TEXT("label")).AsString();
			if (H <= 0)
			{
				AddLog(State, V::Object({ { TEXT("type"), TEXT("plague") }, { TEXT("half"), H }, { TEXT("pre"), true }, { TEXT("text"), FString::Printf(TEXT("End of %s: no plague has reached the trading cities yet."), *Label) } }));
				PayLandWages(State);
				continue;
			}
			AddLog(State, V::Object({ { TEXT("type"), TEXT("plague") }, { TEXT("half"), H }, { TEXT("text"), FString::Printf(TEXT("Plague phase (%s): family members in Stricken cities roll for survival."), *Label) } }));
			MortalityPhase(State);
			PayLandWages(State);
			AdvanceCities(State);
		}
		SettleLoans(State);
		ExpireAgreements(State);
	}

	static void EndGame(FPortsState& State)
	{
		State.phase = TEXT("ended");
		SettleLoans(State, true);
		State.finalScores = RankPlayers(State);
		TArray<int32> Winners;
		TArray<FString> Names;
		for (const FPortsRank& R : *State.finalScores) if (R.place == 1) Winners.Add(R.id);
		for (const int32 Id : Winners) Names.Add(State.players[Id].name);
		State.winner = Winners;
		AddLog(State, V::Object({
			{ TEXT("type"), TEXT("end") },
			{ TEXT("text"), FString::Printf(TEXT("The year 1353 ends. %s %s the game."), *FString::Join(Names, TEXT(" and ")), Winners.Num() > 1 ? TEXT("share") : TEXT("wins")) },
			{ TEXT("factIds"), FPortsData::Get().Timeline().Get(TEXT("epilogue")).Get(TEXT("factIds")) } }));
	}

	bool Advance(FPortsState& State)
	{
		if (State.phase == TEXT("roundStart") || State.phase == TEXT("plague"))
		{
			if (State.phase == TEXT("plague") && State.roundEnd >= FPortsData::Get().Int(TEXT("rounds"))) EndGame(State);
			else StartRound(State);
			return true;
		}
		if (State.phase == TEXT("chronicle")) { EventPhase(State); return true; }
		if (State.phase == TEXT("event"))
		{
			State.phase = TEXT("actions");
			State.turn = 0;
			BeginTurn(State);
			return true;
		}
		if (State.phase == TEXT("ended")) return true;
		// Players are still taking turns: call EndTurn().
		return false;
	}

	// Answer the first pending decision (offer, protect, wage law).
	FPortsResult Decide(FPortsState& State, EPortsChoice Choice)
	{
		FPortsPlayer* P = CurrentPlayer(State);
		if (!P) { FPortsResult R; R.reason = TEXT("It is not a player’s turn."); return R; }
		return ResolveDecision(State, *P, Choice);
	}

	FPortsResult EndTurn(FPortsState& State)
	{
		FPortsResult R;
		FPortsPlayer* P = CurrentPlayer(State);
		if (!P) { R.reason = TEXT("It is not a player’s turn."); return R; }
		if (P->pending.Num()) { R.reason = TEXT("First answer the card waiting for you."); return R; }
		P->ap = 0;
		State.turn++;
		R.ok = true;
		if (State.turn < State.order.Num())
		{
			BeginTurn(State);
			R.next = TEXT("turn");
			return R;
		}
		PlaguePhase(State);
		R.next = TEXT("plague");
		return R;
	}

	// The turn timer ran out: open decisions get their cautious answer (offers
	// are declined, wage laws obeyed) and the next house takes its turn.
	FPortsResult TimeUp(FPortsState& State)
	{
		FPortsPlayer* P = CurrentPlayer(State);
		if (!P) { FPortsResult R; R.reason = TEXT("It is not a player’s turn."); return R; }
		while (P->pending.Num()) ResolveDecision(State, *P, P->pending[0].Get(TEXT("kind")).AsString() == TEXT("wageLaw") ? EPortsChoice::Obey : EPortsChoice::No);
		AddLog(State, V::Object({ { TEXT("type"), TEXT("timeUp") }, { TEXT("player"), P->id }, { TEXT("text"), FString::Printf(TEXT("%s runs out of time. The turn passes on."), *P->name) } }));
		return EndTurn(State);
	}
}
