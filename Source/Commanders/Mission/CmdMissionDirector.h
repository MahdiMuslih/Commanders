#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/CmdTypes.h"
#include "CmdMissionDirector.generated.h"

class FJsonObject;
class FJsonValue;
class ACmdEntity;
class ACmdBuilding;
class ACmdUnit;
class ACmdCinematicDirector;
class ACmdGameState;

enum class ECmdObjectiveState : uint8
{
	Active,
	Completed,
	Failed
};

struct FCmdObjective
{
	FName Id;
	FString Text;
	FString Type;
	TSharedPtr<FJsonObject> Json;
	bool bPrimary = true;
	bool bVisible = true;
	ECmdObjectiveState State = ECmdObjectiveState::Active;
	float ActivatedAt = 0.f;
};

struct FCmdTrigger
{
	FName Id;
	TSharedPtr<FJsonObject> Condition;
	TArray<TSharedPtr<FJsonValue>> Actions;
	bool bEnabled = true;
	bool bRepeat = false;
	bool bFired = false;
	float Interval = 0.f;
	float NextAllowed = 0.f;
	float Delay = 0.f;
};

struct FCmdDialogueLine
{
	FString Speaker;
	FString Text;
	FLinearColor Color = FLinearColor::White;
	float Duration = 4.f;
};

struct FCmdNamedTimer
{
	FString Label;
	float Remaining = 0.f;
	bool bVisible = true;
	bool bExpired = false;
};

/**
 * Data-driven mission runtime. Reads the mission JSON (objectives, triggers, dialogue,
 * cinematics) and drives the session flow: Briefing -> Intro cinematic -> Play -> Outro -> Result.
 * Skirmish matches use the same director with a synthesised "destroy all enemies" objective.
 */
UCLASS()
class COMMANDERS_API ACmdMissionDirector : public AActor
{
	GENERATED_BODY()

public:
	ACmdMissionDirector();

	virtual void Tick(float DeltaSeconds) override;

	void InitMission(const TSharedPtr<FJsonObject>& InMission, bool bInSkirmish);

	/** Shows the briefing (campaign) or goes straight in (skirmish). */
	void StartSession();

	/** Player pressed "Begin Mission" on the briefing screen. */
	void ConfirmBriefing();

	void EndMission(bool bVictory, const FString& Reason);
	void SkipCinematic();
	bool PlayCinematic(FName Id, TFunction<void()> OnFinished = nullptr);

	/** Runs one scripted action (also used by cinematic timeline events). */
	void ExecuteAction(const TSharedPtr<FJsonObject>& Action);
	void ExecuteActions(const TArray<TSharedPtr<FJsonValue>>& Actions);

	// Gameplay events ----------------------------------------------------------
	void OnEntityDestroyed(ACmdEntity* Entity, ACmdEntity* Killer);
	void OnEntityCaptured(ACmdEntity* Entity, int32 OldTeam);
	void OnStructureBuilt(ACmdBuilding* Building);
	void OnUnitProduced(ACmdUnit* Unit);
	void OnTeamDefeated(int32 TeamId);

	// HUD queries --------------------------------------------------------------
	const TArray<FCmdObjective>& GetObjectives() const { return Objectives; }
	const FCmdDialogueLine* GetCurrentDialogue() const { return bHasDialogue ? &CurrentDialogue : nullptr; }
	float GetDialogueAge() const { return DialogueAge; }
	bool GetVisibleTimer(FString& OutLabel, float& OutRemaining) const;
	bool IsSkirmish() const { return bSkirmish; }
	bool IsEnded() const { return bEnded; }
	bool WasVictory() const { return bVictory; }
	float GetMissionTime() const { return MissionTime; }
	ACmdCinematicDirector* GetCinematicDirector() const { return Cinematic; }

	FName MissionId;
	FString Title;
	FString Subtitle;
	FString Location;
	FString Date;
	TArray<FString> BriefingParagraphs;
	FString ResultReason;
	FString VictoryText;
	FString DefeatText;
	int32 PlayerTeam = 0;

private:
	bool EvaluateCondition(const TSharedPtr<FJsonObject>& Condition);
	bool IsAreaOccupied(const TSharedPtr<FJsonObject>& Json);
	bool IsTagDestroyed(FName Tag) const;
	void UpdateObjectives();
	void CheckEndConditions();
	void TickTriggers();
	void TickDialogue(float DeltaSeconds);
	void TickTimers(float DeltaSeconds);
	void TickPending(float DeltaSeconds);
	void BeginPlaying();
	void FinishMission();
	void AddObjectiveFromJson(const TSharedPtr<FJsonObject>& Json);
	FCmdObjective* FindObjective(FName Id);
	void SetObjectiveState(FName Id, ECmdObjectiveState State);
	int32 ResolveTeam(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, int32 Default) const;
	TArray<ACmdEntity*> ResolveEntities(const TSharedPtr<FJsonObject>& Json) const;
	void IssueOrders(const TArray<ACmdEntity*>& Entities, const TSharedPtr<FJsonObject>& Json);
	void QueueDialogue(const TArray<TSharedPtr<FJsonValue>>& Lines);
	ACmdGameState* GS() const;

	TSharedPtr<FJsonObject> Mission;
	TArray<FCmdObjective> Objectives;
	TArray<FCmdTrigger> Triggers;
	TArray<FCmdDialogueLine> DialogueQueue;
	FCmdDialogueLine CurrentDialogue;
	bool bHasDialogue = false;
	float DialogueAge = 0.f;

	struct FPendingActions
	{
		float Remaining = 0.f;
		TArray<TSharedPtr<FJsonValue>> Actions;
	};
	TArray<FPendingActions> Pending;

	TMap<FName, FCmdNamedTimer> Timers;
	FName VisibleTimer;
	TSet<FName> Flags;
	TMap<FName, int32> ProducedCounts;

	UPROPERTY()
	TObjectPtr<ACmdCinematicDirector> Cinematic;

	float MissionTime = 0.f;
	float EvalTimer = 0.f;
	bool bSkirmish = false;
	bool bPlaying = false;
	bool bEnded = false;
	bool bVictory = false;
	float EndDelay = -1.f;
};
