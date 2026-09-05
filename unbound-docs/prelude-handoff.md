# Handoff to Prelude: changelog

For the Prelude of Light agent. A dated list of what changed in SoH: Unbound (branch `unbound`)
that Prelude has to emit or read differently, and what it may now
let the user build. **The contract is [`SPEC.md`](./SPEC.md)**; every entry below cites the SPEC
section that defines it, and when this page and SPEC disagree, SPEC wins. Everything in the code
is tagged `SOH [Unbound]`.

## 2026-09-05 — animated materials from data: `materialAnims`

Vanilla animates water by a per-scene C draw config that rebinds runtime segments 8–13 every
frame; a custom scene had no way to ask for one. A scene setup may now carry `materialAnims`, the
Majora's Mask AnimatedMaterial list in JSON: per entry a segment (8–13, absolute), a display pass
(`opa`/`xlu`/`both`), and one of six recipes — `texScroll`, `twoTexScroll`, `color`,
`colorLerp`, `colorNonLinear`, `texCycle`. The engine binds them after the scene's draw config,
so a custom scene keeps `drawConfig` 0. Version-2 addition, no manifest change; an older reader
ignores the key and draws the material still.

| Change | SPEC | Prelude must |
|---|---|---|
| Optional `materialAnims` on a scene setup: a positional list of entries; the lists *inside* an entry (`layers`, `keyFrames`, colours, `textures`, `frames`) are plain JSON arrays. | §4.2 | Add a material-level authoring field (segment, pass, per-layer `xStep`/`yStep`/`width`/`height`); write the list only when a material uses one. Fast64's glTF extension carries no scroll settings, so this is a Prelude field. |
| The scroll list only sets tile sizes: the material's display list must load its texture with wrap addressing, set its tile(s) up, then call the segment right before its triangles. | §4.2 | Emit `G_DL` (0xDE) with `w1 = (segment << 24) \| 1` after the texture load and before the triangles; for `twoTexScroll` load render tiles 0 and 1; validate wrap addressing at export. The emitter already preserves such calls in vanilla lists. |
| Six segments per pass; materials may share one. | §9 | Assign segments per pass; two materials with the same motion share an entry. Report when a scene wants a seventh. |
| Older readers degrade silently. | §10 | Note in the export summary that animated materials need Unbound 0.6+. Do **not** raise `requires.formatVersion`. |
| Preview. | — | Feed scroll entries into the existing `ScrollLayers` seam (the same one MM's list and OoT's draw configs ride); colour and cycle entries may stay parsed-but-static as they are for MM today. |

Test data: `examples/lake-hylia-reversed-water/` rebinds a vanilla scene's water through the key.

## 2026-09-02 — a setup may bind a custom song: `sound.song`

| Change | SPEC | Prelude must |
|---|---|---|
| Optional `sound.song` on a setup's `sound`: the archive path of a custom sequence (`custom/music/<Name>`) that plays in place of the setup's `seq`. `seq` stays a vanilla id — the theme heard when no mounted layer provides the song (the reader logs the miss). `null` unbinds. An older reader ignores the key (§2) and plays `seq`. | §4.2 | Emit the path only when a song is bound; ship the song's `custom/music/*` entries in the same export; keep `seq` a real vanilla theme, not `NA_BGM_NO_MUSIC` — the engine goes silent before the song is resolved. |

## 2026-08-30 — vertex array v1 is `OARR`, not `OVTX`

Fork-side fix, no Prelude change. Prelude's v1 vertex arrays (`OARR` arrayType 25, header
version 1, 22-byte records) were unreadable because SoH registered the wide record only on the
unused `OVTX` type; the first Lake Hylia export lost every batch that reached past ±32 767. A
Vertex `OARR` of either version now loads as the vertex resource the interpreter sizes offsets by
(§8.1 rewritten to name `OARR`).

## 2026-08-29 — format version 2: integral geometry, no pre-release compatibility

Format version 1 (everything Prelude exported before this date) is **not read** by this build.
There is no migration path; re-export.

| Change | SPEC | Prelude must |
|---|---|---|
| `unbound.json` `formatVersion` and `requires.formatVersion` are `2`; any other value refuses the layer. | §6 | Write `2` for both. |
| `collision.json` `$schema` is `unbound/collision/3`; `/1` and `/2` are rejected. `collision.bin` holds `s32` vertices and `s32` `dist` (same 12/28-byte records as `/2`, integer fields). | §4.4.1 | Emit `/3`; write integers; round `dist`. |
| Collision vertices, bounds and water-box extents are integers. A fractional JSON value is rounded by the reader, so the validator should flag one. | §2, §4.4 | Snap collision to whole units on export. |
| Room `origin` is gone. Mesh vertices are absolute world coordinates. | §4.3 | Stop emitting `origin`; never rebase vertices. |
| Vertex array v1 (`OARR`, arrayType 25, resource header version 1; `s32` positions, 22-byte records) for any mesh with a vertex outside ±32 767. Offsets in exported display lists are byte offsets in units of the emitting version's record size (16 for v0, 22 for v1). | §8.1 | Emit v1 only where needed; compute DL vertex offsets against 22 for v1 meshes. |
| The packed `data0`/`data1` surface-type form and the packed water-box `properties` form are rejected. | §4.4 | Emit only the unpacked fields. |

## 2026-08-28 — review pass (after the format pass)

Reader behaviour Prelude's writer and validator have to know; every row is normative in SPEC.

| Change | SPEC | Prelude must |
|---|---|---|
| The first byte of a `scene.json` / `rooms/<n>.json` / `collision.json` / `paths/*.json` must be `{` — no BOM, whitespace or leading comment. | §2 | Emit `{` first; comments only after it. |
| Transition-actor `params` bits 10–15 must be 0: the list index is added to `params` at spawn. | §4.2 | Do not pack the vanilla index. |
| A mod's `unbound.json` must not list `"scenes"` in `features` unless the mod is a base (provides every vanilla scene in JSON form). | §1.3, §6 | Write `features` honestly; a scene mod lists none or its own kinds. |
| `cameras[].count` is a run length: the camera reads `count` consecutive `cameraPositions` from `positionIndex`. | §4.4 | Keep `positionIndex + count` within the list. |
| Once a base is mounted, vanilla-format scene resources (`scenes/shared/…`) are never read; a scene edit must be a `scene.json` / room delta. | §1.6 | Emit JSON deltas for scene edits. |
| Every document in a setup's `paths` array is read; path index = position in the concatenation. | §4.2 | Any number of documents; order matters. |
| Numeric strings are strict: no whitespace, one sign, one `0x`. | §2 | Emit numbers. |
| Text keys outside 0–65534 (including `0xFFFF`) are skipped with an error; code points above U+00FF and malformed UTF-8 become `?`. | §5 | Validate ids; keep text Latin-1. |
| Unknown mesh `type`, light `type` ≥ 3, mesh `format` other than 1 or 2, or more than 255 type-1 images reject the room. | §4.3 | Validate before export. |
| `$order` on a positional list rejects the document; unknown `$`-prefixed keys are ignored; a `null` deletes in every layer, including a single-layer document. | §3 | — |
| Floor `lightSetting` is valid up to the setup's `lighting` count (no longer 30); water-box `lightSetting` 31 reads as 0. | §4.4, §9 | Validate against the lighting list. |
| A `sceneId`, `drawConfig` or entrance `index` that is negative or out of range rejects the entry. | §7 | Validate. |

## 2026-08-28 — format pass

| Change | SPEC | Prelude must |
|---|---|---|
| Scene registry is one layer-merged `unbound/scenes.json` keyed by scene id; per-file `unbound/scenes/*.json` is gone. Fields renamed: `titleCard` → `titleCardTexture` (scene) / `showTitleCard` (entrance); `entrances` is a keyed object, not an array. | §7 | Emit the new document; drop the old per-file writer. |
| Text is one layer-merged `text/<lang>/messages.json` per language; `unbound/text/*.json` is gone; `messages` is an object keyed by id, `null` deletes; no `language` key. | §5 | Write per-language documents with only the changed ids. |
| Exit values may be entrance **names** (`ENTR_*` or `<scene id>/<entrance id>`). | §4.2 | Emit names for custom entrances; `index` on a registry entrance is only needed for binary scenes. |
| `surfaceTypes` entries are unpacked named fields; `data0`/`data1` is a legacy form. | §4.4 | Read/write the named fields. |
| Water boxes are unpacked: `camera`, `lightSetting`, `room`, `notSwimmable`; `properties` is a legacy form. | §4.4 | Read/write the named fields. |
| Lighting `fogNear` is the 0–1000 value only; the blend rate is `fogBlendRate`. | §4.2 | Split the packed word on import, write both keys. |
| A positional list with a hole, or an unresolvable exit name, fails the document. | §3 | Validate before export. |
| `unbound.json` `formatVersion` / `requires.formatVersion` are checked. | §6 | ~~Write `formatVersion: 1`~~ — `2` as of 2026-08-29. |
| The exporter writes numbers everywhere (message ids stay hex-string keys). | §2 | Accept both on read. |

Mods written against the previous shapes (per-file registry, `unbound/text`, packed collision
words) need re-exporting; as of 2026-08-29 SoH rejects packed collision words outright and does not
read the old registry/text paths.

## 2026-08-27 — world extent, rooms, dyna

| Change | SPEC | Prelude must |
|---|---|---|
| ~~`unbound/collision/2` and the f32 `collision.bin` v2 layout~~ — superseded above. | §4.4.1 | — |
| Every actor/light/path position may be a fractional number (collision is integral as of 2026-08-29). | §2 | Float parse path. |
| ~~Room documents may carry a top-level `origin`~~ — removed above. | §4.3 | — |
| Water boxes accept an explicit `room` (`-1` = all). | §4.4 | Emit `room` always. |
| Lighting entries accept `fogStart`, `fogEnd`, `drawDistance`, `nearPlane` (world units). | §4.2 | Expose in the lighting inspector; suggest `nearPlane` ≥ 50 when `drawDistance` > ~100 000. |
| Mesh-type-2 `radius` may be fractional / > 32 767. | §4.3 | Float parse path. |

Limits Prelude may now let the user exceed, and the ones it must keep validating: SPEC §9.

## Earlier (already in Prelude's `UNBOUND.md`)

Unlimited scenes and entrances via the registry (§7), 29-bit collision vertex ids (§4.4.1),
growable message tables (§5), objects per setup 1 024, actors per room 65 535, live actors
8 192, mesh entries unbounded in JSON (§9).

## Suggested Prelude work items

1. Registry writer: emit `unbound/scenes.json`; entrance references by name in exit lists.
2. Text writer: per-language `messages.json` deltas; import the object form.
3. Collision writer: schema `/3`, integral `s32` vertices/bounds/water boxes, unpacked surface
   types and water boxes; vertex resource v1 where a mesh needs it. Importing old fixtures (packed
   words, `/1`, `/2`, `origin`) is Prelude's concern alone — SoH does not read them.
4. Lighting inspector: `fogBlendRate` and the world-fog keys.
5. Validation updates per SPEC §9; keep the camera-data and per-room `Vtx` warnings.
6. Regenerate test fixtures from a fresh `soh --export-unbound` (works headless).

## How to verify against this build

- Build `unbound` (`cmake --build build-cmake --target soh -j8`), export `oot-unbound.o2r`,
  place it beside `oot.o2r`. Loader errors are prefixed `[Unbound]` and name the document and key.
- Float-matrix rendering parity has had a title-screen run only; the first thing to eyeball is
  that vanilla scenes look identical.
