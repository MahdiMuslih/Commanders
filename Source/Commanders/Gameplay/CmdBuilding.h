#pragma once

#include "CoreMinimal.h"
#include "Gameplay/CmdEntity.h"
#include "CmdBuilding.generated.h"

/** Static structures occupying a rectangular footprint on the grid. */
UCLASS()
class COMMANDERS_API ACmdBuilding : public ACmdEntity
{
	GENERATED_BODY()

public:
	ACmdBuilding();

	/** Call before FinishSpawning. */
	void SetOrigin(const FIntPoint& InOrigin, bool bAnimateConstruction);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsBuilding() const override { return true; }
	virtual FIntPoint GetCell() const override;
	virtual FVector GetAimPoint() const override;
	virtual void SetTeam(int32 NewTeam) override;
	virtual void Die(ACmdEntity* Killer) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	FIntPoint Origin = FIntPoint::ZeroValue;

	UPROPERTY(Replicated)
	FIntPoint RallyCell = FIntPoint(-1, -1);

	UPROPERTY(Replicated)
	bool bPrimary = false;

	UPROPERTY(Replicated)
	bool bRepairing = false;

	bool IsPowered() const;
	bool IsConstructed() const { return BuildProgress >= 1.f; }
	bool ContainsCell(const FIntPoint& C) const;
	FIntPoint GetExitCell() const;
	FIntPoint GetDockCell() const;
	FIntPoint GetNearestAdjacentCell(const FIntPoint& From) const;
	bool Produces(ECmdCategory Category) const;

	void Sell();
	void ToggleRepair();

protected:
	virtual bool CanFireNow() const override;
	virtual bool CanAutoAcquire() const override { return IsConstructed(); }

private:
	void TickRepair(float DeltaSeconds);
	void TickDamageSmoke(float DeltaSeconds);

	UPROPERTY(Replicated)
	float BuildProgress = 1.f;

	float SmokeTimer = 0.f;
	float RepairTimer = 0.f;
	float IncomeTimer = 0.f;
	bool bRegisteredOnGrid = false;
};
