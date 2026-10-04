// Where the map sits in the 3D world. Everything on the board is laid out in
// the web map's pixels (see PortsProjection.h) and converted here, so cities,
// routes and coastlines line up exactly as they do on the website.
// North is +X, east is +Y, up is +Z, and the middle of the map is the origin.
#pragma once

#include "CoreMinimal.h"
#include "PortsData.h"
#include "PortsProjection.h"

namespace PortsMapSpace
{
	// Unreal units for one map pixel.
	constexpr double UnitsPerPixel = 20.0;

	// Heights of the board's layers, in map pixels above the sea.
	constexpr double ShallowsZ = 0.3;
	constexpr double LandZ = 1.0;
	constexpr double LakeZ = 1.3;
	constexpr double RiverZ = 1.5;
	constexpr double RouteZ = 1.8;

	inline FVector ToWorld(const FVector2D& Pixel, double HeightInPixels = 0.0)
	{
		return FVector(
			(PortsProjection::MapHeight() * 0.5 - Pixel.Y) * UnitsPerPixel,
			(Pixel.X - PortsProjection::MapWidth() * 0.5) * UnitsPerPixel,
			HeightInPixels * UnitsPerPixel);
	}

	inline FVector2D ToPixel(const FVector& World)
	{
		return FVector2D(World.Y / UnitsPerPixel + PortsProjection::MapWidth() * 0.5, PortsProjection::MapHeight() * 0.5 - World.X / UnitsPerPixel);
	}

	// The same for a point given as (x east, y south, height), all in map pixels.
	inline FVector ToWorld3(const FVector& P) { return ToWorld(FVector2D(P.X, P.Y), P.Z); }
	inline FVector ToPixel3(const FVector& World) { const FVector2D P = ToPixel(World); return FVector(P.X - PortsProjection::MapWidth() * 0.5, P.Y - PortsProjection::MapHeight() * 0.5, World.Z / UnitsPerPixel) + FVector(PortsProjection::MapWidth() * 0.5, PortsProjection::MapHeight() * 0.5, 0); }

	// A point of a model built round the middle of the map: x to the east, y to the south, z up, in map pixels.
	inline FVector Local3(double X, double Y, double Z) { return FVector(PortsProjection::MapWidth() * 0.5 + X, PortsProjection::MapHeight() * 0.5 + Y, Z); }

	// A city's place on the map, including the nudge the web map gives it (POS in map.js).
	inline FVector2D CityPixel(const FPortsCity& City)
	{
		return PortsProjection::Project(City.Lon, City.Lat) + FVector2D(City.MapDx, City.MapDy);
	}
}
