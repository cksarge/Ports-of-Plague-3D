#include "PortsTerrain.h"

#include "PortsData.h"
#include "PortsMapSpace.h"
#include "PortsProjection.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

using namespace PortsMapSpace;

namespace
{
	double Smooth(double From, double To, double X)
	{
		const double T = FMath::Clamp((X - From) / (To - From), 0.0, 1.0);
		return T * T * (3 - 2 * T);
	}

	// Distances across a grid to the nearest cell marked 0, by sweeping it twice.
	void Sweep(TArray<float>& D, int32 W, int32 H)
	{
		const float Side = 1.f, Diagonal = 1.41421f;
		const auto At = [&](int32 X, int32 Y) -> float& { return D[Y * W + X]; };
		for (int32 Y = 0; Y < H; Y++) for (int32 X = 0; X < W; X++)
		{
			float& V = At(X, Y);
			if (X > 0) V = FMath::Min(V, At(X - 1, Y) + Side);
			if (Y > 0) V = FMath::Min(V, At(X, Y - 1) + Side);
			if (X > 0 && Y > 0) V = FMath::Min(V, At(X - 1, Y - 1) + Diagonal);
			if (X < W - 1 && Y > 0) V = FMath::Min(V, At(X + 1, Y - 1) + Diagonal);
		}
		for (int32 Y = H - 1; Y >= 0; Y--) for (int32 X = W - 1; X >= 0; X--)
		{
			float& V = At(X, Y);
			if (X < W - 1) V = FMath::Min(V, At(X + 1, Y) + Side);
			if (Y < H - 1) V = FMath::Min(V, At(X, Y + 1) + Side);
			if (X < W - 1 && Y < H - 1) V = FMath::Min(V, At(X + 1, Y + 1) + Diagonal);
			if (X > 0 && Y < H - 1) V = FMath::Min(V, At(X - 1, Y + 1) + Diagonal);
		}
	}
}

FPortsTerrain& FPortsTerrain::Get()
{
	static FPortsTerrain Instance;
	return Instance;
}

void FPortsTerrain::Build(const TArray<TArray<FVector2D>>& LandRings, double Margin, const TArray<TArray<FVector2D>>& Lakes, const TArray<TArray<FVector2D>>& Rivers)
{
	Min = FVector2D(-Margin, -Margin);
	Wide = FMath::CeilToInt32((PortsProjection::MapWidth() + 2 * Margin) / CellSize);
	High = FMath::CeilToInt32((PortsProjection::MapHeight() + 2 * Margin) / CellSize);

	// Which cells are land: along each row, the land lies between pairs of outline crossings.
	TArray<bool> Land;
	Land.SetNumZeroed(Wide * High);
	TArray<double> Crossings;
	for (int32 Y = 0; Y < High; Y++)
	{
		const double Py = Min.Y + (Y + 0.5) * CellSize;
		Crossings.Reset();
		for (const TArray<FVector2D>& Ring : LandRings)
		{
			for (int32 i = 0, N = Ring.Num(); i < N; i++)
			{
				const FVector2D& A = Ring[i];
				const FVector2D& B = Ring[(i + 1) % N];
				if ((A.Y <= Py) == (B.Y <= Py)) continue;
				Crossings.Add(A.X + (Py - A.Y) / (B.Y - A.Y) * (B.X - A.X));
			}
		}
		Crossings.Sort();
		for (int32 i = 0; i + 1 < Crossings.Num(); i += 2)
		{
			const int32 From = FMath::Clamp(FMath::CeilToInt32((Crossings[i] - Min.X) / CellSize - 0.5), 0, Wide);
			const int32 To = FMath::Clamp(FMath::FloorToInt32((Crossings[i + 1] - Min.X) / CellSize - 0.5), -1, Wide - 1);
			for (int32 X = From; X <= To; X++) Land[Y * Wide + X] = true;
		}
	}

	const float Far = 1e6f;
	TArray<float> ToSea, ToLand;
	ToSea.SetNumUninitialized(Wide * High);
	ToLand.SetNumUninitialized(Wide * High);
	for (int32 i = 0; i < Wide * High; i++) { ToSea[i] = Land[i] ? Far : 0.f; ToLand[i] = Land[i] ? 0.f : Far; }
	Sweep(ToSea, Wide, High);
	Sweep(ToLand, Wide, High);
	Cells.SetNumUninitialized(Wide * High);
	for (int32 i = 0; i < Wide * High; i++) Cells[i] = static_cast<float>((Land[i] ? ToSea[i] - 0.5f : -(ToLand[i] - 0.5f)) * CellSize);

	// How far every cell is from fresh water: the outlines of the lakes and the lines of the rivers.
	Wet.Init(Far, Wide * High);
	const auto Mark = [&](const TArray<FVector2D>& Line, bool bClosed)
	{
		const int32 N = Line.Num();
		for (int32 i = 0; i + (bClosed ? 0 : 1) < N; i++)
		{
			const FVector2D A = Line[i], B = Line[(i + 1) % N];
			const int32 Steps = FMath::Max(1, FMath::CeilToInt32(FVector2D::Distance(A, B) / CellSize));
			for (int32 k = 0; k <= Steps; k++)
			{
				const FVector2D P = FMath::Lerp(A, B, static_cast<double>(k) / Steps);
				const int32 X = FMath::FloorToInt32((P.X - Min.X) / CellSize), Y = FMath::FloorToInt32((P.Y - Min.Y) / CellSize);
				if (X >= 0 && Y >= 0 && X < Wide && Y < High) Wet[Y * Wide + X] = 0.f;
			}
		}
	};
	for (const TArray<FVector2D>& Lake : Lakes) Mark(Lake, true);
	for (const TArray<FVector2D>& River : Rivers) Mark(River, false);
	Sweep(Wet, Wide, High);
	for (float& V : Wet) V *= static_cast<float>(CellSize);

	CityPixels.Reset();
	for (const FPortsCity& City : FPortsData::Get().Cities) CityPixels.Add(CityPixel(City));

	// Mountains and woods: longitude, latitude, how many are drawn in a row, and their size. The first ten
	// ranges and eight woods are the web map's own (MOUNTAINS and FORESTS in map.js); the rest fill in the
	// real ranges and forests of Europe, North Africa and the Near East so the land looks as it does.
	struct FRow { double Lon, Lat; int32 Count; double Scale; };
	static const FRow MountainRows[] = {
		{ 7.2, 46.4, 5, 1 }, { 10.5, 46.8, 4, 0.9 }, { -0.8, 42.7, 4, 0.85 }, { 22.5, 48.2, 4, 0.85 }, { 21.5, 42.6, 3, 0.8 }, { 12.8, 43.2, 2, 0.7 }, { 8.2, 60.5, 3, 0.8 }, { -4, 32.8, 4, 0.9 }, { 40.5, 43.2, 3, 0.9 }, { 15.5, 50.4, 2, 0.7 },
		// Alps, east and south-west
		{ 13.6, 47.2, 4, 0.85 }, { 6.6, 44.6, 2, 0.8 },
		// Pyrenees (west), Cantabrian Mountains, the Spanish central ranges, Sierra Nevada
		{ 1.4, 42.5, 2, 0.8 }, { -5.2, 43.1, 4, 0.7 }, { -4.6, 40.5, 3, 0.65 }, { -1.8, 40.6, 2, 0.6 }, { -3.0, 37.1, 2, 0.7 },
		// Massif Central, Jura and Vosges
		{ 2.9, 45.1, 3, 0.6 }, { 6.6, 47.6, 2, 0.5 },
		// Apennines, down the length of Italy
		{ 9.8, 44.4, 2, 0.6 }, { 13.6, 42.3, 2, 0.75 }, { 15.6, 40.6, 2, 0.65 }, { 16.3, 39.1, 1, 0.6 },
		// Dinaric Alps, Balkan Mountains, Rhodope, Pindus
		{ 16.6, 44.3, 3, 0.7 }, { 19.0, 43.0, 3, 0.75 }, { 25.0, 42.75, 4, 0.65 }, { 24.3, 41.6, 2, 0.65 }, { 21.3, 39.7, 2, 0.7 }, { 22.4, 37.6, 1, 0.6 },
		// Carpathians: the western arc, the eastern, and the Transylvanian Alps
		{ 19.6, 49.2, 3, 0.75 }, { 25.2, 47.2, 3, 0.75 }, { 24.4, 45.5, 4, 0.75 },
		// Scandinavian mountains and the Scottish Highlands
		{ 6.8, 61.6, 3, 0.8 }, { 10.0, 62.2, 3, 0.75 }, { 13.4, 62.6, 2, 0.7 }, { -4.6, 57.0, 3, 0.55 }, { -3.6, 52.8, 1, 0.5 },
		// Atlas, along North Africa
		{ -7.6, 31.4, 3, 0.9 }, { -0.5, 34.4, 4, 0.7 }, { 4.5, 36.0, 3, 0.7 }, { 7.5, 35.4, 2, 0.7 },
		// Anatolia: the Pontic range along the Black Sea and the Taurus along the south
		{ 33.5, 41.2, 3, 0.65 }, { 37.5, 40.6, 4, 0.75 }, { 30.6, 37.0, 3, 0.75 }, { 34.2, 37.2, 3, 0.8 }, { 37.6, 37.9, 3, 0.75 },
		// Caucasus (west), Lebanon, and the Crimean hills
		{ 37.4, 44.4, 2, 0.75 }, { 36.0, 34.0, 1, 0.7 }, { 34.2, 44.6, 1, 0.45 },
	};
	static const FRow ForestRows[] = {
		{ 9, 51.8, 3, 1 }, { 18, 52.8, 4, 1 }, { 29, 56, 4, 1 }, { 26, 61, 3, 1 }, { 14, 57.3, 3, 1 }, { 34, 52.5, 3, 1 }, { 0.5, 48.8, 2, 1 }, { 4, 50.2, 2, 1 },
		// France and the Low Countries
		{ -1.2, 47.6, 2, 1 }, { 3.6, 47.3, 2, 1 }, { 5.6, 49.6, 2, 1 }, { 0.6, 44.8, 2, 1 },
		// Germany and Bohemia
		{ 8.4, 48.2, 2, 1 }, { 11.4, 50.6, 3, 1 }, { 13.4, 49.2, 3, 1 }, { 11.8, 52.8, 2, 1 }, { 10.2, 48.6, 2, 1 },
		// Poland, Lithuania and the Baltic shore
		{ 21.5, 51.0, 3, 1 }, { 23.5, 53.4, 4, 1 }, { 20.4, 54.0, 2, 1 }, { 24.5, 55.6, 3, 1 }, { 25.5, 57.6, 3, 1 }, { 26.4, 59.0, 2, 1 },
		// The Russian forest
		{ 31.5, 58.6, 4, 1 }, { 35.5, 57.6, 4, 1 }, { 39.5, 56.6, 3, 1 }, { 32.0, 55.0, 4, 1 }, { 37.5, 54.2, 3, 1 }, { 28.0, 54.0, 3, 1 }, { 34.5, 60.4, 4, 1 }, { 39.5, 59.6, 3, 1 }, { 30.4, 61.4, 3, 1 },
		// Scandinavia and Finland
		{ 15.0, 59.6, 3, 1 }, { 16.5, 61.4, 3, 1 }, { 12.4, 59.4, 2, 1 }, { 24.5, 61.6, 3, 1 }, { 28.0, 62.2, 2, 1 }, { 14.4, 56.2, 2, 1 },
		// Britain and Ireland
		{ -1.6, 52.6, 2, 1 }, { -2.6, 54.6, 1, 1 }, { -8.0, 53.0, 1, 1 },
		// The Carpathian and Balkan woods, northern Iberia, northern Anatolia
		{ 23.4, 46.4, 3, 1 }, { 26.6, 48.6, 2, 1 }, { 20.4, 44.4, 2, 1 }, { 23.0, 43.6, 2, 1 }, { 17.6, 45.6, 2, 1 }, { -7.4, 42.6, 2, 1 }, { 32.0, 40.6, 2, 1 }, { 15.0, 47.4, 2, 1 },
	};
	Ranges.Reset();
	Woods.Reset();
	for (const FRow& R : MountainRows) Ranges.Add({ PortsProjection::Project(R.Lon, R.Lat), R.Count, R.Scale });
	for (const FRow& R : ForestRows) Woods.Add({ PortsProjection::Project(R.Lon, R.Lat), R.Count, R.Scale });
}

double FPortsTerrain::Coast(const FVector2D& Pixel) const
{
	if (!IsBuilt()) return 1.0;
	const double Gx = FMath::Clamp((Pixel.X - Min.X) / CellSize - 0.5, 0.0, Wide - 1.001);
	const double Gy = FMath::Clamp((Pixel.Y - Min.Y) / CellSize - 0.5, 0.0, High - 1.001);
	const int32 X = FMath::FloorToInt32(Gx), Y = FMath::FloorToInt32(Gy);
	const double Fx = Gx - X, Fy = Gy - Y;
	const auto At = [&](int32 Cx, int32 Cy) { return static_cast<double>(Cells[Cy * Wide + Cx]); };
	return FMath::Lerp(FMath::Lerp(At(X, Y), At(X + 1, Y), Fx), FMath::Lerp(At(X, Y + 1), At(X + 1, Y + 1), Fx), Fy);
}

double FPortsTerrain::NearWater(const FVector2D& Pixel) const
{
	const int32 X = FMath::Clamp(FMath::FloorToInt32((Pixel.X - Min.X) / CellSize), 0, Wide - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt32((Pixel.Y - Min.Y) / CellSize), 0, High - 1);
	return Wet[Y * Wide + X];
}

// 0 at a city, 1 well away from one: the ground is kept level where a city stands.
double FPortsTerrain::NearCity(const FVector2D& Pixel) const
{
	double Nearest = 1e9;
	for (const FVector2D& City : CityPixels) Nearest = FMath::Min(Nearest, FVector2D::DistSquared(City, Pixel));
	// Rivers run in valleys and lakes lie in hollows: the ground only rises away from them.
	return Smooth(15.0, 40.0, FMath::Sqrt(Nearest)) * (0.3 + 0.7 * Smooth(1.0, 10.0, NearWater(Pixel)));
}

// Mountains are broad ranges, not single hills: each patch is a long swell of high ground, and
// within it the land is folded into ridges and valleys that run into its neighbours, the way a
// relief map shows them. They rise gradually out of the country round them.
double FPortsTerrain::MountainHeight(const FVector2D& Pixel) const
{
	double Swell = 0, Size = 0;
	for (const FPatch& Range : Ranges)
	{
		const FVector2D D = (Pixel - Range.Centre) / FVector2D(8.5 * Range.Count * Range.Scale + 7.0, 11.0 * Range.Scale + 3.0);
		const double Reach = D.Size();
		if (Reach >= 1.3) continue;
		const double Here = 1.0 - Smooth(0.15, 1.3, Reach);
		if (Here > Swell) { Swell = Here; Size = Range.Scale; }
	}
	if (Swell <= 0) return 0;
	// The edge of a range is uneven, so it never ends in a neat oval.
	const double Ragged = FMath::Clamp(Swell + 0.22 * FMath::PerlinNoise2D(FVector2D(Pixel.X * 0.045 + 3.0, Pixel.Y * 0.045 + 11.0)) * (1.0 - Swell), 0.0, 1.0);
	const double Broad = 1.0 - FMath::Abs(FMath::PerlinNoise2D(FVector2D(Pixel.X * 0.060 + 31.0, Pixel.Y * 0.060 - 7.0)));
	const double Fine = 1.0 - FMath::Abs(FMath::PerlinNoise2D(FVector2D(Pixel.X * 0.150 - 13.0, Pixel.Y * 0.150 + 19.0)));
	const double Folds = 0.30 + 0.50 * Broad * Broad + 0.20 * Fine;
	return 9.5 * Size * Ragged * Ragged * (3.0 - 2.0 * Ragged) * Folds;
}

double FPortsTerrain::Mountain(const FVector2D& Pixel) const
{
	return FMath::Clamp(MountainHeight(Pixel) * NearCity(Pixel) / 8.0, 0.0, 1.0);
}

// Woodland thins out raggedly at its edges and has clearings in it, so it never ends in a neat oval.
double FPortsTerrain::Forest(const FVector2D& Pixel) const
{
	double Best = 0;
	for (const FPatch& Wood : Woods)
	{
		const FVector2D D = (Pixel - Wood.Centre) / FVector2D(10.0 * Wood.Count + 6.0, 13.0);
		Best = FMath::Max(Best, 1.0 - Smooth(0.2, 1.25, D.Size()));
	}
	if (Best <= 0) return 0;
	const double Patchy = 0.5 + 0.5 * FMath::PerlinNoise2D(FVector2D(Pixel.X * 0.055 + 9.0, Pixel.Y * 0.055 - 4.0)) + 0.25 * FMath::PerlinNoise2D(FVector2D(Pixel.X * 0.17, Pixel.Y * 0.17 + 23.0));
	return FMath::Clamp(Best * (0.35 + 0.9 * Patchy), 0.0, 1.0) * Smooth(0.05, 0.3, Best);
}

double FPortsTerrain::Height(const FVector2D& Pixel) const
{
	const double D = Coast(Pixel);
	if (D <= 0) return LandZ;
	const double Level = NearCity(Pixel);
	// The shore rises in a low bank; inland the ground rolls gently.
	const double Bank = Smooth(0.0, 9.0, D) * 0.7;
	const double Roll = 0.5 + 0.33 * FMath::PerlinNoise2D(FVector2D(Pixel.X * 0.011, Pixel.Y * 0.011)) + 0.17 * FMath::PerlinNoise2D(FVector2D(Pixel.X * 0.034 + 17.0, Pixel.Y * 0.034 + 5.0));
	const double Hills = Roll * 1.5 * Smooth(5.0, 45.0, D) * Level;
	return LandZ + Bank + Hills + MountainHeight(Pixel) * Level * Smooth(1.0, 10.0, D);
}

double FPortsTerrain::Ground(const FVector2D& Pixel, double SeaLevel) const
{
	return Coast(Pixel) > 0 ? FMath::Max(SeaLevel, Height(Pixel)) : SeaLevel;
}

FVector FPortsTerrain::Normal(const FVector2D& Pixel) const
{
	const double E = 1.5;
	const double Dx = (Height(Pixel + FVector2D(E, 0)) - Height(Pixel - FVector2D(E, 0))) / (2 * E);
	const double Dy = (Height(Pixel + FVector2D(0, E)) - Height(Pixel - FVector2D(0, E))) / (2 * E);
	// Pixel x is world +Y; pixel y is world -X.
	return FVector(Dy, -Dx, 1.0).GetSafeNormal();
}

UTexture2D* FPortsTerrain::MakeFieldTexture(UObject* Outer) const
{
	if (!IsBuilt()) return nullptr;
	UTexture2D* Texture = UTexture2D::CreateTransient(Wide, High, PF_B8G8R8A8);
	if (!Texture) return nullptr;
	Texture->SRGB = false;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->Filter = TF_Bilinear;
	Texture->CompressionSettings = TC_VectorDisplacementmap;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	uint8* Bytes = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 i = 0; i < Wide * High; i++)
	{
		const uint8 V = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32((0.5 + Cells[i] / (2.0 * FieldRange)) * 255.0), 0, 255));
		Bytes[i * 4] = V; Bytes[i * 4 + 1] = V; Bytes[i * 4 + 2] = V; Bytes[i * 4 + 3] = 255;
	}
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}
