#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CmdAICommander.generated.h"

class FJsonObject;
class ACmdGameState;
class ACmdEntity;
class ACmdUnit;
class UCmdGridSubsystem;
struct FCmdEntityDef;

/**
 * Computer opponent. Runs a classic RTS loop:
 *  - economy: power, refineries, harvesters
 *  - build order + base defences, placed with the same rules as the player
 *  - army production with a faction-aware unit mix
 *  - periodic attack waves that grow over time, plus base defence reactions
 * Missions can toggle building/attacking and tune timings through the "ai" action.
 */
UCLASS()
class COMMANDERS_API UCmdAICommander : public UObject
{
	GENERATED_BODY()

public:
	void Init(UWorld* InWorld, int32 InTeamId, int32 InDifficulty);
	void Configure(const TSharedPtr<FJsonObject>& Json);
	void Tick(float DeltaSeconds);
	void LaunchAttack(const FIntPoint& TargetCell);

	int32 TeamId = 1;
	int32 Difficulty = 1;		// 0 easy, 1 normal, 2 hard
	bool bEnabled = true;
	bool bBuildBase = true;
	bool bProduceUnits = true;
	bool bAttack = true;
	bool bDefend = true;
	float FirstAttackDelay = 300.f;
	float AttackInterval = 180.f;
	int32 MinAttackSize = 6;
	float IncomeBonus = 0.f;		// credits / second trickle on harder settings
	FIntPoint FixedAttackTarget = FIntPoint(-1, -1);

	virtual UWorld* GetWorld() const override { return World.Get(); }

private:
	void ThinkDeploy();
	void ThinkEconomy();
	void ThinkProduction();
	void ThinkArmy();
	void PlaceReadyStructure();
	FName ChooseNextStructure() const;
	FName ChooseUnit(int32 Category) const;
	bool FindPlacement(const FCmdEntityDef& Def, FIntPoint& OutOrigin) const;
	ACmdEntity* PickAttackTarget() const;
	TArray<ACmdUnit*> GetArmy(bool bOnlyIdle) const;
	int32 Count(FName Id) const;
	ACmdGameState* GS() const;

	TWeakObjectPtr<UWorld> World;
	float ThinkTimer = 0.f;
	float AttackTimer = 0.f;
	float DefendTimer = 0.f;
	int32 WaveCount = 0;
	FRandomStream Rng;
};
