#include "Gameplay/CmdBuilding.h"
#include "Gameplay/CmdEffect.h"
#include "Gameplay/CmdSpawner.h"
#include "Game/CmdGameState.h"
#include "Data/CmdDataRegistry.h"
#include "World/CmdGridSubsystem.h"
#include "Engine/World.h"

ACmdBuilding::ACmdBuilding()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ACmdBuilding::SetOrigin(const FIntPoint& InOrigin, bool bAnimateConstruction)
{
	Origin = InOrigin;
	BuildProgress = bAnimateConstruction ? 0.f : 1.f;
}

void ACmdBuilding::BeginPlay()
{
	if (UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>())
	{
		if (Def)
		{
			Grid->SetBuilding(Origin, Def->Footprint, this);
			bRegisteredOnGrid = true;
		}
	}
	Super::BeginPlay();

	if (Def && Def->Footprint.Y > 0)
	{
		RallyCell = GetExitCell() + FIntPoint(0, 2);
	}
	if (BuildProgress < 1.f)
	{
		Body->SetRelativeScale3D(FVector(1.f, 1.f, 0.02f));
	}
}

void ACmdBuilding::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bRegisteredOnGrid)
	{
		if (UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>())
		{
			Grid->SetBuilding(Origin, Def ? Def->Footprint : FIntPoint(1, 1), nullptr);
		}
		bRegisteredOnGrid = false;
	}
	Super::EndPlay(Reason);
}

FIntPoint ACmdBuilding::GetCell() const
{
	return Def ? Origin + FIntPoint(Def->Footprint.X / 2, Def->Footprint.Y / 2) : Origin;
}

FVector ACmdBuilding::GetAimPoint() const
{
	return GetActorLocation() + FVector(0.f, 0.f, 60.f);
}

bool ACmdBuilding::ContainsCell(const FIntPoint& C) const
{
	return Def && C.X >= Origin.X && C.Y >= Origin.Y && C.X < Origin.X + Def->Footprint.X && C.Y < Origin.Y + Def->Footprint.Y;
}

FIntPoint ACmdBuilding::GetExitCell() const
{
	// Units roll out of the bottom-centre edge.
	return Def ? FIntPoint(Origin.X + Def->Footprint.X / 2, Origin.Y + Def->Footprint.Y) : Origin;
}

FIntPoint ACmdBuilding::GetDockCell() const
{
	return GetExitCell();
}

FIntPoint ACmdBuilding::GetNearestAdjacentCell(const FIntPoint& From) const
{
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	if (!Def || !Grid)
	{
		return Origin;
	}
	FIntPoint Best = GetExitCell();
	float BestDist = TNumericLimits<float>::Max();
	for (int32 Y = Origin.Y - 1; Y <= Origin.Y + Def->Footprint.Y; ++Y)
	{
		for (int32 X = Origin.X - 1; X <= Origin.X + Def->Footprint.X; ++X)
		{
			const FIntPoint C(X, Y);
			if (ContainsCell(C) || !Grid->IsPassable(C))
			{
				continue;
			}
			const float D = FVector2D::DistSquared(FVector2D(C.X, C.Y), FVector2D(From.X, From.Y));
			if (D < BestDist)
			{
				BestDist = D;
				Best = C;
			}
		}
	}
	return Best;
}

bool ACmdBuilding::Produces(ECmdCategory Category) const
{
	return Def && Def->Produces.Contains(Category);
}

bool ACmdBuilding::IsPowered() const
{
	if (!Def || !Def->bRequiresPower)
	{
		return true;
	}
	const ACmdGameState* GS = GetCmdGameState();
	const FCmdTeam* Team = GS ? GS->GetTeam(TeamId) : nullptr;
	return !Team || !Team->HasLowPower();
}

bool ACmdBuilding::CanFireNow() const
{
	return IsConstructed() && IsPowered();
}

void ACmdBuilding::Tick(float DeltaSeconds)
{
	if (BuildProgress < 1.f)
	{
		// Classic "build-up" animation.
		BuildProgress = FMath::Min(1.f, BuildProgress + DeltaSeconds / 1.6f);
		const float Z = FMath::InterpEaseOut(0.02f, 1.f, BuildProgress, 2.f);
		Body->SetRelativeScale3D(FVector(1.f, 1.f, Z));
		if (BuildProgress >= 1.f)
		{
			if (ACmdGameState* GS = GetCmdGameState())
			{
				GS->OnStructureCompleted(this);
			}
		}
	}

	Super::Tick(DeltaSeconds);
	if (!IsAlive())
	{
		return;
	}
	TickRepair(DeltaSeconds);
	TickDamageSmoke(DeltaSeconds);

	if (Def && Def->Income > 0)
	{
		IncomeTimer += DeltaSeconds;
		if (IncomeTimer >= 10.f)
		{
			IncomeTimer = 0.f;
			if (ACmdGameState* GS = GetCmdGameState())
			{
				if (!GS->IsNeutralTeam(TeamId))
				{
					GS->AddCredits(TeamId, Def->Income, false);
				}
			}
		}
	}
}

void ACmdBuilding::TickRepair(float DeltaSeconds)
{
	if (!bRepairing)
	{
		return;
	}
	if (Health >= GetMaxHealth())
	{
		bRepairing = false;
		return;
	}
	RepairTimer -= DeltaSeconds;
	if (RepairTimer > 0.f)
	{
		return;
	}
	RepairTimer = 0.25f;
	ACmdGameState* GS = GetCmdGameState();
	FCmdTeam* Team = GS ? GS->GetTeam(TeamId) : nullptr;
	if (!Team)
	{
		return;
	}
	// Repairs cost roughly a quarter of the building price for a full heal.
	const float HealAmount = GetMaxHealth() * 0.02f;
	const float Cost = Def ? FMath::Max(1.f, Def->Cost * 0.25f * 0.02f) : 2.f;
	if (Team->Credits < Cost)
	{
		bRepairing = false;
		GS->AddMessage(TeamId, TEXT("Insufficient funds for repair."), FLinearColor(1.f, 0.5f, 0.2f), TEXT("deny"));
		return;
	}
	Team->Credits -= Cost;
	Heal(HealAmount);
}

void ACmdBuilding::TickDamageSmoke(float DeltaSeconds)
{
	if (GetHealthPct() > 0.5f || IsHidden())
	{
		return;
	}
	SmokeTimer -= DeltaSeconds;
	if (SmokeTimer <= 0.f)
	{
		SmokeTimer = GetHealthPct() < 0.25f ? 0.35f : 0.8f;
		const FVector Off(FMath::FRandRange(-0.4f, 0.4f) * GetRadius(), FMath::FRandRange(-0.4f, 0.4f) * GetRadius(), GetBarHeight() * 0.6f);
		ACmdEffect::SpawnSmoke(GetWorld(), GetActorLocation() + Off, 0.8f);
		if (GetHealthPct() < 0.25f && FMath::FRand() < 0.3f)
		{
			ACmdEffect::SpawnFlash(GetWorld(), GetActorLocation() + Off, FLinearColor(1.f, 0.4f, 0.1f), 0.5f);
		}
	}
}

void ACmdBuilding::ToggleRepair()
{
	bRepairing = !bRepairing && Health < GetMaxHealth();
}

void ACmdBuilding::Sell()
{
	if (!IsAlive() || !Def)
	{
		return;
	}
	ACmdGameState* GS = GetCmdGameState();
	if (GS)
	{
		const float Refund = Def->Cost * 0.5f * GetHealthPct();
		GS->AddCredits(TeamId, Refund, false);
		GS->AddMessage(TeamId, FString::Printf(TEXT("%s sold for $%d."), *Def->Name, FMath::RoundToInt(Refund)), FLinearColor(0.9f, 0.9f, 0.6f), TEXT("credits"));
	}
	// A couple of crew members walk out of sold structures.
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	if (Data && Data->GetEntity(TEXT("rifle")))
	{
		const int32 Crew = FMath::Clamp(Def->Footprint.X * Def->Footprint.Y / 3, 1, 3);
		for (int32 i = 0; i < Crew; ++i)
		{
			CmdSpawner::SpawnAtCell(GetWorld(), TEXT("rifle"), TeamId, GetExitCell(), NAME_None, 90.f);
		}
	}
	bDead = true;
	if (GS)
	{
		GS->OnEntitySold(this);
	}
	Destroy();
}

void ACmdBuilding::SetTeam(int32 NewTeam)
{
	Super::SetTeam(NewTeam);
	bRepairing = false;
}

void ACmdBuilding::Die(ACmdEntity* Killer)
{
	if (bDead)
	{
		return;
	}
	// A string of secondary explosions across the footprint.
	if (Def)
	{
		for (int32 i = 0; i < Def->Footprint.X * Def->Footprint.Y; ++i)
		{
			const FVector Off(FMath::FRandRange(-0.5f, 0.5f) * Def->Footprint.X * CmdConst::CellSize, FMath::FRandRange(-0.5f, 0.5f) * Def->Footprint.Y * CmdConst::CellSize, 50.f);
			ACmdEffect::SpawnExplosion(GetWorld(), GetActorLocation() + Off, FMath::FRandRange(0.8f, 1.6f), true);
		}
	}
	Super::Die(Killer);
}
