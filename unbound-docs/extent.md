# Unbound: world extent

Lifts the ±32 767-unit world limit that `s16` positions imposed everywhere. What a modder may
write (number-typed positions, room `origin`, `collision.bin` v2, the world-unit fog keys) and
the resulting limits are in [`SPEC.md`](./SPEC.md) §2, §4.2–4.4 and §9; this file is the engine
side. Overview in [`README.md`](./README.md).

## Three caps behind one number

| Cap | Where | Lift |
|---|---|---|
| **Rendering: `Mtx` is s16.16** | `guMtxF2L` (`gu_pc.c`) packs every model/view matrix; a translation ≥ 32 768 wraps. This caps *actors and the camera*, not just scenes. | `Mtx` is float (`GBI_FLOAT_MTX`, libultraship fork `fast/types.h`). |
| **Rendering: room `Vtx` is `short` under the identity matrix** | `z_room.c` draws room meshes with `gMtxClear`, so mesh vertices *are* world coordinates. | `Vtx.ob` is `s32` (`GBI_S32_VTX`, libultraship fork `fast/lus_gbi.h`), so a room mesh reaches the whole world on its own. Per-room **origin** (SPEC §4.3) predates this and is still honoured. |
| **Data & engine: `s16` positions** | Collision vertices, `CollisionPoly.dist` (plane distance from the *world origin*), bounds, water boxes, spawn entries, transition actors, paths, point lights, mesh-type-2 cull centres, Epona's saved position, `BGCHECK_Y_MIN`/`BGCHECK_XYZ_ABSMAX`, dyna world-space vertex bake, `Sphere16`/`Cylinder16` colliders. | Widened to `f32`/`s32` (below). |

### Float matrices

- `libultraship/include/fast/types.h`: `GBI_FLOAT_MTX` (a LUS CMake option, off by default; SoH's
  root `CMakeLists.txt` turns it on and `sys_matrix.c` `#error`s without it) makes `Mtx` an alias
  of `MtxF`. Vertices are widened separately by `GBI_S32_VTX` (below); `GBI_FLOATS` would make
  both float at once, which is not what this fork does.
- `interpreter.cpp` `GfxSpMatrix`: the fixed-point unpack is skipped and the matrix is `memcpy`'d;
  the frame-interpolation replacement path no longer quantises through an `int` (which overflowed
  past ±32 767 too).
- `MatrixFactory.cpp`: `.o2r` matrix resources are stored s16.16; they are unpacked to float at
  load (SPEC §8).
- SoH: `guMtxF2L`/`guMtxL2F` are copies; `gMtxClear` is a float identity; the two hand-packed
  writers in `sys_matrix.c` (`Matrix_SetTranslateUniformScaleMtx2`, `Matrix_SetTranslateScaleMtx1`)
  build an `MtxF` instead. Nothing else that is compiled in `soh/` touched `Mtx.m[]` directly
  (`ucode_disas.c` still reads `intPart`/`fracPart` but is not built).

Precision: f32 has ~7 significant digits, so at 10⁶ units positions resolve to ~0.06 units. The
Unbound world is therefore ±2²⁰ (1 048 576) — `BGCHECK_XYZ_ABSMAX`.

### s32 vertices

- `GBI_S32_VTX` (a LUS CMake option, off by default; SoH's root `CMakeLists.txt` turns it on)
  makes `Vtx.ob` `int32_t`. It is independent of `GBI_FLOAT_MTX`, and sits alongside `GBI_FLOATS`
  rather than replacing it.
- **`s32`, not `f32`.** Both are 12 bytes, so both hit the same `sizeof(Vtx)` change and the same
  bugs; `s32` additionally keeps vertices integral, which is what the ~692 `.ob[` sites in the game
  assume when they read one back (integer division stays integer, an `f32` assigned to a vertex
  truncates as it did to `s16`), and lets an editor snap exactly. A float build needs a hand-written
  cast in `z_en_jsjutan.c`; an `s32` build needs none.
- **`sizeof(Vtx)` goes 16 → 24, and that is the whole difficulty.** "The size of a vertex" was two
  concepts that had always been one number: the runtime struct, and the 16-byte record in an
  archive that exported display lists carry **byte offsets** into. The archive's size is now
  `OTR_EXPORTED_VTX_SIZE` in `fast/lus_gbi.h`. Three sites conflated them — all silent, all wrong
  only at a non-zero offset, so geometry at offset 0 looked fine and the rest was noise:
  - `gfx_vtx_hash_handler_custom` advanced with `(char*)vtx + offset`. The exporter rewrites every
    `G_VTX` to `G_VTX_OTR_HASH` (`DisplayListExporter.cpp`), so this reached all static geometry.
  - `SegAddr` resolves a segmented address in bytes. Skinned limbs point segment 8 at a per-frame
    vertex buffer (`z_skin.c`), as do Ganon's cape, the Jsjutan carpet and the `z_fbdemo` wipes.
    Vertex commands resolve through **`SegAddrVtx`**, which converts the byte offset to an element
    index; identical to `SegAddr` when the two sizes agree.
  - `gfx_vtx_handler_f3d` divided the packed byte length by `sizeof(F3DVtx)`.
- **Archives are unchanged and need no re-export.** They still hold 16-byte s16 vertices;
  `VertexFactory` widens them on load the way `MatrixFactory` unpacks s16.16 matrices, and the OTR
  vertex opcodes carry a vertex *count*, not a byte length. ZAPD writes through its own `ZVtx`
  (`GetRawDataSize()` is a literal 16), so the format cannot drift with the runtime struct.
- Memory: every vertex costs 8 more bytes. Every allocation in the game is `n * sizeof(Vtx)`, so
  nothing needed resizing.
- `soh/CMakeLists.txt` deliberately does **not** pass `-Wno-incompatible-pointer-types` for C: it
  is the only diagnostic that catches a widened field still read through its old pointer type, and
  its absence hid a `Cylinder16.pos` misread that broke every cylinder-vs-cylinder hit test in the
  game. Note the hole it cannot cover — `SEGMENTED_TO_VIRTUAL` returns `void*`, so a reader left on
  `Vec3s*` still compiles clean.

### Room origin

The room document's `origin` (SPEC §4.3) is carried on `SOH::SetMesh::origin` → `Room.origin` →
`Room_OriginMtx` in `z_room.c`, used by all three mesh draw paths. The converter emits nothing
(vanilla is 0). Prelude rebases a room whose geometry would leave the s16 range and writes the
origin; collision, actor entries and everything else stay absolute — only the mesh vertices are
relative.

With `s32` vertices a room mesh reaches the whole world unaided, so `origin` is no longer needed
for range. **It cannot simply be removed:** Prelude writes a non-zero origin even for scenes that
fit in `s16` (`lake_hylia_hp` spans 18 280 × 11 794 units and still carries
`origin: [-18236, 508, 4043]`), so every already-exported mod stores its mesh relative to one.
Dropping engine support would misposition all of them. Removing it means a SPEC change and a
re-export, or having Prelude stop emitting it first and retiring the engine path much later.

## Widened data

| Field | Was | Now | Loaders touched |
|---|---|---|---|
| `ActorEntry.pos`, `TransitionActorEntry.pos` (+ SOH mirrors) | `Vec3s` | `Vec3f` | binary, XML, JSON, exporter |
| `PolygonDlist2.pos` (mesh-type-2 cull centre) | `Vec3s` | `Vec3f` | same |
| `Path.points` / `PathData.points` | `Vec3s*` | `Vec3f*` | `PathFactory`, `UnboundPathFactory`, exporter; every overlay reader (`z_path.c`, En_Kz, En_Md, En_Nb, En_Mb, En_Mm, En_Cs, En_Daiku_Kakariko and the `Math_Vec3s_ToVec3f` users) — `SEGMENTED_TO_VIRTUAL` returns `void*`, so a reader left on `Vec3s*` compiles and reads garbage |
| `LightPoint.x/y/z` + `Lights_Point*SetInfo` (and the `SOH::LightPoint` mirror) | `s16` | `f32` | binary, XML, JSON, exporter |
| `HorseData.pos` | `Vec3s` | `Vec3f` | JSON save (name-keyed) |
| `CollisionHeader.vtxList`, `dyna.vtxList` | `Vec3s` | `Vec3f` | `collision.bin` v2, binary, XML |
| `CollisionPoly.dist` | `s16` | `f32` | same |
| `CollisionHeader.minBounds/maxBounds` | `Vec3s` | `Vec3f` | same |
| `WaterBox` origin/lengths | `s16` | `f32` | same |
| `BgActor.boundingSphere` | `Sphere16` | `Spheref` | — |
| `BGCHECK_Y_MIN` | `-32000` | `-2147483648.0f` (exact in f32; still the "no floor" sentinel) | — |
| `BGCHECK_XYZ_ABSMAX` | `32760` | `1048576` | — |
| `ColliderCylinder.dim` | `Cylinder16` | `Cylinderf` | `Collider_UpdateCylinder` + direct writers |
| `ColliderJntSphElement.dim.worldSphere` | `Sphere16` | `Spheref` | per-overlay writers |

`collision.bin` v2 (SPEC §4.4.1) is the on-disk form of the widened collision arrays; the loader
still reads v1 when the schema says so, the converter emits v2.

## Fog and draw distance

A big world is pointless if it fades out at 2 500 units. Vanilla had three coupled caps:

| Cap | Why |
|---|---|
| Fog could not **start** past ~2 500 units | `fogNear` is not a distance. The N64 fog factor is `alpha = 256·(u − fogNear)/(1000 − fogNear)` with `u = 1000·f/(f−n)·(1 − n/d)`, so with `zNear = 10` fog starts at `10·1000/(1000 − fogNear)`; the engine clamps `fogNear ≤ 996` because the packed `s16` multiplier `128000/(1000−fogNear)` overflows above it. Raising `zFar` changes this by a factor `f/(f−n)` ≈ 1 — nothing. |
| Far plane ≤ 12 800 | `Environment_Update` clamps `fogFar` to 12 800 and `Play_Draw` uses `lightCtx.fogFar` as `zFar`. Room chunks (`mesh.type` 2) are culled at the same value. |
| Actors vanish at ~1 350 units | `uncullZoneForward` defaults (1000 + 350) were tuned so the fog hides the pop-in. |

### Lift

- **Format.** The lighting entry's `fogStart` / `fogEnd` / `drawDistance` / `nearPlane` keys and
  their defaults are SPEC §4.2. The JSON loader packs `fogNear` + `fogBlendRate` back into the
  in-memory `EnvLightSettings.fogNear` word (`PackFogNear`), and derives the world-fog defaults
  through `Environment_LegacyFogStart` (`z64environment.h`), the one shared conversion from the
  vanilla near value to a distance.
- **Engine.** `EnvLightSettings` (both mirrors, `static_assert`ed identical) and `LightContext`
  gained the world fields; `Environment_Update` blends them through the same day/night and indoor
  cross-fades (`Environment_LerpWorldFog`, which resolves each side into `WorldFogParams` first
  so a legacy↔world mix never reads unset fields), then sets `lightCtx.zNear/zFar`, which
  `Play_Draw` now feeds to the projection and `z_room.c` uses to cull chunks. Vanilla entries take
  `zNear 10 / zFar = fogFar` exactly as before. `adjFogNear` (Nayru's Love, fairies, game over) is
  honoured in world mode by converting the start distance to the 0..1000 scale and back.
- **GPU.** The fog factor is computed on the CPU in the interpreter (`fog = ndcZ·mul + offset`) and
  only mixed in the shader, so the fix is the two numbers: a new extended op **`G_FOGF`**
  (`OTR_G_FOGF`, libultraship fork) carries them as floats; `Play_SetFog` emits it in world mode
  with `mul = 128000/(u₁−u₀)`, `offset = (500−u₀)·256/(u₁−u₀)` from `u(fogStart)`,
  `u(fogEnd)`. `fog_mul/fog_offset` in the interpreter are floats. Vanilla scenes still go
  through `gSPFogPosition`, bit-identical.
- **Culling.** Room-chunk cull uses `zFar`; the chunk radius (`PolygonDlist2.unk_06`, JSON
  `"radius"`) is `f32`. Actor uncull zones are scaled by `zFar / 12800` in world-fog scenes (on top
  of the "Increase Actor Draw Distance" enhancement) — a stopgap, so a scene with
  `drawDistance: 200000` keeps its actors visible ~15× further.
- Depth precision: `zNear 10` against `zFar 10⁶` is a 10⁵ ratio; the OpenGL backend uses a 24-bit
  depth buffer, so far geometry may z-fight. Set `"nearPlane": 50` (or more) in such scenes.

## Not changed (engine notes behind the SPEC §9 limits)

- **Scene camera data** (`CamData.camPosData`, `BGCAM_*` packing in `z_camera.c`) is still
  `Vec3s`: restructuring the `Vec3s[3]` triple is its own format change.
- **Cutscene camera points** (`CutsceneCameraPoint.pos`) are parsed straight out of the cutscene
  command words, so they stay `s16`.
- Cosmetic `Vec3s` (`ColliderInfo.bumper.hitPos`, `EffectSpark`/`Blure`/`ShieldParticle`) and the
  `EffectSs` ±32 000 cull in `z_effect_soft_sprite.c` (raised to `BGCHECK_XYZ_ABSMAX`).

## Verification

1. Vanilla parity after the float-`Mtx` step alone (must be pixel-identical): Hyrule Field,
   Kakariko, Forest Temple, Jabu-Jabu conveyor, Water Temple, Ganon's Tower collapse, Epona +
   save/load, Kokiri cutscene cameras, pause menu / file select (2D matrices).
2. A Prelude scene with a room at `origin: [200000, 0, 0]` and a walkable floor at y = −60 000:
   spawn there, walk, doors, water box, a Deku Baba on a moving platform, ride Epona and save.
3. Path-following NPCs after the `Vec3f` sweep: Kakariko carpenters, King Zora, Mido, Nabooru,
   Moblins in the Lost Woods.
