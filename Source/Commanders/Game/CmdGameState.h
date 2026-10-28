#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Core/CmdTypes.h"
#include "CmdGameState.generated.h"

class ACmdEntity;
class ACmdBuilding;
class ACmdMissionDirector;
struct FCmdEntityDef;

struct FCmdProductionQueue
{
	TArray<FName> Items;		// front item is in progress
	float Progress = 0.f;		// 0..1 for the front item
	float Spent = 0.f;			// credits sunk into the front item
	bool bOnHold = false;
	bool bReady = false;		// structure finished, waiting to be placed
};

struct FCmdTeamStats
{
	int32 UnitsBuilt = 0;
	int32 UnitsLost = 0;
	int32 BuildingsBuilt = 0;
	int32 BuildingsLost = 0;
	int32 Kills = 0;
	float CreditsHarvested = 0.f;
	float CreditsSpent = 0.f;
};

struct FCmdTeam
{
	int32 Id = 0;
	FName Faction;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	int32 Alliance = 0;
	float Credits = 0.f;
	bool bIsLocalPlayer = false;
	bool bIsAI = false;
	bool bIsNeutral = false;
	bool bDefeated = false;
	bool bCanBuild = true;			// missions can disable base building
	float BuildSpeed = 1.f;			// AI difficulty / mission tuning
	int32 PowerProduced = 0;
	int32 PowerConsumed = 0;
	FCmdProductionQueue Queues[CmdConst::CategoryCount];
	TMap<FName, int32> Owned;		// live counts per entity id
	TArray<FName> TechLock;			// entity ids that are disabled for this team
	FCmdTeamStats Stats;
	FIntPoint StartCell = FIntPoint::ZeroValue;

	bool HasLowPower() const { return PowerConsumed > PowerProduced; }
	float PowerFactor() const
	{
		if (!HasLowPower()) return 1.f;
		return PowerProduced <= 0 ? 0.25f : FMath::Clamp(static_cast<float>(PowerProduced) / PowerConsumed, 0.25f, 1.f);
	}
	int32 CountOwned(FName Id) const { const int32* C = Owned.Find(Id); return C ? *C : 0; }
};

struct FCmdMessage
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
	float Time = 0.f;
};

/**
 * Authoritative match state. Owns the team list, the entity registry,
 * the sidebar production logic and routes gameplay events to the mission director.
 */
UCLASS()
class COMMANDERS_API ACmdGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ACmdGameState();

	virtual void Tick(float DeltaSeconds) override;

	// Teams ------------------------------------------------------------------
	TArray<FCmdTeam> Teams;
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
	void AddMessage(int32 TeamId, const FString& Text, const FLinearColor& Color = FLinearColor(0.9f, 0.9f, 0.6f));

	// Flow -------------------------------------------------------------------
	ECmdFlowState Flow = ECmdFlowState::None;
	float MatchTime = 0.f;
	bool bShroudEnabled = true;

	UPROPERTY()
	TObjectPtr<ACmdMissionDirector> MissionDirector;

	void RecalculatePower();

private:
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
	float LastAttackWarning = -100.f;
	bool bLowPowerWarned = false;
};
