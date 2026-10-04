#include "PortsCameraPawn.h"

#include "PortsData.h"
#include "PortsGameFlow.h"
#include "PortsGameMode.h"
#include "PortsMapSpace.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	constexpr float FieldOfView = 40.f;
	// The camera looks down steeply on the whole map and leans in as it comes closer.
	constexpr double FarPitch = 64.0;
	constexpr double NearPitch = 46.0;
	constexpr double ZoomStep = 1.15;
}

APortsCameraPawn::APortsCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetRootComponent());
	Camera->SetFieldOfView(FieldOfView);
	Camera->bConstrainAspectRatio = false;
}

void APortsCameraPawn::BeginPlay()
{
	Super::BeginPlay();
	ShowWholeMap(true);
}

void APortsCameraPawn::ShowWholeMap(bool bJump)
{
	// Centred on the cities rather than the empty Atlantic, as on the web map. During a
	// game the side panel covers the right of the screen, so the view moves east: the
	// cities then sit in the middle of what is left, with the easternmost ones clear of it.
	const double W = PortsProjection::MapWidth(), H = PortsProjection::MapHeight();
	double MinX = W, MaxX = 0;
	for (const FPortsCity& City : FPortsData::Get().Cities)
	{
		const double X = PortsMapSpace::CityPixel(City).X;
		MinX = FMath::Min(MinX, X);
		MaxX = FMath::Max(MaxX, X);
	}
	const double Cities = MaxX > MinX ? (MinX + MaxX) / 2 : W / 2;
	const double HalfViewWidth = FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5)) * DistanceFor(1.0) / PortsMapSpace::UnitsPerPixel;
	const double PanelShare = bSidePanel ? 416.0 / 1920.0 : 0.0;
	TargetTilt = 0;
	bUserView = false;
	if (bJump) Tilt = 0;
	// The camera leans, so the near (southern) edge needs a little more room than the far one.
	SetView(FVector2D(Cities + PanelShare * HalfViewWidth, H / 2 + 34), 1.0, bJump);
}

void APortsCameraPawn::FrameWholeMap()
{
	if (bUserView) return;
	ShowWholeMap();
	GlideRate = 3.6;
}

void APortsCameraPawn::Follow(const FVector2D& Pixel, double AtZoom)
{
	if (bUserView) return;
	const double PanelShare = bSidePanel ? 416.0 / 1920.0 : 0.0;
	const double HalfWidth = FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5)) * DistanceFor(1.0) / PortsMapSpace::UnitsPerPixel;
	SetView(Pixel + FVector2D(PanelShare * HalfWidth / AtZoom, 0), AtZoom);
	TargetTilt = 0;
	GlideRate = 5.0;
}

void APortsCameraPawn::Frame(const TArray<FVector2D>& Pixels, double MaxZoomIn)
{
	if (bUserView || Pixels.Num() == 0) return;
	FBox2D Box(ForceInit);
	for (const FVector2D& P : Pixels) Box += P;
	// How much of the map shows at zoom 1 in the part of the screen left of the side panel.
	const double PanelShare = bSidePanel ? 416.0 / 1920.0 : 0.0;
	const double HalfWidth = FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5)) * DistanceFor(1.0) / PortsMapSpace::UnitsPerPixel;
	const double FreeWidth = 2.0 * HalfWidth * (1.0 - PanelShare), FreeHeight = PortsProjection::MapHeight() * 0.9;
	const FVector2D Need = Box.GetSize() + FVector2D(150.0, 130.0);
	const double NewZoom = FMath::Clamp(FMath::Min(FreeWidth / Need.X, FreeHeight / Need.Y), 1.0, MaxZoomIn);
	// The points sit in the middle of the free part, so the view's own middle is a little to their east.
	SetView(Box.GetCenter() + FVector2D(PanelShare * HalfWidth / NewZoom, 6.0 / NewZoom), NewZoom);
	TargetTilt = 0;
	GlideRate = 3.6;
}

void APortsCameraPawn::SetSidePanel(bool bShowing)
{
	if (bSidePanel == bShowing) return;
	bSidePanel = bShowing;
	ShowWholeMap();
}

void APortsCameraPawn::ZoomBy(double Factor)
{
	bUserView = true;
	GlideRate = 10.0;
	TargetZoom = FMath::Clamp(TargetZoom * Factor, 1.0, MaxZoom);
}

void APortsCameraPawn::SetView(const FVector2D& FocusPixel, double NewZoom, bool bJump)
{
	TargetFocus = FocusPixel;
	TargetZoom = FMath::Clamp(NewZoom, 1.0, MaxZoom);
	ClampTarget();
	if (bJump)
	{
		Focus = TargetFocus;
		Zoom = TargetZoom;
		PlaceCamera();
	}
}

double APortsCameraPawn::PitchFor(double AtZoom) const
{
	return FMath::Lerp(FarPitch, NearPitch, FMath::Clamp((AtZoom - 1.0) / (MaxZoom - 1.0), 0.0, 1.0));
}

double APortsCameraPawn::DistanceFor(double AtZoom) const
{
	// Far enough at zoom 1 for the whole height of the map to be in view.
	double Aspect = 16.0 / 9.0;
	if (const UWorld* World = GetWorld())
	{
		if (const UGameViewportClient* Viewport = World->GetGameViewport())
		{
			FVector2D Size;
			Viewport->GetViewportSize(Size);
			if (Size.X > 0 && Size.Y > 0) Aspect = Size.X / Size.Y;
		}
	}
	const double HalfHorizontal = FMath::DegreesToRadians(FieldOfView * 0.5);
	const double HalfVertical = FMath::Atan(FMath::Tan(HalfHorizontal) / Aspect);
	const double HalfMapHeight = PortsProjection::MapHeight() * 0.5 * PortsMapSpace::UnitsPerPixel;
	const double HalfMapWidth = PortsProjection::MapWidth() * 0.5 * PortsMapSpace::UnitsPerPixel;
	const double Whole = FMath::Max(HalfMapHeight / FMath::Tan(HalfVertical), HalfMapWidth / FMath::Tan(HalfHorizontal)) * 1.1;
	return Whole / AtZoom;
}

void APortsCameraPawn::ClampTarget()
{
	TargetFocus.X = FMath::Clamp(TargetFocus.X, 0.0, PortsProjection::MapWidth());
	TargetFocus.Y = FMath::Clamp(TargetFocus.Y, 0.0, PortsProjection::MapHeight());
}

void APortsCameraPawn::SetShot(const FVector2D& FocusPixel, double AtZoom, double PitchDegrees, double YawDegrees, bool bJump)
{
	if (!bShot)
	{
		// Begins from wherever the camera is now.
		bShot = true;
		Shot = { Focus, Zoom, PitchFor(Zoom) + Tilt, 0.0 };
	}
	ShotTarget = { FocusPixel, AtZoom, PitchDegrees, YawDegrees };
	if (bJump) Shot = ShotTarget;
}

void APortsCameraPawn::EndShot()
{
	if (!bShot) return;
	bShot = false;
	ShowWholeMap(true);
}

void APortsCameraPawn::PlaceCamera()
{
	if (bShot)
	{
		const FRotator Look(-Shot.Pitch, Shot.Yaw, 0);
		const FVector At = PortsMapSpace::ToWorld(Shot.Focus, PortsMapSpace::LandZ);
		SetActorLocationAndRotation(At - Look.Vector() * DistanceFor(Shot.Zoom), Look);
		return;
	}
	const FRotator Look(-FMath::Clamp(PitchFor(Zoom) + Tilt, 22.0, 89.0), 0, 0);
	const FVector At = PortsMapSpace::ToWorld(Focus, PortsMapSpace::LandZ);
	SetActorLocationAndRotation(At - Look.Vector() * DistanceFor(Zoom), Look);
}

bool APortsCameraPawn::GroundUnderCursor(FVector2D& OutPixel) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	FVector Origin, Direction;
	if (!PC || !PC->DeprojectMousePositionToWorld(Origin, Direction) || Direction.Z >= -KINDA_SMALL_NUMBER) return false;
	const double Ground = PortsMapSpace::LandZ * PortsMapSpace::UnitsPerPixel;
	OutPixel = PortsMapSpace::ToPixel(Origin + Direction * ((Ground - Origin.Z) / Direction.Z));
	return true;
}

// Drag to move, wheel or two fingers on a trackpad to zoom around the pointer,
// arrow keys or WASD to move, + and - to zoom, Home for the whole map.
void APortsCameraPawn::ReadInput(float DeltaSeconds)
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	FVector2D Move(0, 0);
	if (PC->IsInputKeyDown(EKeys::Right) || PC->IsInputKeyDown(EKeys::D)) Move.X += 1;
	if (PC->IsInputKeyDown(EKeys::Left) || PC->IsInputKeyDown(EKeys::A)) Move.X -= 1;
	if (PC->IsInputKeyDown(EKeys::Down) || PC->IsInputKeyDown(EKeys::S)) Move.Y += 1;
	if (PC->IsInputKeyDown(EKeys::Up) || PC->IsInputKeyDown(EKeys::W)) Move.Y -= 1;
	if (!Move.IsZero()) { TargetFocus += Move.GetSafeNormal() * (520.0 / TargetZoom) * DeltaSeconds; bUserView = true; GlideRate = 10.0; }

	double Factor = 1.0;
	const APortsGameMode* GameForWheel = GetWorld()->GetAuthGameMode<APortsGameMode>();
	const bool bOverUi = GameForWheel && GameForWheel->GetFlow() && GameForWheel->GetFlow()->IsPointerOverUi();
	if (bOverUi) { /* the wheel belongs to the panel under the pointer */ }
	else if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) Factor *= ZoomStep;
	if (!bOverUi && PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) Factor /= ZoomStep;
	if (PC->WasInputKeyJustPressed(EKeys::Equals) || PC->WasInputKeyJustPressed(EKeys::Add)) Factor *= 1.5;
	if (PC->WasInputKeyJustPressed(EKeys::Hyphen) || PC->WasInputKeyJustPressed(EKeys::Subtract)) Factor /= 1.5;
	if (Factor != 1.0)
	{
		const double NewZoom = FMath::Clamp(TargetZoom * Factor, 1.0, MaxZoom);
		FVector2D Under;
		// Keep the point under the pointer where it is while zooming.
		if (GroundUnderCursor(Under)) TargetFocus = Under + (TargetFocus - Under) * (TargetZoom / NewZoom);
		TargetZoom = NewZoom;
		bUserView = true;
		GlideRate = 10.0;
	}
	if (PC->WasInputKeyJustPressed(EKeys::Home) || PC->WasInputKeyJustPressed(EKeys::H)) ShowWholeMap();

	// Tipping the camera: hold the right button (two fingers on a trackpad) and move up or down, or use Page Up and Page Down.
	if (PC->IsInputKeyDown(EKeys::RightMouseButton))
	{
		float DeltaX = 0, DeltaY = 0;
		PC->GetInputMouseDelta(DeltaX, DeltaY);
		if (!bTilting) bTilting = !bOverUi;
		else { TargetTilt += DeltaY * 0.6; bUserView = true; GlideRate = 10.0; }
	}
	else bTilting = false;
	if (PC->IsInputKeyDown(EKeys::PageUp)) TargetTilt += 40.0 * DeltaSeconds;
	if (PC->IsInputKeyDown(EKeys::PageDown)) TargetTilt -= 40.0 * DeltaSeconds;
	TargetTilt = FMath::Clamp(TargetTilt, 22.0 - PitchFor(TargetZoom), 89.0 - PitchFor(TargetZoom));

	const APortsGameMode* Game = GetWorld()->GetAuthGameMode<APortsGameMode>();
	UPortsGameFlow* Flow = Game ? Game->GetFlow() : nullptr;
	float MouseX = 0, MouseY = 0;
	PC->GetMousePosition(MouseX, MouseY);
	const FVector2D Mouse(MouseX, MouseY);
	if (PC->IsInputKeyDown(EKeys::LeftMouseButton))
	{
		FVector2D Under;
		if (!bPressed)
		{
			// A press that starts on a panel or card belongs to it, not to the map.
			bPressed = true;
			bMoved = false;
			PressScreen = Mouse;
			bDragging = !(Flow && Flow->IsPointerOverUi()) && GroundUnderCursor(GrabPixel);
		}
		else if (bDragging && GroundUnderCursor(Under))
		{
			if (FVector2D::Distance(Mouse, PressScreen) > 6.0) bMoved = true;
			if (bMoved)
			{
				// The map point that was grabbed stays under the pointer.
				bUserView = true;
				GlideRate = 10.0;
				TargetFocus += GrabPixel - Under;
				ClampTarget();
				Focus = TargetFocus;
			}
		}
	}
	else
	{
		if (bPressed && bDragging && !bMoved && Flow) Flow->OnMapClicked(GrabPixel);
		bPressed = false;
		bDragging = false;
	}
	ClampTarget();
}

void APortsCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bShot)
	{
		const double Near = 1.0 - FMath::Exp(-1.9 * DeltaSeconds);
		Shot.Focus = FMath::Lerp(Shot.Focus, ShotTarget.Focus, Near);
		Shot.Zoom = FMath::Lerp(Shot.Zoom, ShotTarget.Zoom, Near);
		Shot.Pitch = FMath::Lerp(Shot.Pitch, ShotTarget.Pitch, Near);
		Shot.Yaw = FMath::Lerp(Shot.Yaw, ShotTarget.Yaw, Near);
		PlaceCamera();
		return;
	}
	ReadInput(DeltaSeconds);
	const double Ease = 1.0 - FMath::Exp(-GlideRate * DeltaSeconds);
	Focus = FMath::Lerp(Focus, TargetFocus, Ease);
	Zoom = FMath::Lerp(Zoom, TargetZoom, Ease);
	Tilt = FMath::Lerp(Tilt, TargetTilt, Ease);
	PlaceCamera();
}
