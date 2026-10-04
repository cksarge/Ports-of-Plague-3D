// Plague spread, severity, survival and aftermath.
#include "PortsRules.h"

using V = FPortsValue;

namespace Ports
{
	FString SeverityName(int32 Severity)
	{
		return FPortsData::Get().Config().Get(TEXT("plague")).Get(TEXT("severityNames")).Get(FString::FromInt(Severity)).AsString();
	}

	// Make a city Stricken. bEarly = brought one round ahead of history by an infected shipment.
	FPortsValue StrikeCity(FPortsState& State, const FString& CityId, bool bEarly, int32 By)
	{
		const FPortsData& D = FPortsData::Get();
		FPortsCityState* C = State.City(CityId);
		if (!C || C->state != TEXT("safe")) return V();
		const FPortsCity& City = *D.FindCity(CityId);
		const int32 Die = Roll(State, 6);
		const int32 Base = D.Int(*FString::Printf(TEXT("plague.severityTable.%d"), Die));
		const int32 Severity = FMath::Max(1, FMath::Min(D.Int(TEXT("plague.severityMax")), Base + City.SeverityMod + D.Int(*(DifficultyPath(State) + TEXT(".severityMod")))));
		C->state = TEXT("stricken");
		C->severity = Severity;
		C->strickenFor = 0;
		C->early = bEarly;
		const FString Text = bEarly
			? FString::Printf(TEXT("Infected cargo brings the plague to %s one round early. (Historically: %s.)"), *City.Name, *City.ArrivalDateText)
			: FString::Printf(TEXT("The plague reaches %s. (Historically: %s.)"), *City.Name, *City.ArrivalDateText);
		return AddLog(State, V::Object({
			{ TEXT("type"), TEXT("arrival") }, { TEXT("city"), CityId }, { TEXT("die"), Die }, { TEXT("severity"), Severity }, { TEXT("early"), bEarly },
			{ TEXT("by"), By >= 0 ? V(By) : V::Null() },
			{ TEXT("text"), FString::Printf(TEXT("%s Severity: %s."), *Text, *SeverityName(Severity)) },
			{ TEXT("factIds"), City.ArrivalFactIds } }));
	}

	// Chronicle phase: every city whose historical arrival falls in this half-year.
	void HistoricalArrivals(FPortsState& State, int32 Half)
	{
		for (const FPortsCity& City : FPortsData::Get().Cities)
		{
			if (City.ArrivalRound != Half) continue;
			if (StrikeCity(State, City.Id).IsUndefined())
			{
				AddLog(State, V::Object({
					{ TEXT("type"), TEXT("arrivalAlready") }, { TEXT("city"), City.Id },
					{ TEXT("text"), FString::Printf(TEXT("%s was already Stricken, earlier than in real history (%s)."), *City.Name, *City.ArrivalDateText) },
					{ TEXT("factIds"), City.ArrivalFactIds } }));
			}
		}
	}

	// Plague phase: every family member in a Stricken city rolls for survival.
	void MortalityPhase(FPortsState& State)
	{
		const FPortsData& D = FPortsData::Get();
		const int32 PrepareBonus = D.Int(TEXT("plague.prepareBonus"));
		const int32 SaveOn = D.Int(TEXT("plague.physicianSaveOn"));
		const int32 Inheritance = D.Int(TEXT("gains.inheritance"));
		for (FPortsPlayer& P : State.players)
		{
			for (const FString& Loc : FamilyLocations(P))
			{
				if (!IsStricken(State, Loc)) continue;
				const int32 Sev = State.City(Loc)->severity;
				const bool bPrepared = P.prepared.Contains(Loc);
				bool bPhysician = P.physician.Contains(Loc);
				V Rolls = V::Array();
				int32 Deaths = 0, Saved = 0;
				bool bLastHeir = false;
				const int32 Members = P.family.Get(Loc);
				for (int32 i = 0; i < Members; i++)
				{
					const int32 Die = Roll(State, 6);
					const int32 Total = Die + (bPrepared ? PrepareBonus : 0);
					bool bDies = Total <= Sev;
					V Save = V::Null();
					if (bDies && bPhysician)
					{
						bPhysician = false;
						const int32 SaveRoll = Roll(State, 6);
						Save = V(SaveRoll);
						if (SaveRoll >= SaveOn) { bDies = false; Saved++; }
					}
					// The last heir never dies: a distant cousin inherits.
					if (bDies && FamilyTotal(P) - Deaths <= 1)
					{
						Rolls.Add(V::Object({ { TEXT("die"), Die }, { TEXT("total"), Total }, { TEXT("dies"), false }, { TEXT("save"), Save }, { TEXT("lastHeir"), true } }));
						bLastHeir = true;
						continue;
					}
					if (bDies) Deaths++;
					Rolls.Add(V::Object({ { TEXT("die"), Die }, { TEXT("total"), Total }, { TEXT("dies"), bDies }, { TEXT("save"), Save } }));
				}
				P.family.Set(Loc, P.family.Get(Loc) - Deaths);
				P.lostFamily += Deaths;
				P.florins += Deaths * Inheritance;
				const FString CityName = D.FindCity(Loc)->Name;
				FString Text = FString::Printf(TEXT("%s: %d family member%s in %s (%s%s). "), *P.name, Members, Members == 1 ? TEXT("") : TEXT("s"), *CityName, *SeverityName(Sev), bPrepared ? TEXT(", prepared") : TEXT(""));
				Text += Deaths ? FString::Printf(TEXT("%d died. The house inherits %dƒ."), Deaths, Deaths * Inheritance) : FString(TEXT("All survived."));
				if (Saved) Text += TEXT(" Nursing care saved one.");
				if (bLastHeir) Text += TEXT(" The last heir survives: a distant cousin would inherit.");
				AddLog(State, V::Object({
					{ TEXT("type"), TEXT("mortality") }, { TEXT("player"), P.id }, { TEXT("city"), Loc }, { TEXT("severity"), Sev }, { TEXT("prepared"), bPrepared },
					{ TEXT("rolls"), Rolls }, { TEXT("deaths"), Deaths }, { TEXT("saved"), Saved }, { TEXT("lastHeir"), bLastHeir }, { TEXT("text"), Text },
					{ TEXT("factIds"), Deaths ? V::Array({ V(TEXT("EC-10")) }) : V::Array() } }));
			}
		}
	}

	// End of the plague phase: Stricken cities age; after enough rounds they enter Aftermath.
	void AdvanceCities(FPortsState& State)
	{
		const FPortsData& D = FPortsData::Get();
		const int32 StrickenRounds = D.Int(TEXT("plague.strickenRounds"));
		for (int32 i = 0; i < State.cities.Num(); i++)
		{
			FPortsCityState& C = State.cities[i];
			if (C.unrest > 0) C.unrest--;
			if (C.state != TEXT("stricken")) continue;
			C.strickenFor++;
			if (C.strickenFor >= StrickenRounds)
			{
				C.state = TEXT("aftermath");
				AddLog(State, V::Object({
					{ TEXT("type"), TEXT("aftermath") }, { TEXT("city"), D.Cities[i].Id },
					{ TEXT("text"), FString::Printf(TEXT("The plague passes from %s. The city enters Aftermath: workers are scarce and prices high."), *D.Cities[i].Name) },
					{ TEXT("factIds"), V::Array({ V(TEXT("EC-02")), V(TEXT("EC-03")) }) } }));
			}
		}
	}
}
