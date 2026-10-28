#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/CmdTypes.h"
#include "Game/CmdNetTypes.h"
#include "CmdGameMode.generated.h"

class FJsonObject;
class UCmdAICommander;
class ACmdMissionDirector;
class ACmdTerrainActor;
class ACmdEnvironmentActor;

/**
 * Builds the whole session at runtime from a scenario description:
 * main-menu attract battle, a campaign mission (JSON) or a generated skirmish.
 * As a listen server it also runs the multiplayer lobby and starts the networked match.
 */
UCLASS()
class COMMANDERS_API ACmdGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACmdGameMode();

	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Multiplayer ------------------------------------------------------------
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	bool IsMultiplayer() const { return SessionMode == ECmdSessionMode::MultiplayerHost; }

	/** Host pressed Start: checks the lobby, then builds the match on every machine. Returns an error or empty. */
	FString StartMatchFromLobby();

	/** Host: back to a fresh lobby, taking every connected player along. */
	void ReturnToLobby();

	UCmdAICommander* GetAICommander(int32 TeamId) const;
	ACmdMissionDirector* GetMissionDirector() const { return Director; }
	ACmdEnvironmentActor* GetEnvironment() const { return Environment; }
	ECmdSessionMode GetSessionMode() const { return SessionMode; }
	FVector GetCameraStart() const { return CameraStart; }

	/** Called by the mission director once the player takes control. */
	void OnMissionStarted();

private:
	void BuildScenario(const TSharedPtr<FJsonObject>& Scenario, bool bIsMenu);
	void SetupTeams(const TSharedPtr<FJsonObject>& Scenario, bool bIsMenu);
	void SpawnEntities(const TSharedPtr<FJsonObject>& Scenario);
	void StartLobby();
	FCmdMatchConfig BuildMatchConfig(FString& OutError) const;

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
