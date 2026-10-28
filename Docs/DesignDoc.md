# COMMANDERS — Game Design Reference

All numbers live in `Content/Data/*.json`. This page summarises them and explains the rules the code applies.

## Core rules

* **Grid.** 1 cell = 200 Unreal units (about 2 m of "toy" scale). Structures occupy whole cells. Map coordinates read like an image: **x → right, y → down**, with (0,0) at the top-left.
* **Damage.** `final = weapon.damage × multiplier[warhead][armor]`. Splash weapons deal 60% damage to everything hostile in the radius, falling off to 40% at the edge.

| Warhead \ Armor | None (infantry) | Light | Heavy | Structure | Aircraft |
|---|---|---|---|---|---|
| Small arms | 1.00 | 0.55 | 0.20 | 0.25 | 0.45 |
| Armor-piercing | 0.30 | 0.85 | 1.00 | 0.70 | 0.80 |
| High explosive | 0.90 | 0.75 | 0.60 | 1.00 | 0.25 |
| Fire | 1.40 | 0.60 | 0.30 | 0.85 | 0 |
| Electric | 1.25 | 1.00 | 1.00 | 0.80 | 0.60 |
| Flak | 0.50 | 0.30 | 0.10 | 0.10 | 1.25 |

* **Anti-air.** A weapon must have `"air": true` to hit aircraft. Rifles and cannons cannot.
* **Power.** Each structure's `power` value is added up. If consumption is higher than production, production speed scales by `produced / consumed` (minimum 25%), the radar switches off, and `requires_power` defences stop firing. Damaged power plants produce less.
* **Production.** There is one queue per sidebar tab per team. Cost is paid gradually while building, and production stalls without credits. Each extra factory of the same type adds +40% speed. Structures wait in a READY state until placed. Placement must be within 3 cells of an existing friendly structure.
* **Economy.** One ore bale is worth $25 and one gem bale is worth $50. A harvester holds 28 bales ($700 of ore). Ore regrows slowly. Oil Derricks pay $150 every 10 seconds to whoever owns them.
* **Shroud.** Each alliance has its own exploration bit per cell. Enemies standing in unexplored cells are hidden, as in Red Alert, which had no re-fogging.
* **Selling** refunds 50% × health and releases 1–3 riflemen. **Repairing** costs about 25% of the structure's price for a full heal.
* **Engineers** capture enemy or neutral structures with `capturable` set, or fully repair friendly ones.

## Structures (shared)

| Structure | Cost | Power | Size | Requires | Notes |
|---|---|---|---|---|---|
| Construction Yard | — | 0 | 3×3 | (deploy MCV) | Builds structures |
| Power Plant | 300 | +100 | 2×2 | — | |
| Ore Refinery | 1600 | −30 | 3×3 | Power Plant | Free Harvester |
| Barracks | 400 | −20 | 2×2 | Power Plant | Infantry |
| War Factory | 1800 | −30 | 3×3 | Refinery | Vehicles |
| Radar Dome | 1000 | −40 | 2×2 | Refinery | Minimap |
| Helipad | 1200 | −20 | 2×2 | Radar, Factory | Aircraft |
| Tech Center | 1500 | −100 | 2×2 | Factory, Radar | Elite units, MCV |

## Western Concord

| Unit | Cost | Role |
|---|---|---|
| Pillbox | 400 | Anti-infantry MG nest |
| Sentinel Gun Turret | 600 | Anti-armour turret |
| Skyguard AA Battery | 800 | Flak (needs power) |
| Ranger Sniper | 600 | Long-range infantry killer |
| Pathfinder Jeep | 500 | Fast scout |
| Lancer Light Tank | 700 | Cheap, fast armour |
| Warden Battle Tank | 950 | Main battle tank |
| Longbow Artillery | 800 | Range 10 howitzer (min range 3) |
| Skyguard Flak Track | 600 | Mobile AA |
| Hawk Gunship | 1200 | Anti-armour rockets |

## Iron Pact

| Unit | Cost | Role |
|---|---|---|
| Flame Bunker | 450 | Anti-infantry flame |
| Flak Cannon | 700 | AA (needs power) |
| Arc Tower | 1200 | Devastating electric bolts (−75 power) |
| Flame Trooper | 300 | Short-range flamer |
| Shock Trooper | 600 | Portable arc projector |
| Wolf Scout Car | 450 | Fast MG car |
| Bear Heavy Tank | 1150 | Twin-barrel heavy tank |
| Thunder Rocket Launcher | 900 | 4-rocket artillery salvo |
| Titan Siege Tank | 2100 | Super-heavy twin siege guns |
| Flak Truck | 600 | Mobile AA |
| Vulture Attack Copter | 1200 | Chaingun gunship |

**Shared infantry and vehicles:** Rifleman ($100), Rocket Soldier ($300, hits aircraft), Engineer ($500), Ore Harvester ($1400), MCV ($2500).

## AI commander

| Setting | Easy | Normal | Hard |
|---|---|---|---|
| First attack | 7:00 | 5:00 | 3:20 |
| Attack interval | 4:00 | 2:50 | 2:00 |
| Minimum wave | 4 | 6 | 9 |
| Build speed | 75% | 100% | 125% |
| Bonus income | 0 | 1.5 $/s | 5 $/s |

The build order is: power whenever the margin drops below 25 → refinery → barracks → factory → second refinery → radar → faction defences (2 + 2×difficulty, growing with each wave) → tech center → helipad → extra refineries and factories. The AI keeps one harvester per refinery (plus one extra above Easy), repairs damaged buildings when it has more than $800, and pulls nearby idle units to defend any structure under attack. Attack waves grow each time and target the closest enemy economic structure first.
