#include "World/CmdEnvironmentActor.h"
#include "World/CmdTerrainActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"

ACmdEnvironmentActor::ACmdEnvironmentActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
}

void ACmdEnvironmentActor::ApplyTheme(const FCmdMapTheme& Theme)
{
	Sun->SetAtmosphereSunLight(true);
	Sun->SetLightColor(Theme.SunColor);
	Sun->SetIntensity(Theme.SunIntensity);
	SetSunRotation(Theme.SunPitch, Theme.SunYaw);

	SkyLight->SetIntensity(Theme.SkyIntensity);
	SkyLight->RecaptureSky();

	Fog->SetFogDensity(Theme.FogDensity);
	Fog->SetFogHeightFalloff(0.002f);
}

void ACmdEnvironmentActor::SetSunRotation(float Pitch, float Yaw)
{
	Sun->SetWorldRotation(FRotator(Pitch, Yaw, 0.f));
}
