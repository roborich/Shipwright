# Unbound: merging loader and converter

How SoH reads the JSON scene documents and how the vanilla archive is converted into them. The
documents themselves — paths, keys, types, merge rules, limits — are defined in
[`SPEC.md`](./SPEC.md); this file never restates them. Overview in [`README.md`](./README.md).
Prelude's implementation guide (`UNBOUND.md` in the Prelude repo) is written against `SPEC.md`.

## 1. Decisions behind the format (settled 2026-08-27, revised 2026-08-28)

- Every alternate setup is stored **in full** (no patch-over-default inheritance). A mod that
  moves one actor therefore patches it in every setup that contains it; the trade is that no
  loader has to reason about inheritance and a setup is always self-describing.
- Entity identity is **Prelude's existing scheme**: vanilla entities keyed by their vanilla list
  index, additions keyed by a minted index that is never reused (Prelude mints past
  `originalCount + 1000`); engine-positional lists merge by index.
- Files: `scene.json` + `rooms/<n>.json` + `collision.json`/`collision.bin`; bulk assets stay as
  today's resources under the extractor's names (§1.1).
- Merge: key-wise, later layer wins, `null` deletes, `"$replace"` escape hatch (SPEC §3). One
  merge rule for every structured document — scenes, collision, paths, text and the registry.
- Loader walks every mounted archive; a JSON file is a first-class `ResourceLoader` format.
- The converter lives in SoH (an export command); the schema is the shared contract.
- Packed vanilla words (surface types, water-box properties, the fog-near word) are unpacked in
  the format so that no bit width of the N64 encoding survives as a cap (SPEC §4.4, §4.2).
- References between documents are by **name** where a number would be an allocation
  (entrances), and by position where the engine itself indexes by position (lighting, exits,
  spawns, cameras). See SPEC §4.2 "Exit value".

### 1.1 Why bulk resources keep their source names

The converter transforms only scene/room headers, collision headers, pathway lists and message
tables. Everything else — display lists, vertices, textures, cutscenes, objects, audio — is copied
**verbatim under its original path**, because renaming a DL would require rewriting every CRC64
reference inside other DLs. Stable, offset-free names therefore come from the *extraction* side
(Torch `stable-names-poc`): convert a Torch-extracted archive and the copied names are stable;
convert a ZAPD-extracted one and they carry offsets exactly as before. The structured documents
are portable either way; only bulk references inherit the source's naming.

`SetCsCamera` (OoT scene command 0x02) has no fields in SoH and is not represented in
`scene.json`.

## 2. Runtime (SoH side)

- libultraship: a file whose first byte is `{` is `RESOURCE_FORMAT_JSON`; its resource type
  name and version come from the top-level `"$schema": "<type>/<version>"`, searched through
  every mounted layer (topmost first) because a patch layer may omit it (SPEC §3.6–3.7).
  `ArchiveManager::LoadFileFromAllLayers(path)` returns every layer's bytes for a path, in mount
  order. `ResourceFactoryJson` is the factory base. Both are game-agnostic.
- SoH (`soh/soh/unbound/`): `UnboundJson` (merge rules and the shared field readers — the
  implementation of SPEC §2 and §3), `UnboundSchema.h` (every key name and `$schema` id, shared
  with the exporter so loader and converter cannot drift apart), and one factory per document
  kind — `ResourceFactoryJsonSceneV1` (`unbound/scene`, `unbound/room`),
  `…CollisionHeaderV1` (`unbound/collision` 1 and 2), `…PathV1` (`unbound/paths`). They
  register under the existing SoH resource types (`Room`, `CollisionHeader`, `Path`) with the JSON
  format and schema version as the discriminator, and build the **same `SOH::Scene` + `SetXxx`
  command objects** the binary/XML factories build — `z_scene_otr.cpp` and everything downstream
  are untouched. Alternate setups become child `Scene` objects under a leading
  `SetAlternateHeaders`, exactly as the binary command produces; every setup gets the top-level
  room list / collision injected.
- Failure policy: `Unbound::DocumentError` is thrown by `PositionalKeys` (hole) and `ResolveExit`
  (unknown entrance name) and caught in each factory's `ReadResource`, which returns null so the
  resource fails to load with one logged line. Missing scalar keys default (SPEC §2); a bad
  sub-resource path (collision, cutscene, pathway, room) is logged and skipped.
- Exit names are resolved through `EntranceDB_RetrieveIndex`, which works because the registry
  (`SceneDB::LoadCustomScenes`, SPEC §7) is populated from `UpdateModFiles(init)` before any scene
  resource is loaded.
- `SceneDB::GetScenePath` returns the `scene.json` path for vanilla scenes whenever an Unbound
  manifest is mounted (`DetectUnboundBase`); legacy `oot.o2r` alone keeps the binary path.
- Text tables are read directly through `LoadMergedJson` in `z_message_OTR.cpp`, not through
  `ResourceLoader` (see `text.md`).

## 3. Converter (SoH side)

`soh --export-unbound <out.o2r>` (and the debug-console command `unbound-export`):

1. For every registry scene (and its MQ variant when `oot-mq.o2r` is mounted): load through the
   existing factories, walk the `Scene` command list, emit the SPEC §4 documents with **vanilla
   indices as keys**, write `collision.bin` v2, copy bulk resources under their source names.
2. Text: dump each language table to `text/<lang>/messages.json` (SPEC §5).
3. Write `unbound.json` with the source ROM hash and build version (SPEC §6).
4. *(not implemented)* Legacy mod conversion: mount the mod over the base, load each scene it
   overrides, diff the resulting command objects against the base scene's, emit only the
   differing keys.

Implementation: `soh/soh/unbound/UnboundExporter.cpp` — `ExportArchive` orchestrates
`ConvertAllScenes` / `ConvertMessages` / `CopyUntouchedFiles` / `WriteManifest`, with one small
`XxxJson` emitter per scene command. Invoked headlessly (runs after the base archive is mounted,
before mods, then exits) or from the console. The output is a **stored** (uncompressed) zip with
fixed timestamps: deterministic, ~60 MB for vanilla, readable by libzip and by Prelude's
fflate-based reader. Compression can be added later without a format change.

## 4. Verification

1. Convert vanilla → `oot-unbound.o2r`; boot from it alone; walk the parity list in
   `collision.md`/`registries.md` (title screen, Kokiri, Field, a dungeon, MQ dungeon, credits).
2. Hand-written two-line `rooms/0.json` delta moves an actor in Hyrule Field
   ([`examples/hyrule-field-actor-delta/`](./examples/hyrule-field-actor-delta/)).
3. A legacy Prelude mod converted through §3.4 produces a delta archive < 5 KB and plays
   identically stacked on the converted base.
4. Two delta mods touching the same room but different actors both apply.
5. Format-pass checks: a `scene.json` whose exit is a name lands in the named scene; a positional
   list with a hole fails to load with one log line; a `collision.json` carrying legacy
   `data0`/`data1` loads with a warning and behaves identically; a `messages.json` with a `null`
   id removes that message.

Per-limit checks (from the lifts in `counts.md`, `collision.md`, `extent.md`):

- **Rooms.** Vanilla: Forest Temple (shutters, clear flags), Water Temple (waterbox rooms),
  Kakariko (Ruto in Jabu-Jabu), Ganon's Castle (En_Holl planes), minimap in Deku Tree. Custom: a
  Prelude scene with 40 rooms, a chest in room 35 whose clear flag survives save/load, water in
  room 35.
- **Dyna collision.** Vanilla: Fire Temple (many Bg_Hidan dyna), Ganon's Tower collapse, Water
  Temple, Zora's Domain, Gerudo Fortress (Bg_Spot15_Rrbox boxes). Custom: a Prelude scene with
  200 dyna platforms; collision viewer shows all; frame time flat vs the same scene with 30.
- **World extent.** Vanilla parity after the float `Mtx` change (Hyrule Field, Kakariko, Forest
  Temple, Jabu conveyor, Water Temple, Ganon collapse, Epona save/load, cutscene cameras in
  Kokiri). Custom: a Prelude scene with rooms at x = 200 000 and a walkable floor at y = −60 000;
  spawn, doors, water, Deku Baba on a moving platform, Epona ride + save; minimap dot; camera data
  in a far room.

## Status

Converter (§3 items 1–3) and the merging loader (§2) are implemented and boot-verified.
`oot-unbound.o2r` beside `oot.o2r` is mounted above it; `SceneDB` resolves vanilla scenes to
`scenes/<name>[_mq]/scene.json` whenever an `unbound.json` is mounted.

Not yet: legacy-mod conversion (§3.4).
