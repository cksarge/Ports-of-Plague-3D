#include "PortsSplash.h"

#include "PortsUi.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// The logo's own proportions (Epic's file is 2372 by 2880).
	constexpr float LogoAspect = 2372.f / 2880.f;

	float Ease(float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * (3.f - 2.f * T); }
	float Span(double Now, float From, float To) { return Ease(static_cast<float>((Now - From) / FMath::Max(0.01f, To - From))); }

	// The background and the logo. The logo is always the whole of Epic's shape at one size; the custom
	// version only chooses which part of it has been drawn so far, and in which of its colours.
	class SPortsSplashArt : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPortsSplashArt) : _Custom(false), _Blank(false) {}
			SLATE_ARGUMENT(bool, Custom)
			SLATE_ARGUMENT(bool, Blank)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			bCustom = Args._Custom;
			bBlank = Args._Blank;
			Started = FSlateApplication::Get().GetCurrentTime();
			White = PortsUi::PictureBrush(TEXT("splash_logo_white"));
			Gold = PortsUi::PictureBrush(TEXT("splash_logo_gold"));
			Hot = PortsUi::PictureBrush(TEXT("splash_logo_hot"));
			Edge = PortsUi::PictureBrush(TEXT("splash_logo_edge"));
			Fill = MakeShared<FSlateColorBrush>(FLinearColor::White);
			RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([](double, float) { return EActiveTimerReturnType::Continue; }));
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16, 9); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override
		{
			const double T = FSlateApplication::Get().GetCurrentTime() - Started;
			const FVector2f Size = Geometry.GetLocalSize();
			// Black for Epic's own logo; for the gilded one, the near-black brown of the game's table.
			// How much of this screen is still there: it thins as a whole while it fades onto the start screen.
			const float Here = Style.GetColorAndOpacityTint().A;
			FLinearColor Ground = bCustom ? FLinearColor(0.012f, 0.007f, 0.004f, 1.f) : FLinearColor::Black;
			Ground.A = Here;
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), Fill.Get(), ESlateDrawEffect::None, Ground);
			if (bBlank) return Layer;

			// Epic's guideline: on a wide screen the logo is half the screen's height, in the middle. On a tall
			// one the symbol is a third of the screen's width (the symbol is the logo's full width).
			float High = Size.Y * 0.5f;
			if (Size.X < Size.Y) High = Size.X * 0.33f / LogoAspect;
			const FVector2f LogoSize(High * LogoAspect, High);
			const FVector2f At((Size.X - LogoSize.X) * 0.5f, (Size.Y - LogoSize.Y) * 0.5f);
			// One of the logo's pictures, whole, or only the part of it inside a window (given in parts of the logo).
			// Before the dark lifts, the logo has already faded into it.
			const float Leaving = 1.f - Span(T, SPortsSplash::Seconds(bCustom) - SPortsSplash::FadeSeconds - 0.6f, SPortsSplash::Seconds(bCustom) - SPortsSplash::FadeSeconds);
			const auto Draw = [&](const FSlateBrush* Brush, float Opacity, float Left = 0, float Top = 0, float Right = 1, float Bottom = 1)
			{
				Opacity *= Leaving * Here;
				if (Opacity <= 0.003f || Right <= Left || Bottom <= Top) return;
				const bool bPart = Left > 0 || Top > 0 || Right < 1 || Bottom < 1;
				if (bPart) Out.PushClip(FSlateClippingZone(Geometry.ToPaintGeometry(FVector2f(LogoSize.X * (Right - Left), LogoSize.Y * (Bottom - Top)), FSlateLayoutTransform(At + FVector2f(LogoSize.X * Left, LogoSize.Y * Top)))));
				FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(LogoSize, FSlateLayoutTransform(At)), Brush, ESlateDrawEffect::None, FLinearColor(1, 1, 1, Opacity));
				if (bPart) Out.PopClip();
			};

			if (!bCustom)
			{
				Draw(White, Span(T, 0.15f, 0.7f));
				return Layer + 1;
			}
			// The outline is drawn from left to right, as with a pen; the gold then rises through it from the
			// foot, a pale line at its edge; the outline fades into the finished logo, and a gleam crosses it once.
			const float Traced = Span(T, 0.2f, 1.5f);
			const float Gilded = Span(T, 1.2f, 2.7f);
			Draw(Edge, 1.f - Span(T, 2.5f, 3.1f), 0, 0, Traced, 1);
			Draw(Gold, 1.f, 0, 1.f - Gilded, 1, 1);
			if (Gilded > 0.f && Gilded < 1.f) Draw(Hot, 1.f, 0, 1.f - Gilded - 0.012f, 1, 1.f - Gilded + 0.012f);
			const float Gleam = static_cast<float>((T - 3.1) / 0.9);
			if (Gleam > 0.f && Gleam < 1.f)
			{
				const float Middle = -0.15f + 1.3f * Gleam;
				Draw(Hot, 0.85f * FMath::Sin(Gleam * UE_PI), FMath::Max(0.f, Middle - 0.07f), 0, FMath::Min(1.f, Middle + 0.07f), 1);
			}
			return Layer + 1;
		}

	private:
		bool bCustom = false;
		bool bBlank = false;
		double Started = 0;
		const FSlateBrush* White = nullptr;
		const FSlateBrush* Gold = nullptr;
		const FSlateBrush* Hot = nullptr;
		const FSlateBrush* Edge = nullptr;
		TSharedPtr<FSlateColorBrush> Fill;
	};
}

void SPortsSplash::Construct(const FArguments& Args)
{
	const bool bCustom = Args._Custom;
	const double Started = FSlateApplication::Get().GetCurrentTime();
	const bool bBlank = Args._Blank || Args._Still;
	const bool bStill = Args._Still;
	const float Total = Seconds(bCustom, bBlank);
	// The trademark notice Epic requires wherever its logo is shown on a splash screen, word for word.
	const FText Notice = FText::FromString(TEXT("Unreal®, Unreal Engine®, and the Unreal Engine® logo are trademarks or registered trademarks of Epic Games, Inc. in the United States of America and elsewhere. Other brands or product names are the trademarks of their respective owners."));
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SPortsSplashArt).Custom(bCustom).Blank(bBlank) ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(40, 0, 40, 44)
		[
			SNew(SBox).MaxDesiredWidth(980)
			[
				SNew(STextBlock)
				.Text(Notice)
				.Font(PortsUi::Serif(18))
				.Justification(ETextJustify::Center)
				.AutoWrapText(true)
				.Visibility(bBlank ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
				// The notice comes and goes with the logo.
				.ColorAndOpacity_Lambda([bCustom, Started, Total]()
				{
					const double T = FSlateApplication::Get().GetCurrentTime() - Started;
					const float Shown = Span(T, 0.15f, 0.7f) * (1.f - Span(T, Total - FadeSeconds - 0.6f, Total - FadeSeconds));
					return FSlateColor(bCustom ? FLinearColor(0.93f, 0.85f, 0.66f, 0.85f * Shown) : FLinearColor(1, 1, 1, 0.8f * Shown));
				})
			]
		]
	];
	SetVisibility(EVisibility::Visible);
	// The whole screen fades away at the end, to the start screen behind it.
	SetRenderOpacity(1.f);
	if (bStill) return;
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this, Started, Total](double, float)
	{
		const double T = FSlateApplication::Get().GetCurrentTime() - Started;
		// The logo and its notice go first, into the dark; then the dark itself lifts slowly off the start screen.
		SetRenderOpacity(1.f - Span(T, Total - FadeSeconds, Total));
		if (T <= Total) return EActiveTimerReturnType::Continue;
		// Gone: what is behind it can be clicked again.
		SetVisibility(EVisibility::Collapsed);
		return EActiveTimerReturnType::Stop;
	}));
}
