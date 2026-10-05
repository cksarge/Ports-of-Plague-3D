// The camera over the board. It always looks at a point on the map (in the web
// map's pixels) from a distance set by the zoom, as the web map's view does.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PortsCameraPawn.generated.h"

class UCameraComponent;

UCLASS()
class PORTSOFPLAGUE_API APortsCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	APortsCameraPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Looks at a map point. Zoom 1 shows the whole map; MaxZoom is the closest view.
	void SetView(const FVector2D& FocusPixel, double NewZoom, bool bJump = false);
	// The view of the whole map: centred on the cities, or, while a game's side panel is
	// showing, moved east so the cities sit in the middle of what the panel leaves free.
	void ShowWholeMap(bool bJump = false);
	// Tells the camera whether the side panel is showing, and glides to the matching whole-map view.
	void SetSidePanel(bool bShowing);

	// Camera work by the game itself: glides so that these map points fill the part of the screen the side panel
	// leaves free, no closer than MaxZoomIn. It does nothing once the player has moved the camera by hand, until
	// they ask for the whole map again (H, or the Reset map button), so it never fights them.
	void Frame(const TArray<FVector2D>& Pixels, double MaxZoomIn);
	// Keeps a moving thing (a ship on its voyage) in the middle of the free part of the screen.
	void Follow(const FVector2D& Pixel, double AtZoom);
	// Back to the whole map, by the game rather than the player.
	void FrameWholeMap();
	bool IsUserView() const { return bUserView; }
	// While locked, the mouse and keys do not move the map (the start screen).
	void SetLocked(bool bLock) { bLocked = bLock; }

	// A staged shot for the finale: looks at a map point from a chosen distance (as a zoom, which may be closer
	// than the player can go), angle down and compass direction, gliding there. The player's hand is off the camera meanwhile.
	void SetShot(const FVector2D& FocusPixel, double AtZoom, double PitchDegrees, double YawDegrees, bool bJump = false);
	void EndShot();
	bool InShot() const { return bShot; }

	// Zooms about the middle of the view (the map's + and - buttons).
	void ZoomBy(double Factor);

	double GetZoom() const { return Zoom; }
	FVector2D GetFocus() const { return Focus; }

	static constexpr double MaxZoom = 5.0;

private:
	void ReadInput(float DeltaSeconds);
	bool GroundUnderCursor(FVector2D& OutPixel) const;
	void ClampTarget();
	void PlaceCamera();
	double DistanceFor(double AtZoom) const;
	double PitchFor(double AtZoom) const;

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	FVector2D Focus = FVector2D::ZeroVector;
	FVector2D TargetFocus = FVector2D::ZeroVector;
	double Zoom = 1.0;
	double TargetZoom = 1.0;
	// How far the player has tipped the camera from its usual angle, in degrees (up is more overhead).
	double Tilt = 0.0;
	double TargetTilt = 0.0;
	bool bTilting = false;
	bool bSidePanel = false;
	// The player has moved the camera themselves since they last asked for the whole map.
	bool bUserView = false;
	bool bLocked = false;
	// How quickly the camera closes on where it is going: quick under the hand, slower when the game moves it.
	double GlideRate = 10.0;

	bool bShot = false;
	struct FShot { FVector2D Focus = FVector2D::ZeroVector; double Zoom = 1, Pitch = 60, Yaw = 0; };
	FShot Shot, ShotTarget;

	bool bDragging = false;
	FVector2D GrabPixel = FVector2D::ZeroVector;
	// A press that has not moved far is a click on the map, not a drag.
	bool bPressed = false;
	bool bMoved = false;
	FVector2D PressScreen = FVector2D::ZeroVector;
};
