# Mission & Cinematic Scripting Reference

Missions live in `Content/Data/Missions/*.json` and are listed in `Content/Data/Campaigns.json`.
Skirmish maps are generated into the same format (`World/CmdMapGenerator.cpp`), so anything below also works for hand-made skirmish scenarios.

**Coordinates are grid cells**: `[x, y]` with x → right and y → down, and (0,0) at the top-left of the map. Areas are `[x, y, width, height]`.
Run `python3 Tools/validate_data.py` after editing.

## Top-level fields

```jsonc
{
  "id": "C01", "title": "Ashfall", "subtitle": "...", "location": "...", "date": "...",
  "briefing": ["paragraph", "paragraph"],
  "victory_text": "...", "defeat_text": "...",
  "map": { ... }, "teams": [ ... ], "entities": [ ... ],
  "camera": [x, y],                       // initial camera focus
  "shroud": true,                          // false = map fully revealed
  "objectives": [ ... ], "triggers": [ ... ],
  "on_start": [ actions ],                 // run when the player takes control
  "intro_cinematic": "intro",              // played after the briefing
  "outro_cinematic": "outro",              // played on victory (before the results screen)
  "defeat_cinematic": "...",               // optional, played on defeat
  "manual_victory": false,                 // true = only a "victory" action ends the mission
  "cinematics": { "intro": { ... }, "outro": { ... } }
}
```

## Map

```jsonc
"map": {
  "width": 64, "height": 56,
  "theme": "temperate | winter | desert | dusk",
  "seed": 7,
  "water": [[x,y,w,h], ...], "rocks": [...], "trees": [...],
  "roads": [...],              // painted last: a road across water is a bridge
  "random_trees": 0.02, "random_rocks": 0.004,
  "clear": [[x,y,w,h]],        // keep random decoration out of these areas
  "border": false,             // ring the map with impassable rock
  "ore": [ { "at": [x,y], "radius": 3.5, "richness": 9, "gems": false } ]
}
```
Terrain is never painted underneath the starting entities.

## Teams

```jsonc
{ "name": "Concord", "faction": "concord|pact|neutral",
  "player": true,                 // exactly one team
  "ai": true, "difficulty": "easy|normal|hard",
  "ai_config": { "build": true, "produce": true, "attack": true, "defend": true,
                 "first_attack": 300, "attack_interval": 180, "attack_size": 6,
                 "income_bonus": 1.5, "attack_target": [x,y] },
  "neutral": true,                // never auto-targeted (civilians)
  "alliance": 0,                  // same alliance = allies, shared shroud
  "credits": 5000, "color": "blue" | [r,g,b],
  "can_build": true,              // false = no structure tab
  "allowed": ["power", ...],      // whitelist tech (everything else locked)
  "locked": ["mcv"] }             // blacklist tech
```
Team index = its position in the array.

## Entities

```jsonc
{ "type": "warden", "team": 0, "at": [x,y], "tag": "Hero", "yaw": 270,
  "health": 0.5, "invulnerable": false }
```
For structures, `at` is the top-left cell of the footprint. `yaw` is in degrees (0 = +x/right, 90 = down, 270 = up).

## Objectives

| type | fields | completes when |
|---|---|---|
| `destroy_all` | – | no hostile, non-neutral team has entities left |
| `eliminate` | `team` | that team has no entities |
| `destroy` | `tag` | every entity with the tag is gone |
| `protect` | `tag` | never completes. It **fails** if the tag dies or is captured by an enemy. |
| `capture` | `tag` | a tagged entity belongs to the player's alliance (fails if destroyed) |
| `reach` | `area`, `team`, `tag`?, `min`? | enough matching units are inside the area |
| `survive` | `seconds` | that long after the objective becomes visible |
| `build` | `entity`, `count` | the player owns that many |
| `custom` | – | only via an `objective_complete` action |

Common fields: `id`, `text`, `primary` (default true), `hidden` (show later with `objective_show`), `time_limit` (seconds, shows a countdown and fails on expiry), `timer_label`, `fail_text`.
**Victory** comes when every visible primary objective (other than `protect`) is complete. **Defeat** comes when any primary objective fails, or the player has nothing left.

## Triggers

```jsonc
{ "id": "wave1", "when": { condition }, "actions": [ ... ],
  "repeat": false, "interval": 60, "delay": 0, "enabled": true }
```

### Conditions
| type | fields |
|---|---|
| `time` | `seconds` since the player took control |
| `area` | `area`, `team` ("player" or index), `tag`?, `min`?, `allied` (default true) |
| `destroyed` | `tag` |
| `captured` | `tag`, `team` |
| `damaged` | `tag`, `below` (health fraction) |
| `objective` | `id`, `state`: "completed" or "failed" |
| `count_below` | `team`, `count`, `what`: "all", "units" or "buildings" |
| `built` | `team`, `entity`, `count` |
| `produced` | `entity`, `count` (player production) |
| `timer` | `name` (expired) |
| `flag` | `name` |
| `team_defeated` | `team` |
| `credits` | `team`, `min` |
| `all` / `any` | `conditions: [...]` |
| `not` | `condition: {...}` |
| `start` | always true |

### Actions
| type | fields |
|---|---|
| `dialogue` | `lines: [{speaker, text, color?, duration?}]` (radio box with portrait and typewriter text) |
| `message` | `text`, `color?` |
| `spawn` | `team`, `units: [...]`, `at`, `tag?`, `yaw?`, `notify?` + an order (`move_to`, `attack_move`, `attack_tag`, `guard`) |
| `order` | `tag` or `team` + an order (`move_to`, `attack_move`, `attack_tag`, `guard`, `stop`) |
| `reveal` | `team`, `at`, `radius` or `all: true` |
| `camera` | `at` |
| `cinematic` | `id` |
| `objective_add` / `objective_show` / `objective_complete` / `objective_fail` | `objective` / `id` |
| `timer` / `timer_stop` | `name`, `seconds`, `label`, `visible` |
| `flag` | `name` |
| `trigger_enable` / `trigger_disable` | `id` |
| `credits` | `team`, `amount` |
| `victory` / `defeat` | `reason` |
| `transfer` | `tag`, `to` (team) |
| `destroy` | `tag` or `team` |
| `damage` | `tag`, `health` (set to a fraction) |
| `invulnerable` | `tag`, `value` |
| `explosion` | `at`, `size` (visual only) |
| `ai` | `team` + any `ai_config` field + `enabled` / `difficulty` |
| `ai_attack` | `team`, `target` (send all idle army units) |
| `build_enable` | `team`, `value` |
| `unlock` | `team`, `entities: [...]` |
| `sun` | `pitch`, `yaw` (lighting mood) |
| `delay` | `seconds`, `actions: [...]` |

## Cinematics

```jsonc
"intro": {
  "letterbox": true, "fade_in": 2.0, "fade_out": 1.0,
  "reveal": true,                         // lift the shroud while it plays
  "shots": [
    { "duration": 7,
      "from": [x, y, heightUU], "to": [x, y, heightUU],     // dolly
      "look": [x, y, zUU], "look_to": [x, y, zUU],
      "fov": 45, "fov_to": 50, "ease": "smooth|linear", "shake": 0.5,
      "title": "CHAPTER ONE", "title_sub": "Ashfall",       // title card
      "speaker": "Gen. Hale", "text": "subtitle line" },
    { "duration": 8, "orbit": { "center": [x, y], "radius": 6, "height": 650, "from": 200, "to": 250 } }
  ],
  "subtitles": [ { "t": 3.0, "speaker": "...", "text": "...", "duration": 4 } ],
  "titles":    [ { "t": 0.5, "text": "...", "sub": "...", "duration": 4 } ],
  "events":    [ { "t": 15.5, "action": { any mission action } } ]
}
```
Cinematics play in real time on the live battlefield, so events can move real units, spawn reinforcements or blow things up. Skipping a cinematic (Space/Esc) still runs all of its remaining events, so the game state stays consistent.
