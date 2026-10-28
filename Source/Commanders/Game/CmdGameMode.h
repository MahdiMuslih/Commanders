#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/CmdTypes.h"
#include "CmdGameMode.generated.h"

class FJsonObject;
class UCmdAICommander;
class ACmdMissionDirector;
class ACmdTerrainActor;
class ACmdEnvironmentActor;

/**
 * Builds the whole session at runtime from a scenario description:
 * main-menu attract battle, a campaign mission (JSON) or a generated skirmish.
 */
UCLASS()
class COMMANDERS_API ACmdGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACmdGameMode();

	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UCmdAICommander* GetAICommander(int32 TeamId) const;
	ACmdMissionDirector* GetMissionDirector() const { return Director; }
	ACmdEnvironmentActor* GetEnvironment() const { return Environment; }
	ECmdSessionMode GetSessionMode() const { return SessionMode; }
	FVector GetCameraStart() const { return CameraStart; }

	/** Called by the mission director once the player takes control. */
	void OnMissionStarted();

private:
	void BuildScenario(const TSharedPtr<FJsonObject>& Scenario, bool bIsMenu);
	void BuildTerrain(const TSharedPtr<FJsonObject>& Map, const TArray<FIntRect>& KeepClear);
	void SetupTeams(const TSharedPtr<FJsonObject>& Scenario, bool bIsMenu);
	void SpawnEntities(const TSharedPtr<FJsonObject>& Scenario);

	UPROPERTY()
	TArray<TObjectPtr<UCmdAICommander>> AICommanders;

	UPROPERTY()
	TObjectPtr<ACmdMissionDirector> Director;

	UPROPERTY()
	TObjectPtr<ACmdTerrainActor> Terrain;

	UPROPERTY()
	TObjectPtr<ACmdEnvironmentActor> Environment;

	TSharedPtr<FJsonObject> ActiveScenario;
	ECmdSessionMode SessionMode = ECmdSessionMode::Menu;
	FVector CameraStart = FVector::ZeroVector;
	bool bAIActive = false;
};
