# SoH: Unbound

**SoH: Unbound** is an experimental fork of Ship of Harkinian (branch `unbound`, based on tag
`9.2.3`) with one purpose: **remove the limits Ocarina of Time inherited from N64 hardware so
modders can build things the vanilla game shape cannot hold.** Its primary consumer is
[Prelude of Light](https://preludeoflight.com), a browser-based o2r editor; Prelude gains an
"Unbound" mode that targets this build (`UNBOUND.md` in the Prelude repo).

It is not a randomizer build and does not try to stay diff-minimal against upstream. Randomizer
and other enhancements that assume the vanilla tables may break; that is accepted.

## Goals

1. **Uncap.** Collision size, scene and entrance count, objects per scene, actors, rooms, mesh
   entries, message ids, world extent — any fixed N64-era number a modder can hit.
2. **Patch, don't replace.** Structured game data (scene/room headers, collision metadata, text)
   is JSON that **merges across archive layers**, so a mod ships only what it changed. No more
   bundling a whole scene because one exit moved.
3. **Portable mods.** No ROM-version-specific names in anything a mod references.
4. **Keep the container.** Everything is still an `.o2r` (zip) loaded by libultraship; only what is
   *inside* the OoT archive was redesigned.

## How it works, in one paragraph

`oot.o2r` is converted once (`soh --export-unbound oot-unbound.o2r`) into the Unbound layout: every
scene/room header becomes `scenes/<name>/scene.json` + `rooms/<n>.json`, collision becomes
`collision.json` + `collision.bin`, message tables become `text/<lang>/messages.json`, and every
other resource is copied verbatim. Placed beside `oot.o2r`, the converted archive is mounted above
it and SoH loads scenes from the JSON, merging every mounted mod's fragment of the same path. Mods
add new scenes and entrances by declaring them in `unbound/scenes.json`. The C game code was
widened wherever a struct field or arena enforced a cap.

## What has been changed

Everything below is tagged `// SOH [Unbound]` in the code. Each area has a detail doc.

| Area | Change | Doc |
|---|---|---|
| **Collision** | Vertex indices and poly ids are 32-bit; the N64 byte budget is gone — node tables are heap-allocated and grow on demand, freed in `Play_Destroy`. Legacy 13-bit packed data is unpacked on load. Dyna actor table and dyna poly/vertex lists grow on demand (no `BG_ACTOR_MAX`). | [`collision.md`](./collision.md) |
| **Scenes & entrances** | `gSceneTable`/`gEntranceTable` replaced by a runtime registry (`SceneDB`). Mods declare scenes + entrances in `unbound/scenes.json`; exit lists reference entrances by name; `EntranceInfo.scene` is 16-bit; custom-scene save flags are stored by scene name. Console: `entrance <name>`. | [`registries.md`](./registries.md) |
| **Text** | Message tables are growable and hash-indexed; `text/<lang>/messages.json` merges across layers and can **add** or delete ids; message buffers 8 KB. | [`text.md`](./text.md) |
| **Counts** | Object bank 1024 (was 128, silently dropping); actors per room and rooms per scene 16-bit; live-actor cap real (was a wrapping u8) and 8192; mesh entries 32-bit, sorted entries 1024; texture cache 8192. Room numbers 16-bit with unbounded clear flags, waterbox rooms and transition actors. Object ids past the vanilla table are usable — object "space" is vestigial on PC. | [`counts.md`](./counts.md) |
| **Scene format** | The JSON layout, entity keys (Prelude's index scheme), and merge rules (`null` deletes, arrays replace, `$replace`, `$order`). | [`scene-format.md`](./scene-format.md) |
| **World extent** | Positions are `f32` end to end: float `Mtx` (libultraship fork `GBI_FLOAT_MTX`), per-room mesh `origin`, f32 collision vertices/`dist`/bounds/water boxes (`collision.bin` v2), spawn entries, paths, point lights, colliders. `BGCHECK_XYZ_ABSMAX` is 2²⁰. Fog and draw distance are per-scene world units (`fogStart`/`fogEnd`/`drawDistance`/`nearPlane`; fog could not start past 2 500 units and `zFar` was 12 800). | [`extent.md`](./extent.md) |
| **Converter** | `soh --export-unbound <out.o2r>` / console `unbound-export`: vanilla → Unbound archive in ~1 s. `soh/soh/unbound/UnboundExporter.cpp`. | `scene-format.md` §5 |
| **Loader** | libultraship gained a JSON resource format (`{` sniff, type from `$schema`, found in any layer) and `LoadFileFromAllLayers`; SoH's JSON factories (`soh/soh/unbound/`) build the same command objects the binary loaders build, so scene execution code is untouched. | `scene-format.md` §4 |

Verified in game: a Prelude-generated mod adding a **new scene with high-poly collision** loads and
plays; a two-line delta mod merges over the converted base (`examples/hyrule-field-actor-delta/`).

## Known remaining limits

Likely next targets, roughly by how often a modder will hit them:

| Limit | Where | Notes |
|---|---|---|
| Scene camera data ±32 767 | `CamData.camPosData` is `Vec3s` (`BGCAM_*` packing in `z_camera.c`) | Fixed-camera zones cannot sit beyond the s16 range; the `Vec3s[3]` triple needs its own format change. |
| Cutscene camera points ±32 767 | `CutsceneCameraPoint.pos` is parsed straight from cutscene command words | |
| One room DL ≤ 65 535 units across | `Vtx` is `short` | Split rooms; each has its own `origin`. |
| Mesh entries in **binary** headers ≤ 255 | binary `SetMesh` stores a u8 count | JSON headers have no such cap. Only matters for legacy archives. |
| Decoded textbox 1 024 bytes | `MESSAGE_DECODED_BUF_SIZE` | Page long text with box-break control codes. |
| Path points ≤ 255 | `PathData.count` is `u8` | The JSON loader logs and cuts a longer path. |
| Entrance layer groups of 4 | `entranceIndex + sceneSetupIndex` arithmetic in `Play_Init` | Custom entrances register 4 identical layers. |
| No minimap / pause map for custom scenes | `Map_Init` keyed by vanilla scene ranges | Needs a registry field for map data. |
| Alternate setups | The JSON stores each in full; up to setup index 13 seen in vanilla | Not a cap, but `SetAlternateHeaders` arrays grow with the highest index. |
| Save data | `sceneFlags[124]` positional for vanilla scenes; custom scenes keyed by name; save states don't capture custom flags | |
| MQ | Converter emits `_mq` scenes only when `oot-mq.o2r` is mounted at export time | |

## Working on the fork

- Build: `cmake --build build-cmake --target soh -j8`. A change to `z64.h` rebuilds nearly
  everything (10+ min on a busy machine); run long builds in the background with a log.
- Smoke test: put `oot-unbound.o2r` beside `oot.o2r` in `build-cmake/soh`, launch, and grep the
  log for `[Unbound]` — the title screen loads Hyrule Field through `scene.json`.
- Regenerate `soh.o2r` (`--target GenerateSohOtr`) if switching from a branch with different
  shaders; a stale one crashes at boot.
- Prefer widening a field over adding a registry; prefer a registry over a static table; prefer
  JSON that merges over binary that replaces. Keep the `SOH [Unbound]` marker on every edit.
- libultraship is a submodule on the fork branch `unbound`; commit there first, then update the
  pointer here.
