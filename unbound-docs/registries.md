# Unbound: scene and entrance registries

Replaces the compiled-in `gSceneTable` / `gEntranceTable` with a runtime registry (`SceneDB`) so
mods can add scenes and the entrances that lead into them. Overview in [`README.md`](./README.md).

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
  `SohUtils::GetSceneName` once the registry is first used at runtime (`LoadCustomScenes`), and resolve
  their o2r path with the vanilla MQ policy (`scenes/{shared,mq,nonmq}/<file>/<file>`;
  `GetScenePath(id, masterQuest)` picks the variant explicitly, `GetScenePath(id)` uses the mounted
  game's setting). Custom entries carry a full path.
- Custom scene ids start at **`0x80`** (`CUSTOM_SCENE_ID_BASE`), leaving `SCENE_ID_MAX` (`0x6E`)
  free — it doubles as an "unused / no scene" sentinel in the vanilla entrance table and in
  Anchor/randomizer code.
- Entrances: `gEntranceTable` is now a **pointer** into the registry's vector, so the ~25 existing
  `gEntranceTable[i]` read sites compile unchanged. `EntranceInfo.scene` is `s16`. A custom
  entrance registers a full 4-entry layer group (all four identical) starting at an index that is
  `>= ENTR_MAX` and a multiple of 4, so `entranceIndex + sceneSetupIndex` keeps working.
- Names: vanilla entrances are addressable by their enum name (`ENTR_HYRULE_FIELD_0`); custom ones
  as `<scene id>/<entrance id>`. `EntranceDB_RetrieveIndex(name)` resolves either. The debug
  console's `entrance` command tries a registered name first, then a hex index (so a name that happens
  to start with hex digits, `ENTR_DEKU_TREE_0`, is never misread as a number).

`gSceneTable` no longer exists. Its two readers (`OTRPlay_SpawnScene`, randomizer `logic.cpp`)
use the registry. `PlayState.loadedScene` and `SceneTableEntry` are gone; their only readers computed
an unused title-file size.

### Custom scene registry: `unbound/scenes.json`

One layer-merged document (§3 of `scene-format.md`: `null` deletes, `$replace`, later archive wins)
keyed by scene id, parsed after all mod archives are mounted (`UpdateModFiles(init)` in
`mod_menu.cpp`). A mod can therefore add a scene, patch another mod's entrance, or delete one, with
the same rules as every other document.

```json
{
  "mymod/lava_temple": {
    "name": "Lava Temple",
    "scene": "scenes/mymod/lava_temple/scene.json",
    "sceneId": 200,
    "drawConfig": 0,
    "titleCardTexture": "textures/mymod/lava_temple_title",
    "entrances": {
      "main": { "index": 1560, "spawn": 0, "showTitleCard": true, "continueBgm": false,
                "endTransition": 2, "startTransition": 2 }
    }
  }
}
```

| Field | Required | Notes |
|---|---|---|
| key | yes | Stable string id of the scene. Also the key under which the scene's saved flags are stored. |
| `name` | no | Display name (menus, crash log). Defaults to the key. |
| `scene` | yes | Full o2r path of the scene resource: a `scene.json`, or a binary/XML scene. Rooms come from the scene's own room list. |
| `sceneId` | no | Explicit numeric id `>= 0x80`. Omit to take the next free one. Only needed when something outside the archive hard-codes the number. |
| `drawConfig` | no | `SDC_*` index (0 = default). Out-of-range values are rejected. |
| `titleCardTexture` | no | o2r path of a title card texture; shown when an entrance sets `showTitleCard`. |
| `entrances` | no | Keyed list; the key is the entrance id, addressable as `<scene id>/<entrance id>`. |
| `entrances.*.index` | no | Explicit first index of the 4-entry group, `>= 1556` and a multiple of 4. Only needed when a **binary** scene's exit list points at this entrance by number; JSON exit lists reference entrances by name and never need it. |
| `entrances.*.spawn` | no | Spawn index into the scene's start-position list. |
| `entrances.*.showTitleCard` / `continueBgm` | no | `EntranceInfo` field flags. |
| `entrances.*.endTransition` / `startTransition` | no | `TRANS_TYPE_*` values; default 2 (fade to black). |
| `entrances.*.layers` | reserved | Per-layer (child/adult × day/night) overrides, not read yet; a custom entrance registers four identical layers today. |

A JSON scene's exit list names entrances (`"exits": { "3": "mymod/lava_temple/main" }`, or a vanilla
`ENTR_*` name); the loader resolves names through the registry when the scene loads, so no mod
carries a table index. Numbers are still accepted (the converter emits them for vanilla scenes).

### Saved scene flags

`SceneFlags_Get(sceneNum)` returns `&gSaveContext.sceneFlags[n]` for vanilla ids and a
registry-owned `SavedSceneFlags` for custom ids; an unregistered id gets zeroed scratch storage and an
error in the log. The four by-`sceneNum` sites (`Play_SaveSceneFlags`, `Actor_InitContext`,
`GameInteractor_RawAction`, the debug save editor's Reload/Save Flags buttons) use it. `SaveManager`
persists custom flags in a new `"unbound"` section as `sceneFlags.<scene id>.{chest,swch,…}`,
keyed by **name**, so they survive id reassignment between mod stacks and never touch the
positional vanilla array (old saves stay valid).

Known gap: save states (`savestates.cpp`) `memcpy` `gSaveContext` only, so they don't capture
custom-scene flags.

### Guards added

- `Play_Init`: an entrance index outside the registry (a save from a different mod stack, or a
  typo in an exit list) logs and falls back to Hyrule Field instead of reading past the table.
- `OTRPlay_SpawnScene`: unknown scene ids and out-of-range draw configs are logged and clamped;
  `Scene_Draw` therefore never indexes `sSceneDrawHandlers` out of bounds.
- `SohUtils::GetSceneName` no longer asserts on custom ids; the crash handler prints the registry
  name for them.

## Not changed (custom scenes fall outside every vanilla range check)

- Minimap / pause map (`Map_Init`, `z_map_mark`, kaleido): all keyed by dungeon or overworld
  `mapIndex` derived from vanilla scene-id ranges; a custom scene simply has no map, as the debug
  slots have today. A registry field for map data is a follow-up.
- MQ selection (`SceneDB::GetScenePath`) and the world-map-area / dungeon-mode range checks
  treat custom ids as "other". Correct for now.
- Title cards for vanilla scenes still come from the 66-case `switch` in `TitleCard_InitPlaceName`;
  custom scenes use the registry's `titleCard`.
- The Better Debug Warp screen (`z_select.c`) lists only vanilla entrances. Use the console
  (`entrance mymod/lava_temple/main`) or an exit from an edited scene.
- Randomizer entrance shuffle still copies exactly `ENTR_MAX` entries; custom entrances are never
  shuffled. Randomizer is out of scope for Unbound.

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
