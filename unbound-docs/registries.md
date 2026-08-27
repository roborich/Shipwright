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

### `SceneDB` (`soh/soh/SceneDB.{h,cpp}`)

Mirrors the existing `ActorDB` pattern: the X-macro tables are kept **only as seed data** for a
`std::vector`, and mods extend the vector at runtime.

- Scenes: `Entry { id, name, displayName, sceneFileName | scenePath, titleCardTexture, drawConfig }`.
  Vanilla entries are named by their enum (`SCENE_DEKU_TREE`) and resolve their o2r path with the
  vanilla MQ policy (`scenes/{shared,mq,nonmq}/<file>/<file>`). Custom entries carry a full path.
- Custom scene ids start at **`0x80`** (`CUSTOM_SCENE_ID_BASE`), leaving `SCENE_ID_MAX` (`0x6E`)
  free — it doubles as an "unused / no scene" sentinel in the vanilla entrance table and in
  Anchor/randomizer code.
- Entrances: `gEntranceTable` is now a **pointer** into the registry's vector, so the ~25 existing
  `gEntranceTable[i]` read sites compile unchanged. `EntranceInfo.scene` is `s16`. A custom
  entrance registers a full 4-entry layer group (all four identical) starting at an index that is
  `>= ENTR_MAX` and a multiple of 4, so `entranceIndex + sceneSetupIndex` keeps working.
- Names: vanilla entrances are addressable by their enum name (`ENTR_HYRULE_FIELD_0`); custom ones
  as `<scene id>/<entrance id>`. `EntranceDB_RetrieveIndex(name)` resolves either. The debug
  console's `entrance` command accepts a name as well as a hex index.

`gSceneTable` no longer exists. Its two readers (`OTRPlay_SpawnScene`, randomizer `logic.cpp`)
use the registry. `play->loadedScene` is set to `NULL`; the only remaining readers computed an
unused title-file size.

### Custom scene files

Any loaded archive may contain `unbound/scenes/<anything>.json`, one scene per file, parsed after
all mod archives are mounted (`UpdateModFiles(init)` in `mod_menu.cpp`):

```json
{
  "id": "mymod/lava_temple",
  "name": "Lava Temple",
  "scene": "scenes/custom/lava_temple/lava_temple",
  "sceneId": 200,
  "drawConfig": 0,
  "titleCard": "textures/mymod/lava_temple_title",
  "entrances": [
    { "id": "main", "index": 1560, "spawn": 0, "titleCard": true,
      "continueBgm": false, "endTransition": 2, "startTransition": 2 }
  ]
}
```

| Field | Required | Notes |
|---|---|---|
| `id` | yes | Stable string id. Also the key under which the scene's saved flags are stored. |
| `name` | no | Display name (menus, crash log). Defaults to `id`. |
| `scene` | yes | Full o2r path of the scene resource (a `Room`/scene resource, binary or XML). Rooms are referenced by the scene's own room list, as today. |
| `sceneId` | no | Explicit numeric id `>= 0x80`. Omit to take the next free one. Only needed when something outside the archive hard-codes the number. |
| `drawConfig` | no | `SDC_*` index (0 = default). Out-of-range values are rejected. |
| `titleCard` | no | o2r path of a title card texture; shown when an entrance sets `titleCard: true`. |
| `entrances[].id` | yes | Entrance name, scoped as `<scene id>/<id>`. |
| `entrances[].index` | no | Explicit first index of the 4-entry group, `>= 1556` and a multiple of 4. **Set it explicitly** when other scenes' exit lists point at this entrance — an auto-assigned index depends on mod load order. |
| `entrances[].spawn` | no | Spawn index into the scene's start-position list. |
| `entrances[].titleCard` / `continueBgm` | no | `EntranceInfo` field flags. |
| `entrances[].endTransition` / `startTransition` | no | `TRANS_TYPE_*` values; default 2 (fade to black). |

A vanilla scene's exit list is just a list of entrance indices, so an edited vanilla scene can
exit into a custom scene by naming its (explicit) index — exactly what Prelude already does for
the debug slots, minus the slot.

### Saved scene flags

`SceneFlags_Get(sceneNum)` returns `&gSaveContext.sceneFlags[n]` for vanilla ids and a
registry-owned `SavedSceneFlags` for custom ids. The three by-`sceneNum` sites
(`Play_SaveSceneFlags`, `Actor_InitContext`, `GameInteractor_RawAction`) use it. `SaveManager`
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
2. Custom: an archive with `unbound/scenes/test.json` pointing at a copied vanilla scene
   (`scenes/shared/…`) with one entrance; `entrance <name>` in the console lands in it; a chest
   opened there is still open after save + reload; the crash-handler / warp UI show the display
   name.
3. Robustness: an old save whose `entranceIndex` no longer exists lands in Hyrule Field with a
   log line, not a crash.
