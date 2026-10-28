#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UWorld;
class ACmdTerrainActor;
class ACmdEnvironmentActor;

/** What BuildEnvironment produced on this machine. */
struct FCmdBuiltWorld
{
	ACmdTerrainActor* Terrain = nullptr;
	ACmdEnvironmentActor* Environment = nullptr;	// null when the level brings its own sun
	FVector CameraStart = FVector::ZeroVector;
};

/**
 * Turns the static part of a scenario (grid, terrain, ore, sky) into a world.
 * The server and every client run this on the same scenario, so nothing in it
 * has to be replicated. Entities are spawned separately, by the server only.
 */
namespace CmdWorldBuilder
{
	/** Initialises the grid and paints terrain and ore from the scenario's "map" block. */
	void BuildGrid(UWorld* World, const TSharedPtr<FJsonObject>& Scenario);

	/** First cell an entity of the given team starts on, or (-1, -1). */
	FIntPoint FindTeamStart(const TSharedPtr<FJsonObject>& Scenario, int32 TeamId);

	/** Grid + shroud viewer + terrain mesh + lighting + initial camera position. */
	FCmdBuiltWorld BuildEnvironment(UWorld* World, const TSharedPtr<FJsonObject>& Scenario, bool bShroud, int32 ViewerAlliance, int32 LocalTeam);
}
