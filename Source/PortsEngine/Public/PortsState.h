// The game state and its helpers: src/engine/state.js and rng.js of the web
// version. The whole game is one state object. Its fields have the same names,
// meaning and order as in state.js, so a state written out by ToValue() is the
// same JSON the web version produces: the players' phones draw their screens
// from it in multi-device play, and saved games use it too.
#pragma once

#include "CoreMinimal.h"
#include "PortsData.h"
#include "PortsValue.h"

// A JavaScript object used as a small map: keys keep the order they were added in.
template <typename T>
struct TPortsMap
{
	TArray<FString> Keys;
	TArray<T> Values;

	int32 Num() const { return Keys.Num(); }
	int32 IndexOf(const FString& Key) const { return Keys.IndexOfByPredicate([&Key](const FString& K) { return K.Equals(Key, ESearchCase::CaseSensitive); }); }
	bool Contains(const FString& Key) const { return IndexOf(Key) != INDEX_NONE; }
	const T* Find(const FString& Key) const { const int32 i = IndexOf(Key); return i == INDEX_NONE ? nullptr : &Values[i]; }
	T Get(const FString& Key, const T& Default = T()) const { const int32 i = IndexOf(Key); return i == INDEX_NONE ? Default : Values[i]; }
	void Set(const FString& Key, const T& Value)
	{
		const int32 i = IndexOf(Key);
		if (i != INDEX_NONE) Values[i] = Value;
		else { Keys.Add(Key); Values.Add(Value); }
	}
	void Remove(const FString& Key)
	{
		const int32 i = IndexOf(Key);
		if (i != INDEX_NONE) { Keys.RemoveAt(i); Values.RemoveAt(i); }
	}
	void Reset() { Keys.Reset(); Values.Reset(); }
};

struct FPortsNextShip { int32 profit = 0; bool safe = false; };
struct FPortsLoan { int32 owed = 0; int32 due = 0; };
struct FPortsDeal { int32 partner = 0; int32 until = 0; };
struct FPortsGates { FString city; int32 until = 0; };

struct FPortsStats
{
	int32 shipments = 0, infected = 0, earned = 0, fled = 0, protectedCount = 0, charity = 0, spread = 0, fortune = 0,
		married = 0, land = 0, loans = 0, defaults = 0, deals = 0, gates = 0, offshore = 0;
};

struct FPortsPlayer
{
	int32 id = 0;
	FString name;
	FString color;
	FString colorName;
	FString crest;
	FString home;
	// bot: a computer plays this house (skill: "easy", "medium" or "hard").
	bool bot = false;
	// Empty means null.
	FString skill;
	FString strategy;
	int32 florins = 0;
	int32 reputation = 0;
	TPortsMap<int32> family;
	TArray<FString> posts;
	int32 lostFamily = 0;
	TPortsMap<bool> offers;
	TArray<FPortsValue> pending;
	int32 ap = 0;
	TArray<FString> shipped;
	TArray<FString> prepared;
	TArray<FString> physician;
	int32 charityThisTurn = 0;
	TPortsMap<bool> free;
	TOptional<FPortsNextShip> nextShip;
	TPortsMap<int32> personalCosts;
	bool englishBlocked = false;
	int32 marriedThisTurn = 0;
	bool proposedThisTurn = false;
	TArray<FString> land;
	TOptional<FPortsLoan> loan;
	TOptional<FPortsDeal> deal;
	TOptional<FPortsGates> gates;
	FPortsStats stats;
};

struct FPortsCityState
{
	FString state = TEXT("safe");
	int32 severity = 0;
	int32 strickenFor = 0;
	bool early = false;
	int32 unrest = 0;
};

struct FPortsByRouteType
{
	int32 all = 0, sea = 0, land = 0;
	int32& Of(const FString& Type) { return Type == TEXT("sea") ? sea : Type == TEXT("land") ? land : all; }
	int32 Of(const FString& Type) const { return Type == TEXT("sea") ? sea : Type == TEXT("land") ? land : all; }
};

struct FPortsCityProfit
{
	TArray<FString> cities;
	bool onlyStricken = false;
	int32 profit = 0;
};

struct FPortsEffects
{
	FPortsByRouteType profit;
	FPortsByRouteType contagion;
	TPortsMap<int32> costs;
	int32 charityBonus = 0;
	int32 marketBonus = 0;
	bool noNewPostsNearPlague = false;
	TArray<FPortsCityProfit> cityProfit;
};

struct FPortsPersecution
{
	FString city;
	int32 round = 0;
	TArray<int32> protectors;
};

// One house's Legacy score (scorePlayer in scoring.js).
struct FPortsScore
{
	int32 wealth = 0, family = 0, reputation = 0, balance = 0, total = 0;
};

// One row of the final ranking (rankPlayers in scoring.js).
struct FPortsRank
{
	int32 id = 0;
	FString name;
	int32 wealth = 0, family = 0, reputation = 0, balance = 0, total = 0, familyCount = 0, rep = 0, place = 0;
};

struct PORTSENGINE_API FPortsState
{
	int32 version = 0;
	FString title;
	FString seed;
	uint32 rng = 0;
	FString difficulty;
	FString mode;
	int32 span = 1;
	// Half-years are numbered 1-12 (late 1347 to early 1353); the pre-plague
	// rounds use 0 (early 1347) and -1 (late 1346).
	int32 firstHalf = 1;
	int32 preRounds = 0;
	int32 turnSeconds = 0;
	int32 round = 0;
	int32 roundEnd = 0;
	FString phase;
	TArray<FPortsPlayer> players;
	TArray<int32> order;
	int32 turn = 0;
	// One entry per city, in cities.json order (an object keyed by city id in the JSON).
	TArray<FPortsCityState> cities;
	TArray<FString> deck;
	TArray<FString> fortuneDeck;
	// Empty means null.
	FString currentEvent;
	TArray<FString> currentChronicle;
	FPortsEffects effects;
	TOptional<FPortsPersecution> persecution;
	TArray<FPortsValue> log;
	int32 logSeq = 0;
	TArray<FString> journal;
	TOptional<TArray<int32>> winner;
	TOptional<TArray<FPortsRank>> finalScores;
	// These two only appear in the state once the first round has begun (and
	// the halfway point has passed), as in the web version. guildFavor may be null.
	bool bHasGuildFavor = false;
	TOptional<int32> guildFavor;
	bool bHasMidLast = false;
	int32 midLast = 0;

	FPortsCityState* City(const FString& Id);
	const FPortsCityState* City(const FString& Id) const;

	// The state as the JSON object the web version uses, and back.
	FPortsValue ToValue() const;
	FString ToJson() const { return ToValue().ToJson(); }
	static bool FromValue(const FPortsValue& Value, FPortsState& Out);
	static bool FromJson(const FString& Json, FPortsState& Out);
};

// A house as chosen on the setup screen.
struct FPortsSetupPlayer
{
	FString name;
	FString home;
	bool bot = false;
	FString skill;
	// Optional: left empty, a bot draws its playing style from its skill level.
	FString strategy;
	FString color;
	FString colorName;
	FString crest;
};

struct FPortsSetup
{
	TArray<FPortsSetupPlayer> players;
	FString difficulty = TEXT("chronicler");
	FString mode = TEXT("standard");
	FString seed;
	// true = the pre-plague rounds are played first.
	bool prePlague = false;
	// true = each turn has a time limit (config.turnTimer.seconds).
	bool timer = false;
	// The time limit in seconds when the players chose their own; 0 = config.turnTimer.seconds.
	int32 timerSeconds = 0;
};

struct FPortsPlayerStyle { const TCHAR* color; const TCHAR* colorName; const TCHAR* crest; };

// The label of a round (roundInfo in state.js).
struct FPortsRoundInfo
{
	bool bValid = false;
	int32 round = 0;
	bool pre = false;
	FString label;
	FString months;
	FString season;
	FString headline;
	FPortsValue factIds;
};

namespace Ports
{
	extern PORTSENGINE_API const TCHAR* const ESTATE;
	constexpr int32 SAVE_VERSION = 3;
	extern PORTSENGINE_API const FPortsPlayerStyle PLAYER_STYLES[6];

	// ---------- rng.js: seeded dice ----------
	PORTSENGINE_API uint32 SeedFrom(const FString& Value);
	PORTSENGINE_API double NextRandom(FPortsState& State);
	PORTSENGINE_API int32 Roll(FPortsState& State, int32 Sides = 6);
	PORTSENGINE_API TArray<FString> Shuffle(FPortsState& State, const TArray<FString>& List);
	// An index into a list of this length (pick in rng.js).
	PORTSENGINE_API int32 PickIndex(FPortsState& State, int32 Length);

	// ---------- state.js ----------
	PORTSENGINE_API const FString& OtherEnd(const FPortsRoute& Route, const FString& CityId);
	PORTSENGINE_API TArray<FString> Neighbors(const FString& CityId);

	// Returns a plain-language problem, or an empty string if the setup is fine.
	PORTSENGINE_API FString ValidateSetup(const TArray<FPortsSetupPlayer>& Players);
	PORTSENGINE_API bool CreateGame(const FPortsSetup& Setup, FPortsState& OutState, FString& OutProblem);
	PORTSENGINE_API void RollTurnOrder(FPortsState& State);
	PORTSENGINE_API FPortsEffects EmptyEffects();
	// Adds an entry to the log (and its facts to the journal). Returns the finished entry.
	PORTSENGINE_API FPortsValue AddLog(FPortsState& State, const FPortsValue& Entry);

	// The house whose turn it is, or null outside the action phase.
	PORTSENGINE_API FPortsPlayer* CurrentPlayer(FPortsState& State);
	PORTSENGINE_API const FPortsPlayer* CurrentPlayer(const FPortsState& State);

	PORTSENGINE_API int32 FamilyTotal(const FPortsPlayer& P);
	PORTSENGINE_API int32 FamilyAt(const FPortsPlayer& P, const FString& Loc);
	PORTSENGINE_API TArray<FString> FamilyLocations(const FPortsPlayer& P);

	PORTSENGINE_API bool IsStricken(const FPortsState& State, const FString& CityId);
	PORTSENGINE_API bool IsAftermath(const FPortsState& State, const FString& CityId);
	// A Safe city next to a Stricken one: shown on the map as a warning.
	PORTSENGINE_API bool IsThreatened(const FPortsState& State, const FString& CityId);
	// The house (if any, other than P) whose closed gates turn strangers away from a city.
	PORTSENGINE_API const FPortsPlayer* GatesClosedBy(const FPortsState& State, const FString& CityId, const FPortsPlayer* P = nullptr);
	// Half-year number at which something lasting `Rounds` rounds (counting this one) ends.
	PORTSENGINE_API int32 UntilRound(const FPortsState& State, int32 Rounds);
	PORTSENGINE_API bool IsPrePlague(const FPortsState& State);
	PORTSENGINE_API double PreSpan(const FPortsState& State);
	// The timeline entry of any half-year, including the pre-plague ones (undefined if there is none).
	PORTSENGINE_API const FPortsValue& HalfInfo(int32 Half);
	PORTSENGINE_API void ClampReputation(FPortsPlayer& P);
	PORTSENGINE_API int32 Cost(const FPortsState& State, const FString& Key, const FPortsPlayer* P = nullptr);
	// Action points an action takes (1 unless config.json says otherwise).
	PORTSENGINE_API int32 ApCost(const FString& Type);
	// Paths into config.json for the game's difficulty and mode, e.g. "difficulty.chronicler".
	PORTSENGINE_API FString DifficultyPath(const FPortsState& State);
	PORTSENGINE_API FString ModePath(const FPortsState& State);
	PORTSENGINE_API int32 TotalRounds(const FPortsState& State);
	PORTSENGINE_API int32 RoundNumber(const FPortsState& State);
	PORTSENGINE_API FPortsRoundInfo RoundInfo(const FPortsState& State);
}
