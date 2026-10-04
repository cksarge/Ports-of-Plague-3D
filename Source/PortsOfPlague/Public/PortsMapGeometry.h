// Shape helpers for the board: reading the coastline paths of map.json,
// curving routes the way the web map does, and building flat meshes.
#pragma once

#include "CoreMinimal.h"

class UProceduralMeshComponent;
class UMaterialInterface;

namespace PortsGeo
{
	// Splits an SVG path made only of M, L and Z commands (as in map.json) into its lines.
	void ParsePath(const FString& Path, TArray<TArray<FVector2D>>& OutLines);

	// The smooth curve the web map draws through a route's points (smoothPath in map.js),
	// as a line of short straight pieces.
	TArray<FVector2D> SmoothCurve(const TArray<FVector2D>& Points, int32 StepsPerSegment = 14);

	double Length(const TArray<FVector2D>& Line);
	FVector2D PointAtLength(const TArray<FVector2D>& Line, double Distance);
	// The part of a line between two distances along it.
	TArray<FVector2D> Slice(const TArray<FVector2D>& Line, double From, double To);

	double SignedArea(const TArray<FVector2D>& Ring);

	// Fills closed rings with triangles the way SVG does (a ring wound the other way
	// inside another cuts a hole). ExtraPoints are added inside the shapes so the
	// surface has points away from its edges. Triangles index into OutPoints.
	bool Triangulate(const TArray<TArray<FVector2D>>& Rings, const TArray<FVector2D>& ExtraPoints, TArray<FVector2D>& OutPoints, TArray<int32>& OutTriangles);

	// Collects triangles for one mesh section. Positions are in map pixels.
	struct FMeshBuilder
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FLinearColor> Colors;
		// Texture coordinates: left empty unless a picture is laid on the mesh.
		TArray<FVector2D> UVs;

		int32 AddVertex(const FVector2D& Pixel, double HeightInPixels, const FVector& Normal, const FLinearColor& Color);
		// Adds a triangle so that it faces along Facing.
		void AddTriangle(int32 A, int32 B, int32 C, const FVector& Facing);
		// A flat strip along a line, facing up.
		void AddRibbon(const TArray<FVector2D>& Line, double Width, double HeightInPixels, const FLinearColor& Color, bool bClosed = false);
		// A flat disc, facing up.
		void AddDisc(const FVector2D& Centre, double Radius, double HeightInPixels, const FLinearColor& Color, int32 Sides = 10);
		// A flat rectangle carrying a whole picture, facing up. Centre and size are in map pixels.
		void AddPicture(const FVector2D& Centre, const FVector2D& Size, double HeightInPixels);
		// A wall dropping from one height to another along a closed ring, facing outwards.
		void AddSkirt(const TArray<FVector2D>& Ring, double TopHeight, double BottomHeight, const FLinearColor& Color);

		// A flat strip laid over uneven ground: HeightAt gives the height under each point.
		void AddRibbonOn(const TArray<FVector2D>& Line, double Width, TFunctionRef<double(const FVector2D&)> HeightAt, const FLinearColor& Color);

		// Solid shapes for models. Points are (x east, y south, height), all in map pixels.
		int32 AddPoint(const FVector& Point, const FVector& Normal, const FLinearColor& Color);
		// A flat face through the corners given, facing away from Inside.
		void AddFace(const TArray<FVector>& Corners, const FVector& Inside, const FLinearColor& Color);
		// A box standing on Base (the middle of its floor), turned by Yaw degrees.
		void AddBox(const FVector& Base, const FVector& Size, double Yaw, const FLinearColor& Color);
		// A round tower, or a cone when TopRadius is 0: smooth all the way round.
		void AddRound(const FVector& Base, double Radius, double TopRadius, double Height, const FLinearColor& Color, int32 Sides = 10, bool bCap = true);
		// A house: four walls and a pitched roof whose ridge runs along its length (x before turning).
		void AddHouse(const FVector& Base, const FVector2D& Size, double WallHeight, double RoofHeight, double Yaw, const FLinearColor& Wall, const FLinearColor& Roof);

		// A round body following a line of points (x east, y south, height), as thick at each as its radius there.
		void AddTube(const TArray<FVector>& Points, const TArray<double>& Radii, const FLinearColor& Top, const FLinearColor& Belly, int32 Sides = 8);

		void Commit(UProceduralMeshComponent* Mesh, int32 Section, UMaterialInterface* Material) const;
	};

	// A direction given in map pixels (x east, y south, z up) as a direction in the world.
	inline FVector ToWorldDirection(const FVector& D) { return FVector(-D.Y, D.X, D.Z); }

	// A colour written as on the website, e.g. "#f4e8c8".
	FLinearColor Hex(const TCHAR* Code);
}
