#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UCmdDataRegistry;
struct FCmdSkirmishSettings;

/**
 * Procedural scenario generation. Produces the same JSON "scenario" structure that
 * hand-authored campaign missions use, so the game mode has a single setup path.
 */
namespace CmdMapGenerator
{
	TSharedPtr<FJsonObject> GenerateSkirmish(const FCmdSkirmishSettings& Settings, const UCmdDataRegistry* Data);

	/** AI vs AI battle that plays behind the main menu. */
	TSharedPtr<FJsonObject> GenerateMenuBattle(int32 Seed);
}
