#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CmdEnvironmentActor.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
struct FCmdMapTheme;

/** Sun, sky, atmosphere and fog for the procedurally generated battlefield. */
UCLASS()
class COMMANDERS_API ACmdEnvironmentActor : public AActor
{
	GENERATED_BODY()

public:
	ACmdEnvironmentActor();

	void ApplyTheme(const FCmdMapTheme& Theme);

	/** Used by cinematics for dramatic lighting changes. */
	void SetSunRotation(float Pitch, float Yaw);

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UExponentialHeightFogComponent> Fog;
};
