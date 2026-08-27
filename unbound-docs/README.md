# SoH: Unbound — design

**Unbound** is an experimental build of Ship of Harkinian whose only purpose is to remove the
limits that Ocarina of Time inherited from N64 hardware, so that modding tools (primarily
[Prelude of Light](https://preludeoflight.com)) can produce content the vanilla game shape
cannot hold: bigger collision, more rooms/actors/objects, new scenes and entrances, added text,
and — critically — **mods that ship deltas instead of whole resources**.

It is a fork of SoH `9.2.3` on the `unbound` branch. It is not a randomizer build; randomizer
and other enhancements that depend on the static vanilla tables are out of scope and may break.
Players who want those use vanilla SoH.

This document is the overview. Each area has a detail doc:

| Doc | Status |
|-----|--------|
| [`collision.md`](./collision.md) — uncapped collision geometry | **built; boots to title (Hyrule Field attract scene) with Prelude mods mounted; gameplay verification pending** |
| [`scene-format.md`](./scene-format.md) — the Unbound archive layout, JSON schema, merge rules, loader + converter | **spec written; no code yet** |
| [`text.md`](./text.md) — growable hash-indexed message tables, JSON merge files, additive mods | **built; boots to title (Hyrule Field attract scene) with Prelude mods mounted; gameplay verification pending** |
| [`registries.md`](./registries.md) — scene / entrance registry (`SceneDB`), JSON scene files, name-keyed save flags | **built; boots to title (Hyrule Field attract scene) with Prelude mods mounted; gameplay verification pending** |
| [`counts.md`](./counts.md) — object bank, actor/room header counts, live actor cap, mesh entry / sort / texture cache caps | **built; boots to title (Hyrule Field attract scene) with Prelude mods mounted; gameplay verification pending** |

## Guiding principles

1. **Keep the `.o2r` container.** It is a zip of named entries; libultraship already loads
   binary, XML, and `.meta`-described resources from it and layers archives. Everything
   *inside* the OoT archive is up for redesign; the container and the loader machinery are not.
2. **No obligation to the vanilla shape.** The current resource structures mirror N64 memory
   layouts (13-bit vertex indices, `u8` room counts, static ROM tables). Unbound resources are
   shaped by what a PC build wants to consume, not by what the N64 needed.
3. **Stable, human names.** Resource names never encode ROM byte offsets. This is done at
   extraction time (Torch, branch `stable-names-poc`) and is what makes a mod portable across
   ROM revisions.
4. **A mod file *is* a patch.** Structured resources (scene/room headers, collision metadata,
   text, registries) are text (JSON/XML) and **merge by key across archive layers**. Bulk data
   (vertex/poly arrays, display lists, textures) replaces whole. A Prelude user who changes one
   exit ships a few hundred bytes, not a scene.
5. **Lift limits in the game code, not just the format.** A wider file format is useless while
   `z_bgcheck.c` still allocates N64-sized arenas. Every limit lift is a game-code change first;
   the format follows.
6. **Compile-time, not CVar.** Most lifts change struct layouts. Unbound is a build
   (`SOH_UNBOUND`), not a toggle. Unbound archives carry a marker so a vanilla SoH refuses them
   cleanly rather than crashing.
7. **Vanilla `oot.o2r` is the source, never the target.** A converter (Torch, or SoH on first
   launch — the same shape as today's extractor) produces `oot-unbound.o2r`. The same converter
   reads legacy mods. Upstream adoption is a possible outcome, never a design constraint.

## Where the limits actually are

The survey that motivated this design (SoH 9.2.3, `unbound` branch):

| Limit | Where | Value today | Lift |
|---|---|---|---|
| Collision vertices per header | `COLPOLY_VTX_INDEX` 13-bit, `z64bgcheck.h` | 8 191 | widen indices to u32 |
| Collision polys per header | `SSNode.polyId` s16 | 32 767 | widen node table to u32 |
| Collision arena | `BgCheck_Allocate`, hardcoded per-scene byte budgets from the N64 | ~0x1CC00 ×2 | size from actual counts, heap-allocated |
| Dyna (actor) collision polys/verts | `polyListMax`/`vtxListMax` 512 ×2 | 1 024 | generous fixed cap, growable node list |
| Rooms per scene | `PlayState.numRooms` u8 | 255 | u16 |
| Actors per room header | `numSetupActors` u8 (importer already reads u32, then truncates) | 255 | u16/u32 |
| Object bank | `OBJECT_EXCHANGE_BANK_MAX` (SoH already raised 19→128), `ObjectContext.num` u8 | 128 | dynamic |
| Object space | fixed ~1 MB arena in `z_scene.c` | 1 024 000 B | per-object heap allocation |
| Actor IDs | `ActorDB` | **already dynamic** | reuse as the registry model |
| Live actors | `ACTOR_NUMBER_MAX` | 2 000 | raise |
| Scenes | `gSceneTable` static macro table, `SCENE_ID_MAX` 110; save flags indexed by scene | 110 | JSON registry + keyed save flags |
| Entrances | `gEntranceTable` static, `ENTR_MAX` 1 556 | 1 556 | JSON registry |
| Text | one binary blob per language; `override/text/` merge can replace but not add | — | map-based table, additive merge |
| Mesh entries / sorted entries | `u8` count, `SHAPE_SORT_MAX` 64 | 255 / 64 | widen |
| Texture cache | 1 024 | 1 024 | raise |

What is **not** a limit: the archive format. SoH already registers XML factories for scene
commands, collision, text, paths, skeletons, DLs, vertices and audio; scene headers already
reference sub-resources by name string. The pain is naming (offsets), override granularity
(whole file), and the C-side caps above.

## The Unbound archive layout

Final names are settled per detail doc; this is the shape.

```
unbound.json                       # manifest: format version, game, source ROM family, features used
scenes/<scene>/scene.json          # scene setups (all alternate headers as an array), exits,
                                   # entrances, lighting, objects, room list, collision ref
scenes/<scene>/rooms/<n>.json      # room header: mesh refs, actors, objects, flags
scenes/<scene>/collision.json      # bounds, surface types, water boxes, camera data
scenes/<scene>/collision.bin       # vertex + poly arrays, u32 indices, counts from the header
scenes/<scene>/rooms/<n>/…         # DLs, vertices, textures — stable Torch names
text/<lang>/messages.json          # id → { box, ypos, text }
tables/scenes.json                 # scene registry (replaces gSceneTable)
tables/entrances.json              # entrance registry (replaces gEntranceTable)
tables/objects.json                # object registry (replaces gObjectTable)
objects/<name>/…
```

### Merge semantics

Today the archive manager keeps a flat `CRC64(path) → archive` map and the last archive added
wins per path (`libultraship/src/ship/resource/archive/ArchiveManager.cpp`, `AddArchive`).
Unbound keeps that for bulk resources and adds a **merging loader** for structured ones:

- `*.json` structured resources are loaded from **every** archive that has the path, lowest
  layer first, and deep-merged: objects merge by key, arrays of entities merge by `id`, a
  `null` value deletes, later layers win.
- Bulk resources (`*.bin`, DLs, textures, vertices) keep last-wins whole-file replacement.
- Scenes/rooms/registries/text are all structured, so a mod is by construction a patch.

This subsumes the `override/text/` mechanism and means Prelude never has to bundle a whole
scene because a header pointed at a ROM-specific name.

### Identity

Scenes, entrances and objects get **string IDs** (`kokiri_forest`, `mymod/lava_temple`).
Numeric IDs are assigned at load time, as `ActorDB` already does for actors. Save data keys
per-scene flags by string ID in the JSON save.

## Build

`SOH_UNBOUND` is a CMake option that defines the macro for `soh/` and `libultraship/`. It is
**on** for Unbound builds. Code that changes struct layout or allocation lives inside
`#ifdef SOH_UNBOUND` where a vanilla path must be preserved, otherwise it is simply changed
with a `// SOH [Unbound]` marker — the fork is not trying to stay diff-minimal against upstream.

## Order of work

1. **Collision** — the most-reported Prelude limit; self-contained; no format decision
   required to land it (the loader accepts today's binary/XML with widened in-memory types).
2. **Text** — trivial lift, immediate modding win.
3. **Scene/room** — new layout + merging loader; widen room/actor/object counts.
4. **Registries** — scenes, entrances, objects; save-flag keying.
5. **Mesh/texture caps.**
6. **Converter** — vanilla `oot.o2r` → `oot-unbound.o2r`, legacy mod → Unbound mod.

## Known risks

- Enhancements that use `ENTR_*`/`SCENE_*` enums as array indices (mostly randomizer) break at
  step 4. Accepted.
- `ResourceMgr_PatchGfxByName` refuses `IsCustom` resources and XML resources are always
  custom (`ResourceManagerHelpers.cpp`). Gfx-patch-based enhancements silently stop applying to
  text-format DLs. Decide at step 3.
- MQ scene selection is a `/nonmq/`→`/mq/` path string replace
  (`ResourceManagerHelpers.cpp`, `ResourceMgr_GetResourceByNameHandlingMQ`). The new layout
  must keep an equivalent.
- Sail/Anchor sync actor and scene IDs numerically; custom scenes need the string IDs on the
  wire.
