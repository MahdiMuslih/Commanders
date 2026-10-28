#include "Gameplay/CmdEntity.h"
#include "Audio/CmdAudioSubsystem.h"
#include "Gameplay/CmdProjectile.h"
#include "Gameplay/CmdEffect.h"
#include "Game/CmdGameState.h"
#include "Data/CmdDataRegistry.h"
#include "World/CmdGridSubsystem.h"
#include "World/CmdVisualSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

ACmdEntity::ACmdEntity()
{
	PrimaryActorTick.bCanEverTick = true;

	// The server simulates; clients receive identity, health and a smoothed transform.
	// Every entity is always relevant: the shroud, not distance, decides what a player sees.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	NetUpdateFrequency = 10.f;
	MinNetUpdateFrequency = 2.f;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);

	Turret = CreateDefaultSubobject<USceneComponent>(TEXT("Turret"));
	Turret->SetupAttachment(Body);
}

void ACmdEntity::Setup(FName InDefId, int32 InTeamId)
{
	DefId = InDefId;
	TeamId = InTeamId;
	ResolveDefinition();
	Health = GetMaxHealth();
}

void ACmdEntity::ResolveDefinition()
{
	if (const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this))
	{
		Def = Data->GetEntity(DefId);
		Weapon = Def ? Data->GetWeapon(Def->Weapon) : nullptr;
	}
	bHasTurret = Def && Def->HasTurret();
}

void ACmdEntity::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACmdEntity, DefId);
	DOREPLIFETIME(ACmdEntity, TeamId);
	DOREPLIFETIME(ACmdEntity, MissionTag);
	DOREPLIFETIME(ACmdEntity, Health);
	DOREPLIFETIME(ACmdEntity, bInvulnerable);
	DOREPLIFETIME(ACmdEntity, Kills);
	DOREPLIFETIME(ACmdEntity, NetLocation);
	DOREPLIFETIME(ACmdEntity, NetYaw);
	DOREPLIFETIME(ACmdEntity, NetTurretYaw);
}

void ACmdEntity::RefreshTeamColor()
{
	UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(this);
	if (Visuals && Def)
	{
		Visuals->RecolorParts(PartComponents, Def->Parts, GetTeamColor());
	}
}

void ACmdEntity::OnRep_TeamId(int32 OldTeamId)
{
	// Captured on the server. The initial value arrives before BeginPlay and needs no handling.
	if (!HasActorBegunPlay() || OldTeamId == TeamId)
	{
		return;
	}
	RefreshTeamColor();
	if (ACmdGameState* GS = GetCmdGameState())
	{
		GS->OnEntityChangedTeam(this, OldTeamId);
	}
}

void ACmdEntity::PublishNetTransform()
{
	if (GetNetMode() == NM_Standalone)
	{
		return;
	}
	NetLocation = GetActorLocation();
	NetYaw = FRotator::CompressAxisToShort(GetActorRotation().Yaw);
	NetTurretYaw = FRotator::CompressAxisToShort(TurretYaw);
}

void ACmdEntity::TickNetSmoothing(float DeltaSeconds)
{
	const FVector Current = GetActorLocation();
	const FVector Desired = NetLocation;
	if (!IsBuilding())
	{
		if (FVector::DistSquared(Current, Desired) > FMath::Square(8.f * CmdConst::CellSize))
		{
			SetActorLocation(Desired);	// teleported (or first update after a hitch)
		}
		else
		{
			SetActorLocation(FMath::VInterpTo(Current, Desired, DeltaSeconds, 10.f));
		}
		const FRotator DesiredRot(0.f, FRotator::DecompressAxisFromShort(NetYaw), 0.f);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), DesiredRot, DeltaSeconds, 10.f));
	}
	if (bHasTurret)
	{
		const float DesiredTurret = FRotator::NormalizeAxis(FRotator::DecompressAxisFromShort(NetTurretYaw));
		TurretYaw = FRotator::NormalizeAxis(TurretYaw + FRotator::NormalizeAxis(DesiredTurret - TurretYaw) * FMath::Min(1.f, DeltaSeconds * 10.f));
		Turret->SetRelativeRotation(FRotator(0.f, TurretYaw, 0.f));
	}
}

void ACmdEntity::BeginPlay()
{
	if (!Def && !DefId.IsNone())
	{
		ResolveDefinition();	// replicated in on a client
	}
	if (HasAuthority())
	{
		PublishNetTransform();
	}
	Super::BeginPlay();
	BuildVisuals();
	AcquireTimer = FMath::FRand() * 0.5f;
	if (ACmdGameState* GS = GetCmdGameState())
	{
		GS->RegisterEntity(this);
	}
}

void ACmdEntity::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ACmdGameState* GS = GetCmdGameState())
	{
		GS->UnregisterEntity(this);
	}
	Super::EndPlay(Reason);
}

void ACmdEntity::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsAlive())
	{
		return;
	}
	if (HasAuthority())
	{
		TickWeapon(DeltaSeconds);
		PublishNetTransform();
	}
	else
	{
		TickNetSmoothing(DeltaSeconds);
	}

	for (const TPair<UStaticMeshComponent*, float>& Spinner : Spinners)
	{
		if (Spinner.Key)
		{
			Spinner.Key->AddLocalRotation(FRotator(0.f, Spinner.Value * DeltaSeconds, 0.f));
		}
	}
}

void ACmdEntity::BuildVisuals()
{
	if (!Def)
	{
		return;
	}
	if (UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(this))
	{
		Visuals->BuildParts(this, Body, Turret, Def->Parts, GetTeamColor(), PartComponents);
	}
	for (int32 i = 0; i < Def->Parts.Num() && i < PartComponents.Num(); ++i)
	{
		if (Def->Parts[i].Spin != 0.f)
		{
			Spinners.Emplace(PartComponents[i], Def->Parts[i].Spin);
		}
	}
}

bool ACmdEntity::IsAircraft() const
{
	return Def && Def->bAircraft;
}

bool ACmdEntity::IsInfantry() const
{
	return Def && Def->IsInfantry();
}

float ACmdEntity::GetMaxHealth() const
{
	return Def ? Def->MaxHealth : 100.f;
}

float ACmdEntity::GetHealthPct() const
{
	return FMath::Clamp(Health / FMath::Max(1.f, GetMaxHealth()), 0.f, 1.f);
}

FIntPoint ACmdEntity::GetCell() const
{
	return Cmd::WorldToCell(GetActorLocation());
}

FVector ACmdEntity::GetAimPoint() const
{
	return GetActorLocation() + FVector(0.f, 0.f, Def ? Def->Height * 0.4f : 40.f);
}

float ACmdEntity::GetRadius() const
{
	return Def ? Def->Radius : 50.f;
}

float ACmdEntity::GetBarHeight() const
{
	return Def ? Def->Height : 100.f;
}

ACmdGameState* ACmdEntity::GetCmdGameState() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetGameState<ACmdGameState>() : nullptr;
}

FLinearColor ACmdEntity::GetTeamColor() const
{
	const ACmdGameState* GS = GetCmdGameState();
	return GS ? GS->GetTeamColor(TeamId) : FLinearColor::Gray;
}

void ACmdEntity::SetTeam(int32 NewTeam)
{
	if (NewTeam == TeamId)
	{
		return;
	}
	ACmdGameState* GS = GetCmdGameState();
	const int32 OldTeam = TeamId;
	TeamId = NewTeam;
	Target = nullptr;
	bTargetForced = false;
	if (UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(this))
	{
		if (Def)
		{
			Visuals->RecolorParts(PartComponents, Def->Parts, GetTeamColor());
		}
	}
	if (GS)
	{
		GS->OnEntityChangedTeam(this, OldTeam);
	}
}

void ACmdEntity::ApplyDamage(float Amount, ECmdWarhead Warhead, ACmdEntity* DamageSource)
{
	if (!IsAlive() || bInvulnerable || !Def)
	{
		return;
	}
	const float Final = Amount * Cmd::DamageMultiplier(Warhead, Def->Armor);
	if (Final <= 0.f)
	{
		return;
	}
	Health -= Final;
	LastDamagedTime = GetWorld()->GetTimeSeconds();
	if (DamageSource && DamageSource != this)
	{
		LastAttacker = DamageSource;
	}
	if (ACmdGameState* GS = GetCmdGameState())
	{
		GS->OnEntityDamaged(this, DamageSource);
	}
	if (Health <= 0.f)
	{
		Health = 0.f;
		Die(DamageSource);
	}
}

void ACmdEntity::Heal(float Amount)
{
	if (IsAlive())
	{
		Health = FMath::Min(GetMaxHealth(), Health + Amount);
	}
}

void ACmdEntity::Die(ACmdEntity* Killer)
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	bSelected = false;

	const float Size = IsInfantry() ? 0.5f : (IsBuilding() ? 2.5f : 1.3f);
	ACmdEffect::SpawnExplosion(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 40.f), Size, !IsInfantry());
	ACmdGameState* GS = GetCmdGameState();
	if (GS && IsInfantry())
	{
		GS->BroadcastSound(TEXT("death"), GetActorLocation());
	}
	else if (GS && IsBuilding())
	{
		GS->BroadcastSound(TEXT("collapse"), GetActorLocation());
	}

	if (Killer && Killer != this)
	{
		++Killer->Kills;
	}
	if (GS)
	{
		GS->OnEntityKilled(this, Killer);
	}
	Destroy();
}

bool ACmdEntity::IsHostileTo(const ACmdEntity* Other) const
{
	const ACmdGameState* GS = GetCmdGameState();
	return Other && GS && GS->AreHostile(TeamId, Other->TeamId);
}

bool ACmdEntity::CanTarget(const ACmdEntity* Other) const
{
	if (!Weapon || !Other || Other == this || !Other->IsAlive())
	{
		return false;
	}
	if (Other->IsAircraft() ? !Weapon->bTargetAir : !Weapon->bTargetGround)
	{
		return false;
	}
	return true;
}

float ACmdEntity::DistanceTo2D(const ACmdEntity* Other) const
{
	if (!Other)
	{
		return TNumericLimits<float>::Max();
	}
	const float D = FVector::Dist2D(GetActorLocation(), Other->GetActorLocation());
	// Structures are big: measure to their edge.
	return Other->IsBuilding() ? FMath::Max(0.f, D - Other->GetRadius() * 0.8f) : D;
}

bool ACmdEntity::IsInRange(const ACmdEntity* Other, float SlackCells) const
{
	if (!Weapon || !Other)
	{
		return false;
	}
	const float D = DistanceTo2D(Other);
	return D <= (Weapon->Range + SlackCells) * CmdConst::CellSize && D >= Weapon->MinRange * CmdConst::CellSize;
}

float ACmdEntity::GetAcquireRange() const
{
	return Weapon ? Weapon->Range : 0.f;
}

ACmdEntity* ACmdEntity::FindBestTarget(float RangeCells) const
{
	const ACmdGameState* GS = GetCmdGameState();
	if (!GS || !Weapon)
	{
		return nullptr;
	}
	const float MaxDist = RangeCells * CmdConst::CellSize;
	ACmdEntity* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (ACmdEntity* Other : GS->Entities)
	{
		if (!Other || !Other->IsAlive() || !IsHostileTo(Other) || GS->IsNeutralTeam(Other->TeamId) || !CanTarget(Other))
		{
			continue;
		}
		const float D = DistanceTo2D(Other);
		if (D > MaxDist || D < Weapon->MinRange * CmdConst::CellSize)
		{
			continue;
		}
		// Prefer things that shoot back, then wounded targets, then the closest.
		float Score = D;
		if (!Other->HasWeapon()) Score += 600.f;
		if (Other->IsBuilding()) Score += 300.f;
		Score -= (1.f - Other->GetHealthPct()) * 200.f;
		Score -= Cmd::DamageMultiplier(Weapon->Warhead, Other->Def ? Other->Def->Armor : ECmdArmor::None) * 300.f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Other;
		}
	}
	return Best;
}

void ACmdEntity::SetForcedTarget(ACmdEntity* NewTarget)
{
	Target = NewTarget;
	bTargetForced = NewTarget != nullptr;
}

bool ACmdEntity::NeedsToFaceTarget() const
{
	return !bHasTurret && !IsBuilding();
}

float ACmdEntity::GetTurretWorldYaw() const
{
	return GetActorRotation().Yaw + TurretYaw;
}

void ACmdEntity::RotateTurretTowards(const FVector& Location, float DeltaSeconds)
{
	if (!bHasTurret)
	{
		return;
	}
	const FVector Dir = Location - GetActorLocation();
	const float Desired = FRotator::NormalizeAxis(Dir.Rotation().Yaw - GetActorRotation().Yaw);
	const float Rate = (Def ? FMath::Max(Def->TurnRate, 90.f) : 180.f) * 1.5f;
	const float Delta = FRotator::NormalizeAxis(Desired - TurretYaw);
	TurretYaw = FRotator::NormalizeAxis(TurretYaw + FMath::Clamp(Delta, -Rate * DeltaSeconds, Rate * DeltaSeconds));
	Turret->SetRelativeRotation(FRotator(0.f, TurretYaw, 0.f));
}

FVector ACmdEntity::GetMuzzleLocation() const
{
	const FVector Offset = Def ? Def->MuzzleOffset : FVector(50.f, 0.f, 50.f);
	if (bHasTurret)
	{
		return Turret->GetComponentTransform().TransformPosition(Offset);
	}
	return GetActorTransform().TransformPosition(Offset);
}

void ACmdEntity::TickWeapon(float DeltaSeconds)
{
	if (!Weapon)
	{
		return;
	}
	WeaponCooldown -= DeltaSeconds;

	ACmdEntity* T = Target.Get();
	if (T && (!T->IsAlive() || !CanTarget(T) || (!bTargetForced && !IsHostileTo(T))))
	{
		Target = nullptr;
		bTargetForced = false;
		T = nullptr;
	}
	if (T && !bTargetForced && !IsInRange(T, 0.75f))
	{
		Target = nullptr;	// auto-acquired target walked away
		T = nullptr;
	}

	if (!T && CanAutoAcquire())
	{
		AcquireTimer -= DeltaSeconds;
		if (AcquireTimer <= 0.f)
		{
			AcquireTimer = 0.35f + FMath::FRand() * 0.25f;
			T = FindBestTarget(GetAcquireRange());
			Target = T;
		}
	}

	if (!T)
	{
		if (bHasTurret && FMath::Abs(TurretYaw) > 0.5f)
		{
			const float Rate = 90.f * DeltaSeconds;
			TurretYaw = FMath::Abs(TurretYaw) <= Rate ? 0.f : TurretYaw - FMath::Sign(TurretYaw) * Rate;
			Turret->SetRelativeRotation(FRotator(0.f, TurretYaw, 0.f));
		}
		return;
	}

	RotateTurretTowards(T->GetAimPoint(), DeltaSeconds);

	if (WeaponCooldown > 0.f || !CanFireNow() || !IsInRange(T))
	{
		return;
	}

	const float DesiredYaw = (T->GetActorLocation() - GetActorLocation()).Rotation().Yaw;
	if (bHasTurret)
	{
		if (FMath::Abs(FRotator::NormalizeAxis(DesiredYaw - GetTurretWorldYaw())) > 12.f)
		{
			return;
		}
	}
	else if (NeedsToFaceTarget() && !IsAircraft())
	{
		if (FMath::Abs(FRotator::NormalizeAxis(DesiredYaw - GetActorRotation().Yaw)) > 25.f)
		{
			return;
		}
	}

	FireAt(T);
}

void ACmdEntity::FireAt(ACmdEntity* At)
{
	if (!Weapon || !At)
	{
		return;
	}
	WeaponCooldown = Weapon->Cooldown * FMath::FRandRange(0.95f, 1.05f);
	const FVector Muzzle = GetMuzzleLocation();

	// Sound, muzzle flash, beams and (on clients) look-alike projectiles: one broadcast per volley.
	// In single player the multicast simply runs locally.
	if (ACmdGameState* GS = GetCmdGameState())
	{
		GS->MulticastShot(Weapon->Id, Muzzle, At, At->GetAimPoint());
	}

	for (int32 i = 0; i < Weapon->Burst; ++i)
	{
		if (Weapon->ProjectileSpeed <= 0.f)
		{
			// Instant beam weapons (arc towers, shock troopers). The beam itself was drawn by the broadcast.
			ACmdProjectile::ApplyImpact(GetWorld(), this, TeamId, *Weapon, At, At->GetAimPoint(), false);
		}
		else
		{
			const FVector Spread = Weapon->Burst > 1 ? FVector(FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-40.f, 40.f), 0.f) : FVector::ZeroVector;
			ACmdProjectile::Launch(GetWorld(), this, *Weapon, Muzzle + Spread * 0.3f, At, At->GetAimPoint() + Spread, i * 0.08f);
		}
	}
	OnFired();
}

void ACmdEntity::UpdateShroudVisibility(int32 ViewerAlliance)
{
	const ACmdGameState* GS = GetCmdGameState();
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	if (!GS || !Grid)
	{
		return;
	}
	bool bVisible = true;
	if (ViewerAlliance >= 0 && GS->GetAlliance(TeamId) != ViewerAlliance)
	{
		bVisible = Grid->IsExplored(ViewerAlliance, GetCell());
	}
	if (bVisible == bHiddenByShroud)
	{
		bHiddenByShroud = !bVisible;
		SetActorHiddenInGame(bHiddenByShroud);
	}
}
