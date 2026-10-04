// Fortune cards: a personal deck. A house draws one when it rolls a 6 on the
// profit die or opens a new trading post, so every player's game differs.
#include "PortsRules.h"

using V = FPortsValue;

namespace Ports
{
	const FPortsValue& FortuneById(const FString& Id)
	{
		static const FPortsValue None;
		for (const FPortsValue& Card : FPortsData::Get().Fortune().GetItems()) if (Card.Get(TEXT("id")).AsString() == Id) return Card;
		return None;
	}

	// Applies a signed gain of florins / reputation to one house.
	static void ApplyGain(FPortsPlayer& P, const FPortsValue& E)
	{
		if (E.Get(TEXT("florins")).Truthy()) P.florins = FMath::Max(0, P.florins + E.Get(TEXT("florins")).AsInt());
		if (E.Get(TEXT("reputation")).Truthy()) { P.reputation += E.Get(TEXT("reputation")).AsInt(); ClampReputation(P); }
	}

	FPortsValue DrawFortune(FPortsState& State, FPortsPlayer& P, const FString& Reason)
	{
		const FPortsData& D = FPortsData::Get();
		if (State.fortuneDeck.Num() == 0)
		{
			TArray<FString> Ids;
			for (const FPortsValue& Card : D.Fortune().GetItems()) Ids.Add(Card.Get(TEXT("id")).AsString());
			State.fortuneDeck = Shuffle(State, Ids);
		}
		const FString CardId = State.fortuneDeck[0];
		State.fortuneDeck.RemoveAt(0);
		const FPortsValue& Card = FortuneById(CardId);
		const FPortsValue& E = Card.Get(TEXT("effect"));
		const FString Type = E.Get(TEXT("type")).AsString();
		const int32 StartFamily = D.Int(TEXT("start.family"));
		const int32 Inheritance = D.Int(TEXT("gains.inheritance"));
		FString Result;
		V Extra = V::Object();

		if (Type == TEXT("gain"))
		{
			ApplyGain(P, E);
		}
		else if (Type == TEXT("nextShip"))
		{
			FPortsNextShip Next;
			Next.profit = (P.nextShip.IsSet() ? P.nextShip->profit : 0) + E.Get(TEXT("profit")).AsInt(0);
			Next.safe = E.Get(TEXT("safe")).Truthy() || (P.nextShip.IsSet() && P.nextShip->safe);
			P.nextShip = Next;
		}
		else if (Type == TEXT("extraAP"))
		{
			const int32 Ap = E.Get(TEXT("ap")).AsInt();
			if (Ap < 0 && P.ap < 1) Result = TEXT("You had no action points left to lose.");
			P.ap = FMath::Max(0, P.ap + Ap);
		}
		else if (Type == TEXT("freeAction"))
		{
			P.free.Set(E.Get(TEXT("action")).AsString(), true);
		}
		else if (Type == TEXT("newFamily"))
		{
			if (FamilyTotal(P) < StartFamily) P.family.Set(P.home, P.family.Get(P.home, 0) + E.Get(TEXT("gain")).AsInt());
			else Result = TEXT("Your house is already at full strength, so the couple settles elsewhere.");
		}
		else if (Type == TEXT("omen"))
		{
			const int32 Next = State.roundEnd + 1;
			const int32 Upto = FMath::Min(D.Int(TEXT("rounds")), State.roundEnd + State.span);
			V Cities = V::Array();
			TArray<FString> Lines;
			for (int32 i = 0; i < D.Cities.Num(); i++)
			{
				const FPortsCity& C = D.Cities[i];
				if (C.ArrivalRound >= Next && C.ArrivalRound <= Upto && State.cities[i].state == TEXT("safe"))
				{
					Cities.Add(V(C.Id));
					Lines.Add(FString::Printf(TEXT("%s (%s)"), *C.Name, *C.ArrivalDateText));
				}
			}
			Extra.Set(TEXT("cities"), Cities);
			Result = Lines.Num() ? FString::Printf(TEXT("Next round the plague will reach: %s."), *FString::Join(Lines, TEXT("; "))) : FString(TEXT("No new cities are struck next round."));
		}
		else if (Type == TEXT("treaty"))
		{
			FPortsPlayer* Poorest = nullptr;
			for (FPortsPlayer& O : State.players)
			{
				if (O.id == P.id) continue;
				if (!Poorest || O.florins < Poorest->florins) Poorest = &O;
			}
			const int32 Florins = E.Get(TEXT("florins")).AsInt();
			P.florins += Florins;
			Poorest->florins += Florins;
			Extra.Set(TEXT("partner"), Poorest->id);
			Result = FString::Printf(TEXT("%s and %s each gain %dƒ."), *P.name, *Poorest->name, Florins);
		}
		else if (Type == TEXT("offer"))
		{
			const auto OrEmpty = [](const FPortsValue& In) { return In.IsMissing() ? V::Object() : In; };
			P.pending.Insert(V::Object({
				{ TEXT("kind"), TEXT("offer") }, { TEXT("card"), Card.Get(TEXT("id")) }, { TEXT("offer"), E.Get(TEXT("id")) }, { TEXT("label"), E.Get(TEXT("label")) },
				{ TEXT("decline"), E.Get(TEXT("decline")) }, { TEXT("cost"), OrEmpty(E.Get(TEXT("cost"))) }, { TEXT("gain"), OrEmpty(E.Get(TEXT("gain"))) },
				{ TEXT("reveal"), V::Null() }, { TEXT("declinePenalty"), E.Get(TEXT("declinePenalty")).IsMissing() ? V::Null() : E.Get(TEXT("declinePenalty")) },
				{ TEXT("fortune"), true } }), 0);
		}
		else if (Type == TEXT("losePercent"))
		{
			const int32 Loss = FMath::FloorToInt32((static_cast<double>(P.florins) * E.Get(TEXT("percent")).AsNumber()) / 100.0);
			P.florins -= Loss;
			Result = FString::Printf(TEXT("You pay back %dƒ."), Loss);
		}
		else if (Type == TEXT("illness"))
		{
			FString Loc;
			bool bAny = false;
			for (const FString& L : FamilyLocations(P))
			{
				if (!IsStricken(State, L)) continue;
				if (!bAny || State.City(L)->severity > State.City(Loc)->severity) Loc = L;
				bAny = true;
			}
			if (!bAny)
			{
				Result = TEXT("Your family is not in a Stricken city. The fever passes.");
			}
			else
			{
				const int32 Sev = State.City(Loc)->severity;
				const int32 Die = Roll(State, 6);
				bool bDies = Die <= Sev;
				bool bLastHeir = false;
				if (bDies && FamilyTotal(P) <= 1) { bDies = false; bLastHeir = true; Extra.Set(TEXT("lastHeir"), true); }
				if (bDies) { P.family.Set(Loc, P.family.Get(Loc) - 1); P.lostFamily++; P.florins += Inheritance; }
				Extra.Set(TEXT("city"), Loc);
				Extra.Set(TEXT("die"), Die);
				Extra.Set(TEXT("severity"), Sev);
				Extra.Set(TEXT("dies"), bDies);
				const FString Outcome = bDies ? FString::Printf(TEXT("they die. The house inherits %dƒ."), Inheritance) : bLastHeir ? FString(TEXT("the last heir survives.")) : FString(TEXT("they recover."));
				Result = FString::Printf(TEXT("A family member in %s rolls %d (dies on %d or less): %s"), *D.FindCity(Loc)->Name, Die, Sev, *Outcome);
			}
		}
		else if (Type == TEXT("personalCost"))
		{
			const FString Key = E.Get(TEXT("cost")).AsString();
			P.personalCosts.Set(Key, P.personalCosts.Get(Key, 0) + E.Get(TEXT("delta")).AsInt());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Unknown fortune effect %s"), *Type);
		}

		P.stats.fortune++;
		V Entry = V::Object({ { TEXT("type"), TEXT("fortune") }, { TEXT("player"), P.id }, { TEXT("card"), Card.Get(TEXT("id")) }, { TEXT("reason"), Reason }, { TEXT("result"), Result } });
		for (int32 i = 0; i < Extra.GetKeys().Num(); i++) Entry.Set(Extra.GetKeys()[i], Extra.ValueAt(i));
		Entry.Set(TEXT("text"), FString::Printf(TEXT("%s draws a Fortune card: %s. %s"), *P.name, *Card.Get(TEXT("title")).AsString(), *Result).TrimStartAndEnd());
		Entry.Set(TEXT("factIds"), Card.Get(TEXT("factIds")));
		return AddLog(State, Entry);
	}
}
