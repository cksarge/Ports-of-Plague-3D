// Starts the game: loads the data files, sets up the light and sky, and puts
// the board in the world. Everything is made in code, so any level will do.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "PortsGameMode.generated.h"

class APortsMapActor;
class UPortsGameFlow;

UCLASS()
class PORTSOFPLAGUE_API APortsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	APortsPlayerController();

	virtual void BeginPlay() override;
};

UCLASS()
class PORTSOFPLAGUE_API APortsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APortsGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// For checking how smoothly the game runs.
	double SpeedFrames = 0, SpeedSeconds = 0, SlowestFrame = 0;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	APortsMapActor* GetMap() const { return Map; }
	UPortsGameFlow* GetFlow() const { return Flow; }

	// Dims the board for the finale: 0 is the usual light, 1 is night, when only candles and embers show clearly.
	void SetNight(float Amount);

private:
	void SpawnLightAndSky();
	void RunDevOptions();

	UPROPERTY()
	TObjectPtr<APortsMapActor> Map;

	UPROPERTY()
	TObjectPtr<UPortsGameFlow> Flow;

	UPROPERTY()
	TObjectPtr<class ADirectionalLight> SunLight;

	UPROPERTY()
	TObjectPtr<class ASkyLight> SkyGlow;

	float Night = 0.f, NightTarget = 0.f;

	FString LoadError;
};
