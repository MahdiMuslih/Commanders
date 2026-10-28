#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CmdEffect.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

/**
 * Lightweight procedural VFX: explosions, muzzle flashes, energy beams and smoke puffs.
 * No particle assets required; each effect is a few tinted primitives plus a light.
 */
UCLASS()
class COMMANDERS_API ACmdEffect : public AActor
{
	GENERATED_BODY()

public:
	ACmdEffect();

	static void SpawnExplosion(UWorld* World, const FVector& Location, float InSize, bool bWithDebris);
	static void SpawnFlash(UWorld* World, const FVector& Location, const FLinearColor& Color, float InSize);
	static void SpawnBeam(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color);
	static void SpawnSmoke(UWorld* World, const FVector& Location, float InSize);

	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EKind : uint8 { Explosion, Flash, Beam, Smoke, Debris };

	static ACmdEffect* SpawnBase(UWorld* World, const FVector& Location, EKind InKind, float InLifetime);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Core;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Light;

	EKind Kind = EKind::Flash;
	float Age = 0.f;
	float Lifetime = 0.3f;
	float Size = 1.f;
	float LightPeak = 0.f;
	FVector Velocity = FVector::ZeroVector;
	FVector BaseScale = FVector::OneVector;
};
