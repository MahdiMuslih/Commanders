# Multiplayer

COMMANDERS supports networked skirmish for up to **4 players plus AI opponents**, over a LAN or a direct IP connection. One player hosts (a *listen server*) and everyone else joins with the host's address. The story campaign stays single player.

## Playing

1. **Main menu → Multiplayer.** Click the name field and type your commander name.
2. **Host:** press **HOST**. You land in the lobby. Tell your friends your IP address. On a LAN that's your local address (for example `192.168.1.20`). Over the internet, the host must forward **UDP port 7777**.
3. **Join:** type the host's address into the join field (`192.168.1.20`, or `host:port` if the host uses a different port) and press **JOIN**.
4. **Lobby.** Each player picks a **faction** and a **team** (1–4). Players on the same team are allies; by default everyone starts on a different team (free-for-all). Clients press **READY**. The host chooses the AI opponents, AI difficulty, whether the AI fights as one allied team, map size, starting credits and theatre, then presses **START MATCH** when everyone is ready.
5. **In the match**, press `Enter` to chat with everyone and `Esc` for the menu. The menu doesn't pause a networked game.
6. **Winning:** the match ends when only one team has forces left. Each player gets their own victory or defeat screen. A player who is wiped out early becomes a **spectator**: the shroud lifts, and they can keep watching or leave.
7. **After the match**, the host can press **RETURN TO LOBBY** to bring everyone back for a rematch.

If a player disconnects mid-match, an AI commander takes over their side. If the host leaves, the match ends for everyone and clients return to the multiplayer menu with a message.

## How it works

The game uses **server-authoritative replication**. The host's machine runs the whole simulation: movement, pathfinding, combat, production, economy, AI and win conditions. Clients send orders and draw what the server tells them.

| Piece | File | Role |
|---|---|---|
| Lobby & match start | `Game/CmdGameMode.cpp` | `StartLobby`, `PreLogin` (rejects joins when the game is full or already running), `PostLogin`, `Logout` (AI takeover), `StartMatchFromLobby`, `ReturnToLobby` |
| Match description | `Game/CmdNetTypes.h` | `FCmdMatchConfig`: seed, map size, theatre, credits and one `FCmdMatchSlot` per side |
| Deterministic battlefield | `World/CmdMapGenerator.cpp`, `World/CmdWorldBuilder.cpp` | The server replicates only the match config. Every machine runs `GenerateMatch` + `BuildEnvironment` on it and gets identical terrain, ore and start positions, so no map data travels over the network. Single-player skirmish uses the same path. |
| Player seats | `Game/CmdPlayerState.cpp` | Replicated faction, team, ready flag, seat (`SlotTeam`) and per-player `MatchResult` |
| Match state | `Game/CmdGameState.cpp` | Replicates teams (credits, production queues, stats), flow, match time, lobby settings and match config. It routes announcer messages to the players who own a team, and broadcasts cosmetic events. |
| Entities | `Gameplay/CmdEntity.cpp`, `CmdUnit.cpp`, `CmdBuilding.cpp` | Replicated identity, team, health, orders and construction progress. Position and turret angle are published at 10 Hz and smoothly interpolated on clients. Every entity is always relevant: the shroud, not distance, decides what you see. |
| Orders | `Gameplay/CmdCommands.cpp` | Every player action (move, attack, harvest, deploy, build, place, sell, repair, rally…) becomes an `FCmdCommand`. A client sends it with `ServerCommand`. The server runs it with `CmdCommands::Execute` and rejects orders for units the player doesn't own. Single player goes through the same function. |
| Effects & sound | `ACmdGameState::MulticastShot/Effect/Sound/Ore` | One unreliable multicast per volley carries the sound, muzzle flash, beams and, on clients, harmless look-alike projectiles that burst on impact. Explosions and death sounds are multicast, and ore changes are batched every 0.4 s. `ACmdEffect::Spawn*` broadcast automatically on a server; `Spawn*Local` are for this machine only (UI feedback). |
| Connection handling | `Game/CmdGameInstance.cpp` | `HostGame` (opens the level with `?listen`), `JoinGame` (`ClientTravel`), `LeaveGame`, and network/travel failure messages |

Bandwidth and timeout settings are in `Config/DefaultEngine.ini`.

## Testing in the editor

*Play → Advanced Settings → Multiplayer Options*: set **Number of Players** to 2 and **Net Mode** to *Play Standalone*. Each window then opens the main menu on its own. Host in one window and join `127.0.0.1` from the other.

## Limitations

* No online matchmaking or server browser. Players connect by IP. An Online Subsystem (Steam, EOS) can be added for sessions and NAT traversal.
* No dedicated-server build target. The host plays on the server.
* Players can't join a match that has already started.
* Campaign co-op isn't supported. Missions are scripted around a single player.
