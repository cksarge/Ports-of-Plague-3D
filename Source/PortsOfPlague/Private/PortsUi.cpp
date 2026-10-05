#include "PortsUi.h"

#include "PortsData.h"
#include "PortsState.h"
#include "Brushes/SlateImageBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SLeafWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/CompositeFont.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Textures/SlateShaderResource.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	TSharedPtr<FSlateStyleSet> StyleSet;
	TSharedPtr<FStandaloneCompositeFont> SerifFont, CapsFont, BlackFont;

	FString UiDir() { return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("UI")); }

	FLinearColor Hex(const TCHAR* Code, float Alpha = 1.f)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor::FromHex(Code));
		C.A = Alpha;
		return C;
	}

	// Mixes a colour with black, as the stylesheet's color-mix() does (in sRGB).
	FLinearColor Darken(const FLinearColor& Color, float Keep)
	{
		const FColor S = Color.ToFColorSRGB();
		return FLinearColor::FromSRGBColor(FColor(static_cast<uint8>(S.R * Keep), static_cast<uint8>(S.G * Keep), static_cast<uint8>(S.B * Keep), S.A));
	}

	FLinearColor Brighten(const FLinearColor& Color, float By)
	{
		return FLinearColor(FMath::Min(1.f, Color.R * By), FMath::Min(1.f, Color.G * By), FMath::Min(1.f, Color.B * By), Color.A);
	}

	// Paints a box look. Returns the next free layer.
	int32 PaintLook(const FPortsBoxLook& L, const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, float Bright = 1.f)
	{
		const FVector2f Size = G.GetLocalSize();
		if (Size.X <= 0 || Size.Y <= 0) return Layer;
		const auto Part = [&G](const FVector2f& Offset, const FVector2f& PartSize) { return G.ToPaintGeometry(PartSize, FSlateLayoutTransform(Offset)); };
		const FVector4f Corners = L.bTopOnly ? FVector4f(L.Radius, L.Radius, 0, 0) : FVector4f(L.Radius, L.Radius, L.Radius, L.Radius);
		const FVector4 CornersD(Corners.X, Corners.Y, Corners.Z, Corners.W);

		if (L.SoftShadow > 0)
		{
			for (int32 i = 1; i <= 4; i++)
			{
				const float Grow = i * 5.f;
				const FSlateRoundedBoxBrush Blur(FLinearColor(0.012f, 0.006f, 0.f, L.SoftShadow * 0.13f), L.Radius + Grow);
				FSlateDrawElement::MakeBox(Out, Layer, Part(FVector2f(-Grow, -Grow + 6.f), Size + FVector2f(Grow * 2, Grow * 2)), &Blur, ESlateDrawEffect::None, Blur.TintColor.GetSpecifiedColor());
			}
		}
		if (L.ShadowDrop > 0 && L.Shadow.A > 0)
		{
			const FSlateRoundedBoxBrush Under(L.Shadow, CornersD);
			FSlateDrawElement::MakeBox(Out, Layer, Part(FVector2f(0, L.ShadowDrop), Size), &Under, ESlateDrawEffect::None, L.Shadow);
		}
		if (L.Top.A > 0 || L.Bottom.A > 0)
		{
			const float Extent = L.bSideways ? Size.X : Size.Y;
			const auto At = [&L](float D) { return L.bSideways ? FVector2f(D, 0) : FVector2f(0, D); };
			TArray<FSlateGradientStop> Stops;
			Stops.Add(FSlateGradientStop(At(0), Brighten(L.Top, Bright)));
			if (L.MidAt >= 0) Stops.Add(FSlateGradientStop(At(Extent * L.MidAt), Brighten(L.Mid, Bright)));
			Stops.Add(FSlateGradientStop(At(Extent), Brighten(L.Bottom, Bright)));
			// "Vertical" stops are vertical lines, so the colour changes from left to right.
			FSlateDrawElement::MakeGradient(Out, Layer + 1, G.ToPaintGeometry(), Stops, L.bSideways ? Orient_Vertical : Orient_Horizontal, ESlateDrawEffect::None, Corners);
		}
		if (L.TopBarHeight > 0)
		{
			FSlateDrawElement::MakeGradient(Out, Layer + 2, Part(FVector2f(0, 0), FVector2f(Size.X, L.TopBarHeight)),
				{ FSlateGradientStop(FVector2f(0, 0), L.TopBar), FSlateGradientStop(FVector2f(0, L.TopBarHeight), L.TopBar) }, Orient_Horizontal, ESlateDrawEffect::None, FVector4f(L.Radius, L.Radius, 0, 0));
		}
		if (L.LeftBarWidth > 0)
		{
			FSlateDrawElement::MakeGradient(Out, Layer + 2, Part(FVector2f(0, 0), FVector2f(L.LeftBarWidth, Size.Y)),
				{ FSlateGradientStop(FVector2f(0, 0), L.LeftBar), FSlateGradientStop(FVector2f(0, Size.Y), L.LeftBar) }, Orient_Horizontal, ESlateDrawEffect::None, FVector4f(L.Radius, 0, 0, L.Radius));
		}
		if (L.BorderWidth > 0)
		{
			const FSlateRoundedBoxBrush Edge(FLinearColor::Transparent, CornersD, L.Border, L.BorderWidth);
			FSlateDrawElement::MakeBox(Out, Layer + 3, G.ToPaintGeometry(), &Edge, ESlateDrawEffect::None, FLinearColor::Transparent);
		}
		if (L.InsetWidth > 0)
		{
			const FSlateRoundedBoxBrush Line(FLinearColor::Transparent, FMath::Max(1.f, L.Radius - L.InsetAt), L.Inset, L.InsetWidth);
			FSlateDrawElement::MakeBox(Out, Layer + 3, Part(FVector2f(L.InsetAt, L.InsetAt), Size - FVector2f(L.InsetAt * 2, L.InsetAt * 2)), &Line, ESlateDrawEffect::None, FLinearColor::Transparent);
		}
		return Layer + 4;
	}

	FTextBlockStyle Text(const FSlateFontInfo& Font, const TCHAR* Color, int32 Spacing = 0)
	{
		FSlateFontInfo F = Font;
		F.LetterSpacing = Spacing;
		return FTextBlockStyle().SetFont(F).SetColorAndOpacity(FSlateColor(Hex(Color)));
	}
}

// ---------- The shimmering title ----------
// .title in game.css: crimson letters with a band of gold that crosses them from
// right to left every seven seconds (it rests for a moment at each end).
class SPortsShimmer : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsShimmer) {}
		SLATE_ARGUMENT(FString, Text)
		SLATE_ARGUMENT(FSlateFontInfo, Font)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Text = Args._Text;
		Font = Args._Font;
		SetVisibility(EVisibility::HitTestInvisible);
		// Keeps the title repainting while it is on screen.
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([](double, float) { return EActiveTimerReturnType::Continue; }));
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font)) + FVector2D(0, 4);
	}

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		const float Scale = FMath::Max(0.1f, G.Scale);
		const FVector2f Size(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font, Scale) / Scale);
		const float Left = (G.GetLocalSize().X - Size.X) * 0.5f;
		const auto Draw = [&](int32 L, const FVector2f& Offset, const FLinearColor& Tint)
		{
			FSlateDrawElement::MakeText(Out, L, G.ToPaintGeometry(Size, FSlateLayoutTransform(FVector2f(Left, 0) + Offset)), Text, Font, ESlateDrawEffect::None, Tint);
		};
		const FLinearColor Crimson = Hex(TEXT("#9a1d12")), Flame = Hex(TEXT("#e0573f")), Gold = Hex(TEXT("#f3d27a"));
		Draw(Layer, FVector2f(0, 2), FLinearColor(0.1f, 0.02f, 0.f, 0.25f));
		Draw(Layer + 1, FVector2f(0, 0), Crimson);

		// The gradient is one tile twice as wide as the title: the gold sits in its middle and the tile slides one whole width.
		const double Cycle = FMath::Fmod(FSlateApplication::Get().GetCurrentTime(), 7.0) / 7.0;
		const double Linear = FMath::Clamp((Cycle - 0.15) / 0.55, 0.0, 1.0);
		const double Eased = Linear < 0.5 ? 2 * Linear * Linear : 1 - FMath::Pow(-2 * Linear + 2, 2.0) / 2;
		const float W = Size.X;
		const int32 Slices = 32;
		for (int32 Tile = 0; Tile < 2; Tile++)
		{
			const float Centre = W * (1 + 2 * Tile) - 2 * W * static_cast<float>(Eased);
			const float Half = 0.24f * W;
			for (int32 i = 0; i < Slices; i++)
			{
				const float X0 = Centre - Half + (2 * Half) * i / Slices;
				const float X1 = X0 + (2 * Half) / Slices;
				const float A = FMath::Max(0.f, X0), B = FMath::Min(W, X1);
				if (B <= A) continue;
				const float Away = FMath::Abs((X0 + X1) * 0.5f - Centre) / (2 * W);
				const FLinearColor Tint = Away < 0.05f ? FMath::Lerp(Gold, Flame, Away / 0.05f) : FMath::Lerp(Flame, Crimson, FMath::Clamp((Away - 0.05f) / 0.07f, 0.f, 1.f));
				Out.PushClip(FSlateClippingZone(G.MakeChild(FVector2f(B - A, Size.Y + 6), FSlateLayoutTransform(FVector2f(Left + A, 0)))));
				Draw(Layer + 2, FVector2f(0, 0), Tint);
				Out.PopClip();
			}
		}
		return Layer + 2;
	}

private:
	FString Text;
	FSlateFontInfo Font;
};

// ---------- The drop cap ----------
// .drop-cap in game.css: a large red initial at the left, with the first lines of
// the paragraph running beside it and the rest underneath at full width.
class SPortsDropCap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsDropCap) {}
		SLATE_ARGUMENT(FString, Text)
		// How wide the paragraph will be, when the page knows; it then has its right height from the first frame.
		SLATE_ARGUMENT(float, Width)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		if (Args._Width > 0.f) Width = Args._Width;
		Initial = Args._Text.Left(1);
		Args._Text.Mid(1).ParseIntoArray(Words, TEXT(" "), true);
		CapFont = PortsUi::Caps(52.f, true);
		BodyFont = PortsUi::Serif(18.f);
		SetVisibility(EVisibility::HitTestInvisible);
	}

	virtual FVector2D ComputeDesiredSize(float LayoutScale) const override
	{
		// Words are measured at the size they are drawn at, so the lines counted here are the lines painted.
		if (LayoutScale > 0.f) Scale = FMath::Max(0.1f, LayoutScale);
		return FVector2D(200, Layout(Width).Num() * LineHeight + 4);
	}

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		// The height depends on the width, which is only known once the page is laid out.
		const float Now = G.GetLocalSize().X;
		if (!FMath::IsNearlyEqual(Now, Width, 0.5f))
		{
			Width = Now;
			const_cast<SPortsDropCap*>(this)->Invalidate(EInvalidateWidgetReason::Layout);
		}
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2f CapSize(Measure->Measure(Initial, CapFont));
		const auto Draw = [&](const FString& String, const FSlateFontInfo& Font, const FVector2f& At, const FLinearColor& Tint)
		{
			FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2f(Measure->Measure(String, Font)), FSlateLayoutTransform(At)), String, Font, ESlateDrawEffect::None, Tint);
		};
		const float CapTop = (2 * LineHeight - CapSize.Y) * 0.5f - 1.f;
		Draw(Initial, CapFont, FVector2f(1, CapTop + 1), Hex(TEXT("#f3d27a")));
		Draw(Initial, CapFont, FVector2f(0, CapTop), Hex(TEXT("#b0261a")));
		if (!FMath::IsNearlyEqual(Scale, FMath::Max(0.1f, G.Scale), 0.001f))
		{
			Scale = FMath::Max(0.1f, G.Scale);
			const_cast<SPortsDropCap*>(this)->Invalidate(EInvalidateWidgetReason::Layout);
		}
		const TArray<FString> Lines = Layout(Now);
		const float Indent = CapSize.X + 7.f;
		for (int32 i = 0; i < Lines.Num(); i++) Draw(Lines[i], BodyFont, FVector2f(i < 2 ? Indent : 0.f, i * LineHeight), Hex(TEXT("#26190a")));
		return Layer;
	}

private:
	// Breaks the paragraph into lines: the first two are shorter, to leave room for the initial.
	TArray<FString> Layout(float ForWidth) const
	{
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const float Indent = Measure->Measure(Initial, CapFont).X + 7.f;
		TArray<FString> Lines;
		FString Line;
		for (const FString& Word : Words)
		{
			const FString Longer = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
			const float Room = ForWidth - (Lines.Num() < 2 ? Indent : 0.f);
			if (!Line.IsEmpty() && Measure->Measure(Longer, BodyFont, Scale).X / Scale > Room) { Lines.Add(Line); Line = Word; }
			else Line = Longer;
		}
		if (!Line.IsEmpty()) Lines.Add(Line);
		return Lines;
	}

	FString Initial;
	TArray<FString> Words;
	FSlateFontInfo CapFont;
	FSlateFontInfo BodyFont;
	mutable float Width = 640.f;
	mutable float Scale = 1.f;
	static constexpr float LineHeight = 26.f;
};

// ---------- Dice ----------
// A real cube with six pipped faces, thrown onto the felt: it tumbles in from the side, bounces twice and
// settles on the number the rules engine rolled, with the timing and flight of the web version's dice
// (dice.js). The cube is turned and seen in perspective here, face by face, and lit from the upper left.
namespace
{
	TFunction<void()> GClickSound, GDiceSound;
	float GDiceBase = 0.45f;
	int32 GDiceCount = 0;
	bool GDiceStill = false;
}

class SPortsDie : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsDie) : _Value(1), _Kind(TEXT("ivory")), _Size(62.f), _Delay(0.f) {}
		SLATE_ARGUMENT(int32, Value)
		SLATE_ARGUMENT(FString, Kind)
		SLATE_ARGUMENT(float, Size)
		// Seconds after it appears that the die is thrown; below zero it simply lies there.
		SLATE_ARGUMENT(float, Delay)
		// The first die of a throw makes the sound for all of them.
		SLATE_ARGUMENT(bool, Sounds)
	SLATE_END_ARGS()

	static constexpr float RollSeconds = 1.2f;

	void Construct(const FArguments& Args)
	{
		Value = Args._Value;
		Size = Args._Size;
		for (int32 n = 1; n <= 6; n++) Faces[n - 1] = PortsUi::PictureBrush(FString::Printf(TEXT("die_%s_%d"), *Args._Kind, n));
		Shadow = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::Black, Size * 0.24f);
		FRandomStream Random(FMath::Rand());
		Rest = Random.FRandRange(-14.f, 14.f);
		const auto Spins = [&Random]() { return (Random.FRand() < 0.5f ? -1.f : 1.f) * 360.f * (1 + Random.RandRange(0, 1)) + Random.FRandRange(-90.f, 90.f); };
		SpinX = Spins();
		SpinY = Spins();
		SpinZ = Random.FRandRange(-180.f, 180.f);
		Skid = Random.FRandRange(-10.f, 10.f);
		const float Angle = Random.FRandRange(0.f, 2.f * UE_PI);
		From = FVector2f(FMath::Cos(Angle) * Random.FRandRange(70.f, 100.f), FMath::Sin(Angle) * Random.FRandRange(25.f, 40.f));
		Duration = RollSeconds * Random.FRandRange(0.9f, 1.f);
		bStill = Args._Delay < 0.f;
		Start = FSlateApplication::Get().GetCurrentTime() + FMath::Max(0.f, Args._Delay);
		SetVisibility(EVisibility::HitTestInvisible);
		bSounds = Args._Sounds;
		if (!bStill) RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double Now, float)
		{
			if (bSounds && Now >= Start) { bSounds = false; if (GDiceSound) GDiceSound(); }
			return Now > Start + Duration + 0.1 ? EActiveTimerReturnType::Stop : EActiveTimerReturnType::Continue;
		}));
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		const double Now = FSlateApplication::Get().GetCurrentTime();
		const float T = bStill ? 1.f : static_cast<float>(FMath::Clamp((Now - Start) / Duration, 0.0, 1.0));
		if (!bStill && Now < Start) return Layer;

		// The flight: [time, share of the way still to go, height], a drop, two shrinking bounces, then rest.
		static const float Flight[6][3] = { { 0, 1, 150 }, { 0.36f, 0.32f, 0 }, { 0.58f, 0.14f, 38 }, { 0.76f, 0.05f, 0 }, { 0.88f, 0.015f, 9 }, { 1, 0, 0 } };
		float Left = 0, Height = 0;
		for (int32 i = 0; i < 5; i++)
		{
			if (T > Flight[i + 1][0] && i < 4) continue;
			const float U = FMath::Clamp((T - Flight[i][0]) / (Flight[i + 1][0] - Flight[i][0]), 0.f, 1.f);
			// Falling speeds up; rising slows down.
			const bool bFalling = Flight[i][2] > Flight[i + 1][2];
			const float E = bFalling ? U * U : 1.f - (1.f - U) * (1.f - U);
			Left = FMath::Lerp(Flight[i][1], Flight[i + 1][1], bFalling ? U : E);
			Height = FMath::Lerp(Flight[i][2], Flight[i + 1][2], E);
			break;
		}
		const float Scale = Size / 62.f;
		const FVector2f Offset = From * Left * Scale;
		Height *= Scale;

		// The tumble stops at the second landing; after that the die only skids round a little.
		const float Settle = 0.76f;
		FQuat Turn;
		if (T < Settle)
		{
			const float U = T / Settle;
			const float Spin = FMath::Pow(1.f - U, 1.7f);
			Turn = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(SpinX * Spin)) * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(SpinY * Spin))
				* FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Rest + Skid + SpinZ * Spin));
		}
		else
		{
			const float U = (T - Settle) / (1.f - Settle);
			Turn = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Rest + Skid * (1.f - U) * (1.f - U)));
		}

		// Its shadow stays on the felt: further off and fainter while the die is high.
		const float Fade = bStill ? 1.f : FMath::Clamp(T / 0.12f, 0.f, 1.f);
		{
			const float Grow = 1.f + Height / 300.f;
			const FVector2f ShadowSize(Size * 0.94f * Grow, Size * 0.94f * Grow);
			const FVector2f At = FVector2f(Size, Size) * 0.5f + Offset + FVector2f(Height * 0.25f + 2.5f * Scale, Height * 0.35f + 4.f * Scale) - ShadowSize * 0.5f;
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(ShadowSize, FSlateLayoutTransform(At)), Shadow.Get(), ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.34f * Fade * FMath::Clamp(1.f - Height / 200.f, 0.f, 1.f)));
		}

		// Where each number sits on the cube (opposite faces add up to seven), as a direction out of its middle.
		static const FVector Normals[6] = { FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0), FVector(-1, 0, 0), FVector(0, 0, -1) };
		// Turned so that the rolled number faces the viewer when the die lies still.
		const FQuat Base = Value == 6 ? FQuat(FVector::XAxisVector, UE_PI) : FQuat::FindBetweenNormals(Normals[Value - 1], FVector::ZAxisVector);
		const FQuat Pose = Turn * Base;
		const float Half = Size * 0.5f;
		const float Eye = 620.f * Scale;
		const FVector2f Middle = FVector2f(Size, Size) * 0.5f + Offset;
		const FVector Light = FVector(-0.32, -0.42, 0.85).GetSafeNormal();
		const FSlateRenderTransform& ToScreen = G.GetAccumulatedRenderTransform();
		const auto Project = [&](const FVector& P)
		{
			const float Near = Eye / FMath::Max(60.f, Eye - static_cast<float>(P.Z) - Height);
			return Middle + FVector2f(static_cast<float>(P.X), static_cast<float>(P.Y)) * Near;
		};
		// Each face is drawn as a grid of small pieces, so its picture stays true when the face is seen at a slant.
		const auto DrawFace = [&](int32 Number, float Shrink, bool bPicture, int32 OnLayer)
		{
			const FVector N = Pose.RotateVector(Normals[Number - 1]);
			if (N.Z <= 0.01) return;
			const FVector A = FMath::Abs(Normals[Number - 1].Z) > 0.5 ? FVector(1, 0, 0) : FVector(0, 0, 1);
			const FVector U = Pose.RotateVector(FVector::CrossProduct(Normals[Number - 1], A).GetSafeNormal());
			const FVector Vv = FVector::CrossProduct(N, U);
			const FSlateBrush* Brush = Faces[Number - 1];
			if (!Brush) return;
			const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*Brush);
			const FSlateShaderResourceProxy* Proxy = Handle.GetResourceProxy();
			const FVector2f UvStart = Proxy ? FVector2f(Proxy->StartUV) : FVector2f::ZeroVector, UvSize = Proxy ? FVector2f(Proxy->SizeUV) : FVector2f(1, 1);
			const float Lit = FMath::Clamp(0.5f + 0.56f * static_cast<float>(FMath::Max(0.0, FVector::DotProduct(N, Light))), 0.f, 1.f) * (bPicture ? 1.f : 0.8f);
			const FColor Tint = FLinearColor(Lit, Lit, Lit, Fade).ToFColor(false);
			const int32 Cells = 4;
			TArray<FSlateVertex> Vertices;
			TArray<SlateIndex> Indices;
			for (int32 y = 0; y <= Cells; y++) for (int32 x = 0; x <= Cells; x++)
			{
				const float Fx = static_cast<float>(x) / Cells, Fy = static_cast<float>(y) / Cells;
				const FVector Point = (N + U * (Fx * 2 - 1) + Vv * (Fy * 2 - 1)) * Half * Shrink;
				// The pictures are 64 wide with the face in the middle 60; a plain face takes its colour from a spot no pip ever covers.
				const FVector2f Uv = bPicture ? FVector2f(2.f / 64.f + Fx * 60.f / 64.f, 2.f / 64.f + Fy * 60.f / 64.f) : FVector2f(0.5f, 0.2f);
				Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(ToScreen, Project(Point), UvStart + Uv * UvSize, Tint));
			}
			for (int32 y = 0; y < Cells; y++) for (int32 x = 0; x < Cells; x++)
			{
				const int32 I = y * (Cells + 1) + x;
				Indices.Append({ static_cast<SlateIndex>(I), static_cast<SlateIndex>(I + 1), static_cast<SlateIndex>(I + Cells + 1), static_cast<SlateIndex>(I + 1), static_cast<SlateIndex>(I + Cells + 2), static_cast<SlateIndex>(I + Cells + 1) });
			}
			FSlateDrawElement::MakeCustomVerts(Out, OnLayer, Handle, Vertices, Indices, nullptr, 0, 0);
		};
		// A slightly smaller plain cube inside fills the gaps the rounded corners of the faces would leave.
		for (int32 n = 1; n <= 6; n++) DrawFace(n, 0.93f, false, Layer + 1);
		for (int32 n = 1; n <= 6; n++) DrawFace(n, 1.f, true, Layer + 2);
		return Layer + 2;
	}

private:
	int32 Value = 1;
	float Size = 62.f;
	const FSlateBrush* Faces[6] = {};
	TSharedPtr<FSlateRoundedBoxBrush> Shadow;
	float Rest = 0, SpinX = 0, SpinY = 0, SpinZ = 0, Skid = 0, Duration = 1.2f;
	FVector2f From = FVector2f::ZeroVector;
	double Start = 0;
	bool bStill = false;
	bool bSounds = false;
};

// ---------- Entrances ----------
// How a page or a card arrives. A page rises a little into place (dialog[open] in game.css). A card is
// dealt: it lifts off the table showing its back, turns over, and settles face up.
class SPortsEntrance : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsEntrance) : _Kind(0), _Delay(0.f) {}
		SLATE_ARGUMENT(int32, Kind)
		SLATE_ARGUMENT(float, Delay)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Kind = Args._Kind;
		Duration = Kind == 1 ? 0.7f : Kind == 2 ? 0.85f : 0.28f;
		Start = FSlateApplication::Get().GetCurrentTime() + Args._Delay;
		Back = MakeShared<FSlateRoundedBoxBrush>(PortsUi::Color(TEXT("#6a120c")), 14.f, PortsUi::Color(TEXT("#d9a82b")), 3.f);
		Inner = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::Transparent, 9.f, PortsUi::Color(TEXT("#f3d27a")), 2.f);
		ChildSlot[ Args._Content.Widget ];
		SetRenderTransformPivot(FVector2D(0.5, 0.5));
		Apply(0.f);
		// Something that arrives later takes its place on the page at once, but is not drawn at all until its moment.
		if (Args._Delay > 0.f) ChildSlot.GetWidget()->SetVisibility(EVisibility::Hidden);
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double Now, float)
		{
			if (Now >= Start && ChildSlot.GetWidget()->GetVisibility() == EVisibility::Hidden) ChildSlot.GetWidget()->SetVisibility(EVisibility::SelfHitTestInvisible);
			const float T = static_cast<float>(FMath::Clamp((Now - Start) / Duration, 0.0, 1.0));
			Apply(T);
			return T >= 1.f ? EActiveTimerReturnType::Stop : EActiveTimerReturnType::Continue;
		}));
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override
	{
		if (!bBack) return SCompoundWidget::OnPaint(Args, G, Culling, Out, Layer, Style, bEnabled);
		// The card's back: deep red, ruled in gold.
		const FVector2f Size = G.GetLocalSize();
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), Back.Get(), ESlateDrawEffect::None, PortsUi::Color(TEXT("#6a120c")));
		if (Size.X > 40 && Size.Y > 40) FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(Size - FVector2f(28, 28), FSlateLayoutTransform(FVector2f(14, 14))), Inner.Get(), ESlateDrawEffect::None, FLinearColor::Transparent);
		return Layer + 1;
	}

private:
	void Apply(float T)
	{
		const float E = 1.f - FMath::Pow(1.f - T, 3.f);
		if (Kind == 1)
		{
			// Half a turn: its back shows for the first half, edge-on in the middle.
			const float Angle = UE_PI * (1.f - E);
			bBack = Angle > UE_HALF_PI;
			const float Lift = FMath::Sin(T * UE_PI);
			SetRenderTransform(FSlateRenderTransform(FScale2D(FMath::Max(0.02f, FMath::Abs(FMath::Cos(Angle))) * (1.f + 0.05f * Lift), 1.f + 0.05f * Lift), FVector2D(0, -22.0 * Lift)));
			SetRenderOpacity(FMath::Clamp(T / 0.12f, 0.f, 1.f));
		}
		else
		{
			bBack = false;
			SetRenderTransform(FSlateRenderTransform(FScale2D(0.98f + 0.02f * E), FVector2D(0, 16.0 * (1.f - E))));
			SetRenderOpacity(E);
		}
	}

	int32 Kind = 0;
	float Duration = 0.28f;
	double Start = 0;
	bool bBack = false;
	TSharedPtr<FSlateRoundedBoxBrush> Back, Inner;
};

// ---------- Painted boxes and buttons ----------

void SPortsBox::Construct(const FArguments& Args)
{
	Look = Args._Look;
	bBlockMouse = Args._BlockMouse;
	ChildSlot.Padding(Args._Padding)[ Args._Content.Widget ];
}

int32 SPortsBox::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
	const int32 Next = PaintLook(Look, Geometry, Out, Layer);
	return SCompoundWidget::OnPaint(Args, Geometry, Culling, Out, Next, Style, bEnabled);
}

FReply SPortsBox::OnMouseButtonDown(const FGeometry&, const FPointerEvent&)
{
	return bBlockMouse ? FReply::Handled() : FReply::Unhandled();
}

void SPortsButton::Construct(const FArguments& Args)
{
	Look = Args._Look;
	HoverLook = Args._HoverLook;
	bCanClick = Args._Enabled;
	HoverShift = Args._HoverShift;
	DownShift = Args._DownShift;
	OnClicked = Args._OnClicked;
	if (!bCanClick)
	{
		SetRenderOpacity(Args._DisabledOpacity);
		Look.ShadowDrop = 0;
	}
	FMargin Padding = Args._Padding;
	Padding.Bottom += Args._Look.ShadowDrop;
	ChildSlot.Padding(Padding)[ Args._Content.Widget ];
}

int32 SPortsButton::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
	FPortsBoxLook Now = bHover && bCanClick ? HoverLook : Look;
	if (bDown) Now.ShadowDrop = FMath::Max(0.f, Now.ShadowDrop - 2.f);
	// The hard shadow hangs below the button's own box.
	const FVector2f Size = Geometry.GetLocalSize() - FVector2f(0, Look.ShadowDrop);
	const FGeometry Face = Geometry.MakeChild(Size, FSlateLayoutTransform());
	const int32 Next = PaintLook(Now, Face, Out, Layer);
	return SCompoundWidget::OnPaint(Args, Geometry, Culling, Out, Next, Style, bEnabled);
}

void SPortsButton::UpdateShift()
{
	const FVector2D Shift = !bCanClick ? FVector2D::ZeroVector : bDown ? DownShift : bHover ? HoverShift : FVector2D::ZeroVector;
	SetRenderTransform(FSlateRenderTransform(FVector2f(Shift)));
}

FReply SPortsButton::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	if (!bCanClick) return FReply::Handled();
	bDown = true;
	UpdateShift();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SPortsButton::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !bDown) return FReply::Unhandled();
	bDown = false;
	UpdateShift();
	const bool bInside = Geometry.IsUnderLocation(Event.GetScreenSpacePosition());
	// The click may rebuild the screen this button is on, so nothing of the button is used after it.
	const TFunction<void()> Clicked = OnClicked;
	const FReply Reply = FReply::Handled().ReleaseMouseCapture();
	if (bInside && Clicked) { if (GClickSound) GClickSound(); Clicked(); }
	return Reply;
}

void SPortsButton::OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event)
{
	SCompoundWidget::OnMouseEnter(Geometry, Event);
	bHover = true;
	UpdateShift();
}

void SPortsButton::OnMouseLeave(const FPointerEvent& Event)
{
	SCompoundWidget::OnMouseLeave(Event);
	bHover = false;
	UpdateShift();
}

FCursorReply SPortsButton::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return bCanClick ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Cursor(EMouseCursor::SlashedCircle);
}

// ---------- Fonts, styles and pictures ----------

namespace PortsUi
{
	FLinearColor Color(const TCHAR* HexCode) { return Hex(HexCode); }

	// The stylesheet's sizes are in pixels; Slate's are in points (72 to the stylesheet's 96).
	FSlateFontInfo Serif(float Pixels, const TCHAR* Face) { Init(); return FSlateFontInfo(SerifFont, Pixels * 0.75f, Face); }
	FSlateFontInfo Caps(float Pixels, bool bHeavy) { Init(); return FSlateFontInfo(CapsFont, Pixels * 0.75f, bHeavy ? TEXT("Bold") : TEXT("Regular")); }
	FSlateFontInfo Black(float Pixels) { Init(); return FSlateFontInfo(BlackFont, Pixels * 0.75f, TEXT("Regular")); }

	void Init()
	{
		if (StyleSet.IsValid()) return;
		const FString Fonts = FPaths::Combine(UiDir(), TEXT("Fonts"));
		const FString Engine = FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate"), TEXT("Fonts"));
		const auto Add = [](FTypeface& Face, const TCHAR* Name, const FString& File) { Face.AppendFont(Name, File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad); };

		SerifFont = MakeShared<FStandaloneCompositeFont>();
		Add(SerifFont->DefaultTypeface, TEXT("Regular"), Fonts / TEXT("EBGaramond-Regular.ttf"));
		Add(SerifFont->DefaultTypeface, TEXT("Italic"), Fonts / TEXT("EBGaramond-Italic.ttf"));
		Add(SerifFont->DefaultTypeface, TEXT("Bold"), Fonts / TEXT("EBGaramond-SemiBold.ttf"));
		Add(SerifFont->FallbackTypeface.Typeface, TEXT("Regular"), Engine / TEXT("DroidSansFallback.ttf"));

		// Cinzel has no arrows or fleurons: those come from EB Garamond.
		CapsFont = MakeShared<FStandaloneCompositeFont>();
		Add(CapsFont->DefaultTypeface, TEXT("Regular"), Fonts / TEXT("Cinzel-SemiBold.ttf"));
		Add(CapsFont->DefaultTypeface, TEXT("Bold"), Fonts / TEXT("Cinzel-ExtraBold.ttf"));
		Add(CapsFont->FallbackTypeface.Typeface, TEXT("Regular"), Fonts / TEXT("EBGaramond-SemiBold.ttf"));

		BlackFont = MakeShared<FStandaloneCompositeFont>();
		Add(BlackFont->DefaultTypeface, TEXT("Regular"), Fonts / TEXT("UnifrakturMaguntia.ttf"));
		Add(BlackFont->FallbackTypeface.Typeface, TEXT("Regular"), Fonts / TEXT("EBGaramond-Regular.ttf"));

		StyleSet = MakeShared<FSlateStyleSet>("PortsOfPlagueStyle");
		FSlateStyleSet& S = *StyleSet;
		const TCHAR* Ink = TEXT("#26190a");
		const TCHAR* Soft = TEXT("#4d3a22");
		const TCHAR* Red = TEXT("#b0261a");
		// Whole paragraphs and headings.
		S.Set("Ports.Body", Text(Serif(18), Ink));
		S.Set("Ports.Small", Text(Serif(15.5f), Soft));
		S.Set("Ports.Title", Text(Black(92), TEXT("#9a1d12")));
		S.Set("Ports.TitleSmall", Text(Black(48), TEXT("#9a1d12")));
		S.Set("Ports.Subtitle", Text(Serif(21.6f, TEXT("Italic")), Soft));
		S.Set("Ports.H1", Text(Caps(36, true), Ink, 30));
		S.Set("Ports.H2", Text(Caps(25.2f, true), Red, 30));
		S.Set("Ports.H3", Text(Caps(19.8f, true), Ink, 30));
		S.Set("Ports.Label", Text(Caps(15.3f), Ink, 20));
		S.Set("Ports.NoteTitle", Text(Caps(17.1f, true), Red, 30));
		S.Set("Ports.Light", Text(Serif(18), TEXT("#f3e6c4")));
		S.Set("Ports.LightSmall", Text(Serif(15.3f), TEXT("#f3e6c4")));
		S.Set("Ports.Ribbon", Text(Caps(18, true), TEXT("#fff6e0"), 30));
		S.Set("Ports.CardKind", Text(Caps(14, false), TEXT("#f3d27a"), 140));
		S.Set("Ports.CardTitle", Text(Caps(27.9f, true), TEXT("#fff6e0"), 30));
		S.Set("Ports.Year", Text(Black(61), Red));
		S.Set("Ports.Total", Text(Caps(28.8f, true), Ink));
		S.Set("Ports.Place", Text(Caps(30.6f, true), TEXT("#7a560c")));
		S.Set("Ports.Brand", Text(Black(28.8f), TEXT("#f3d27a")));
		S.Set("Ports.Date", Text(Caps(20.7f, true), TEXT("#f3d27a"), 30));
		S.Set("Ports.DateSmall", Text(Serif(15.3f), TEXT("#f3dfb4")));
		S.Set("Ports.Stat", Text(Caps(23.4f, true), Ink));
		S.Set("Ports.Btn", Text(Caps(18), Ink, 40));
		S.Set("Ports.BtnLight", Text(Caps(18), TEXT("#fff6e0"), 40));
		S.Set("Ports.BtnGold", Text(Caps(18), TEXT("#2a1605"), 40));
		S.Set("Ports.BtnSmall", Text(Caps(16.2f), Ink, 40));
		S.Set("Ports.BtnSmallLight", Text(Caps(16.2f), TEXT("#ffffff"), 40));
		S.Set("Ports.BtnSmallGold", Text(Caps(16.2f), TEXT("#f3d27a"), 40));
		S.Set("Ports.PassTitle", Text(Caps(43.2f, true), TEXT("#f3d27a"), 30));
		S.Set("Ports.PassText", Text(Serif(20.7f), TEXT("#fbe9c0")));
		S.Set("Ports.Toast", Text(Serif(17.6f), TEXT("#fbf1d6")));
		// Inline styles used inside markup.
		S.Set("b", Text(Serif(18, TEXT("Bold")), Ink));
		S.Set("i", Text(Serif(18, TEXT("Italic")), Ink));
		S.Set("small", Text(Serif(15.5f), Soft));
		S.Set("sb", Text(Serif(15.5f, TEXT("Bold")), Ink));
		S.Set("si", Text(Serif(15.5f, TEXT("Italic")), Soft));
		S.Set("risk", Text(Serif(18, TEXT("Bold")), Red));
		S.Set("srisk", Text(Serif(15.3f, TEXT("Bold")), Red));
		S.Set("warn", Text(Serif(18, TEXT("Bold")), TEXT("#a8640a")));
		S.Set("lapis", Text(Serif(18, TEXT("Bold")), TEXT("#1d4a86")));
		S.Set("debate", Text(Serif(15.3f, TEXT("Italic")), TEXT("#1d4a86")));
		S.Set("id", Text(Caps(13), TEXT("#7a560c")));
		S.Set("cite", Text(Serif(14.4f), Soft));
		S.Set("key", Text(Serif(14.4f, TEXT("Bold")), TEXT("#7a560c")));
		S.Set("lkey", Text(Serif(14.4f), TEXT("#f3dfb4")));
		S.Set("caps", Text(Caps(17.6f), Ink, 30));
		S.Set("capsb", Text(Caps(19.8f, true), Ink, 30));
		S.Set("capsred", Text(Caps(18.9f, true), Red, 30));
		S.Set("legend", Text(Caps(14.4f, true), Red, 20));
		// The lobby's room-code box: where to join, and the code itself.
		S.Set("chip", Text(Caps(16.f, false), TEXT("#f3d27a")));
		S.Set("chipcode", Text(Caps(21.f, true), TEXT("#fff6e0"), 120));
		S.Set("chipat", Text(Serif(14.4f), TEXT("#fbe9c0")));
		S.Set("doton", Text(Serif(13.f), TEXT("#2e9d5b")));
		S.Set("dotoff", Text(Serif(13.f), TEXT("#b9ad92")));
		S.Set("roomat", Text(Serif(28.f), TEXT("#fbe9c0")));
		S.Set("roomaddr", Text(Serif(28.f, TEXT("Bold")), TEXT("#f3d27a")));
		S.Set("roomem", Text(Serif(28.f, TEXT("Italic")), TEXT("#fbe9c0")));
		S.Set("roomcode", Text(Caps(70.f, true), TEXT("#fff6e0"), 180));
		S.Set("track", Text(Serif(11.f), TEXT("#f3d27a")));
		S.Set("trackon", Text(Serif(12.5f), TEXT("#5c0d09")));
		S.Set("lb", Text(Serif(18, TEXT("Bold")), TEXT("#fff6e0")));
		S.Set("lsmall", Text(Serif(15.3f), TEXT("#f3dfb4")));
		S.Set("lcaps", Text(Caps(14, false), TEXT("#f3d27a"), 140));
		S.Set("big", Text(Serif(22.5f, TEXT("Bold")), Ink));
		S.Set("dropcap", Text(Caps(50, true), Red));
		S.Set("btnsmall", Text(Serif(14.8f), Soft));
		S.Set("btnsmalllight", Text(Serif(14.8f), TEXT("#f3dfb4")));

		// The pictures rendered from the website's artwork (Tools/make_ui_art.mjs), at half their pixel size.
		TArray<FString> Files;
		const FString ArtDir = FPaths::Combine(UiDir(), TEXT("Art"));
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(ArtDir, TEXT("*.png")), true, false);
		for (const FString& File : Files)
		{
			S.Set(*(TEXT("Art.") + FPaths::GetBaseFilename(File)), new FSlateImageBrush(FPaths::Combine(ArtDir, File), FVector2D(32, 32)));
		}
		FSlateStyleRegistry::RegisterSlateStyle(S);
	}

	void Shutdown()
	{
		if (!StyleSet.IsValid()) return;
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
		SerifFont.Reset();
		CapsFont.Reset();
		BlackFont.Reset();
	}

	const ISlateStyle& Style()
	{
		Init();
		return *StyleSet;
	}

	FString Esc(const FString& Text)
	{
		return Text.Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;")).Replace(TEXT("\""), TEXT("&quot;"));
	}

	TSharedRef<SWidget> Rich(const FString& Markup, const TCHAR* TextStyle, ETextJustify::Type Justify, bool bWrap, float WrapAt)
	{
		// With a known width the text wraps at once; otherwise it wraps to whatever room it is given, a frame later.
		return SNew(SRichTextBlock)
			.Text(FText::FromString(Markup))
			.TextStyle(&Style().GetWidgetStyle<FTextBlockStyle>(TextStyle))
			.DecoratorStyleSet(&Style())
			.AutoWrapText(bWrap && WrapAt <= 0.f)
			.WrapTextAt(bWrap ? WrapAt : 0.f)
			.LineHeightPercentage(1.08f)
			.Justification(Justify);
	}

	FPortsBoxLook PlainLook(const FLinearColor& Fill, float Radius, const FLinearColor& Border, float BorderWidth)
	{
		FPortsBoxLook L;
		L.Top = L.Bottom = Fill;
		L.Radius = Radius;
		L.Border = Border;
		L.BorderWidth = BorderWidth;
		return L;
	}

	TSharedRef<SWidget> Box(const FPortsBoxLook& Look, const TSharedRef<SWidget>& Content, const FMargin& Padding, bool bBlockMouse)
	{
		return SNew(SPortsBox).Look(Look).Padding(Padding).BlockMouse(bBlockMouse)[ Content ];
	}

	TSharedRef<SWidget> Tinted(const FLinearColor& Tint, const TSharedRef<SWidget>& Content, const FMargin& Padding, bool bSmallCorners)
	{
		return Box(PlainLook(Tint, bSmallCorners ? 6.f : 12.f), Content, Padding);
	}

	// The website's buttons (.btn and its variants in game.css).
	TSharedRef<SWidget> Button(const FString& Markup, TFunction<void()> OnClick, EButton Kind, bool bEnabled, const FString&, float WrapAt)
	{
		const bool bSmall = Kind == EButton::Small || Kind == EButton::SmallOn || Kind == EButton::SmallGhostLight;
		FPortsBoxLook L;
		L.Radius = 12;
		L.BorderWidth = 2;
		L.ShadowDrop = 3;
		const TCHAR* TextStyle = bSmall ? TEXT("Ports.BtnSmall") : TEXT("Ports.Btn");
		switch (Kind)
		{
		case EButton::Primary:
			L.Top = Hex(TEXT("#c93a2c")); L.Bottom = Hex(TEXT("#8a1a10")); L.Border = L.Shadow = Hex(TEXT("#4f0c07"));
			TextStyle = TEXT("Ports.BtnLight");
			break;
		case EButton::Gold:
			L.Top = Hex(TEXT("#f7d77a")); L.Bottom = Hex(TEXT("#c4901c")); L.Border = L.Shadow = Hex(TEXT("#6b4a0a"));
			TextStyle = TEXT("Ports.BtnGold");
			break;
		case EButton::Ghost:
			L.Border = Hex(TEXT("#4d3a22")); L.ShadowDrop = 0;
			break;
		case EButton::SmallGhostLight:
			L.Top = L.Bottom = Hex(TEXT("#fff8e2"), 0.12f); L.Border = Hex(TEXT("#f3d27a"), 0.6f); L.ShadowDrop = 0;
			TextStyle = TEXT("Ports.BtnSmallGold");
			break;
		case EButton::SmallOn:
			L.Top = Hex(TEXT("#2a5d9e")); L.Bottom = Hex(TEXT("#16396a")); L.Border = L.Shadow = Hex(TEXT("#0c2344"));
			TextStyle = TEXT("Ports.BtnSmallLight");
			break;
		default:
			L.Top = Hex(TEXT("#fff8e2")); L.Bottom = Hex(TEXT("#ebd49c")); L.Border = Hex(TEXT("#3b2413")); L.Shadow = Hex(TEXT("#6b4a1f"));
			break;
		}
		FPortsBoxLook Hover = L;
		Hover.Top = Brighten(L.Top, 1.08f);
		Hover.Bottom = Brighten(L.Bottom, 1.08f);
		return SNew(SPortsButton)
			.Look(L).HoverLook(Hover)
			.Enabled(bEnabled)
			.OnClicked(OnClick)
			.Padding(bSmall ? FMargin(14, 5) : FMargin(22, 8))
			[
				SNew(SBox).MinDesiredHeight(bSmall ? 28.f : 30.f).VAlign(VAlign_Center).HAlign(HAlign_Center)
				[
					Rich(Markup, TextStyle, ETextJustify::Center, Markup.Contains(TEXT("\n")), WrapAt)
				]
			];
	}

	const FSlateBrush* PictureBrush(const FString& Name)
	{
		return Style().GetBrush(*(TEXT("Art.") + Name));
	}

	TSharedRef<SWidget> Picture(const FString& Name, const FVector2D& Size)
	{
		return SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y)[ SNew(SImage).Image(PictureBrush(Name)) ];
	}

	TSharedRef<SWidget> ShimmerTitle(const FString& Text, float Pixels)
	{
		return SNew(SPortsShimmer).Text(Text).Font(Black(Pixels));
	}

	TSharedRef<SWidget> DropCapText(const FString& Text, float Width)
	{
		return SNew(SPortsDropCap).Text(Text).Width(Width);
	}

	// .frame in game.css: parchment, a brown border, a gold line just inside it and a red fleuron in two corners.
	TSharedRef<SWidget> Frame(const TSharedRef<SWidget>& Content, bool bBlockMouse)
	{
		FPortsBoxLook L;
		L.Top = Hex(TEXT("#fffaf0")); L.Mid = Hex(TEXT("#f7ecd0")); L.MidAt = 0.45f; L.Bottom = Hex(TEXT("#f0dfb4"));
		L.Radius = 10;
		L.Border = Hex(TEXT("#8a6a36")); L.BorderWidth = 3;
		L.Inset = Hex(TEXT("#d9a82b")); L.InsetAt = 8; L.InsetWidth = 2;
		L.SoftShadow = 0.45f;
		const FSlateFontInfo Fleuron = Serif(22);
		return SNew(SOverlay)
			+ SOverlay::Slot()[ Box(L, Content, FMargin(29, 26), bBlockMouse) ]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(5, 0, 0, 0)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("❦"))).Font(Fleuron).ColorAndOpacity(Hex(TEXT("#b0261a"))).Visibility(EVisibility::HitTestInvisible)
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 5, 0)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("❦"))).Font(Fleuron).ColorAndOpacity(Hex(TEXT("#b0261a"))).Visibility(EVisibility::HitTestInvisible)
				.RenderTransform(FSlateRenderTransform(FQuat2D(UE_PI))).RenderTransformPivot(FVector2D(0.5, 0.5))
			];
	}

	// .panel in game.css: parchment with a coloured ribbon across the top.
	TSharedRef<SWidget> Panel(const TSharedRef<SWidget>& RibbonContent, const FLinearColor& Ribbon, const TSharedRef<SWidget>& Content)
	{
		FPortsBoxLook L;
		L.Top = Hex(TEXT("#fffaf0")); L.Bottom = Hex(TEXT("#f7ecd0"));
		L.Radius = 12;
		L.Border = Hex(TEXT("#d8bc7c")); L.BorderWidth = 2;
		L.SoftShadow = 0.4f;
		FPortsBoxLook R;
		R.Top = Ribbon; R.Bottom = Darken(Ribbon, 0.65f);
		R.bSideways = true;
		R.bTopOnly = true;
		R.Radius = 12;
		return Box(L, SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Box(R, RibbonContent, FMargin(14, 6)) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(14, 8, 14, 11))[ Content ], FMargin(0), true);
	}

	TSharedRef<SWidget> Panel(const FString& Title, const FLinearColor& Ribbon, const TSharedRef<SWidget>& Content)
	{
		return Panel(Rich(Esc(Title), TEXT("Ports.Ribbon")), Ribbon, Content);
	}

	FString CrestGlyph(const FString& Shape)
	{
		if (Shape == TEXT("square")) return TEXT("■");
		if (Shape == TEXT("triangle")) return TEXT("▲");
		if (Shape == TEXT("diamond")) return TEXT("◆");
		if (Shape == TEXT("hexagon")) return TEXT("⬢");
		if (Shape == TEXT("star")) return TEXT("★");
		return TEXT("●");
	}

	// Each crest shape belongs to one house colour (PLAYER_STYLES), so the picture carries both.
	TSharedRef<SWidget> Crest(const FString&, const FString& Shape, float Size)
	{
		return Picture(TEXT("crest_") + Shape, FVector2D(Size, Size));
	}

	TSharedRef<SWidget> Crest(const FPortsPlayer& Player, float Size)
	{
		return Crest(Player.color, Player.crest, Size);
	}

	TSharedRef<SWidget> Banner(const FString& Shape, float Width)
	{
		return Picture(TEXT("banner_") + Shape, FVector2D(Width, Width * 214.f / 150.f));
	}

	TSharedRef<SWidget> Die(int32 Value, bool bRed, bool bGold, bool bSmall, const FString& LabelMarkup)
	{
		const float Size = bSmall ? 42.f : 62.f;
		// The dice of one throw land one after another, all within the same 1.2 seconds (rollDice in dice.js).
		const float Gap = 0.07f;
		const float Delay = GDiceStill ? -1.f : GDiceBase + GDiceCount * Gap;
		GDiceCount++;
		const TSharedRef<SVerticalBox> Out = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(SPortsDie).Value(FMath::Clamp(Value, 1, 6)).Kind(bRed ? TEXT("red") : bGold ? TEXT("gold") : TEXT("ivory")).Size(Size).Delay(Delay).Sounds(GDiceCount == 1) ];
		if (!LabelMarkup.IsEmpty())
		{
			Out->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[ Rich(LabelMarkup, TEXT("Ports.LightSmall"), ETextJustify::Center, false) ];
		}
		return Out;
	}

	void SetClickSound(TFunction<void()> Play) { GClickSound = MoveTemp(Play); }
	void SetDiceSound(TFunction<void()> Play) { GDiceSound = MoveTemp(Play); }
	void ResetDice() { GDiceBase = 0.45f; GDiceCount = 0; }
	void NextDiceTray() { if (GDiceCount > 0) { GDiceBase += SPortsDie::RollSeconds + 0.1f; GDiceCount = 0; } }
	float DiceTrayStart() { return GDiceStill ? 0.f : GDiceBase; }
	float DiceSettleTime() { return GDiceStill ? 0.f : GDiceBase + SPortsDie::RollSeconds + FMath::Max(0, GDiceCount - 1) * 0.07f; }
	void SetDiceStill(bool bStill) { GDiceStill = bStill; }

	TSharedRef<SWidget> Entrance(const TSharedRef<SWidget>& Content, int32 Kind, float Delay)
	{
		return SNew(SPortsEntrance).Kind(Kind).Delay(Delay)[ Content ];
	}

	// THEME_COLORS in art.js.
	FTheme Theme(const FString& Id)
	{
		struct FRow { const TCHAR* Id; const TCHAR* Main; const TCHAR* Light; const TCHAR* Label; };
		static const FRow Rows[] = {
			{ TEXT("trade"), TEXT("#1d4a86"), TEXT("#dbe6f5"), TEXT("Trade") },
			{ TEXT("timeline"), TEXT("#6b4a1f"), TEXT("#efe2c4"), TEXT("Timeline") },
			{ TEXT("cities"), TEXT("#8a5a12"), TEXT("#f5e7c6"), TEXT("Cities") },
			{ TEXT("social"), TEXT("#a82318"), TEXT("#f7dcd6"), TEXT("Society") },
			{ TEXT("economic"), TEXT("#8b6b0a"), TEXT("#f6ebc4"), TEXT("Economy") },
			{ TEXT("medical"), TEXT("#1f6b4f"), TEXT("#d7ede3"), TEXT("Medicine") },
			{ TEXT("church"), TEXT("#5b2a86"), TEXT("#e8dcf2"), TEXT("Church") },
			{ TEXT("persecution"), TEXT("#3b3024"), TEXT("#e6e0d6"), TEXT("Persecution") },
			{ TEXT("fortune"), TEXT("#b07d12"), TEXT("#fbf0cf"), TEXT("Fortune") },
		};
		for (const FRow& R : Rows) if (Id == R.Id) return { Hex(R.Main), Hex(R.Light), R.Label };
		return { Hex(Rows[1].Main), Hex(Rows[1].Light), Id };
	}

	FString ShortCite(const FString& SourceId)
	{
		for (const FPortsValue& S : FPortsData::Get().Files[TEXT("sources")].Get(TEXT("sources")).GetItems())
		{
			if (S.Get(TEXT("id")).AsString() != SourceId) continue;
			// First part of the MLA citation (author and title) is enough on screen.
			const FString Plain = S.Get(TEXT("mla")).AsString().Replace(TEXT("*"), TEXT(""));
			for (int32 Dot = 0; Dot < Plain.Len(); Dot++)
			{
				if (Plain[Dot] != TEXT('.')) continue;
				int32 Quote = Dot + 1;
				while (Quote < Plain.Len() && FChar::IsWhitespace(Plain[Quote])) Quote++;
				if (Quote < Plain.Len() && Plain[Quote] == TEXT('"'))
				{
					const int32 End = Plain.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Quote + 1);
					if (End != INDEX_NONE) return Plain.Left(End + 1);
				}
			}
			TArray<FString> Parts;
			Plain.ParseIntoArray(Parts, TEXT(". "), false);
			return Parts.Num() > 1 ? Parts[0] + TEXT(". ") + Parts[1] : Plain;
		}
		return SourceId;
	}
}

// ---------- Pages ----------

namespace
{
	// What is left of a width after taking some away (0 means "not known").
	float Less(float Width, float By) { return Width > 0.f ? FMath::Max(40.f, Width - By) : 0.f; }
}

TSharedRef<SWidget> FPortsDoc::Build(float AtWidth) const
{
	// -PortsNoWidths builds every page the old way, without telling it its width: used once to prove
	// that the layout check in SPortsRoot::Tick does notice a page that changes size after it appears.
	static const bool bForget = FParse::Param(FCommandLine::Get(), TEXT("PortsNoWidths"));
	if (bForget) AtWidth = 0.f;
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (const FItem& Item : Items)
	{
		Box->AddSlot().AutoHeight().Padding(Item.Padding)[ Item.Make(Less(AtWidth, Item.Padding.Left + Item.Padding.Right)) ];
	}
	return Box;
}

FPortsDoc& FPortsDoc::AddBuilt(FMake Make, const FMargin& Padding)
{
	Items.Add({ MoveTemp(Make), Padding });
	return *this;
}

FPortsDoc& FPortsDoc::Add(const TSharedRef<SWidget>& Widget, const FMargin& Padding)
{
	return AddBuilt([Widget](float) { return Widget; }, Padding);
}

FPortsDoc& FPortsDoc::Text(const FString& Markup, const TCHAR* Style, ETextJustify::Type Justify, const FMargin& Padding)
{
	const FString StyleName = Style;
	return AddBuilt([Markup, StyleName, Justify](float W) { return PortsUi::Rich(Markup, *StyleName, Justify, true, W); }, Padding);
}

FPortsDoc& FPortsDoc::H1(const FString& Text_, ETextJustify::Type Justify) { return Text(PortsUi::Esc(Text_), TEXT("Ports.H1"), Justify, FMargin(0, 0, 0, 12)); }
FPortsDoc& FPortsDoc::H2(const FString& Markup, ETextJustify::Type Justify) { return Text(Markup, TEXT("Ports.H2"), Justify, FMargin(0, 2, 0, 8)); }
FPortsDoc& FPortsDoc::H3(const FString& Markup) { return Text(Markup, TEXT("Ports.H3"), ETextJustify::Left, FMargin(0, 8, 0, 5)); }
FPortsDoc& FPortsDoc::Label(const FString& Text_) { return Add(PortsUi::Rich(PortsUi::Esc(Text_), TEXT("Ports.Label"), ETextJustify::Left, false), FMargin(0, 8, 0, 3)); }
FPortsDoc& FPortsDoc::P(const FString& Markup, ETextJustify::Type Justify) { return Text(Markup, TEXT("Ports.Body"), Justify, FMargin(0, 5)); }
FPortsDoc& FPortsDoc::Small(const FString& Markup) { return Text(Markup, TEXT("Ports.Small"), ETextJustify::Left, FMargin(0, 2)); }
FPortsDoc& FPortsDoc::Light(const FString& Markup) { return Text(Markup, TEXT("Ports.Light"), ETextJustify::Left, FMargin(0, 3)); }
FPortsDoc& FPortsDoc::Space(float Height) { return Add(SNew(SBox).HeightOverride(Height), FMargin(0)); }

FPortsDoc& FPortsDoc::DropCap(const FString& Text_)
{
	if (Text_.IsEmpty()) return *this;
	return AddBuilt([Text_](float W) { return PortsUi::DropCapText(Text_, W); }, FMargin(0, 5));
}

FPortsDoc& FPortsDoc::Bullets(const TArray<FString>& Items_, bool bNumbered)
{
	for (int32 i = 0; i < Items_.Num(); i++)
	{
		const FString Mark = bNumbered ? FString::Printf(TEXT("%d."), i + 1) : FString(TEXT("•"));
		const FString Item = Items_[i];
		const float MarkWidth = bNumbered ? 26.f : 12.f;
		AddBuilt([Mark, Item, MarkWidth](float W)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 8, 0)[ SNew(SBox).WidthOverride(MarkWidth)[ PortsUi::Rich(Mark, TEXT("Ports.Body"), ETextJustify::Left, false) ] ]
				+ SHorizontalBox::Slot().FillWidth(1)[ PortsUi::Rich(Item, TEXT("Ports.Body"), ETextJustify::Left, true, Less(W, MarkWidth + 16)) ];
		}, FMargin(0, 2));
	}
	return *this;
}

FPortsDoc& FPortsDoc::Nest(const FPortsDoc& Inner, const FMargin& Padding, float Inset)
{
	return AddBuilt([Inner, Inset](float W) { return Inner.Build(Less(W, Inset)); }, Padding);
}

FPortsDoc& FPortsDoc::Boxed(const FPortsBoxLook& Look, const FPortsDoc& Inner, const FMargin& BoxPadding, const FMargin& Padding)
{
	return AddBuilt([Look, Inner, BoxPadding](float W) { return PortsUi::Box(Look, Inner.Build(Less(W, BoxPadding.Left + BoxPadding.Right)), BoxPadding); }, Padding);
}

FPortsDoc& FPortsDoc::Columns(const TArray<FPortsDoc>& Pages, float Gap, const FMargin& Padding)
{
	return AddBuilt([Pages, Gap](float W)
	{
		const int32 N = FMath::Max(1, Pages.Num());
		const float Each = W > 0.f ? (W - Gap * (N - 1)) / N : 0.f;
		const TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
		for (int32 i = 0; i < Pages.Num(); i++) Line->AddSlot().FillWidth(1).Padding(i ? Gap : 0, 0, 0, 0)[ Pages[i].Build(Each) ];
		return Line;
	}, Padding);
}

// .card in game.css: a border in the theme's colour, a darkening band with a round
// illustration, the kind in small gold capitals, the title, and a gold rule under the band.
FPortsDoc& FPortsDoc::Card(const FString& ThemeId, const FString& KindMarkup, const FString& Title, const FPortsDoc& Body)
{
	return AddBuilt([ThemeId, KindMarkup, Title, Body](float W)
	{
		const PortsUi::FTheme T = PortsUi::Theme(ThemeId);
		FPortsBoxLook Outer = PortsUi::PlainLook(Hex(TEXT("#fffdf6")), 14, T.Main, 3);
		Outer.SoftShadow = 0.4f;
		FPortsBoxLook Band;
		Band.Top = T.Main; Band.Bottom = Darken(T.Main, 0.55f);
		Band.bSideways = true;
		Band.bTopOnly = true;
		Band.Radius = 12;
		const float TitleWidth = Less(W, 4 + 14 + 74 + 12 + 16);
		return PortsUi::Entrance(PortsUi::Box(Outer, SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 2, 0)
			[
				PortsUi::Box(Band, SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[ PortsUi::Picture(TEXT("illus_") + ThemeId, FVector2D(74, 74)) ]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ PortsUi::Rich(KindMarkup.ToUpper(), TEXT("Ports.CardKind"), ETextJustify::Left, true, TitleWidth) ]
						+ SVerticalBox::Slot().AutoHeight()[ PortsUi::Rich(PortsUi::Esc(Title), TEXT("Ports.CardTitle"), ETextJustify::Left, true, TitleWidth) ]
					], FMargin(14, 8, 16, 10))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2, 0)[ PortsUi::Box(PortsUi::PlainLook(Hex(TEXT("#d9a82b")), 0), SNew(SBox).HeightOverride(4), FMargin(0)) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(18, 12, 18, 16))[ Body.Build(Less(W, 36)) ], FMargin(0)), 1, 0.12f);
	}, FMargin(0, 4));
}

// .dice-tray in game.css: green felt in a wooden rim.
FPortsDoc& FPortsDoc::Tray(const FPortsDoc& Body)
{
	FPortsBoxLook L;
	L.Top = Hex(TEXT("#2b643b")); L.Bottom = Hex(TEXT("#1a4226"));
	L.Radius = 14;
	L.Border = Hex(TEXT("#5a3920")); L.BorderWidth = 4;
	return Boxed(L, Body, FMargin(15, 12), FMargin(0, 6));
}

// .panel in game.css: parchment with a coloured ribbon across the top.
FPortsDoc& FPortsDoc::Panel(const TSharedRef<SWidget>& RibbonContent, const FLinearColor& Ribbon, const FPortsDoc& Body, const FMargin& Padding)
{
	return AddBuilt([RibbonContent, Ribbon, Body](float W) { return PortsUi::Panel(RibbonContent, Ribbon, Body.Build(Less(W, 28))); }, Padding);
}

FPortsDoc& FPortsDoc::Panel(const FString& Title, const FLinearColor& Ribbon, const FPortsDoc& Body, const FMargin& Padding)
{
	return Panel(PortsUi::Rich(PortsUi::Esc(Title), TEXT("Ports.Ribbon"), ETextJustify::Left, false), Ribbon, Body, Padding);
}

FPortsDoc& FPortsDoc::Fact(const FString& FactId)
{
	const FPortsData& Data = FPortsData::Get();
	for (const FPortsValue& F : Data.Facts().GetItems())
	{
		if (F.Get(TEXT("id")).AsString() != FactId) continue;
		Text(FString::Printf(TEXT("<id>%s</>  %s"), *PortsUi::Esc(FactId), *PortsUi::Esc(F.Get(TEXT("text")).AsString())), TEXT("Ports.Body"), ETextJustify::Left, FMargin(0, 5, 0, 0));
		if (F.Get(TEXT("debate")).IsString()) Text(FString::Printf(TEXT("<debate>Historians disagree: %s</>"), *PortsUi::Esc(F.Get(TEXT("debate")).AsString())), TEXT("Ports.Small"), ETextJustify::Left, FMargin(0, 1));
		TArray<FString> Cites;
		for (const FPortsValue& S : F.Get(TEXT("sources")).GetItems()) Cites.Add(PortsUi::Esc(PortsUi::ShortCite(S.AsString())));
		Text(FString::Printf(TEXT("<cite>Source: %s</>"), *FString::Join(Cites, TEXT("; "))), TEXT("Ports.Small"), ETextJustify::Left, FMargin(0, 0, 0, 4));
		break;
	}
	return *this;
}

// .note in game.css: pale, with a red bar down the left.
FPortsDoc& FPortsDoc::Note(const TArray<FString>& FactIds, const FString& Title)
{
	const FPortsData& Data = FPortsData::Get();
	TArray<FString> Known;
	for (const FString& Id : FactIds)
	{
		if (Known.Contains(Id)) continue;
		if (Data.Facts().GetItems().ContainsByPredicate([&Id](const FPortsValue& F) { return F.Get(TEXT("id")).AsString() == Id; })) Known.Add(Id);
	}
	if (Known.Num() == 0) return *this;
	FPortsDoc Inner;
	Inner.Text(PortsUi::Esc(Title), TEXT("Ports.NoteTitle"), ETextJustify::Left, FMargin(0, 0, 0, 2));
	for (const FString& Id : Known) Inner.Fact(Id);
	FPortsBoxLook L = PortsUi::PlainLook(Hex(TEXT("#fff8e8")), 10, Hex(TEXT("#d8bc7c")), 1);
	L.LeftBar = Hex(TEXT("#b0261a"));
	L.LeftBarWidth = 5;
	return Boxed(L, Inner, FMargin(19, 10, 14, 10), FMargin(0, 12, 0, 2));
}

// .choice in game.css: pale, with a blue bar that turns red under the pointer.
FPortsDoc& FPortsDoc::Choice(const FString& MainMarkup, const FString& SubMarkup, const FString& Why, TFunction<void()> OnClick)
{
	return AddBuilt([MainMarkup, SubMarkup, Why, OnClick](float W)
	{
		const float TextWidth = Less(W, 18 + 12 + 30);
		const TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ PortsUi::Rich(MainMarkup, TEXT("Ports.Body"), ETextJustify::Left, true, TextWidth) ];
		if (!SubMarkup.IsEmpty()) Lines->AddSlot().AutoHeight()[ PortsUi::Rich(SubMarkup, TEXT("Ports.Small"), ETextJustify::Left, true, TextWidth) ];
		if (!Why.IsEmpty()) Lines->AddSlot().AutoHeight()[ PortsUi::Rich(FString::Printf(TEXT("<srisk>%s</>"), *PortsUi::Esc(Why)), TEXT("Ports.Small"), ETextJustify::Left, true, TextWidth) ];
		FPortsBoxLook L = PortsUi::PlainLook(Hex(TEXT("#fffdf6")), 12, Hex(TEXT("#d8bc7c")), 2);
		L.LeftBar = Why.IsEmpty() ? Hex(TEXT("#1d4a86")) : Hex(TEXT("#b9ad92"));
		L.LeftBarWidth = 6;
		FPortsBoxLook Hover = PortsUi::PlainLook(Hex(TEXT("#fff8e2")), 12, Hex(TEXT("#d9a82b")), 2);
		Hover.LeftBar = Hex(TEXT("#b0261a"));
		Hover.LeftBarWidth = 6;
		return SNew(SPortsButton)
			.Look(L).HoverLook(Hover)
			.Enabled(Why.IsEmpty())
			.DisabledOpacity(0.6f)
			.HoverShift(FVector2D(3, 0)).DownShift(FVector2D(3, 1))
			.OnClicked(OnClick)
			.Padding(FMargin(18, 8, 12, 8))
			[
				SNew(SBox).MinDesiredHeight(34)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Lines ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)[ SNew(SBox).WidthOverride(20)[ PortsUi::Rich(Why.IsEmpty() ? TEXT("→") : TEXT("✕"), TEXT("Ports.Body"), ETextJustify::Left, false) ] ]
				]
			];
	}, FMargin(0, 5));
}

FPortsDoc& FPortsDoc::Row(const TArray<TSharedRef<SWidget>>& Widgets, float Gap, EHorizontalAlignment Align)
{
	return AddBuilt([Widgets, Gap, Align](float W)
	{
		// A row that knows its width wraps correctly at once; otherwise it only learns its width a frame
		// after it appears, and until then asks for far too much height (the page visibly jumps).
		const TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).UseAllottedSize(W <= 0.f).PreferredSize(W).InnerSlotPadding(FVector2D(Gap, Gap)).HAlign(Align);
		for (const TSharedRef<SWidget>& Item : Widgets) Wrap->AddSlot().VAlign(VAlign_Center)[ Item ];
		return Wrap;
	}, FMargin(0, 4));
}

FPortsDoc& FPortsDoc::Buttons(const TArray<TSharedRef<SWidget>>& Widgets)
{
	return Row(Widgets, 10, HAlign_Right).Space(2);
}

// ---------- A page shown smaller ----------

void SPortsZoom::Construct(const FArguments& Args)
{
	Zoom = FMath::Max(0.05f, Args._Zoom);
	// The content is laid out at its own size and only drawn smaller. Laid out smaller instead, its text would be
	// measured at another size, break its lines elsewhere, and no longer fit the boxes made for it.
	const TSharedRef<SWidget> Content = Args._Content.Widget;
	Content->SetRenderTransformPivot(FVector2D::ZeroVector);
	Content->SetRenderTransform(FSlateRenderTransform(Zoom));
	ChildSlot[ Content ];
}

FVector2D SPortsZoom::ComputeDesiredSize(float) const
{
	return FVector2D(ChildSlot.GetWidget()->GetDesiredSize()) * Zoom;
}

void SPortsZoom::OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Children) const
{
	const TSharedRef<SWidget> Child = ChildSlot.GetWidget();
	if (Children.Accepts(Child->GetVisibility())) Children.AddWidget(Geometry.MakeChild(Child, FVector2D(Geometry.GetLocalSize()) / Zoom, FSlateLayoutTransform()));
}
