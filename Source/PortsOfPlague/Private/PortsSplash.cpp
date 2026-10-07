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

	// The black background and Epic's logo, whole and unaltered, at one size.
	class SPortsSplashArt : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPortsSplashArt) : _Blank(false) {}
			SLATE_ARGUMENT(bool, Blank)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			bBlank = Args._Blank;
			Started = FSlateApplication::Get().GetCurrentTime();
			Logo = PortsUi::PictureBrush(TEXT("splash_logo_white"));
			Fill = MakeShared<FSlateColorBrush>(FLinearColor::White);
			RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([](double, float) { return EActiveTimerReturnType::Continue; }));
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16, 9); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override
		{
			const double T = FSlateApplication::Get().GetCurrentTime() - Started;
			const FVector2f Size = Geometry.GetLocalSize();
			// How much of this screen is still there: it thins as a whole while it fades onto the start screen.
			const float Here = Style.GetColorAndOpacityTint().A;
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), Fill.Get(), ESlateDrawEffect::None, FLinearColor(0, 0, 0, Here));
			if (bBlank) return Layer;

			// Epic's guideline: on a wide screen the logo is half the screen's height, in the middle. On a tall
			// one the symbol is a third of the screen's width (the symbol is the logo's full width).
			float High = Size.Y * 0.5f;
			if (Size.X < Size.Y) High = Size.X * 0.33f / LogoAspect;
			const FVector2f LogoSize(High * LogoAspect, High);
			const FVector2f At((Size.X - LogoSize.X) * 0.5f, (Size.Y - LogoSize.Y) * 0.5f);
			// The logo fades in, and before the black lifts it has already faded back into it.
			const float End = SPortsSplash::Seconds() - SPortsSplash::FadeSeconds;
			const float Opacity = Span(T, 0.15f, 0.7f) * (1.f - Span(T, End - 0.6f, End)) * Here;
			if (Opacity > 0.003f) FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(LogoSize, FSlateLayoutTransform(At)), Logo, ESlateDrawEffect::None, FLinearColor(1, 1, 1, Opacity));
			return Layer + 1;
		}

	private:
		bool bBlank = false;
		double Started = 0;
		const FSlateBrush* Logo = nullptr;
		TSharedPtr<FSlateColorBrush> Fill;
	};
}

void SPortsSplash::Construct(const FArguments& Args)
{
	const double Started = FSlateApplication::Get().GetCurrentTime();
	const bool bBlank = Args._Blank || Args._Still;
	const bool bStill = Args._Still;
	const float Total = Seconds(bBlank);
	// The trademark notice Epic requires wherever its logo is shown on a splash screen, word for word.
	const FText Notice = FText::FromString(TEXT("Unreal®, Unreal Engine®, and the Unreal Engine® logo are trademarks or registered trademarks of Epic Games, Inc. in the United States of America and elsewhere. Other brands or product names are the trademarks of their respective owners."));
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SPortsSplashArt).Blank(bBlank) ]
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
				.ColorAndOpacity_Lambda([Started, Total]()
				{
					const double T = FSlateApplication::Get().GetCurrentTime() - Started;
					const float Shown = Span(T, 0.15f, 0.7f) * (1.f - Span(T, Total - FadeSeconds - 0.6f, Total - FadeSeconds));
					return FSlateColor(FLinearColor(1, 1, 1, 0.8f * Shown));
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
		// The logo and its notice go first, into the black; then the black itself lifts slowly off the start screen.
		SetRenderOpacity(1.f - Span(T, Total - FadeSeconds, Total));
		if (T <= Total) return EActiveTimerReturnType::Continue;
		// Gone: what is behind it can be clicked again.
		SetVisibility(EVisibility::Collapsed);
		return EActiveTimerReturnType::Stop;
	}));
}
