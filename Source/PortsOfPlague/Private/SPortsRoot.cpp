#include "SPortsRoot.h"

#include "PortsCameraPawn.h"
#include "PortsMapActor.h"
#include "PortsUi.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SOverlay.h"

using PortsUi::Color;

// ---------- Map lettering ----------

// Route values sit in a pale disc ringed in the route's colour (.route-value in game.css).
SPortsMapLabels::SPortsMapLabels()
	: SeaBadge(Color(TEXT("#fbf5e4")), 100.f, Color(TEXT("#10375c")), 1.5f)
	, LandBadge(Color(TEXT("#fbf5e4")), 100.f, Color(TEXT("#6b4423")), 1.5f)
	, Chip(FLinearColor::White, 5.f)
	, FamilyDisc(Color(TEXT("#fff8e8")), 100.f, Color(TEXT("#1a1208")), 1.5f)
{
	// Fully rounded ends: a square box becomes a disc.
	SeaBadge.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	FamilyDisc.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	LandBadge.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
}

void SPortsMapLabels::Construct(const FArguments& Args)
{
	Map = Args._Map;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SPortsMapLabels::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const APortsMapActor* Board = Map.Get();
	const APlayerController* PC = Board && Board->GetWorld() ? Board->GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !Board) return LayerId;

	const float Scale = FMath::Max(AllottedGeometry.Scale, 0.01f);
	const FVector2D Bounds = AllottedGeometry.GetLocalSize();
	const APortsCameraPawn* Pawn = Cast<APortsCameraPawn>(PC->GetPawn());
	// Lettering grows a little as the camera comes closer.
	const float Grow = Pawn ? 1.f + 0.22f * static_cast<float>(Pawn->GetZoom() - 1.0) : 1.f;
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const auto ToLocal = [&](const FVector& World, FVector2D& Out)
	{
		FVector2D Screen;
		if (!PC->ProjectWorldLocationToScreen(World, Screen, true)) return false;
		Out = Screen / Scale;
		return Out.X > -200 && Out.Y > -100 && Out.X < Bounds.X + 200 && Out.Y < Bounds.Y + 100;
	};
	const auto Box = [&](int32 Layer, const FVector2D& TopLeft, const FVector2D& Size, const FSlateBrush* Brush, const FLinearColor& Tint)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)), Brush, ESlateDrawEffect::None, Tint);
	};
	const auto Text = [&](int32 Layer, const FVector2D& TopLeft, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Tint)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(Measure->Measure(String, Font), FSlateLayoutTransform(TopLeft)), String, Font, ESlateDrawEffect::None, Tint);
	};

	const FLinearColor Ink = Color(TEXT("#26190a"));
	const FLinearColor Pale = Color(TEXT("#fbf3de"));

	// Route values.
	const FSlateFontInfo BadgeFont = PortsUi::Caps(13.5f * Grow, true);
	const float Disc = 20.f * Grow;
	const int32 Quiet = Board->GetQuiet();
	for (const FPortsRouteBadge& Badge : Board->GetRouteBadges())
	{
		if (Quiet > 0) break;
		FVector2D At;
		if (!ToLocal(Badge.Position, At)) continue;
		Box(LayerId, At - FVector2D(Disc, Disc) * 0.5, FVector2D(Disc, Disc), Badge.bSea ? &SeaBadge : &LandBadge, FLinearColor::White);
		const FString Value = FString::FromInt(Badge.Value);
		Text(LayerId + 1, At - Measure->Measure(Value, BadgeFont) * 0.5, Value, BadgeFont, Ink);
	}

	// City names: dark ink with a pale outline; home cities in crimson (.city .label in game.css).
	FSlateFontInfo CityFont = PortsUi::Serif(19.f * Grow, TEXT("Bold"));
	FSlateFontInfo HomeFont = PortsUi::Caps(17.f * Grow, false);
	HomeFont.OutlineSettings = FFontOutlineSettings(2, Pale);
	CityFont.OutlineSettings = FFontOutlineSettings(2, Pale);
	const FSlateFontInfo CountFont = PortsUi::Serif(14.f * Grow, TEXT("Bold"));
	FSlateFontInfo PipFont = PortsUi::Serif(17.f * Grow, TEXT("Bold"));
	PipFont.OutlineSettings = FFontOutlineSettings(2, Pale);
	const FLinearColor HomeInk = Color(TEXT("#7a1410"));
	const FLinearColor Plague = Color(TEXT("#b0261a"));

	for (const FPortsCityLabel& Label : Board->GetCityLabels())
	{
		FVector2D At;
		FSlateRect NameBox(0, 0, 0, 0);
		if (Quiet < 2 && ToLocal(Label.Anchor, At))
		{
			const FSlateFontInfo& NameFont = Label.bHome ? HomeFont : CityFont;
			const FVector2D Size = Measure->Measure(Label.Name, NameFont);
			FVector2D TopLeft(At.X, At.Y - Size.Y * 0.5);
			if (Label.Align < 0) TopLeft.X -= Size.X;
			else if (Label.Align == 0) { TopLeft.X -= Size.X * 0.5; TopLeft.Y = At.Y - Size.Y * 0.8; }
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)), Label.Name, NameFont, ESlateDrawEffect::None,
				Label.bHome ? HomeInk : Ink);
			NameBox = FSlateRect(TopLeft.X, TopLeft.Y, TopLeft.X + Size.X, TopLeft.Y + Size.Y);
		}

		// What the game has put at this city: plague pips above, house flags below.
		const FPortsCityView* View = Board->FindCityView(Label.CityId);
		if (!View || Quiet > 0) continue;
		FVector2D Above, Below;
		if (View->Severity > 0 && ToLocal(Label.PipAnchor, Above))
		{
			FString Pips;
			for (int32 i = 0; i < View->Severity; i++) Pips += TEXT("●");
			const FVector2D Size = Measure->Measure(Pips, PipFont);
			FVector2D TopLeft = Above - Size * FVector2D(0.5, 1.0);
			// A name written above its city would be covered: the pips then sit just over the name instead.
			const FSlateRect PipBox(TopLeft.X, TopLeft.Y + Size.Y * 0.2f, TopLeft.X + Size.X, TopLeft.Y + Size.Y * 0.9f);
			if (NameBox.GetSize().X > 0 && FSlateRect::DoRectanglesIntersect(PipBox, NameBox)) TopLeft.Y = NameBox.Top - Size.Y * 0.82f;
			Text(LayerId + 3, TopLeft, Pips, PipFont, Plague);
		}
		// How many of a house's family live here: a number in a pale disc at the foot of its flag (banner in art.js).
		const float Dot = 19.f * Grow;
		for (const FPortsCityToken& T : View->Tokens)
		{
			if (T.Family <= 0 || !ToLocal(T.Foot, Below)) continue;
			const FVector2D DotAt(Below.X - Dot * 0.5f, Below.Y - Dot * 0.2f);
			Box(LayerId + 5, DotAt, FVector2D(Dot, Dot), &FamilyDisc, FLinearColor::White);
			const FString Count = FString::FromInt(T.Family);
			Text(LayerId + 6, DotAt + (FVector2D(Dot, Dot) - Measure->Measure(Count, CountFont)) * 0.5, Count, CountFont, Ink);
		}
	}

	// Messages that rise from a city and fade: "+9ƒ", "Infected!".
	// .floater in game.css: gold for a gain, pale red for a loss, outlined in ink.
	FSlateFontInfo FloatFont = PortsUi::Caps(24.f * Grow, true);
	FloatFont.OutlineSettings = FFontOutlineSettings(2, Ink);
	const double Now = Board->GetWorld()->GetTimeSeconds();
	for (const FPortsFloater& F : Board->GetFloaters())
	{
		const float T = static_cast<float>((Now - F.Start) / 2.2);
		FVector2D At;
		if (T < 0 || T > 1 || !ToLocal(F.Position, At)) continue;
		FLinearColor Tint = F.bGain ? Color(TEXT("#f3d27a")) : Color(TEXT("#ff8a7a"));
		Tint.A = FMath::Clamp(2.f - 2.f * T, 0.f, 1.f);
		Text(LayerId + 6, At - Measure->Measure(F.Text, FloatFont) * FVector2D(0.5, 1.0) - FVector2D(0, 20.f + 36.f * T), F.Text, FloatFont, Tint);
	}
	return LayerId + 6;
}

// ---------- Root ----------

void SPortsRoot::Construct(const FArguments& Args)
{
	SetVisibility(EVisibility::SelfHitTestInvisible);
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SPortsMapLabels).Map(Args._Map) ]
		+ SOverlay::Slot()[ SAssignNew(ScreenSlot, SBox).Visibility(EVisibility::SelfHitTestInvisible) ]
		+ SOverlay::Slot()[ SAssignNew(DialogLayer, SOverlay).Visibility(EVisibility::SelfHitTestInvisible) ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 30)
		[
			SAssignNew(ToastLayer, SVerticalBox).Visibility(EVisibility::HitTestInvisible)
		]
	];
}

void SPortsRoot::SetScreen(const TSharedRef<SWidget>& Screen)
{
	// The new screen's panels were marked while it was being built, so only the old screen's are dropped here.
	ScreenSlot->SetContent(Screen);
	Solid.RemoveAll([](const TWeakPtr<SWidget>& W) { return !W.IsValid(); });
	Watch(Screen, TEXT("screen"));
}

void SPortsRoot::Tick(const FGeometry& Geometry, const double Time, const float Delta)
{
	SCompoundWidget::Tick(Geometry, Time, Delta);
	for (int32 i = Watched.Num() - 1; i >= 0; i--)
	{
		FWatched& W = Watched[i];
		const TSharedPtr<SWidget> Widget = W.Widget.Pin();
		if (!Widget.IsValid()) { Watched.RemoveAt(i); continue; }
		const FVector2D Now = Widget->GetDesiredSize();
		// Until the page has been measured once it has no size; the first size it reports is the one first shown.
		if (W.Frames == 0 && Now.IsNearlyZero()) continue;
		W.Frames++;
		if (W.Frames == 1) W.First = Now;
		else if (!Now.Equals(W.First, 0.5))
		{
			UE_LOG(LogTemp, Warning, TEXT("PortsLayout: JUMP %s %.0fx%.0f -> %.0fx%.0f (frame %d)"), *W.Name, W.First.X, W.First.Y, Now.X, Now.Y, W.Frames);
			Watched.RemoveAt(i);
		}
		else if (W.Frames >= 8)
		{
			UE_LOG(LogTemp, Display, TEXT("PortsLayout: steady %s %.0fx%.0f"), *W.Name, Now.X, Now.Y);
			Watched.RemoveAt(i);
		}
	}
}

float SPortsRoot::ViewHeight() const
{
	const float H = GetCachedGeometry().GetLocalSize().Y;
	return H > 100.f ? H : 720.f;
}

void SPortsRoot::OpenDialog(TFunction<TSharedRef<SWidget>(FPortsClose)> Build, const FPortsDialogOptions& Options, FPortsClose OnClose)
{
	const int32 Id = NextId++;
	TWeakPtr<SPortsRoot> Weak = SharedThis(this);
	const FPortsClose Close = [Weak, Id](const FString& Value) { if (const TSharedPtr<SPortsRoot> Root = Weak.Pin()) Root->CloseById(Id, Value); };

	// The scroll bar lies in the card's right margin rather than taking room from the text, so a card that
	// has to scroll is laid out exactly like one that does not.
	const TSharedRef<SScrollBar> Bar = SNew(SScrollBar);
	const TSharedRef<SWidget> Built = Build(Close);
	Watch(Built, Options.bStory ? TEXT("story card") : Options.bSide ? TEXT("side prompt") : Options.bWide ? TEXT("wide card") : TEXT("card"));
	// While a card is being dealt it may lift and turn a little outside the page; the page only trims its
	// contents to its edge (as it must, to scroll) once the card has settled.
	const TSharedRef<SScrollBox> Scroll = SNew(SScrollBox).ExternalScrollbar(Bar).Clipping(Options.bStory ? EWidgetClipping::Inherit : EWidgetClipping::ClipToBounds)
		+ SScrollBox::Slot().Padding(FMargin(0, 0, 14, 0))[ Built ];
	if (Options.bStory)
	{
		TWeakPtr<SScrollBox> WeakScroll = Scroll;
		RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateLambda([WeakScroll](double, float)
		{
			if (const TSharedPtr<SScrollBox> Box = WeakScroll.Pin()) Box->SetClipping(EWidgetClipping::ClipToBounds);
			return EActiveTimerReturnType::Stop;
		}));
	}
	const TSharedRef<SWidget> Frame = SNew(SBox)
		.WidthOverride(Options.FrameWidth())
		.MaxDesiredHeight(TAttribute<FOptionalSize>::CreateLambda([Weak]() { const TSharedPtr<SPortsRoot> Root = Weak.Pin(); return FOptionalSize((Root.IsValid() ? Root->ViewHeight() : 720.f) * 0.9f); }))
		[
			PortsUi::Entrance(PortsUi::Frame(SNew(SOverlay)
				+ SOverlay::Slot()[ Scroll ]
				+ SOverlay::Slot().HAlign(HAlign_Right)[ Bar ]), 0)
		];

	TSharedRef<SOverlay> Layer = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible);
	if (!Options.bSide)
	{
		// A dark veil over everything behind; clicking it cancels a choice that may be cancelled.
		const bool bDismiss = Options.bDismissable;
		Layer->AddSlot()
		[
			SNew(SBorder)
			.BorderImage(PortsUi::PictureBrush(TEXT("bg_dialog_veil")))
			.Padding(0)
			.OnMouseButtonDown_Lambda([Close, bDismiss](const FGeometry&, const FPointerEvent&) { if (bDismiss) Close(FString()); return FReply::Handled(); })
		];
		Layer->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ Frame ];
	}
	else
	{
		Layer->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 78, 16, 16)[ Frame ];
	}

	FDialog Dialog;
	Dialog.Id = Id;
	Dialog.Widget = Layer;
	Dialog.Frame = Frame;
	Dialog.Options = Options;
	Dialog.OnClose = OnClose;
	Dialogs.Add(Dialog);
	DialogLayer->AddSlot()[ Layer ];

	if (Options.AutoClose > 0.f)
	{
		RegisterActiveTimer(Options.AutoClose, FWidgetActiveTimerDelegate::CreateLambda([Close](double, float) { Close(TEXT("ok")); return EActiveTimerReturnType::Stop; }));
	}
}

void SPortsRoot::CloseById(int32 Id, const FString& Value)
{
	const int32 Index = Dialogs.IndexOfByPredicate([Id](const FDialog& D) { return D.Id == Id; });
	if (Index == INDEX_NONE) return;
	const FDialog Dialog = Dialogs[Index];
	Dialogs.RemoveAt(Index);
	DialogLayer->RemoveSlot(Dialog.Widget.ToSharedRef());
	if (Dialog.OnClose) Dialog.OnClose(Value);
}

void SPortsRoot::CloseTop(const FString& Value)
{
	if (Dialogs.Num()) CloseById(Dialogs.Last().Id, Value);
}

void SPortsRoot::CloseAllSilently()
{
	for (const FDialog& D : Dialogs) DialogLayer->RemoveSlot(D.Widget.ToSharedRef());
	Dialogs.Reset();
}

bool SPortsRoot::HasModalDialog() const
{
	return Dialogs.ContainsByPredicate([](const FDialog& D) { return !D.Options.bSide; });
}

bool SPortsRoot::HasStoryDialog() const
{
	return Dialogs.ContainsByPredicate([](const FDialog& D) { return D.Options.bStory; });
}

bool SPortsRoot::CancelTop()
{
	if (Dialogs.Num() == 0) return false;
	if (Dialogs.Last().Options.bDismissable) CloseTop(FString());
	return true;
}

bool SPortsRoot::ConfirmTop()
{
	if (Dialogs.Num() == 0) return false;
	// A copy: closing removes the card from the list, and its own text with it.
	const FString Value = Dialogs.Last().Options.EnterValue;
	if (!Value.IsEmpty()) CloseTop(Value);
	return true;
}

void SPortsRoot::Toast(const FString& Text, float Seconds)
{
	// .toast in game.css: dark wood with a gold edge.
	FPortsBoxLook Look;
	Look.Top = PortsUi::Color(TEXT("#3b2413")); Look.Bottom = PortsUi::Color(TEXT("#26190a"));
	Look.Radius = 12;
	Look.Border = PortsUi::Color(TEXT("#d9a82b")); Look.BorderWidth = 2;
	Look.SoftShadow = 0.45f;
	const TSharedRef<SWidget> Message = PortsUi::Box(Look, PortsUi::Rich(PortsUi::Esc(Text), TEXT("Ports.Toast"), ETextJustify::Center), FMargin(18, 10));
	const TSharedRef<SWidget> Sized = SNew(SBox).MaxDesiredWidth(900.f)[ Message ];
	// At most three messages at once: the oldest makes way.
	while (ToastLayer->NumSlots() >= 3) ToastLayer->RemoveSlot(ToastLayer->GetSlot(0).GetWidget());
	ToastLayer->AddSlot().AutoHeight().Padding(0, 4).HAlign(HAlign_Center)[ Sized ];
	TWeakPtr<SVerticalBox> Layer = ToastLayer;
	RegisterActiveTimer(Seconds, FWidgetActiveTimerDelegate::CreateLambda([Layer, Sized](double, float)
	{
		if (const TSharedPtr<SVerticalBox> Box = Layer.Pin()) Box->RemoveSlot(Sized);
		return EActiveTimerReturnType::Stop;
	}));
}

// Whether the pointer is on a panel or card. Checked against where each one actually is on
// screen, so the wheel and clicks over a panel never reach the map behind it.
bool SPortsRoot::IsPointerOverUi() const
{
	if (HasModalDialog()) return true;
	const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
	const auto Under = [&Cursor](const TSharedPtr<SWidget>& Widget)
	{
		return Widget.IsValid() && Widget->GetCachedGeometry().GetLocalSize().X > 1.f && Widget->GetCachedGeometry().IsUnderLocation(Cursor);
	};
	for (const FDialog& D : Dialogs) if (Under(D.Frame)) return true;
	for (const TWeakPtr<SWidget>& W : Solid) if (Under(W.Pin())) return true;
	return false;
}
