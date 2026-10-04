#include "PortsData.h"

#include "PortsProjection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, PortsEngine);

namespace
{
	const TCHAR* const DataFiles[] = {
		TEXT("config"), TEXT("facts"), TEXT("sources"), TEXT("cities"), TEXT("routes"), TEXT("timeline"),
		TEXT("events"), TEXT("rulebook"), TEXT("actions"), TEXT("music"), TEXT("map"),
	};

	void Flatten(const FPortsValue& Value, const FString& Path, TMap<FString, double>& Out)
	{
		if (Value.IsNumber()) Out.Add(Path, Value.AsNumber());
		else if (Value.IsBool()) Out.Add(Path, Value.AsBool() ? 1.0 : 0.0);
		else if (Value.IsObject())
		{
			for (int32 i = 0; i < Value.GetKeys().Num(); i++)
			{
				Flatten(Value.ValueAt(i), Path.IsEmpty() ? Value.GetKeys()[i] : Path + TEXT(".") + Value.GetKeys()[i], Out);
			}
		}
	}
}

FPortsData& FPortsData::Get()
{
	static FPortsData Instance;
	return Instance;
}

FString FPortsData::DefaultDataDir()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"));
}

bool FPortsData::EnsureLoaded()
{
	FPortsData& Data = Get();
	if (Data.IsLoaded()) return true;
	FString Error;
	if (Data.Load(DefaultDataDir(), Error)) return true;
	UE_LOG(LogTemp, Error, TEXT("Ports of Plague: %s"), *Error);
	return false;
}

const FPortsCity* FPortsData::FindCity(const FString& Id) const
{
	const int32* Index = CityIndex.Find(Id);
	return Index ? &Cities[*Index] : nullptr;
}

int32 FPortsData::CityIndexOf(const FString& Id) const
{
	const int32* Index = CityIndex.Find(Id);
	return Index ? *Index : INDEX_NONE;
}

const FPortsRoute* FPortsData::FindRoute(const FString& Id) const
{
	const int32* Index = RouteIndex.Find(Id);
	return Index ? &Routes[*Index] : nullptr;
}

const TArray<int32>& FPortsData::RoutesFrom(const FString& CityId) const
{
	static const TArray<int32> None;
	const TArray<int32>* Found = RoutesByCity.Find(CityId);
	return Found ? *Found : None;
}

double FPortsData::Number(const TCHAR* Path) const
{
	const double* Found = ConfigNumbers.Find(Path);
	checkf(Found, TEXT("config.json has no number at %s"), Path);
	return Found ? *Found : 0.0;
}

bool FPortsData::TryNumber(const FString& Path, double& Out) const
{
	const double* Found = ConfigNumbers.Find(Path);
	if (Found) Out = *Found;
	return Found != nullptr;
}

bool FPortsData::Load(const FString& DataDir, FString& OutError)
{
	bLoaded = false;
	Files.Reset();
	Cities.Reset();
	Routes.Reset();
	HomeCities.Reset();
	CityIndex.Reset();
	RouteIndex.Reset();
	RoutesByCity.Reset();
	ConfigNumbers.Reset();

	for (const TCHAR* Name : DataFiles)
	{
		const FString Path = FPaths::Combine(DataDir, FString(Name) + TEXT(".json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("Could not read %s."), *Path);
			return false;
		}
		FPortsValue Root;
		if (!FPortsValue::Parse(Text, Root) || !Root.IsObject())
		{
			OutError = FString::Printf(TEXT("%s is not valid JSON."), *Path);
			return false;
		}
		Files.Add(Name, MoveTemp(Root));
	}

	Flatten(Config(), FString(), ConfigNumbers);

	for (const FPortsValue& C : Files[TEXT("cities")].Get(TEXT("cities")).GetItems())
	{
		FPortsCity City;
		City.Raw = C;
		City.Id = C.Get(TEXT("id")).AsString();
		City.Name = C.Get(TEXT("name")).AsString();
		City.Modern = C.Get(TEXT("modern")).AsString();
		City.Region = C.Get(TEXT("region")).AsString();
		City.Lat = C.Get(TEXT("lat")).AsNumber();
		City.Lon = C.Get(TEXT("lon")).AsNumber();
		const FPortsValue& Arrival = C.Get(TEXT("arrival"));
		City.ArrivalRound = Arrival.Get(TEXT("round")).AsInt();
		City.ArrivalDateText = Arrival.Get(TEXT("dateText")).AsString();
		City.ArrivalFactIds = Arrival.Get(TEXT("factIds"));
		City.SeverityMod = C.Get(TEXT("severityMod")).AsInt();
		City.bHome = C.Get(TEXT("home")).IsObject();
		const FPortsValue& MapInfo = C.Get(TEXT("map"));
		City.MapDx = MapInfo.Get(TEXT("dx")).AsNumber();
		City.MapDy = MapInfo.Get(TEXT("dy")).AsNumber();
		if (MapInfo.Get(TEXT("label")).IsString()) City.MapLabel = MapInfo.Get(TEXT("label")).AsString();
		if (City.bHome) HomeCities.Add(City.Id);
		CityIndex.Add(City.Id, Cities.Num());
		Cities.Add(MoveTemp(City));
	}

	for (const FPortsValue& R : Files[TEXT("routes")].Get(TEXT("routes")).GetItems())
	{
		FPortsRoute Route;
		Route.Id = R.Get(TEXT("id")).AsString();
		Route.A = R.Get(TEXT("a")).AsString();
		Route.B = R.Get(TEXT("b")).AsString();
		Route.Type = R.Get(TEXT("type")).AsString();
		Route.bSea = Route.Type == TEXT("sea");
		Route.Value = R.Get(TEXT("value")).AsInt();
		for (const FPortsValue& Point : R.Get(TEXT("via")).GetItems())
		{
			if (Point.Num() == 2) Route.Via.Add(FVector2D(Point[0].AsNumber(), Point[1].AsNumber()));
		}
		if (!CityIndex.Contains(Route.A) || !CityIndex.Contains(Route.B))
		{
			OutError = FString::Printf(TEXT("Route %s joins a city that is not in cities.json."), *Route.Id);
			return false;
		}
		const int32 Index = Routes.Num();
		RouteIndex.Add(Route.Id, Index);
		RoutesByCity.FindOrAdd(Route.A).Add(Index);
		RoutesByCity.FindOrAdd(Route.B).Add(Index);
		Routes.Add(MoveTemp(Route));
	}

	const FPortsValue& MapFile = Files[TEXT("map")];
	Map.Width = MapFile.Get(TEXT("width")).AsInt();
	Map.Height = MapFile.Get(TEXT("height")).AsInt();
	Map.Credit = MapFile.Get(TEXT("credit")).AsString();
	Map.Land = MapFile.Get(TEXT("land")).AsString();
	Map.Lakes = MapFile.Get(TEXT("lakes")).AsString();
	Map.Rivers = MapFile.Get(TEXT("rivers")).AsString();

	// The coastlines were generated with the web version's projection: make
	// sure this build's copy of it still agrees.
	if (Map.Width != static_cast<int32>(PortsProjection::MapWidth()) || Map.Height != static_cast<int32>(PortsProjection::MapHeight()))
	{
		OutError = FString::Printf(TEXT("map.json is %d x %d, but the projection gives %d x %d."), Map.Width, Map.Height,
			static_cast<int32>(PortsProjection::MapWidth()), static_cast<int32>(PortsProjection::MapHeight()));
		return false;
	}

	bLoaded = true;
	return true;
}
