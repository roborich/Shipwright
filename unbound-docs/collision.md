# Unbound: collision geometry

Removes every fixed cap on scene (static) collision and relaxes the actor (dyna) collision
caps, so a Prelude-built scene can carry arbitrarily large collision. Overview and rationale
in [`README.md`](./README.md).

## The caps, and why they exist

All in `soh/include/z64bgcheck.h` and `soh/src/code/z_bgcheck.c`:

| Cap | Cause |
|---|---|
| 8 191 vertices | `CollisionPoly` packs a 13-bit vertex index and 3 xpFlags bits into each `u16` (`COLPOLY_VTX_INDEX`, `COLPOLY_VIA_FLAG_TEST`); bit 13 of `flags_vIB` is the conveyor flag. |
| 32 767 polys | The spatial lookup is a linked list of `SSNode { s16 polyId; u16 next; }`; `SS_NULL = 0xFFFF` also caps the node table at 65 534 entries. `CollisionHeader.numVertices/numPolygons` are `u16`. |
| Node table | `BgCheck_Allocate` derives the node count from an N64 byte budget (`0x1CC00`, or one of eight hardcoded per-scene values, or `0xF000` for "spot" scenes), doubled by an existing SoH workaround. Overflow is `LOG_HUNGUP_THREAD` / `assert`. |
| Arena | Everything is carved from the play-state bump arena (`THA_AllocEndAlign`, ~3.8 MB total for the whole gamestate, `z_play.c` `GameState_Realloc`). |
| Dyna | `polyListMax`/`vtxListMax` 512 (×2 SoH), `polyNodesMax` 1 000; exceeding them is a fatal assert. |

## What changes

### Structs (`z64bgcheck.h`, mirrored in `soh/soh/resource/type/CollisionHeader.h`)

`CollisionPoly` keeps its field names so the ~130 uses in `z_bgcheck.c` stay readable, but
widens the packed words:

```c
typedef struct {
    u16 type;
    union {
        u32 vtxData[3];
        struct {
            u32 flags_vIA; // bits 29-31 xpFlags, bits 0-28 vertex index
            u32 flags_vIB; // bit 29 conveyor,   bits 0-28 vertex index
            u32 vIC;
        };
    };
    Vec3s normal;
    f32 dist; // world extent: was s16
} CollisionPoly;

#define COLPOLY_VTX_INDEX(vI)            ((vI) & 0x1FFFFFFFu)
#define COLPOLY_VIA_FLAG_TEST(vIA, f)    ((vIA) & (((f) & 7) << 29))
#define COLPOLY_VIA_FLAGS_MASK           0xE0000000u
#define COLPOLY_VIB_CONVEYOR             (1u << 29)
```

Vertices, bounds, `dist` and water-box extents are `f32` (`BGCHECK_XYZ_ABSMAX` = 2²⁰); see
[`extent.md`](./extent.md).

`SurfaceType` and `WaterBox` are stored **unpacked**: the two vanilla `data[2]` words become named
fields (`camera`, `exit`, `lightSetting` as `s32`; `floorType`, `wallFlags`, `wallType`,
`floorProperty`, `isSoft`, `isHorseBlocked`, `material`, `floorEffect`, `echo`, `canHookshot`,
`conveyorSpeed`, `conveyorDirection`, `isWallDamage` as `u8`), and the water-box `properties` word
becomes `camera`, `lightSetting`, `room` (`-1` = all rooms) and `notSwimmable`. The `SurfaceType_Get*`
accessors in `z_bgcheck.c` read fields through one `SurfaceType_Get()` (which hands back an all-zero
entry when a poly has none), so their ~200 callers are unchanged. This lifts the per-scene caps the
packing imposed — 255 cameras, 31 exits, 31 light settings, 63 water-box rooms. Legacy data is
converted once at the loader boundary by `SurfaceType_Unpack(data0, data1)` and
`WaterBox_UnpackProperties()` (prototypes in `z64bgcheck.h`; `SOH::UnpackSurfaceType` /
`SOH::UnpackWaterBoxProperties` wrap them for the C++ mirrors); nothing writes the packed form.

`SSNode` becomes `{ s32 polyId; u32 next; }`, `SS_NULL` becomes `0xFFFFFFFF`, and every
`SSList.head`, `SSNodeList.max/count`, `DynaLookup.polyStartIndex`, `BgActor.vtxStartIndex`,
`CollisionHeader.numVertices/numPolygons` and the corresponding locals/params in `z_bgcheck.c`
widen to 32 bits. `StaticLookup` grows from 6 to 12 bytes; the lookup table is tiny either way.

The in-memory struct is the **only** contract: the vanilla struct layout was never exposed to
mods because every collision header is materialised by the SoH importer.

### Loader (`CollisionHeaderFactory.cpp`)

- Binary v0 (today's `oot.o2r`): reads the `u16` packed words and **unpacks** them —
  `index = v & 0x1FFF`, xpFlags `(v >> 13) << 29`, conveyor bit 13 → bit 29. Vanilla data and
  every existing mod keep loading.
- XML: `VertexA/B/C` are plain indices. If the element carries an `XpFlags` attribute (0-7)
  and/or `Conveyor` (0/1) they are applied; if it carries neither, the attribute values are
  treated as legacy packed `u16` words and unpacked as above. This is the Unbound authoring
  form for Prelude until the `collision.json` + `collision.bin` split lands.

### Allocation (`BgCheck_Allocate`)

The N64 byte budget goes away: `BgCheck_Allocate` no longer computes `memSize` (the per-scene
table, the "spot"/"mini" sizes and `CollisionContext.memSize` are deleted); the only thing still
chosen per scene is the vanilla lookup-grid shape (`BgCheck_SetVanillaSubdivisions`). Every table
grows through one helper, `BgCheck_ReallocTable` (realloc into a temp, double, fatal on heap
exhaustion like the arena was).

- Static node table: **growable**. Allocated at `max(2 × numPolygons, 4096)` nodes and doubled
  on demand (`SSNodeList_Grow`). Nodes are addressed by index, so `realloc` is safe as long as
  no caller holds an `SSNode*` across an insert — `StaticLookup_AddPolyToSSList` was rewritten
  to re-derive its cursor from an index for exactly that reason.
- `polyCheckTbl` (one byte per static poly), the node table, the static lookup grid, and the
  dyna poly/vertex/node lists are `malloc`'d and released by a new `BgCheck_Free`, called from
  `Play_Destroy`. They no longer touch the play-state arena, so a 2 M-poly scene does not
  starve actors of memory.
- Subdivision: vanilla amounts (`16×4×16`, or the per-scene overrides) are kept for parity
  when the header has ≤ 16 384 polys; above that the grid scales with the cube root of the
  poly count (capped at 64 per axis) so lookup cost stays bounded on huge scenes.
- Dyna: `polyListMax`/`vtxListMax` start at 16 384 each and **grow** in `DynaPoly_Setup`
  (`DynaPoly_EnsureListCapacity`): a pre-pass sums every live bg actor's polys/verts before any
  `DynaPoly_ExpandSRT` runs, and if the lists are too small they are reallocated (×2) and the
  lookup is invalidated so the "transform unchanged" fast path cannot re-link polys in the old
  buffer. Actors keep `CollisionPoly*` into these lists (`Actor.floorPoly/wallPoly`, camera,
  a handful of overlay caches) — those pointers are already logically stale every frame, but
  they must stay *readable*, so superseded buffers are parked on `dyna.retiredBuffers` (a fixed
  array of 32 — growth doubles, so a list retires one buffer per doubling) and freed with
  everything else in `BgCheck_Free`. Geometric growth bounds the parked memory to the final
  size. The dyna node list grows the same way (nodes are addressed by index).
- Dyna actors: `BG_ACTOR_MAX` (50) is gone. `bgActors`/`bgActorFlags` are heap tables of
  `dyna.bgActorMax` slots (initially 64), doubled by `DynaPoly_SetBgActor` when the free-slot
  scan fails. `BGCHECK_SCENE` is a fixed sentinel (`0x7FFF`) instead of the table size, and
  `BGACTOR_INVALID` (same value) is the "could not allocate" return the ~60 overlay
  `== BG_ACTOR_MAX` tests now name. `Actor.floorBgId/wallBgId` (were **u8** — silently
  truncating past 254) and `Camera.bgCheckId/nextBGCheckId` (s16) are `s32`. Query loops run
  over `bgActorMax`, which doubling keeps within 2× the live count. `DynaPoly_IsBgIdBgActor` is
  a pure range check (`0 ≤ bgId < BGCHECK_SCENE`, no context); the `z_bgcheck.c` sites that
  index the table also check the context's `bgActorMax` (`DynaPoly_IsBgIdInTable`). Also fixed: the
  "transform unchanged" branch of `DynaPoly_ExpandSRT` still passed an `s16` to the widened
  `s32*` `DynaSSNodeList_SetSSListHead` (4-byte read of a 2-byte local).

Memory impact on vanilla scenes: negligible (a few hundred KB moved from the arena to the heap).

## Not changed

- Surface types (`u16 type` → 65 535 per header), water boxes (`u16`) — already ample.
- Camera positions (`CamData.camPosData`) stay `Vec3s`; see the README's remaining limits.

## Consumers touched

- `soh/src/code/z_bgcheck.c` — all of the above.
- `soh/src/code/z_play.c` — `BgCheck_Free` in `Play_Destroy`.
- `soh/soh/resource/type/CollisionHeader.h`, `importer/CollisionHeaderFactory.cpp` — struct
  mirror + unpacking.
- `soh/soh/Enhancements/debugger/colViewer.cpp` — reads `numPolygons` / vertex indices via the
  macros; no logic change.

## Status

Implemented on the `unbound` branch. See the README status table for build/verification state.

## Verification

1. Build (RelWithDebInfo in `build-cmake`).
2. Vanilla parity: load Hyrule Field, Kakariko, Forest Temple, Shadow Temple, Ganon's Tower
   collapse — walk, hookshot, ladder, conveyor (Jabu-Jabu / Water Temple), water surfaces,
   dyna platforms. Collision viewer overlay should match pre-change.
3. Stress: a Prelude-exported scene with > 8 191 vertices and > 32 767 polys loads and is
   walkable; scene transition back and forth shows no heap growth (`BgCheck_Free` works).
