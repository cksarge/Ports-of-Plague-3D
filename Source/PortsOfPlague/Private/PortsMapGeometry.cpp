#include "PortsMapGeometry.h"

#include "PortsMapSpace.h"
#include "CompGeom/Delaunay2.h"
#include "CompGeom/PolygonTriangulation.h"
#include "IndexTypes.h"
#include "ProceduralMeshComponent.h"

namespace PortsGeo
{
	void ParsePath(const FString& Path, TArray<TArray<FVector2D>>& OutLines)
	{
		OutLines.Reset();
		const TCHAR* Ch = *Path;
		TArray<FVector2D>* Current = nullptr;
		while (*Ch)
		{
			if (*Ch == TEXT('M'))
			{
				Current = &OutLines.AddDefaulted_GetRef();
				++Ch;
			}
			else if (*Ch == TEXT('L') || *Ch == TEXT('Z') || *Ch == TEXT(' '))
			{
				++Ch;
			}
			else
			{
				// A point: "x,y".
				const auto ReadNumber = [&Ch](double& Out)
				{
					const TCHAR* Start = Ch;
					while (*Ch == TEXT('-') || *Ch == TEXT('.') || (*Ch >= TEXT('0') && *Ch <= TEXT('9'))) ++Ch;
					if (Ch == Start) return false;
					Out = FCString::Atod(*FString::ConstructFromPtrSize(Start, UE_PTRDIFF_TO_INT32(Ch - Start)));
					return true;
				};
				double X = 0, Y = 0;
				if (!ReadNumber(X)) { ++Ch; continue; }
				if (*Ch != TEXT(',')) continue;
				++Ch;
				if (!ReadNumber(Y)) continue;
				if (Current) Current->Add(FVector2D(X, Y));
			}
		}
	}

	TArray<FVector2D> SmoothCurve(const TArray<FVector2D>& P, int32 Steps)
	{
		if (P.Num() <= 2) return P;
		TArray<FVector2D> Out;
		Out.Add(P[0]);
		for (int32 i = 0; i < P.Num() - 1; i++)
		{
			const FVector2D P0 = P[FMath::Max(i - 1, 0)];
			const FVector2D P1 = P[i];
			const FVector2D P2 = P[i + 1];
			const FVector2D P3 = P[FMath::Min(i + 2, P.Num() - 1)];
			const FVector2D C1 = P1 + (P2 - P0) / 6.0;
			const FVector2D C2 = P2 - (P3 - P1) / 6.0;
			for (int32 s = 1; s <= Steps; s++)
			{
				const double T = static_cast<double>(s) / Steps;
				const double U = 1.0 - T;
				Out.Add(P1 * (U * U * U) + C1 * (3 * U * U * T) + C2 * (3 * U * T * T) + P2 * (T * T * T));
			}
		}
		return Out;
	}

	double Length(const TArray<FVector2D>& Line)
	{
		double Total = 0;
		for (int32 i = 1; i < Line.Num(); i++) Total += FVector2D::Distance(Line[i - 1], Line[i]);
		return Total;
	}

	FVector2D PointAtLength(const TArray<FVector2D>& Line, double Distance)
	{
		if (Line.Num() == 0) return FVector2D::ZeroVector;
		double Walked = 0;
		for (int32 i = 1; i < Line.Num(); i++)
		{
			const double Step = FVector2D::Distance(Line[i - 1], Line[i]);
			if (Walked + Step >= Distance && Step > 0) return FMath::Lerp(Line[i - 1], Line[i], (Distance - Walked) / Step);
			Walked += Step;
		}
		return Line.Last();
	}

	TArray<FVector2D> Slice(const TArray<FVector2D>& Line, double From, double To)
	{
		TArray<FVector2D> Out;
		double Walked = 0;
		for (int32 i = 1; i < Line.Num(); i++)
		{
			const double Step = FVector2D::Distance(Line[i - 1], Line[i]);
			const double Next = Walked + Step;
			if (Step > 0 && Next >= From && Walked <= To)
			{
				if (Out.Num() == 0) Out.Add(FMath::Lerp(Line[i - 1], Line[i], FMath::Clamp((From - Walked) / Step, 0.0, 1.0)));
				Out.Add(FMath::Lerp(Line[i - 1], Line[i], FMath::Clamp((To - Walked) / Step, 0.0, 1.0)));
			}
			Walked = Next;
			if (Walked > To) break;
		}
		return Out;
	}

	double SignedArea(const TArray<FVector2D>& Ring)
	{
		double Area = 0;
		for (int32 i = 0; i < Ring.Num(); i++)
		{
			const FVector2D& A = Ring[i];
			const FVector2D& B = Ring[(i + 1) % Ring.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		return Area * 0.5;
	}

	bool Triangulate(const TArray<TArray<FVector2D>>& Rings, const TArray<FVector2D>& ExtraPoints, TArray<FVector2D>& OutPoints, TArray<int32>& OutTriangles)
	{
		using namespace UE::Geometry;
		OutPoints.Reset();
		OutTriangles.Reset();
		TArray<FIndex2i> Edges;
		for (const TArray<FVector2D>& Ring : Rings)
		{
			if (Ring.Num() < 3) continue;
			const int32 First = OutPoints.Num();
			OutPoints.Append(Ring);
			for (int32 i = 0; i < Ring.Num(); i++) Edges.Add(FIndex2i(First + i, First + (i + 1) % Ring.Num()));
		}
		OutPoints.Append(ExtraPoints);

		FDelaunay2 Delaunay;
		Delaunay.bAutomaticallyFixEdgesToDuplicateVertices = true;
		Delaunay.bValidateEdges = false;
		TArray<FIndex3i> Filled;
		if (Delaunay.Triangulate(TArrayView<const FVector2d>(OutPoints), TArrayView<const FIndex2i>(Edges)))
		{
			Delaunay.GetFilledTriangles(Filled, Edges, FDelaunay2::EFillMode::NonZeroWinding);
		}
		if (Filled.Num() > 0)
		{
			for (const FIndex3i& T : Filled) { OutTriangles.Add(T.A); OutTriangles.Add(T.B); OutTriangles.Add(T.C); }
			return true;
		}

		// Fallback: fill each ring by itself (no holes, no inner points).
		UE_LOG(LogTemp, Warning, TEXT("Ports map: Delaunay fill failed, filling each outline separately."));
		OutPoints.Reset();
		for (const TArray<FVector2D>& Ring : Rings)
		{
			if (Ring.Num() < 3) continue;
			TArray<FIndex3i> Tris;
			PolygonTriangulation::TriangulateSimplePolygon<double>(Ring, Tris, false);
			const int32 First = OutPoints.Num();
			OutPoints.Append(Ring);
			for (const FIndex3i& T : Tris) { OutTriangles.Add(First + T.A); OutTriangles.Add(First + T.B); OutTriangles.Add(First + T.C); }
		}
		return OutTriangles.Num() > 0;
	}

	int32 FMeshBuilder::AddVertex(const FVector2D& Pixel, double HeightInPixels, const FVector& Normal, const FLinearColor& Color)
	{
		Vertices.Add(PortsMapSpace::ToWorld(Pixel, HeightInPixels));
		Normals.Add(Normal);
		Colors.Add(Color);
		return Vertices.Num() - 1;
	}

	void FMeshBuilder::AddTriangle(int32 A, int32 B, int32 C, const FVector& Facing)
	{
		// Unreal draws the side of a triangle whose corners run clockwise.
		const FVector Cross = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]);
		if (FVector::DotProduct(Cross, Facing) > 0) Swap(B, C);
		Triangles.Add(A);
		Triangles.Add(B);
		Triangles.Add(C);
	}

	void FMeshBuilder::AddRibbon(const TArray<FVector2D>& Line, double Width, double HeightInPixels, const FLinearColor& Color, bool bClosed)
	{
		const int32 N = Line.Num();
		if (N < 2) return;
		const auto SegmentNormal = [&](int32 i) -> FVector2D
		{
			const FVector2D D = (Line[(i + 1) % N] - Line[i]).GetSafeNormal();
			return FVector2D(-D.Y, D.X);
		};
		const int32 Segments = bClosed ? N : N - 1;
		const int32 First = Vertices.Num();
		for (int32 i = 0; i < N; i++)
		{
			const bool bHasPrev = bClosed || i > 0;
			const bool bHasNext = bClosed || i < N - 1;
			const FVector2D Prev = bHasPrev ? SegmentNormal((i + N - 1) % N) : SegmentNormal(i);
			const FVector2D Next = bHasNext ? SegmentNormal(i) : Prev;
			FVector2D Mid = (Prev + Next).GetSafeNormal();
			if (Mid.IsNearlyZero()) Mid = Next;
			// Corners keep the strip's width, without long spikes at sharp turns.
			const double Miter = FMath::Min(2.0, 1.0 / FMath::Max(0.2, FVector2D::DotProduct(Mid, Next)));
			const FVector2D Offset = Mid * (Width * 0.5 * Miter);
			AddVertex(Line[i] + Offset, HeightInPixels, FVector::UpVector, Color);
			AddVertex(Line[i] - Offset, HeightInPixels, FVector::UpVector, Color);
		}
		for (int32 i = 0; i < Segments; i++)
		{
			const int32 A = First + i * 2;
			const int32 B = First + ((i + 1) % N) * 2;
			AddTriangle(A, A + 1, B, FVector::UpVector);
			AddTriangle(A + 1, B + 1, B, FVector::UpVector);
		}
	}

	void FMeshBuilder::AddDisc(const FVector2D& Centre, double Radius, double HeightInPixels, const FLinearColor& Color, int32 Sides)
	{
		const int32 Middle = AddVertex(Centre, HeightInPixels, FVector::UpVector, Color);
		for (int32 i = 0; i < Sides; i++)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * i / Sides;
			AddVertex(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius, HeightInPixels, FVector::UpVector, Color);
		}
		for (int32 i = 0; i < Sides; i++) AddTriangle(Middle, Middle + 1 + i, Middle + 1 + (i + 1) % Sides, FVector::UpVector);
	}

	void FMeshBuilder::AddPicture(const FVector2D& Centre, const FVector2D& Size, double HeightInPixels)
	{
		const FVector2D Half = Size * 0.5;
		const int32 First = AddVertex(Centre + FVector2D(-Half.X, -Half.Y), HeightInPixels, FVector::UpVector, FLinearColor::White);
		AddVertex(Centre + FVector2D(Half.X, -Half.Y), HeightInPixels, FVector::UpVector, FLinearColor::White);
		AddVertex(Centre + FVector2D(Half.X, Half.Y), HeightInPixels, FVector::UpVector, FLinearColor::White);
		AddVertex(Centre + FVector2D(-Half.X, Half.Y), HeightInPixels, FVector::UpVector, FLinearColor::White);
		UVs.SetNumZeroed(First);
		UVs.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
		AddTriangle(First, First + 1, First + 2, FVector::UpVector);
		AddTriangle(First, First + 2, First + 3, FVector::UpVector);
	}

	void FMeshBuilder::AddSkirt(const TArray<FVector2D>& Ring, double TopHeight, double BottomHeight, const FLinearColor& Color)
	{
		const int32 N = Ring.Num();
		if (N < 3) return;
		for (int32 i = 0; i < N; i++)
		{
			const FVector2D& A = Ring[i];
			const FVector2D& B = Ring[(i + 1) % N];
			const FVector2D D = (B - A).GetSafeNormal();
			// The filled side is on the left of every ring (holes run the other way round),
			// so the right is always outwards. Then into world axes: pixel x is world Y, pixel y is world -X.
			const FVector2D Out = FVector2D(D.Y, -D.X);
			const FVector Normal(-Out.Y, Out.X, 0);
			const int32 V = AddVertex(A, TopHeight, Normal, Color);
			AddVertex(B, TopHeight, Normal, Color);
			AddVertex(A, BottomHeight, Normal, Color);
			AddVertex(B, BottomHeight, Normal, Color);
			AddTriangle(V, V + 1, V + 2, Normal);
			AddTriangle(V + 1, V + 3, V + 2, Normal);
		}
	}

	void FMeshBuilder::AddRibbonOn(const TArray<FVector2D>& Line, double Width, TFunctionRef<double(const FVector2D&)> HeightAt, const FLinearColor& Color)
	{
		if (Line.Num() < 2) return;
		// Short steps, so the strip follows the ground between the line's own points.
		const double Total = Length(Line);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Total / 2.5));
		const int32 First = Vertices.Num();
		for (int32 i = 0; i <= Steps; i++)
		{
			const double At = Total * i / Steps;
			const FVector2D P = PointAtLength(Line, At);
			const FVector2D Ahead = PointAtLength(Line, FMath::Min(Total, At + 0.5)) - PointAtLength(Line, FMath::Max(0.0, At - 0.5));
			const FVector2D Side = FVector2D(-Ahead.Y, Ahead.X).GetSafeNormal() * (Width * 0.5);
			AddVertex(P + Side, HeightAt(P + Side), FVector::UpVector, Color);
			AddVertex(P - Side, HeightAt(P - Side), FVector::UpVector, Color);
		}
		for (int32 i = 0; i < Steps; i++)
		{
			const int32 A = First + i * 2;
			AddTriangle(A, A + 1, A + 2, FVector::UpVector);
			AddTriangle(A + 1, A + 3, A + 2, FVector::UpVector);
		}
	}

	namespace
	{
		// A direction in map pixels (x east, y south, z up) as a direction in the world (north +X, east +Y).
		FVector PixelToWorldDirection(const FVector& D) { return FVector(-D.Y, D.X, D.Z); }
		FVector Turn(const FVector& P, double Yaw)
		{
			const double C = FMath::Cos(FMath::DegreesToRadians(Yaw)), S = FMath::Sin(FMath::DegreesToRadians(Yaw));
			return FVector(P.X * C - P.Y * S, P.X * S + P.Y * C, P.Z);
		}
	}

	int32 FMeshBuilder::AddPoint(const FVector& Point, const FVector& Normal, const FLinearColor& Color)
	{
		return AddVertex(FVector2D(Point.X, Point.Y), Point.Z, PixelToWorldDirection(Normal).GetSafeNormal(), Color);
	}

	void FMeshBuilder::AddFace(const TArray<FVector>& Corners, const FVector& Inside, const FLinearColor& Color)
	{
		if (Corners.Num() < 3) return;
		FVector Normal = FVector::CrossProduct(Corners[1] - Corners[0], Corners[2] - Corners[0]).GetSafeNormal();
		if (FVector::DotProduct(Normal, Corners[0] - Inside) < 0) Normal = -Normal;
		const int32 First = Vertices.Num();
		for (const FVector& Corner : Corners) AddPoint(Corner, Normal, Color);
		const FVector Facing = PixelToWorldDirection(Normal);
		for (int32 i = 1; i + 1 < Corners.Num(); i++) AddTriangle(First, First + i, First + i + 1, Facing);
	}

	void FMeshBuilder::AddBox(const FVector& Base, const FVector& Size, double Yaw, const FLinearColor& Color)
	{
		const FVector H(Size.X * 0.5, Size.Y * 0.5, Size.Z);
		const auto C = [&](double X, double Y, double Z) { return Base + Turn(FVector(X * H.X, Y * H.Y, Z * H.Z), Yaw); };
		const FVector Inside = Base + FVector(0, 0, H.Z * 0.5);
		AddFace({ C(-1, -1, 1), C(1, -1, 1), C(1, 1, 1), C(-1, 1, 1) }, Inside, Color);
		AddFace({ C(-1, -1, 0), C(1, -1, 0), C(1, -1, 1), C(-1, -1, 1) }, Inside, Color);
		AddFace({ C(-1, 1, 0), C(1, 1, 0), C(1, 1, 1), C(-1, 1, 1) }, Inside, Color);
		AddFace({ C(-1, -1, 0), C(-1, 1, 0), C(-1, 1, 1), C(-1, -1, 1) }, Inside, Color);
		AddFace({ C(1, -1, 0), C(1, 1, 0), C(1, 1, 1), C(1, -1, 1) }, Inside, Color);
	}

	void FMeshBuilder::AddRound(const FVector& Base, double Radius, double TopRadius, double Height, const FLinearColor& Color, int32 Sides, bool bCap)
	{
		const int32 First = Vertices.Num();
		// The wall leans by this much, which tilts its facing upwards.
		const double Lean = (Radius - TopRadius) / FMath::Max(0.001, Height);
		for (int32 i = 0; i <= Sides; i++)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * i / Sides;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0);
			const FVector Normal = (Out + FVector(0, 0, Lean)).GetSafeNormal();
			AddPoint(Base + Out * Radius, Normal, Color);
			AddPoint(Base + Out * TopRadius + FVector(0, 0, Height), Normal, Color);
		}
		for (int32 i = 0; i < Sides; i++)
		{
			const int32 A = First + i * 2;
			const FVector Facing = Normals[A];
			AddTriangle(A, A + 1, A + 2, Facing);
			if (TopRadius > 0.001) AddTriangle(A + 1, A + 3, A + 2, Facing);
		}
		if (bCap && TopRadius > 0.001)
		{
			TArray<FVector> Top;
			for (int32 i = 0; i < Sides; i++)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * i / Sides;
				Top.Add(Base + FVector(FMath::Cos(Angle) * TopRadius, FMath::Sin(Angle) * TopRadius, Height));
			}
			AddFace(Top, Base, Color);
		}
	}

	void FMeshBuilder::AddHouse(const FVector& Base, const FVector2D& Size, double WallHeight, double RoofHeight, double Yaw, const FLinearColor& Wall, const FLinearColor& Roof)
	{
		const double X = Size.X * 0.5, Y = Size.Y * 0.5;
		const auto C = [&](double Px, double Py, double Pz) { return Base + Turn(FVector(Px, Py, Pz), Yaw); };
		const FVector Inside = Base + FVector(0, 0, WallHeight * 0.5);
		const double Top = WallHeight + RoofHeight;
		AddFace({ C(-X, -Y, 0), C(X, -Y, 0), C(X, -Y, WallHeight), C(-X, -Y, WallHeight) }, Inside, Wall);
		AddFace({ C(-X, Y, 0), C(X, Y, 0), C(X, Y, WallHeight), C(-X, Y, WallHeight) }, Inside, Wall);
		// The end walls run up into the gable.
		AddFace({ C(-X, -Y, 0), C(-X, Y, 0), C(-X, Y, WallHeight), C(-X, 0, Top), C(-X, -Y, WallHeight) }, Inside, Wall);
		AddFace({ C(X, -Y, 0), C(X, Y, 0), C(X, Y, WallHeight), C(X, 0, Top), C(X, -Y, WallHeight) }, Inside, Wall);
		// The roof overhangs the walls a little.
		const double E = 0.12 * Size.Y, L = X + 0.06 * Size.X;
		const double Drop = RoofHeight * E / FMath::Max(0.001, Y);
		AddFace({ C(-L, -Y - E, WallHeight - Drop), C(L, -Y - E, WallHeight - Drop), C(L, 0, Top), C(-L, 0, Top) }, Inside, Roof);
		AddFace({ C(-L, Y + E, WallHeight - Drop), C(L, Y + E, WallHeight - Drop), C(L, 0, Top), C(-L, 0, Top) }, Inside, Roof);
	}

	void FMeshBuilder::AddTube(const TArray<FVector>& Points, const TArray<double>& Radii, const FLinearColor& Top, const FLinearColor& Belly, int32 Sides)
	{
		const int32 N = Points.Num();
		if (N < 2 || Radii.Num() != N) return;
		const int32 First = Vertices.Num();
		for (int32 i = 0; i < N; i++)
		{
			const FVector Along = (Points[FMath::Min(i + 1, N - 1)] - Points[FMath::Max(i - 1, 0)]).GetSafeNormal();
			FVector Side = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();
			if (Side.IsNearlyZero()) Side = FVector(1, 0, 0);
			const FVector Over = FVector::CrossProduct(Side, Along).GetSafeNormal();
			for (int32 k = 0; k <= Sides; k++)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * k / Sides;
				const FVector Out = Side * FMath::Cos(Angle) + Over * FMath::Sin(Angle);
				// Paler underneath, as sea creatures are.
				AddPoint(Points[i] + Out * Radii[i], Out, FMath::Lerp(Belly, Top, static_cast<float>(FMath::Clamp(Out.Z * 0.5 + 0.6, 0.0, 1.0))));
			}
		}
		for (int32 i = 0; i + 1 < N; i++) for (int32 k = 0; k < Sides; k++)
		{
			const int32 A = First + i * (Sides + 1) + k, B = A + Sides + 1;
			AddTriangle(A, A + 1, B, Normals[A]);
			AddTriangle(A + 1, B + 1, B, Normals[A + 1]);
		}
	}

	void FMeshBuilder::Commit(UProceduralMeshComponent* Mesh, int32 Section, UMaterialInterface* Material) const
	{
		Mesh->CreateMeshSection_LinearColor(Section, Vertices, Triangles, Normals, UVs.Num() == Vertices.Num() ? UVs : TArray<FVector2D>(), Colors, TArray<FProcMeshTangent>(), false, false);
		if (Material) Mesh->SetMaterial(Section, Material);
	}

	FLinearColor Hex(const TCHAR* Code)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Code));
	}
}
