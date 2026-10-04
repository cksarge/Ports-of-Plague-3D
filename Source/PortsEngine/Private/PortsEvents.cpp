// Chronicle and Event card effects, and the player decisions they create.
#include "PortsRules.h"

using V = FPortsValue;

namespace Ports
{
	const FPortsValue& CardById(const FString& Id)
	{
		const FPortsData& D = FPortsData::Get();
		for (const FPortsValue& Card : D.Chronicle().GetItems()) if (Card.Get(TEXT("id")).AsString() == Id) return Card;
		for (const FPortsValue& Card : D.Deck().GetItems()) if (Card.Get(TEXT("id")).AsString() == Id) return Card;
		return FortuneById(Id);
	}

	FPortsValue ChoiceToValue(EPortsChoice Choice)
	{
		switch (Choice)
		{
		case EPortsChoice::Yes: return V(true);
		case EPortsChoice::Obey: return V(TEXT("obey"));
		case EPortsChoice::Pay: return V(TEXT("pay"));
		default: return V(false);
		}
	}

	static void ApplyEffect(FPortsState& State, const FPortsValue& Card, const FPortsValue& E)
	{
		const FPortsData& D = FPortsData::Get();
		FPortsEffects& Fx = State.effects;
		const FString Type = E.Get(TEXT("type")).AsString();
		const auto Effect = [&State](const FPortsPlayer& P, const FString& Text)
		{
			AddLog(State, V::Object({ { TEXT("type"), TEXT("effect") }, { TEXT("player"), P.id }, { TEXT("text"), Text } }));
		};
		const auto RouteType = [&E]() { return E.Get(TEXT("routeType")).IsMissing() ? FString(TEXT("all")) : E.Get(TEXT("routeType")).AsString(); };

		if (Type == TEXT("none"))
		{
		}
		else if (Type == TEXT("multi"))
		{
			for (const FPortsValue& Sub : E.Get(TEXT("effects")).GetItems()) ApplyEffect(State, Card, Sub);
		}
		else if (Type == TEXT("noNewPostsNearPlague"))
		{
			Fx.noNewPostsNearPlague = true;
		}
		else if (Type == TEXT("roundModifier"))
		{
			Fx.profit.Of(RouteType()) += E.Get(TEXT("profit")).AsInt();
		}
		else if (Type == TEXT("contagionModifier"))
		{
			Fx.contagion.Of(RouteType()) += E.Get(TEXT("delta")).AsInt();
		}
		else if (Type == TEXT("costModifier"))
		{
			const FString Key = E.Get(TEXT("cost")).AsString();
			Fx.costs.Set(Key, Fx.costs.Get(Key, 0) + E.Get(TEXT("delta")).AsInt());
		}
		else if (Type == TEXT("charityBonus"))
		{
			Fx.charityBonus += E.Get(TEXT("delta")).AsInt();
		}
		else if (Type == TEXT("marketBonus"))
		{
			Fx.marketBonus += E.Get(TEXT("delta")).AsInt();
		}
		else if (Type == TEXT("onePlayer"))
		{
			FPortsPlayer& P = State.players[PickIndex(State, State.players.Num())];
			const FPortsValue& Gain = E.Get(TEXT("effect"));
			const int32 Florins = Gain.Get(TEXT("florins")).AsInt(), Reputation = Gain.Get(TEXT("reputation")).AsInt();
			if (Florins) P.florins = FMath::Max(0, P.florins + Florins);
			if (Reputation) { P.reputation += Reputation; ClampReputation(P); }
			TArray<FString> Parts;
			if (Florins) Parts.Add(FString::Printf(TEXT("%s %dƒ"), Florins > 0 ? TEXT("gains") : TEXT("loses"), FMath::Abs(Florins)));
			if (Reputation) Parts.Add(FString::Printf(TEXT("%s %d reputation"), Reputation > 0 ? TEXT("gains") : TEXT("loses"), FMath::Abs(Reputation)));
			Effect(P, FString::Printf(TEXT("The lot falls on %s, who %s."), *P.name, *FString::Join(Parts, TEXT(" and "))));
		}
		else if (Type == TEXT("cityProfitModifier"))
		{
			Fx.cityProfit.Add({ E.Get(TEXT("cities")).ToStrings(), E.Get(TEXT("onlyStricken")).Truthy(), E.Get(TEXT("profit")).AsInt() });
		}
		else if (Type == TEXT("freeAction"))
		{
			for (FPortsPlayer& P : State.players) P.free.Set(E.Get(TEXT("action")).AsString(), true);
		}
		else if (Type == TEXT("lastPlaceBonus"))
		{
			FPortsPlayer& P = State.players[LastPlaceId(State)];
			const int32 Florins = E.Get(TEXT("florins")).AsInt();
			P.florins += Florins;
			Effect(P, FString::Printf(TEXT("%s, in last place, gains %dƒ."), *P.name, Florins));
		}
		else if (Type == TEXT("florinsPerPost"))
		{
			const FString Wanted = E.Get(TEXT("state")).AsString();
			const int32 Florins = E.Get(TEXT("florins")).AsInt();
			for (FPortsPlayer& P : State.players)
			{
				int32 N = 0;
				for (const FString& C : P.posts) if (State.City(C)->state == Wanted) N++;
				if (N)
				{
					P.florins = FMath::Max(0, P.florins + N * Florins);
					Effect(P, FString::Printf(TEXT("%s %s %dƒ."), *P.name, Florins < 0 ? TEXT("loses") : TEXT("gains"), FMath::Abs(N * Florins)));
				}
			}
		}
		else if (Type == TEXT("loseIfRich"))
		{
			const int32 Threshold = E.Get(TEXT("threshold")).AsInt(), Florins = E.Get(TEXT("florins")).AsInt();
			for (FPortsPlayer& P : State.players)
			{
				if (P.florins >= Threshold)
				{
					P.florins -= Florins;
					Effect(P, FString::Printf(TEXT("%s pays %dƒ to creditors."), *P.name, Florins));
				}
			}
		}
		else if (Type == TEXT("postBonus"))
		{
			const FPortsValue& Cities = E.Get(TEXT("cities"));
			const int32 Florins = E.Get(TEXT("florins")).AsInt(), Reputation = E.Get(TEXT("reputation")).AsInt();
			for (FPortsPlayer& P : State.players)
			{
				if (!P.posts.ContainsByPredicate([&Cities](const FString& C) { return Cities.ContainsString(C); })) continue;
				if (Florins) P.florins = FMath::Max(0, P.florins + Florins);
				if (Reputation) { P.reputation += Reputation; ClampReputation(P); }
				TArray<FString> Parts;
				if (Florins) Parts.Add(FString::Printf(TEXT("%s%dƒ"), Florins > 0 ? TEXT("+") : TEXT(""), Florins));
				if (Reputation) Parts.Add(FString::Printf(TEXT("%s%d reputation"), Reputation > 0 ? TEXT("+") : TEXT(""), Reputation));
				Effect(P, FString::Printf(TEXT("%s: %s."), *P.name, *FString::Join(Parts, TEXT(", "))));
			}
		}
		else if (Type == TEXT("reputationByFlight"))
		{
			const int32 Fled = E.Get(TEXT("fled")).AsInt(), Stayed = E.Get(TEXT("stayed")).AsInt();
			for (FPortsPlayer& P : State.players)
			{
				const bool bFled = FamilyAt(P, ESTATE) > 0;
				P.reputation += bFled ? Fled : Stayed;
				ClampReputation(P);
				Effect(P, bFled ? FString::Printf(TEXT("%s loses %d reputation."), *P.name, -Fled) : FString::Printf(TEXT("%s gains %d reputation."), *P.name, Stayed));
			}
		}
		else if (Type == TEXT("reputationIfOffer"))
		{
			const FString Offer = E.Get(TEXT("offer")).AsString();
			const int32 Reputation = E.Get(TEXT("reputation")).AsInt();
			for (FPortsPlayer& P : State.players)
			{
				if (P.offers.Get(Offer, false))
				{
					P.reputation += Reputation;
					ClampReputation(P);
					Effect(P, FString::Printf(TEXT("%s loses %d reputation."), *P.name, -Reputation));
				}
			}
		}
		else if (Type == TEXT("newFamily"))
		{
			const int32 MinLost = E.Get(TEXT("minLost")).AsInt(), Gain = E.Get(TEXT("gain")).AsInt();
			for (FPortsPlayer& P : State.players)
			{
				if (P.lostFamily >= MinLost && FamilyTotal(P) < D.Int(TEXT("start.family")))
				{
					P.family.Set(P.home, P.family.Get(P.home, 0) + Gain);
					Effect(P, FString::Printf(TEXT("%s welcomes a new family member in %s."), *P.name, *D.FindCity(P.home)->Name));
				}
			}
		}
		else if (Type == TEXT("offer"))
		{
			const auto OrEmpty = [](const FPortsValue& In) { return In.IsMissing() ? V::Object() : In; };
			for (FPortsPlayer& P : State.players)
			{
				P.pending.Add(V::Object({
					{ TEXT("kind"), TEXT("offer") }, { TEXT("card"), Card.Get(TEXT("id")) }, { TEXT("offer"), E.Get(TEXT("id")) }, { TEXT("label"), E.Get(TEXT("label")) },
					{ TEXT("decline"), E.Get(TEXT("decline")) }, { TEXT("cost"), OrEmpty(E.Get(TEXT("cost"))) }, { TEXT("gain"), OrEmpty(E.Get(TEXT("gain"))) },
					{ TEXT("reveal"), E.Get(TEXT("reveal")).IsMissing() ? V::Null() : E.Get(TEXT("reveal")) } }));
			}
		}
		else if (Type == TEXT("persecution"))
		{
			const FString City = E.Get(TEXT("city")).AsString();
			State.City(City)->unrest = D.Int(TEXT("penalties.unrestRounds"));
			State.persecution = FPortsPersecution{ City, State.round, {} };
			for (FPortsPlayer& P : State.players)
			{
				P.pending.Add(V::Object({ { TEXT("kind"), TEXT("protect") }, { TEXT("card"), Card.Get(TEXT("id")) }, { TEXT("city"), City } }));
			}
		}
		else if (Type == TEXT("wageLaw"))
		{
			const FPortsValue& Cities = E.Get(TEXT("cities"));
			for (FPortsPlayer& P : State.players)
			{
				if (P.posts.ContainsByPredicate([&Cities](const FString& C) { return Cities.ContainsString(C); }))
				{
					P.pending.Add(V::Object({ { TEXT("kind"), TEXT("wageLaw") }, { TEXT("card"), Card.Get(TEXT("id")) }, { TEXT("cities"), Cities } }));
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Unknown card effect %s"), *Type);
		}
	}

	// Applies a card. Global effects change this round's modifiers; choices
	// become pending decisions each player answers at the start of their turn.
	FPortsValue ApplyCard(FPortsState& State, const FPortsValue& Card)
	{
		const FPortsValue Entry = AddLog(State, V::Object({
			{ TEXT("type"), TEXT("card") }, { TEXT("card"), Card.Get(TEXT("id")) },
			{ TEXT("deck"), Card.Has(TEXT("round")) ? TEXT("chronicle") : TEXT("event") },
			{ TEXT("text"), Card.Get(TEXT("text")) }, { TEXT("factIds"), Card.Get(TEXT("factIds")) } }));
		ApplyEffect(State, Card, Card.Get(TEXT("effect")));
		return Entry;
	}

	// Checks whether a player can accept a pending decision.
	FString CanAccept(const FPortsState& State, const FPortsPlayer& P, const FPortsValue& Dec)
	{
		const FPortsData& D = FPortsData::Get();
		const FString Kind = Dec.Get(TEXT("kind")).AsString();
		const int32 Price = Dec.Get(TEXT("cost")).Get(TEXT("florins")).AsInt(0);
		if (Kind == TEXT("offer") && Price > P.florins) return FString::Printf(TEXT("You need %dƒ to accept."), Price);
		if (Kind == TEXT("protect"))
		{
			if (P.florins < D.Int(TEXT("costs.protectCommunity"))) return FString::Printf(TEXT("Protecting the community costs %dƒ."), D.Int(TEXT("costs.protectCommunity")));
			if (P.ap < 1) return TEXT("Protecting the community takes 1 action point.");
		}
		if (Kind == TEXT("deal"))
		{
			const FPortsPlayer& From = State.players[Dec.Get(TEXT("from")).AsInt()];
			if (P.deal.IsSet()) return FString::Printf(TEXT("You already have a partnership with %s."), *State.players[P.deal->partner].name);
			if (From.deal.IsSet()) return FString::Printf(TEXT("%s has found another partner in the meantime."), *From.name);
		}
		return FString();
	}

	// Resolves one pending decision.
	FPortsResult ResolveDecision(FPortsState& State, FPortsPlayer& P, EPortsChoice Choice)
	{
		const FPortsData& D = FPortsData::Get();
		FPortsResult Out;
		if (P.pending.Num() == 0) { Out.reason = TEXT("There is no decision waiting."); return Out; }
		const FPortsValue Dec = P.pending[0];
		const FString Kind = Dec.Get(TEXT("kind")).AsString();
		// In JavaScript, "obey" and "pay" count as yes for anything that is not a wage law.
		const bool bYes = Choice != EPortsChoice::No;
		if (Kind == TEXT("wageLaw"))
		{
			if (Choice != EPortsChoice::Obey && Choice != EPortsChoice::Pay) { Out.reason = TEXT("Choose to obey the law or pay market wages."); return Out; }
		}
		else if (Choice == EPortsChoice::Yes)
		{
			const FString Why = CanAccept(State, P, Dec);
			if (!Why.IsEmpty()) { Out.reason = Why; return Out; }
		}
		P.pending.RemoveAt(0);
		FString Text;
		V Extra = V::Object();
		if (Kind == TEXT("offer"))
		{
			if (bYes)
			{
				P.florins -= Dec.Get(TEXT("cost")).Get(TEXT("florins")).AsInt(0);
				P.florins += Dec.Get(TEXT("gain")).Get(TEXT("florins")).AsInt(0);
				P.reputation += Dec.Get(TEXT("gain")).Get(TEXT("reputation")).AsInt(0);
				ClampReputation(P);
				P.offers.Set(Dec.Get(TEXT("offer")).AsString(), true);
				Text = FString::Printf(TEXT("%s: %s."), *P.name, *Dec.Get(TEXT("label")).AsString());
				if (Dec.Get(TEXT("reveal")).Truthy()) Text += TEXT(" ") + Dec.Get(TEXT("reveal")).AsString();
			}
			else
			{
				Text = FString::Printf(TEXT("%s: %s."), *P.name, *Dec.Get(TEXT("decline")).AsString());
				const FPortsValue& Pen = Dec.Get(TEXT("declinePenalty"));
				if (Pen.Get(TEXT("reputation")).Truthy())
				{
					P.reputation -= Pen.Get(TEXT("reputation")).AsInt();
					ClampReputation(P);
					Text += FString::Printf(TEXT(" −%d reputation."), Pen.Get(TEXT("reputation")).AsInt());
				}
				if (Pen.Get(TEXT("florins")).Truthy())
				{
					P.florins = FMath::Max(0, P.florins - Pen.Get(TEXT("florins")).AsInt());
					Text += FString::Printf(TEXT(" −%dƒ."), Pen.Get(TEXT("florins")).AsInt());
				}
				if (Pen.Get(TEXT("blockHome")).Truthy() && !P.shipped.Contains(P.home))
				{
					P.shipped.Add(P.home);
					Text += FString::Printf(TEXT(" Your post in %s cannot ship this round."), *D.FindCity(P.home)->Name);
				}
			}
		}
		else if (Kind == TEXT("protect"))
		{
			const FString City = D.FindCity(Dec.Get(TEXT("city")).AsString())->Name;
			if (bYes)
			{
				P.florins -= D.Int(TEXT("costs.protectCommunity"));
				P.ap -= 1;
				P.reputation += D.Int(TEXT("gains.protectReputation"));
				ClampReputation(P);
				P.stats.protectedCount++;
				if (State.persecution.IsSet()) State.persecution->protectors.Add(P.id);
				Text = FString::Printf(TEXT("%s spends money and influence to shelter and defend the Jewish community of %s. In real history, those who tried to protect the community were overruled."), *P.name, *City);
			}
			else
			{
				Text = FString::Printf(TEXT("%s does not intervene in %s."), *P.name, *City);
			}
		}
		else if (Kind == TEXT("deal"))
		{
			FPortsPlayer& From = State.players[Dec.Get(TEXT("from")).AsInt()];
			if (bYes)
			{
				const int32 Until = UntilRound(State, D.Int(TEXT("limits.dealRounds")));
				P.deal = FPortsDeal{ From.id, Until };
				From.deal = FPortsDeal{ P.id, Until };
				P.stats.deals++;
				From.stats.deals++;
				Text = FString::Printf(TEXT("%s and %s become partners until the end of next round. When either ships to a city where the other has a post, both earn %dƒ more."), *P.name, *From.name, D.Int(TEXT("gains.dealBonus")));
			}
			else
			{
				Text = FString::Printf(TEXT("%s declines %s's partnership."), *P.name, *From.name);
			}
		}
		else if (Kind == TEXT("wageLaw"))
		{
			if (Choice == EPortsChoice::Obey)
			{
				P.reputation += D.Int(TEXT("wageLaw.obeyReputation"));
				ClampReputation(P);
				P.englishBlocked = true;
				Text = FString::Printf(TEXT("%s obeys the wage law. Workers refuse the old wages, so the house's English posts cannot ship this round."), *P.name);
			}
			else
			{
				const int32 Die = Roll(State, 6);
				const bool bFined = Die <= D.Int(TEXT("wageLaw.fineMaxRoll"));
				if (bFined) P.florins = FMath::Max(0, P.florins - D.Int(TEXT("wageLaw.fine")));
				Extra.Set(TEXT("die"), Die);
				Extra.Set(TEXT("fined"), bFined);
				Text = FString::Printf(TEXT("%s pays market wages. Inspection die: %d. %s"), *P.name, Die, bFined ? *FString::Printf(TEXT("Fined %dƒ."), D.Int(TEXT("wageLaw.fine"))) : TEXT("No fine."));
			}
		}
		const FPortsValue& Card = CardById(Dec.Get(TEXT("card")).AsString());
		const V FactIds = Kind == TEXT("deal") ? V::Array({ V(TEXT("TR-04")) }) : Card.Get(TEXT("factIds")).IsMissing() ? V::Array() : Card.Get(TEXT("factIds"));
		V Entry = V::Object({ { TEXT("type"), TEXT("decision") }, { TEXT("player"), P.id }, { TEXT("kind"), Dec.Get(TEXT("kind")) }, { TEXT("choice"), ChoiceToValue(Choice) }, { TEXT("text"), Text }, { TEXT("factIds"), FactIds } });
		for (int32 i = 0; i < Extra.GetKeys().Num(); i++) Entry.Set(Extra.GetKeys()[i], Extra.ValueAt(i));
		Out.ok = true;
		Out.entry = AddLog(State, Entry);
		Out.decision = Dec;
		return Out;
	}
}
