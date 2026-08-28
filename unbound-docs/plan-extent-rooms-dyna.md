# Plan: rooms > 127, dyna > 50, world extent > ±32 767

Working plan for the next three lifts from the README's "Known remaining limits". Written from a
code survey on 2026-08-27; file:line references are as of commit `10ed93393`.

**Status (end of 2026-08-27):** all three implemented — rooms Phases A–C, dyna, extent Phases 0–3
(collider shapes done by making `Sphere16`/`Cylinder16` f32 in place rather than a type swap).
Builds clean; title-screen smoke runs clean against both the old v1 archive and a freshly exported
v2 archive. **Not yet done:** the in-game verification lists below (visual parity after the float
`Mtx` change is the important one), and `CamData` positions stay `s16` (README limits). The
authoritative descriptions now live in `counts.md` (rooms), `collision.md` (dyna) and `extent.md`;
this file is the checklist.

**Recommended order: rooms → dyna → extent.** Rooms and dyna are each well under a day and
self-contained. Extent is the large one (it reaches into rendering) and its first step is a design
decision, not code; the survey found it is really *three* caps wearing one number.

---

## 1. Rooms (≤ 127 → 32 767)

### What the survey found

The `s8` is not the first wall. Room-keyed *state* breaks at **32**:

| Cap | Where | Effect past it |
|---|---|---|
| **32** | `ActorContext.flags.clear/tempClear` are `u32`, `Flags_SetClear(play, room)` does `1 << room` (`z_actor.c:760-807`); persisted as `SavedSceneFlags.clear` (`z64save.h:112`) | UB on the first boss/chest/shutter in room ≥ 32 |
| **32** | Minimap visited-rooms `sceneFlags[].rooms \|= gBitFlags[room]` (`z_map_exp.c:87,493`, `gBitFlags[32]`) | OOB read |
| **63** | Waterbox room is a 6-bit field in `properties`, `0x3F` = all rooms (`z64bgcheck.h:35`, `z_bgcheck.c:4344`) | water in rooms ≥ 63 never applies |
| **64** | Transition actors: index packed as `(i << 10)` into `s16 params` (`z_actor.c:3467`), `numActors` u8 (`z64.h:1416`) | doors past 64 alias |
| 127 | `Room.num`, `Actor.room`, `TransitionActorEntry.sides[].room` s8 | the documented limit |
| 255 | `EntranceEntry.room` u8, `RespawnData.roomIndex` u8 | |

### Phase A — the s8 → s16 sweep (mechanical, ~2 h)

Widen, in lockstep, game struct + C++ resource mirror:

- `Room.num` `z64.h:1075`; `Actor.room` `z64actor.h:215`;
  `TransitionActorEntry.sides[].room` `z64.h:1244` **and** `resource/type/scenecommand/SetTransitionActorList.h:15`;
  `EntranceEntry.room` (u8→s16) `z64.h:1255` **and** `SetEntranceList.h:14`.
- Save-side (JSON save, name-keyed, so source-compatible): `RespawnData.roomIndex` `z64save.h:138`,
  `SohStats.roomNum` `:91`, `SceneTimestamp.room` `:62` (keep the `254` sentinel),
  `BetterSceneSelectGrottoData.roomIndex` `:1368`. **Do not touch** the frozen legacy structs at
  `SaveManager.cpp:2490,2515` — they describe on-disk layouts.
- Port layer: `WarpPoint.roomNum` `Warping.cpp:23`; `GrottoLoadInfo.room` `randomizer_grotto.h:20`;
  `EnemyRandomizer.cpp:162,318` `int8_t roomNum` params; `hook_handlers.cpp:1996-1997` `s8` locals;
  `valueViewer.cpp:32` **`TYPE_S8` → `TYPE_S16`** (reads through `void*`, silently wrong otherwise);
  Anchor `TeleportTo.cpp:29,41,48` `get<s8>()` (wire format — both peers must run the same build).
- Narrow locals: `z_play.c:2133` `s8 roomIndex`; `z_actor.c:3455` `u8 numActors`;
  `z_en_ru1.h:32-34` + nine `s8` locals in `z_en_ru1.c`; `z_bg_relay_objects.h:15`;
  `z_door_shutter.c:192,555`; `z_bg_mori_idomizu.c:110`.
- The other ~60 overlays only assign `-1` or compare — no edits.

**Sentinel trap (the one non-mechanical bit).** `-1` on `Actor.room` = persistent, on `sides[].room`
= no room. Binary loaders currently get `-1` *by accident* of truncating `0xFF` into an s8:

- `SetTransitionActorListFactory.cpp:20,22` `ReadUByte()` → add `raw == 0xFF ? -1 : raw`.
- `SetEntranceListFactory.cpp:20` `ReadInt8()` into a u8 — same fix, opposite direction.
- `UnboundSceneFactory.cpp:426,428` `(s8)` casts → `(s16)`. The exporter (`UnboundExporter.cpp:565`)
  already emits `-1`, so converted archives are forward-compatible.

### Phase B — room-keyed flags become unbounded (~3 h)

- **Clear flags.** Vanilla scenes keep `SavedSceneFlags.clear` u32 positional (save compat).
  `Flags_*Clear` route through a helper: `room < 32` → existing bit; else a growable bitset held by
  the by-name custom-scene flag store that `registries.md` already introduced. Live copy in
  `ActorContext.flags` gets the same shape. Save states already don't capture custom flags (README);
  unchanged.
- **Minimap visited bits.** Guard `room < 32` in `z_map_exp.c:87,493` (minimap for custom scenes is
  limit #7, separate work). Same guard for `BottleAdventure.cpp:238-358`, which abuses the field.
- **Waterbox room.** Add `s32 room` to the in-memory `WaterBox` (game struct + `CollisionHeader.h`
  mirror), unpacked from `properties` by every loader; `collision.json` waterboxes accept an optional
  explicit `"room"` key (`-1` = all) that overrides the packed bits. `z_bgcheck.c:4344` and
  `colViewer.cpp:683` read the field. Merges key-wise for free.

### Phase C — transition actors > 64 (~1 h, same pass because doors are what reference rooms)

`TransitionActorContext.numActors` u8 → u16. Stop packing the index into `params`: add
`s16 transitionIndex` to `Actor` (set in `Actor_SpawnTransitionActors` after spawn) and a
`TRANSITION_ACTOR_INDEX(actor)` macro replacing `(u16)params >> 0xA` at `z_player.c:5474`,
`z_play.c:2196`, `z_door_shutter.c:562`, `z_en_holl.c` ×7, `z_en_door.c`. Keep writing the packed
form into `params` too, so any site we miss still works for the first 64.

### Verify

Vanilla: Forest Temple (shutters, clear flags), Water Temple (waterbox rooms), Kakariko (En_Ru1
is Ruto — Jabu-Jabu), Ganon's Castle (En_Holl planes), minimap in Deku Tree. Custom: a Prelude scene
with 40 rooms, a chest in room 35 whose clear flag survives save/load, water in room 35.

---

## 2. Dyna collision (50 actors / 16 384 polys+verts → unbounded)

### What the survey found

- `BgActor bgActors[BG_ACTOR_MAX]` + `u16 bgActorFlags[BG_ACTOR_MAX]` are **by-value inside
  `PlayState`** (`z64bgcheck.h:152-153` → `:171` → `z64.h:1439`), which is why 50 is compile-time.
- `BGCHECK_SCENE == BG_ACTOR_MAX` is the "static scene" sentinel, *and* `DynaPoly_SetBgActor`
  returns `BG_ACTOR_MAX` as its failure value (`z_bgcheck.c:2737`), tested `== BG_ACTOR_MAX` by
  ~60 actors. Three meanings, one number.
- `Actor.floorBgId/wallBgId` are **u8** (`z64actor.h:232-233`), assigned from s32 — silent
  truncation past 254. `Camera.bgCheckId/nextBGCheckId` s16 (`z64camera.h:1201,1210`).
- Poly/vtx lists are already `malloc`'d (`z_bgcheck.c:2650,2665`) but fixed at 16 384
  (`:1523-1525`) with fatal asserts (`:2883-2884`) because `CollisionPoly*` pointers escape into
  `Actor.floorPoly/wallPoly`, `Camera.atEyePoly`, `CamColChk.poly` and seven overlay caches
  (ObjOshihiki, EnDekubaba, EnKarebaba, MirRay, EnSw, EnNwc). Those pointers are *already*
  logically stale every frame (`DynaPoly_Setup` rebuilds the list from 0); only buffer identity is
  relied on. `z_bgcheck.c:3234-3237` does pointer subtraction — the buffer must stay contiguous.
- Every dyna query loops `0..BG_ACTOR_MAX` (12 loops in `z_bgcheck.c`, several per-actor per-frame).
  Raising the cap naively makes collision O(cap) — the real cost of this lift.
- `z_en_item00.c:806` hardcodes `50`. `colViewer.cpp:484` loops the macro.
- **Live UB bug:** `z_bgcheck.c:2894-2903` still passes `s16 polyIndex` to
  `DynaSSNodeList_SetSSListHead(…, s32*)` — the earlier widening missed the "transform unchanged"
  branch. Reads 4 bytes from a 2-byte local and re-caps dyna poly ids at 32 767. Fix first.

### Design

1. **Sentinels.** `BGCHECK_SCENE` becomes a fixed constant (`0x7FFFFFFF`), decoupled from capacity.
   Add `BGACTOR_INVALID` (= `BGCHECK_SCENE`, the value actors already test), sed the ~60
   `== BG_ACTOR_MAX` / `!= BG_ACTOR_MAX` actor sites to it, then **delete `BG_ACTOR_MAX`** so every straggler is a compile error. Guards at
   `z_bgcheck.c:283-287, 1701-1707` use `DynaPoly_IsBgIdBgActor`.
2. **Widen ids.** `Actor.floorBgId/wallBgId` → s32; `Camera.bgCheckId/nextBGCheckId` and the
   `prevBgId` local (`z_camera.c:7252`) → s32.
3. **Slots off `PlayState`, growable.** `BgActor* bgActors; u16* bgActorFlags; s32 bgActorMax;
   s32* activeIds; s32 activeCount;` — start at 64, `realloc` ×2 in `DynaPoly_SetBgActor` when
   the free-slot scan fails (nothing holds a `BgActor*` across a spawn; `colViewer.cpp:486` is
   loop-local). `activeIds` is a dense list maintained on set/delete; **all twelve query loops
   iterate it**, so a scene with 30 dyna actors costs what it costs today. Free in `BgCheck_Free`.
4. **Poly/vtx lists grow at the one safe point, retire old buffers.** Pre-pass at the top of
   `DynaPoly_Setup` sums `numPolygons/numVertices` over active slots; if it exceeds capacity,
   allocate ×2 and push the old buffer onto a retire list freed in `BgCheck_Free`. Stale
   `CollisionPoly*` in actors keep pointing at readable memory with exactly today's
   "stale-but-harmless" semantics — no invalidation sweep, no overlay edits. Retired memory is
   bounded by the final size (geometric growth). Delete the asserts at `2883-2884`; the
   `UNBOUND_DYNA_*` constants become initial sizes.
5. Node list is already growable and shares the 32-bit `SSNode` — nothing to do.

### Verify

Vanilla: Fire Temple (many Bg_Hidan dyna), Ganon's Tower collapse, Water Temple, Zora's Domain
(Ruto/Jabu belly), Gerudo Fortress (Bg_Spot15_Rrbox boxes). Custom: a Prelude scene with 200 dyna
platforms; collision viewer shows all; frame time flat vs the same scene with 30.

---

## 3. World extent (±32 767 → ~±1 000 000)

### What the survey found — three separate caps

**(i) Data & collision (s16 everywhere).** Collision `vtxList Vec3s`, `min/maxBounds`,
`CollisionPoly.dist` (**plane distance from the world origin** — widening vertices alone buys
nothing), `WaterBox` origin+length, `CamData.camPosData` (`Vec3s[3]` triples packed by the
`BGCAM_*` macros, `z_camera.c:24-27`). Entities: `ActorEntry.pos`, `TransitionActorEntry.pos`,
`Path.points`, `PolygonDlist2.pos` (mesh-type-2 cull centre), `LightPoint.x/y/z`,
`CutsceneCameraPoint.pos`, `HorseData.pos`. Runtime `Actor.world.pos`, camera `at/eye`, culling,
audio, quake, effects (`EffectSs`) are **already float** — the narrowing happens at spawn and at a
handful of engine choke points.

**(ii) Engine choke points.** `BGCHECK_XYZ_ABSMAX` (log-only), `BGCHECK_Y_MIN = -32000` (**a
sentinel**, "no floor", ~50 `==`/`<=` comparisons across engine + 25 overlays — a world below
y −32000 reads as bottomless), `BgCheck_Vec3fToVec3s` and the dyna world-space vertex bake
(`z_bgcheck.c:2921-2925`) with `Sphere16 boundingSphere`, `Cylinder16 ColliderCylinder.dim` (160
overlays via the single `Collider_UpdateCylinder` + 39 direct), `Sphere16 worldSphere` (28
overlays, no choke point), `ColliderQuad` mid-points, minimap `s16 tempX/tempZ` (`z_map_exp.c:608`).

**(iii) Rendering — the real gate.** `Vtx.ob` is `short` and **room DLs draw under the identity
modelview** (`z_room.c:60,66`, `gMtxClear`), so scene mesh vertices are absolute world coordinates.
And `Mtx` is s16.16: `guMtxF2L` (`gu_pc.c:4-18`) wraps any translation ≥ 32 768 — for actors and
the view matrix too, so even a rebased room can't be *placed* far away. The dormant `GBI_FLOATS`
path in libultraship (`gbi.h:1016`, `interpreter.cpp:1146-1157`, `MatrixFactory.cpp:18`) flips
both `Vtx` and `Mtx` to float under one `#ifdef`; SoH's interpolation replacement path
(`interpreter.cpp:1141`) also quantises through an `int` cast.

### Decision to make first: rendering

| | A. Full `GBI_FLOATS` (float `Vtx` + `Mtx`) | **B. Float `Mtx` only + room origins (recommended)** |
|---|---|---|
| Geometry beyond ±32 767 | native | per room: mesh vertices are relative to a room `origin`; `Room_Draw` loads a translate matrix instead of `gMtxClear` |
| Blast radius | every `Vtx` producer: `VertexFactory` (binary s16), all static `Vtx` arrays in `soh/`, OTRExporter, raw `gSPVertex` users (wind wisps), `Vtx` size assumptions | `Mtx` producers only: `guMtxF2L/L2F`, `Matrix_MtxFToMtx`, the four hand-packers `sys_matrix.c:933-1045`, `gMtxClear`, `MatrixFactory` (must *unpack* stored fixed-point, not `ReadFloat`), interpolation quantise |
| Format | `Vtx` resources change | `rooms/<n>.json` gains optional `"origin": [x,y,z]` (default 0); converter emits 0; Prelude rebases |
| Precision | f32 everywhere | f32 model/view; vertices stay exact integers per room |

B is a libultraship-fork change (`GBI_FLOAT_MTX`, split out of `GBI_FLOATS`) plus ~6 SoH matrix
functions plus one line in `z_room.c`. Everything actor-shaped is model-local `Vtx` under a float
model matrix, so actors are covered for free. Practical limit becomes f32 precision: ~0.06 units
at 10⁶, so set `BGCHECK_XYZ_ABSMAX` to 2²⁰ (1 048 576) and document that as the Unbound world.

### Phases

0. **Rendering (B).** LUS fork: `GBI_FLOAT_MTX`; `Mtx` becomes `union { s32 m[4][4]; f32 mf[4][4]; }`
   with the interpreter's `memcpy` branch; `MatrixFactory` unpacks s16.16 → float; drop the
   `int` quantise in the replacement path. SoH: the matrix functions above write floats; remove the
   `Matrix_CheckFloats` range warning. Room origin: `"origin"` in room JSON → `Room` field →
   translate `Mtx` in `Room_Draw` (both `flags & 1` and `& 2` branches). Verify vanilla first — this
   phase alone must be pixel-identical.
1. **Data plumbing** (no engine logic). Entities → `Vec3f`: `ActorEntry.pos`, `TransitionActorEntry.pos`,
   `PolygonDlist2.pos`, `Path.points`, `LightPoint` (+ `Lights_Point*SetInfo` signatures, 23
   overlays + `z_kankyo.c`), `CutsceneCameraPoint.pos`, `HorseData.pos`. Each has four
   producers: Unbound JSON factory, binary factory, XML factory, exporter. Collision → s32:
   `vtxList` (`Vec3i`), `min/maxBounds`, `dist`, `WaterBox` fields. **`collision.bin` v2**: vertices
   `s32 ×3` (12 B), polys `{u16 type, u16 pad, u32 vA,vB,vC, s16 nx,ny,nz, s16 pad, s32 dist}`
   (28 B); `"$schema": "unbound/collision/2"`, loader keeps reading v1. `CamData` restructured to
   `{ Vec3f pos; Vec3s rot; s16 fov; s16 jfifId; }` with `cameraPositions` in JSON becoming
   `{ "pos", "rot", "fov", "jfif" }` objects — last, and can slip: fixed-camera zones far from
   origin are rare.
2. **Engine choke points.** `BGCHECK_Y_MIN` → `-2147483648.0f` (exact in f32; every comparison
   uses the macro — grep for literal `-32000` first); `BGCHECK_XYZ_ABSMAX` → 2²⁰; grid copies
   bounds from the widened header (`z_bgcheck.c:1654-1659`, math already float);
   `CollisionPoly_GetMinY` returns f32; dyna `vtxList` → `Vec3f`, `BgCheck_Vec3fToVec3s` gone,
   `boundingSphere` → `Spheref`; `code_800430A0.c:28`; minimap `tempX/tempZ` → f32.
3. **Colliders.** `Cylinder16 → Cylinderf` in `ColliderCylinder.dim` inside `Collider_UpdateCylinder`
   (init structs stay `Cylinder16`, the copy converts), then the 39 direct-writer overlays;
   `Sphere16 worldSphere → Spheref` across 28 overlays; `ColliderQuad.dcMid/baMid → Vec3f`.
   `ColliderInfo.bumper.hitPos` and the `EffectSpark/Blure/ShieldParticle` `Vec3s` are cosmetic —
   leave, document.
4. **Verify.** Vanilla parity after every phase (Hyrule Field, Kakariko, Forest Temple, Jabu conveyor,
   Water Temple, Ganon collapse, Epona save/load, cutscene cameras in Kokiri). Custom: a Prelude
   scene with rooms at x = 200 000 and a walkable floor at y = −60 000; spawn, doors, water, Deku
   Baba on a moving platform, Epona ride + save; minimap dot; camera data in a far room.

### Effort

Phase 0 ≈ 1 day (mostly verification); 1 ≈ 1 day; 2 ≈ ½ day; 3 ≈ 1 day (overlay sweep). Phases 1–3
can be split across sessions because each leaves vanilla working.
