#!/usr/bin/env python3
"""
Validates the COMMANDERS data files (Content/Data) without needing Unreal.

Checks entity/weapon/faction references, model templates, mission maps
(bounds, overlapping structures, units spawned inside structures),
script triggers/actions/conditions, objective ids, tags and cinematics.

Usage:  python3 Tools/validate_data.py            (exit code 1 on errors)
"""
import json
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content", "Data")

ACTION_TYPES = {
    "dialogue", "message", "spawn", "order", "reveal", "camera", "cinematic", "objective_add",
    "objective_show", "objective_complete", "objective_fail", "timer", "timer_stop", "flag",
    "trigger_enable", "trigger_disable", "credits", "victory", "defeat", "transfer", "destroy",
    "damage", "explosion", "invulnerable", "ai", "ai_attack", "build_enable", "unlock", "sun", "delay",
}
CONDITION_TYPES = {
    "time", "area", "destroyed", "captured", "objective", "count_below", "built", "produced",
    "timer", "flag", "damaged", "team_defeated", "credits", "all", "any", "not", "start",
}
OBJECTIVE_TYPES = {"destroy_all", "destroy", "protect", "reach", "survive", "capture", "build", "eliminate", "custom"}
SHAPES = {"cube", "sphere", "cylinder", "cone", "plane"}
CATEGORIES = {"structure", "infantry", "vehicle", "aircraft"}
THEMES = {"temperate", "winter", "desert", "dusk"}

errors = []
warnings = []


def err(where, msg):
    errors.append(f"[{where}] {msg}")


def warn(where, msg):
    warnings.append(f"[{where}] {msg}")


def load(rel):
    path = os.path.join(ROOT, rel)
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:  # noqa: BLE001
        err(rel, f"cannot load: {e}")
        return None


def as_list(v):
    if v is None:
        return []
    return v if isinstance(v, list) else [v]


def check_part(where, part):
    shape = part.get("shape", "cube")
    if shape not in SHAPES and not shape.startswith("/"):
        err(where, f"unknown shape '{shape}'")
    for key in ("at", "size", "rot"):
        if key in part and (not isinstance(part[key], list) or len(part[key]) not in (1, 2, 3)):
            err(where, f"'{key}' must be a list of 1-3 numbers")


def main():
    factions = load("Factions.json") or {}
    weapons = load("Weapons.json") or {}
    entities_doc = load("Entities.json") or {}
    campaigns = load("Campaigns.json") or {}
    load("Cinematics.json")

    faction_ids = {f["id"] for f in factions.get("factions", [])}
    weapon_ids = set()
    for w in weapons.get("weapons", []):
        if w["id"] in weapon_ids:
            err("Weapons", f"duplicate weapon {w['id']}")
        weapon_ids.add(w["id"])

    templates = entities_doc.get("templates", {})
    for name, parts in templates.items():
        for p in parts:
            check_part(f"template {name}", p)

    ents = {}
    for e in entities_doc.get("entities", []):
        eid = e.get("id")
        if eid in ents:
            err("Entities", f"duplicate entity {eid}")
        ents[eid] = e
    for eid, e in ents.items():
        where = f"entity {eid}"
        cat = e.get("category", "infantry")
        if cat not in CATEGORIES:
            err(where, f"bad category {cat}")
        if e.get("weapon") and e["weapon"] not in weapon_ids:
            err(where, f"unknown weapon {e['weapon']}")
        for req in e.get("prereqs", []):
            if req not in ents:
                err(where, f"unknown prereq {req}")
        for key in ("free_unit", "deploys_into"):
            if e.get(key) and e[key] not in ents:
                err(where, f"unknown {key} {e[key]}")
        fac = e.get("faction", "any")
        if fac != "any" and fac not in faction_ids:
            err(where, f"unknown faction {fac}")
        for t in as_list(e.get("model")):
            if t not in templates:
                err(where, f"unknown model template {t}")
        for p in e.get("parts", []):
            check_part(where, p)
        if not e.get("parts") and not e.get("model"):
            warn(where, "has no visual parts")
        if e.get("buildable", True) and cat != "structure" and not e.get("prereqs"):
            warn(where, "buildable unit without prerequisites")

    for f in factions.get("factions", []):
        for key in ("ai_defenses", "starter_units"):
            for x in f.get(key, []):
                if x not in ents:
                    err(f"faction {f['id']}", f"unknown {key} entry {x}")

    for c in campaigns.get("campaigns", []):
        if c.get("faction") not in faction_ids:
            err(f"campaign {c.get('id')}", "unknown faction")
        for m in c.get("missions", []):
            mission = load(m["file"])
            if mission is not None:
                if mission.get("id") != m["id"]:
                    err(m["file"], f"id mismatch ({mission.get('id')} vs {m['id']})")
                check_mission(m["file"], mission, ents)

    for w in warnings:
        print("WARNING", w)
    for e in errors:
        print("ERROR  ", e)
    total_missions = sum(len(c.get("missions", [])) for c in campaigns.get("campaigns", []))
    print(f"\n{len(ents)} entities, {len(weapon_ids)} weapons, {total_missions} missions checked: "
          f"{len(errors)} error(s), {len(warnings)} warning(s)")
    return 1 if errors else 0


def check_mission(where, m, ents):
    mp = m.get("map", {})
    W, H = mp.get("width", 64), mp.get("height", 64)
    if mp.get("theme", "temperate") not in THEMES:
        err(where, f"unknown theme {mp.get('theme')}")
    teams = m.get("teams", [])
    nteams = len(teams)
    if sum(1 for t in teams if t.get("player")) != 1:
        err(where, "exactly one team must have \"player\": true")
    for i, t in enumerate(teams):
        for key in ("allowed", "locked"):
            for x in t.get(key, []):
                if x not in ents:
                    err(where, f"team {i} {key} has unknown entity {x}")

    def in_map(x, y):
        return 0 <= x < W and 0 <= y < H

    occupied = {}
    tags = set()
    unit_cells = []
    for i, e in enumerate(m.get("entities", [])):
        t = e.get("type")
        d = ents.get(t)
        w = f"{where} entity#{i} {t}"
        if d is None:
            err(w, "unknown type")
            continue
        team = e.get("team", 0)
        if not (0 <= team < nteams):
            err(w, f"bad team {team}")
        x, y = e.get("at", [0, 0])[:2]
        if e.get("tag"):
            tags.add(e["tag"])
        if d.get("category") == "structure":
            fw, fh = d.get("footprint", [1, 1])
            if not (in_map(x, y) and in_map(x + fw - 1, y + fh - 1)):
                err(w, f"footprint at {x},{y} leaves the map")
            for yy in range(y, y + fh):
                for xx in range(x, x + fw):
                    if (xx, yy) in occupied:
                        err(w, f"overlaps {occupied[(xx, yy)]} at {xx},{yy}")
                    occupied[(xx, yy)] = t
        else:
            if not in_map(x, y):
                err(w, f"unit at {x},{y} outside map")
            unit_cells.append((x, y, t, i))
    for x, y, t, i in unit_cells:
        if (x, y) in occupied:
            warn(where, f"unit #{i} {t} starts inside structure {occupied[(x, y)]} at {x},{y} (will be nudged)")

    objectives = {}
    for o in m.get("objectives", []):
        oid = o.get("id")
        if oid in objectives:
            err(where, f"duplicate objective {oid}")
        objectives[oid] = o
        if o.get("type") not in OBJECTIVE_TYPES:
            err(where, f"objective {oid} has unknown type {o.get('type')}")
        if o.get("type") in ("destroy", "protect", "capture") and not o.get("tag"):
            err(where, f"objective {oid} needs a tag")
        if o.get("type") == "build" and o.get("entity") not in ents:
            err(where, f"objective {oid} builds unknown entity")

    cinematics = m.get("cinematics", {})
    for key in ("intro_cinematic", "outro_cinematic", "defeat_cinematic"):
        if m.get(key) and m[key] not in cinematics:
            err(where, f"{key} '{m[key]}' not defined")

    spawn_tags = set()

    def check_actions(actions, ctx):
        for a in actions:
            at = a.get("type")
            w = f"{where} {ctx} action {at}"
            if at not in ACTION_TYPES:
                err(w, "unknown action type")
                continue
            if at == "spawn":
                for u in as_list(a.get("units")):
                    if u not in ents:
                        err(w, f"unknown unit {u}")
                team = a.get("team", 1)
                if isinstance(team, int) and not (0 <= team < nteams):
                    err(w, f"bad team {team}")
                xy = a.get("at", [0, 0])
                if not in_map(xy[0], xy[1]):
                    err(w, f"spawn point {xy} outside map")
                if a.get("tag"):
                    spawn_tags.add(a["tag"])
            if at in ("objective_show", "objective_complete", "objective_fail") and a.get("id") not in objectives:
                err(w, f"unknown objective {a.get('id')}")
            if at == "objective_add":
                o = a.get("objective", {})
                objectives[o.get("id")] = o
            if at == "cinematic" and a.get("id") not in cinematics:
                err(w, f"unknown cinematic {a.get('id')}")
            if at == "delay":
                check_actions(a.get("actions", []), ctx + " delay")
            for key in ("attack_move", "move_to", "target"):
                if key in a and not in_map(a[key][0], a[key][1]):
                    err(w, f"{key} {a[key]} outside map")

    check_actions(m.get("on_start", []), "on_start")
    for t in m.get("triggers", []):
        check_actions(t.get("actions", []), f"trigger {t.get('id')}")
    for cid, c in cinematics.items():
        for ev in c.get("events", []):
            check_actions([ev.get("action", {})], f"cinematic {cid}")
        if not c.get("shots"):
            err(where, f"cinematic {cid} has no shots")

    all_tags = tags | spawn_tags

    def check_condition(c, ctx):
        if not isinstance(c, dict):
            err(where, f"{ctx}: condition must be an object")
            return
        ct = c.get("type")
        if ct not in CONDITION_TYPES:
            err(where, f"{ctx}: unknown condition {ct}")
        if ct in ("destroyed", "captured", "damaged") and c.get("tag") not in all_tags:
            err(where, f"{ctx}: tag '{c.get('tag')}' never exists")
        if ct == "objective" and c.get("id") not in objectives:
            err(where, f"{ctx}: unknown objective {c.get('id')}")
        if ct == "built" and c.get("entity") not in ents:
            err(where, f"{ctx}: unknown entity {c.get('entity')}")
        for sub in c.get("conditions", []):
            check_condition(sub, ctx)
        if "condition" in c:
            check_condition(c["condition"], ctx)

    trigger_ids = set()
    for t in m.get("triggers", []):
        if t.get("id") in trigger_ids:
            err(where, f"duplicate trigger {t.get('id')}")
        trigger_ids.add(t.get("id"))
        check_condition(t.get("when"), f"trigger {t.get('id')}")
    for o in objectives.values():
        if o.get("tag") and o["tag"] not in all_tags:
            err(where, f"objective {o.get('id')} references missing tag {o['tag']}")


if __name__ == "__main__":
    sys.exit(main())
