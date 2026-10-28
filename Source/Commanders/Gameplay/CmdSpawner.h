#pragma once

#include "CoreMinimal.h"

class UWorld;
class ACmdEntity;
class ACmdUnit;
class ACmdBuilding;

/** Central place for creating entities so every spawn path initialises them the same way. */
namespace CmdSpawner
{
	COMMANDERS_API ACmdUnit* SpawnUnit(UWorld* World, FName DefId, int32 TeamId, const FVector& Location, float Yaw, FName Tag);
	COMMANDERS_API ACmdBuilding* SpawnBuilding(UWorld* World, FName DefId, int32 TeamId, const FIntPoint& Origin, FName Tag, bool bAnimate);

	/** Spawns a unit or a structure at a cell (structures use the cell as their top-left origin). */
	COMMANDERS_API ACmdEntity* SpawnAtCell(UWorld* World, FName DefId, int32 TeamId, const FIntPoint& Cell, FName Tag, float Yaw = 90.f);
}
