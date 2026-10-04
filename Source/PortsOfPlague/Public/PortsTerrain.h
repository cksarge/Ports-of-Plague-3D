// The lie of the land: how far every point of the board is from the coast, and
// how high the ground stands there. The sea's colour and surf, the hills and
// mountains, and everything that stands or runs on the land (cities, routes,
// rivers, flags) all read their heights from here, so they always agree.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;

class FPortsTerrain
{
public:
	static FPortsTerrain& Get();

	// Works out the coast from the land's outlines (map.json), which reach Margin map pixels past the web map's frame.
	// Lakes and rivers keep the ground low around them, so water never lies on a mountainside.
	void Build(const TArray<TArray<FVector2D>>& LandRings, double Margin, const TArray<TArray<FVector2D>>& Lakes, const TArray<TArray<FVector2D>>& Rivers);
	bool IsBuilt() const { return Cells.Num() > 0; }

	// Map pixels to the nearest coast: positive on land, negative at sea.
	double Coast(const FVector2D& Pixel) const;
	// How high the land stands at a point, in map pixels above the sea. At sea this is the bare land level.
	double Height(const FVector2D& Pixel) const;
	// The surface something lying on the board rests on: the land, or the sea level given.
	double Ground(const FVector2D& Pixel, double SeaLevel) const;
	// Which way the land faces at a point (in world axes).
	FVector Normal(const FVector2D& Pixel) const;
	// 0 on the plains, rising to 1 at a mountain's top.
	double Mountain(const FVector2D& Pixel) const;
	// 0 away from woods, 1 in the middle of one.
	double Forest(const FVector2D& Pixel) const;

	// The coast distances as a picture for the sea's material: 0.5 at the coast, lower out to sea, over FieldRange pixels each way.
	UTexture2D* MakeFieldTexture(UObject* Outer) const;
	static constexpr double FieldRange = 64.0;

	// Where the web map draws its mountains and woods (MOUNTAINS and FORESTS in map.js): middle, how many, how large.
	struct FPatch { FVector2D Centre; int32 Count; double Scale; };
	const TArray<FPatch>& Mountains() const { return Ranges; }
	const TArray<FPatch>& Forests() const { return Woods; }

private:
	double CellSize = 2.0;
	FVector2D Min = FVector2D::ZeroVector;
	int32 Wide = 0, High = 0;
	TArray<float> Cells;
	// Map pixels to the nearest lake or river.
	TArray<float> Wet;
	double NearWater(const FVector2D& Pixel) const;
	TArray<FVector2D> CityPixels;
	TArray<FPatch> Ranges, Woods;

	double MountainHeight(const FVector2D& Pixel) const;
	double NearCity(const FVector2D& Pixel) const;
};
