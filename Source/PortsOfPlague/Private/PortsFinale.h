// The end-of-game finale: a show in seven scenes before the results page, after finale.js
// in the web version. Every sentence and number comes from the same places as there. Here the
// scenes are staged on the 3D board (see PortsMapFinale.cpp and PortsFlowFinale.cpp), and this
// widget lays the words, counters and controls over it.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FPortsFinaleHouse
{
	FString Name, Crest, Home;
	FLinearColor Color = FLinearColor::White;
	// Where the house's home city stands on the board.
	FVector Spot = FVector::ZeroVector;
	int32 Alive = 0, Lost = 0;
	bool bStood = false;
	// When each of its lost family members falls, in seconds into The Toll.
	TArray<double> FallAt;
};

struct FPortsFinaleRow
{
	int32 Place = 0, House = 0, Total = 0;
	int32 Parts[4] = { 0, 0, 0, 0 };
	// When the row comes in, in seconds into The Reckoning.
	double At = 0;
};

struct FPortsFinaleHonour
{
	int32 Icon = 0;
	FString Title, What;
	TArray<int32> Winners;
};

// Everything the finale shows, worked out once when it starts; Scene and T move as it plays.
struct FPortsFinaleModel
{
	TArray<FString> SceneIds, SceneLabels;
	int32 Scene = 0;
	// Seconds into the scene, and how long it stays before moving on by itself.
	double T = 0, Hold = 1;
	bool bPaused = false;

	TArray<FPortsFinaleHouse> Houses;
	FString MetaLine, LostLine, ShipLine, StandLine, WinLine, WinnerNames;
	int32 Pct = 0, Infected = 0, TopTotal = 0, MaxTotal = 1;
	double TollStart = 0, TollFall = 0, TollTextAt = 0;
	TArray<FString> Early;
	int32 EarlyMore = 0;
	double ReplayEnd = 0, CaptionAt = 0;
	TArray<FPortsFinaleHonour> Honours;
	TArray<FPortsFinaleRow> Rows;
	double SuspenseAt = 0, FlashAt = 0;
	TArray<int32> Winners;
	TWeakObjectPtr<class APlayerController> Player;
};

class SPortsFinale : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsFinale) {}
		SLATE_ARGUMENT(TSharedPtr<FPortsFinaleModel>, Model)
		// Moves the show: -1 back, +1 next, 1000 + i to scene i, 99 to the results, 0 pause or play.
		SLATE_ARGUMENT(TFunction<void(int32)>, OnMove)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
	TSharedPtr<FPortsFinaleModel> Model;
	TFunction<void(int32)> OnMove;
	// Where the controls were drawn last, and what each does when clicked.
	mutable TArray<TPair<FSlateRect, int32>> Hot;
	// Brushes made while painting, kept until the next frame has been drawn.
	mutable TArray<TSharedPtr<struct FSlateBrush>> Brushes;
};
