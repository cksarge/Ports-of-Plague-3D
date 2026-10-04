// Map projection, the same as src/projection.js: an equirectangular
// projection centred on Europe, with longitude scaled by the cosine of 36°N.
// Results are in map pixels (x to the east, y to the south), the units used
// by data/map.json and by the web map.
#pragma once

#include "CoreMinimal.h"

namespace PortsProjection
{
	constexpr double West = -11.0;
	constexpr double East = 42.0;
	constexpr double North = 62.5;
	constexpr double South = 29.5;
	constexpr double Phi0 = 36.0;
	constexpr double Height = 896.0;

	inline double K() { return Height / (North - South); }
	inline double Cos0() { return FMath::Cos(FMath::DegreesToRadians(Phi0)); }

	inline double MapHeight() { return Height; }
	inline double MapWidth() { return FMath::RoundToDouble((East - West) * Cos0() * K()); }

	inline FVector2D Project(double Lon, double Lat)
	{
		return FVector2D((Lon - West) * Cos0() * K(), (North - Lat) * K());
	}
}
