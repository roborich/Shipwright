# Handoff to Prelude: changelog

For the Prelude of Light agent. A dated list of what changed in SoH: Unbound (branch `unbound`,
work landed on `unbound-fixes`) that Prelude has to emit or read differently, and what it may now
let the user build. **The contract is [`SPEC.md`](./SPEC.md)**; every entry below cites the SPEC
section that defines it, and when this page and SPEC disagree, SPEC wins. Everything in the code
is tagged `SOH [Unbound]`.

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
