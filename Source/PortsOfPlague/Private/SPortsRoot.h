// Everything drawn flat on the screen over the 3D board: the map's lettering,
// the current screen (menu, setup, game panels, final scores), the stack of
// open cards and choices, and short messages.
#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class APlayerController;
class APortsMapActor;
class SOverlay;
class SVerticalBox;
class SBox;

// Closes the card or choice it belongs to, with the value that was chosen ("" = cancelled).
using FPortsClose = TFunction<void(const FString&)>;

struct FPortsDialogOptions
{
	bool bWide = false;
	// At the side of the screen, leaving the map in view (the action pickers).
	bool bSide = false;
	// Esc or a click outside closes it.
	bool bDismissable = true;
	// What the Enter key chooses, if anything.
	FString EnterValue;
	// A story card (not a choice): the turn timer waits while it is open.
	bool bStory = false;
	// Closes by itself after this many seconds, if above zero.
	float AutoClose = 0.f;

	// How wide the inside of the card is, in pixels (dialog in game.css: 780, 1000 when wide, 560 at the side).
	float FrameWidth() const { return bSide ? 560.f : bWide ? 1000.f : 780.f; }
	float InnerWidth() const { return FrameWidth() - 58.f - 14.f; }
};

// City names, route values, plague pips, house banners and floating messages.
class SPortsMapLabels : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsMapLabels) {}
		SLATE_ARGUMENT(TWeakObjectPtr<APortsMapActor>, Map)
	SLATE_END_ARGS()

	SPortsMapLabels();
	void Construct(const FArguments& Args);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	TWeakObjectPtr<APortsMapActor> Map;
	FSlateRoundedBoxBrush SeaBadge;
	FSlateRoundedBoxBrush LandBadge;
	FSlateRoundedBoxBrush Chip;
	FSlateRoundedBoxBrush FamilyDisc;
};

class SPortsRoot : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsRoot) {}
		SLATE_ARGUMENT(TWeakObjectPtr<APortsMapActor>, Map)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	// Replaces the current screen.
	void SetScreen(const TSharedRef<SWidget>& Screen);
	// A widget of the screen that covers part of the map (so clicks on it are not clicks on the map).
	void MarkSolid(const TSharedRef<SWidget>& Widget) { Solid.Add(Widget); }
	// For checking the game: writes to the log whether a newly shown page keeps the size it first had,
	// or changes it a moment later (which is seen as a jump).
	void Watch(const TSharedRef<SWidget>& Widget, const FString& Name) { Watched.Add({ Widget, Name }); }
	virtual void Tick(const FGeometry& Geometry, const double Time, const float Delta) override;

	void OpenDialog(TFunction<TSharedRef<SWidget>(FPortsClose)> Build, const FPortsDialogOptions& Options, FPortsClose OnClose);
	bool HasDialog() const { return Dialogs.Num() > 0; }
	bool HasModalDialog() const;
	bool HasStoryDialog() const;
	bool TopIsSide() const { return Dialogs.Num() > 0 && Dialogs.Last().Options.bSide; }
	void CloseTop(const FString& Value);
	// Closes everything without telling anyone (when a turn's time runs out).
	void CloseAllSilently();
	// Esc and Enter. Return true if a dialog used the key.
	bool CancelTop();
	bool ConfirmTop();

	void Toast(const FString& Text, float Seconds = 3.2f);
	bool IsPointerOverUi() const;
	float ViewHeight() const;

private:
	struct FDialog
	{
		int32 Id = 0;
		TSharedPtr<SWidget> Widget;
		TSharedPtr<SWidget> Frame;
		FPortsDialogOptions Options;
		FPortsClose OnClose;
	};
	void CloseById(int32 Id, const FString& Value);

	TSharedPtr<SBox> ScreenSlot;
	TSharedPtr<SOverlay> DialogLayer;
	TSharedPtr<SVerticalBox> ToastLayer;
	TArray<FDialog> Dialogs;
	TArray<TWeakPtr<SWidget>> Solid;
	struct FWatched { TWeakPtr<SWidget> Widget; FString Name; int32 Frames = 0; FVector2D First = FVector2D::ZeroVector; };
	TArray<FWatched> Watched;
	int32 NextId = 1;
};
