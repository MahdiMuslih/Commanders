#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UCmdDataRegistry;
struct FCmdSkirmishSettings;
struct FCmdMatchConfig;

/**
 * Procedural scenario generation. Produces the same JSON "scenario" structure that
 * hand-authored campaign missions use, so the game mode has a single setup path.
 */
namespace CmdMapGenerator
{
	TSharedPtr<FJsonObject> GenerateSkirmish(const FCmdSkirmishSettings& Settings, const UCmdDataRegistry* Data);

	/** Single-player skirmish settings expressed as a match config (seat 0 = the player). */
	FCmdMatchConfig MakeSkirmishConfig(const FCmdSkirmishSettings& Settings);

	/**
	 * Deterministic battlefield for a match config: every machine that runs this with the
	 * same config gets the same terrain, ore and start positions. LocalSeat marks which
	 * team is "player" on this machine (INDEX_NONE on a dedicated view).
	 */
	TSharedPtr<FJsonObject> GenerateMatch(const FCmdMatchConfig& Config, const UCmdDataRegistry* Data, int32 LocalSeat);

	/** AI vs AI battle that plays behind the main menu. */
	TSharedPtr<FJsonObject> GenerateMenuBattle(int32 Seed);
}
