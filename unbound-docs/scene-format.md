# Unbound: scene format and merging loader

The authoritative spec for the Unbound archive layout, the JSON schema of its structured
resources, the layer-merge rules, and the loader/converter that implement them. Overview in
[`README.md`](./README.md). Prelude's implementation guide (`UNBOUND.md` in the Prelude repo)
is derived from this document; when they disagree, this one wins.

Decisions this spec is built on (settled 2026-08-27):

- Every alternate setup is stored **in full** (no patch-over-default inheritance).
- Entity identity is **Prelude's existing scheme**: vanilla entities keyed by their vanilla
  list index, additions keyed by a minted index that is never reused; engine-positional
  lists merge by index.
- Files: `scene.json` + `rooms/<n>.json` + `collision.json`/`collision.bin`; bulk assets stay
  as today's resources under Torch's stable names.
- Merge: key-wise, later layer wins, `null` deletes, `"$replace"` escape hatch.
- Loader walks every mounted archive; JSON bypasses `ResourceLoader`.
- The converter lives in SoH (an export command), and the schema is a shared spec.

## 1. Archive layout

```
unbound.json                          manifest (required; also how tools detect the format)
version                               LUS version file, unchanged (SoH's archive gate)
scenes/<scene>/scene.json             scene header, all setups in full
scenes/<scene>/rooms/<n>.json         room header, all setups in full
scenes/<scene>/collision.json         collision metadata (bounds, surface types, water boxes, cameras)
scenes/<scene>/collision.bin          collision bulk (vertices + polys, u32 indices)
scenes/<scene>/paths/<name>.json      pathway lists (one per SetPathways target resource)
<original path>                       cutscenes, DLs, vertices, textures, objects, audio: copied
                                      verbatim under their source names (see §1.1)
text/<lang>/messages.json             message table (see text.md; same schema as unbound/text)
unbound/scenes/*.json                 custom scene + entrance declarations (see registries.md)
unbound/text/*.json                   message merge files (see text.md)
objects/<name>/…                      unchanged
```

`<scene>` is the vanilla scene file leaf minus `_scene` (`spot04`, `ydan`), which is already
ROM-version independent. Master Quest dungeons are separate directories (`ydan_mq/`); the
registry entry's `variants.mq` names it and `SceneDB::GetScenePath` picks it. No path-string
substitution.

### 1.1 Bulk resources keep their source names

The converter transforms only scene/room headers, collision headers, pathway lists and message
tables. Everything else — display lists, vertices, textures, cutscenes, objects, audio — is copied
**verbatim under its original path**, because renaming a DL would require rewriting every CRC64
reference inside other DLs. Stable, offset-free names therefore come from the *extraction* side
(Torch `stable-names-poc`): convert a Torch-extracted archive and the copied names are stable;
convert a ZAPD-extracted one and they carry offsets exactly as before. The structured documents
are portable either way; only bulk references inherit the source's naming.

### `unbound.json`

```json
{
  "format": "unbound",
  "formatVersion": 1,
  "game": "oot",
  "source": { "romHash": "0x...", "label": "NTSC 1.2", "converter": "soh 9.2.3-unbound" },
  "features": ["scenes", "text", "collision"]
}
```

`format`/`formatVersion` are the only required keys. A *mod* archive that only patches may
carry a manifest with just those two, plus `"requires": { "formatVersion": 1 }`.

## 2. Structured resource schema

Conventions used throughout:

- Integers are JSON numbers; hex allowed as strings (`"0x0F12"`) wherever an id is expected.
- Vectors are 3-element arrays `[x, y, z]`; colours are `[r, g, b]`.
- **Keyed lists** are JSON objects whose keys are entity ids (strings). They carry an optional
  `"$order"` array listing keys in engine order; keys absent from `$order` follow after it in
  *key sort order*: keys that parse as integers first, ascending numerically, then the rest
  lexically. The converter emits vanilla lists without `$order`.
- **Positional lists** are JSON objects keyed by decimal index strings (`"0"`, `"1"`, …).
  They exist where the engine addresses entries by position and cannot tolerate holes.
- Setups: `"setups"` is an object keyed by setup index (`"0"`…`"3"` for child day/night,
  adult day/night; `"4"`+ for cutscene setups). Each setup is complete.

### 2.1 `scene.json`

```json
{
  "$schema": "unbound/scene/1",
  "collision": "scenes/kokiri_forest/collision.json",
  "rooms": { "0": "scenes/kokiri_forest/rooms/0.json", "1": "…" },
  "setups": {
    "0": {
      "specialObjects": { "elfMessage": 0, "globalObject": 2 },
      "skybox":  { "id": 1, "weather": 0, "indoors": 0, "unk": 0 },
      "sound":   { "seq": 0x27, "natureAmbience": 0, "reverb": 0 },
      "cameraSettings": { "cameraMovement": 0, "worldMapArea": 0 },
      "cutscene": "scenes/kokiri_forest/cutscenes/intro",
      "paths": ["scenes/kokiri_forest/paths/kokiri_forest_pathway.json"],
      "lighting": {
        "0": { "ambient": [80,80,80], "light1Dir": [49,49,49], "light1Color": [180,180,180],
               "light2Dir": [-49,-49,-49], "light2Color": [60,60,60],
               "fogColor": [120,140,170], "fogNear": 990, "fogFar": 3200 }
      },
      "entrances": { "0": { "spawn": 0, "room": 0 } },
      "spawns":    { "0": { "id": 0, "pos": [0,0,0], "rot": [0,0,0], "params": 0x0FFF } },
      "exits":     { "0": 0x00CD },
      "transitionActors": {
        "0": { "id": 0x0009, "pos": [0,0,0], "rotY": 0, "params": 0,
               "front": { "room": 0, "effects": 0 }, "back": { "room": 1, "effects": 0 } }
      }
    },
    "1": { "…full copy…": true }
  }
}
```

Field ↔ SoH command mapping:

| key | command | list kind |
|---|---|---|
| `specialObjects` | `SetSpecialObjects` | scalar |
| `skybox` | `SetSkyboxSettings` | scalar |
| `sound` | `SetSoundSettings` | scalar |
| `cameraSettings` | `SetCameraSettings` | scalar (optional) |
| `cutscene` | `SetCutscenes` | path string (optional) |
| `paths` | `SetPathways` | array of `paths/*.json` paths (optional) |
| `lighting` | `SetLightingSettings` | **positional** (surfaces reference the index) |
| `entrances` | `SetEntranceList` | **positional** (entrance table `spawn` is an index) |
| `spawns` | `SetStartPositionList` | **positional** (entrances reference it) |
| `exits` | `SetExitList` | **positional** (surface exit index) |
| `transitionActors` | `SetTransitionActorList` | **positional** (door params reference it) |
| `rooms` (top level) | `SetRoomList` | positional; identical across setups |
| `collision` (top level) | `SetCollisionHeader` | path; identical across setups |
| — | `SetAlternateHeaders` | implied by `setups` |
| — | `SetEndMarker` | implied |

`SetCsCamera` (OoT 0x02) has no fields in SoH and is not represented.

### 2.2 `rooms/<n>.json`

```json
{
  "$schema": "unbound/room/1",
  "setups": {
    "0": {
      "behavior": { "gameplayFlags": 0, "gameplayFlags2": 0 },
      "echo": 0,
      "time": { "hour": 255, "minute": 255, "increment": 255 },
      "skyboxModifier": { "skyboxDisabled": 0, "sunMoonDisabled": 0 },
      "wind": { "west": 0, "vertical": 0, "south": 0, "speed": 0 },
      "objects": { "0": 0x0002, "1": 0x0160 },
      "lights":  { "0": { "type": 1, "pos": [0,0,0], "color": [255,255,255], "glow": 0, "radius": 200 } },
      "actors": {
        "$order": ["0", "1", "2", "1000"],
        "0":    { "id": 0x0015, "pos": [100, 0, -40], "rot": [0, 0x4000, 0], "params": 0x0002 },
        "1000": { "id": 0x0095, "pos": [0, 0, 0],     "rot": [0, 0, 0],      "params": 0 }
      },
      "mesh": {
        "type": 0,
        "entries": {
          "0": { "opa": "scenes/kokiri_forest/rooms/0/mesh/floor_opa", "xlu": null }
        }
      }
    }
  }
}
```

| key | command | list kind |
|---|---|---|
| `behavior` | `SetRoomBehavior` | scalar |
| `echo` | `SetEchoSettings` | scalar |
| `time` | `SetTimeSettings` | scalar |
| `skyboxModifier` | `SetSkyboxModifier` | scalar |
| `wind` | `SetWindSettings` | scalar (optional) |
| `objects` | `SetObjectList` | positional (bank slot order; SoH tolerates any order) |
| `lights` | `SetLightList` | positional (optional) |
| `actors` | `SetActorList` | **keyed** (spawn order only; `$order` preserves it) |
| `mesh` | `SetMesh` | see below |

`actors` is the one keyed list. Keys are the **vanilla list index** for converted entries and
a **minted index** for additions; Prelude mints past `originalCount + 1000` and never reuses.
Any key that is a decimal integer is valid; tools may also use non-numeric keys for their own
additions (`"prelude-8f3a"`) as long as they are unique within the list.

`mesh.type`:
- `0` — `entries` positional `{ opa, xlu }` paths (either may be `null`).
- `1` — `{ "type": 1, "format": 1|2, "dlist": path, "image": {…} | "images": {…} }` with the
  `BgImage` fields (`source`, `tlut`, `width`, `height`, `fmt`, `siz`, `mode0`, `tlutCount`,
  and `id`/`unk00` for multi).
- `2` — `entries` positional `{ "pos": [x,y,z], "radius": n, "opa": path, "xlu": path }`.

### 2.3 `collision.json` + `collision.bin`

```json
{
  "$schema": "unbound/collision/1",
  "bounds": { "min": [-2000, -300, -2000], "max": [2000, 900, 2000] },
  "bulk": { "file": "scenes/kokiri_forest/collision.bin", "vertices": 1832, "polys": 2410 },
  "surfaceTypes": { "0": { "data0": "0x00000000", "data1": "0x00000000" } },
  "cameras":  { "0": { "sType": 1, "count": 0, "positionIndex": null } },
  "cameraPositions": { "0": [0,0,0], "1": [0,0,0] },
  "waterBoxes": { "0": { "xMin": 0, "ySurface": 0, "zMin": 0, "xLength": 0, "zLength": 0, "properties": "0x0" } }
}
```

`collision.bin` is little-endian, no LUS header: `vertices × { s16 x, y, z }` (6 bytes each),
padded to 4, then `polys × { u16 type, u32 vA, u32 vB, u32 vC, s16 nx, ny, nz, s16 dist }`
(24 bytes). Vertex words use the in-memory packing from `collision.md` (index bits 0–28,
xpFlags/conveyor bits 29–31). Bulk replaces whole; `collision.json` merges key-wise.

### 2.4 `paths/<name>.json`

```json
{ "$schema": "unbound/paths/1",
  "paths": { "0": { "points": [ [0,0,0], [10,0,0] ] } } }
```
One document per source pathway resource; `paths` is positional. A setup's `paths` array lists
the documents its `SetPathways` command referenced.

## 3. Merge rules

Applied per path, lowest mounted archive first, when a structured resource is loaded:

1. **Objects** merge key-wise; a later layer's value for a key replaces the earlier one,
   recursively for object values.
2. **`null`** deletes the key. For positional lists this is only legal at the tail (the engine
   cannot skip an index); the loader logs and refuses a hole.
3. **Arrays** (`pos`, `rot`, colours, `$order`) replace whole. `$order` from the highest layer
   that provides it wins; keys it omits are appended in key order.
4. **`"$replace": true`** on any object means "ignore lower layers for this subtree"; the key
   itself is dropped after merging.
5. **Bulk files** (`.bin`, DLs, vertices, textures, cutscenes) keep last-archive-wins.
6. A layer may omit any key; the merged document must satisfy the schema, or the scene
   falls back to the lowest complete layer with an error in the log.

Consequences: a Prelude "move one actor" mod is `rooms/2.json` containing
`{"setups":{"0":{"actors":{"12":{"pos":[…]}}}}}`; "change one exit" is
`scene.json` with `{"setups":{"0":{"exits":{"3":"0x0211"}}}}`; a self-contained scene is
unnecessary because every reference is to a stable name that exists in every conversion.

## 4. Runtime (SoH side)

- `ArchiveManager::LoadFileFromAllLayers(path)` (new, libultraship fork): walks `mArchives`
  in mount order and returns every layer's bytes for that path.
- `UnboundSceneLoader` (`soh/soh/resource/unbound/`): merges the JSON documents per §3,
  validates, and builds the **same `SOH::Scene` + `SetXxx` command objects** the binary/XML
  factories build — `z_scene_otr.cpp` and everything downstream are untouched. Alternate
  setups become child `Scene` objects exactly as `SetAlternateHeaders` produces today.
- `SceneDB::GetScenePath` returns the `scene.json` path for scenes in an Unbound archive
  (the manifest at mount time flips a per-archive flag); legacy `oot.o2r` keeps the old path.
- Collision: `CollisionHeader` gets a JSON+bin constructor alongside the binary/XML ones.
- JSON bypasses `ResourceLoader` (first-byte sniff would misread `{` as binary), the same way
  `SceneDB`/`OTRMessage_Init` already read `unbound/**.json`.

## 5. Converter (SoH side)

`soh-macos --export-unbound <out.o2r>` (and a dev-menu button):

1. For every registry scene (and its MQ variant): load through the existing factories, walk
   the `Scene` command list, emit §2 documents with **vanilla indices as keys**, write
   `collision.bin`, copy bulk resources under their stable names.
2. Text: dump each language table to `text/<lang>/messages.json`.
3. Write `unbound.json` with the source ROM hash/label.
4. Legacy mod conversion: mount the mod over the base, load each scene it overrides, diff the
   resulting command objects against the base scene's, emit only the differing keys. Whole
   replaced scenes (Prelude "self-contained" exports) shrink to their real delta.

Implementation: `soh/soh/Enhancements/unbound/UnboundExporter.cpp`. Invoked headlessly with
`soh --export-unbound <out.o2r>` (runs after the base archive is mounted, before mods, then
exits) or from the debug console with `unbound-export <out.o2r>`. The output is a **stored**
(uncompressed) zip with fixed timestamps: deterministic, ~51 MB for vanilla, readable by libzip
and by Prelude's fflate-based reader. Compression can be added later without a format change.

## 6. Verification

1. Convert vanilla → `oot-unbound.o2r`; boot from it alone; walk the parity list in
   `collision.md`/`registries.md` (title screen, Kokiri, Field, a dungeon, MQ dungeon, credits).
2. Hand-written two-line `rooms/0.json` delta moves an actor in Kokiri Forest.
3. A legacy Prelude mod converted through §5.4 produces a delta archive < 5 KB and plays
   identically stacked on the converted base.
4. Two delta mods touching the same room but different actors both apply.

## Status

Converter (§5 items 1–3) implemented; legacy-mod conversion (§5.4) and the merging loader (§4)
are not written yet, so SoH cannot boot from the converted archive until §4 lands.
