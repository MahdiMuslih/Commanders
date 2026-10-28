#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/CmdTypes.h"
#include "Engine/NetSerialization.h"
#include "CmdEntity.generated.h"

struct FCmdEntityDef;
struct FCmdWeaponDef;
class UStaticMeshComponent;
class ACmdGameState;

/**
 * Base class for everything that has an owner, hit points and (optionally) a gun:
 * infantry, vehicles, aircraft and structures.
 */
UCLASS(Abstract)
class COMMANDERS_API ACmdEntity : public AActor
{
	GENERATED_BODY()

public:
	ACmdEntity();

	/** Must be called between SpawnActorDeferred and FinishSpawning. */
	virtual void Setup(FName InDefId, int32 InTeamId);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Identity (replicated) ------------------------------------------------
	const FCmdEntityDef* Def = nullptr;

	UPROPERTY(Replicated)
	FName DefId;

	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = 0;

	UPROPERTY(Replicated)
	FName MissionTag;

	// State ----------------------------------------------------------------
	UPROPERTY(Replicated)
	float Health = 1.f;

	UPROPERTY(Replicated)
	bool bInvulnerable = false;

	UPROPERTY(Replicated)
	int32 Kills = 0;

	bool bSelected = false;
	bool bDead = false;
	float LastDamagedTime = -1000.f;
	TWeakObjectPtr<ACmdEntity> LastAttacker;

	/** True on the machine that simulates this entity (standalone, or the server). */
	bool IsSimulated() const { return HasAuthority(); }

	virtual bool IsBuilding() const { return false; }
	bool IsAircraft() const;
	bool IsInfantry() const;
	bool IsAlive() const { return !bDead && Health > 0.f && !IsActorBeingDestroyed(); }
	float GetHealthPct() const;
	float GetMaxHealth() const;

	virtual FIntPoint GetCell() const;
	virtual FVector GetAimPoint() const;
	float GetRadius() const;
	float GetBarHeight() const;
	FLinearColor GetTeamColor() const;
	ACmdGameState* GetCmdGameState() const;

	virtual void SetTeam(int32 NewTeam);

	/** Re-tints the model (clients call this once the team list has replicated). */
	void RefreshTeamColor();
	virtual void ApplyDamage(float Amount, ECmdWarhead Warhead, ACmdEntity* DamageSource);
	virtual void Die(ACmdEntity* Killer);
	void Heal(float Amount);

	// Combat ---------------------------------------------------------------
	const FCmdWeaponDef* GetWeapon() const { return Weapon; }
	bool HasWeapon() const { return Weapon != nullptr; }
	bool CanTarget(const ACmdEntity* Other) const;
	bool IsHostileTo(const ACmdEntity* Other) const;
	bool IsInRange(const ACmdEntity* Other, float SlackCells = 0.f) const;
	float DistanceTo2D(const ACmdEntity* Other) const;
	ACmdEntity* FindBestTarget(float RangeCells) const;

	/** Explicit attack order target (player clicked an enemy). */
	void SetForcedTarget(ACmdEntity* NewTarget);
	ACmdEntity* GetCurrentTarget() const { return Target.Get(); }

	/** Shroud: hides enemies standing in unexplored cells for the local player. */
	void UpdateShroudVisibility(int32 ViewerAlliance);

	const TArray<UStaticMeshComponent*>& GetPartComponents() const { return PartComponents; }

protected:
	/** Looks up Def / Weapon from DefId (clients do this when the entity replicates in). */
	void ResolveDefinition();

	UFUNCTION()
	void OnRep_TeamId(int32 OldTeamId);

	/** Server: publish transform for clients. Clients: glide towards the published transform. */
	void PublishNetTransform();
	void TickNetSmoothing(float DeltaSeconds);

	UPROPERTY(Replicated)
	FVector_NetQuantize NetLocation;

	UPROPERTY(Replicated)
	uint16 NetYaw = 0;

	UPROPERTY(Replicated)
	uint16 NetTurretYaw = 0;

	virtual bool CanFireNow() const { return true; }
	virtual bool CanAutoAcquire() const { return true; }
	virtual bool NeedsToFaceTarget() const;
	virtual float GetAcquireRange() const;
	virtual void OnFired() {}

	void TickWeapon(float DeltaSeconds);
	void FireAt(ACmdEntity* At);
	void BuildVisuals();
	FVector GetMuzzleLocation() const;
	void RotateTurretTowards(const FVector& Location, float DeltaSeconds);
	float GetTurretWorldYaw() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Turret;

	TArray<UStaticMeshComponent*> PartComponents;
	TArray<TPair<UStaticMeshComponent*, float>> Spinners;

	const FCmdWeaponDef* Weapon = nullptr;
	TWeakObjectPtr<ACmdEntity> Target;
	bool bTargetForced = false;
	float WeaponCooldown = 0.f;
	float AcquireTimer = 0.f;
	float TurretYaw = 0.f;		// relative to body
	bool bHasTurret = false;
	bool bHiddenByShroud = false;
};
