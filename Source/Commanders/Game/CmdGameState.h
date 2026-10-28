#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/NetSerialization.h"
#include "Core/CmdTypes.h"
#include "Game/CmdNetTypes.h"
#include "Game/CmdGameInstance.h"
#include "CmdGameState.generated.h"

class ACmdEntity;
class ACmdBuilding;
class ACmdMissionDirector;
class ACmdPlayerController;
struct FCmdEntityDef;

USTRUCT()
struct FCmdProductionQueue
{
	GENERATED_BODY()

	UPROPERTY() TArray<FName> Items;	// front item is in progress
	UPROPERTY() float Progress = 0.f;	// 0..1 for the front item
	UPROPERTY() float Spent = 0.f;		// credits sunk into the front item
	UPROPERTY() bool bOnHold = false;
	UPROPERTY() bool bReady = false;	// structure finished, waiting to be placed
};

USTRUCT()
struct FCmdTeamStats
{
	GENERATED_BODY()

	UPROPERTY() int32 UnitsBuilt = 0;
	UPROPERTY() int32 UnitsLost = 0;
	UPROPERTY() int32 BuildingsBuilt = 0;
	UPROPERTY() int32 BuildingsLost = 0;
	UPROPERTY() int32 Kills = 0;
	UPROPERTY() float CreditsHarvested = 0.f;
	UPROPERTY() float CreditsSpent = 0.f;
};

/**
 * A side in the match. UPROPERTY fields replicate to every client; the rest
 * (power, owned counts, warning throttles) is derived locally on each machine.
 */
USTRUCT()
struct FCmdTeam
{
	GENERATED_BODY()

	FCmdTeam() { Queues.SetNum(CmdConst::CategoryCount); }

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FName Faction;
	UPROPERTY() FString Name;
	UPROPERTY() FLinearColor Color = FLinearColor::White;
	UPROPERTY() int32 Alliance = 0;
	UPROPERTY() float Credits = 0.f;
	UPROPERTY() bool bIsLocalPlayer = false;
	UPROPERTY() bool bIsAI = false;
	UPROPERTY() bool bIsHuman = false;
	UPROPERTY() bool bIsNeutral = false;
	UPROPERTY() bool bDefeated = false;
	UPROPERTY() bool bCanBuild = true;			// missions can disable base building
	UPROPERTY() float BuildSpeed = 1.f;			// AI difficulty / mission tuning
	UPROPERTY() TArray<FCmdProductionQueue> Queues;
	UPROPERTY() TArray<FName> TechLock;			// entity ids that are disabled for this team
	UPROPERTY() FCmdTeamStats Stats;
	UPROPERTY() FIntPoint StartCell = FIntPoint::ZeroValue;

	// Derived locally, never replicated.
	int32 PowerProduced = 0;
	int32 PowerConsumed = 0;
	TMap<FName, int32> Owned;
	float LastAttackWarning = -100.f;
	bool bLowPowerWarned = false;

	bool HasLowPower() const { return PowerConsumed > PowerProduced; }
	float PowerFactor() const
	{
		if (!HasLowPower()) return 1.f;
		return PowerProduced <= 0 ? 0.25f : FMath::Clamp(static_cast<float>(PowerProduced) / PowerConsumed, 0.25f, 1.f);
	}
	int32 CountOwned(FName EntityId) const { const int32* C = Owned.Find(EntityId); return C ? *C : 0; }
};

struct FCmdMessage
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
	float Time = 0.f;
};

/** Cosmetic effect kinds broadcast by the server. */
enum class ECmdNetEffect : uint8
{
	Explosion,
	Flash,
	Beam,
	Smoke
};

/**
 * Match state, authoritative on the server and replicated to clients.
 * Owns the team list, the entity registry, the sidebar production logic and
 * routes gameplay events to the mission director. In networked games it also
 * carries the lobby settings and the match config clients build their world from,
 * and broadcasts cosmetic events (shots, explosions, ore changes).
 */
UCLASS()
class COMMANDERS_API ACmdGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ACmdGameState();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Teams ------------------------------------------------------------------
	UPROPERTY(Replicated)
	TArray<FCmdTeam> Teams;

	/** The team this machine's player commands (local, never replicated). */
	int32 LocalTeamId = 0;

	int32 AddTeam(const FCmdTeam& Team);
	FCmdTeam* GetTeam(int32 TeamId) { return Teams.IsValidIndex(TeamId) ? &Teams[TeamId] : nullptr; }
	const FCmdTeam* GetTeam(int32 TeamId) const { return Teams.IsValidIndex(TeamId) ? &Teams[TeamId] : nullptr; }
	FCmdTeam* GetLocalTeam() { return GetTeam(LocalTeamId); }
	int32 GetAlliance(int32 TeamId) const;
	int32 GetLocalAlliance() const { return GetAlliance(LocalTeamId); }
	bool AreAllied(int32 A, int32 B) const;
	bool AreHostile(int32 A, int32 B) const { return !AreAllied(A, B); }
	bool IsNeutralTeam(int32 TeamId) const;
	FLinearColor GetTeamColor(int32 TeamId) const;
	void AddCredits(int32 TeamId, float Amount, bool bHarvested);

	// Entities ---------------------------------------------------------------
	UPROPERTY()
	TArray<TObjectPtr<ACmdEntity>> Entities;

	void RegisterEntity(ACmdEntity* Entity);
	void UnregisterEntity(ACmdEntity* Entity);
	void OnEntityDamaged(ACmdEntity* Victim, ACmdEntity* Attacker);
	void OnEntityKilled(ACmdEntity* Victim, ACmdEntity* Killer);
	void OnEntitySold(ACmdBuilding* Building);
	void OnEntityChangedTeam(ACmdEntity* Entity, int32 OldTeam);
	void OnStructureCompleted(ACmdBuilding* Building);

	int32 CountTeamEntities(int32 TeamId, bool bIncludeBuildings = true, bool bIncludeUnits = true) const;
	TArray<ACmdEntity*> GetEntitiesWithTag(FName Tag) const;
	bool HasEverSeenTag(FName Tag) const { return SeenTags.Contains(Tag); }
	FVector GetTeamBaseLocation(int32 TeamId) const;

	// Production -------------------------------------------------------------
	bool HasPrerequisites(int32 TeamId, const FCmdEntityDef& Def, FString* OutMissing = nullptr) const;
	bool HasProducer(int32 TeamId, ECmdCategory Category) const;
	bool CanTeamBuild(int32 TeamId, const FCmdEntityDef& Def) const;
	bool IsVisibleInSidebar(int32 TeamId, const FCmdEntityDef& Def) const;
	TArray<const FCmdEntityDef*> GetBuildOptions(int32 TeamId, ECmdCategory Category) const;
	bool QueueProduction(int32 TeamId, FName DefId, bool bNotify = true);
	void CancelProduction(int32 TeamId, FName DefId);
	void ToggleHold(int32 TeamId, ECmdCategory Category);
	int32 CountQueued(int32 TeamId, FName DefId) const;
	bool PlaceReadyStructure(int32 TeamId, const FIntPoint& Origin);
	FName GetReadyStructure(int32 TeamId) const;
	ACmdBuilding* FindProducer(int32 TeamId, ECmdCategory Category) const;
	void SetPrimary(ACmdBuilding* Building);
	bool HasRadar(int32 TeamId) const;

	// Messages (EVA style notifications) -----------------------------------
	TArray<FCmdMessage> Messages;

	/** Longer-lived copy of messages for the lobby's chat panel. */
	TArray<FCmdMessage> ChatLog;
	/**
	 * Announcer message + sound for a team's player(s). On a networked server it is routed
	 * to the owning players' machines; INDEX_NONE means everyone.
	 */
	void AddMessage(int32 TeamId, const FString& Text, const FLinearColor& Color = FLinearColor(0.9f, 0.9f, 0.6f), FName Sound = TEXT("message"));
	void ShowMessageLocal(const FString& Text, const FLinearColor& Color, FName Sound);

	// Flow -------------------------------------------------------------------
	UPROPERTY(Replicated)
	ECmdFlowState Flow = ECmdFlowState::None;

	UPROPERTY(Replicated)
	float MatchTime = 0.f;

	/** Networked match: why it ended ("Victory to ..."). */
	UPROPERTY(Replicated)
	FString MatchResultText;

	/** Flow as this machine should present it: a networked match ends in a victory for some and a defeat for others. */
	ECmdFlowState GetDisplayFlow() const;

	/** Local: false for spectators / menu / cinematics. */
	bool bShroudEnabled = true;

	// Multiplayer ------------------------------------------------------------
	bool IsNetworked() const { return GetNetMode() != NM_Standalone; }

	/** Host-edited lobby options (map size, theatre, credits, AI opponents). */
	UPROPERTY(Replicated)
	FCmdSkirmishSettings LobbySettings;

	/** Set by the server when the match starts; clients generate their battlefield from it. */
	UPROPERTY(ReplicatedUsing = OnRep_MatchConfig)
	FCmdMatchConfig MatchConfig;

	/** Cosmetic weapon fire: muzzle flash, sound, beam and (on clients) visual projectiles. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(FName WeaponId, FVector_NetQuantize Muzzle, ACmdEntity* Target, FVector_NetQuantize TargetLocation);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastEffect(uint8 Kind, FVector_NetQuantize A, FVector_NetQuantize B, FColor Color, float Size, bool bFlag);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSound(FName Sound, FVector_NetQuantize Location);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOre(const TArray<FCmdOreDelta>& Changes);

	/** Plays a positional sound on every machine (locally in single player). */
	void BroadcastSound(FName Sound, const FVector& Location);

	UPROPERTY()
	TObjectPtr<ACmdMissionDirector> MissionDirector;

	void RecalculatePower();

private:
	UFUNCTION()
	void OnRep_MatchConfig();

	/** Clients: build grid, terrain and camera once the config and our own seat are known. */
	void TryBuildClientWorld();
	void FlushOreChanges();

	void TickProduction(float DeltaSeconds);
	void TickShroud(float DeltaSeconds);
	void FinishProduction(FCmdTeam& Team, ECmdCategory Category);
	void SpawnProducedUnit(int32 TeamId, const FCmdEntityDef& Def);
	void CheckDefeat();

	TSet<FName> SeenTags;
	float ShroudTimer = 0.f;
	float PowerTimer = 0.f;
	float DefeatTimer = 0.f;
	float OreTimer = 0.f;
	float OreFlushTimer = 0.f;
	bool bClientWorldBuilt = false;
	bool bSpectatorView = false;

	/** Networked match: once our side is eliminated, lift the shroud and watch. */
	void UpdateSpectatorView();
};
