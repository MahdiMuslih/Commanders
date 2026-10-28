#include "Gameplay/CmdSpawner.h"
#include "Gameplay/CmdUnit.h"
#include "Gameplay/CmdBuilding.h"
#include "Data/CmdDataRegistry.h"
#include "World/CmdGridSubsystem.h"
#include "Commanders.h"
#include "Engine/World.h"

namespace CmdSpawner
{
	ACmdUnit* SpawnUnit(UWorld* World, FName DefId, int32 TeamId, const FVector& Location, float Yaw, FName Tag)
	{
		const UCmdDataRegistry* Data = UCmdDataRegistry::Get(World);
		const FCmdEntityDef* Def = Data ? Data->GetEntity(DefId) : nullptr;
		if (!World || !Def || Def->bIsBuilding)
		{
			UE_LOG(LogCommanders, Warning, TEXT("SpawnUnit: unknown unit '%s'"), *DefId.ToString());
			return nullptr;
		}
		FVector SpawnLocation = Location;
		SpawnLocation.Z = Def->bAircraft ? FMath::Max(Location.Z, 0.f) : 0.f;
		const FTransform Transform(FRotator(0.f, Yaw, 0.f), SpawnLocation);
		ACmdUnit* Unit = World->SpawnActorDeferred<ACmdUnit>(ACmdUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Unit)
		{
			return nullptr;
		}
		Unit->Setup(DefId, TeamId);
		Unit->MissionTag = Tag;
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	ACmdBuilding* SpawnBuilding(UWorld* World, FName DefId, int32 TeamId, const FIntPoint& Origin, FName Tag, bool bAnimate)
	{
		const UCmdDataRegistry* Data = UCmdDataRegistry::Get(World);
		const FCmdEntityDef* Def = Data ? Data->GetEntity(DefId) : nullptr;
		if (!World || !Def || !Def->bIsBuilding)
		{
			UE_LOG(LogCommanders, Warning, TEXT("SpawnBuilding: unknown structure '%s'"), *DefId.ToString());
			return nullptr;
		}
		const FVector Center((Origin.X + Def->Footprint.X * 0.5f) * CmdConst::CellSize, (Origin.Y + Def->Footprint.Y * 0.5f) * CmdConst::CellSize, 0.f);
		const FTransform Transform(FRotator::ZeroRotator, Center);
		ACmdBuilding* Building = World->SpawnActorDeferred<ACmdBuilding>(ACmdBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Building)
		{
			return nullptr;
		}
		Building->Setup(DefId, TeamId);
		Building->MissionTag = Tag;
		Building->SetOrigin(Origin, bAnimate);
		Building->FinishSpawning(Transform);
		return Building;
	}

	ACmdEntity* SpawnAtCell(UWorld* World, FName DefId, int32 TeamId, const FIntPoint& Cell, FName Tag, float Yaw)
	{
		const UCmdDataRegistry* Data = UCmdDataRegistry::Get(World);
		const FCmdEntityDef* Def = Data ? Data->GetEntity(DefId) : nullptr;
		if (!Def)
		{
			UE_LOG(LogCommanders, Warning, TEXT("SpawnAtCell: unknown entity '%s'"), *DefId.ToString());
			return nullptr;
		}
		if (Def->bIsBuilding)
		{
			return SpawnBuilding(World, DefId, TeamId, Cell, Tag, false);
		}
		FIntPoint SpawnCell = Cell;
		if (!Def->bAircraft)
		{
			if (const UCmdGridSubsystem* Grid = World->GetSubsystem<UCmdGridSubsystem>())
			{
				SpawnCell = Grid->FindNearestPassable(Cell, 8);
			}
		}
		FVector Location = Cmd::CellToWorld(SpawnCell);
		if (Def->bAircraft)
		{
			Location.Z = Def->FlyHeight;
		}
		return SpawnUnit(World, DefId, TeamId, Location, Yaw, Tag);
	}
}
