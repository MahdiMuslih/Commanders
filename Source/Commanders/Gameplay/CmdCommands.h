#pragma once

#include "CoreMinimal.h"
#include "Game/CmdNetTypes.h"

class UWorld;

/**
 * The single authoritative entry point for player orders. The local player in single
 * player, the host and every remote client all end up here (clients via a server RPC),
 * so the rules are identical and the server can reject orders for units a player
 * does not own.
 */
namespace CmdCommands
{
	/** Runs a command for a team. Returns false if nothing could be done. */
	bool Execute(UWorld* World, int32 TeamId, const FCmdCommand& Command);
}
