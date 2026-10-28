#include "Gameplay/CmdUnit.h"
#include "Gameplay/CmdBuilding.h"
#include "Gameplay/CmdSpawner.h"
#include "Gameplay/CmdEffect.h"
#include "Game/CmdGameState.h"
#include "Data/CmdDataRegistry.h"
#include "World/CmdGridSubsystem.h"
#include "Engine/World.h"

namespace
{
	constexpr float LeashCells = 7.f;
	constexpr float HarvestInterval = 0.35f;
	constexpr float UnloadInterval = 0.06f;
}

ACmdUnit::ACmdUnit()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ACmdUnit::BeginPlay()
{
	Super::BeginPlay();
	GuardCell = GetCell();
	Goal = GuardCell;
	LastProgressLocation = GetActorLocation();
	BobPhase = FMath::FRand() * 6.f;
	if (IsAircraft())
	{
		Altitude = GetActorLocation().Z;
	}
	if (IsHarvester())
	{
		OrderHarvest();
	}
}

bool ACmdUnit::IsHarvester() const
{
	return Def && Def->bHarvester;
}

bool ACmdUnit::IsEngineer() const
{
	return Def && Def->bEngineer;
}

float ACmdUnit::GetSpeedUU() const
{
	return (Def ? Def->Speed : 2.f) * CmdConst::CellSize;
}

void ACmdUnit::SetInitialAltitude(float Z)
{
	Altitude = Z;
	FVector L = GetActorLocation();
	L.Z = Z;
	SetActorLocation(L);
}

// ---------------------------------------------------------------------------
// Orders
// ---------------------------------------------------------------------------

void ACmdUnit::OrderMove(const FIntPoint& Cell, bool bAttackMove)
{
	Order = bAttackMove ? ECmdOrder::AttackMove : ECmdOrder::Move;
	Harvest = EHarvest::None;
	OrderTarget = nullptr;
	SetForcedTarget(nullptr);
	if (!bAttackMove)
	{
		Target = nullptr;
	}
	Goal = Cell;
	GuardCell = Cell;
	bReturningToGuard = false;
	SetPathTo(Cell);
}

void ACmdUnit::OrderAttack(ACmdEntity* NewTarget)
{
	if (!NewTarget || !HasWeapon() || !CanTarget(NewTarget))
	{
		return;
	}
	Order = ECmdOrder::Attack;
	Harvest = EHarvest::None;
	OrderTarget = NewTarget;
	SetForcedTarget(NewTarget);
	ClearPath();
	RepathTimer = 0.f;
}

void ACmdUnit::OrderStop()
{
	Order = ECmdOrder::Idle;
	Harvest = EHarvest::None;
	OrderTarget = nullptr;
	SetForcedTarget(nullptr);
	ClearPath();
	GuardCell = GetCell();
	bReturningToGuard = false;
}

void ACmdUnit::OrderGuard()
{
	OrderStop();
	Order = ECmdOrder::Guard;
}

void ACmdUnit::OrderHarvest(const FIntPoint* InOreCell)
{
	if (!IsHarvester())
	{
		return;
	}
	Order = ECmdOrder::Harvest;
	if (InOreCell)
	{
		OreCell = *InOreCell;
		SetPathTo(OreCell);
		Harvest = Cargo >= Def->HarvestCapacity ? EHarvest::SeekRefinery : EHarvest::ToOre;
	}
	else
	{
		Harvest = Cargo >= Def->HarvestCapacity ? EHarvest::SeekRefinery : EHarvest::SeekOre;
	}
}

void ACmdUnit::OrderCapture(ACmdBuilding* Building)
{
	if (!IsEngineer() || !Building)
	{
		return;
	}
	Order = ECmdOrder::Capture;
	OrderTarget = Building;
	const FIntPoint Approach = Building->GetNearestAdjacentCell(GetCell());
	SetPathTo(Approach);
}

void ACmdUnit::OrderDock(ACmdBuilding* Refinery)
{
	if (!IsHarvester() || !Refinery || !Refinery->Def || !Refinery->Def->bRefinery)
	{
		return;
	}
	Order = ECmdOrder::Harvest;
	DockTarget = Refinery;
	Harvest = EHarvest::ToRefinery;
	SetPathTo(Refinery->GetDockCell());
}

bool ACmdUnit::CanDeploy() const
{
	if (!Def || Def->DeploysInto.IsNone())
	{
		return false;
	}
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdEntityDef* BuildingDef = Data ? Data->GetEntity(Def->DeploysInto) : nullptr;
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	if (!BuildingDef || !Grid)
	{
		return false;
	}
	const FIntPoint Origin = GetCell() - FIntPoint(BuildingDef->Footprint.X / 2, BuildingDef->Footprint.Y / 2);
	return Grid->CanPlaceStructure(TeamId, *BuildingDef, Origin, false, this);
}

bool ACmdUnit::TryDeploy()
{
	if (!CanDeploy())
	{
		if (ACmdGameState* GS = GetCmdGameState())
		{
			GS->AddMessage(TeamId, TEXT("Cannot deploy here."), FLinearColor(1.f, 0.4f, 0.3f));
		}
		return false;
	}
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdEntityDef* BuildingDef = Data->GetEntity(Def->DeploysInto);
	const FIntPoint Origin = GetCell() - FIntPoint(BuildingDef->Footprint.X / 2, BuildingDef->Footprint.Y / 2);
	const FName Tag = MissionTag;
	const int32 Team = TeamId;
	UWorld* World = GetWorld();

	bDead = true;	// leave quietly, the building takes over
	if (ACmdGameState* GS = GetCmdGameState())
	{
		GS->UnregisterEntity(this);
	}
	Destroy();
	CmdSpawner::SpawnBuilding(World, BuildingDef->Id, Team, Origin, Tag, true);
	return true;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void ACmdUnit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsAlive())
	{
		return;
	}

	if (IsHarvester())
	{
		TickHarvester(DeltaSeconds);
	}
	TickOrder(DeltaSeconds);
	if (!IsAlive())
	{
		return;	// e.g. an engineer that just entered a building
	}

	// Face the target when we can't swivel a turret.
	if (ACmdEntity* T = Target.Get())
	{
		if (!IsMoving() && NeedsToFaceTarget())
		{
			TurnTowards((T->GetActorLocation() - GetActorLocation()).Rotation().Yaw, DeltaSeconds);
		}
	}

	if (!IsAircraft())
	{
		ApplySeparation(DeltaSeconds);
	}
	UpdateAltitude(DeltaSeconds);

	// Little walk cycle for infantry.
	if (IsInfantry())
	{
		BobPhase += DeltaSeconds * (IsMoving() ? 14.f : 0.f);
		Body->SetRelativeLocation(FVector(0.f, 0.f, IsMoving() ? FMath::Abs(FMath::Sin(BobPhase)) * 6.f : 0.f));
	}
}

bool ACmdUnit::CanAutoAcquire() const
{
	switch (Order)
	{
	case ECmdOrder::Move:
		return bHasTurret;		// tanks shoot on the move, troops just run
	case ECmdOrder::Capture:
	case ECmdOrder::Harvest:
		return false;
	default:
		return true;
	}
}

float ACmdUnit::GetAcquireRange() const
{
	const float WeaponRange = Weapon ? Weapon->Range : 0.f;
	if (Order == ECmdOrder::AttackMove || Order == ECmdOrder::Guard)
	{
		return FMath::Max(WeaponRange, Def ? Def->Sight : WeaponRange);
	}
	return WeaponRange;
}

void ACmdUnit::TickIdleResponse()
{
	// Return fire when shot from beyond our range.
	ACmdEntity* Attacker = LastAttacker.Get();
	if (!Attacker || !Attacker->IsAlive() || !HasWeapon() || !CanTarget(Attacker) || !IsHostileTo(Attacker))
	{
		return;
	}
	if (GetWorld()->GetTimeSeconds() - LastDamagedTime > 2.f || Target.IsValid())
	{
		return;
	}
	Target = Attacker;
	bReturningToGuard = false;
}

void ACmdUnit::TickOrder(float DeltaSeconds)
{
	RepathTimer -= DeltaSeconds;
	ACmdEntity* T = Target.Get();

	switch (Order)
	{
	case ECmdOrder::Idle:
	case ECmdOrder::Guard:
	{
		TickIdleResponse();
		T = Target.Get();
		if (T && !IsInRange(T))
		{
			// Chase, but stay on a leash around the guard position.
			const float Leash = (Order == ECmdOrder::Guard ? LeashCells * 1.5f : LeashCells) * CmdConst::CellSize;
			if (FVector::Dist2D(T->GetActorLocation(), Cmd::CellToWorld(GuardCell)) > Leash)
			{
				Target = nullptr;
				bReturningToGuard = true;
				SetPathTo(GuardCell);
			}
			else if (RepathTimer <= 0.f)
			{
				RepathTimer = 0.8f;
				IsAircraft() ? SetPathToWorld(T->GetActorLocation()) : SetPathTo(T->GetCell());
			}
		}
		else if (T && IsInRange(T))
		{
			ClearPath();
		}
		else if (!T && !IsMoving() && FVector::Dist2D(GetActorLocation(), Cmd::CellToWorld(GuardCell)) > 2.5f * CmdConst::CellSize && !bReturningToGuard)
		{
			bReturningToGuard = true;
			SetPathTo(GuardCell);
		}
		if (FollowPath(DeltaSeconds))
		{
			bReturningToGuard = false;
		}
		break;
	}

	case ECmdOrder::Move:
		if (FollowPath(DeltaSeconds))
		{
			Order = ECmdOrder::Idle;
			GuardCell = GetCell();
		}
		break;

	case ECmdOrder::AttackMove:
		if (T && IsInRange(T))
		{
			// Hold position and fight; the weapon tick does the shooting.
		}
		else if (T)
		{
			if (RepathTimer <= 0.f)
			{
				RepathTimer = 0.8f;
				IsAircraft() ? SetPathToWorld(T->GetActorLocation()) : SetPathTo(T->GetCell());
			}
			FollowPath(DeltaSeconds);
		}
		else
		{
			if (!IsMoving() && FVector::Dist2D(GetActorLocation(), Cmd::CellToWorld(Goal)) > CmdConst::CellSize * 1.5f && RepathTimer <= 0.f)
			{
				RepathTimer = 1.f;
				SetPathTo(Goal);
			}
			if (FollowPath(DeltaSeconds) && FVector::Dist2D(GetActorLocation(), Cmd::CellToWorld(Goal)) <= CmdConst::CellSize * 2.f)
			{
				Order = ECmdOrder::Idle;
				GuardCell = GetCell();
			}
		}
		break;

	case ECmdOrder::Attack:
	{
		ACmdEntity* OT = OrderTarget.Get();
		if (!OT || !OT->IsAlive())
		{
			OrderStop();
			break;
		}
		if (!Target.IsValid())
		{
			SetForcedTarget(OT);
		}
		if (IsInRange(OT))
		{
			ClearPath();
		}
		else
		{
			if (RepathTimer <= 0.f || !IsMoving())
			{
				RepathTimer = 1.f;
				if (IsAircraft())
				{
					SetPathToWorld(OT->GetActorLocation());
				}
				else if (OT->IsBuilding())
				{
					SetPathTo(Cast<ACmdBuilding>(OT)->GetNearestAdjacentCell(GetCell()));
				}
				else
				{
					SetPathTo(OT->GetCell());
				}
			}
			FollowPath(DeltaSeconds);
		}
		break;
	}

	case ECmdOrder::Capture:
	{
		ACmdBuilding* B = Cast<ACmdBuilding>(OrderTarget.Get());
		if (!B || !B->IsAlive())
		{
			OrderStop();
			break;
		}
		if (DistanceTo2D(B) < 90.f)
		{
			ACmdGameState* GS = GetCmdGameState();
			if (B->TeamId == TeamId)
			{
				B->Heal(B->GetMaxHealth());
				if (GS) GS->AddMessage(TeamId, FString::Printf(TEXT("%s repaired."), *B->Def->Name));
			}
			else if (B->Def && B->Def->bCapturable)
			{
				if (GS) GS->AddMessage(TeamId, FString::Printf(TEXT("%s captured."), *B->Def->Name), FLinearColor(0.4f, 1.f, 0.4f));
				if (GS) GS->AddMessage(B->TeamId, FString::Printf(TEXT("%s has been captured!"), *B->Def->Name), FLinearColor(1.f, 0.3f, 0.2f));
				B->SetTeam(TeamId);
			}
			bDead = true;
			if (GS)
			{
				GS->UnregisterEntity(this);
			}
			Destroy();
			return;
		}
		if (!IsMoving() && RepathTimer <= 0.f)
		{
			RepathTimer = 1.f;
			SetPathTo(B->GetNearestAdjacentCell(GetCell()));
		}
		FollowPath(DeltaSeconds);
		break;
	}

	case ECmdOrder::Harvest:
	case ECmdOrder::Dock:
		FollowPath(DeltaSeconds);
		break;
	}
}

void ACmdUnit::TickHarvester(float DeltaSeconds)
{
	if (Order != ECmdOrder::Harvest)
	{
		return;
	}
	UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	ACmdGameState* GS = GetCmdGameState();
	if (!Grid || !GS || !Def)
	{
		return;
	}

	HarvestTimer -= DeltaSeconds;
	switch (Harvest)
	{
	case EHarvest::Waiting:
		if (HarvestTimer <= 0.f)
		{
			Harvest = Cargo > 0 ? EHarvest::SeekRefinery : EHarvest::SeekOre;
		}
		break;

	case EHarvest::SeekOre:
	{
		FIntPoint Found;
		const FIntPoint From = Grid->GetOre(OreCell) > 0 ? OreCell : GetCell();
		if (Grid->FindNearestOre(From, Found, 48))
		{
			OreCell = Found;
			SetPathTo(OreCell);
			Harvest = EHarvest::ToOre;
		}
		else
		{
			Harvest = Cargo > 0 ? EHarvest::SeekRefinery : EHarvest::Waiting;
			HarvestTimer = 4.f;
		}
		break;
	}

	case EHarvest::ToOre:
		if (!IsMoving())
		{
			if (FVector::Dist2D(GetActorLocation(), Cmd::CellToWorld(OreCell)) < CmdConst::CellSize * 1.2f && Grid->GetOre(OreCell) > 0)
			{
				Harvest = EHarvest::Harvesting;
				HarvestTimer = HarvestInterval;
			}
			else
			{
				Harvest = EHarvest::SeekOre;
			}
		}
		break;

	case EHarvest::Harvesting:
		if (HarvestTimer <= 0.f)
		{
			HarvestTimer = HarvestInterval;
			Cargo += Grid->HarvestOre(OreCell, 1);
			if (Cargo >= Def->HarvestCapacity)
			{
				Harvest = EHarvest::SeekRefinery;
			}
			else if (Grid->GetOre(OreCell) <= 0)
			{
				Harvest = EHarvest::SeekOre;
			}
		}
		break;

	case EHarvest::SeekRefinery:
	{
		ACmdBuilding* Refinery = DockTarget.Get();
		if (!Refinery || !Refinery->IsAlive() || Refinery->TeamId != TeamId)
		{
			Refinery = FindNearestRefinery();
		}
		if (Refinery)
		{
			DockTarget = Refinery;
			SetPathTo(Refinery->GetDockCell());
			Harvest = EHarvest::ToRefinery;
		}
		else
		{
			Harvest = EHarvest::Waiting;
			HarvestTimer = 3.f;
		}
		break;
	}

	case EHarvest::ToRefinery:
	{
		ACmdBuilding* Refinery = DockTarget.Get();
		if (!Refinery || !Refinery->IsAlive() || Refinery->TeamId != TeamId)
		{
			Harvest = EHarvest::SeekRefinery;
			break;
		}
		if (!IsMoving())
		{
			if (FVector::Dist2D(GetActorLocation(), Cmd::CellToWorld(Refinery->GetDockCell())) < CmdConst::CellSize * 1.4f)
			{
				Harvest = Cargo > 0 ? EHarvest::Unloading : EHarvest::SeekOre;
				HarvestTimer = UnloadInterval;
			}
			else
			{
				SetPathTo(Refinery->GetDockCell());
			}
		}
		break;
	}

	case EHarvest::Unloading:
		if (HarvestTimer <= 0.f)
		{
			HarvestTimer = UnloadInterval;
			if (Cargo > 0)
			{
				--Cargo;
				GS->AddCredits(TeamId, CmdConst::OreValuePerBale, true);
			}
			if (Cargo <= 0)
			{
				Harvest = EHarvest::SeekOre;
			}
		}
		break;

	default:
		break;
	}
}

ACmdBuilding* ACmdUnit::FindNearestRefinery() const
{
	const ACmdGameState* GS = GetCmdGameState();
	if (!GS)
	{
		return nullptr;
	}
	ACmdBuilding* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (ACmdEntity* E : GS->Entities)
	{
		ACmdBuilding* B = Cast<ACmdBuilding>(E);
		if (B && B->TeamId == TeamId && B->IsAlive() && B->Def && B->Def->bRefinery)
		{
			const float D = FVector::DistSquared2D(B->GetActorLocation(), GetActorLocation());
			if (D < BestDist)
			{
				BestDist = D;
				Best = B;
			}
		}
	}
	return Best;
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

void ACmdUnit::ClearPath()
{
	Path.Reset();
	PathIndex = 0;
}

void ACmdUnit::SetPathTo(const FIntPoint& Cell)
{
	if (IsAircraft())
	{
		SetPathToWorld(Cmd::CellToWorld(Cell));
		return;
	}
	ClearPath();
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	if (!Grid)
	{
		return;
	}
	TArray<FIntPoint> Cells;
	Grid->FindPath(GetCell(), Cell, Cells);
	for (const FIntPoint& C : Cells)
	{
		Path.Add(Cmd::CellToWorld(C));
	}
	StuckTimer = 0.f;
	StuckCount = 0;
	LastProgressLocation = GetActorLocation();
}

void ACmdUnit::SetPathToWorld(const FVector& Location)
{
	ClearPath();
	Path.Add(FVector(Location.X, Location.Y, 0.f));
}

float ACmdUnit::TurnTowards(float DesiredYaw, float DeltaSeconds)
{
	FRotator Rot = GetActorRotation();
	const float Delta = FRotator::NormalizeAxis(DesiredYaw - Rot.Yaw);
	const float Step = (Def ? Def->TurnRate : 360.f) * DeltaSeconds;
	Rot.Yaw = FRotator::NormalizeAxis(Rot.Yaw + FMath::Clamp(Delta, -Step, Step));
	SetActorRotation(Rot);
	return FMath::Abs(Delta) - FMath::Min(FMath::Abs(Delta), Step);
}

bool ACmdUnit::FollowPath(float DeltaSeconds)
{
	if (PathIndex >= Path.Num())
	{
		return true;
	}

	const FVector Location = GetActorLocation();
	const FVector Next = Path[PathIndex];
	FVector ToNext = Next - Location;
	ToNext.Z = 0.f;
	const float Dist = ToNext.Size();
	const bool bFinal = PathIndex == Path.Num() - 1;
	const float ArriveRadius = bFinal ? 25.f : 70.f;

	if (Dist <= ArriveRadius)
	{
		++PathIndex;
		return PathIndex >= Path.Num();
	}

	const FVector Dir = ToNext / Dist;
	const float Remaining = TurnTowards(Dir.Rotation().Yaw, DeltaSeconds);

	// Vehicles slow down to turn on the spot, infantry and aircraft just go.
	float SpeedFactor = 1.f;
	if (!IsInfantry() && !IsAircraft())
	{
		SpeedFactor = Remaining > 70.f ? 0.f : FMath::Clamp(1.f - Remaining / 90.f, 0.25f, 1.f);
	}
	const float Step = FMath::Min(Dist, GetSpeedUU() * SpeedFactor * DeltaSeconds);
	FVector NewLocation = Location + Dir * Step;

	if (!IsAircraft())
	{
		const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
		const FIntPoint NewCell = Cmd::WorldToCell(NewLocation);
		if (Grid && NewCell != GetCell() && !Grid->IsPassable(NewCell))
		{
			// Something got built in our way.
			const FIntPoint Dest = Cmd::WorldToCell(Path.Last());
			SetPathTo(Dest);
			return false;
		}

		StuckTimer += DeltaSeconds;
		if (StuckTimer > 1.5f)
		{
			const float Progress = FVector::Dist2D(Location, LastProgressLocation);
			LastProgressLocation = Location;
			StuckTimer = 0.f;
			if (Progress < 40.f)
			{
				++StuckCount;
				const FIntPoint Dest = Cmd::WorldToCell(Path.Last());
				if (StuckCount >= 3 || FVector::Dist2D(Location, Path.Last()) < CmdConst::CellSize * 1.5f)
				{
					ClearPath();	// close enough, give up gracefully
					return true;
				}
				const int32 SavedCount = StuckCount;
				SetPathTo(Dest);
				StuckCount = SavedCount;
				return false;
			}
		}
	}

	NewLocation.Z = Location.Z;
	SetActorLocation(NewLocation);
	return false;
}

void ACmdUnit::ApplySeparation(float DeltaSeconds)
{
	const ACmdGameState* GS = GetCmdGameState();
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	if (!GS || !Grid)
	{
		return;
	}
	const FVector Location = GetActorLocation();
	const float MyRadius = GetRadius();
	FVector Push = FVector::ZeroVector;
	for (const ACmdEntity* Other : GS->Entities)
	{
		if (Other == this || !Other || Other->IsBuilding() || Other->IsAircraft() || !Other->IsAlive())
		{
			continue;
		}
		const FVector Delta = Location - Other->GetActorLocation();
		const float MinDist = MyRadius + Other->GetRadius();
		const float D2 = Delta.SizeSquared2D();
		if (D2 >= MinDist * MinDist)
		{
			continue;
		}
		const float D = FMath::Sqrt(D2);
		const FVector Away = D > 1.f ? FVector(Delta.X / D, Delta.Y / D, 0.f) : FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal();
		Push += Away * (MinDist - D);
	}
	if (Push.IsNearlyZero())
	{
		return;
	}
	// Moving units push through, idle ones make room.
	const float Strength = IsMoving() ? 2.f : 5.f;
	FVector Offset = Push * FMath::Min(1.f, Strength * DeltaSeconds);
	Offset = Offset.GetClampedToMaxSize(GetSpeedUU() * DeltaSeconds + 2.f);
	const FVector NewLocation = Location + Offset;
	if (Grid->IsPassable(Cmd::WorldToCell(NewLocation)))
	{
		SetActorLocation(NewLocation);
	}
}

void ACmdUnit::UpdateAltitude(float DeltaSeconds)
{
	if (!IsAircraft())
	{
		return;
	}
	const float TargetZ = Def->FlyHeight + FMath::Sin(GetWorld()->GetTimeSeconds() * 1.7f + BobPhase) * 15.f;
	Altitude = FMath::FInterpTo(Altitude, TargetZ, DeltaSeconds, 1.5f);
	FVector L = GetActorLocation();
	L.Z = Altitude;
	SetActorLocation(L);
	// Bank a little into the direction of travel.
	const float Pitch = IsMoving() ? -8.f : 0.f;
	Body->SetRelativeRotation(FMath::RInterpTo(Body->GetRelativeRotation(), FRotator(Pitch, 0.f, 0.f), DeltaSeconds, 3.f));
}
