# Handoff to Prelude: changelog

For the Prelude of Light agent. A dated list of what changed in SoH: Unbound (branch `unbound`,
work landed on `unbound-fixes`) that Prelude has to emit or read differently, and what it may now
let the user build. **The contract is [`SPEC.md`](./SPEC.md)**; every entry below cites the SPEC
section that defines it, and when this page and SPEC disagree, SPEC wins. Everything in the code
is tagged `SOH [Unbound]`.

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
| `unbound.json` `formatVersion` / `requires.formatVersion` are checked. | §6 | Write `formatVersion: 1`. |
| The exporter writes numbers everywhere (message ids stay hex-string keys). | §2 | Accept both on read. |

Mods written against the previous shapes (per-file registry, `unbound/text`, packed collision
words) need re-exporting; SoH still loads packed collision words with a warning but does not read
the old registry/text paths.

## 2026-08-27 — world extent, rooms, dyna

| Change | SPEC | Prelude must |
|---|---|---|
| `collision.json` `$schema` `unbound/collision/2` and the f32 `collision.bin` v2 layout. | §4.4.1 | Emit v2; read both by schema. |
| Every position / bounds / water-box extent may be a fractional number. | §2 | Float parse path. |
| Room documents may carry a top-level `origin`. | §4.3 | Emit when geometry would leave the `Vtx` s16 range; rebase mesh vertices only. |
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
3. Collision writer: schema `/2`, unpacked surface types and water boxes; reader: accept the
   legacy forms too so old fixtures still import.
4. Lighting inspector: `fogBlendRate` and the world-fog keys.
5. Validation updates per SPEC §9; keep the camera-data and per-room `Vtx` warnings.
6. Regenerate test fixtures from a fresh `soh --export-unbound` (works headless).

## How to verify against this build

- Build `unbound-fixes` (`cmake --build build-cmake --target soh -j8`), export `oot-unbound.o2r`,
  place it beside `oot.o2r`. Loader errors are prefixed `[Unbound]` and name the document and key.
- Float-matrix rendering parity has had a title-screen run only; the first thing to eyeball is
  that vanilla scenes look identical.
