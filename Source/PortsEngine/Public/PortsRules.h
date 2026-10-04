// The rules: scoring.js, plague.js, fortune.js, events.js, actions.js and
// turn.js of the web version, function for function. The interface calls
// Advance() after showing each phase; players act with PerformAction(),
// Decide(), EndTurn() and, when a turn timer runs out, TimeUp().
#pragma once

#include "CoreMinimal.h"
#include "PortsState.h"

// An action a house takes on its turn: the same fields as the web version's action objects.
struct FPortsAction
{
	// ship, post, move, prepare, physician, charity, marry, land, loan, deal or gates.
	FString type;
	FString from;
	FString to;
	FString route;
	FString city;
	FString kind;
	int32 count = 0;
	bool bCountIsWhole = true;
	int32 partner = -1;
	bool offshore = false;

	PORTSENGINE_API FPortsValue ToValue() const;
	PORTSENGINE_API static FPortsAction FromValue(const FPortsValue& Value);
};

// The answer to a waiting decision: true or false for offers, protection and
// partnerships; "obey" or "pay" for wage laws.
enum class EPortsChoice : uint8 { No, Yes, Obey, Pay };

// What an engine call returns: ok, or the plain-language reason it was refused.
struct FPortsResult
{
	bool ok = false;
	FString reason;
	// The log entry the call produced.
	FPortsValue entry;
	// EndTurn: "turn" if another house plays next, "plague" if the round is over.
	FString next;
	// Decide: the decision that was answered.
	FPortsValue decision;
};

// A shipment's expected profit before the dice (shipQuote in actions.js).
struct FPortsShipQuote
{
	const FPortsRoute* route = nullptr;
	FString from;
	FString to;
	// { label, value } for each line of the sum.
	FPortsValue parts;
	int32 fixed = 0;
	int32 min = 0;
	int32 max = 0;
	int32 contagionRisk = 0;
	bool safe = false;
	bool offshore = false;
	int32 fee = 0;
	// The partner house that also earns, or -1.
	int32 partner = -1;
};

namespace Ports
{
	// ---------- scoring.js ----------
	PORTSENGINE_API FPortsScore ScorePlayer(const FPortsPlayer& P);
	// The same sum on fractions, for the bots' "what if" estimates.
	PORTSENGINE_API double ScoreTotal(double Florins, int32 Posts, int32 Land, double FamilyMembers, double Reputation);
	PORTSENGINE_API TArray<FPortsRank> RankPlayers(const FPortsState& State);
	PORTSENGINE_API int32 LastPlaceId(const FPortsState& State);

	// ---------- plague.js ----------
	PORTSENGINE_API FString SeverityName(int32 Severity);
	// Makes a city Stricken. Returns the log entry, or undefined if the city was not Safe.
	PORTSENGINE_API FPortsValue StrikeCity(FPortsState& State, const FString& CityId, bool bEarly = false, int32 By = -1);
	PORTSENGINE_API void HistoricalArrivals(FPortsState& State, int32 Half);
	PORTSENGINE_API void MortalityPhase(FPortsState& State);
	PORTSENGINE_API void AdvanceCities(FPortsState& State);

	// ---------- fortune.js ----------
	PORTSENGINE_API const FPortsValue& FortuneById(const FString& Id);
	PORTSENGINE_API FPortsValue DrawFortune(FPortsState& State, FPortsPlayer& P, const FString& Reason);

	// ---------- events.js ----------
	PORTSENGINE_API const FPortsValue& CardById(const FString& Id);
	PORTSENGINE_API FPortsValue ApplyCard(FPortsState& State, const FPortsValue& Card);
	// Why a house cannot accept a waiting decision, or an empty string if it can.
	PORTSENGINE_API FString CanAccept(const FPortsState& State, const FPortsPlayer& P, const FPortsValue& Decision);
	PORTSENGINE_API FPortsResult ResolveDecision(FPortsState& State, FPortsPlayer& P, EPortsChoice Choice);
	PORTSENGINE_API FPortsValue ChoiceToValue(EPortsChoice Choice);

	// ---------- actions.js ----------
	PORTSENGINE_API FPortsShipQuote ShipQuote(const FPortsState& State, const FPortsPlayer& P, const FString& RouteId, const FString& From, bool bOffshore = false);
	PORTSENGINE_API int32 CharityCost(const FPortsState& State, const FPortsPlayer& P);
	// Why the current house cannot take an action, or an empty string if it can.
	PORTSENGINE_API FString CheckAction(const FPortsState& State, const FPortsAction& Action);
	PORTSENGINE_API FString CheckShip(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& Action);
	PORTSENGINE_API FString CheckPost(const FPortsState& State, const FPortsPlayer* P, const FPortsAction& Action);
	PORTSENGINE_API FPortsResult PerformAction(FPortsState& State, const FPortsAction& Action);
	PORTSENGINE_API TArray<FPortsAction> LegalShipments(const FPortsState& State, const FPortsPlayer& P);
	PORTSENGINE_API TArray<FPortsAction> LegalPosts(const FPortsState& State, const FPortsPlayer& P);

	// ---------- turn.js ----------
	PORTSENGINE_API int32 ActionPointsFor(const FPortsState& State, const FPortsPlayer& P);
	// Moves on from the roundStart, chronicle, event and plague phases. False during the action phase.
	PORTSENGINE_API bool Advance(FPortsState& State);
	PORTSENGINE_API TArray<int32> HalvesOfRound(const FPortsState& State);
	PORTSENGINE_API FPortsResult Decide(FPortsState& State, EPortsChoice Choice);
	PORTSENGINE_API FPortsResult TimeUp(FPortsState& State);
	PORTSENGINE_API FPortsResult EndTurn(FPortsState& State);
}
