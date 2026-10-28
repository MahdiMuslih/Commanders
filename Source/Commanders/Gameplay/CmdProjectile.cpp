#include "Gameplay/CmdProjectile.h"
#include "Gameplay/CmdEntity.h"
#include "Gameplay/CmdEffect.h"
#include "Game/CmdGameState.h"
#include "World/CmdVisualSubsystem.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

ACmdProjectile::ACmdProjectile()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetGenerateOverlapEvents(false);
	RootComponent = Mesh;

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Mesh);
	Light->SetCastShadows(false);
	Light->SetIntensity(0.f);
}

ACmdProjectile* ACmdProjectile::Launch(UWorld* World, ACmdEntity* InShooter, const FCmdWeaponDef& InWeapon, const FVector& From, ACmdEntity* InTarget, const FVector& TargetLocation, float InDelay)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACmdProjectile* P = World->SpawnActor<ACmdProjectile>(ACmdProjectile::StaticClass(), From, (TargetLocation - From).Rotation(), Params);
	if (!P)
	{
		return nullptr;
	}
	P->Weapon = InWeapon;
	P->Shooter = InShooter;
	P->ShooterTeam = InShooter ? InShooter->TeamId : 0;
	P->Target = InTarget;
	P->Start = From;
	P->End = TargetLocation;
	P->Distance = FMath::Max(1.f, FVector::Dist(From, TargetLocation));
	P->Delay = InDelay;
	P->bHoming = InWeapon.ArcHeight <= 0.f;

	if (UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(World))
	{
		P->Mesh->SetStaticMesh(Visuals->GetMesh(TEXT("sphere")));
		P->Mesh->SetMaterial(0, Visuals->GetColorMaterial(InWeapon.Color * 3.f));
	}
	// Bullets are stretched tracers, shells are round.
	const float S = InWeapon.ProjectileScale;
	const bool bTracer = InWeapon.ProjectileSpeed > 5000.f;
	P->Mesh->SetWorldScale3D(bTracer ? FVector(S * 4.f, S * 0.6f, S * 0.6f) : FVector(S));
	if (S >= 0.12f)
	{
		P->Light->SetLightColor(InWeapon.Color);
		P->Light->SetIntensity(3000.f);
		P->Light->SetAttenuationRadius(300.f);
	}
	P->SetActorHiddenInGame(InDelay > 0.f);
	return P;
}

void ACmdProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Delay > 0.f)
	{
		Delay -= DeltaSeconds;
		if (Delay <= 0.f)
		{
			SetActorHiddenInGame(false);
		}
		return;
	}

	if (bHoming)
	{
		if (ACmdEntity* T = Target.Get())
		{
			if (T->IsAlive())
			{
				End = T->GetAimPoint();
			}
		}
	}

	Distance = FMath::Max(Distance, 1.f);
	Travelled += Weapon.ProjectileSpeed * DeltaSeconds;

	FVector NewLocation;
	if (bHoming)
	{
		const FVector Current = GetActorLocation();
		const FVector ToEnd = End - Current;
		const float Remaining = ToEnd.Size();
		const float Step = Weapon.ProjectileSpeed * DeltaSeconds;
		if (Step >= Remaining)
		{
			SetActorLocation(End);
			Detonate();
			return;
		}
		NewLocation = Current + ToEnd / Remaining * Step;
	}
	else
	{
		const float Alpha = FMath::Clamp(Travelled / Distance, 0.f, 1.f);
		NewLocation = FMath::Lerp(Start, End, Alpha);
		NewLocation.Z += Weapon.ArcHeight * 4.f * Alpha * (1.f - Alpha);
		if (Alpha >= 1.f)
		{
			SetActorLocation(End);
			Detonate();
			return;
		}
	}

	const FVector Dir = NewLocation - GetActorLocation();
	if (!Dir.IsNearlyZero())
	{
		SetActorRotation(Dir.Rotation());
	}
	SetActorLocation(NewLocation);

	if (Travelled > Distance * 3.f + 4000.f)
	{
		Destroy();	// safety net
	}
}

void ACmdProjectile::Detonate()
{
	ACmdEntity* DirectTarget = Target.Get();
	// Lobbed shells only hit what is standing where they land.
	if (DirectTarget && !bHoming && FVector::Dist2D(DirectTarget->GetActorLocation(), End) > DirectTarget->GetRadius() + 60.f)
	{
		DirectTarget = nullptr;
	}
	ApplyImpact(GetWorld(), Shooter.Get(), ShooterTeam, Weapon, DirectTarget, GetActorLocation());
	Destroy();
}

void ACmdProjectile::ApplyImpact(UWorld* World, ACmdEntity* InShooter, int32 InShooterTeam, const FCmdWeaponDef& InWeapon, ACmdEntity* DirectTarget, const FVector& Location)
{
	if (!World)
	{
		return;
	}
	const bool bBig = InWeapon.Splash > 0.4f || InWeapon.Warhead == ECmdWarhead::HighExplosive;
	if (InWeapon.Warhead == ECmdWarhead::Fire)
	{
		ACmdEffect::SpawnFlash(World, Location, FLinearColor(1.f, 0.35f, 0.05f), 0.6f + InWeapon.Splash);
	}
	else if (bBig)
	{
		ACmdEffect::SpawnExplosion(World, Location, 0.4f + InWeapon.Splash * 0.6f, false);
	}
	else
	{
		ACmdEffect::SpawnFlash(World, Location, InWeapon.Color, 0.15f);
	}

	if (DirectTarget && DirectTarget->IsAlive())
	{
		DirectTarget->ApplyDamage(InWeapon.Damage, InWeapon.Warhead, InShooter);
	}

	if (InWeapon.Splash <= 0.f)
	{
		return;
	}

	ACmdGameState* GS = World->GetGameState<ACmdGameState>();
	if (!GS)
	{
		return;
	}
	const float Radius = InWeapon.Splash * CmdConst::CellSize;
	TArray<ACmdEntity*> Victims;
	for (ACmdEntity* E : GS->Entities)
	{
		if (!E || E == DirectTarget || !E->IsAlive() || GS->AreAllied(InShooterTeam, E->TeamId))
		{
			continue;
		}
		if (E->IsAircraft() && !InWeapon.bTargetAir)
		{
			continue;
		}
		const float D = FMath::Max(0.f, FVector::Dist2D(E->GetActorLocation(), Location) - E->GetRadius() * 0.5f);
		if (D <= Radius)
		{
			Victims.Add(E);
		}
	}
	for (ACmdEntity* E : Victims)
	{
		const float D = FMath::Max(0.f, FVector::Dist2D(E->GetActorLocation(), Location) - E->GetRadius() * 0.5f);
		const float Falloff = 1.f - 0.6f * (D / FMath::Max(1.f, Radius));
		if (IsValid(E) && E->IsAlive())
		{
			E->ApplyDamage(InWeapon.Damage * 0.6f * Falloff, InWeapon.Warhead, InShooter);
		}
	}
}
