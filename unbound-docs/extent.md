# Unbound: world extent

Lifts the ±32 767-unit world limit that `s16` positions imposed everywhere. Overview in
[`README.md`](./README.md).

## Three caps behind one number

| Cap | Where | Lift |
|---|---|---|
| **Rendering: `Mtx` is s16.16** | `guMtxF2L` (`gu_pc.c`) packs every model/view matrix; a translation ≥ 32 768 wraps. This caps *actors and the camera*, not just scenes. | `Mtx` is float (`GBI_FLOAT_MTX`, libultraship fork `fast/types.h`). |
| **Rendering: room `Vtx` is `short` under the identity matrix** | `z_room.c` draws room meshes with `gMtxClear`, so mesh vertices *are* world coordinates. | Per-room **origin** (`rooms/<n>.json` `"origin"`): vertices are authored relative to it, `Room_Draw` loads a translate matrix. Vanilla rooms keep `[0,0,0]` and the identity. |
| **Data & engine: `s16` positions** | Collision vertices, `CollisionPoly.dist` (plane distance from the *world origin*), bounds, water boxes, spawn entries, transition actors, paths, point lights, mesh-type-2 cull centres, Epona's saved position, `BGCHECK_Y_MIN`/`BGCHECK_XYZ_ABSMAX`, dyna world-space vertex bake, `Sphere16`/`Cylinder16` colliders. | Widened to `f32`/`s32` (below). |

### Float matrices

- `libultraship/include/fast/types.h`: `GBI_FLOAT_MTX` (on by default on the fork) makes `Mtx` an
  alias of `MtxF`. `Vtx` stays `short` — that is what `GBI_FLOATS` would also change, and every
  vertex producer (binary `Vtx` resources, static arrays, OTRExporter) would have to follow.
- `interpreter.cpp` `GfxSpMatrix`: the fixed-point unpack is skipped and the matrix is `memcpy`'d;
  the frame-interpolation replacement path no longer quantises through an `int` (which overflowed
  past ±32 767 too).
- `MatrixFactory.cpp`: `.o2r` matrix resources are stored s16.16; they are unpacked to float at load.
- SoH: `guMtxF2L`/`guMtxL2F` are copies; `gMtxClear` is a float identity; the two hand-packed
  writers in `sys_matrix.c` (`Matrix_SetTranslateUniformScaleMtx2`, `Matrix_SetTranslateScaleMtx1`)
  build an `MtxF` instead. Nothing else that is compiled in `soh/` touched `Mtx.m[]` directly (`ucode_disas.c`
  still reads `intPart`/`fracPart` but is not built).

Precision: f32 has ~7 significant digits, so at 10⁶ units positions resolve to ~0.06 units. The
Unbound world is therefore ±2²⁰ (1 048 576) — `BGCHECK_XYZ_ABSMAX`.

### Room origin

`rooms/<n>.json` (top level, next to `setups`): `"origin": [x, y, z]` (numbers, default 0). Carried
on `SOH::SetMesh::origin` → `Room.origin` → `Room_OriginMtx` in `z_room.c`, used by all three mesh
draw paths. The converter emits nothing (vanilla is 0). Prelude rebases a room whose geometry
would leave the s16 range and writes the origin. Collision, actor entries and everything else
stay **absolute** — only the mesh vertices are relative.

## Widened data

| Field | Was | Now | Loaders touched |
|---|---|---|---|
| `ActorEntry.pos`, `TransitionActorEntry.pos` (+ SOH mirrors) | `Vec3s` | `Vec3f` | binary, XML, JSON, exporter |
| `PolygonDlist2.pos` (mesh-type-2 cull centre) | `Vec3s` | `Vec3f` | same |
| `Path.points` / `PathData.points` | `Vec3s*` | `Vec3f*` | `PathFactory`, `UnboundPathFactory`, exporter; every overlay reader (`z_path.c`, En_Kz, En_Md, En_Nb, En_Mb, En_Mm, En_Cs, En_Daiku_Kakariko and the `Math_Vec3s_ToVec3f` users) — `SEGMENTED_TO_VIRTUAL` returns `void*`, so a reader left on `Vec3s*` compiles and reads garbage |
| `LightPoint.x/y/z` + `Lights_Point*SetInfo` | `s16` | `f32` | binary, XML, JSON, exporter |
| `HorseData.pos` | `Vec3s` | `Vec3f` | JSON save (name-keyed) |
| `CollisionHeader.vtxList`, `dyna.vtxList` | `Vec3s` | `Vec3f` | `collision.bin` **v2**, binary, XML |
| `CollisionPoly.dist` | `s16` | `f32` | same |
| `CollisionHeader.minBounds/maxBounds` | `Vec3s` | `Vec3f` | same |
| `WaterBox` origin/lengths | `s16` | `f32` | same |
| `BgActor.boundingSphere` | `Sphere16` | `Spheref` | — |
| `BGCHECK_Y_MIN` | `-32000` | `-2147483648.0f` (exact in f32; still the "no floor" sentinel) | — |
| `BGCHECK_XYZ_ABSMAX` | `32760` | `1048576` | — |
| `ColliderCylinder.dim` | `Cylinder16` | `Cylinderf` | `Collider_UpdateCylinder` + direct writers |
| `ColliderJntSphElement.dim.worldSphere` | `Sphere16` | `Spheref` | per-overlay writers |

### `collision.bin` v2

`"$schema": "unbound/collision/2"`, little-endian, no header:
`vertices × { f32 x, y, z }` (12 B), then `polys × { u16 type, u16 pad, u32 vA, u32 vB, u32 vC,
s16 nx, ny, nz, s16 pad, f32 dist }` (28 B). The loader still reads v1 (`s16` vertices, 24-byte
polys) when the schema says `/1`; the converter emits v2.

## Fog and draw distance

A big world is pointless if it fades out at 2 500 units. Vanilla had three coupled caps:

| Cap | Why |
|---|---|
| Fog could not **start** past ~2 500 units | `fogNear` is not a distance. The N64 fog factor is `alpha = 256·(u − fogNear)/(1000 − fogNear)` with `u = 1000·f/(f−n)·(1 − n/d)`, so with `zNear = 10` fog starts at `10·1000/(1000 − fogNear)`; the engine clamps `fogNear ≤ 996` because the packed `s16` multiplier `128000/(1000−fogNear)` overflows above it. Raising `zFar` changes this by a factor `f/(f−n)` ≈ 1 — nothing. |
| Far plane ≤ 12 800 | `Environment_Update` clamps `fogFar` to 12 800 and `Play_Draw` uses `lightCtx.fogFar` as `zFar`. Room chunks (`mesh.type` 2) are culled at the same value. |
| Actors vanish at ~1 350 units | `uncullZoneForward` defaults (1000 + 350) were tuned so the fog hides the pop-in. |

### Lift

- **Format.** A `lighting` entry may carry `"fogStart"`, `"fogEnd"`, `"drawDistance"` and
  `"nearPlane"` (world units, numbers). Any of the first three switches the entry to *world fog*;
  the rest default: `drawDistance` ← `fogFar` (or 12 800), `fogEnd` ← `drawDistance`, `fogStart` ←
  the vanilla `fogNear` converted to a distance, `nearPlane` ← 0 (keep the view's 10). `fogNear` /
  `fogFar` stay for vanilla and for the blend-rate bits packed in `fogNear`.
- **Engine.** `EnvLightSettings` (both mirrors) and `LightContext` gained the world fields;
  `Environment_Update` blends them through the same day/night and indoor cross-fades
  (`Environment_LerpWorldFog`), then sets `lightCtx.zNear/zFar`, which `Play_Draw` now feeds to the
  projection and `z_room.c` uses to cull chunks. Vanilla entries take `zNear 10 / zFar = fogFar`
  exactly as before. `adjFogNear` (Nayru's Love, fairies, game over) is honoured in world mode by
  converting the start distance to the 0..1000 scale and back.
- **GPU.** The fog factor is computed on the CPU in the interpreter (`fog = ndcZ·mul + offset`) and
  only mixed in the shader, so the fix is the two numbers: a new extended op **`G_FOGF`**
  (`OTR_G_FOGF`, libultraship fork) carries them as floats; `Play_SetFog` emits it in world mode
  with `mul = 128000/(u₁−u₀)`, `offset = (500−u₀)·256/(u₁−u₀)` from `u(fogStart)`, `u(fogEnd)`.
  `fog_mul/fog_offset` in the interpreter are floats. Vanilla scenes still go through
  `gSPFogPosition`, bit-identical.
- **Culling.** Room-chunk cull uses `zFar`; the chunk radius (`PolygonDlist2.unk_06`, JSON
  `"radius"`) is `f32`. Actor uncull zones are scaled by `zFar / 12800` in world-fog scenes (on top
  of the "Increase Actor Draw Distance" enhancement), so a scene with `drawDistance: 200000`
  keeps its actors visible ~15× further.
- Depth precision: `zNear 10` against `zFar 10⁶` is a 10⁵ ratio; the OpenGL backend uses a 24-bit
  depth buffer, so far geometry may z-fight. Set `"nearPlane": 50` (or more) in such scenes.

## Not changed (documented limits)

- **Scene camera data** (`CamData.camPosData`, `BGCAM_*` packing in `z_camera.c`) is still
  `Vec3s`: fixed-camera zones cannot sit beyond ±32 767. Restructuring the `Vec3s[3]` triple is
  its own format change.
- **Cutscene camera points** (`CutsceneCameraPoint.pos`) are parsed straight out of the cutscene
  command words, so they stay `s16`.
- Cosmetic `Vec3s` (`ColliderInfo.bumper.hitPos`, `EffectSpark`/`Blure`/`ShieldParticle`) and the
  `EffectSs` ±32 000 cull in `z_effect_soft_sprite.c` (raised to `BGCHECK_XYZ_ABSMAX`).
- `Vtx` remains `short`; a single DL cannot span more than 65 535 units — split rooms.

## Verification

1. Vanilla parity after the float-`Mtx` step alone (must be pixel-identical): Hyrule Field,
   Kakariko, Forest Temple, Jabu-Jabu conveyor, Water Temple, Ganon's Tower collapse, Epona +
   save/load, Kokiri cutscene cameras, pause menu / file select (2D matrices).
2. A Prelude scene with a room at `origin: [200000, 0, 0]` and a walkable floor at y = −60 000:
   spawn there, walk, doors, water box, a Deku Baba on a moving platform, ride Epona and save.
