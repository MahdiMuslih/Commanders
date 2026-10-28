#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CmdDataRegistry.h"
#include "CmdProjectile.generated.h"

class ACmdEntity;
class UStaticMeshComponent;
class UPointLightComponent;

/** Shells, rockets, bullets and fireballs. Homing unless the weapon lobs (artillery). */
UCLASS()
class COMMANDERS_API ACmdProjectile : public AActor
{
	GENERATED_BODY()

public:
	ACmdProjectile();

	/**
	 * Fires a projectile. Cosmetic projectiles (clients in a networked game) fly and burst
	 * exactly like real ones but deal no damage: the server's real projectile does that.
	 */
	static ACmdProjectile* Launch(UWorld* World, ACmdEntity* InShooter, const FCmdWeaponDef& InWeapon, const FVector& From, ACmdEntity* InTarget, const FVector& TargetLocation, float InDelay = 0.f, bool bInCosmetic = false);

	/** Applies direct + splash damage at a point. Shared by projectiles and instant weapons. */
	static void ApplyImpact(UWorld* World, ACmdEntity* InShooter, int32 InShooterTeam, const FCmdWeaponDef& InWeapon, ACmdEntity* DirectTarget, const FVector& Location, bool bSpawnEffects = true);

	/** The impact flash / explosion for a weapon, on this machine only. */
	static void SpawnImpactEffects(UWorld* World, const FCmdWeaponDef& InWeapon, const FVector& Location);

	virtual void Tick(float DeltaSeconds) override;

private:
	void Detonate();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Light;

	FCmdWeaponDef Weapon;
	TWeakObjectPtr<ACmdEntity> Shooter;
	TWeakObjectPtr<ACmdEntity> Target;
	int32 ShooterTeam = 0;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	float Distance = 1.f;
	float Travelled = 0.f;
	float Delay = 0.f;
	bool bHoming = true;
	bool bCosmetic = false;
};
