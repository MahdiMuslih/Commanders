#include "Gameplay/CmdEffect.h"
#include "World/CmdVisualSubsystem.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

ACmdEffect::ACmdEffect()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetGenerateOverlapEvents(false);
	RootComponent = Mesh;

	Core = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Core"));
	Core->SetupAttachment(Mesh);
	Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Core->SetCastShadow(false);
	Core->SetVisibility(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Mesh);
	Light->SetCastShadows(false);
	Light->SetIntensity(0.f);
}

ACmdEffect* ACmdEffect::SpawnBase(UWorld* World, const FVector& Location, EKind InKind, float InLifetime)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACmdEffect* E = World->SpawnActor<ACmdEffect>(ACmdEffect::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (E)
	{
		E->Kind = InKind;
		E->Lifetime = InLifetime;
	}
	return E;
}

void ACmdEffect::SpawnExplosion(UWorld* World, const FVector& Location, float InSize, bool bWithDebris)
{
	UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(World);
	ACmdEffect* E = SpawnBase(World, Location, EKind::Explosion, 0.55f + InSize * 0.15f);
	if (!E || !Visuals)
	{
		return;
	}
	E->Size = InSize;
	E->Mesh->SetStaticMesh(Visuals->GetMesh(TEXT("sphere")));
	E->Mesh->SetMaterial(0, Visuals->GetColorMaterial(FLinearColor(4.f, 1.4f, 0.2f)));
	E->Core->SetStaticMesh(Visuals->GetMesh(TEXT("sphere")));
	E->Core->SetMaterial(0, Visuals->GetColorMaterial(FLinearColor(6.f, 5.f, 2.f)));
	E->Core->SetRelativeScale3D(FVector(0.6f));
	E->Core->SetVisibility(true);
	E->BaseScale = FVector(InSize);
	E->Mesh->SetWorldScale3D(FVector(0.1f));
	E->LightPeak = 20000.f * InSize;
	E->Light->SetLightColor(FLinearColor(1.f, 0.55f, 0.2f));
	E->Light->SetAttenuationRadius(400.f + 500.f * InSize);
	E->Light->SetIntensity(E->LightPeak);

	SpawnSmoke(World, Location + FVector(0.f, 0.f, 30.f), InSize);
	if (bWithDebris)
	{
		const int32 Count = FMath::Clamp(FMath::RoundToInt(InSize * 4.f), 2, 10);
		for (int32 i = 0; i < Count; ++i)
		{
			ACmdEffect* D = SpawnBase(World, Location, EKind::Debris, FMath::FRandRange(0.6f, 1.1f));
			if (!D)
			{
				continue;
			}
			D->Mesh->SetStaticMesh(Visuals->GetMesh(TEXT("cube")));
			D->Mesh->SetMaterial(0, Visuals->GetColorMaterial(FLinearColor(0.05f, 0.05f, 0.05f)));
			D->BaseScale = FVector(FMath::FRandRange(0.08f, 0.18f) * FMath::Max(1.f, InSize * 0.7f));
			D->Mesh->SetWorldScale3D(D->BaseScale);
			D->Velocity = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(1.2f, 2.2f)) * 400.f * FMath::Max(0.7f, InSize * 0.6f);
		}
	}
}

void ACmdEffect::SpawnFlash(UWorld* World, const FVector& Location, const FLinearColor& Color, float InSize)
{
	UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(World);
	ACmdEffect* E = SpawnBase(World, Location, EKind::Flash, 0.12f + InSize * 0.1f);
	if (!E || !Visuals)
	{
		return;
	}
	E->Size = InSize;
	E->Mesh->SetStaticMesh(Visuals->GetMesh(TEXT("sphere")));
	E->Mesh->SetMaterial(0, Visuals->GetColorMaterial(Color * 5.f));
	E->BaseScale = FVector(InSize);
	E->Mesh->SetWorldScale3D(E->BaseScale);
	E->LightPeak = 6000.f * FMath::Max(0.3f, InSize);
	E->Light->SetLightColor(Color);
	E->Light->SetAttenuationRadius(250.f + 300.f * InSize);
	E->Light->SetIntensity(E->LightPeak);
}

void ACmdEffect::SpawnBeam(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color)
{
	UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(World);
	const FVector Mid = (From + To) * 0.5f;
	ACmdEffect* E = SpawnBase(World, Mid, EKind::Beam, 0.22f);
	if (!E || !Visuals)
	{
		return;
	}
	const FVector Dir = To - From;
	const float Length = Dir.Size();
	// Cylinder's axis is +Z; rotate it onto the beam direction.
	const FQuat Rot = FQuat::FindBetweenNormals(FVector::UpVector, Dir.GetSafeNormal());
	E->SetActorRotation(Rot);
	E->Mesh->SetStaticMesh(Visuals->GetMesh(TEXT("cylinder")));
	E->Mesh->SetMaterial(0, Visuals->GetColorMaterial(Color * 6.f));
	E->BaseScale = FVector(0.12f, 0.12f, Length / 100.f);
	E->Mesh->SetWorldScale3D(E->BaseScale);
	E->LightPeak = 15000.f;
	E->Light->SetLightColor(Color);
	E->Light->SetAttenuationRadius(FMath::Max(400.f, Length * 0.6f));
	E->Light->SetIntensity(E->LightPeak);
	SpawnFlash(World, To, Color, 0.35f);
}

void ACmdEffect::SpawnSmoke(UWorld* World, const FVector& Location, float InSize)
{
	UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(World);
	const int32 Count = FMath::Clamp(FMath::RoundToInt(InSize * 2.f), 1, 5);
	for (int32 i = 0; i < Count; ++i)
	{
		ACmdEffect* E = SpawnBase(World, Location + FVector(FMath::FRandRange(-40.f, 40.f) * InSize, FMath::FRandRange(-40.f, 40.f) * InSize, 0.f), EKind::Smoke, FMath::FRandRange(1.6f, 2.6f));
		if (!E || !Visuals)
		{
			continue;
		}
		E->Size = InSize;
		E->Mesh->SetStaticMesh(Visuals->GetMesh(TEXT("sphere")));
		E->Mesh->SetMaterial(0, Visuals->GetColorMaterial(FLinearColor(0.06f, 0.055f, 0.05f)));
		E->BaseScale = FVector(FMath::FRandRange(0.6f, 1.0f) * InSize);
		E->Mesh->SetWorldScale3D(E->BaseScale * 0.3f);
		E->Velocity = FVector(FMath::FRandRange(-20.f, 20.f), FMath::FRandRange(-20.f, 20.f), FMath::FRandRange(60.f, 110.f));
	}
}

void ACmdEffect::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	const float T = FMath::Clamp(Age / FMath::Max(0.01f, Lifetime), 0.f, 1.f);
	if (T >= 1.f)
	{
		Destroy();
		return;
	}

	switch (Kind)
	{
	case EKind::Explosion:
	{
		// Fast swell then collapse.
		const float Grow = T < 0.3f ? FMath::InterpEaseOut(0.1f, 1.f, T / 0.3f, 2.f) : FMath::Lerp(1.f, 0.05f, (T - 0.3f) / 0.7f);
		Mesh->SetWorldScale3D(BaseScale * Grow * 1.6f);
		Light->SetIntensity(LightPeak * (1.f - T) * (1.f - T));
		break;
	}
	case EKind::Flash:
		Mesh->SetWorldScale3D(BaseScale * (1.f - T * 0.6f));
		Light->SetIntensity(LightPeak * (1.f - T));
		break;
	case EKind::Beam:
	{
		const float Flicker = 0.6f + 0.4f * FMath::Sin(Age * 90.f);
		Mesh->SetWorldScale3D(FVector(BaseScale.X * Flicker * (1.f - T), BaseScale.Y * Flicker * (1.f - T), BaseScale.Z));
		Light->SetIntensity(LightPeak * (1.f - T));
		break;
	}
	case EKind::Smoke:
		AddActorWorldOffset(Velocity * DeltaSeconds);
		Mesh->SetWorldScale3D(BaseScale * FMath::Lerp(0.3f, 1.6f, T) * (T > 0.75f ? (1.f - (T - 0.75f) * 4.f) : 1.f));
		break;
	case EKind::Debris:
		Velocity.Z -= 1800.f * DeltaSeconds;
		AddActorWorldOffset(Velocity * DeltaSeconds);
		AddActorLocalRotation(FRotator(400.f * DeltaSeconds, 300.f * DeltaSeconds, 0.f));
		if (GetActorLocation().Z < 0.f)
		{
			Velocity = FVector::ZeroVector;
		}
		break;
	}
}
