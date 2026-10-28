#pragma once

#include "CoreMinimal.h"
#include "Gameplay/CmdEntity.h"
#include "CmdUnit.generated.h"

class ACmdBuilding;

/** Infantry, vehicles and aircraft. */
UCLASS()
class COMMANDERS_API ACmdUnit : public ACmdEntity
{
	GENERATED_BODY()

public:
	ACmdUnit();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Orders ---------------------------------------------------------------
	void OrderMove(const FIntPoint& Cell, bool bAttackMove);
	void OrderAttack(ACmdEntity* NewTarget);
	void OrderStop();
	void OrderGuard();
	void OrderHarvest(const FIntPoint* OreCell = nullptr);
	void OrderCapture(ACmdBuilding* Building);
	void OrderDock(ACmdBuilding* Refinery);
	bool CanDeploy() const;
	bool TryDeploy();

	ECmdOrder GetOrder() const { return Order; }
	bool IsMoving() const { return HasAuthority() ? PathIndex < Path.Num() : bNetMoving; }
	bool IsHarvester() const;
	bool IsEngineer() const;
	int32 GetCargo() const { return Cargo; }
	FIntPoint GetGoalCell() const { return Goal; }

	/** Used by scripted reinforcements that fly/drive in from the map edge. */
	void SetInitialAltitude(float Z);

protected:
	virtual bool CanAutoAcquire() const override;
	virtual float GetAcquireRange() const override;

private:
	enum class EHarvest : uint8 { None, SeekOre, ToOre, Harvesting, SeekRefinery, ToRefinery, Unloading, Waiting };

	void SetPathTo(const FIntPoint& Cell);
	void SetPathToWorld(const FVector& Location);
	void ClearPath();
	bool FollowPath(float DeltaSeconds);	// true when arrived
	float TurnTowards(float DesiredYaw, float DeltaSeconds);
	void TickOrder(float DeltaSeconds);
	void TickHarvester(float DeltaSeconds);
	void TickIdleResponse();
	void ApplySeparation(float DeltaSeconds);
	void UpdateAltitude(float DeltaSeconds);
	ACmdBuilding* FindNearestRefinery() const;
	float GetSpeedUU() const;

	UPROPERTY(Replicated)
	ECmdOrder Order = ECmdOrder::Idle;

	UPROPERTY(Replicated)
	bool bNetMoving = false;

	FIntPoint Goal = FIntPoint::ZeroValue;
	TWeakObjectPtr<ACmdEntity> OrderTarget;
	TArray<FVector> Path;
	int32 PathIndex = 0;
	float RepathTimer = 0.f;
	float StuckTimer = 0.f;
	int32 StuckCount = 0;
	FVector LastProgressLocation = FVector::ZeroVector;
	FIntPoint GuardCell = FIntPoint::ZeroValue;
	bool bReturningToGuard = false;
	float Altitude = 0.f;
	float BobPhase = 0.f;

	// Harvester
	EHarvest Harvest = EHarvest::None;

	UPROPERTY(Replicated)
	int32 Cargo = 0;

	float HarvestTimer = 0.f;
	FIntPoint OreCell = FIntPoint(-1, -1);
	TWeakObjectPtr<ACmdBuilding> DockTarget;
};
