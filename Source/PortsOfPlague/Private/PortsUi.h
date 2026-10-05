// The look of every screen, taken from the website's stylesheet
// (src/styles/game.css): its three typefaces, its colours, the illuminated
// parchment frame, the raised buttons, the ribboned panels, the cards, the dice
// tray and the historical notes. Sizes are the stylesheet's own pixel sizes.
// Text uses a light markup: <b>bold</>, <i>italic</>, <small>…</>, <risk>…</>.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWidget.h"

struct FPortsPlayer;
class ISlateStyle;

// How a box is painted: a top-to-bottom gradient with rounded corners, a
// border, a hard drop shadow under it (the website's raised buttons), a soft
// shadow, an inset line (the gold line inside the frame), and a bar of colour
// down its left side or across its top.
struct FPortsBoxLook
{
	FLinearColor Top = FLinearColor::Transparent;
	FLinearColor Bottom = FLinearColor::Transparent;
	// An optional colour part of the way down (MidAt from 0 to 1; below 0 for none).
	FLinearColor Mid = FLinearColor::Transparent;
	float MidAt = -1.f;
	// The gradient runs left to right instead (panel ribbons, card bands).
	bool bSideways = false;
	float Radius = 12.f;
	// Square bottom corners (a ribbon or band at the top of a panel).
	bool bTopOnly = false;
	FLinearColor Border = FLinearColor::Transparent;
	float BorderWidth = 0.f;
	FLinearColor Shadow = FLinearColor::Transparent;
	float ShadowDrop = 0.f;
	float SoftShadow = 0.f;
	FLinearColor Inset = FLinearColor::Transparent;
	float InsetAt = 0.f;
	float InsetWidth = 0.f;
	FLinearColor LeftBar = FLinearColor::Transparent;
	float LeftBarWidth = 0.f;
	FLinearColor TopBar = FLinearColor::Transparent;
	float TopBarHeight = 0.f;
};

// A painted box around some content.
class SPortsBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsBox) : _Padding(FMargin(0)), _BlockMouse(false) {}
		SLATE_ARGUMENT(FPortsBoxLook, Look)
		SLATE_ARGUMENT(FMargin, Padding)
		// Clicks on the box are not passed on to the map behind it.
		SLATE_ARGUMENT(bool, BlockMouse)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
	FPortsBoxLook Look;
	bool bBlockMouse = false;
};

// A button painted like the website's: it lifts a little under the pointer and sinks when pressed.
class SPortsButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsButton) : _Padding(FMargin(0)), _Enabled(true), _DisabledOpacity(0.5f), _HoverShift(FVector2D(0, -1)), _DownShift(FVector2D(0, 2)) {}
		SLATE_ARGUMENT(FPortsBoxLook, Look)
		SLATE_ARGUMENT(FPortsBoxLook, HoverLook)
		SLATE_ARGUMENT(FMargin, Padding)
		SLATE_ARGUMENT(bool, Enabled)
		SLATE_ARGUMENT(float, DisabledOpacity)
		SLATE_ARGUMENT(FVector2D, HoverShift)
		SLATE_ARGUMENT(FVector2D, DownShift)
		SLATE_ARGUMENT(TFunction<void()>, OnClicked)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void OnMouseLeave(const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;
	bool CanClick() const { return bCanClick; }

private:
	void UpdateShift();
	FPortsBoxLook Look;
	FPortsBoxLook HoverLook;
	bool bCanClick = true;
	bool bHover = false;
	bool bDown = false;
	FVector2D HoverShift;
	FVector2D DownShift;
	TFunction<void()> OnClicked;
};

// Shows its content smaller (or larger) by a fixed amount, laying it out in the room that leaves it.
class SPortsZoom : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsZoom) : _Zoom(1.f) {}
		SLATE_ARGUMENT(float, Zoom)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual FVector2D ComputeDesiredSize(float LayoutScale) const override;
	virtual void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Children) const override;

private:
	float Zoom = 1.f;
};

namespace PortsUi
{
	void Init();
	void Shutdown();
	const ISlateStyle& Style();

	FLinearColor Color(const TCHAR* HexCode);
	// Makes plain text safe to put inside markup.
	FString Esc(const FString& Text);

	// The website's typefaces, at the stylesheet's pixel sizes.
	// EB Garamond (body text): Face is Regular, Italic or Bold.
	FSlateFontInfo Serif(float Pixels, const TCHAR* Face = TEXT("Regular"));
	// Cinzel (headings, labels, buttons): semi-bold, or extra-bold when heavy.
	FSlateFontInfo Caps(float Pixels, bool bHeavy = false);
	// UnifrakturMaguntia (the blackletter title).
	FSlateFontInfo Black(float Pixels);

	enum class EButton : uint8 { Normal, Primary, Ghost, Gold, Small, SmallOn, SmallGhostLight };

	TSharedRef<SWidget> Rich(const FString& Markup, const TCHAR* TextStyle = TEXT("Ports.Body"), ETextJustify::Type Justify = ETextJustify::Left, bool bWrap = true, float WrapAt = 0.f);
	TSharedRef<SWidget> Button(const FString& Markup, TFunction<void()> OnClick, EButton Kind = EButton::Normal, bool bEnabled = true, const FString& Tip = FString(), float WrapAt = 0.f);
	TSharedRef<SWidget> Box(const FPortsBoxLook& Look, const TSharedRef<SWidget>& Content, const FMargin& Padding = FMargin(0), bool bBlockMouse = false);
	// A rounded, flat-coloured box around some content.
	TSharedRef<SWidget> Tinted(const FLinearColor& Tint, const TSharedRef<SWidget>& Content, const FMargin& Padding = FMargin(12), bool bSmallCorners = false);
	// One of the pictures in Content/UI/Art, at a size in pixels.
	TSharedRef<SWidget> Picture(const FString& Name, const FVector2D& Size);
	const FSlateBrush* PictureBrush(const FString& Name);

	// The game's title in blackletter, with the website's glint of gold passing across it.
	TSharedRef<SWidget> ShimmerTitle(const FString& Text, float Pixels);
	// A paragraph that opens with a large red initial, the text running round it.
	TSharedRef<SWidget> DropCapText(const FString& Text, float Width = 0.f);

	// The illuminated parchment frame of the menus and cards, with its two red fleurons.
	TSharedRef<SWidget> Frame(const TSharedRef<SWidget>& Content, bool bBlockMouse = true);
	// A sidebar panel: parchment with a coloured ribbon across the top.
	TSharedRef<SWidget> Panel(const TSharedRef<SWidget>& RibbonContent, const FLinearColor& Ribbon, const TSharedRef<SWidget>& Content);
	TSharedRef<SWidget> Panel(const FString& Title, const FLinearColor& Ribbon, const TSharedRef<SWidget>& Content);
	FPortsBoxLook PlainLook(const FLinearColor& Fill, float Radius = 12.f, const FLinearColor& Border = FLinearColor::Transparent, float BorderWidth = 0.f);

	// A house's crest: its shape in its colour.
	TSharedRef<SWidget> Crest(const FString& ColorHex, const FString& Shape, float Size = 22);
	TSharedRef<SWidget> Crest(const FPortsPlayer& Player, float Size = 22);
	// A house's hanging banner (the pass-the-device screen and the turn order).
	TSharedRef<SWidget> Banner(const FString& Shape, float Width);
	// The crest's shape as a character (for running text and the map).
	FString CrestGlyph(const FString& Shape);
	// A die showing a value. Label goes underneath.
	TSharedRef<SWidget> Die(int32 Value, bool bRed = false, bool bGold = false, bool bSmall = false, const FString& LabelMarkup = FString());
	// What is heard when any button is clicked, and when a throw of dice starts.
	void SetClickSound(TFunction<void()> Play);
	void SetDiceSound(TFunction<void()> Play);

	// Dice made after this are thrown together, a moment after their card appears.
	void ResetDice();
	// Dice made after this wait until the ones before have settled (the turn-order card rolls one tray after another).
	void NextDiceTray();
	// How long after the card appears the last die made so far comes to rest, in seconds.
	float DiceSettleTime();
	// How long after the card appears the dice made next will be thrown.
	float DiceTrayStart();
	// Whether dice tumble at all (not while the game is checking itself).
	void SetDiceStill(bool bStill);

	// A page or card arriving: 0 rises into place (dialog in game.css), 1 is a card turned over from its back (.card).
	// 2 rises as 0 does, but unhurried (the menu arriving from the start screen).
	TSharedRef<SWidget> Entrance(const TSharedRef<SWidget>& Content, int32 Kind, float Delay = 0.f);

	struct FTheme { FLinearColor Main; FLinearColor Light; FString Label; };
	FTheme Theme(const FString& Id);
	// A fact's first source, shortened to author and title (shortCite in notes.js).
	FString ShortCite(const FString& SourceId);
}

// Lays content out top to bottom, like a page.
//
// A page is a list of things to build, and is only built once its width is
// known (Width(), then Widget()). Each part is then told exactly how wide it
// is: a card's text knows the card's inner width, a tray's dice row knows the
// tray's. Without that, text and rows only find their width a frame after
// they appear and the page visibly jumps.
class FPortsDoc
{
public:
	using FMake = TFunction<TSharedRef<SWidget>(float Width)>;

	FPortsDoc() = default;

	// The width this page will be given, in pixels.
	FPortsDoc& Width(float Pixels) { WrapAt = Pixels; return *this; }

	FPortsDoc& H1(const FString& Text, ETextJustify::Type Justify = ETextJustify::Left);
	FPortsDoc& H2(const FString& Markup, ETextJustify::Type Justify = ETextJustify::Left);
	FPortsDoc& H3(const FString& Markup);
	// A small heading in capitals above a field ("Number of houses").
	FPortsDoc& Label(const FString& Text);
	FPortsDoc& P(const FString& Markup, ETextJustify::Type Justify = ETextJustify::Left);
	// Any text in one of the named styles.
	FPortsDoc& Text(const FString& Markup, const TCHAR* Style, ETextJustify::Type Justify = ETextJustify::Left, const FMargin& Padding = FMargin(0, 3));
	// A paragraph that opens with a large red initial.
	FPortsDoc& DropCap(const FString& Text);
	FPortsDoc& Small(const FString& Markup);
	FPortsDoc& Light(const FString& Markup);
	FPortsDoc& Bullets(const TArray<FString>& Items, bool bNumbered = false);
	// A ready-made widget (it is not told the width).
	FPortsDoc& Add(const TSharedRef<SWidget>& Widget, const FMargin& Padding = FMargin(0, 4));
	// Something built when the width is known.
	FPortsDoc& AddBuilt(FMake Make, const FMargin& Padding = FMargin(0, 4));
	// Another page inside this one, narrower by Inset.
	FPortsDoc& Nest(const FPortsDoc& Inner, const FMargin& Padding = FMargin(0, 4), float Inset = 0.f);
	// Another page inside a painted box with this padding.
	FPortsDoc& Boxed(const FPortsBoxLook& Look, const FPortsDoc& Inner, const FMargin& BoxPadding, const FMargin& Padding = FMargin(0, 4));
	FPortsDoc& Space(float Height = 8);
	// A card with a colour-coded, illustrated top band (cardHtml in prompts.js).
	FPortsDoc& Card(const FString& ThemeId, const FString& KindMarkup, const FString& Title, const FPortsDoc& Body);
	// The green felt tray the dice lie on.
	FPortsDoc& Tray(const FPortsDoc& Body);
	// A sidebar panel: parchment with a coloured ribbon across the top.
	FPortsDoc& Panel(const FString& Title, const FLinearColor& Ribbon, const FPortsDoc& Body, const FMargin& Padding = FMargin(0, 0, 0, 13));
	FPortsDoc& Panel(const TSharedRef<SWidget>& RibbonContent, const FLinearColor& Ribbon, const FPortsDoc& Body, const FMargin& Padding = FMargin(0, 0, 0, 13));
	// Historical facts with their sources, word for word from facts.json (noteHtml in notes.js).
	FPortsDoc& Note(const TArray<FString>& FactIds, const FString& Title = TEXT("Historical Note"));
	FPortsDoc& Fact(const FString& FactId);
	// A choice in a list: main line, smaller line, and the reason if it cannot be chosen.
	FPortsDoc& Choice(const FString& MainMarkup, const FString& SubMarkup, const FString& Why, TFunction<void()> OnClick);
	// A row of widgets, wrapping to the next line when it is full.
	FPortsDoc& Row(const TArray<TSharedRef<SWidget>>& Widgets, float Gap = 8, EHorizontalAlignment Align = HAlign_Left);
	FPortsDoc& Buttons(const TArray<TSharedRef<SWidget>>& Widgets);
	// Pages side by side, sharing the width equally.
	FPortsDoc& Columns(const TArray<FPortsDoc>& Pages, float Gap = 16, const FMargin& Padding = FMargin(0, 4));

	// Builds the page at the width given by Width().
	TSharedRef<SWidget> Widget() const { return Build(WrapAt); }
	TSharedRef<SWidget> Build(float AtWidth) const;

private:
	struct FItem
	{
		FMake Make;
		FMargin Padding;
	};
	TArray<FItem> Items;
	float WrapAt = 0.f;
};
