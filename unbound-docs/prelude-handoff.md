# Handoff to Prelude: limits lifted on 2026-08-27

For the Prelude of Light agent. What changed in SoH: Unbound (branch `unbound`), what Prelude must
emit differently, and what it may now allow the user to build. Authoritative detail: `counts.md`
(rooms), `collision.md` (dyna), `extent.md` (world extent), `scene-format.md` (format); when this
summary and an area doc disagree, the area doc wins. Everything in the code is tagged `SOH [Unbound]`.

## 1. Format changes Prelude has to handle

| Change | Emit / read | Back-compat |
|---|---|---|
| **`collision.json` `$schema` is now `unbound/collision/2`** and `collision.bin` v2: vertices `f32 x,y,z` (12 B), polys `{ u16 type, u16 pad, u32 vA, vB, vC, s16 nx, ny, nz, s16 pad, f32 dist }` (28 B), no vertex padding. | Emit v2 when writing collision. Read both when importing: the version comes from the schema string. | SoH still loads `/1` (s16, 24-byte polys). The converter now writes v2, so a freshly exported `oot-unbound.o2r` is v2 throughout. |
| **Every `pos` / `bounds` / water-box extent may be a fractional number** (JSON number, not only int). | Parse with a float path. Emitting integers is still fine. | Ints still accepted everywhere. |
| **Room documents may carry a top-level `"origin": [x, y, z]`** (next to `setups`). Mesh vertices of that room are relative to it; the game places the room with a translate matrix. Default `[0,0,0]`. | Emit when a room's geometry would leave the `Vtx` s16 range (±32 767). Everything else in the room — collision, actors, lights, paths — stays **absolute**. | Absent = vanilla behaviour. |
| **Water boxes accept an explicit `"room"`** (`-1` = all rooms). It overrides the 6-bit room field packed in `properties`. | Emit `room` always; keep `properties` as before. The converter now emits it. | Missing key → unpacked from `properties` (old cap 63 applies only then). |
| **`"rot"` stays `Vec3s`; `"pos"` is float.** No change to keys, only to the allowed value range/type. | — | — |

| **Lighting entries accept `"fogStart"`, `"fogEnd"`, `"drawDistance"`, `"nearPlane"`** (world units). Any of the first three switches that entry to world-unit fog; the others default sensibly (`drawDistance` ← `fogFar` or 12 800, `fogEnd` ← `drawDistance`, `fogStart` ← vanilla `fogNear` converted, `nearPlane` ← 0 = keep 10). | Expose them in the lighting inspector. Keep emitting `fogNear`/`fogFar` too (blend rate lives in `fogNear`'s high bits). Suggest `nearPlane` ≥ 50 when `drawDistance` > ~100 000 (24-bit depth). | Absent = vanilla fog (fog cannot start past 2 500 units, far plane ≤ 12 800). |
| Mesh-type-2 `"radius"` may be fractional / > 32 767. | float path | ints fine |

Since then the format pass (2026-08-28) changed the registry (`unbound/scenes.json`, layer-merged),
text (`text/<lang>/messages.json`, layer-merged; `unbound/text/*.json` is gone), named exits,
unpacked `surfaceTypes` / water boxes, and `fogNear` + `fogBlendRate` — see `scene-format.md`,
`registries.md` and `text.md`.

## 2. Limits Prelude may now let the user exceed

| Limit | Was | Now | Notes for the editor |
|---|---|---|---|
| Rooms per scene | 255 (header) / **127 addressable** / **32 with working clear flags & minimap bits** | 65 535 header, 32 767 addressable, clear flags unbounded | Room numbers ≥ 32 have no minimap "visited" bit (no minimap for custom scenes anyway). |
| Transition actors (doors/planes) per scene | 64 | 65 535 | Door `params` still carry the index in bits 10–15 for the first 64; SoH ignores that for placement now. Prelude should keep writing them the vanilla way. |
| Water boxes per room number | rooms ≤ 63 | any room | via the `room` key above |
| Dyna (moving-collision) actors alive | 50 | unbounded (grows ×2 from 64) | |
| Dyna polys / verts total across live dyna actors | 16 384 each | unbounded (grows) | |
| World extent (any position) | ±32 767 | **±1 048 576** (`BGCHECK_XYZ_ABSMAX` = 2²⁰) | f32 precision ≈ 0.06 units at the edge. |
| Floor height sentinel | −32 000 = "no floor" | −2 147 483 648 | A floor at y = −40 000 now works. |
| Collision vertex / bounds / `dist` range | s16 | f32 | |
| Water box size | 32 767 per side | unbounded | |
| Spawn / transition / mesh-type-2 cull positions | s16 | f32 | |
| Path waypoints | s16 | f32 | |
| Point-light positions | s16 | f32 | |
| Hit-box (`Cylinder16` / `Sphere16`) positions | s16 | f32 | game-side only; no format impact |

Earlier sessions (already in Prelude's `UNBOUND.md`, listed for completeness): unlimited scenes and
entrances via `SceneDB`, 29-bit collision vertex ids / 32-bit poly ids, growable node tables,
objects per scene 1 024, actors per room 65 535, live actors 8 192, mesh entries 32-bit, sorted
entries 1 024, growable message tables, message buffer 8 KB.

## 3. Still capped — Prelude should keep validating these

| Limit | Value | Why it stayed |
|---|---|---|
| Scene camera data (`cameraPositions` in `collision.json`) | ±32 767 | `CamData` packs pos/rot/fov into `Vec3s[3]`; needs its own format change. Warn if a camera position leaves the range. |
| Cutscene camera points | ±32 767 | Parsed straight from cutscene command words. |
| One room mesh (`Vtx`) | 65 535 units across, and every vertex within ±32 767 **of the room `origin`** | `Vtx` is still `short`. Prelude must split big areas into rooms and set each room's `origin`. |
| Decoded text box | 1 024 bytes | Split long text with box-break codes. |
| Entrance layer groups | 4 per entrance | Custom entrances still register 4 identical layers. |
| Minimap / pause map for custom scenes | none | Unchanged. |
| Binary (legacy) `SetMesh` entries | 255 | JSON rooms have no cap; only matters for non-JSON archives. |

## 4. Suggested Prelude work items

1. Collision writer: switch to schema `/2` and the f32/28-byte layout; reader: branch on schema.
2. Number parsing: accept floats for `pos`, `bounds`, water-box extents, `origin`.
3. Room `origin`: compute when exporting a room whose vertices exceed the s16 range; rebase mesh
   vertices only. Show the origin in the room inspector.
4. Water box editor: expose `room` (with "all rooms" = −1); drop the 63 validation.
5. Validation updates: room count/number limits → 32 767; transition actors → 65 535; world bounds
   → ±1 048 576; keep the camera-data and per-room `Vtx` warnings.
6. Regenerate any test fixtures from a fresh `soh --export-unbound` (that CLI works headless now;
   it used to hang in the ROM-extractor prompt).

## 5. How to verify against this build

- Build `unbound` (`cmake --build build-cmake --target soh -j8`), export `oot-unbound.o2r`, place it
  beside `oot.o2r`. The log line `[Unbound] scenes/<scene>/collision.json` errors if a schema is
  unknown; v1 and v2 both load.
- Not yet verified visually in this session: float-matrix rendering parity. A quick title-screen run
  is clean, but the first thing to eyeball is that vanilla scenes look identical.
