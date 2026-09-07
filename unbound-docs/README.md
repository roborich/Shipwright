# SoH: Unbound

**SoH: Unbound** is an experimental fork of Ship of Harkinian (branch `unbound`, based on tag
`9.2.3`) with one purpose: **remove the limits Ocarina of Time inherited from N64 hardware so
modders can build things the vanilla game shape cannot hold.** Its primary consumer is
[Prelude of Light](https://preludeoflight.com), a browser-based o2r editor; Prelude gains an
"Unbound" mode that targets this build (`UNBOUND.md` in the Prelude repo).

It is not a randomizer build and does not try to stay diff-minimal against upstream. Randomizer
and other enhancements that assume the vanilla tables may break; that is accepted.

## Read this first

- **[`SPEC.md`](./SPEC.md) — the contract.** Every way an Unbound `.o2r` differs from a vanilla
  one: paths, documents, keys, types, merge rules, limits. Code reviews of this fork and of
  Prelude's export are held to it; a fix that would change it is a format change. Nothing else in
  this directory is normative.
- The other files are *how* and *why*: they explain the engine changes behind each part of the
  spec and may change freely.

## Goals

1. **Uncap.** Collision size, scene and entrance count, objects per scene, actors, rooms, mesh
   entries, message ids, world extent — any fixed N64-era number a modder can hit.
2. **Patch, don't replace.** Structured game data (scene/room headers, collision metadata, text,
   the scene registry) is JSON that **merges across archive layers**, so a mod ships only what it
   changed. No more bundling a whole scene because one exit moved.
3. **Portable mods.** No ROM-version-specific names, and no allocated numbers, in anything a mod
   references: entrances are referenced by name.
4. **Keep the container.** Everything is still an `.o2r` (zip) loaded by libultraship; only what is
   *inside* the OoT archive was redesigned.

## How it works, in one paragraph

`oot.o2r` is converted on first launch (`SOH::Unbound::EnsureBaseArchive`; also `soh --export-unbound
<out>`) into the Unbound layout: every
scene/room header becomes `scenes/<name>/scene.json` + `rooms/<n>.json`, collision becomes
`collision.json` + `collision.bin`, message tables become `text/<lang>/messages.json`, and every
other resource is copied verbatim. Placed beside `oot.o2r`, the converted archive is mounted above
it and SoH loads scenes from the JSON, merging every mounted mod's fragment of the same path. Mods
add new scenes and entrances by declaring them in `unbound/scenes.json`. The C game code was
widened wherever a struct field or arena enforced a cap.

## What has been changed

Everything below is tagged `// SOH [Unbound]` in the code. Each area has a how-doc; the format
facts are in the cited SPEC sections.

| Area | Change | How-doc | SPEC |
|---|---|---|---|
| **Collision** | Vertex indices and poly ids are 32-bit; the N64 byte budget is gone — node tables are heap-allocated and grow on demand, freed in `Play_Destroy`. Legacy packed data is unpacked on load. Surface types and water boxes are unpacked structs. Dyna actor table and dyna poly/vertex lists grow on demand. | [`collision.md`](./collision.md) | §4.4, §8 |
| **Scenes & entrances** | `gSceneTable`/`gEntranceTable` replaced by a runtime registry (`SceneDB`) fed from a layer-merged `unbound/scenes.json`; exit lists reference entrances by name; custom-scene save flags are stored by scene name. Console: `entrance <name>`. | [`registries.md`](./registries.md) | §7, §4.2 |
| **Text** | Message tables are growable and hash-indexed; `text/<lang>/messages.json` merges across layers and can add or delete ids; message buffers 8 KB. | [`text.md`](./text.md) | §5 |
| **Counts** | Object bank 1024; actors per room and rooms per scene 16-bit; live-actor cap real and 8192; mesh entries unbounded; room numbers 16-bit with unbounded clear flags, waterbox rooms and transition actors. Object ids past the vanilla table are usable. | [`counts.md`](./counts.md) | §9 |
| **Scene format** | Merging JSON loader, converter, entity-key scheme and the decisions behind them. | [`scene-format.md`](./scene-format.md) | §2–§4, §6 |
| **World extent** | Actor-side positions are `f32`: float `Mtx` (libultraship fork `GBI_FLOAT_MTX`), spawns, paths, point lights, colliders. Geometry is integral and wide: room meshes use `s32` vertices (`GBI_S32_VTX`, vertex resource v1), so one room is no longer capped at 65 535 units, and collision is `s32` (vertices, bounds, plane distances, water boxes). Fog and draw distance are per-scene world units. | [`extent.md`](./extent.md) | §4.2–4.4, §9 |
| **Converter** | Runs at boot when `oot-unbound.o2r` is missing or its manifest `source` (converter build, ROM hashes) differs; also `soh --export-unbound <out.o2r>` / console `unbound-export`. Vanilla → Unbound archive in ~1 s. `soh/soh/unbound/UnboundExporter.cpp`. | `scene-format.md` §3 | — |
| **Animated materials** | A scene setup's `materialAnims` list is the data-driven form of what a scene draw config does in C (scrolling water, colour cycles, flipbooks): the Majora's Mask AnimatedMaterial system, bound per pass after the draw config. | [`materials.md`](./materials.md) | §4.2, §9 |
| **Loader** | libultraship gained a JSON resource format (`{` sniff, type from `$schema`, found in any layer) and `LoadFileFromAllLayers`; SoH's JSON factories (`soh/soh/unbound/`) build the same command objects the binary loaders build, so scene execution code is untouched. | `scene-format.md` §2 | §3 |
| **Prelude** | Dated changelog of what Prelude must emit differently. | [`prelude-handoff.md`](./prelude-handoff.md) | — |

Verified in game: a Prelude-generated mod adding a **new scene with high-poly collision** loads and
plays; a two-line delta mod merges over the converted base (`examples/hyrule-field-actor-delta/`).

## Known remaining limits

The modder-facing list — what a tool must still validate — is SPEC §9. Engine-internal notes
behind them and likely next targets:

- Scene camera data (`CamData.camPosData` `Vec3s[3]`) and cutscene camera points need their own
  format change to leave the s16 range.
- No minimap / pause map for custom scenes: `Map_Init` is keyed by vanilla scene ranges; needs a
  registry field for map data.
- Save data: `sceneFlags[124]` stays positional for vanilla scenes; custom scenes are keyed by
  name in the `unbound` save section; save states don't capture custom flags.
- Alternate setups are stored in full; `SetAlternateHeaders` arrays grow with the highest index
  (13 seen in vanilla). Not a cap.
- MQ: the converter emits `_mq` scenes only when `oot-mq.o2r` is mounted at export time.
- A newer-`formatVersion` layer is refused as a base but its files still merge (libultraship
  mounts whole archives).

## Working on the fork

- Build: `cmake --build build-cmake --target soh -j8`. A change to `z64.h` or `z64bgcheck.h`
  rebuilds nearly everything (10+ min on a busy machine); run long builds in the background with a
  log.
- Smoke test: launch with `oot.o2r` in `build-cmake/soh` (delete `oot-unbound.o2r` to force a
  conversion) and grep the log for `[Unbound]` — the title screen loads Hyrule Field through `scene.json`.
- Play test: [`testing.md`](./testing.md) — ten minutes that exercise every widened type; the title
  screen exercises almost none of them.
- Regenerate `soh.o2r` (`--target GenerateSohOtr`) if switching from a branch with different
  shaders; a stale one crashes at boot.
- Prefer widening a field over adding a registry; prefer a registry over a static table; prefer
  JSON that merges over binary that replaces; prefer a name over an allocated number. Keep the
  `SOH [Unbound]` marker on every edit. Key names live once, in `soh/soh/unbound/UnboundSchema.h`.
- A change to what an archive may contain is a SPEC change first (SPEC §10), code second.
- After widening a type, run `scripts/unbound-pointer-drift.sh`. The game's C files build with `-w`,
  so a caller that still passes the *old* pointer type (a `Vec3s*` reader on f32 path points, a `Vec3f*`
  alias of the s32 dyna vertex list) compiles silently and reads the new bytes as the old type; three of
  those shipped in 0.3. The script re-checks every file with only `-Wincompatible-pointer-types` on and
  keeps only diagnostics that name a widened type. Expected output is six vanilla lines — SoH passing a
  resource to a `char*` name parameter (`z_bgcheck.c:3849`, `z_player_lib.c:2277`, `z_bg_spot03_taki.c:42`,
  `z_en_ganon_mant.c:320`, `z_en_jsjutan.c:129-130`); anything else is drift. `--narrowing` lists the
  ~90 places a widened value is stored narrower (world-extent limits, mostly vanilla `s16` positions).
  The C++ resource mirrors are checked separately by `static_assert`s in their factories.
- libultraship is a submodule on the fork branch `unbound`; commit there first, then update the
  pointer here.

## Releasing

`roborich/Shipwright` ships two products from one workflow, `.github/workflows/generate-builds.yml`
(kept identical on both release branches; the tag decides everything):

| Product | Branch | Tag | Release title |
|---|---|---|---|
| SoH: Unbound | `unbound` | `<SoH version>-unbound<X.Y>` (`9.2.3-unbound0.1`) | `SoH: Unbound <tag>` |
| SoH (cel-shading fork) | `wind-waker-style-cel-shading` | `<SoH version>-celshade<X.Y>` | `SoH (cel-shading fork) <tag>` |

Pushing a release branch warms the CI caches; pushing a tag builds macOS, Linux and Windows and
publishes the release with assets `SoH-<tag>-{Mac.dmg,Linux.appimage,Win64.zip}`. Release notes are
generated from the previous tag of the *same* product, prefixed by `.github/release-notes/<product>.md`.

```
git checkout unbound && git push origin unbound        # warm caches (optional)
git tag 9.2.3-unbound0.1 && git push origin 9.2.3-unbound0.1
```

`gh` defaults to upstream here; pass `-R roborich/Shipwright` to watch the run or the release.
