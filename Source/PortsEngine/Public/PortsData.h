// Loads every data file once, like src/data.js in the web version. The files
// in Content/Data are unchanged copies of the web game's data/ folder, so both
// versions read exactly the same numbers and facts.
#pragma once

#include "CoreMinimal.h"
#include "PortsValue.h"

struct FPortsCity
{
	FString Id;
	FString Name;
	FString Modern;
	FString Region;
	double Lat = 0;
	double Lon = 0;
	int32 ArrivalRound = 0;
	FString ArrivalDateText;
	FPortsValue ArrivalFactIds;
	int32 SeverityMod = 0;
	bool bHome = false;
	// The city's whole entry in cities.json.
	FPortsValue Raw;
	// Where the web map nudges the city (in map pixels) and puts its label.
	double MapDx = 0;
	double MapDy = 0;
	FString MapLabel = TEXT("right");
};

struct FPortsRoute
{
	FString Id;
	FString A;
	FString B;
	// "sea" or "land".
	FString Type;
	bool bSea = true;
	int32 Value = 0;
	// Points the route bends through, as (lon, lat).
	TArray<FVector2D> Via;
};

// data/map.json: the generated coastlines, as SVG path strings in map pixels.
struct FPortsMapArt
{
	int32 Width = 0;
	int32 Height = 0;
	FString Credit;
	FString Land;
	FString Lakes;
	FString Rivers;
};

class PORTSENGINE_API FPortsData
{
public:
	static FPortsData& Get();

	// Where the data files live (Content/Data).
	static FString DefaultDataDir();

	// Loads every file. Returns false and a plain-language reason on failure.
	bool Load(const FString& DataDir, FString& OutError);
	bool IsLoaded() const { return bLoaded; }
	// Loads from the default folder if nothing is loaded yet.
	static bool EnsureLoaded();

	const FPortsCity* FindCity(const FString& Id) const;
	int32 CityIndexOf(const FString& Id) const;
	const FPortsRoute* FindRoute(const FString& Id) const;
	// The routes that touch a city, in routes.json order (ROUTES_BY_CITY in state.js).
	const TArray<int32>& RoutesFrom(const FString& CityId) const;

	TArray<FPortsCity> Cities;
	TArray<FPortsRoute> Routes;
	TArray<FString> HomeCities;
	FPortsMapArt Map;

	// Every file as parsed JSON, keyed by file name without ".json".
	TMap<FString, FPortsValue> Files;

	// The same views of the files as DATA in src/data.js.
	const FPortsValue& Config() const { return Files[TEXT("config")]; }
	const FPortsValue& Timeline() const { return Files[TEXT("timeline")]; }
	const FPortsValue& Chronicle() const { return Files[TEXT("events")].Get(TEXT("chronicle")); }
	const FPortsValue& Deck() const { return Files[TEXT("events")].Get(TEXT("deck")); }
	const FPortsValue& Fortune() const { return Files[TEXT("events")].Get(TEXT("fortune")); }
	const FPortsValue& Remedies() const { return Files[TEXT("actions")].Get(TEXT("remedies")); }
	const FPortsValue& Actions() const { return Files[TEXT("actions")].Get(TEXT("actions")); }
	const FPortsValue& Facts() const { return Files[TEXT("facts")].Get(TEXT("facts")); }

	// A number from config.json by its path, e.g. Number(TEXT("costs.openPost")).
	// Stops with an error if the path is missing, so a typing mistake cannot pass unnoticed.
	double Number(const TCHAR* Path) const;
	int32 Int(const TCHAR* Path) const { return static_cast<int32>(Number(Path)); }
	// The same, for values that config.json may leave out.
	bool TryNumber(const FString& Path, double& Out) const;

private:
	bool bLoaded = false;
	TMap<FString, int32> CityIndex;
	TMap<FString, int32> RouteIndex;
	TMap<FString, TArray<int32>> RoutesByCity;
	TMap<FString, double> ConfigNumbers;
};
