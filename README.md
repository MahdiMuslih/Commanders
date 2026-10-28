# COMMANDERS: The Iron Winter

A classic base-building real-time strategy game in the spirit of the 1990s *Command & Conquer: Red Alert* series. It's built in **Unreal Engine 5 (C++)** and has a full two-sided story campaign with in-engine cinematics.

> Winter, 1958. The Iron Pact crosses the Varn River without warning, and unnatural storms follow its armies.
> Fight for the **Western Concord** to stop the "Iron Winter", or march with the **Iron Pact** and find out what your Premier is truly willing to sacrifice.

The whole game is **code- and data-driven**. Every unit, building, explosion, sound effect, piece of music, menu, map, mission, line of dialogue and cutscene comes from C++ and plain JSON. The repository contains **no binary assets** (`.uasset`/`.umap`), so it opens and plays on a clean engine install. Units are assembled from engine primitives in a "toy soldier" style. You can swap in real art later without touching code (see [Replacing the art](#replacing-the-art)).

---

## Features

| Area | What's in |
|---|---|
| **Factions** | Western Concord (mobile, precise, air power) and Iron Pact (heavy armour, rockets, tesla-style Arc Towers), each with unique units and defences. |
| **Base building** | Construction Yard, Power Plants, Ore Refineries, Barracks, War Factory, Radar Dome, Helipad, Tech Center, faction defences. Footprint placement with the classic "must build next to your base" rule and a build-up animation. |
| **Economy** | Ore and gem fields that slowly regrow, Harvesters that mine and unload automatically, capturable Oil Derricks, selling and repairing. |
| **Power** | Every structure draws power. On low power, production slows, the radar goes dark and powered defences (Arc Towers, flak) shut down. |
| **Sidebar** | A right-hand sidebar like Red Alert's: credits, power meter, radar minimap, four production tabs, progress overlays, queueing (Shift = ×5), hold/cancel, READY-to-place, and tooltips with stats and missing prerequisites. |
| **Combat** | Armour classes × warhead table, turrets that track targets, projectiles with homing or ballistic arcs, splash damage, instant energy beams, anti-air rules, and procedural explosions, smoke and debris. |
| **Units** | 30+ unit and structure types: riflemen, rocket troops, engineers, snipers, flame and shock troopers, jeeps, light, battle and siege tanks, artillery, rocket trucks, flak, harvesters, MCVs and attack helicopters. |
| **Control** | Box and double-click selection, attack-move, force-fire, stop, guard, deploy, control groups, rally points, primary factory, minimap commands, edge scrolling, zoom and rotate. |
| **Pathfinding** | A* on the tactical grid with string-pulling, formation spreading, local unit separation, stuck detection and re-pathing around new buildings. |
| **Shroud** | Per-alliance exploration shroud, as in Red Alert. It hides enemies and renders as black terrain blocks. |
| **AI** | Skirmish commander with a build order, power and harvester management, faction defences, unit mix scaled by difficulty, escalating attack waves, base defence and repair. Missions can script it. |
| **Campaign** | 12 story missions (6 per side) with briefings, objectives, triggers, scripted waves, reinforcements, radio dialogue, timers, and intro/outro cinematics. Progress is saved. |
| **Cinematics** | Real-time cutscenes defined in JSON: dolly and orbit camera shots, letterbox, fades, title cards, subtitles, camera shake, and timeline events that drive live gameplay (spawns, orders, explosions, lighting changes). |
| **Skirmish** | Procedural maps: 1–3 AI opponents, 3 difficulties, 3 map sizes, 4 theatres (temperate, winter, desert, dusk) and a choice of starting credits. |
| **Audio** | About 40 synthesised effects (gunfire, cannons, rockets, flame, arc bolts, explosions, collapses, UI and announcer chimes) with camera-relative panning. A live procedural soundtrack follows the game: a brooding menu theme, briefing tension, and an industrial battle march whose drums and lead grow with nearby combat, plus victory and defeat stings. Volume controls are in the menus; `M` mutes the music. Any sound or music mood can be swapped for real assets via `Audio.json`. |
| **Front end** | Main menu over a live AI-vs-AI battle, campaign and mission select, skirmish setup, how-to-play, briefing screen with a recon map, pause menu, results with stats, and campaign epilogues. |

---

## Getting started

### Requirements
* **Unreal Engine 5.4** (5.3–5.5 should work; change `EngineAssociation` in `Commanders.uproject` if needed).
* Visual Studio 2022 (Windows) or Xcode / Rider (macOS/Linux) with the UE C++ toolchain.
* Python 3 (optional, only for the data validator).

### Build & run
1. Right-click `Commanders.uproject` and choose **Generate Visual Studio project files**.
2. Open `Commanders.sln`, set the configuration to **Development Editor**, and build.
3. Launch the editor and press **Play** (Standalone or New Editor Window gives the best mouse handling).

No level setup is needed. `GameDefaultMap` points at the engine's empty `/Engine/Maps/Entry` level, and `ACmdGameMode` generates the terrain, lighting, sky and every actor at runtime. To use your own level (for example to add art or a custom skybox), create an empty level, save it, and set it as the default map in *Project Settings → Maps & Modes*. The game mode reuses whatever level is loaded. If your level contains a directional light, the procedural sky and sun are skipped.

### Packaging
`Config/DefaultGame.ini` already stages `Content/Data` as UFS, so the JSON files ship inside the pak. Package as usual (*Platforms → Windows → Package Project*).

---

## How to play

| Input | Action |
|---|---|
| Left click / drag | Select / box-select (double click = all of that type on screen) |
| Right click | Move, attack, capture (Engineer), harvest (Harvester on ore), dock (Harvester on a Refinery) |
| Ctrl + right click | Force-fire on anything |
| `A`, then click | Attack-move |
| `S` / `G` / `D` | Stop / Guard / Deploy MCV |
| `Ctrl+1..9`, `1..9` | Assign / recall a control group (press twice to centre the camera) |
| `Ctrl+A` | Select all combat units |
| Arrows, screen edge, middle-drag | Scroll |
| Mouse wheel, `Q`/`E` | Zoom, rotate |
| `H` | Jump to your base |
| `X` / `R` | Sell / Repair mode |
| `O` / `F1` / `Esc` | Objectives / Help overlay / Pause |
| `M` | Mute / unmute music |
| Sidebar | Left = build / place when READY, right = hold, then cancel. Shift-click queues 5. |
| Production building + right click on ground | Set rally point (right-click the building itself to make it primary) |
| `Space` / `Esc` during cutscenes | Skip |

Classic flow: **deploy the MCV → Power Plant → Ore Refinery → Barracks → War Factory → Radar → expand.** Watch your power meter.

---

## Project layout

```
Commanders.uproject
Config/                     Engine/game/input ini (default map, game mode, packaging)
Content/Data/               ALL game content as JSON
  Factions.json             factions, AI defence lists, skirmish starter units
  Weapons.json              weapons (damage, range, warhead, projectile behaviour)
  Entities.json             units & structures: stats, tech tree, procedural 3D models
  Campaigns.json            the two campaigns, synopsis/epilogue, mission order
  Cinematics.json           shared cinematics
  Audio.json                mix levels and optional real-audio overrides
  Missions/*.json           12 story missions (map, teams, entities, objectives, triggers, cinematics)
Source/Commanders/
  Core/       CmdTypes (enums, damage table, grid<->world), CmdJson (JSON helpers)
  Data/       UCmdDataRegistry - loads every definition at startup
  World/      Grid (A*, ore, shroud), terrain & environment actors, procedural
              model builder, skirmish/menu map generator
  Gameplay/   ACmdEntity (health/weapons/targeting), ACmdUnit (movement, orders,
              harvesting, capture, deploy, aircraft), ACmdBuilding, projectiles,
              effects, spawner
  Game/       Game mode (session builder), game state (teams, power, production,
              events), game instance (progress, travel), save game
  Mission/    Mission director (objectives/triggers/actions/dialogue/timers),
              cinematic director
  AI/         UCmdAICommander
  Player/     RTS camera pawn, player controller (selection, commands, placement)
  Audio/      procedural sound bank, live music engine, software mixer
              (USynthComponent), audio subsystem and per-level audio actor
  UI/         ACmdHUD - menus, briefing, sidebar, minimap, overlays, results
Docs/
  Story.md        world, characters and the full campaign storyline
  DesignDoc.md    factions, units, economy and balance reference
  Scripting.md    mission/cinematic JSON reference for designers
  Audio.md        how the procedural audio works, sound ids, overrides
Tools/validate_data.py      validates all data without launching Unreal
```

### Architecture in one paragraph
`UCmdGameInstance` remembers *what* to play (menu, mission id or skirmish settings) and reloads the level to start it. On `StartPlay`, `ACmdGameMode` turns a **scenario** into a world: either a mission JSON, or one produced by `CmdMapGenerator` in the same format. It initialises `UCmdGridSubsystem`, spawns the terrain and environment, registers teams in `ACmdGameState`, spawns entities through `CmdSpawner`, creates an `UCmdAICommander` per AI team, and hands control to `ACmdMissionDirector`. The director runs the session flow (*Briefing → intro cinematic → play → outro cinematic → results*), evaluates objectives and triggers every 0.25 s, and executes scripted actions. `ACmdGameState` owns production, power, credits, shroud updates and defeat detection. Entities own their own combat and movement. `ACmdPlayerController` polls input directly, so no input assets are required. `ACmdHUD` draws the entire UI on the canvas.

---

## Modding & content

* **New unit:** add an entry to `Entities.json` (stats + `parts` model) and, if it fires, a weapon to `Weapons.json`. It appears in the sidebar as soon as its `prereqs` are met.
* **New mission:** copy a file from `Content/Data/Missions`, edit it, and list it in `Campaigns.json`. See **[Docs/Scripting.md](Docs/Scripting.md)** for every objective, trigger condition, action and cinematic field.
* **Validate:** `python3 Tools/validate_data.py` checks references, map bounds, overlapping structures, tags, objectives and script types.

### Replacing the art
Every model part has a `shape`, which can be a basic primitive or any static mesh path such as `"/Game/Art/Tanks/SM_Bear.SM_Bear"`. Replace a unit's `parts` with a single real mesh (marking the turret mesh `"turret": true`) and the procedural model is gone. Tinting uses the `Color` parameter of `/Engine/BasicShapes/BasicShapeMaterial`. Give your own material a `Color` vector parameter for team colours, or change `UCmdVisualSubsystem::GetColorMaterial`.

Real audio works the same way. `Content/Data/Audio.json` maps any sound id or music mood to an imported asset, which replaces the synthesised version. See **[Docs/Audio.md](Docs/Audio.md)**. Voice-over for the radio dialogue is not included.

---

## Status & honest limitations
* This repository was written without access to an Unreal Editor or compiler in the authoring environment. The code targets UE 5.4 APIs and was carefully cross-checked, but expect to fix a few compile errors on your first build.
* Single player only. The simulation is not replicated.
* Visuals are deliberately procedural placeholders. They are readable and team-coloured, but they are not final art.
