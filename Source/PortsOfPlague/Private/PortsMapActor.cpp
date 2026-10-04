#include "PortsMapActor.h"

#include "PortsData.h"
#include "PortsState.h"
#include "PortsUi.h"
#include "PortsMapGeometry.h"
#include "PortsMapSpace.h"
#include "PortsTerrain.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Algo/Reverse.h"

using namespace PortsMapSpace;
using PortsGeo::Hex;

namespace
{
	enum ESection { Sea = 0, Lakes, Rivers, Routes, Margin, Table, TableShadow, Cartouche, Compass, Highlight, Rings };
	// Sections of the models' mesh (which casts shadows): the land itself and what stands on it.
	enum EModel { Land = 0, Trees };

	// Things lying on the board sit this far above the ground under them, in map pixels.
	constexpr double RouteLift = 0.55;
	constexpr double RiverLift = 0.22;

	// The painted map reaches this far past the web map's frame, in map pixels:
	// the coastlines in map.json are drawn that far out. Around it runs the
	// blank margin of the parchment sheet, and under the sheet is a table.
	constexpr double LandMargin = 420.0;
	constexpr double SeaMargin = LandMargin;
	constexpr double SheetMargin = 78.0;
	constexpr double TableZ = -1.4;
	constexpr double TableSize = 40000.0;

	// One point on the sheet's edge: where it meets the painted map, and which way is out.
	struct FEdgePoint
	{
		FVector2D Inner;
		FVector2D Out;
		double Along = 0;
	};

	// Walks once round a rectangle, rounding its corners, in steps of about `Step`.
	TArray<FEdgePoint> WalkEdge(const FVector2D& Min, const FVector2D& Max, double Step)
	{
		TArray<FEdgePoint> Points;
		const FVector2D Corners[4] = { FVector2D(Min.X, Min.Y), FVector2D(Max.X, Min.Y), FVector2D(Max.X, Max.Y), FVector2D(Min.X, Max.Y) };
		const FVector2D Normals[4] = { FVector2D(0, -1), FVector2D(1, 0), FVector2D(0, 1), FVector2D(-1, 0) };
		double Along = 0;
		for (int32 Side = 0; Side < 4; Side++)
		{
			const FVector2D From = Corners[Side], To = Corners[(Side + 1) % 4];
			const double Len = FVector2D::Distance(From, To);
			const int32 Steps = FMath::Max(1, FMath::RoundToInt32(Len / Step));
			for (int32 i = 0; i < Steps; i++)
			{
				Points.Add({ FMath::Lerp(From, To, static_cast<double>(i) / Steps), Normals[Side], Along + Len * i / Steps });
			}
			Along += Len;
			// The corner: turn from this side's direction to the next.
			const FVector2D Next = Normals[(Side + 1) % 4];
			for (int32 i = 0; i <= 8; i++)
			{
				const double T = i / 8.0;
				Points.Add({ To, (Normals[Side] * FMath::Cos(T * UE_DOUBLE_HALF_PI) + Next * FMath::Sin(T * UE_DOUBLE_HALF_PI)).GetSafeNormal(), Along + T * Step * 4 });
			}
			Along += Step * 4;
		}
		return Points;
	}
}

APortsMapActor::APortsMapActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Surface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Surface"));
	SetRootComponent(Surface);
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Surface->SetCastShadow(false);
	Models = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Models"));
	Models->SetupAttachment(Surface);
	Models->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Models->SetCastShadow(true);
	Towns = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Towns"));
	Towns->SetupAttachment(Surface);
	Towns->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Towns->SetCastShadow(true);
	Flags = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Flags"));
	Flags->SetupAttachment(Surface);
	Flags->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flags->SetCastShadow(false);
	Effects = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Effects"));
	Effects->SetupAttachment(Surface);
	Effects->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Effects->SetCastShadow(false);
	Effects->SetTranslucentSortPriority(5);
	Creature = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Creature"));
	Creature->SetupAttachment(Surface);
	Creature->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Creature->SetCastShadow(true);
	Props = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Props"));
	Props->SetupAttachment(Surface);
	Props->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Props->SetCastShadow(true);
	Props->SetTranslucentSortPriority(8);
}

// How high something lying on the board sits at a point: on the land, or on the sea.
double APortsMapActor::GroundAt(const FVector2D& Pixel, double Lift) const
{
	return FPortsTerrain::Get().Ground(Pixel, RouteZ - RouteLift) + Lift;
}

void APortsMapActor::BeginPlay()
{
	Super::BeginPlay();
	if (!FPortsData::Get().IsLoaded()) return;

	SurfaceMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ports/Materials/M_PortsVertexColor.M_PortsVertexColor"));
	ColorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ports/Materials/M_PortsColor.M_PortsColor"));
	ParchmentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ports/Materials/M_PortsParchment.M_PortsParchment"));
	FlagMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ports/Materials/M_PortsFlag.M_PortsFlag"));
	GlowMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ports/Materials/M_PortsGlow.M_PortsGlow"));
	GlazeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ports/Materials/M_PortsGlaze.M_PortsGlaze"));
	TableMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ports/Materials/M_PortsTable.M_PortsTable"));
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (!SurfaceMaterial || !ColorMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("Ports map: materials are missing. Run Tools/build_content.sh."));
	}

	// The coast and the heights of the land come first: everything else stands on them.
	TArray<TArray<FVector2D>> LandRings;
	PortsGeo::ParsePath(FPortsData::Get().Map.Land, LandRings);
	TArray<TArray<FVector2D>> LakeRings, RiverLines;
	PortsGeo::ParsePath(FPortsData::Get().Map.Lakes, LakeRings);
	PortsGeo::ParsePath(FPortsData::Get().Map.Rivers, RiverLines);
	FPortsTerrain::Get().Build(LandRings, LandMargin, LakeRings, RiverLines);

	BuildSea();
	BuildFrame();
	BuildLand();
	BuildForests();
	BuildWater();
	BuildRoutes();
	BuildDecorations();
	BuildCities();
	StainAmount.Init(0.0, FPortsData::Get().Cities.Num());
	StainOn.Init(false, FPortsData::Get().Cities.Num());
	StartTraffic();
}

FVector APortsMapActor::CityLocation(const FString& CityId) const
{
	const FPortsCity* City = FPortsData::Get().FindCity(CityId);
	return City ? ToWorld(CityPixel(*City), FPortsTerrain::Get().Height(CityPixel(*City))) : FVector::ZeroVector;
}

// The sea: real water. Its material reads how far each point is from the coast, so it is
// pale in the shallows, deep further out, and breaks in lines of surf along every shore.
void APortsMapActor::BuildSea()
{
	const double W = PortsProjection::MapWidth(), H = PortsProjection::MapHeight();
	PortsGeo::FMeshBuilder Mesh;
	const FVector2D Min(-SeaMargin, -SeaMargin), Max(W + SeaMargin, H + SeaMargin);
	const int32 First = Mesh.AddVertex(Min, 0, FVector::UpVector, FLinearColor::White);
	Mesh.AddVertex(FVector2D(Max.X, Min.Y), 0, FVector::UpVector, FLinearColor::White);
	Mesh.AddVertex(Max, 0, FVector::UpVector, FLinearColor::White);
	Mesh.AddVertex(FVector2D(Min.X, Max.Y), 0, FVector::UpVector, FLinearColor::White);
	Mesh.AddTriangle(First, First + 1, First + 2, FVector::UpVector);
	Mesh.AddTriangle(First, First + 2, First + 3, FVector::UpVector);

	UMaterialInterface* Water = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ports/Materials/M_PortsWater.M_PortsWater"));
	CoastField = FPortsTerrain::Get().MakeFieldTexture(this);
	if (Water && CoastField)
	{
		SeaPaint = UMaterialInstanceDynamic::Create(Water, this);
		SeaPaint->SetTextureParameterValue(TEXT("Field"), CoastField);
		SeaPaint->SetScalarParameterValue(TEXT("Still"), 0.f);
		LakePaint = UMaterialInstanceDynamic::Create(Water, this);
		LakePaint->SetTextureParameterValue(TEXT("Field"), CoastField);
		LakePaint->SetScalarParameterValue(TEXT("Still"), 1.f);
	}
	Mesh.Commit(Surface, ESection::Sea, SeaPaint ? static_cast<UMaterialInterface*>(SeaPaint) : SurfaceMaterial.Get());
}

// The edge of the board. The map is painted on a sheet of parchment: past the
// painted part is the sheet's blank margin, ruled with brown, gold and red
// lines like the frames on the website, ending in an uneven, darkened edge.
// The sheet lies on a walnut table and casts a soft shadow on it.
void APortsMapActor::BuildFrame()
{
	const double W = PortsProjection::MapWidth(), H = PortsProjection::MapHeight();
	const FVector2D Min(-LandMargin, -LandMargin), Max(W + LandMargin, H + LandMargin);
	const TArray<FEdgePoint> Edge = WalkEdge(Min, Max, 9.0);
	const int32 N = Edge.Num();

	// How far out the sheet reaches at each point: never quite straight, with a few nicks.
	TArray<FVector2D> Outer;
	Outer.Reserve(N);
	for (const FEdgePoint& P : Edge)
	{
		const float S = static_cast<float>(P.Along);
		const double Wobble = FMath::PerlinNoise1D(S * 0.006f) * 9.0 + FMath::PerlinNoise1D(S * 0.031f + 40.f) * 3.5 + FMath::PerlinNoise1D(S * 0.13f + 90.f) * 1.2;
		const double Nick = FMath::Max(0.0, FMath::PerlinNoise1D(S * 0.017f + 300.f) - 0.55) * 26.0;
		Outer.Add(P.Inner + P.Out * (SheetMargin + Wobble - Nick));
	}

	const FLinearColor Sheet = Hex(TEXT("#ecd9aa"));
	const FLinearColor Worn = Hex(TEXT("#d8bc7c"));
	const FLinearColor Burnt = Hex(TEXT("#8a6a36"));
	PortsGeo::FMeshBuilder Mesh;
	// Three rings of points: the painted map's edge, most of the way out, and the sheet's edge.
	for (int32 i = 0; i < N; i++)
	{
		Mesh.AddVertex(Edge[i].Inner, 0, FVector::UpVector, Sheet);
		Mesh.AddVertex(FMath::Lerp(Edge[i].Inner, Outer[i], 0.62), 0, FVector::UpVector, FMath::Lerp(Sheet, Worn, 0.35f));
		Mesh.AddVertex(FMath::Lerp(Edge[i].Inner, Outer[i], 0.93), 0, FVector::UpVector, Worn);
		Mesh.AddVertex(Outer[i], 0, FVector::UpVector, Burnt);
	}
	for (int32 i = 0; i < N; i++)
	{
		const int32 A = i * 4, B = ((i + 1) % N) * 4;
		for (int32 Ring = 0; Ring < 3; Ring++)
		{
			Mesh.AddTriangle(A + Ring, A + Ring + 1, B + Ring, FVector::UpVector);
			Mesh.AddTriangle(A + Ring + 1, B + Ring + 1, B + Ring, FVector::UpVector);
		}
	}
	// The thickness of the sheet.
	Mesh.AddSkirt(Outer, 0, TableZ, Hex(TEXT("#5a3920")));

	// Ruled lines round the painted map.
	const auto Rule = [&](double Offset, double Width, const TCHAR* Color, double Z)
	{
		const TArray<FVector2D> Box = { Min + FVector2D(-Offset, -Offset), FVector2D(Max.X + Offset, Min.Y - Offset), Max + FVector2D(Offset, Offset), FVector2D(Min.X - Offset, Max.Y + Offset) };
		Mesh.AddRibbon(Box, Width, Z, Hex(Color), true);
	};
	Rule(0.0, 2.2, TEXT("#4d3a22"), 0.35);
	Rule(9.0, 1.4, TEXT("#8a6a36"), 0.35);
	Rule(15.0, 4.2, TEXT("#d9a82b"), 0.35);
	Rule(21.0, 1.2, TEXT("#b0261a"), 0.35);
	// A red and gold lozenge at each corner.
	for (const FVector2D& Corner : { FVector2D(Min.X - 15, Min.Y - 15), FVector2D(Max.X + 15, Min.Y - 15), FVector2D(Max.X + 15, Max.Y + 15), FVector2D(Min.X - 15, Max.Y + 15) })
	{
		Mesh.AddDisc(Corner, 11.0, 0.7, Hex(TEXT("#d9a82b")), 4);
		Mesh.AddDisc(Corner, 7.5, 1.05, Hex(TEXT("#b0261a")), 4);
		Mesh.AddDisc(Corner, 3.0, 1.4, Hex(TEXT("#f3d27a")), 4);
	}
	Mesh.Commit(Surface, ESection::Margin, ParchmentMaterial ? ParchmentMaterial : SurfaceMaterial);

	// The table, and the sheet's soft shadow on it (the light comes from the north-west).
	UMaterialInterface* TableLook = TableMaterial ? TableMaterial : SurfaceMaterial;
	const FLinearColor Lit = TableMaterial ? FLinearColor::White : Hex(TEXT("#3b2413"));
	PortsGeo::FMeshBuilder Table;
	const FVector2D Centre(W * 0.5, H * 0.5);
	const int32 First = Table.AddVertex(Centre + FVector2D(-TableSize, -TableSize), TableZ, FVector::UpVector, Lit);
	Table.AddVertex(Centre + FVector2D(TableSize, -TableSize), TableZ, FVector::UpVector, Lit);
	Table.AddVertex(Centre + FVector2D(TableSize, TableSize), TableZ, FVector::UpVector, Lit);
	Table.AddVertex(Centre + FVector2D(-TableSize, TableSize), TableZ, FVector::UpVector, Lit);
	Table.AddTriangle(First, First + 1, First + 2, FVector::UpVector);
	Table.AddTriangle(First, First + 2, First + 3, FVector::UpVector);
	Table.Commit(Surface, ESection::Table, TableLook);

	PortsGeo::FMeshBuilder Shadow;
	const FVector2D Cast(4.0, 5.0);
	const FLinearColor Dark = Lit * 0.3f;
	for (int32 i = 0; i < N; i++)
	{
		Shadow.AddVertex(Outer[i] - Edge[i].Out * 6.0 + Cast, TableZ + 0.35, FVector::UpVector, Dark);
		Shadow.AddVertex(Outer[i] + Edge[i].Out * 5.0 + Cast, TableZ + 0.35, FVector::UpVector, FMath::Lerp(Dark, Lit, 0.45f));
		Shadow.AddVertex(Outer[i] + Edge[i].Out * 22.0 + Cast, TableZ + 0.35, FVector::UpVector, Lit);
	}
	for (int32 i = 0; i < N; i++)
	{
		const int32 A = i * 3, B = ((i + 1) % N) * 3;
		for (int32 Ring = 0; Ring < 2; Ring++)
		{
			Shadow.AddTriangle(A + Ring, A + Ring + 1, B + Ring, FVector::UpVector);
			Shadow.AddTriangle(A + Ring + 1, B + Ring + 1, B + Ring, FVector::UpVector);
		}
	}
	Shadow.Commit(Surface, ESection::TableShadow, TableLook);
}

void APortsMapActor::BuildLand()
{
	const FPortsMapArt& Art = FPortsData::Get().Map;
	const FPortsTerrain& Terrain = FPortsTerrain::Get();
	TArray<TArray<FVector2D>> Rings;
	PortsGeo::ParsePath(Art.Land, Rings);

	// Points inside the land, so that it can rise and fall: closer together where the mountains stand.
	FBox2D Bounds(ForceInit);
	for (const TArray<FVector2D>& Ring : Rings) for (const FVector2D& P : Ring) Bounds += P;
	TArray<FVector2D> Inner;
	const double Spacing = 9.0;
	FRandomStream Random(1348);
	for (double Y = Bounds.Min.Y + 0.53; Y < Bounds.Max.Y; Y += Spacing)
	{
		for (double X = Bounds.Min.X + 0.37; X < Bounds.Max.X; X += Spacing)
		{
			const FVector2D P(X, Y);
			if (Terrain.Coast(P) > 1.5) Inner.Add(P);
			if (Terrain.Mountain(P) > 0.04 || Terrain.Mountain(P + FVector2D(Spacing, Spacing)) > 0.04)
			{
				for (double Dy = 0; Dy < Spacing; Dy += 3.0) for (double Dx = 0; Dx < Spacing; Dx += 3.0)
				{
					if (Dx == 0 && Dy == 0) continue;
					const FVector2D Q = P + FVector2D(Dx + Random.FRandRange(-0.6f, 0.6f), Dy + Random.FRandRange(-0.6f, 0.6f));
					if (Terrain.Coast(Q) > 1.5) Inner.Add(Q);
				}
			}
		}
	}

	TArray<FVector2D> Points;
	TArray<int32> Triangles;
	PortsGeo::Triangulate(Rings, Inner, Points, Triangles);

	// The web map's parchment: lightest a little above and left of the middle, darker at the edges.
	// Mountains are drawn in a warmer brown with pale tops; woodland is faintly green.
	const FLinearColor Stops[3] = { Hex(TEXT("#f6e7bd")), Hex(TEXT("#ead08e")), Hex(TEXT("#d9b56a")) };
	const FLinearColor Rock = Hex(TEXT("#b99a6a")), Snow = Hex(TEXT("#fbf6e8")), Wood = Hex(TEXT("#c3bd84"));
	const FVector2D Size = Bounds.GetSize();
	const FVector2D Centre = Bounds.Min + Size * 0.45;
	const auto LandColor = [&](const FVector2D& P)
	{
		const FVector2D D((P.X - Centre.X) / Size.X, (P.Y - Centre.Y) / Size.Y);
		const double T = FMath::Clamp(D.Size() / 0.8, 0.0, 1.0);
		FLinearColor C = T < 0.7 ? FMath::Lerp(Stops[0], Stops[1], T / 0.7) : FMath::Lerp(Stops[1], Stops[2], (T - 0.7) / 0.3);
		const double M = Terrain.Mountain(P);
		// High ground is tinted only a little, like the layer colours of a relief map: the light does the rest.
		C = FMath::Lerp(C, Rock, static_cast<float>(0.34 * FMath::SmoothStep(0.05, 0.6, M)));
		C = FMath::Lerp(C, Snow, static_cast<float>(0.75 * FMath::SmoothStep(0.72, 0.98, M)));
		C = FMath::Lerp(C, Wood, static_cast<float>(Terrain.Forest(P) * 0.42));
		// Slopes are shaded as a map-maker would hatch them: the steeper, the darker.
		const double Steep = 1.0 - Terrain.Normal(P).Z;
		return C * static_cast<float>(1.0 - 0.16 * FMath::SmoothStep(0.03, 0.5, Steep));
	};

	PortsGeo::FMeshBuilder Mesh;
	for (const FVector2D& P : Points) Mesh.AddVertex(P, Terrain.Height(P), Terrain.Normal(P), LandColor(P));
	for (int32 i = 0; i + 2 < Triangles.Num(); i += 3) Mesh.AddTriangle(Triangles[i], Triangles[i + 1], Triangles[i + 2], FVector::UpVector);
	// The coast: a low bank in the web map's coastline brown.
	const FLinearColor Coast = Hex(TEXT("#6b4a1f"));
	for (const TArray<FVector2D>& Ring : Rings) Mesh.AddSkirt(Ring, LandZ, 0, Coast);
	Mesh.Commit(Models, EModel::Land, ParchmentMaterial ? ParchmentMaterial : SurfaceMaterial);
	UE_LOG(LogTemp, Log, TEXT("Ports map: land has %d outlines, %d triangles."), Rings.Num(), Triangles.Num() / 3);
}

// Woods: small trees scattered thickly in the heart of each forest and thinly towards its edge, in the
// soft olive of a painted map rather than a bright green, so the woods sit in the land like the hills do.
void APortsMapActor::BuildForests()
{
	const FPortsTerrain& Terrain = FPortsTerrain::Get();
	PortsGeo::FMeshBuilder Mesh;
	FRandomStream Random(1349);
	const FLinearColor Trunk = Hex(TEXT("#8a7350"));
	const FLinearColor Greens[3] = { Hex(TEXT("#8f9462")), Hex(TEXT("#7f8a58")), Hex(TEXT("#9a9c68")) };
	int32 Count = 0;
	for (const FPortsTerrain::FPatch& Wood : Terrain.Forests())
	{
		const FVector2D Reach(13.0 * Wood.Count + 8.0, 17.0);
		for (int32 i = 0; i < Wood.Count * 60; i++)
		{
			const FVector2D P = Wood.Centre + FVector2D(Random.FRandRange(-1.f, 1.f) * Reach.X, Random.FRandRange(-1.f, 1.f) * Reach.Y);
			// The thicker the wood here, the likelier a tree.
			const double Thick = Terrain.Forest(P);
			if (Random.FRand() > Thick * 1.1) continue;
			if (Terrain.Coast(P) < 3.0 || Terrain.Mountain(P) > 0.6 || !CityAt(P, 17.0).IsEmpty()) continue;
			const double S = Random.FRandRange(0.6f, 0.95f) * (0.75 + 0.25 * Thick);
			const FVector Foot(P.X, P.Y, Terrain.Height(P));
			const FLinearColor Green = Greens[Random.RandRange(0, 2)] * Random.FRandRange(0.92f, 1.05f);
			Mesh.AddRound(Foot, 0.3 * S, 0.24 * S, 1.0 * S, Trunk, 5, false);
			Mesh.AddRound(Foot + FVector(0, 0, 0.8 * S), 1.5 * S, 0, 2.3 * S, Green, 7);
			Mesh.AddRound(Foot + FVector(0, 0, 2.0 * S), 1.05 * S, 0, 2.0 * S, Green * 1.06f, 7);
			Count++;
		}
	}
	Mesh.Commit(Models, EModel::Trees, SurfaceMaterial);
	UE_LOG(LogTemp, Log, TEXT("Ports map: planted %d trees."), Count);
}

void APortsMapActor::BuildWater()
{
	const FPortsMapArt& Art = FPortsData::Get().Map;
	const FPortsTerrain& Terrain = FPortsTerrain::Get();

	// Lakes are the same water as the sea, lying still in the land.
	TArray<TArray<FVector2D>> Lakes;
	PortsGeo::ParsePath(Art.Lakes, Lakes);
	PortsGeo::FMeshBuilder Lake;
	for (const TArray<FVector2D>& Ring : Lakes)
	{
		TArray<TArray<FVector2D>> One = { Ring };
		TArray<FVector2D> Points;
		TArray<int32> Triangles;
		if (!PortsGeo::Triangulate(One, TArray<FVector2D>(), Points, Triangles)) continue;
		// Each lake lies level, just clear of the highest ground on its shore.
		double Level = 0;
		for (const FVector2D& P : Points) Level = FMath::Max(Level, Terrain.Height(P));
		const int32 First = Lake.Vertices.Num();
		for (const FVector2D& P : Points) Lake.AddVertex(P, Level + 0.2, FVector::UpVector, FLinearColor::White);
		for (int32 i = 0; i + 2 < Triangles.Num(); i += 3) Lake.AddTriangle(First + Triangles[i], First + Triangles[i + 1], First + Triangles[i + 2], FVector::UpVector);
	}
	if (Lake.Vertices.Num()) Lake.Commit(Surface, ESection::Lakes, LakePaint ? static_cast<UMaterialInterface*>(LakePaint) : SurfaceMaterial.Get());

	// Rivers run over the land, following its rise and fall.
	PortsGeo::FMeshBuilder Mesh;
	TArray<TArray<FVector2D>> Rivers;
	PortsGeo::ParsePath(Art.Rivers, Rivers);
	const FLinearColor RiverColor = Hex(TEXT("#3f8ea6"));
	const auto Bed = [&Terrain](const FVector2D& P) { return Terrain.Height(P) + RiverLift; };
	for (const TArray<FVector2D>& River : Rivers) Mesh.AddRibbonOn(River, 1.5, Bed, RiverColor);
	Mesh.Commit(Surface, ESection::Rivers, SurfaceMaterial);
}

// Whether a point lies on the mound a town stands on: roads and sea lanes stop at its edge.
bool APortsMapActor::InTown(const FVector2D& Pixel) const
{
	for (const FPortsCity& City : FPortsData::Get().Cities)
	{
		const double Reach = (City.bHome ? 1.2 : 1.0) * 8.2 + 2.9;
		if (FVector2D::DistSquared(CityPixel(City), Pixel) < Reach * Reach) return true;
	}
	return false;
}

// Routes are drawn as on the web map: sea routes as a line of dark blue dots,
// land routes as brown dashes.
void APortsMapActor::BuildRoutes()
{
	const FPortsData& Data = FPortsData::Get();
	const FLinearColor SeaColor = Hex(TEXT("#10375c"));
	const FLinearColor LandColor = Hex(TEXT("#6b4423"));
	PortsGeo::FMeshBuilder Mesh;
	for (const FPortsRoute& Route : Data.Routes)
	{
		TArray<FVector2D> Points;
		Points.Add(CityPixel(*Data.FindCity(Route.A)));
		for (const FVector2D& LonLat : Route.Via) Points.Add(PortsProjection::Project(LonLat.X, LonLat.Y));
		Points.Add(CityPixel(*Data.FindCity(Route.B)));
		const TArray<FVector2D> Curve = PortsGeo::SmoothCurve(Points);
		const double Total = PortsGeo::Length(Curve);

		if (Route.bSea)
		{
			for (double D = 0; D <= Total; D += 6.0)
			{
				const FVector2D P = PortsGeo::PointAtLength(Curve, D);
				if (InTown(P)) continue;
				Mesh.AddDisc(P, 1.6, GroundAt(P, RouteLift + 0.1), SeaColor, 8);
			}
		}
		else
		{
			const auto Road = [this](const FVector2D& P) { return GroundAt(P, RouteLift); };
			// Cut into short lengths so that a dash stops at a town's wall instead of running through it.
			for (double D = 0; D < Total; D += 12.0) for (double Bit = D; Bit < FMath::Min(D + 7.0, Total); Bit += 1.75)
			{
				const double To = FMath::Min3(Bit + 1.75, D + 7.0, Total);
				if (InTown(PortsGeo::PointAtLength(Curve, Bit)) || InTown(PortsGeo::PointAtLength(Curve, To))) continue;
				Mesh.AddRibbonOn(PortsGeo::Slice(Curve, Bit, To), 2.4, Road, LandColor);
			}
		}

		FPortsRouteBadge Badge;
		Badge.RouteId = Route.Id;
		const FVector2D Middle = PortsGeo::PointAtLength(Curve, Total * 0.5);
		Badge.Position = ToWorld(Middle, GroundAt(Middle, RouteLift));
		Badge.Value = Route.Value;
		Badge.bSea = Route.bSea;
		RouteBadges.Add(Badge);
		RouteCurves.Add(Route.Id, Curve);
	}
	Mesh.Commit(Surface, ESection::Routes, SurfaceMaterial);
}

// The title cartouche and the compass rose, placed and sized as on the web map
// (createMap in map.js). The pictures are the web version's own drawings.
void APortsMapActor::BuildDecorations()
{
	UMaterialInterface* ArtMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Ports/Materials/M_PortsMapArt.M_PortsMapArt"));
	if (!ArtMaterial) return;
	const auto Place = [&](int32 Section, const TCHAR* TexturePath, const FVector2D& LonLat, const FVector2D& Offset, const FVector2D& Size)
	{
		UTexture2D* Picture = LoadObject<UTexture2D>(nullptr, TexturePath);
		if (!Picture) return;
		PortsGeo::FMeshBuilder Mesh;
		Mesh.AddPicture(PortsProjection::Project(LonLat.X, LonLat.Y) + Offset, Size, 3.4);
		UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(ArtMaterial, this);
		Paint->SetTextureParameterValue(TEXT("Art"), Picture);
		Mesh.Commit(Surface, Section, Paint);
	};
	// Both lie a little above the board, like plaques laid on it, so the small northern islands do not show through them.
	// The cartouche is drawn 288 x 72 around its anchor; the compass 128 x 128 (its middle 6 below the picture's), at 0.8 size.
	Place(ESection::Cartouche, TEXT("/Game/Ports/Textures/T_MapCartouche.T_MapCartouche"), FVector2D(-4.6, 60.6), FVector2D(0, 0), FVector2D(288, 72));
	Place(ESection::Compass, TEXT("/Game/Ports/Textures/T_MapCompass.T_MapCompass"), FVector2D(-8.4, 57.0), FVector2D(0, -6 * 0.8), FVector2D(128, 128) * 0.8);
}

UMaterialInstanceDynamic* APortsMapActor::AddBlock(UStaticMesh* Shape, const FVector2D& Pixel, double BaseHeight, const FVector& SizeInPixels, const FLinearColor& Color, UStaticMeshComponent** OutBlock)
{
	if (!Shape) return nullptr;
	UStaticMeshComponent* Block = NewObject<UStaticMeshComponent>(this);
	Block->SetStaticMesh(Shape);
	Block->SetMobility(EComponentMobility::Movable);
	Block->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Block->SetupAttachment(GetRootComponent());
	// The engine's basic shapes are 100 units across and centred on their middle.
	Block->SetWorldScale3D(SizeInPixels * UnitsPerPixel / 100.0);
	Block->SetWorldLocation(ToWorld(Pixel, BaseHeight + SizeInPixels.Z * 0.5));
	UMaterialInstanceDynamic* Paint = nullptr;
	if (ColorMaterial)
	{
		Paint = UMaterialInstanceDynamic::Create(ColorMaterial, this);
		Paint->SetVectorParameterValue(TEXT("Color"), Color);
		Block->SetMaterial(0, Paint);
		Paints.Add(Paint);
	}
	Block->RegisterComponent();
	if (OutBlock) *OutBlock = Block;
	return Paint;
}

// How large a city's model is: home cities are a little larger.
static double TownScale(const FPortsCity& City) { return City.bHome ? 1.2 : 1.0; }
static constexpr double TownRadius = 8.2;

// A walled town: a ring of battlemented wall with round towers and a gatehouse facing south,
// and inside it a huddle of houses round a church. Home cities are larger, with red roofs and
// a twin-towered cathedral. Look is 0 as built, 1 Stricken (dark reds), 2 Aftermath (grey).
void APortsMapActor::BuildTown(int32 Index, int32 Look)
{
	const FPortsCity& City = FPortsData::Get().Cities[Index];
	const FVector2D At = CityPixel(City);
	const double S = TownScale(City), R = TownRadius * S;
	const double Floor = FPortsTerrain::Get().Height(At) + 0.5;
	FRandomStream Random(7919 * (Index + 1));

	// Colours: the web map's pale stone, with brown roofs (red for home cities).
	FLinearColor Stone = Hex(City.bHome ? TEXT("#fff3d2") : TEXT("#f4e8c8"));
	FLinearColor Shade = Hex(TEXT("#d9c79a"));
	FLinearColor Roof = Hex(City.bHome ? TEXT("#b0261a") : TEXT("#a0603a"));
	FLinearColor RoofB = Hex(City.bHome ? TEXT("#96301f") : TEXT("#7a4a2a"));
	FLinearColor Mound = Hex(TEXT("#cdb27a"));
	FLinearColor Dark = Hex(TEXT("#2a1c0c"));
	FLinearColor Gold = Hex(TEXT("#d9a82b"));
	if (Look == 1)
	{
		Stone = Hex(TEXT("#b8322a")); Shade = Hex(TEXT("#5c0f09")); Roof = Hex(TEXT("#7a1a12")); RoofB = Hex(TEXT("#5c0f09")); Mound = Hex(TEXT("#8a5a48")); Gold = Hex(TEXT("#6b4a1f"));
	}
	else if (Look == 2)
	{
		Stone = Hex(TEXT("#e2ddd0")); Shade = Hex(TEXT("#c4beb0")); Roof = Hex(TEXT("#a39d92")); RoofB = Hex(TEXT("#8d877c")); Mound = Hex(TEXT("#c4bca8")); Gold = Hex(TEXT("#8a8474"));
	}

	PortsGeo::FMeshBuilder Mesh;
	const FVector Middle(At.X, At.Y, Floor);
	// The mound the town stands on; it also lifts the town clear of the roads and rivers that pass under it.
	Mesh.AddRound(FVector(At.X, At.Y, Floor - 1.2), R + 2.6, R + 1.6, 1.2, Mound, 28);

	// The wall: straight lengths between corner points, towers on every other corner, battlements along the top.
	const int32 Corners = City.bHome ? 12 : 10;
	const double WallHeight = 2.5 * S, Thick = 0.9 * S;
	const auto Corner = [&](int32 i)
	{
		// The first corner is turned so that one length of wall faces due south, for the gate.
		const double Angle = UE_DOUBLE_HALF_PI + UE_DOUBLE_PI / Corners + 2.0 * UE_DOUBLE_PI * i / Corners;
		return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * R;
	};
	for (int32 i = 0; i < Corners; i++)
	{
		const FVector2D A = Corner(i), B = Corner(i + 1);
		const FVector2D Mid = (A + B) * 0.5;
		const double Length = FVector2D::Distance(A, B);
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(B.Y - A.Y, B.X - A.X));
		// When the plague is in the town its walls are banded dark and light red, like the web map's hatching.
		const FLinearColor Wall = (Look == 1 && (i % 2)) ? Shade : Stone;
		Mesh.AddBox(Middle + FVector(Mid.X, Mid.Y, 0), FVector(Length + 0.2, Thick, WallHeight), Yaw, Wall);
		const int32 Teeth = 4;
		for (int32 t = 0; t < Teeth; t++)
		{
			const FVector2D P = FMath::Lerp(A, B, (t + 0.5) / Teeth);
			Mesh.AddBox(Middle + FVector(P.X, P.Y, WallHeight), FVector(Length / Teeth * 0.5, Thick, 0.55 * S), Yaw, Wall);
		}
		if (i % 2 == 0)
		{
			const FVector Foot = Middle + FVector(A.X, A.Y, 0);
			const double TowerHeight = (3.9 + Random.FRandRange(0.f, 0.6f)) * S;
			Mesh.AddRound(Foot, 1.25 * S, 1.2 * S, TowerHeight, Stone, 10, false);
			Mesh.AddRound(Foot + FVector(0, 0, TowerHeight), 1.55 * S, 1.55 * S, 0.35 * S, Shade, 10);
			Mesh.AddRound(Foot + FVector(0, 0, TowerHeight + 0.35 * S), 1.6 * S, 0, 2.3 * S, Roof, 10);
		}
	}
	// The gatehouse, in the middle of the south wall, with a dark archway.
	{
		const FVector2D Gate = (Corner(Corners - 1) + Corner(0)) * 0.5;
		const FVector Foot = Middle + FVector(Gate.X, Gate.Y + 0.1 * S, 0);
		Mesh.AddBox(Foot, FVector(3.0, 1.7, 3.7) * S, 0, Stone);
		Mesh.AddBox(Foot + FVector(0, 0.75 * S, 0), FVector(1.3 * S, 0.3 * S, 2.1 * S), 0, Dark);
		Mesh.AddHouse(Foot + FVector(0, 0, 3.7 * S), FVector2D(3.3, 2.0) * S, 0.0, 1.3 * S, 0, Stone, Roof);
	}

	// Houses, crowded inside the wall and turned every which way.
	const int32 Houses = City.bHome ? 15 : 11;
	TArray<FVector2D> Placed;
	const double ChurchRoom = 3.4 * S;
	for (int32 Try = 0, Built = 0; Try < 200 && Built < Houses; Try++)
	{
		const double Angle = Random.FRandRange(0.f, 2.f * UE_PI), Reach = FMath::Sqrt(Random.FRand()) * (R - 2.3 * S);
		const FVector2D P(FMath::Cos(Angle) * Reach, FMath::Sin(Angle) * Reach);
		if (P.Size() < ChurchRoom) continue;
		if (Placed.ContainsByPredicate([&](const FVector2D& Q) { return FVector2D::Distance(P, Q) < 2.1 * S; })) continue;
		Placed.Add(P);
		Built++;
		const FVector2D Size = FVector2D(Random.FRandRange(1.9f, 2.7f), Random.FRandRange(1.3f, 1.7f)) * S;
		Mesh.AddHouse(Middle + FVector(P.X, P.Y, 0), Size, Random.FRandRange(1.0f, 1.7f) * S, Random.FRandRange(0.9f, 1.3f) * S, Random.FRandRange(0.f, 180.f),
			Random.FRand() < 0.5 ? Stone : Shade, Random.FRand() < 0.5 ? Roof : RoofB);
	}

	// The church: a nave with a tower and spire. A home city has a cathedral with two towers and a taller nave.
	{
		const double Nave = City.bHome ? 3.2 * S : 2.3 * S;
		Mesh.AddHouse(Middle + FVector(0.6 * S, 0, 0), FVector2D(5.0, 2.5) * S, Nave, 1.7 * S, 0, Stone, Roof);
		const auto Tower = [&](double Y, double Height)
		{
			const FVector Foot = Middle + FVector(-2.4 * S, Y, 0);
			Mesh.AddBox(Foot, FVector(1.6 * S, 1.6 * S, Height), 0, Stone);
			Mesh.AddRound(Foot + FVector(0, 0, Height), 1.25 * S, 0, 2.9 * S, RoofB, 4);
			Mesh.AddRound(Foot + FVector(0, 0, Height + 2.8 * S), 0.22 * S, 0.22 * S, 0.5 * S, Gold, 5);
		};
		if (City.bHome) { Tower(-0.95 * S, 6.2 * S); Tower(0.95 * S, 6.2 * S); }
		else Tower(0, 5.0 * S);
	}
	Mesh.Commit(Towns, Index, SurfaceMaterial);
}

void APortsMapActor::BuildCities()
{
	const FPortsData& Data = FPortsData::Get();
	TownLooks.Init(0, Data.Cities.Num());
	for (int32 i = 0; i < Data.Cities.Num(); i++)
	{
		const FPortsCity& City = Data.Cities[i];
		BuildTown(i, 0);
		const FVector2D At = CityPixel(City);
		const double S = TownScale(City);
		const double Floor = FPortsTerrain::Get().Height(At) + 0.5;

		// Label positions follow LABEL in map.js.
		FPortsCityLabel Label;
		Label.CityId = City.Id;
		Label.Name = FText::FromString(City.Name);
		Label.bHome = City.bHome;
		FVector2D Anchor = At + FVector2D(15, 2);
		Label.Align = 1;
		if (City.MapLabel == TEXT("left")) { Anchor = At + FVector2D(-15, 2); Label.Align = -1; }
		else if (City.MapLabel == TEXT("above")) { Anchor = At + FVector2D(0, -17); Label.Align = 0; }
		// The web map puts these 46 below, clear of the house banners.
		else if (City.MapLabel == TEXT("below")) { Anchor = At + FVector2D(0, 26); Label.Align = 0; }
		Label.Anchor = ToWorld(Anchor, GroundAt(Anchor, 0));
		Label.PipAnchor = ToWorld(At + FVector2D(0, -4), Floor + 10 * S);
		Label.TokenAnchor = ToWorld(At + FVector2D(0, 13 * S), GroundAt(At + FVector2D(0, 13 * S), 0));
		CityLabels.Add(Label);
	}
}

// The rings round the cities (.city .ring in game.css): amber dashes for a Safe city next to the plague,
// red for Stricken, grey for Aftermath, and bright gold for a city that can be chosen.
void APortsMapActor::BuildRings(const TArray<FRing>& Rings)
{
	PortsGeo::FMeshBuilder Mesh;
	for (const FRing& Ring : Rings)
	{
		const FPortsCity& City = FPortsData::Get().Cities[Ring.City];
		const FVector2D At = CityPixel(City);
		const double Radius = TownRadius * TownScale(City) + 4.4;
		const auto Over = [this](const FVector2D& P) { return GroundAt(P, 0.5); };
		const int32 Steps = 48;
		const int32 Pieces = Ring.bDashed ? 12 : 1;
		for (int32 Piece = 0; Piece < Pieces; Piece++)
		{
			TArray<FVector2D> Arc;
			const int32 From = Piece * Steps / Pieces;
			const int32 To = Ring.bDashed ? From + Steps / Pieces * 3 / 5 + 1 : Steps;
			for (int32 i = From; i <= To; i++)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * i / Steps;
				Arc.Add(At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
			}
			Mesh.AddRibbonOn(Arc, Ring.Width, Over, Ring.Color);
		}
	}
	if (Mesh.Vertices.Num()) Mesh.Commit(Surface, ESection::Rings, SurfaceMaterial);
	else Surface->ClearMeshSection(ESection::Rings);
}

// ---------- Showing the game ----------

// The outline of a house's crest (crestAt in art.js), as points round (0, 0) with a radius of 1.
static TArray<FVector2D> CrestShape(const FString& Crest)
{
	TArray<FVector2D> P;
	if (Crest == TEXT("square")) P = { { -0.82, -0.82 }, { 0.82, -0.82 }, { 0.82, 0.82 }, { -0.82, 0.82 } };
	else if (Crest == TEXT("triangle")) P = { { 0, -1 }, { 0.95, 0.75 }, { -0.95, 0.75 } };
	else if (Crest == TEXT("diamond")) P = { { 0, -1.05 }, { 0.8, 0 }, { 0, 1.05 }, { -0.8, 0 } };
	else if (Crest == TEXT("hexagon")) P = { { -0.5, -0.87 }, { 0.5, -0.87 }, { 1, 0 }, { 0.5, 0.87 }, { -0.5, 0.87 }, { -1, 0 } };
	else if (Crest == TEXT("star"))
	{
		for (int32 i = 0; i < 10; i++)
		{
			const double R = (i % 2) ? 0.45 : 1.0, Angle = i * UE_DOUBLE_PI / 5 - UE_DOUBLE_HALF_PI;
			P.Add(FVector2D(R * FMath::Cos(Angle), R * FMath::Sin(Angle)));
		}
	}
	else for (int32 i = 0; i < 16; i++) P.Add(FVector2D(FMath::Cos(i * UE_DOUBLE_PI / 8), FMath::Sin(i * UE_DOUBLE_PI / 8)) * 0.95);
	return P;
}

// Trading posts: a flag on a pole for each house, planted in a row south of the city and leaning back a
// little like a pin in a map, so its cloth faces the table's edge. The cloth is the web map's swallow-tailed
// banner in the house's colour with its crest (banner in art.js), and it waves. A house that has closed the
// city's gates hangs a portcullis in its colour across the gateway.
void APortsMapActor::BuildFlags()
{
	const FPortsData& Data = FPortsData::Get();
	PortsGeo::FMeshBuilder Poles, Cloth;
	const FLinearColor Gold = Hex(TEXT("#f3d27a")), Ink = Hex(TEXT("#1a1208")), Pale = Hex(TEXT("#fff8e8"));
	// The flag leans back by this much: Up runs along the pole, Front is the way the cloth faces.
	const double Lean = FMath::DegreesToRadians(14.0);
	const FVector Up(0, -FMath::Sin(Lean), FMath::Cos(Lean)), Front(0, FMath::Cos(Lean), FMath::Sin(Lean)), Right(1, 0, 0);
	const double K = 0.52;	// map pixels for one unit of the web banner's drawing
	for (int32 c = 0; c < Data.Cities.Num(); c++)
	{
		FPortsCityView* View = CityViews.Find(Data.Cities[c].Id);
		if (!View) continue;
		const FPortsCity& City = Data.Cities[c];
		const FVector2D At = CityPixel(City);
		const double R = TownRadius * TownScale(City);
		for (int32 i = 0; i < View->Tokens.Num(); i++)
		{
			FPortsCityToken& T = View->Tokens[i];
			const FVector2D Spot = At + FVector2D((i - (View->Tokens.Num() - 1) * 0.5) * 9.5 - 3.5, R + 9.5);
			const FVector Foot(Spot.X, Spot.Y, GroundAt(Spot, 0));
			T.Foot = ToWorld(Spot, Foot.Z);

			// The pole (26 long in the drawing), its gold knob, and a small stone foot.
			const double Length = 26.0 * K;
			const int32 First = Poles.Vertices.Num();
			Poles.AddRound(FVector::ZeroVector, 0.34, 0.26, Length, Gold, 6, false);
			Poles.AddRound(FVector(0, 0, Length), 0.62, 0.0, 1.0, Gold, 6);
			Poles.AddRound(FVector(0, 0, Length - 0.5), 0.62, 0.62, 0.5, Gold, 6);
			// Built upright at the origin, then leaned and moved to its place.
			for (int32 v = First; v < Poles.Vertices.Num(); v++)
			{
				const FVector Local = ToPixel3(Poles.Vertices[v]);
				Poles.Vertices[v] = ToWorld3(Foot + Right * Local.X + Front * Local.Y + Up * Local.Z);
			}
			Poles.AddRound(Foot - FVector(0, 0, 0.3), 1.0, 0.7, 0.9, Hex(TEXT("#8a6a36")), 8);

			// The cloth: a point on it is (along, down) from the top of the pole, in the drawing's units.
			const auto OnCloth = [&](double Along, double Down, double Out) { return Foot + Up * ((24.0 - Down) * K) + Right * (Along * K) + Front * Out; };
			const auto Shape = [&](const TArray<FVector2D>& Outline, const FLinearColor& Color, double Out)
			{
				// A fan from the outline's middle; each point carries how far it is from the pole, which is how much it waves.
				FVector2D Mid(0, 0);
				for (const FVector2D& P : Outline) Mid += P / Outline.Num();
				const int32 Centre = Cloth.AddPoint(OnCloth(Mid.X, Mid.Y, Out), Front, Color);
				Cloth.UVs.Add(FVector2D(Mid.X / 14.6, 0));
				for (const FVector2D& P : Outline)
				{
					Cloth.AddPoint(OnCloth(P.X, P.Y, Out), Front, Color);
					Cloth.UVs.Add(FVector2D(P.X / 14.6, 0));
				}
				for (int32 k = 0; k < Outline.Num(); k++) Cloth.AddTriangle(Centre, Centre + 1 + k, Centre + 1 + (k + 1) % Outline.Num(), PortsGeo::ToWorldDirection(Front));
			};
			// M0.6 -23 h14 l-3.2 5 l3.2 5 h-14z, edged in ink.
			Shape({ { 0.0, 0.3 }, { 15.6, 0.3 }, { 12.2, 6.0 }, { 15.6, 11.7 }, { 0.0, 11.7 } }, Ink, 0.0);
			Shape({ { 0.6, 1.0 }, { 14.2, 1.0 }, { 11.2, 6.0 }, { 14.2, 11.0 }, { 0.6, 11.0 } }, T.Color, 0.14);
			TArray<FVector2D> Crest = CrestShape(T.Crest);
			TArray<FVector2D> Edge = Crest;
			for (FVector2D& P : Edge) P = FVector2D(6.2, 6.0) + P * 3.3;
			for (FVector2D& P : Crest) P = FVector2D(6.2, 6.0) + P * 2.7;
			Shape(Edge, Ink, 0.28);
			Shape(Crest, Pale, 0.42);

			if (T.bGatesClosed)
			{
				// The portcullis: upright bars and two cross-bars across the gateway in the south wall.
				const FVector Gate(At.X, At.Y + R * FMath::Cos(UE_DOUBLE_PI / (City.bHome ? 12 : 10)) + 1.25 * TownScale(City), FPortsTerrain::Get().Height(At) + 0.5);
				const double W = 3.6 * TownScale(City), H = 3.3 * TownScale(City);
				for (int32 b = 0; b < 5; b++) Poles.AddBox(Gate + FVector((b - 2) * W / 4.6, 0, 0), FVector(0.34, 0.34, H), 0, T.Color);
				for (int32 b = 0; b < 2; b++) Poles.AddBox(Gate + FVector(0, 0, H * (0.35 + 0.42 * b)), FVector(W, 0.38, 0.34), 0, T.Color);
				Poles.AddBox(Gate + FVector(0, 0, H), FVector(W + 0.5, 0.5, 0.45), 0, Ink);
			}
		}
	}
	if (Poles.Vertices.Num()) Poles.Commit(Flags, 0, SurfaceMaterial); else Flags->ClearMeshSection(0);
	if (Cloth.Vertices.Num()) Cloth.Commit(Flags, 1, FlagMaterial ? FlagMaterial : SurfaceMaterial); else Flags->ClearMeshSection(1);
}

void APortsMapActor::ClearState()
{
	CityViews.Reset();
	for (int32 i = 0; i < TownLooks.Num(); i++) if (TownLooks[i] != 0) { TownLooks[i] = 0; BuildTown(i, 0); }
	for (int32 i = 0; i < StainOn.Num(); i++) StainOn[i] = false;
	BuildRings(TArray<FRing>());
	BuildFlags();
	BuildHighlights(TArray<FString>());
}

// The cities follow updateMap in map.js: dark red and a red ring for Stricken, grey for Aftermath,
// an amber ring for a Safe city next to the plague, gold for a city that can be chosen.
void APortsMapActor::ApplyState(const FPortsState& State, const TArray<FString>& Selectable, const TArray<FString>& HighlightRoutes)
{
	const FPortsData& Data = FPortsData::Get();
	CityViews.Reset();
	TArray<FRing> Rings;
	for (int32 i = 0; i < Data.Cities.Num() && i < State.cities.Num(); i++)
	{
		const FPortsCity& City = Data.Cities[i];
		const FPortsCityState& C = State.cities[i];
		const bool bStricken = C.state == TEXT("stricken"), bAftermath = C.state == TEXT("aftermath");
		const int32 Look = bStricken ? 1 : bAftermath ? 2 : 0;
		if (StainOn.IsValidIndex(i)) StainOn[i] = bStricken;
		if (TownLooks.IsValidIndex(i) && TownLooks[i] != Look) { TownLooks[i] = Look; BuildTown(i, Look); }
		if (Selectable.Contains(City.Id)) Rings.Add({ i, Hex(TEXT("#f3d27a")) * 1.7f, 2.6, false });
		else if (bStricken) Rings.Add({ i, Hex(TEXT("#c0281c")), 1.9, false });
		else if (bAftermath) Rings.Add({ i, Hex(TEXT("#6f6a5f")), 1.9, false });
		else if (Ports::IsThreatened(State, City.Id)) Rings.Add({ i, Hex(TEXT("#e7a02b")), 1.9, true });
		FPortsCityView View;
		View.Severity = bStricken ? C.severity : 0;
		for (const FPortsPlayer& P : State.players)
		{
			if (!P.posts.Contains(City.Id)) continue;
			FPortsCityToken Token;
			Token.Color = Hex(*P.color);
			Token.Glyph = PortsUi::CrestGlyph(P.crest);
			Token.Crest = P.crest;
			Token.Family = Ports::FamilyAt(P, City.Id);
			Token.bGatesClosed = P.gates.IsSet() && P.gates->city == City.Id;
			View.Tokens.Add(Token);
		}
		if (View.Severity > 0 || View.Tokens.Num()) CityViews.Add(City.Id, View);
	}
	BuildRings(Rings);
	BuildFlags();
	BuildHighlights(HighlightRoutes);
}

void APortsMapActor::BuildHighlights(const TArray<FString>& RouteIds)
{
	if (RouteIds == ShownHighlights) return;
	ShownHighlights = RouteIds;
	PortsGeo::FMeshBuilder Mesh;
	for (const FString& Id : RouteIds)
	{
		const auto Over = [this](const FVector2D& P) { return GroundAt(P, RouteLift + 0.25); };
		const TArray<FVector2D>* Curve = RouteCurves.Find(Id);
			if (!Curve) continue;
			// The gold line runs from wall to wall, not over the towns at its ends.
			const double Total = PortsGeo::Length(*Curve);
			double From = 0, To = Total;
			while (From < Total * 0.4 && InTown(PortsGeo::PointAtLength(*Curve, From))) From += 1.0;
			while (To > Total * 0.6 && InTown(PortsGeo::PointAtLength(*Curve, To))) To -= 1.0;
			Mesh.AddRibbonOn(PortsGeo::Slice(*Curve, From, To), 5.0, Over, Hex(TEXT("#f3d27a")) * 1.5f);
	}
	if (Mesh.Vertices.Num()) Mesh.Commit(Surface, ESection::Highlight, SurfaceMaterial);
	else Surface->ClearMeshSection(ESection::Highlight);
}

void APortsMapActor::FloatText(const FString& CityId, const FString& Text, bool bGain)
{
	const double Now = GetWorld()->GetTimeSeconds();
	Floaters.RemoveAll([Now](const FPortsFloater& F) { return Now - F.Start > 3.0; });
	FPortsFloater F;
	F.Position = CityLocation(CityId) + FVector(0, 0, 12 * UnitsPerPixel);
	F.Text = Text;
	F.bGain = bGain;
	F.Start = Now;
	Floaters.Add(F);
}

FString APortsMapActor::CityAt(const FVector2D& Pixel, double Reach) const
{
	FString Best;
	double BestDistance = Reach;
	for (const FPortsCity& City : FPortsData::Get().Cities)
	{
		const double Distance = FVector2D::Distance(CityPixel(City), Pixel);
		if (Distance < BestDistance) { BestDistance = Distance; Best = City.Id; }
	}
	return Best;
}

// ---------- Ships and carts ----------

namespace
{
	// Models are built round the middle of the map, which is the origin of the world, so their points come out
	// as plain offsets: x to the right, y towards the stern (the bow points along -y), z up, in map pixels.
	FVector Local(double X, double Y, double Z) { return FVector(PortsProjection::MapWidth() * 0.5 + X, PortsProjection::MapHeight() * 0.5 + Y, Z); }

	void TwoSided(PortsGeo::FMeshBuilder& Mesh, const TArray<FVector>& Corners, const FLinearColor& Color)
	{
		const FVector Normal = FVector::CrossProduct(Corners[1] - Corners[0], Corners[2] - Corners[0]).GetSafeNormal();
		Mesh.AddFace(Corners, Corners[0] - Normal, Color);
		Mesh.AddFace(Corners, Corners[0] + Normal, Color);
	}

	// A hull: the same shape for both ships, fuller and higher for the northern cog, long and low for the galley.
	void AddHull(PortsGeo::FMeshBuilder& Mesh, double Length, double Beam, double Height, double Fullness, const FLinearColor& Side, const FLinearColor& Deck, const FLinearColor& Rail)
	{
		const int32 Stations = 10;
		const FVector Inside = Local(0, 0, Height * 0.5);
		const auto Half = [&](double T) { return Beam * FMath::Pow(FMath::Max(0.0, 1.0 - FMath::Pow(FMath::Abs(T), Fullness)), 0.6); };
		const auto Top = [&](double T) { return Height * (1.0 + 0.38 * T * T + (T < 0 ? 0.12 * T * T : 0)); };
		const auto Keel = [&](double T) { return Height * 0.75 * FMath::Pow(FMath::Abs(T), 3.0); };
		for (int32 i = 0; i < Stations; i++)
		{
			// T runs from the bow (-1) to the stern (1).
			const double A = -1.0 + 2.0 * i / Stations, B = -1.0 + 2.0 * (i + 1) / Stations;
			const double Ya = A * Length * 0.5, Yb = B * Length * 0.5;
			for (const double Hand : { -1.0, 1.0 })
			{
				Mesh.AddFace({ Local(Hand * Half(A), Ya, Top(A)), Local(Hand * Half(B), Yb, Top(B)), Local(0, Yb, Keel(B)), Local(0, Ya, Keel(A)) }, Inside, Side);
				// The rail: a pale strake along the top of the side.
				Mesh.AddFace({ Local(Hand * Half(A) * 1.04, Ya, Top(A) + 0.02), Local(Hand * Half(B) * 1.04, Yb, Top(B) + 0.02), Local(Hand * Half(B) * 1.04, Yb, Top(B) - 0.3), Local(Hand * Half(A) * 1.04, Ya, Top(A) - 0.3) }, Inside, Rail);
			}
			const double Da = Top(A) - 0.35, Db = Top(B) - 0.35;
			Mesh.AddFace({ Local(-Half(A), Ya, Da), Local(Half(A), Ya, Da), Local(Half(B), Yb, Db), Local(-Half(B), Yb, Db) }, Local(0, 0, -5), Deck);
		}
	}

	// A wheel on its edge at the side of a cart: its axle runs across (along x).
	void AddWheel(PortsGeo::FMeshBuilder& Mesh, const FVector& Hub, double Radius, const FLinearColor& Color)
	{
		const int32 Sides = 10;
		TArray<FVector> Inner, Outer;
		for (int32 i = 0; i < Sides; i++)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * i / Sides;
			const FVector Rim(0, FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
			Inner.Add(Hub + Rim - FVector(0.22, 0, 0));
			Outer.Add(Hub + Rim + FVector(0.22, 0, 0));
		}
		Mesh.AddFace(Inner, Hub, Color);
		Mesh.AddFace(Outer, Hub, Color);
		for (int32 i = 0; i < Sides; i++) Mesh.AddFace({ Inner[i], Inner[(i + 1) % Sides], Outer[(i + 1) % Sides], Outer[i] }, Hub, Color * 0.7f);
	}
}

// Kind 0 is a cog (the round ship of the northern seas), 1 a galley (the long oared ship of the
// Mediterranean), 2 a covered cart drawn by a horse. Main is the sail or the cart's cover, and
// Flag the pennant at the masthead: black when the cargo is infected, as on the web map.
void APortsMapActor::BuildVessel(UProceduralMeshComponent* Model, int32 Kind, const FLinearColor& Main, const FLinearColor& Hull, const FLinearColor& Flag)
{
	PortsGeo::FMeshBuilder Mesh;
	const FLinearColor Wood = Hex(TEXT("#8a6a36")), Rail = Hex(TEXT("#c9a96a")), Rope = Hex(TEXT("#3b2413")), Foam(0.80f, 0.90f, 0.88f);
	if (Kind == 2)
	{
		// The cart: a box on four wheels under a hooped cover, and a horse between the shafts.
		Mesh.AddBox(Local(0, 1.2, 1.3), FVector(3.3, 5.6, 1.3), 0, Wood);
		Mesh.AddHouse(Local(0, 1.2, 2.6), FVector2D(5.4, 3.2), 0.5, 1.9, 90, Main, Main * 0.82f);
		for (const double X : { -1.9, 1.9 }) for (const double Y : { -0.6, 3.0 }) AddWheel(Mesh, Local(X, Y, 1.25), 1.25, Rope);
		for (const double X : { -0.9, 0.9 }) Mesh.AddBox(Local(X, -3.2, 1.7), FVector(0.2, 3.6, 0.2), 0, Rope);
		const FLinearColor Horse = Hex(TEXT("#6b4423"));
		Mesh.AddBox(Local(0, -4.6, 1.5), FVector(1.25, 3.2, 1.5), 0, Horse);
		Mesh.AddBox(Local(0, -6.4, 2.6), FVector(0.8, 1.0, 1.5), 0, Horse);
		Mesh.AddBox(Local(0, -7.1, 3.6), FVector(0.7, 1.5, 0.8), 0, Horse);
		for (const double X : { -0.42, 0.42 }) for (const double Y : { -5.8, -3.5 }) Mesh.AddBox(Local(X, Y, 0), FVector(0.34, 0.34, 1.6), 0, Horse * 0.8f);
		Mesh.AddBox(Local(0, -2.9, 2.6), FVector(0.25, 0.25, 1.3), 0, Rope);
	}
	else
	{
		const bool bCog = Kind == 0;
		const double Length = bCog ? 12.0 : 15.5, Beam = bCog ? 2.3 : 1.55, Height = bCog ? 2.2 : 1.5;
		AddHull(Mesh, Length, Beam, Height, bCog ? 2.6 : 2.0, Hull, Wood, Rail);
		const double Deck = Height - 0.35;
		const double MastY = bCog ? -0.4 : -1.6, MastTop = bCog ? 11.0 : 9.5;
		Mesh.AddRound(Local(0, MastY, Deck), 0.3, 0.22, MastTop - Deck, Rope, 6);
		if (bCog)
		{
			// Castles fore and aft, and one square sail on a yard.
			Mesh.AddBox(Local(0, 4.3, Deck + 0.5), FVector(3.3, 2.8, 1.5), 0, Hull * 1.25f);
			Mesh.AddBox(Local(0, 4.3, Deck + 2.0), FVector(3.6, 3.0, 0.3), 0, Rail);
			Mesh.AddBox(Local(0, -4.9, Deck + 0.9), FVector(2.2, 1.8, 1.1), 0, Hull * 1.25f);
			Mesh.AddBox(Local(0, MastY, 9.4), FVector(8.2, 0.3, 0.3), 0, Rope);
			// The sail bellies forward between the yard and its foot.
			const double Columns = 4;
			for (int32 c = 0; c < Columns; c++) for (int32 r = 0; r < 3; r++)
			{
				const auto P = [&](int32 Cc, int32 Rr)
				{
					const double U = Cc / Columns, V = Rr / 3.0;
					const double Belly = 1.5 * FMath::Sin(V * UE_DOUBLE_PI) * (0.5 + 0.5 * FMath::Sin(U * UE_DOUBLE_PI));
					return Local((U - 0.5) * 7.6 * (1.0 - 0.08 * V), MastY - 0.25 - Belly, 9.3 - V * 5.6);
				};
				TwoSided(Mesh, { P(c, r), P(c + 1, r), P(c + 1, r + 1), P(c, r + 1) }, (c % 2) ? Main : Main * 0.88f);
			}
		}
		else
		{
			// One long slanting yard with a three-cornered sail, and a bank of oars each side.
			const FVector Peak = Local(0, MastY + 5.2, 11.6), Tack = Local(0, MastY - 6.2, 3.6), Clew = Local(1.6, MastY + 4.0, Deck + 1.0);
			Mesh.AddBox((Peak + Tack) * 0.5, FVector(0.3, 0.3, 0.3), 0, Rope);
			const int32 Steps = 6;
			for (int32 k = 0; k < Steps; k++)
			{
				const FVector A = FMath::Lerp(Tack, Peak, static_cast<double>(k) / Steps), B = FMath::Lerp(Tack, Peak, static_cast<double>(k + 1) / Steps);
				// The yard itself, in short lengths, and the cloth below it.
				TwoSided(Mesh, { A + FVector(0.18, 0, 0.18), B + FVector(0.18, 0, 0.18), B - FVector(0.18, 0, 0.18), A - FVector(0.18, 0, 0.18) }, Rope);
				const FVector Ca = FMath::Lerp(Tack, Clew, static_cast<double>(k) / Steps), Cb = FMath::Lerp(Tack, Clew, static_cast<double>(k + 1) / Steps);
				const FVector Belly(0.9, 0, 0);
				TwoSided(Mesh, { A, B, Cb + Belly * FMath::Sin((k + 1.0) / Steps * UE_DOUBLE_PI), Ca + Belly * FMath::Sin(static_cast<double>(k) / Steps * UE_DOUBLE_PI) }, (k % 2) ? Main : Main * 0.88f);
			}
			for (int32 k = 0; k < 7; k++) for (const double Hand : { -1.0, 1.0 })
			{
				const double Y = -3.6 + k * 1.25;
				TwoSided(Mesh, { Local(Hand * 1.2, Y - 0.1, Deck + 0.3), Local(Hand * 1.2, Y + 0.1, Deck + 0.3), Local(Hand * 4.6, Y + 0.75, 0.15), Local(Hand * 4.6, Y + 0.45, 0.15) }, Rail);
			}
			Mesh.AddHouse(Local(0, 5.6, Deck), FVector2D(2.6, 2.4), 0.9, 0.9, 90, Main * 0.85f, Main * 0.7f);
		}
		// The pennant at the masthead.
		TwoSided(Mesh, { Local(0, MastY, MastTop + 1.3), Local(0, MastY + 3.0, MastTop + 0.7), Local(0, MastY, MastTop + 0.1) }, Flag);
		Mesh.AddRound(Local(0, MastY, MastTop), 0.2, 0.2, 1.4, Rope, 5);
		// The water the ship pushes aside: two pale streaks spreading astern.
		for (const double Hand : { -1.0, 1.0 })
		{
			Mesh.AddFace({ Local(Hand * Beam * 0.7, -Length * 0.3, 0.12), Local(Hand * (Beam + 2.4), Length * 0.75, 0.12), Local(Hand * (Beam + 0.9), Length * 0.8, 0.12) }, Local(0, 0, -5), Foam);
		}
		Mesh.AddFace({ Local(-Beam * 0.5, Length * 0.46, 0.12), Local(Beam * 0.5, Length * 0.46, 0.12), Local(0, Length * 0.95, 0.12) }, Local(0, 0, -5), Foam);
	}
	Mesh.Commit(Model, 0, SurfaceMaterial);
}

UProceduralMeshComponent* APortsMapActor::NewVesselModel()
{
	UProceduralMeshComponent* Model = NewObject<UProceduralMeshComponent>(this);
	Model->SetMobility(EComponentMobility::Movable);
	Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Model->SetCastShadow(true);
	Model->SetupAttachment(GetRootComponent());
	Model->RegisterComponent();
	VesselModels.Add(Model);
	return Model;
}

// Which model sails a route: carts on land, cogs where either end is north of 47 degrees, galleys elsewhere (map.js).
static int32 VesselKind(const FPortsRoute& Route)
{
	if (!Route.bSea) return 2;
	const FPortsData& Data = FPortsData::Get();
	return FMath::Max(Data.FindCity(Route.A)->Lat, Data.FindCity(Route.B)->Lat) > 47.0 ? 0 : 1;
}

// Sends one of the everyday ships or carts off along a route chosen at random (startAmbient in map.js).
void APortsMapActor::SendTraffic(FVessel& V)
{
	const FPortsData& Data = FPortsData::Get();
	TArray<const FPortsRoute*> Pool;
	for (const FPortsRoute& Route : Data.Routes) if (Route.bSea == V.bSea) Pool.Add(&Route);
	if (Pool.Num() == 0) return;
	const FPortsRoute& Route = *Pool[FMath::RandRange(0, Pool.Num() - 1)];
	V.Path = RouteCurves[Route.Id];
	const bool bReverse = FMath::RandBool();
	if (bReverse) Algo::Reverse(V.Path);
	V.Length = PortsGeo::Length(V.Path);
	V.Duration = V.bSea ? 12.0 + V.Length * 0.028 : 22.0 + V.Length * 0.060;
	V.T = 0;
	// A ship leaving a Stricken port flies a black pennant.
	const FString& From = bReverse ? Route.B : Route.A;
	const bool bPlague = StainOn.IsValidIndex(Data.CityIndexOf(From)) && StainOn[Data.CityIndexOf(From)];
	const int32 Kind = VesselKind(Route);
	if (Kind == 0) BuildVessel(V.Model, 0, Hex(TEXT("#f4e8c8")), Hex(TEXT("#6b4423")), bPlague ? Hex(TEXT("#1a1208")) : Hex(TEXT("#c9971f")));
	else if (Kind == 1) BuildVessel(V.Model, 1, Hex(TEXT("#fbf5e4")), Hex(TEXT("#5a3a1a")), bPlague ? Hex(TEXT("#1a1208")) : Hex(TEXT("#a82318")));
	else BuildVessel(V.Model, 2, Hex(TEXT("#e6d6ae")), Hex(TEXT("#6b4423")), FLinearColor::White);
}

void APortsMapActor::StartTraffic()
{
	// Seven ships and three carts, smaller than a house's own (0.72 against 1.25 on the web map).
	for (int32 i = 0; i < 10; i++)
	{
		FVessel V;
		V.Model = NewVesselModel();
		V.bSea = i < 7;
		V.Size = 0.72 / 1.25;
		SendTraffic(V);
		V.T = FMath::FRandRange(0.f, 0.9f);
		Traffic.Add(V);
	}
}

// Puts a ship or cart where it has got to along its path: on the water or the road, heading the way it goes.
void APortsMapActor::PlaceVessel(const FVessel& V, double Along, double Grow)
{
	const FVector2D At = PortsGeo::PointAtLength(V.Path, Along);
	const FVector2D Ahead = PortsGeo::PointAtLength(V.Path, FMath::Min(V.Length, Along + 2.0));
	const FVector2D Behind = PortsGeo::PointAtLength(V.Path, FMath::Max(0.0, Along - 2.0));
	const double Time = GetWorld()->GetTimeSeconds() + V.Duration * 7.0;
	const bool bAfloat = V.bSea && FPortsTerrain::Get().Coast(At) < 1.0;
	FVector Where = ToWorld(At, GroundAt(At, V.bSea ? 0.0 : RouteLift));
	FRotator Facing = (ToWorld(Ahead) - ToWorld(Behind)).Rotation();
	if (bAfloat)
	{
		// Riding the swell.
		Where.Z += FMath::Sin(Time * 1.9) * 2.0;
		Facing.Roll = FMath::Sin(Time * 1.6) * 5.0;
		Facing.Pitch = FMath::Sin(Time * 1.25 + 1.0) * 2.5;
	}
	// The model's bow points north as built.
	V.Model->SetWorldLocationAndRotation(Where, Facing);
	V.Model->SetWorldScale3D(FVector(V.Size * Grow));
}

// A house's ship (or cart) makes its journey with the same easing and timing as animateShipment in map.js.
void APortsMapActor::AnimateShipment(const FString& RouteId, const FString& From, const FLinearColor& Color, bool bInfected, TFunction<void()> OnDone)
{
	const TArray<FVector2D>* Curve = RouteCurves.Find(RouteId);
	const FPortsRoute* Route = FPortsData::Get().FindRoute(RouteId);
	if (!Curve || !Route || bShipping)
	{
		if (OnDone) OnDone();
		return;
	}
	if (!Shipment.Model) Shipment.Model = NewVesselModel();
	Shipment.Path = *Curve;
	if (Route->A != From) Algo::Reverse(Shipment.Path);
	Shipment.Length = PortsGeo::Length(Shipment.Path);
	Shipment.Duration = FMath::Min(2.2, 0.7 + Shipment.Length * 0.0022);
	Shipment.bSea = Route->bSea;
	Shipment.Size = 1.0;
	ShipmentStart = GetWorld()->GetTimeSeconds();
	ShipmentDone = MoveTemp(OnDone);
	BuildVessel(Shipment.Model, VesselKind(*Route), Color, Hex(TEXT("#4a2f14")), bInfected ? Hex(TEXT("#1a1208")) : Hex(TEXT("#fff8e8")));
	Shipment.Model->SetVisibility(true);
	PlaceVessel(Shipment, 0, 1.0);
	ShipmentAt = Shipment.Path[0];
	bShipping = true;
	BuildHighlights({ RouteId });
}

// The plague on the land: a dark red stain spreads round a Stricken city (animateStrike in map.js) and
// fades when the plague has passed, and three puffs of foul air rise from it, one after another.
void APortsMapActor::DrawPlague(float DeltaSeconds)
{
	const FPortsData& Data = FPortsData::Get();
	bool bAny = false, bMoving = false;
	for (int32 i = 0; i < StainAmount.Num(); i++)
	{
		const double Target = StainOn[i] ? 1.0 : 0.0;
		if (StainAmount[i] != Target)
		{
			// 1.8 seconds to spread, 1.5 to fade.
			StainAmount[i] = Target > StainAmount[i] ? FMath::Min(1.0, StainAmount[i] + DeltaSeconds / 1.8) : FMath::Max(0.0, StainAmount[i] - DeltaSeconds / 1.5);
			bMoving = true;
		}
		bAny |= StainAmount[i] > 0;
	}
	if (bMoving || bStainsDrawn != bAny)
	{
		bStainsDrawn = bAny;
		PortsGeo::FMeshBuilder Mesh;
		const FLinearColor Core = Hex(TEXT("#5a0d07")), Edge = Hex(TEXT("#8f1d14"));
		for (int32 i = 0; i < StainAmount.Num(); i++)
		{
			if (StainAmount[i] <= 0) continue;
			const FVector2D At = CityPixel(Data.Cities[i]);
			// It grows as it appears (ease-out) and simply pales as it goes.
			const double Shown = StainAmount[i];
			const double Radius = 42.0 * (StainOn[i] ? 0.1 + 0.9 * (1.0 - FMath::Square(1.0 - Shown)) : 1.0);
			const int32 Sides = 28;
			const double Rings[4] = { 0.0, 0.3, 0.6, 1.0 };
			const float Alpha[4] = { 0.92f, 0.78f, 0.5f, 0.0f };
			const int32 First = Mesh.Vertices.Num();
			for (int32 r = 0; r < 4; r++) for (int32 k = 0; k < Sides; k++)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * k / Sides;
				const FVector2D P = At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius * Rings[r];
				FLinearColor C = FMath::Lerp(Core, Edge, static_cast<float>(FMath::Min(1.0, Rings[r] / 0.6)));
				C.A = Alpha[r] * static_cast<float>(Shown);
				Mesh.AddVertex(P, GroundAt(P, 0.42), FVector::UpVector, C);
			}
			for (int32 r = 0; r < 3; r++) for (int32 k = 0; k < Sides; k++)
			{
				const int32 A = First + r * Sides + k, B = First + r * Sides + (k + 1) % Sides;
				Mesh.AddTriangle(A, B, A + Sides, FVector::UpVector);
				Mesh.AddTriangle(B, B + Sides, A + Sides, FVector::UpVector);
			}
		}
		if (Mesh.Vertices.Num()) Mesh.Commit(Effects, 0, FParse::Param(FCommandLine::Get(), TEXT("PortsNoGlaze")) || !GlazeMaterial ? SurfaceMaterial : GlazeMaterial); else Effects->ClearMeshSection(0);
	}

	// Foul air: each puff rises, swells and thins over four and a half seconds (.puff in game.css).
	bool bAir = false;
	for (int32 i = 0; i < StainOn.Num(); i++) bAir |= StainOn[i];
	if (!bAir)
	{
		if (bAirDrawn) { Effects->ClearMeshSection(1); bAirDrawn = false; }
		return;
	}
	bAirDrawn = true;
	PortsGeo::FMeshBuilder Air;
	const double Now = GetWorld()->GetTimeSeconds();
	const FLinearColor Smoke = Hex(TEXT("#3b4a32"));
	for (int32 i = 0; i < StainOn.Num(); i++)
	{
		if (!StainOn[i]) continue;
		const FVector2D At = CityPixel(Data.Cities[i]);
		const double Floor = FPortsTerrain::Get().Height(At);
		for (int32 Puff = 0; Puff < 3; Puff++)
		{
			const double T = FMath::Frac((Now + i * 0.37) / 4.5 - Puff / 3.0);
			const double Eased = 1.0 - FMath::Square(1.0 - T);
			const double Size = FMath::Lerp(0.5, 1.6, Eased);
			const float Opacity = static_cast<float>(T < 0.25 ? T / 0.25 * 0.9 : 0.9 * (1.0 - (T - 0.25) / 0.75)) * 1.05f;
			const FVector Centre(At.X + (Puff - 1) * 7.0, At.Y - 2.0, Floor + 5.0 + Eased * 15.0);
			const double Rx = (10.0 + Puff * 2.0) * Size, Ry = (7.0 + Puff) * Size;
			// A soft round cloud, tipped up to face the table's edge.
			const int32 Sides = 18;
			const int32 Middle = Air.AddVertex(FVector2D(Centre.X, Centre.Y), Centre.Z, FVector::UpVector, FLinearColor(Smoke.R, Smoke.G, Smoke.B, Opacity));
			for (int32 k = 0; k < Sides; k++)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * k / Sides;
				const double Up = -FMath::Sin(Angle) * Ry;
				Air.AddVertex(FVector2D(Centre.X + FMath::Cos(Angle) * Rx, Centre.Y - Up * 0.45), Centre.Z + Up * 0.8, FVector::UpVector, FLinearColor(Smoke.R, Smoke.G, Smoke.B, 0.f));
			}
			for (int32 k = 0; k < Sides; k++) Air.AddTriangle(Middle, Middle + 1 + k, Middle + 1 + (k + 1) % Sides, FVector::UpVector);
		}
	}
	Air.Commit(Effects, 1, FParse::Param(FCommandLine::Get(), TEXT("PortsNoGlaze")) || !GlazeMaterial ? SurfaceMaterial : GlazeMaterial);
}

// A sea serpent or a whale surfaces somewhere in open water, swims a little way, and goes under again.
void APortsMapActor::DrawCreature()
{
	const double Now = GetWorld()->GetTimeSeconds();
	// For checking: -PortsCreature=x,y,kind brings one up at a chosen place four seconds in (kind 0 the serpent, 1 the whale).
	static bool bAsked = false;
	FString Asked;
	if (!bAsked && Now > 4.0 && FParse::Value(FCommandLine::Get(), TEXT("PortsCreature="), Asked, false))
	{
		bAsked = true;
		TArray<FString> Parts;
		Asked.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3) { CreatureAt = FVector2D(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1])); CreatureKind = FCString::Atoi(*Parts[2]); CreatureWay = FVector2D(0.9, 0.44); CreatureStart = Now; }
	}
	const double Life = CreatureKind == 0 ? 13.0 : 11.0;
	if (CreatureKind >= 0 && (Now - CreatureStart > Life || Quiet > 0))
	{
		CreatureKind = -1;
		Creature->ClearMeshSection(0);
		CreatureNext = Now + FMath::FRandRange(22.f, 55.f);
	}
	if (CreatureKind < 0)
	{
		if (Now < CreatureNext || Quiet > 0) return;
		// Somewhere well out from every coast.
		for (int32 Try = 0; Try < 40; Try++)
		{
			const FVector2D Spot(FMath::FRandRange(20.f, static_cast<float>(PortsProjection::MapWidth()) - 20.f), FMath::FRandRange(20.f, static_cast<float>(PortsProjection::MapHeight()) - 20.f));
			if (FPortsTerrain::Get().Coast(Spot) > -26.0) continue;
			CreatureAt = Spot;
			const float Angle = FMath::FRandRange(0.f, 2.f * UE_PI);
			CreatureWay = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
			CreatureKind = FMath::RandBool() ? 0 : 1;
			CreatureStart = Now;
			break;
		}
		if (CreatureKind < 0) { CreatureNext = Now + 5.0; return; }
	}
	const double T = Now - CreatureStart;
	// It comes up over a second and a half, and goes down the same way at the end.
	const double Up = FMath::SmoothStep(0.0, 1.5, T) * (1.0 - FMath::SmoothStep(Life - 1.8, Life, T));
	const FVector2D Side(-CreatureWay.Y, CreatureWay.X);
	const FVector2D Mid = CreatureAt + CreatureWay * (T * 1.7);
	PortsGeo::FMeshBuilder Mesh;
	if (CreatureKind == 0)
	{
		// The serpent: coils arching out of the water one behind another, the head held up in front.
		const FLinearColor Back = Hex(TEXT("#2f6b4f")), Belly = Hex(TEXT("#b9c98a"));
		TArray<FVector> Points;
		TArray<double> Radii;
		const int32 N = 40;
		for (int32 i = 0; i <= N; i++)
		{
			const double S = static_cast<double>(i) / N;
			const double Wave = FMath::Sin(S * 3.2 * 2.0 * UE_DOUBLE_PI - T * 2.4);
			double Z = 3.3 * Wave - 1.5;
			// The neck rises to the head over the last part of its length.
			const double Neck = FMath::SmoothStep(0.82, 1.0, S);
			Z = FMath::Lerp(Z, 5.2 + 0.4 * FMath::Sin(T * 2.0), Neck);
			const FVector2D P = Mid + CreatureWay * ((S - 0.5) * 40.0) + Side * (0.9 * FMath::Sin(S * 9.0 + T));
			Points.Add(FVector(P.X, P.Y, Z - 7.0 * (1.0 - Up)));
			Radii.Add(S < 0.12 ? 0.25 + 1.15 * S / 0.12 : S > 0.93 ? FMath::Lerp(1.9, 0.5, (S - 0.93) / 0.07) : 1.4 + 0.45 * Neck);
		}
		Mesh.AddTube(Points, Radii, Back, Belly, 8);
		// A fin along the back of each coil, and two pale eyes.
		for (int32 i = 4; i < N - 6; i += 2) Mesh.AddRound(Points[i] + FVector(0, 0, Radii[i] * 0.7), 0.55, 0, 1.3, Hex(TEXT("#b0261a")), 4);
		const FVector Head = Points[N - 2];
		for (const double Hand : { -1.0, 1.0 }) Mesh.AddRound(Head + FVector(Side.X * Hand * 1.1, Side.Y * Hand * 1.1, 0.9), 0.42, 0.2, 0.5, Hex(TEXT("#fff3c4")), 6);
	}
	else
	{
		// The whale: its back rolls up through the surface and under again, with a spout as it breathes.
		const FLinearColor Back = Hex(TEXT("#3f5266")), Belly = Hex(TEXT("#c9d3d6"));
		const double Roll = FMath::Sin(T * 1.25);
		static const double Girth[11] = { 0.5, 2.1, 3.2, 3.7, 3.6, 3.2, 2.6, 1.9, 1.2, 0.7, 0.35 };
		TArray<FVector> Points;
		TArray<double> Radii;
		for (int32 i = 0; i <= 10; i++)
		{
			const double S = i / 10.0;
			const FVector2D P = Mid + CreatureWay * ((0.5 - S) * 21.0);
			const double Arch = 2.1 * FMath::Sin(S * UE_DOUBLE_PI) + (S > 0.75 ? (S - 0.75) * 9.0 * FMath::Max(0.0, -Roll) : 0.0);
			Points.Add(FVector(P.X, P.Y, -2.3 + Arch + 0.9 * Roll - 7.0 * (1.0 - Up)));
			Radii.Add(Girth[i]);
		}
		Mesh.AddTube(Points, Radii, Back, Belly, 10);
		// The flukes of its tail.
		const FVector Tail = Points[10];
		const FVector Way(CreatureWay.X, CreatureWay.Y, 0), Across(Side.X, Side.Y, 0);
		for (const double Hand : { -1.0, 1.0 })
		{
			const TArray<FVector> Fluke = { Tail + Way * 0.6, Tail - Way * 3.4 + Across * Hand * 3.6 + FVector(0, 0, 0.5), Tail - Way * 2.2 + Across * Hand * 0.4 };
			Mesh.AddFace(Fluke, Tail - FVector(0, 0, 5), Back);
			Mesh.AddFace(Fluke, Tail + FVector(0, 0, 5), Back);
		}
		// The spout, when its back is highest.
		const double Blow = FMath::Clamp((Roll - 0.55) / 0.45, 0.0, 1.0) * Up;
		if (Blow > 0.05)
		{
			const FVector Hole = Points[2] + FVector(0, 0, Radii[2]);
			for (int32 k = 0; k < 5; k++)
			{
				const FVector Lean((k - 2) * 0.55 * Side.X, (k - 2) * 0.55 * Side.Y, 0);
				Mesh.AddRound(Hole + Lean * Blow, 0.3, 0.05, (4.0 + (k % 2) * 1.6) * Blow, FLinearColor(0.86f, 0.94f, 0.95f), 5, false);
			}
		}
	}
	Mesh.Commit(Creature, 0, SurfaceMaterial);
}

void APortsMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	DrawPlague(DeltaSeconds);
	DrawCreature();

	for (FVessel& V : Traffic)
	{
		V.T += DeltaSeconds / V.Duration;
		if (V.T >= 1.0) SendTraffic(V);
		// They grow out of their port and shrink into the next, as the web map fades them in and out.
		PlaceVessel(V, V.T * V.Length, FMath::Clamp(FMath::Min(V.T, 1.0 - V.T) * 8.0, 0.02, 1.0));
	}

	// Voyages being sailed again in the finale.
	for (int32 i = Replays.Num() - 1; i >= 0; i--)
	{
		FReplay& R = Replays[i];
		const double Sailed = GetWorld()->GetTimeSeconds() - R.Start;
		const double Part = FMath::Clamp(Sailed / R.Vessel.Duration, 0.0, 1.0);
		const double Smooth = Part < 0.5 ? 2 * Part * Part : 1 - FMath::Pow(-2 * Part + 2, 2.0) / 2;
		PlaceVessel(R.Vessel, Smooth * R.Vessel.Length, 1.0);
		if (Sailed > R.Vessel.Duration + 0.15)
		{
			R.Vessel.Model->SetVisibility(false);
			SpareModels.Add(R.Vessel.Model);
			TFunction<void()> Done = MoveTemp(R.Done);
			Replays.RemoveAt(i);
			if (Done) Done();
		}
	}

	if (!bShipping) return;
	const double Since = GetWorld()->GetTimeSeconds() - ShipmentStart;
	const double T = FMath::Clamp(Since / Shipment.Duration, 0.0, 1.0);
	const double Eased = T < 0.5 ? 2 * T * T : 1 - FMath::Pow(-2 * T + 2, 2.0) / 2;
	PlaceVessel(Shipment, Eased * Shipment.Length, 1.0);
	ShipmentAt = PortsGeo::PointAtLength(Shipment.Path, Eased * Shipment.Length);
	if (T >= 1.0 && Since > Shipment.Duration + 0.15)
	{
		Shipment.Model->SetVisibility(false);
		bShipping = false;
		BuildHighlights(TArray<FString>());
		TFunction<void()> Done = MoveTemp(ShipmentDone);
		ShipmentDone = nullptr;
		if (Done) Done();
	}
}
