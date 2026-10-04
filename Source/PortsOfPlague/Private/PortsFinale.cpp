#include "PortsFinale.h"

#include "PortsUi.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

namespace
{
	const TCHAR* const PartNames[4] = { TEXT("Wealth"), TEXT("Family"), TEXT("Reputation"), TEXT("Balance") };
	// The Legacy bar's colours on the finale (.k-wealth and the rest in game.css).
	const TCHAR* const PartColors[4] = { TEXT("#d9a82b"), TEXT("#2f8f68"), TEXT("#3a73c0"), TEXT("#8a52b8") };

	float Clamp01(double X) { return static_cast<float>(FMath::Clamp(X, 0.0, 1.0)); }
	float EaseOut(double X) { const float T = Clamp01(X); return 1.f - FMath::Pow(1.f - T, 3.f); }
	// Overshoots a little before settling, like the web version's cubic-bezier(0.2, 1.4, 0.4, 1).
	float Spring(double X) { const float T = Clamp01(X); const float S = 1.70158f * 1.3f; const float U = T - 1.f; return 1.f + U * U * ((S + 1.f) * U + S); }
}

void SPortsFinale::Construct(const FArguments& Args)
{
	Model = Args._Model;
	OnMove = Args._OnMove;
}

FReply SPortsFinale::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2f At = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	for (const TPair<FSlateRect, int32>& Spot : Hot)
	{
		if (Spot.Key.ContainsPoint(FVector2D(At)))
		{
			if (OnMove) OnMove(Spot.Value);
			return FReply::Handled();
		}
	}
	return FReply::Handled();
}

int32 SPortsFinale::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
	if (!Model.IsValid()) return Layer;
	const FPortsFinaleModel& M = *Model;
	const FVector2f Size = G.GetLocalSize();
	const float W = Size.X, H = Size.Y, Cx = W * 0.5f;
	const double T = M.T;
	const FString Scene = M.SceneIds.IsValidIndex(M.Scene) ? M.SceneIds[M.Scene] : FString();
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	Brushes.Reset();
	Hot.Reset();

	const FLinearColor Cream = PortsUi::Color(TEXT("#fbefd2")), Gold = PortsUi::Color(TEXT("#f3d27a")), Dark(0.012f, 0.004f, 0.002f);
	const auto Faded = [](FLinearColor C, float Alpha) { C.A *= Alpha; return C; };
	const auto Round = [&](float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f) -> const FSlateBrush*
	{
		TSharedPtr<FSlateBrush> Brush = OutlineWidth > 0.f ? MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, Radius, Outline, OutlineWidth) : MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, Radius);
		Brushes.Add(Brush);
		return Brush.Get();
	};
	const auto Box = [&](int32 On, const FVector2f& At, const FVector2f& Extent, const FSlateBrush* Brush, const FLinearColor& Tint)
	{
		FSlateDrawElement::MakeBox(Out, On, G.ToPaintGeometry(Extent, FSlateLayoutTransform(At)), Brush, ESlateDrawEffect::None, Tint);
	};
	// Text is measured at the size it is really drawn at (the window's scale), so that words set side by side meet as they should.
	const float Scale = FMath::Max(G.Scale, 0.01f);
	const auto Width = [&](const FString& S, const FSlateFontInfo& Font) { return static_cast<float>(Measure->Measure(S, Font, Scale).X) / Scale; };
	const auto Tall = [&](const FSlateFontInfo& Font) { return static_cast<float>(Measure->GetMaxCharacterHeight(Font)); };
	// Text with its top left corner at a point; Grow enlarges it about its middle.
	const auto Write = [&](int32 On, const FString& S, const FSlateFontInfo& Font, FVector2f At, const FLinearColor& Tint, float Grow = 1.f)
	{
		if (Tint.A <= 0.004f || S.IsEmpty()) return;
		const FVector2f Extent(FVector2f(Measure->Measure(S, Font, Scale)) / Scale + FVector2f(2.f, 0.f));
		if (Grow != 1.f) At -= Extent * (Grow - 1.f) * 0.5f;
		FSlateDrawElement::MakeText(Out, On, G.ToPaintGeometry(Extent, FSlateLayoutTransform(Grow, At)), S, Font, ESlateDrawEffect::None, Tint);
	};
	const auto Centre = [&](int32 On, const FString& S, const FSlateFontInfo& Font, float X, float Y, const FLinearColor& Tint, float Grow = 1.f)
	{
		Write(On, S, Font, FVector2f(X - Width(S, Font) * 0.5f, Y), Tint, Grow);
	};
	// How far something that rises into place at a given second has come: its opacity, and how far below its place it still is.
	const auto Rise = [&](double At, float& Alpha, float& Drop, double Seconds = 0.8)
	{
		const float E = EaseOut((T - At) / Seconds);
		Alpha = E;
		Drop = 18.f * (1.f - E);
	};
	// A centred paragraph that wraps; <b>...</b> marks words drawn larger and in gold (.fin-text strong).
	const auto Paragraph = [&](int32 On, const FString& Markup, float X, float Y, float MaxWidth, float Pixels, float Alpha, bool bDraw = true) -> float
	{
		const FSlateFontInfo Plain = PortsUi::Serif(Pixels), Strong = PortsUi::Serif(Pixels * 1.15f, TEXT("Bold"));
		struct FWord { FString Text; bool bStrong; };
		TArray<FWord> Words;
		bool bStrong = false;
		FString Rest = Markup;
		while (!Rest.IsEmpty())
		{
			const int32 Tag = Rest.Find(bStrong ? TEXT("</b>") : TEXT("<b>"));
			const FString Piece = Tag == INDEX_NONE ? Rest : Rest.Left(Tag);
			TArray<FString> Bits;
			Piece.ParseIntoArray(Bits, TEXT(" "), true);
			for (const FString& Bit : Bits) Words.Add({ Bit, bStrong });
			if (Tag == INDEX_NONE) break;
			Rest = Rest.Mid(Tag + (bStrong ? 4 : 3));
			bStrong = !bStrong;
		}
		// The width of a space cannot be measured by itself (trailing space is not counted), so it is taken from between two letters.
		const float Space = Width(TEXT("a a"), Plain) - Width(TEXT("aa"), Plain), LineHeight = Pixels * 1.45f;
		TArray<TArray<FWord>> Lines;
		TArray<float> Widths;
		float Line = 0;
		Lines.AddDefaulted();
		for (const FWord& Word : Words)
		{
			const float Wide = Width(Word.Text, Word.bStrong ? Strong : Plain);
			if (Lines.Last().Num() && Line + Space + Wide > MaxWidth) { Widths.Add(Line); Lines.AddDefaulted(); Line = 0; }
			Line += (Lines.Last().Num() ? Space : 0.f) + Wide;
			Lines.Last().Add(Word);
		}
		Widths.Add(Line);
		if (bDraw) for (int32 i = 0; i < Lines.Num(); i++)
		{
			// Words of the same kind that follow one another are written as one piece, so the spaces between them are the font's own.
			TArray<FWord> Runs;
			for (const FWord& Word : Lines[i])
			{
				if (Runs.Num() && Runs.Last().bStrong == Word.bStrong) Runs.Last().Text += TEXT(" ") + Word.Text;
				else Runs.Add(Word);
			}
			float Whole = 0;
			for (const FWord& Run : Runs) Whole += Width(Run.Text, Run.bStrong ? Strong : Plain);
			Whole += Space * (Runs.Num() - 1);
			float At = X - Whole * 0.5f;
			for (const FWord& Run : Runs)
			{
				const FSlateFontInfo& Font = Run.bStrong ? Strong : Plain;
				Write(On, Run.Text, Font, FVector2f(At, Y + i * LineHeight - (Run.bStrong ? Pixels * 0.12f : 0.f)), Faded(Run.bStrong ? Gold : Cream, Alpha));
				At += Width(Run.Text, Font) + Space;
			}
		}
		return Lines.Num() * LineHeight;
	};
	// The heading of a scene: a small spaced line over a blackletter title (.fin-kicker, .fin-h).
	const auto Head = [&](const TCHAR* Kicker, const TCHAR* Title, float Y, bool bPlate = false)
	{
		const float In = EaseOut(T / 0.8);
		FSlateFontInfo Small = PortsUi::Caps(19.f, true);
		Small.LetterSpacing = 300;
		const FSlateFontInfo Big = PortsUi::Black(72.f);
		if (bPlate) Box(Layer + 2, FVector2f(Cx - Width(Title, Big) * 0.5f - 32.f, Y - 8.f), FVector2f(Width(Title, Big) + 64.f, 126.f), Round(16.f), FLinearColor(0.078f, 0.031f, 0.012f, 0.7f * In));
		Centre(Layer + 3, FString(Kicker).ToUpper(), Small, Cx, Y, Faded(Gold, 0.85f * In));
		Centre(Layer + 3, Title, Big, Cx, Y + 27.f, FLinearColor(0.23f, 0.04f, 0.02f, In));
		Centre(Layer + 4, Title, Big, Cx, Y + 24.f, Faded(Gold, In));
	};
	const auto Project = [&](const FVector& World, FVector2f& At)
	{
		const APlayerController* PC = M.Player.Get();
		FVector2D Screen;
		if (!PC || !PC->ProjectWorldLocationToScreen(World, Screen, true)) return false;
		At = FVector2f(Screen / FMath::Max(G.Scale, 0.01f));
		return true;
	};
	// A house's crest and name on a dark plate, centred on a point.
	const auto NamePlate = [&](const FPortsFinaleHouse& House, const FVector2f& At, float Alpha, const FString& Under = FString())
	{
		const FSlateFontInfo Font = PortsUi::Serif(21.f, TEXT("Bold")), Small = PortsUi::Serif(17.f, TEXT("Italic"));
		const float Wide = FMath::Max(Width(House.Name, Font) + 30.f, Under.IsEmpty() ? 0.f : Width(Under, Small)) + 24.f;
		const float High = Under.IsEmpty() ? 34.f : 58.f;
		Box(Layer + 2, FVector2f(At.X - Wide * 0.5f, At.Y), FVector2f(Wide, High), Round(9.f), FLinearColor(0.05f, 0.02f, 0.01f, 0.78f * Alpha));
		Box(Layer + 3, FVector2f(At.X - Wide * 0.5f, At.Y + High - 3.f), FVector2f(Wide, 3.f), Round(1.f), Faded(House.Color, Alpha));
		const float Inner = Width(House.Name, Font) + 28.f;
		Box(Layer + 3, FVector2f(At.X - Inner * 0.5f, At.Y + 7.f), FVector2f(21.f, 21.f), PortsUi::PictureBrush(TEXT("crest_") + House.Crest), FLinearColor(1, 1, 1, Alpha));
		Write(Layer + 3, House.Name, Font, FVector2f(At.X - Inner * 0.5f + 28.f, At.Y + 4.f), Faded(Cream, Alpha));
		if (!Under.IsEmpty()) Centre(Layer + 3, Under, Small, At.X, At.Y + 30.f, Faded(Cream, 0.75f * Alpha));
	};

	// Plates for several houses at once: where two would overlap, the later one moves down clear of the earlier.
	struct FPlate { int32 House; FVector2f At; float Alpha; FString Under; };
	const auto DrawPlates = [&](TArray<FPlate>& Plates)
	{
		const FSlateFontInfo Font = PortsUi::Serif(21.f, TEXT("Bold"));
		TArray<FSlateRect> Placed;
		for (FPlate& Plate : Plates)
		{
			const float Wide = Width(M.Houses[Plate.House].Name, Font) + 62.f, High = Plate.Under.IsEmpty() ? 38.f : 62.f;
			for (int32 Try = 0; Try < 8; Try++)
			{
				const FSlateRect Mine(Plate.At.X - Wide * 0.5f, Plate.At.Y, Plate.At.X + Wide * 0.5f, Plate.At.Y + High);
				const FSlateRect* Hit = Placed.FindByPredicate([&Mine](const FSlateRect& Other) { return FSlateRect::DoRectanglesIntersect(Mine, Other); });
				if (!Hit) break;
				Plate.At.Y = Hit->Bottom + 5.f;
			}
			Placed.Add(FSlateRect(Plate.At.X - Wide * 0.5f, Plate.At.Y, Plate.At.X + Wide * 0.5f, Plate.At.Y + High));
			NamePlate(M.Houses[Plate.House], Plate.At, Plate.Alpha, Plate.Under);
		}
	};

	// The stage darkens towards its edges and its foot, so the words stand clear of the board behind them.
	const float Veil = Scene == TEXT("honours") || Scene == TEXT("reckoning") ? 0.5f : Scene == TEXT("title") ? 0.42f : Scene == TEXT("vigil") ? 0.2f : 0.16f;
	Box(Layer, FVector2f::ZeroVector, Size, Round(0.f), FLinearColor(Dark.R, Dark.G, Dark.B, Veil));
	{
		TArray<FSlateGradientStop> Top, Foot;
		Top.Add(FSlateGradientStop(FVector2D::ZeroVector, FLinearColor(Dark.R, Dark.G, Dark.B, 0.78f)));
		Top.Add(FSlateGradientStop(FVector2D(0, 230), FLinearColor(Dark.R, Dark.G, Dark.B, 0.f)));
		FSlateDrawElement::MakeGradient(Out, Layer + 1, G.ToPaintGeometry(FVector2f(W, 230.f), FSlateLayoutTransform(FVector2f::ZeroVector)), Top, Orient_Horizontal, ESlateDrawEffect::None);
		Foot.Add(FSlateGradientStop(FVector2D::ZeroVector, FLinearColor(Dark.R, Dark.G, Dark.B, 0.f)));
		Foot.Add(FSlateGradientStop(FVector2D(0, 330), FLinearColor(Dark.R, Dark.G, Dark.B, 0.9f)));
		FSlateDrawElement::MakeGradient(Out, Layer + 1, G.ToPaintGeometry(FVector2f(W, 330.f), FSlateLayoutTransform(FVector2f(0, H - 330.f))), Foot, Orient_Horizontal, ESlateDrawEffect::None);
	}
	const float TextBase = H - 96.f;

	if (Scene == TEXT("title"))
	{
		// 1. Anno Domini 1353: the letters drop in one by one, and the four digits land like seals pressed into wax.
		float Alpha, Drop;
		FSlateFontInfo Small = PortsUi::Caps(19.f, true);
		Small.LetterSpacing = 300;
		Rise(0.2, Alpha, Drop);
		const float Top = H * 0.5f - 250.f;
		Centre(Layer + 3, TEXT("MCCCLIII"), Small, Cx, Top + Drop, Faded(Gold, 0.85f * Alpha));
		const FSlateFontInfo Letters = PortsUi::Black(128.f), Digits = PortsUi::Black(166.f);
		const FString Words = TEXT("Anno Domini"), Year = TEXT("1353");
		float X = Cx - Width(Words, Letters) * 0.5f;
		for (int32 i = 0; i < Words.Len(); i++)
		{
			const FString Ch = Words.Mid(i, 1);
			const double P = (T - (0.35 + i * 0.06)) / 0.9;
			const float E = Spring(P), A = Clamp01(P * 2.5);
			const FVector2f At(X, Top + 44.f - 77.f * (1.f - E));
			Write(Layer + 3, Ch, Letters, At + FVector2f(0, 4), FLinearColor(0.35f, 0.05f, 0.03f, A), 1.f + 0.5f * (1.f - E));
			Write(Layer + 4, Ch, Letters, At, Faded(Gold, A), 1.f + 0.5f * (1.f - E));
			X += Width(Ch, Letters);
		}
		X = Cx - Width(Year, Digits) * 0.5f;
		for (int32 i = 0; i < Year.Len(); i++)
		{
			const FString Ch = Year.Mid(i, 1);
			const double P = (T - (1.3 + i * 0.28)) / 0.55;
			const float E = Spring(P), A = Clamp01(P * 3.0);
			const FVector2f At(X, Top + 178.f);
			Write(Layer + 3, Ch, Digits, At + FVector2f(0, 4), FLinearColor(0.35f, 0.05f, 0.03f, A), 1.f + 2.f * (1.f - E));
			Write(Layer + 4, Ch, Digits, At, Faded(PortsUi::Color(TEXT("#ff9a6a")), A), 1.f + 2.f * (1.f - E));
			X += Width(Ch, Digits);
		}
		const FSlateFontInfo Lede = PortsUi::Serif(29.f, TEXT("Italic"));
		Rise(2.7, Alpha, Drop);
		Centre(Layer + 3, TEXT("The years of trade and plague are over."), Lede, Cx, Top + 384.f + Drop, Faded(Cream, Alpha));
		Centre(Layer + 3, TEXT("Now every house is judged."), Lede, Cx, Top + 424.f + Drop, Faded(Cream, Alpha));
		FSlateFontInfo Meta = PortsUi::Caps(17.6f, false);
		Meta.LetterSpacing = 120;
		Rise(3.4, Alpha, Drop);
		Centre(Layer + 3, M.MetaLine, Meta, Cx, Top + 478.f + Drop, Faded(Gold, 0.8f * Alpha));
	}
	else if (Scene == TEXT("toll"))
	{
		// 2. The Toll: the figures stand on the board at each house's home; here, their names and the dial.
		Head(TEXT("Your game"), TEXT("The Toll"), 34.f);
		// Each house in a row, as on the web version: its name, and a figure for every family member it began with.
		// The lost turn grey, marked with a cross, as they fall on the board.
		{
			const int32 N = M.Houses.Num();
			const float RowHigh = N > 4 ? 76.f : 86.f, Gap = 10.f, RowWide = 470.f;
			const float Top = H * 0.5f - (N * (RowHigh + Gap)) * 0.5f - 10.f;
			for (int32 i = 0; i < N; i++)
			{
				const FPortsFinaleHouse& House = M.Houses[i];
				float Alpha, Drop;
				Rise(0.3 + i * 0.12, Alpha, Drop);
				const FVector2f At(36.f, Top + i * (RowHigh + Gap) + Drop);
				Box(Layer + 2, At, FVector2f(RowWide, RowHigh), Round(10.f), FLinearColor(0.05f, 0.02f, 0.01f, 0.74f * Alpha));
				Box(Layer + 3, At + FVector2f(0, 6), FVector2f(6.f, RowHigh - 12.f), Round(3.f), Faded(House.Color, Alpha));
				Box(Layer + 3, At + FVector2f(18, 9), FVector2f(24, 24), PortsUi::PictureBrush(TEXT("crest_") + House.Crest), FLinearColor(1, 1, 1, Alpha));
				Write(Layer + 3, House.Name, PortsUi::Serif(21.f, TEXT("Bold")), At + FVector2f(50, 7), Faded(Cream, Alpha));
				const int32 Count = House.Alive + House.Lost;
				for (int32 k = 0; k < Count; k++)
				{
					const float Pop = Spring((T - (0.5 + k * 0.04)) / 0.45);
					if (Pop <= 0.f) continue;
					const int32 Doomed = k - House.Alive;
					const float Fall = Doomed >= 0 && House.FallAt.IsValidIndex(Doomed) ? Clamp01((T - House.FallAt[Doomed]) / 0.6) : 0.f;
					const FLinearColor Ink = Faded(FMath::Lerp(House.Color, PortsUi::Color(TEXT("#5d574b")), Fall), Alpha * (1.f - 0.2f * Fall));
					// A small robed figure: a round head on a body that widens to its foot.
					const float S = Pop * (1.f - 0.12f * Fall);
					const FVector2f Foot(At.X + 30.f + k * 27.f, At.Y + RowHigh - 8.f);
					Box(Layer + 3, Foot + FVector2f(-5.f * S, -30.f * S), FVector2f(10.f * S, 10.f * S), Round(5.f * S), Ink);
					Box(Layer + 3, Foot + FVector2f(-5.5f * S, -19.f * S), FVector2f(11.f * S, 10.f * S), Round(5.f * S), Ink);
					Box(Layer + 3, Foot + FVector2f(-8.f * S, -13.f * S), FVector2f(16.f * S, 13.f * S), Round(3.f * S), Ink);
					if (Fall > 0.3f)
					{
						const FLinearColor Cross = Faded(PortsUi::Color(TEXT("#ff8a6a")), Alpha * Clamp01((Fall - 0.3f) / 0.4f));
						Box(Layer + 4, Foot + FVector2f(-1.2f, -22.f), FVector2f(2.4f, 13.f), Round(1.f), Cross);
						Box(Layer + 4, Foot + FVector2f(-4.5f, -18.5f), FVector2f(9.f, 2.4f), Round(1.f), Cross);
					}
				}
			}
		}
		// The dial: the houses' losses filling in red, beside the band of the historians' estimate (a third to 60 percent).
		{
			float Alpha, Drop;
			Rise(0.7, Alpha, Drop);
			const FVector2f Mid(W - 210.f, H * 0.5f - 40.f + Drop);
			const float R = 107.f, Band = 131.f;
			const auto Arc = [&](float Radius, float From, float To, float Thick, const FLinearColor& Tint)
			{
				if (To <= From) return;
				TArray<FVector2f> Points;
				const int32 Steps = FMath::Max(2, FMath::CeilToInt32((To - From) * 96.f));
				for (int32 k = 0; k <= Steps; k++)
				{
					const float Angle = (FMath::Lerp(From, To, static_cast<float>(k) / Steps) - 0.25f) * 2.f * UE_PI;
					Points.Add(Mid + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
				}
				FSlateDrawElement::MakeLines(Out, Layer + 3, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, Tint, true, Thick);
			};
			Box(Layer + 2, Mid - FVector2f(168, 168), FVector2f(336, 336), Round(168.f), FLinearColor(0.05f, 0.02f, 0.01f, 0.62f * Alpha));
			Arc(R, 0.f, 1.f, 28.f, FLinearColor(1.f, 0.87f, 0.64f, 0.12f * Alpha));
			Arc(Band, 1.f / 3.f, 0.6f, 11.f, FLinearColor(0.55f, 0.045f, 0.022f, 0.8f * Alpha));
			const float Fill = 1.f - FMath::Square(1.f - Clamp01((T - M.TollStart) / FMath::Max(0.8, M.TollFall)));
			Arc(R, 0.f, FMath::Min(100, M.Pct) / 100.f * Fill, 28.f, Faded(PortsUi::Color(TEXT("#e0573f")), Alpha));
			Centre(Layer + 4, FString::Printf(TEXT("%d%%"), FMath::RoundToInt32(M.Pct * Fill)), PortsUi::Caps(60.f, true), Mid.X, Mid.Y - 46.f, Faded(Cream, Alpha));
			Centre(Layer + 4, TEXT("of the families lost"), PortsUi::Serif(18.f, TEXT("Italic")), Mid.X, Mid.Y + 22.f, Faded(Cream, 0.75f * Alpha));
			const FSlateFontInfo Key = PortsUi::Serif(17.f);
			Box(Layer + 3, FVector2f(Mid.X - 150.f, Mid.Y + 192.f), FVector2f(15, 15), Round(3.f), Faded(PortsUi::Color(TEXT("#e0573f")), Alpha));
			Write(Layer + 3, TEXT("Your houses"), Key, FVector2f(Mid.X - 128.f, Mid.Y + 186.f), Faded(Cream, 0.85f * Alpha));
			Box(Layer + 3, FVector2f(Mid.X - 150.f, Mid.Y + 222.f), FVector2f(15, 7), Round(3.f), FLinearColor(0.55f, 0.045f, 0.022f, 0.8f * Alpha));
			Write(Layer + 3, TEXT("Historians’ estimate for Europe"), Key, FVector2f(Mid.X - 128.f, Mid.Y + 212.f), Faded(Cream, 0.85f * Alpha));
		}
		const float Shown = EaseOut((T - M.TollTextAt) / 0.9);
		const float High = Paragraph(Layer + 3, M.LostLine, Cx, 0, 1000.f, 26.f, 0.f, false);
		Paragraph(Layer + 3, M.LostLine, Cx, TextBase - High + 14.f * (1.f - Shown), 1000.f, 26.f, Shown);
	}
	else if (Scene == TEXT("ships"))
	{
		// 3. Plague Ships: the infected cargoes sail again on the board; the tally counts them and the early arrivals are stamped.
		Head(TEXT("Your game"), TEXT("Plague Ships"), 30.f, true);
		float Alpha, Drop;
		Rise(0.5, Alpha, Drop);
		const float Counted = 1.f - FMath::Pow(1.f - Clamp01((T - 0.9) / FMath::Max(0.6, M.ReplayEnd - 0.9)), 3.f);
		const FString Number = FString::FromInt(FMath::RoundToInt32(M.Infected * Counted));
		const FString Label = M.Infected == 1 ? TEXT("INFECTED CARGO") : TEXT("INFECTED CARGOES");
		const FSlateFontInfo Big = PortsUi::Caps(80.f, true);
		FSlateFontInfo Small = PortsUi::Caps(13.6f, false);
		Small.LetterSpacing = 100;
		const float Wide = FMath::Max(Width(Label, Small), Width(Number, Big)) + 40.f;
		const FVector2f At(40.f, H * 0.5f - 80.f + Drop);
		Box(Layer + 2, At, FVector2f(Wide, 138.f), Round(16.f, FLinearColor(0.75f, 0.1f, 0.05f, 0.75f * Alpha), 2.f), FLinearColor(0.078f, 0.031f, 0.012f, 0.82f * Alpha));
		Centre(Layer + 3, Number, Big, At.X + Wide * 0.5f, At.Y + 8.f, Faded(PortsUi::Color(TEXT("#ff8a6a")), Alpha));
		Centre(Layer + 3, Label, Small, At.X + Wide * 0.5f, At.Y + 104.f, Faded(Cream, Alpha));

		// Each city the plague reached early gets a red stamp, one after another.
		if (M.Early.Num() && T >= M.ReplayEnd)
		{
			const FSlateFontInfo Heading = PortsUi::Caps(14.4f, true), Chip = PortsUi::Caps(17.f, true);
			const FString Title = TEXT("Plague arrived early");
			float Y = H * 0.5f - (M.Early.Num() * 44.f + 40.f) * 0.5f;
			const float Right = W - 40.f;
			Box(Layer + 2, FVector2f(Right - Width(Title, Heading) - 24.f, Y), FVector2f(Width(Title, Heading) + 24.f, 30.f), Round(8.f), FLinearColor(0.078f, 0.031f, 0.012f, 0.8f));
			Write(Layer + 3, Title, Heading, FVector2f(Right - Width(Title, Heading) - 12.f, Y + 4.f), Gold);
			Y += 40.f;
			for (int32 i = 0; i < M.Early.Num(); i++)
			{
				const double P = (T - (M.ReplayEnd + 0.3 + i * 0.45)) / 0.5;
				if (P <= 0) break;
				const float E = Spring(P), A = Clamp01(P * 3.0);
				const float ChipWide = Width(M.Early[i], Chip) + 26.f;
				const FVector2f ChipSize(ChipWide, 36.f);
				const FVector2f ChipAt(Right - ChipWide, Y + i * 44.f);
				const float Grow = 1.f + 1.4f * (1.f - E);
				FSlateDrawElement::MakeBox(Out, Layer + 3, G.ToPaintGeometry(ChipSize, FSlateLayoutTransform(Grow, ChipAt - ChipSize * (Grow - 1.f) * 0.5f)),
					Round(6.f, Faded(Gold, A), 2.f), ESlateDrawEffect::None, Faded(PortsUi::Color(TEXT("#8f1d14")), A));
				Write(Layer + 4, M.Early[i], Chip, ChipAt + FVector2f(13.f, 6.f), FLinearColor(1.f, 0.88f, 0.72f, A), Grow);
			}
			if (M.EarlyMore > 0 && T >= M.ReplayEnd + 0.3 + M.Early.Num() * 0.45)
			{
				const FString More = FString::Printf(TEXT("and %d more"), M.EarlyMore);
				const FSlateFontInfo Italic = PortsUi::Serif(18.f, TEXT("Italic"));
				Box(Layer + 2, FVector2f(Right - Width(More, Italic) - 16.f, Y + M.Early.Num() * 44.f), FVector2f(Width(More, Italic) + 16.f, 28.f), Round(6.f), FLinearColor(0.078f, 0.031f, 0.012f, 0.8f));
				Write(Layer + 3, More, Italic, FVector2f(Right - Width(More, Italic) - 8.f, Y + M.Early.Num() * 44.f + 2.f), Cream);
			}
		}
		const float Shown = EaseOut((T - M.CaptionAt) / 0.9);
		const float High = Paragraph(Layer + 3, M.ShipLine, Cx, 0, 960.f, 24.f, 0.f, false);
		const float Y = TextBase - High + 14.f * (1.f - Shown);
		Box(Layer + 2, FVector2f(Cx - 510.f, Y - 12.f), FVector2f(1020.f, High + 22.f), Round(14.f, Faded(Gold, 0.4f * Shown), 1.f), FLinearColor(0.078f, 0.031f, 0.012f, 0.85f * Shown));
		Paragraph(Layer + 3, M.ShipLine, Cx, Y, 960.f, 24.f, Shown);
	}
	else if (Scene == TEXT("vigil"))
	{
		// 4. A Matter of Conscience: a candle stands at each house's home, lit for those who took a stand.
		Head(TEXT("Your game"), TEXT("A Matter of Conscience"), 34.f);
		TArray<FPlate> Plates;
		for (int32 i = 0; i < M.Houses.Num(); i++)
		{
			float Alpha, Drop;
			Rise(0.6 + i * 0.45, Alpha, Drop, 0.9);
			FVector2f At;
			if (Project(M.Houses[i].Spot, At)) Plates.Add({ i, At + FVector2f(0, 64.f + Drop), M.Houses[i].bStood ? Alpha : Alpha * 0.8f, M.Houses[i].bStood ? TEXT("took a stand") : TEXT("did not take a stand") });
		}
		DrawPlates(Plates);
		float Alpha, Drop;
		Rise(1.2 + M.Houses.Num() * 0.45, Alpha, Drop);
		const float High = Paragraph(Layer + 3, M.StandLine, Cx, 0, 1000.f, 26.f, 0.f, false);
		Paragraph(Layer + 3, M.StandLine, Cx, TextBase - High + Drop, 1000.f, 26.f, Alpha);
	}
	else if (Scene == TEXT("honours"))
	{
		// 5. Honours of the Realm: a card for each, turned over one after another.
		Head(TEXT("Before the reckoning"), TEXT("Honours of the Realm"), 60.f);
		const int32 N = M.Honours.Num();
		const int32 Columns = N == 1 ? 1 : (N == 2 || N == 4) ? 2 : 3;
		const float CardWide = 360.f, CardHigh = 236.f, Gap = 20.f;
		const int32 Lines = (N + Columns - 1) / Columns;
		const float Left = Cx - (Columns * CardWide + (Columns - 1) * Gap) * 0.5f;
		const float Top = FMath::Max(230.f, (H - 90.f + 200.f) * 0.5f - (Lines * CardHigh + (Lines - 1) * Gap) * 0.5f);
		for (int32 i = 0; i < N; i++)
		{
			const FPortsFinaleHonour& A = M.Honours[i];
			const double P = (T - (0.8 + i * 0.7)) / 0.8;
			if (P <= 0) continue;
			// Turned in from edge-on, a little small, as the web version's fin-flip.
			const float E = FMath::Min(1.06f, Spring(P)), Alpha = Clamp01(P * 2.5);
			const float Squeeze = FMath::Max(0.02f, FMath::Sin(Clamp01(P) * UE_HALF_PI)), Grow = 0.8f + 0.2f * E;
			const FVector2f CardSize(CardWide * Squeeze * Grow, CardHigh * Grow);
			const FVector2f Mid(Left + (i % Columns) * (CardWide + Gap) + CardWide * 0.5f, Top + (i / Columns) * (CardHigh + Gap) + CardHigh * 0.5f);
			const FVector2f At = Mid - CardSize * 0.5f;
			const FLinearColor House = M.Houses.IsValidIndex(A.Winners[0]) ? M.Houses[A.Winners[0]].Color : FLinearColor::White;
			Box(Layer + 2, At - FVector2f(4, 4), CardSize + FVector2f(8, 8), Round(19.f), FLinearColor(0, 0, 0, 0.25f * Alpha));
			TArray<FSlateGradientStop> Paper;
			Paper.Add(FSlateGradientStop(FVector2D::ZeroVector, Faded(PortsUi::Color(TEXT("#fffaf0")), Alpha)));
			Paper.Add(FSlateGradientStop(FVector2D(0, CardSize.Y), Faded(PortsUi::Color(TEXT("#efdcae")), Alpha)));
			FSlateDrawElement::MakeGradient(Out, Layer + 3, G.ToPaintGeometry(CardSize, FSlateLayoutTransform(At)), Paper, Orient_Horizontal, ESlateDrawEffect::None, FVector4f(16, 16, 16, 16));
			Box(Layer + 4, At, CardSize, Round(16.f, Faded(PortsUi::Color(TEXT("#d9a82b")), Alpha), 3.f), FLinearColor::Transparent);
			Box(Layer + 4, At + FVector2f(10 * Squeeze, 3), FVector2f(CardSize.X - 20 * Squeeze, 7.f), Round(3.f), Faded(House, Alpha));
			if (Squeeze < 0.86f) continue;
			const float A1 = Alpha * Clamp01((Squeeze - 0.86f) / 0.14f);
			Box(Layer + 5, FVector2f(Mid.X - 42.f, At.Y + 18.f), FVector2f(84, 88), PortsUi::PictureBrush(FString::Printf(TEXT("honour_%d"), A.Icon)), FLinearColor(1, 1, 1, A1));
			Centre(Layer + 5, A.Title.ToUpper(), PortsUi::Caps(18.f, true), Mid.X, At.Y + 110.f, Faded(PortsUi::Color(TEXT("#b0261a")), A1));
			// Who holds it: one house, or two joined by "&".
			const FSlateFontInfo Who = PortsUi::Serif(19.f, TEXT("Bold"));
			FString Names;
			for (int32 k = 0; k < A.Winners.Num(); k++) Names += (k ? TEXT("  &  ") : TEXT("")) + M.Houses[A.Winners[k]].Name;
			const float Fit = FMath::Min(1.f, (CardWide - 24.f) / FMath::Max(1.f, Width(Names, Who)));
			Centre(Layer + 5, Names, PortsUi::Serif(19.f * Fit, TEXT("Bold")), Mid.X, At.Y + 142.f, Faded(PortsUi::Color(TEXT("#26190a")), A1));
			Centre(Layer + 5, A.What, PortsUi::Serif(18.f, TEXT("Italic")), Mid.X, At.Y + 176.f, Faded(PortsUi::Color(TEXT("#4d3a22")), A1));
			for (int32 k = 0; k < A.Winners.Num(); k++)
			{
				const float Spread = (k - (A.Winners.Num() - 1) * 0.5f) * 30.f;
				Box(Layer + 5, FVector2f(Mid.X - 10.f + Spread, At.Y + 206.f), FVector2f(20, 20), PortsUi::PictureBrush(TEXT("crest_") + M.Houses[A.Winners[k]].Crest), FLinearColor(1, 1, 1, A1));
			}
		}
	}
	else if (Scene == TEXT("reckoning"))
	{
		// 6. The Reckoning: the Legacy scores from last place to first, while the same bars rise as towers on the board.
		Head(TEXT("Final Legacy"), TEXT("The Reckoning"), 24.f);
		{
			const FSlateFontInfo Key = PortsUi::Serif(17.f);
			float Total = 0;
			for (int32 k = 0; k < 4; k++) Total += Width(PartNames[k], Key) + 22.f + (k ? 18.f : 0.f);
			float X = Cx - Total * 0.5f;
			for (int32 k = 0; k < 4; k++)
			{
				Box(Layer + 3, FVector2f(X, 149.f), FVector2f(15, 15), Round(3.f), PortsUi::Color(PartColors[k]));
				Write(Layer + 3, PartNames[k], Key, FVector2f(X + 22.f, 143.f), Cream);
				X += Width(PartNames[k], Key) + 40.f;
			}
		}
		{
			TArray<FPlate> Plates;
			for (const FPortsFinaleRow& R : M.Rows)
			{
				FVector2f At;
				if (T >= R.At && Project(M.Houses[R.House].Spot, At)) Plates.Add({ R.House, At + FVector2f(0, 46.f), Clamp01((T - R.At) / 0.5), FString() });
			}
			DrawPlates(Plates);
		}
		const int32 N = M.Rows.Num();
		const float RowHigh = N > 4 ? 58.f : 68.f, Gap = N > 4 ? 8.f : 12.f, RowsWide = 1150.f;
		const float Top = H - 96.f - N * (RowHigh + Gap);
		const float Left = Cx - RowsWide * 0.5f;
		for (int32 i = 0; i < N; i++)
		{
			const FPortsFinaleRow& R = M.Rows[i];
			const FPortsFinaleHouse& House = M.Houses[R.House];
			const double Since = T - R.At;
			if (Since <= 0) continue;
			const float In = Spring(Since / 0.6), Alpha = Clamp01(Since / 0.5);
			const bool bWin = R.Place == 1;
			const FVector2f At(Left - 40.f * (1.f - In), Top + i * (RowHigh + Gap));
			const FVector2f RowSize(RowsWide, RowHigh);
			if (bWin) Box(Layer + 2, At - FVector2f(3, 3), RowSize + FVector2f(6, 6), Round(16.f), Faded(PortsUi::Color(TEXT("#d9a82b")), 0.5f * Alpha));
			Box(Layer + 3, At, RowSize, Round(14.f, bWin ? Faded(PortsUi::Color(TEXT("#d9a82b")), Alpha) : Faded(Gold, 0.18f * Alpha), 2.f),
				bWin ? FLinearColor(0.42f, 0.3f, 0.1f, 0.82f * Alpha) : FLinearColor(0.12f, 0.07f, 0.04f, 0.8f * Alpha));
			Box(Layer + 4, At + FVector2f(0, 6), FVector2f(8.f, RowHigh - 12.f), Round(3.f), Faded(House.Color, Alpha));
			Centre(Layer + 4, FString::FromInt(R.Place), PortsUi::Caps(32.f, true), At.X + 36.f, At.Y + RowHigh * 0.5f - 21.f, Faded(Gold, Alpha));
			Box(Layer + 4, FVector2f(At.X + 68.f, At.Y + RowHigh * 0.5f - 15.f), FVector2f(30, 30), PortsUi::PictureBrush(TEXT("crest_") + House.Crest), FLinearColor(1, 1, 1, Alpha));
			Write(Layer + 4, House.Name, PortsUi::Serif(20.f, TEXT("Bold")), FVector2f(At.X + 108.f, At.Y + RowHigh * 0.5f - 25.f), Faded(Cream, Alpha));
			Write(Layer + 4, FString::Printf(TEXT("of %s"), *House.Home), PortsUi::Serif(16.f), FVector2f(At.X + 108.f, At.Y + RowHigh * 0.5f + 1.f), Faded(Cream, 0.75f * Alpha));
			// The bar: one piece for each part of the Legacy, growing in turn.
			const float BarLeft = At.X + 330.f, BarWide = RowsWide - 330.f - 96.f, BarHigh = RowHigh - 24.f;
			float X = BarLeft;
			for (int32 k = 0; k < 4; k++)
			{
				const float Full = BarWide * R.Parts[k] / FMath::Max(1, M.MaxTotal);
				const float Grown = Full * EaseOut((Since - 0.35 - k * 0.3) / 0.7);
				if (Grown > 0.5f)
				{
					Box(Layer + 4, FVector2f(X, At.Y + 12.f), FVector2f(Grown, BarHigh), Round(k == 0 ? 6.f : 2.f), Faded(PortsUi::Color(PartColors[k]), Alpha));
					const FString Value = FString::FromInt(R.Parts[k]);
					const FSlateFontInfo Small = PortsUi::Caps(15.f, true);
					if (R.Parts[k] > 0 && Grown > Width(Value, Small) + 8.f) Centre(Layer + 5, Value, Small, X + Grown * 0.5f, At.Y + RowHigh * 0.5f - 10.f, FLinearColor(0.08f, 0.04f, 0.01f, Alpha));
				}
				X += Grown;
			}
			const float Counted = 1.f - FMath::Pow(1.f - Clamp01((Since - 0.35) / 1.3), 3.f);
			const FSlateFontInfo TotalFont = PortsUi::Caps(34.f, true);
			const FString TotalText = FString::FromInt(FMath::RoundToInt32(R.Total * Counted));
			Write(Layer + 4, TotalText, TotalFont, FVector2f(At.X + RowsWide - 18.f - Width(TotalText, TotalFont), At.Y + RowHigh * 0.5f - 22.f), Faded(bWin ? Gold : Cream, Alpha));
		}
		if (T >= M.SuspenseAt && T < M.FlashAt)
		{
			// The pause before the winner: the question fills the middle of the screen, on a dark plate ringed with gold, and throbs.
			const float In = Spring((T - M.SuspenseAt) / 0.6), A = Clamp01((T - M.SuspenseAt) / 0.4);
			const float Throb = 0.5f + 0.5f * FMath::Sin(static_cast<float>(T - M.SuspenseAt) * 5.5f);
			const FString Line = TEXT("And the greatest Legacy belongs to…");
			const FSlateFontInfo Font = PortsUi::Black(74.f);
			const float Wide = Width(Line, Font) + 110.f, High = 150.f;
			const FVector2f At(Cx - Wide * 0.5f, FMath::Max(190.f, Top * 0.5f + 20.f));
			const float Grow = 0.86f + 0.14f * In + 0.012f * Throb;
			const FVector2f Plate(Wide, High);
			FSlateDrawElement::MakeBox(Out, Layer + 5, G.ToPaintGeometry(Plate + FVector2f(16, 16), FSlateLayoutTransform(Grow, At - FVector2f(8, 8) - (Plate + FVector2f(16, 16)) * (Grow - 1.f) * 0.5f)),
				Round(34.f), ESlateDrawEffect::None, FLinearColor(1.f, 0.78f, 0.3f, (0.16f + 0.2f * Throb) * A));
			FSlateDrawElement::MakeBox(Out, Layer + 6, G.ToPaintGeometry(Plate, FSlateLayoutTransform(Grow, At - Plate * (Grow - 1.f) * 0.5f)),
				Round(28.f, Faded(Gold, A), 3.f), ESlateDrawEffect::None, FLinearColor(0.06f, 0.02f, 0.01f, 0.9f * A));
			Centre(Layer + 7, Line, Font, Cx, At.Y + 32.f, FLinearColor(0.3f, 0.05f, 0.02f, A), Grow);
			Centre(Layer + 8, Line, Font, Cx, At.Y + 28.f, Faded(FMath::Lerp(Gold, FLinearColor(1.f, 0.96f, 0.8f), Throb * 0.6f), A), Grow);
		}
		// A flash of gold light as the winner is revealed.
		const float Flash = T >= M.FlashAt ? 1.f - Clamp01((T - M.FlashAt) / 1.1) : 0.f;
		if (Flash > 0) Box(Layer + 9, FVector2f::ZeroVector, Size, Round(0.f), FLinearColor(1.f, 0.9f, 0.55f, 0.55f * Flash * Flash));
	}
	else if (Scene == TEXT("crown"))
	{
		// 7. Victory: the crown comes down over the winner's city on the board; here, the proclamation.
		float Alpha, Drop;
		FSlateFontInfo Small = PortsUi::Caps(19.f, true);
		Small.LetterSpacing = 300;
		const float Top = H - 372.f;
		Box(Layer + 2, FVector2f(Cx - 620.f, Top - 26.f), FVector2f(1240.f, 322.f), Round(26.f), FLinearColor(0.05f, 0.02f, 0.01f, 0.62f * Clamp01((T - 0.7) / 0.8)));
		Rise(1.4, Alpha, Drop);
		Centre(Layer + 3, TEXT("VICTORY · ANNO DOMINI 1353"), Small, Cx, Top + Drop, Faded(Gold, 0.85f * Alpha));
		const FSlateFontInfo Name = PortsUi::Black(FMath::Min(104.f, 104.f * 1500.f / FMath::Max(1.f, Width(M.WinnerNames, PortsUi::Black(104.f)))));
		const float NameIn = Spring((T - 0.9) / 0.9), NameAlpha = Clamp01((T - 0.9) / 0.4);
		Centre(Layer + 3, M.WinnerNames, Name, Cx, Top + 40.f, FLinearColor(0.3f, 0.05f, 0.02f, NameAlpha), 1.f + 0.6f * (1.f - NameIn));
		Centre(Layer + 4, M.WinnerNames, Name, Cx, Top + 36.f, Faded(Gold, NameAlpha), 1.f + 0.6f * (1.f - NameIn));
		Rise(2.1, Alpha, Drop);
		Centre(Layer + 3, M.WinLine, PortsUi::Serif(28.f, TEXT("Italic")), Cx, Top + 168.f + Drop, Faded(Cream, Alpha));
		Rise(2.5, Alpha, Drop);
		const FString Score = FString::FromInt(M.TopTotal), Words = TEXT(" Legacy points");
		const FSlateFontInfo ScoreFont = PortsUi::Caps(46.f, true), WordsFont = PortsUi::Caps(24.f, false);
		const float Whole = Width(Score, ScoreFont) + Width(Words, WordsFont);
		Write(Layer + 3, Score, ScoreFont, FVector2f(Cx - Whole * 0.5f, Top + 212.f + Drop), Faded(PortsUi::Color(TEXT("#ff9a6a")), Alpha));
		Write(Layer + 3, Words, WordsFont, FVector2f(Cx - Whole * 0.5f + Width(Score, ScoreFont), Top + 236.f + Drop), Faded(Cream, Alpha));
	}

	// The controls: back, the scenes, pause, next (its gold line shows when the show moves on by itself), and straight to the results.
	{
		const float BarY = H - 62.f, High = 44.f;
		const FSlateFontInfo ButtonFont = PortsUi::Caps(18.f, true), DotFont = PortsUi::Caps(12.5f, false);
		const auto Button = [&](float X, float Wide, const FString& Label, int32 Code, bool bRed, bool bOn)
		{
			const FVector2f At(X, BarY);
			if (bRed)
			{
				TArray<FSlateGradientStop> Stops;
				Stops.Add(FSlateGradientStop(FVector2D::ZeroVector, PortsUi::Color(TEXT("#c93a2c"))));
				Stops.Add(FSlateGradientStop(FVector2D(0, High), PortsUi::Color(TEXT("#8a1a10"))));
				FSlateDrawElement::MakeGradient(Out, Layer + 7, G.ToPaintGeometry(FVector2f(Wide, High), FSlateLayoutTransform(At)), Stops, Orient_Horizontal, ESlateDrawEffect::None, FVector4f(22, 22, 22, 22));
				const float Done = Clamp01(M.T / FMath::Max(0.1, M.Hold));
				Box(Layer + 8, At + FVector2f(14.f, High - 8.f), FVector2f((Wide - 28.f) * Done, 4.f), Round(2.f), Faded(Gold, M.bPaused ? 0.45f : 1.f));
			}
			Box(Layer + 8, At, FVector2f(Wide, High), Round(22.f, Faded(Gold, bRed ? 1.f : 0.6f), 2.f), FLinearColor::Transparent);
			Centre(Layer + 9, Label, ButtonFont, X + Wide * 0.5f, BarY + 9.f, Faded(bRed ? PortsUi::Color(TEXT("#fff6e0")) : Gold, bOn ? 1.f : 0.3f));
			if (bOn) Hot.Add(TPair<FSlateRect, int32>(FSlateRect(X, BarY, X + Wide, BarY + High), Code));
		};
		Button(22.f, 50.f, TEXT("‹"), -1, false, M.Scene > 0);
		const float SkipWide = Width(TEXT("Results »"), ButtonFont) + 36.f, NextWide = Width(TEXT("Next ›"), ButtonFont) + 40.f;
		Button(W - 22.f - SkipWide, SkipWide, TEXT("Results »"), 99, false, true);
		Button(W - 30.f - SkipWide - NextWide, NextWide, TEXT("Next ›"), 1, true, true);
		Button(W - 38.f - SkipWide - NextWide - 54.f, 54.f, M.bPaused ? TEXT("▶") : TEXT("II"), 0, false, true);
		float Total = 0;
		for (const FString& Label : M.SceneLabels) Total += Width(Label.ToUpper(), DotFont) + 40.f + 6.f;
		float X = Cx - Total * 0.5f - 60.f;
		for (int32 i = 0; i < M.SceneLabels.Num(); i++)
		{
			const FString Label = M.SceneLabels[i].ToUpper();
			const float Wide = Width(Label, DotFont) + 40.f;
			const bool bHere = i == M.Scene, bSeen = i < M.Scene;
			const FLinearColor Ink = bHere ? PortsUi::Color(TEXT("#1c0c05")) : bSeen ? Faded(Gold, 0.85f) : Faded(Cream, 0.5f);
			Box(Layer + 7, FVector2f(X, BarY + 6.f), FVector2f(Wide, 32.f), Round(16.f, bHere ? Gold : Faded(Gold, 0.25f), 1.f), bHere ? Gold : FLinearColor(0.05f, 0.02f, 0.01f, 0.45f));
			Box(Layer + 8, FVector2f(X + 11.f, BarY + 17.5f), FVector2f(9, 9), Round(4.5f), Ink);
			Write(Layer + 8, Label, DotFont, FVector2f(X + 27.f, BarY + 13.f), Ink);
			Hot.Add(TPair<FSlateRect, int32>(FSlateRect(X, BarY, X + Wide, BarY + 44.f), 1000 + i));
			X += Wide + 6.f;
		}
	}
	return Layer + 9;
}
