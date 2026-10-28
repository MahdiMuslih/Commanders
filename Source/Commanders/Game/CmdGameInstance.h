#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Core/CmdTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "CmdGameInstance.generated.h"

class UCmdSaveGame;
class UNetDriver;

USTRUCT(BlueprintType)
struct FCmdSkirmishSettings
{
	GENERATED_BODY()

	UPROPERTY() FName PlayerFaction = TEXT("concord");
	UPROPERTY() int32 NumEnemies = 1;
	UPROPERTY() int32 Difficulty = 1;		// 0 easy, 1 normal, 2 hard
	UPROPERTY() int32 MapSize = 80;		// cells per side
	UPROPERTY() int32 StartingCredits = 5000;
	UPROPERTY() int32 Seed = 0;
	UPROPERTY() int32 Theme = 0;			// 0 temperate, 1 winter, 2 desert, 3 dusk
	UPROPERTY() bool bAlliedAI = false;		// multiplayer: AI opponents fight as one team
};

/** Persists across level loads: which session to build, campaign progress, settings. */
UCLASS()
class COMMANDERS_API UCmdGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	UPROPERTY()
	ECmdSessionMode Mode = ECmdSessionMode::Menu;

	UPROPERTY()
	FName CurrentMission;

	UPROPERTY()
	FCmdSkirmishSettings Skirmish;

	/** Menu page to open when returning to the menu (e.g. the campaign list after a mission). */
	UPROPERTY()
	FName PendingMenuPage;

	UPROPERTY()
	TObjectPtr<UCmdSaveGame> Save;

	virtual void Shutdown() override;

	void StartMission(FName MissionId);

	// Multiplayer ------------------------------------------------------------
	/** Opens the battlefield as a listen server and shows the lobby. */
	void HostGame();

	/** Connects to a host ("192.168.1.20" or "host:7777"). */
	void JoinGame(const FString& Address);

	/** Leaves a networked game (or stops hosting) and returns to the main menu. */
	void LeaveGame(const FString& Reason = FString());

	FString GetPlayerName() const;
	void SetPlayerName(const FString& Name);
	FString GetLastJoinAddress() const;

	/** Why the last networked game ended unexpectedly; shown once by the menu. */
	UPROPERTY()
	FString NetworkError;
	void StartSkirmish(const FCmdSkirmishSettings& Settings);
	void ReturnToMenu(FName MenuPage = NAME_None);
	void RestartSession();

	/** Records the result of the current mission and unlocks the next one. */
	void ReportResult(bool bVictory);

	bool IsMissionUnlocked(FName MissionId) const;
	bool IsMissionCompleted(FName MissionId) const;
	FName GetNextMission(FName MissionId) const;
	bool IsLastMissionOfCampaign(FName MissionId) const;
	void SaveProgress();
	void ResetProgress();

	/** Unlocks every mission (handy for testing / the "all missions" cheat in the menu). */
	void UnlockAll();

private:
	void TravelToBattlefield();
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
};
