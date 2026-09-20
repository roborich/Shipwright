# Unbound: scene and entrance registries

Replaces the compiled-in `gSceneTable` / `gEntranceTable` with a runtime registry (`SceneDB`) so
mods can add scenes and the entrances that lead into them. The registry document itself
(`unbound/scenes.json`: keys, defaults, id rules) is defined in [`SPEC.md`](./SPEC.md) §7 and
exit-by-name in SPEC §4.2; this file covers why and how. Overview in [`README.md`](./README.md).

## The cap, and why it exists

Prelude has been squeezing custom scenes into the **six spare debug scene slots** of the vanilla
table (`prelude-of-light/SCENE-IMPORT.md`, "The spare-slot mechanism") because:

- `gSceneTable[]` and `gEntranceTable[]` are C arrays generated from `tables/scene_table.h` and
  `tables/entrance_table.h` at compile time (`soh/src/code/z_scene_table.c`).
- `EntranceInfo.scene` was an `s8` — 127 scenes, full stop.
- `gSaveContext.sceneFlags[124]` is a fixed positional array, serialised positionally by
  `SaveManager`.
- `Play_Init` dispatches `gEntranceTable[entranceIndex + sceneSetupIndex]` with no bounds check;
  entrances are grouped in fours (child day/night, adult day/night) and the group is not shiftable.

## What changes

### `SceneDB` (`soh/soh/unbound/SceneDB.{h,cpp}`)

Mirrors the existing `ActorDB` pattern: the X-macro tables are kept **only as seed data** for a
`std::vector`, and mods extend the vector at runtime.

- Scenes: `Entry { id, name, displayName, sceneFileName | scenePath, titleCardTexture, drawConfig }`.
  Vanilla entries are named by their enum (`SCENE_DEKU_TREE`), take their `displayName` from
  `SohUtils::GetSceneName` once the registry is first used at runtime (`LoadCustomScenes`), and
  resolve their o2r path with the vanilla MQ policy (`scenes/{shared,mq,nonmq}/<file>/<file>`;
  `GetScenePath(id, masterQuest)` picks the variant explicitly, `GetScenePath(id)` uses the mounted
  game's setting). Custom entries carry a full path.
- Custom scene ids start at `CUSTOM_SCENE_ID_BASE` (SPEC §7), leaving `SCENE_ID_MAX` (`0x6E`)
  free — it doubles as an "unused / no scene" sentinel in the vanilla entrance table and in
  Anchor/randomizer code.
- Entrances: `gEntranceTable` is now a **pointer** into the registry's vector, so the ~25 existing
  `gEntranceTable[i]` read sites compile unchanged. `EntranceInfo.scene` is `s16`. A custom
  entrance registers a full 4-entry layer group starting at an index that is `>= ENTR_MAX` and a
  multiple of 4 (SPEC §7), so `entranceIndex + sceneSetupIndex` keeps working.
- Names: `EntranceDB_RetrieveIndex(name)` resolves both vanilla enum names and custom
  `<scene id>/<entrance id>` names; the JSON loader's `ResolveExit` and the debug console's
  `entrance` command both use it. The console tries a registered name first, then a hex index, so
  a name that starts with hex digits (`ENTR_DEKU_TREE_0`) is never misread as a number.
- Registry loading (`LoadCustomScenes`): `unbound/scenes.json` is read through
  `Unbound::LoadMergedJson` after all mod archives are mounted (`UpdateModFiles(init)` in
  `mod_menu.cpp`), so a mod can add a scene, patch another mod's entrance, or delete one with the
  same merge rules as every other document. Each entry is registered in its own `try` — one
  malformed entry is logged and skipped, the rest load; `drawConfig` is range-checked before it is
  narrowed, and a `sceneId` or entrance `index` left over from an older mod is ignored with a
  warning, because the game assigns both. `DetectUnboundBase` parses every layer's `unbound.json`
  for the version check and treats a layer as a base only when its `features` lists `"scenes"`
  (SPEC §1.3, §6), so a mod may carry a manifest for `requires.formatVersion` without rerouting
  vanilla scenes.

`gSceneTable` no longer exists. Its two readers (`OTRPlay_SpawnScene`, randomizer `logic.cpp`)
use the registry. `PlayState.loadedScene` and `SceneTableEntry` are gone; their only readers
computed an unused title-file size.

Illustrative registry entry (the contract is SPEC §7):

```json
{ "mymod/lava_temple": { "scene": "scenes/mymod/lava_temple/scene.json",
                         "entrances": { "main": { "spawn": 0, "showTitleCard": true } } } }
```

### Saved scene flags

`SceneFlags_Get(sceneNum)` returns `&gSaveContext.sceneFlags[n]` for vanilla ids and a
registry-owned `SavedSceneFlags` for custom ids; an unregistered id gets zeroed scratch storage and
an error in the log. The four by-`sceneNum` sites (`Play_SaveSceneFlags`, `Actor_InitContext`,
`GameInteractor_RawAction`, the debug save editor's Reload/Save Flags buttons) use it.
`SaveManager` persists custom flags in a new `"unbound"` section as
`sceneFlags.<scene id>.{chest,swch,…}`, keyed by **name**, so they survive id reassignment between
mod stacks and never touch the positional vanilla array (old saves stay valid).

Known gap: save states (`savestates.cpp`) `memcpy` `gSaveContext` only, so they don't capture
custom-scene flags.

### Guards added

- `Play_Init`: an entrance index outside the registry (a save from a different mod stack, or a
  typo in an exit list) logs and falls back to Hyrule Field instead of reading past the table.
- `OTRPlay_SpawnScene`: unknown scene ids and out-of-range draw configs are logged and clamped;
  `Scene_Draw` therefore never indexes `sSceneDrawHandlers` out of bounds.
- `SohUtils::GetSceneName` no longer asserts on custom ids; the crash handler prints the registry
  name for them.

## Epona in a custom scene

Vanilla allows the horse in exactly five scenes. `func_8006CFC0` in `z_horse.c` held the list, and
`func_8006DC68` gates **the whole horse-spawn pass** on it, which is why placing an `EnHorse` in a
custom scene by hand — the workaround before this — gets you a horse that Epona's Song cannot call
and that does not survive a scene transition: `z_player.c` sets `AREG(6)` on a mounted exit with no
scene check, but the arriving scene fails the gate before the code that re-spawns and re-mounts her
can run.

The list is now seed data in `SceneDB` and the gate is `SceneDB_HorseAllowed`, so a scene opts in
through the registry:

```json
"mymod/plains": {
  "scene": "scenes/mymod/plains/scene.json",
  "horse": { "pos": [1200, 0, -400], "angle": 16384 }
}
```

Presence of `horse` is the permission; `pos`/`angle` are where she waits when the player has not
brought her. `"horse": true` allows her with no idle spot — right for a scene you only ride
through, but note that Epona's Song calls a horse that is *already* in the scene rather than
creating one, so without `pos` she is only ever there because the player brought her. Vanilla's
five are seeded, so vanilla answers are unchanged, and the key is additive (SPEC §10, version 2).

What it takes besides the gate:

- **Her object.** She is spawned from the registry, not from a room's actor list, so
  `Scene_CommandObjectList` (`z_scene_otr.cpp`) appends `OBJECT_HORSE` to the object list of every
  room of a *custom* horse scene that does not already list it. It has to be per room: an object
  past the room's own list is dropped the next time that command runs, and `func_80031A28` kills
  the actor with it. Mod authors do not have to think about this. Vanilla rooms are left alone —
  the five vanilla horse scenes already ship object lists that account for her.
- **A call point.** `EnHorse_Spawn` moves her to the nearest *off-screen* entry of
  `sHorseSpawns[]`, 169 hand-placed points covering the five vanilla scenes. A custom scene has
  none, so `EnHorse_SpawnNearPlayer` generates one: ring positions around the player tried from
  directly behind him outwards. A candidate has to pass `EnHorse_CalcFloorHeight` — the same test
  her movement code applies to the ground ahead, which rejects no floor, water, a slope past 35°
  and a horse-blocked surface — and be within 300 units of the player vertically. Hand-placed
  points made that implicit; a generated one has to earn it, or she arrives in a lake or at the
  bottom of a canyon she cannot be ridden out of. She always arrives facing him. Vanilla scenes
  keep the table.

  Being off camera is a *preference*, not a requirement: the first candidate that is off screen
  wins, and if every standable heading is on screen she arrives at the first of those instead. A
  scene can be small enough to be visible all over, and a horse that appears in view is better
  than a song that does nothing. Only ground she cannot stand on, or a point within 100 units of
  the camera, rules a heading out entirely.

  The on-screen test is `EnHorse_CallPointOnScreen`, which projects the point itself, deliberately
  *not* vanilla's `func_80A5BBBC`. That one answers the question with the actor culling test, which
  pads the point by `uncullZoneScale` — 600 units for a horse — because it decides whether a horse
  standing there would be *drawn*. Vanilla's call points are thousands of units apart and clear
  that padding; every point on a 300-unit ring falls inside it, so reusing the culling test
  rejected all eight headings at every camera angle and the song silently did nothing.
- **Where she is parked.** `gSaveContext.horseData.scene` is a numeric id, stable for a custom
  scene only when the registry assigned it explicitly, so the `unbound` save section stores the
  scene *name* alongside it and that name wins on load — the same reasoning as the scene flags.
  If the mod that owned the scene is gone, the name no longer resolves and `func_8006D074` puts her
  back at her Hyrule Field default, which is what vanilla does with a bad parked scene anyway.

She stays adult-only (`func_8006DC68`), and Epona's Song itself never had a scene list — it sets
`DREG(53)`, which only an existing `EnHorse` consumes.

### Better Debug Warp screen (`soh/soh/unbound/UnboundSceneSelect.{h,cpp}`)

The screen (`z_select.c`) reads its list through `SelectContext.betterScenes`/`count`, so
`Select_SwitchBetterWarpMode` points them at `UnboundSceneSelect_BuildList`: the vanilla
`sBetterScenes` followed by one line per custom scene that has an entrance, numbered on from the
vanilla 50 (`51:Lava Temple`). The scene's display name is used for every language; each entrance
is labelled by its key (`main`), since SPEC §7 gives entrances no display name. A scene shows at
most 18 entrances (the screen's fixed `entrancePairs`); the rest are logged and stay reachable from
the console. Custom entries are never MQ. The remembered line is a list position, so a changed mod
set can land it on another scene; the saved entrance position is clamped to that scene's entrances,
and a saved scroll window that no longer holds the line (it wrapped at the old list length) is
re-anchored on it, since the screen only scrolls to follow a line that starts inside the window.

## Not changed (custom scenes fall outside every vanilla range check)

- Minimap / pause map (`Map_Init`, `z_map_mark`, kaleido): all keyed by dungeon or overworld
  `mapIndex` derived from vanilla scene-id ranges; a custom scene simply has no map, as the debug
  slots have today. A registry field for map data is a follow-up.
- MQ selection (`SceneDB::GetScenePath`) and the world-map-area / dungeon-mode range checks
  treat custom ids as "other". Correct for now.
- Title cards for vanilla scenes still come from the 66-case `switch` in `TitleCard_InitPlaceName`;
  custom scenes use the registry's `titleCardTexture`.
- Randomizer entrance shuffle still copies exactly `ENTR_MAX` entries; custom entrances are never
  shuffled. Randomizer is out of scope for Unbound.
- Per-layer entrance overrides (`layers`, SPEC §7) are reserved but not read.

## Status

Implemented on the `unbound` branch. See the README status table for build/verification state.

## Verification

1. Vanilla parity: boot, file select, several scene transitions incl. MQ dungeon, title cards,
   scene flags persisting across a save/load (chests stay opened).
2. Custom: an archive whose `unbound/scenes.json` points at a copied vanilla scene
   (`scenes/shared/…`) with one entrance; `entrance <name>` in the console lands in it; a chest
   opened there is still open after save + reload; the crash-handler / warp UI show the display
   name.
3. Robustness: an old save whose `entranceIndex` no longer exists lands in Hyrule Field with a
   log line, not a crash.
4. Names: a `scene.json` exit naming `mymod/x/main` and one naming `ENTR_KOKIRI_FOREST_0` both
   resolve; a misspelt name fails the scene with one log line.
5. Epona: in a custom scene with `horse`, Epona's Song calls her from off camera; riding her
   through an exit into another horse scene keeps her under Link; dismounting, saving and
   reloading finds her where she was left; a scene without the key still refuses her.
6. Debug warp: with the Better Debug Warp screen on, custom scenes follow `50:Debug` under their
   display names, C-left/right cycles their entrance keys, and A lands at the chosen entrance as
   the chosen age and time of day; restarting without the mod drops the lines, and a remembered
   custom line falls back to a valid one without a crash.
