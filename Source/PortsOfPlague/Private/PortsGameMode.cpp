#include "PortsGameMode.h"

#include "PortsCameraPawn.h"
#include "PortsData.h"
#include "PortsMapActor.h"
#include "PortsGameFlow.h"
#include "PortsUi.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UnrealClient.h"

APortsPlayerController::APortsPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
}

void APortsPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

APortsGameMode::APortsGameMode()
{
	DefaultPawnClass = APortsCameraPawn::StaticClass();
	PlayerControllerClass = APortsPlayerController::StaticClass();
	HUDClass = nullptr;
	PrimaryActorTick.bCanEverTick = true;
}

void APortsGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	FPortsData& Data = FPortsData::Get();
	if (!Data.Load(FPortsData::DefaultDataDir(), LoadError))
	{
		UE_LOG(LogTemp, Error, TEXT("Ports of Plague: %s"), *LoadError);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("Ports of Plague: loaded %d cities and %d routes."), Data.Cities.Num(), Data.Routes.Num());

	SpawnLightAndSky();
	Map = GetWorld()->SpawnActor<APortsMapActor>(FVector::ZeroVector, FRotator::ZeroRotator);
}

// The board is lit like a map on a table in a candle-lit room: one warm, low light from the
// north-west (the usual side for maps, so hills and towns throw their shadows to the south-east),
// a soft warm glow from all round, a thin haze over the far side of the table, and the picture
// darkening a little towards its corners.
void APortsGameMode::SpawnLightAndSky()
{
	UWorld* World = GetWorld();
	const FVector SunDirection(-0.75, 0.85, -0.95);
	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 20000), SunDirection.Rotation());
	SunLight = Sun;
	if (UDirectionalLightComponent* Light = Sun ? Cast<UDirectionalLightComponent>(Sun->GetLightComponent()) : nullptr)
	{
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensity(6.2f);
		Light->SetLightColor(FLinearColor(1.0f, 0.92f, 0.79f));
		Light->SetAtmosphereSunLight(true);
		Light->LightSourceAngle = 2.5f;
		Light->MarkRenderStateDirty();
	}
	World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0, 0, 20000), FRotator::ZeroRotator);
	SkyGlow = Sky;
	if (USkyLightComponent* Light = Sky ? Sky->GetLightComponent() : nullptr)
	{
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetRealTimeCapture(true);
		Light->SetIntensity(1.15f);
		Light->SetLightColor(FLinearColor(1.0f, 0.95f, 0.86f));
	}
	AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector(0, 0, 0), FRotator::ZeroRotator);
	if (UExponentialHeightFogComponent* Haze = Fog ? Fog->GetComponent() : nullptr)
	{
		Haze->SetMobility(EComponentMobility::Movable);
		Haze->SetFogDensity(0.0035f);
		Haze->SetFogHeightFalloff(0.02f);
		Haze->SetFogInscatteringColor(FLinearColor(0.95f, 0.72f, 0.42f));
		Haze->SetStartDistance(9000.f);
		Haze->SetFogMaxOpacity(0.4f);
	}
	APostProcessVolume* Look = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Look)
	{
		Look->bUnbound = true;
		FPostProcessSettings& S = Look->Settings;
		S.bOverride_VignetteIntensity = true; S.VignetteIntensity = 0.55f;
		// The picture's brightness is fixed: it does not creep up when the board is dimmed for the finale.
		S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = 1.5f;
		S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = 1.5f;
		S.bOverride_BloomIntensity = true; S.BloomIntensity = 0.f;
		S.bOverride_AmbientOcclusionIntensity = true; S.AmbientOcclusionIntensity = 0.f;
		S.bOverride_ColorSaturation = true; S.ColorSaturation = FVector4(1.04, 1.04, 1.04, 1.0);
		S.bOverride_ColorContrast = true; S.ColorContrast = FVector4(1.04, 1.04, 1.04, 1.0);
	}
}

void APortsGameMode::SetNight(float Amount)
{
	NightTarget = FMath::Clamp(Amount, 0.f, 1.f);
}

void APortsGameMode::StartPlay()
{
	Super::StartPlay();
	// On a Retina screen the screens and lettering are drawn at the screen's full sharpness, while the 3D map
	// behind them is drawn at half that and scaled up, which keeps the game smooth.
	const float ScreenScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(0, 0);
	if (ScreenScale > 1.2f)
	{
		if (IConsoleVariable* Share = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))) Share->Set(FMath::Clamp(100.f / ScreenScale, 50.f, 100.f), ECVF_SetByGameSetting);
	}
	UE_LOG(LogTemp, Display, TEXT("PortsScreen: scale %.2f."), ScreenScale);
	if (!LoadError.IsEmpty() && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 600.f, FColor::Red, FString::Printf(TEXT("Ports of Plague could not load its data: %s"), *LoadError));
	}
	if (Map && LoadError.IsEmpty())
	{
		Flow = NewObject<UPortsGameFlow>(this);
		Flow->Start(Map);
	}
	RunDevOptions();
}

void APortsGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetWorld()->GetTimeSeconds() > 3.0) { SpeedFrames++; SpeedSeconds += DeltaSeconds; SlowestFrame = FMath::Max(SlowestFrame, static_cast<double>(DeltaSeconds)); }
	if (Flow) Flow->Tick(DeltaSeconds);
	if (!FMath::IsNearlyEqual(Night, NightTarget, 0.002f))
	{
		// The light changes over a second or so, never at a stroke.
		Night = FMath::FInterpTo(Night, NightTarget, DeltaSeconds, 1.6f);
		if (UDirectionalLightComponent* Light = SunLight ? Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()) : nullptr)
		{
			Light->SetIntensity(FMath::Lerp(6.2f, 0.55f, Night));
			Light->SetLightColor(FMath::Lerp(FLinearColor(1.0f, 0.92f, 0.79f), FLinearColor(0.50f, 0.62f, 1.0f), Night));
		}
		if (USkyLightComponent* Light = SkyGlow ? SkyGlow->GetLightComponent() : nullptr)
		{
			Light->SetIntensity(FMath::Lerp(1.15f, 0.22f, Night));
			Light->SetLightColor(FMath::Lerp(FLinearColor(1.0f, 0.95f, 0.86f), FLinearColor(0.55f, 0.66f, 1.0f), Night));
		}
	}
}

void APortsGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Flow) Flow->Stop();
	Super::EndPlay(Reason);
}

// Options for checking the game from the command line:
//   -PortsView=x,y,zoom   look at a map point (in map pixels) at a zoom from 1 to 5
//   -PortsTest=spec       open a screen or start a game straight away (see UPortsGameFlow::StartTestGame)
//   -PortsShot=file.png   save a picture of the screen, then quit
//   -PortsShotDelay=8     seconds to wait before the picture (default 8)
void APortsGameMode::RunDevOptions()
{
	FString View;
	if (FParse::Value(FCommandLine::Get(), TEXT("PortsView="), View, false))
	{
		TArray<FString> Parts;
		View.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3)
		{
			const FVector2D Focus(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]));
			const double Zoom = FCString::Atod(*Parts[2]);
			FTimerHandle Handle;
			GetWorldTimerManager().SetTimer(Handle, [this, Focus, Zoom]()
			{
				if (APortsCameraPawn* Pawn = Cast<APortsCameraPawn>(UGameplayStatics::GetPlayerPawn(this, 0))) { Pawn->SetView(Focus, Zoom, true); Pawn->ZoomBy(1.0); }
			}, 0.5f, false);
		}
	}

	FString Test;
	if (Flow && FParse::Value(FCommandLine::Get(), TEXT("PortsTest="), Test, false)) Flow->StartTestGame(Test);

	FString Shot;
	if (FParse::Value(FCommandLine::Get(), TEXT("PortsShot="), Shot))
	{
		float Delay = 8.f;
		FParse::Value(FCommandLine::Get(), TEXT("PortsShotDelay="), Delay);
		FTimerHandle ShotHandle, QuitHandle;
		GetWorldTimerManager().SetTimer(ShotHandle, [this, Shot]()
		{
			if (Flow) Flow->TestBeforeShot();
			// How smoothly the game has been running, measured from three seconds in.
			if (SpeedSeconds > 0) UE_LOG(LogTemp, Display, TEXT("PortsSpeed: %.0f frames a second on average, slowest frame %.0f ms."), SpeedFrames / SpeedSeconds, SlowestFrame * 1000.0);
			FScreenshotRequest::RequestScreenshot(Shot, true, false);
		}, Delay, false);
		GetWorldTimerManager().SetTimer(QuitHandle, [this]() { UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false); }, Delay + 2.f, false);
	}
}
