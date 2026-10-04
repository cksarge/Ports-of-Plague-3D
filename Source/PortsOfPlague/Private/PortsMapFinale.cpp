// What the finale stands on the board: sparks, the family members by their homes, candles,
// towers of Legacy, and the crown with coins and paper falling round it. Everything is drawn
// afresh each moment from what the flow says should be showing (PortsFlowFinale.cpp).
#include "PortsMapActor.h"

#include "PortsData.h"
#include "PortsMapGeometry.h"
#include "PortsMapSpace.h"
#include "PortsTerrain.h"
#include "Algo/Reverse.h"
#include "Components/PointLightComponent.h"
#include "ProceduralMeshComponent.h"

using namespace PortsMapSpace;
using PortsGeo::Hex;

namespace
{
	// A flat piece facing the camera, for things that glow. Positions are in the world.
	void AddSpark(PortsGeo::FMeshBuilder& Mesh, const FVector& At, const FVector& Right, const FVector& Up, float Wide, float High, const FLinearColor& Core, const FLinearColor& Edge)
	{
		const int32 Middle = Mesh.Vertices.Num();
		Mesh.Vertices.Add(At);
		Mesh.Normals.Add(FVector::UpVector);
		Mesh.Colors.Add(Core);
		const int32 Sides = 8;
		for (int32 i = 0; i < Sides; i++)
		{
			const float Angle = 2.f * UE_PI * i / Sides;
			Mesh.Vertices.Add(At + Right * (FMath::Cos(Angle) * Wide) + Up * (FMath::Sin(Angle) * High));
			Mesh.Normals.Add(FVector::UpVector);
			Mesh.Colors.Add(Edge);
		}
		for (int32 i = 0; i < Sides; i++) { Mesh.Triangles.Add(Middle); Mesh.Triangles.Add(Middle + 1 + i); Mesh.Triangles.Add(Middle + 1 + (i + 1) % Sides); }
	}

	// A flat piece that shows from both sides (a coin or a scrap of paper turning as it falls).
	void AddLeaf(PortsGeo::FMeshBuilder& Mesh, const FVector& At, const FVector& A, const FVector& B, const FLinearColor& Color)
	{
		const FVector Normal = FVector::CrossProduct(A, B).GetSafeNormal();
		for (const double Side : { 1.0, -1.0 })
		{
			const int32 First = Mesh.Vertices.Num();
			for (const FVector& Corner : { At - A - B, At + A - B, At + A + B, At - A + B })
			{
				Mesh.Vertices.Add(Corner);
				Mesh.Normals.Add(Normal * Side);
				Mesh.Colors.Add(Color);
			}
			Mesh.AddTriangle(First, First + 1, First + 2, Normal * Side);
			Mesh.AddTriangle(First, First + 2, First + 3, Normal * Side);
		}
	}

	// A number between 0 and 1 that is always the same for the same piece.
	float Chance(int32 Index, int32 Salt) { return FMath::Frac(FMath::Sin(Index * 12.9898f + Salt * 78.233f) * 43758.5453f); }
}

void APortsMapActor::ClearFinale()
{
	if (Props)
	{
		Props->ClearMeshSection(0);
		Props->ClearMeshSection(1);
	}
	for (UPointLightComponent* Lamp : Lamps) if (Lamp) Lamp->SetVisibility(false);
	for (FReplay& R : Replays) if (R.Vessel.Model) { R.Vessel.Model->SetVisibility(false); SpareModels.Add(R.Vessel.Model); }
	Replays.Reset();
}

void APortsMapActor::SetQuiet(int32 Level)
{
	Quiet = Level;
	// The map's own title and compass (sections 7 and 8 of the board) are put away with the city names.
	if (Surface) { Surface->SetMeshSectionVisible(7, Level < 2); Surface->SetMeshSectionVisible(8, Level < 2); }
	if (Flags) Flags->SetVisibility(Level == 0);
	if (Level > 0) BuildRings(TArray<FRing>());
	for (FVessel& V : Traffic) if (V.Model) V.Model->SetVisibility(Level == 0);
}

void APortsMapActor::ShowStainsAt(const TArray<FString>& CityIds)
{
	const FPortsData& Data = FPortsData::Get();
	for (int32 i = 0; i < StainOn.Num(); i++) StainOn[i] = CityIds.Contains(Data.Cities[i].Id);
}

void APortsMapActor::ReplayVoyage(const FString& RouteId, const FString& From, const FLinearColor& Color, TFunction<void()> OnDone)
{
	const TArray<FVector2D>* Curve = RouteCurves.Find(RouteId);
	const FPortsRoute* Route = FPortsData::Get().FindRoute(RouteId);
	if (!Curve || !Route) { if (OnDone) OnDone(); return; }
	FReplay R;
	R.Vessel.Model = SpareModels.Num() ? SpareModels.Pop() : NewVesselModel();
	R.Vessel.Path = *Curve;
	if (Route->A != From) Algo::Reverse(R.Vessel.Path);
	R.Vessel.Length = PortsGeo::Length(R.Vessel.Path);
	// A little slower than in play, so that a dozen ships can be watched at once.
	R.Vessel.Duration = FMath::Min(2.2, 0.7 + R.Vessel.Length * 0.0022);
	R.Vessel.bSea = Route->bSea;
	R.Vessel.Size = 1.35;
	R.Start = GetWorld()->GetTimeSeconds();
	R.Done = MoveTemp(OnDone);
	const int32 Kind = !Route->bSea ? 2 : FMath::Max(FPortsData::Get().FindCity(Route->A)->Lat, FPortsData::Get().FindCity(Route->B)->Lat) > 47.0 ? 0 : 1;
	BuildVessel(R.Vessel.Model, Kind, Color, Hex(TEXT("#4a2f14")), Hex(TEXT("#1a1208")));
	R.Vessel.Model->SetVisibility(true);
	PlaceVessel(R.Vessel, 0, 1.0);
	Replays.Add(MoveTemp(R));
}

void APortsMapActor::DrawFinale(const FPortsFinaleProps& P)
{
	const FPortsTerrain& Terrain = FPortsTerrain::Get();
	PortsGeo::FMeshBuilder Solid, Glow;
	int32 LampsUsed = 0;
	const auto Lamp = [&](const FVector& At, const FLinearColor& Color, float Strength, float Reach)
	{
		if (!Lamps.IsValidIndex(LampsUsed))
		{
			UPointLightComponent* New = NewObject<UPointLightComponent>(this);
			New->SetMobility(EComponentMobility::Movable);
			New->SetupAttachment(GetRootComponent());
			New->bUseInverseSquaredFalloff = false;
			New->SetLightFalloffExponent(2.2f);
			New->SetCastShadows(false);
			New->RegisterComponent();
			Lamps.Add(New);
		}
		UPointLightComponent* L = Lamps[LampsUsed++];
		L->SetVisibility(true);
		L->SetWorldLocation(At);
		L->SetLightColor(Color);
		L->SetIntensity(Strength);
		L->SetAttenuationRadius(Reach);
	};

	// Sparks: each rises from the board, drifting, and burns out before it starts again.
	const double W = PortsProjection::MapWidth(), H = PortsProjection::MapHeight();
	for (int32 i = 0; i < P.Embers; i++)
	{
		const float Life = 7.f + 7.f * Chance(i, 1);
		const float T = FMath::Frac(static_cast<float>(P.Time) / Life + Chance(i, 2));
		const FVector2D From(-60.0 + (W + 120.0) * Chance(i, 3), -40.0 + (H + 80.0) * Chance(i, 4));
		const FVector At = ToWorld(From + FVector2D((Chance(i, 5) - 0.5) * 60.0 * T, 0), 4.0 + 150.0 * T);
		const float Bright = T < 0.1f ? T / 0.1f * 0.9f : T < 0.8f ? FMath::Lerp(0.9f, 0.55f, (T - 0.1f) / 0.7f) : 0.55f * (1.f - (T - 0.8f) / 0.2f);
		const float Size = (1.3f + 1.7f * Chance(i, 6)) * UnitsPerPixel;
		AddSpark(Glow, At, P.Right, P.Up, Size, Size, FLinearColor(3.2f, 2.6f, 1.5f, Bright), FLinearColor(1.9f, 0.9f, 0.12f, 0.f));
	}

	// Family members: a robed figure for each; the lost rise a little, fall, and turn grey.
	const FLinearColor Ash = Hex(TEXT("#5d574b"));
	for (const FPortsFinaleProps::FFigure& F : P.Figures)
	{
		if (F.Pop <= 0.f) continue;
		const float Fallen = FMath::Clamp(F.Fall, 0.f, 1.f);
		const float Size = F.Pop * (1.f + 0.15f * FMath::Sin(FMath::Min(1.f, Fallen / 0.35f) * UE_PI)) * (1.f - 0.12f * Fallen);
		const FLinearColor Color = FMath::Lerp(F.Color, Ash, Fallen);
		const int32 First = Solid.Vertices.Num();
		Solid.AddRound(Local3(0, 0, 0), 2.1, 0.75, 5.4, Color, 9, false);
		Solid.AddRound(Local3(0, 0, 5.0), 1.3, 1.45, 1.0, Color * 1.12f, 9, false);
		Solid.AddRound(Local3(0, 0, 6.0), 1.45, 0.5, 1.2, Color * 1.12f, 9, true);
		// Stood up at the middle of the map, then scaled, tipped over towards the east, and set in its place.
		const double Tip = FMath::DegreesToRadians(84.0 * FMath::Clamp((Fallen - 0.2f) / 0.8f, 0.f, 1.f));
		const double Ground = Terrain.Ground(F.At, RouteZ) + 0.4;
		for (int32 v = First; v < Solid.Vertices.Num(); v++)
		{
			const FVector Q = Solid.Vertices[v] / UnitsPerPixel * Size * 1.7;
			// World axes: X is north, Y is east, Z is up.
			const FVector Turned(Q.X, Q.Y * FMath::Cos(Tip) + Q.Z * FMath::Sin(Tip), -Q.Y * FMath::Sin(Tip) + Q.Z * FMath::Cos(Tip));
			Solid.Vertices[v] = ToWorld(F.At, Ground) + Turned * UnitsPerPixel;
			const FVector N = Solid.Normals[v];
			Solid.Normals[v] = FVector(N.X, N.Y * FMath::Cos(Tip) + N.Z * FMath::Sin(Tip), -N.Y * FMath::Sin(Tip) + N.Z * FMath::Cos(Tip));
		}
	}

	// Candles: a tall wax candle in a low dish. A lit one burns with a flame that leans and flickers, and lights the land round it.
	for (int32 i = 0; i < P.Candles.Num(); i++)
	{
		const FPortsFinaleProps::FCandle& C = P.Candles[i];
		if (C.Rise <= 0.f) continue;
		const double Ground = Terrain.Ground(C.At, RouteZ) + 0.4;
		const FVector Foot(C.At.X, C.At.Y, Ground);
		const float High = 24.f * C.Rise;
		const FLinearColor Wax = C.bLit ? Hex(TEXT("#fff3d2")) : Hex(TEXT("#8f8674"));
		Solid.AddRound(Foot, 5.6, 4.6, 0.9, Hex(TEXT("#8a5a12")), 14);
		Solid.AddRound(Foot + FVector(0, 0, 0.9), 3.0, 2.7, High, Wax, 12);
		Solid.AddRound(Foot + FVector(0, 0, 0.9 + High), 0.22, 0.18, 1.2, Hex(TEXT("#1a1208")), 5);
		if (!C.bLit) continue;
		const float Flicker = 0.88f + 0.12f * FMath::Sin(static_cast<float>(P.Time) * 9.f + i * 2.1f) * FMath::Sin(static_cast<float>(P.Time) * 5.3f + i);
		const FVector Tip = ToWorld(C.At, Ground + 0.9 + High + 4.2);
		AddSpark(Glow, Tip, P.Right, P.Up, 2.2f * UnitsPerPixel, 5.0f * UnitsPerPixel * Flicker, FLinearColor(6.f, 4.6f, 2.2f, C.Rise), FLinearColor(2.2f, 0.8f, 0.08f, 0.f));
		AddSpark(Glow, Tip, P.Right, P.Up, 9.f * UnitsPerPixel, 9.f * UnitsPerPixel, FLinearColor(1.f, 0.62f, 0.2f, 0.34f * C.Rise * Flicker), FLinearColor(1.f, 0.5f, 0.1f, 0.f));
		Lamp(Tip + FVector(0, 0, 60), FLinearColor(1.f, 0.72f, 0.36f), 5.5f * C.Rise * Flicker, 2400.f);
	}

	// Towers of Legacy: four storeys in the colours of the finale's bars; the winner's is ringed with gold.
	static const TCHAR* const Storeys[4] = { TEXT("#d9a82b"), TEXT("#2f8f68"), TEXT("#3a73c0"), TEXT("#8a52b8") };
	for (const FPortsFinaleProps::FTower& T : P.Towers)
	{
		const double Ground = Terrain.Ground(T.At, RouteZ) + 0.4;
		double Z = Ground;
		bool bAny = false;
		for (int32 k = 0; k < 4; k++)
		{
			if (T.Parts[k] <= 0.01f) continue;
			bAny = true;
			Solid.AddBox(FVector(T.At.X, T.At.Y, Z), FVector(10.0, 10.0, T.Parts[k]), 0, Hex(Storeys[k]) * 1.25f);
			Z += T.Parts[k];
			Solid.AddBox(FVector(T.At.X, T.At.Y, Z), FVector(11.0, 11.0, 0.3), 0, Hex(TEXT("#f3d27a")));
			Z += 0.25;
		}
		if (bAny) Solid.AddBox(FVector(T.At.X, T.At.Y, Ground - 0.5), FVector(13.0, 13.0, 0.5), 0, Hex(TEXT("#5a3920")));
		if (T.bWinner && bAny)
		{
			const FVector Top = ToWorld(T.At, Z + 5.0);
			AddSpark(Glow, Top, P.Right, P.Up, 15.f * UnitsPerPixel, 15.f * UnitsPerPixel, FLinearColor(2.6f, 2.0f, 0.8f, 0.5f), FLinearColor(1.5f, 0.9f, 0.1f, 0.f));
			Lamp(Top, FLinearColor(1.f, 0.84f, 0.45f), 7.f, 3000.f);
		}
	}

	// The crown: a jewelled circlet with five points, turning slowly as it comes down, in a shaft of gold light.
	if (P.bCrown)
	{
		const double Ground = Terrain.Ground(P.CrownAt, RouteZ);
		const float E = 1.f - FMath::Pow(1.f - FMath::Clamp(P.CrownDrop, 0.f, 1.f), 3.f);
		const double Z = Ground + FMath::Lerp(95.0, 21.0, static_cast<double>(E)) + 0.8 * FMath::Sin(P.Time * 1.7);
		const double Spin = P.Time * 0.55;
		const FLinearColor GoldLight = Hex(TEXT("#f6d77a")), GoldDark = Hex(TEXT("#b9831c"));
		const double R = 8.6;
		const int32 Sides = 30;
		const auto Rim = [&](int32 i, double Radius, double Height) { const double A = Spin + 2.0 * UE_DOUBLE_PI * i / Sides; return FVector(P.CrownAt.X + FMath::Cos(A) * Radius, P.CrownAt.Y + FMath::Sin(A) * Radius, Z + Height); };
		const FVector Middle(P.CrownAt.X, P.CrownAt.Y, Z + 2.0);
		for (int32 i = 0; i < Sides; i++)
		{
			// The band, outside and inside, and its top edge rising to a point every sixth of the way round.
			const auto Peak = [&](int32 k) { const double U = FMath::Abs(FMath::Frac(k * 5.0 / Sides + 0.5) - 0.5) * 2.0; return 3.4 + 6.2 * FMath::Pow(1.0 - U, 1.6); };
			const FLinearColor Shade = (i % 2) ? GoldLight : FMath::Lerp(GoldLight, GoldDark, 0.35f);
			Solid.AddFace({ Rim(i, R, 0), Rim(i + 1, R, 0), Rim(i + 1, R, Peak(i + 1)), Rim(i, R, Peak(i)) }, Middle, Shade);
			Solid.AddFace({ Rim(i, R - 0.5, 0), Rim(i + 1, R - 0.5, 0), Rim(i + 1, R - 0.5, Peak(i + 1)), Rim(i, R - 0.5, Peak(i)) }, Rim(i, R + 4, 2), GoldDark);
			Solid.AddFace({ Rim(i, R + 0.35, -0.2), Rim(i + 1, R + 0.35, -0.2), Rim(i + 1, R + 0.35, 1.5), Rim(i, R + 0.35, 1.5) }, Middle, GoldDark);
		}
		for (int32 k = 0; k < 5; k++)
		{
			// A pearl on each point, and jewels round the band.
			const FVector Point = Rim(k * Sides / 5, R - 0.25, 9.6);
			Solid.AddRound(Point, 0.95, 0.5, 1.3, Hex(TEXT("#fff3c4")), 8);
			const FVector Jewel = Rim(k * Sides / 5 + Sides / 10, R + 0.5, 1.9);
			Solid.AddRound(Jewel - FVector(0, 0, 0.8), 0.95, 0.95, 1.6, Hex(k % 2 ? TEXT("#1d4a86") : TEXT("#b0261a")), 8);
		}
		const FVector Heart = ToWorld(P.CrownAt, Z + 4.0);
		AddSpark(Glow, Heart, P.Right, P.Up, 30.f * UnitsPerPixel, 30.f * UnitsPerPixel, FLinearColor(2.2f, 1.7f, 0.7f, 0.42f * E), FLinearColor(1.4f, 0.8f, 0.1f, 0.f));
		// The shaft of light it comes down in: long thin rays fanning from above.
		for (int32 k = 0; k < 9; k++)
		{
			const float Lean = (k - 4) * 0.13f + 0.05f * FMath::Sin(static_cast<float>(P.Time) * 0.7f + k);
			const FVector Along = (P.Up + P.Right * Lean).GetSafeNormal();
			const FVector Across = FVector::CrossProduct(Along, FVector::CrossProduct(P.Right, P.Up)).GetSafeNormal();
			AddSpark(Glow, Heart + Along * 110.f * UnitsPerPixel, Across, Along, (2.2f + 1.3f * Chance(k, 9)) * UnitsPerPixel, 120.f * UnitsPerPixel,
				FLinearColor(1.5f, 1.15f, 0.5f, 0.2f * E), FLinearColor(1.2f, 0.8f, 0.2f, 0.f));
		}
		Lamp(Heart + FVector(0, 0, 160), FLinearColor(1.f, 0.82f, 0.45f), 9.f * E, 4600.f);

		// Gold florins and scraps of coloured paper, bursting out and drifting down, each turning as it falls.
		const int32 Pieces = 190;
		for (int32 i = 0; i < Pieces; i++)
		{
			const float Life = 4.2f + 3.f * Chance(i, 11);
			const float T = FMath::Frac(static_cast<float>(P.Time) / Life + Chance(i, 12));
			const float Angle = 2.f * UE_PI * Chance(i, 13), Reach = (10.f + 46.f * FMath::Sqrt(Chance(i, 14))) * (0.35f + 0.65f * FMath::Min(1.f, T * 3.f));
			const FVector2D Over = P.CrownAt + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Reach + FVector2D(FMath::Sin(P.Time * 0.9 + i) * 2.5, 0);
			const double High = Ground + 4.0 + 84.0 * (1.0 - T * T);
			const bool bCoin = i % 3 == 0;
			const FLinearColor Color = bCoin || P.Confetti.Num() == 0 ? (i % 2 ? GoldLight : Hex(TEXT("#d9a82b"))) : P.Confetti[i % P.Confetti.Num()];
			const float Roll = static_cast<float>(P.Time) * (2.4f + 3.f * Chance(i, 15)) + i;
			const FVector A = (P.Right * FMath::Cos(Roll) + FVector::UpVector * FMath::Sin(Roll)) * (bCoin ? 1.25f : 1.5f) * UnitsPerPixel;
			const FVector B = (P.Up * FMath::Cos(Roll * 0.7f) + P.Right * FMath::Sin(Roll * 0.7f)) * (bCoin ? 1.25f : 0.8f) * UnitsPerPixel;
			AddLeaf(Solid, ToWorld(Over, High), A, B, Color * (E > 0.4f ? 1.f : 0.f));
		}
	}

	if (Solid.Vertices.Num()) Solid.Commit(Props, 0, SurfaceMaterial); else Props->ClearMeshSection(0);
	if (Glow.Vertices.Num()) Glow.Commit(Props, 1, GlowMaterial ? GlowMaterial : SurfaceMaterial); else Props->ClearMeshSection(1);
	for (int32 i = LampsUsed; i < Lamps.Num(); i++) if (Lamps[i]) Lamps[i]->SetVisibility(false);
}
