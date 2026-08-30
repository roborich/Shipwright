# SoH: Unbound — archive format specification

**Format version 1.** This document is the contract. It lists every way the contents of an
`.o2r` archive read by SoH: Unbound may differ from a vanilla Ship of Harkinian archive, as seen
from outside — by a tool that writes one (Prelude of Light), a tool that reads one, or a person
inspecting the zip. It says *what* is accepted and *what it means*; it does not say how SoH
implements it or why a decision was made. Those live in the other `unbound-docs/` files and may
change; this document changes only with the format version.

Reviews of SoH: Unbound code and of Prelude's Unbound export are held to this text. A fix that
would require changing this text is a format change, not a fix.

Normative words: **must**, **must not**, **accepted** (read without error), **rejected** (the
document or archive is not loaded and an error is logged), **ignored** (read without effect).

---

## 1. Container

1. An Unbound archive is a zip file with the `.o2r` extension, exactly as vanilla. Stored
   (uncompressed) and deflated entries are both accepted.
2. The vanilla `version` file at the archive root is unchanged: when present it **must** be a
   supported ROM hash, exactly as vanilla. The game still needs one mounted archive with a valid
   `version`.
3. A **base** archive (one that provides vanilla scenes in the §4 form) **must** contain
   `unbound.json` (§6) at its root with `"scenes"` in its `features`; that is how a reader
   detects the format and routes vanilla scenes to `scene.json`. A mod layer may omit
   `unbound.json` or carry one without `"scenes"`; its documents merge regardless.
4. Every path not listed in this specification is a vanilla resource and is read exactly as
   vanilla SoH reads it (last mounted archive wins, whole file).
5. Archives are mounted in layers, lowest first: SoH's own archive, the vanilla game archives,
   the Unbound base archive (`oot-unbound.o2r`), then mods in SoH's mod-load order. The
   *structured documents* of §4–§7 merge across layers (§3); everything else replaces whole.
6. Once a base is mounted, every vanilla scene is read from `scenes/<scene>/scene.json` (§4.1);
   vanilla-format scene resources in any layer are not read.

## 2. Value conventions

These apply to every JSON document in this specification.

| Kind | Written by the converter | Accepted on read |
|---|---|---|
| Integer | JSON number | JSON number (a fractional number is truncated toward zero); a string that is, in full and without whitespace, an optionally signed decimal or `0x` hex integer (`"0x0F12"`, `"3858"`, `"-5"`); a JSON boolean (`true` = 1) |
| Number (may be fractional) | JSON number | JSON number; a string that is, in full and without whitespace, a decimal number with optional fraction/exponent, or a `0x` hex integer. Booleans are not numbers. |
| Vector | `[x, y, z]` of numbers | 3-element array; extra elements ignored; fewer than 3 → the whole vector is `[0,0,0]` |
| Colour | `[r, g, b]` integers 0–255 | 3-element array |
| Path | string, forward slashes, archive-relative, no leading slash | string |
| Key of a **keyed list** | string | any unique string |
| Key of a **positional list** | decimal index string `"0"`, `"1"`, … | decimal index strings; the set **must** be contiguous from `"0"` after merging (§3.2) |

- Position vectors (`pos`, `origin`, vertices, bounds, water-box extents, path points, point
  lights) are **numbers** and may be fractional. Rotation vectors (`rot`) are integers in the
  vanilla binary-angle unit (−32768…32767). Direction vectors (`dir`, `light1Dir`, `light2Dir`)
  are integers −128…127.
- A missing scalar key takes the zero/empty default of its type unless this document states
  another default (the non-zero defaults are: `time.hour/minute/increment` 255, `mesh.format` 1,
  `water box.room` −1, registry `endTransition`/`startTransition` 2). A key whose value has the
  wrong JSON type is treated as missing.
- Ranges stated in this document are requirements on the **writer**. Unless a rule says
  "rejected", the reader does not validate them: an out-of-range value is stored in the engine
  field's width and wraps.
- Unknown keys are ignored.
- Keys beginning with `$` are reserved (§3); an unknown `$` key is ignored.
- JSON comments (`//` and `/* */`) are accepted in every document. The first byte of a §4
  document **must** be `{` — no leading whitespace, comment, or byte-order mark.

## 3. Layering and merge rules

Applies to every path in §4–§7. For a given path, the documents from all mounted archives are
merged, lowest layer first:

1. **Objects** merge key-wise, recursively. A later layer's value for a key replaces the earlier
   value; an object value merges into an object value.
2. **`null`** deletes the key. In a positional list a deletion is legal only at the tail: if the
   merged result has a hole (`"0"`, `"1"`, `"3"`), the document is **rejected**.
   A layer that is not parsable JSON is skipped with an error; the remaining layers still merge.
3. **Arrays** replace whole (vectors, colours, `$order`, `paths`).
4. **`"$replace": true`** inside an object means "discard every lower layer's value of this
   object"; the key is removed from the merged result.
5. **`"$order": [keys…]`** on a keyed list gives the engine order of the listed keys. The
   highest layer that provides `$order` wins. A key listed more than once counts once; a listed
   key that does not exist is ignored. Keys not listed follow the listed ones in *key sort
   order*: keys that are optionally signed decimal integers first, ascending numerically, then
   the rest in byte order. A keyed list without `$order` is entirely in key sort order. `$order`
   is legal only on keyed lists (`actors`, the registry and its `entrances`; on `messages` it has
   no effect); on a positional list the document is **rejected**.
6. **`"$schema": "<type>/<version>"`** names the document type. A §4 document is **rejected**
   unless at least one layer provides it; the highest layer that provides it wins, and only a
   top-level `$schema` string counts. A version this document does not list as accepted is
   **rejected**. A §5 document carries `$schema` as self-description; the reader does not
   validate it. §6 and §7 documents carry none.
7. A layer may omit any key, including `$schema`; only the merged document has to be complete.
8. Bulk files (`collision.bin`, display lists, textures, cutscenes, audio, objects) do not merge:
   the highest layer wins.

## 4. Scenes

### 4.1 Paths

```
scenes/<scene>/scene.json           $schema unbound/scene/1
scenes/<scene>/rooms/<n>.json       $schema unbound/room/1      n = room number, decimal
scenes/<scene>/collision.json       $schema unbound/collision/3 (1, 2 accepted)
scenes/<scene>/collision.bin        bulk, referenced from collision.json
scenes/<scene>/paths/<name>.json    $schema unbound/paths/1
```

`<scene>` for a vanilla scene is the vanilla scene file name without `_scene` (`spot00`, `ydan`);
Master Quest variants are `<scene>_mq`. A custom scene may use any directory name that does not
collide with a vanilla one; it is reached only through the registry (§7).

Bulk resources a scene document points at (display lists, textures, cutscenes) keep the paths
they had in the archive the scene was converted from; this specification does not rename them.

### 4.2 `scene.json`

```json
{
  "$schema": "unbound/scene/1",
  "collision": "scenes/spot00/collision.json",
  "rooms": { "0": "scenes/spot00/rooms/0.json" },
  "setups": { "0": { … }, "1": { … } }
}
```

| Key | Type | Meaning |
|---|---|---|
| `collision` | path | The scene's `collision.json`. A scene without one has no collision. |
| `rooms` | positional list of paths | Room documents in room-number order. A scene without rooms draws nothing. |
| `setups` | object keyed by setup index | Setup `"0"` is required; the document is **rejected** without it. `"0"`–`"3"` are child-day, child-night, adult-day, adult-night; `"4"` and up are cutscene setups. Every setup is **complete** — nothing is inherited from setup `"0"`. The setup with the highest index determines how many setups exist; a missing intermediate index, or an index beyond the highest, behaves as vanilla's empty alternate header: the engine falls back to setup `"0"` (setup `"3"` first tries `"2"`). Keys are decimal integers; other keys are ignored. |

Every key of a setup is optional. An absent key means the corresponding scene command is not
emitted and the engine keeps its default state, exactly as a vanilla scene that lacks the command.

A setup object holds:

| Key | Type | Meaning |
|---|---|---|
| `specialObjects` | `{ elfMessage: int, globalObject: int }` | Navi hint id, global object id |
| `skybox` | `{ id, weather, indoors, unk }` ints | vanilla skybox settings |
| `sound` | `{ seq, natureAmbience, reverb }` ints | vanilla sound settings |
| `cameraSettings` | `{ cameraMovement, worldMapArea }` ints | vanilla camera settings |
| `cutscene` | path | cutscene resource |
| `paths` | array of paths | pathway documents (§4.5); their `paths` lists are concatenated in array order, and a path index is a position in that concatenation |
| `lighting` | positional list of lighting entries (below) | ≤ 255 entries (§9); a `lightSetting` index at or beyond the count selects entry `"0"` |
| `entrances` | positional list of `{ spawn: int 0–255, room: int }` | `spawn` indexes `spawns`; `room` is a room number |
| `spawns` | positional list of `{ id: int, pos: vec, rot: vec, params: int }` | player spawn entries; `params` is the vanilla packed word |
| `exits` | positional list of exit values (below) | referenced 1-based by surface types (§4.4) |
| `transitionActors` | positional list of transition-actor entries (below) | |

The vanilla `SetCsCamera` command (0x02) carries no data in SoH and has no JSON form.

**Lighting entry**

| Key | Type | Meaning |
|---|---|---|
| `ambient`, `light1Color`, `light2Color`, `fogColor` | colour | |
| `light1Dir`, `light2Dir` | direction vector | |
| `fogNear` | int 0–1000 | vanilla fog-near value, **without** the blend-rate bits (the reader keeps the low 10 bits) |
| `fogBlendRate` | int 0–63 | the vanilla high six bits of the packed fog-near word (the reader keeps the low 6 bits) |
| `fogFar` | int | vanilla fog-far / far-plane value |
| `fogStart`, `fogEnd`, `drawDistance`, `nearPlane` | number, optional | **World-unit fog.** If any of the first three is present the entry uses world-unit fog: fog is fully clear at `fogStart` and fully dense at `fogEnd`, geometry is drawn to `drawDistance`, and the near plane is `nearPlane` (0 = the vanilla 10). Defaults when only some are present: `drawDistance` ← `fogFar` (or 12 800 if `fogFar` is 0); `fogEnd` ← `drawDistance`; `fogStart` ← the distance the vanilla `fogNear` would have produced; `nearPlane` ← 0. Absent all three, the entry has vanilla fog and vanilla limits (fog cannot start past ~2 500 units, far plane ≤ 12 800). |

**Exit value** — one of:

- a non-negative JSON integer (the §2 boolean and fractional forms are not accepted here): an
  index into the entrance table (vanilla numbering; custom entrances have the index from §7);
- a string naming an entrance: a vanilla entrance enum name (`"ENTR_HYRULE_FIELD_0"`) or a custom
  entrance `"<scene id>/<entrance id>"` from §7. A string that is not a registered name and not
  an integer in the §2 string form makes the document **rejected**.

Any other JSON type makes the document **rejected**.

**Transition-actor entry**

```json
{ "id": 9, "pos": [0, 0, 0], "rotY": 0, "params": 0,
  "front": { "room": 0, "effects": 0 }, "back": { "room": 1, "effects": 0 } }
```

`room` is a room number, `-1` = none. Transition actors are identified by their position in this
list; the index is not read from `params` for actors spawned from this list, and `params` bits
10–15 **must** be 0. `id` is 13-bit. There is no limit of 64.

### 4.3 `rooms/<n>.json`

```json
{
  "$schema": "unbound/room/1",
  "origin": [0, 0, 0],
  "setups": { "0": { … } }
}
```

| Key | Type | Meaning |
|---|---|---|
| `origin` | vector, optional, default `[0,0,0]` | The room's mesh vertices are relative to this world position. Everything else in the room (collision, actors, lights, paths) is absolute. |
| `setups` | as in §4.2 | Every setup complete. |

A room setup holds:

Every key is optional (as in §4.2, an absent key emits no command).

| Key | Type | Meaning |
|---|---|---|
| `behavior` | `{ gameplayFlags, gameplayFlags2 }` ints | |
| `echo` | int | |
| `time` | `{ hour, minute, increment }` ints | default 255 = unset |
| `skyboxModifier` | `{ skyboxDisabled, sunMoonDisabled }` ints | |
| `wind` | `{ west, vertical, south, speed }` ints | |
| `objects` | positional list of object ids | The object bank has 1 024 slots shared with the 2–3 always-loaded keep objects; entries that do not fit are dropped with an error. Any id is accepted, including ids beyond the vanilla object table (the engine loads nothing for them; actor code names its own assets). |
| `lights` | positional list of light entries (below) | |
| `actors` | **keyed list** of actor entries (below) | spawn order = `$order`, then key sort order |
| `mesh` | mesh object (below) | |

**Light entry** — `type` 1: directional `{ type: 1, dir: direction vector, color }`; `type` 0
or 2: point light `{ type, pos: vec, color, glow: int 0–255, radius: int −32768…32767 }`. Any
other `type` makes the document **rejected**.

**Actor entry** — `{ id: int, pos: vec, rot: vec, params: int }`. Keys are opaque identity
strings; the converter uses the vanilla list index as the key. `params` is the vanilla packed word
for that actor type. The number of actors per room is limited only by the live-actor cap (§9).

**Mesh object**

| `type` | Shape |
|---|---|
| `0` | `{ "type": 0, "entries": positional list of { "opa": path or null, "xlu": path or null } }` |
| `1` | `{ "type": 1, "format": 1 or 2, "opa": path or null, "xlu": path or null, "image": image }` (format 1) or `"images": positional list of image` (format 2; ≤ 255 images, more → the document is **rejected**). `image` = `{ source: path, tlut: int, width, height, fmt, siz, mode0, tlutCount: ints, unk0C: int, id: int, unk00: int }`; `id`/`unk00` are meaningful only for format 2. Any other `format` makes the document **rejected**. |
| `2` | `{ "type": 2, "entries": positional list of { "pos": vec, "radius": number, "opa": path or null, "xlu": path or null } }`. `radius` is a world-unit cull radius, unbounded. ≤ 1 024 entries; extra entries are not drawn (§9). |

Type-0 entry count is unbounded. Any other `type` makes the document **rejected**.

### 4.4 `collision.json` + `collision.bin`

```json
{
  "$schema": "unbound/collision/3",
  "bounds": { "min": [x,y,z], "max": [x,y,z] },
  "bulk": { "file": "scenes/spot00/collision.bin", "vertices": 1832, "polys": 2410 },
  "surfaceTypes": { "0": { … } },
  "cameras": { "0": { "sType": 1, "count": 0, "positionIndex": null } },
  "cameraPositions": { "0": [x,y,z] },
  "waterBoxes": { "0": { … } }
}
```

| Key | Type | Meaning |
|---|---|---|
| `bounds.min`, `bounds.max` | vector | world-unit collision bounds |
| `bulk.file` | path | the `collision.bin` (highest layer wins, never merged) |
| `bulk.vertices`, `bulk.polys` | int | counts in the bulk file; the file **must** be at least the size §4.4.1 implies |
| `surfaceTypes` | positional list | referenced by polygon `type` |
| `cameras` | positional list of `{ sType: int, count: int, positionIndex: int or null }` | `positionIndex` is the first of `count` consecutive `cameraPositions` entries the camera uses (position, rotation, fov/flags triples, as vanilla); `null` or a negative value = none; an index at or beyond the list reads `[0,0,0]`. `positionIndex + count` **must not** exceed the list. `sType` is 16-bit; `count` is a signed 16-bit value |
| `cameraPositions` | positional list of vectors | **integers −32768…32767** (a remaining vanilla limit, §9; the reader does not validate) |
| `waterBoxes` | positional list | ≤ 65 535 entries; more → the document is **rejected** |

**Surface type** — every field is an integer; all are unpacked (nothing is a bit-packed word):

| Field | Range | Field | Range |
|---|---|---|---|
| `camera` | index into `cameras`, unbounded | `material` | 0–15 |
| `exit` | **1-based** index into the setup's `exits` (`exit` N selects `exits["N−1"]`), unbounded; 0 = none | `floorEffect` | 0–3 |
| `floorType` | 0–31 | `lightSetting` | index into `lighting` (§4.2) |
| `wallFlags` | 0–7 | `echo` | 0–63 |
| `wallType` | 0–31 | `canHookshot` | 0/1 |
| `floorProperty` | 0–15 | `conveyorSpeed` | 0–7 |
| `isSoft` | 0/1 | `conveyorDirection` | 0–63 |
| `isHorseBlocked` | 0/1 | `isWallDamage` | 0/1 |

Legacy form, **accepted with a warning**: `{ "data0": int, "data1": int }` — the vanilla packed
words, unpacked on read.

**Water box**

| Field | Type | Meaning |
|---|---|---|
| `xMin`, `ySurface`, `zMin`, `xLength`, `zLength` | number | world units, unbounded |
| `camera` | int | index into `cameras` |
| `lightSetting` | int 0–254 | index into `lighting`; 31 = none (the vanilla sentinel, read as 0) |
| `room` | int | room number the box belongs to; `-1` = every room. Default when absent: `-1`. |
| `notSwimmable` | 0/1 | vanilla property bit 19: the box is excluded from the swim-surface query and found only by the ripple-effect query |

Legacy form, **accepted with a warning**: `"properties": int` (the vanilla packed word), unpacked
on read; an explicit `room` beside it still wins, and any other explicit field beside it is
ignored.

#### 4.4.1 `collision.bin`

Little-endian, no header, exactly two arrays back to back. The `$schema` version of the
accompanying `collision.json` selects the layout:

| Version | Vertex | Polygon |
|---|---|---|
| `unbound/collision/3` (current) | `s32 x, y, z` (12 bytes) | `u16 type; u16 pad; u32 vA; u32 vB; u32 vC; s16 nx; s16 ny; s16 nz; s16 pad; s32 dist` (28 bytes) |
| `unbound/collision/2` (accepted) | `f32 x, y, z` (12 bytes) | `u16 type; u16 pad; u32 vA; u32 vB; u32 vC; s16 nx; s16 ny; s16 nz; s16 pad; f32 dist` (28 bytes) |
| `unbound/collision/1` (accepted) | `s16 x, y, z` (6 bytes), vertex block padded to a multiple of 4 | `u16 type; u32 vA; u32 vB; u32 vC; s16 nx; s16 ny; s16 nz; s16 dist; s16 pad` (24 bytes) |

Version 3 is 2 with the two floating-point fields made integral; the record sizes are identical.
Collision is integral so that a scene's geometry means exactly what its author placed — an editor
snapping to whole units gets back what it wrote, with no seam where two surfaces that should meet
are a fraction apart. A version-2 document still loads; its values are **rounded**, not truncated,
so a vertex written as `99.9999` becomes `100`.

`dist` is derived from a unit normal, so it is fractional even when every vertex is integral. It
is rounded, which displaces a plane by at most half a unit — exactly what vanilla did when it
stored `dist` in an `s16`.

Polygon fields: `type` indexes `surfaceTypes` (a header holds at most 65 535 surface types);
`vA`, `vB`, `vC` are vertex words: bits 0–28 the vertex index, bits 29–31 flags (`vA`: xpFlags;
`vB`: bit 29 = conveyor, bits 30–31 reserved, write 0; `vC`: reserved, write 0); `nx, ny, nz`
the unit normal scaled by 32767; `dist` the plane distance from the world origin. Vertex and
polygon counts are unbounded (indices are 29-bit); bytes past the declared counts are ignored.
A `$schema` whose type is not `unbound/collision` or whose version is not 1, 2 or 3 is
**rejected**.

### 4.5 `paths/<name>.json`

```json
{ "$schema": "unbound/paths/1", "paths": { "0": { "points": [ [x,y,z], … ] } } }
```

`paths` is a positional list; `points` is an array of vectors (numbers). A path holds at most
**255 points** (§9); longer paths are truncated and an error is logged. `<name>` is any file
name; a scene refers to the document by its full path (§4.2).

## 5. Text

```
text/<lang>/messages.json          $schema unbound/text/1     lang ∈ eng, ger, fra, jpn, staff
```

```json
{
  "$schema": "unbound/text/1",
  "messages": {
    "0x0F12": { "box": 0, "ypos": 0, "text": "…" },
    "0x0071": null
  }
}
```

- `messages` **must** be an object (keyed list); any other type is an error and the table is
  empty. The key is the message id (integer 0–65534, written as a decimal or `0x` hex string; the
  converter writes `0x` and four upper-case hex digits). `0xFFFF` is the table terminator; an
  entry whose key is `0xFFFF`, negative, above 65534, or not an integer is skipped with an error.
  A `null` value deletes the id (§3.2). An entry that is neither an object nor `null` is skipped.
- `box`: textbox type 0–15 (high nibble); `ypos`: textbox y-position 0–15 (low nibble); the
  reader does not validate.
- `text`: required — an entry without a string `text` is skipped with an error. The raw message
  bytes as a JSON string in which each code point U+0000–U+00FF is one byte (control codes are
  written as JSON escapes). The message terminator byte `0x02` is appended if absent. Code points
  above U+00FF have no byte form; each becomes `?` and a warning is logged.
- The document carries no language key; the folder is the language. `$schema` is
  self-description for tools and is not otherwise interpreted.
- Ids may be added freely; a message table has no fixed size. A single message may be up to
  8 192 bytes; a longer one is truncated with an error. One decoded textbox is limited to
  1 024 bytes (§9).

Vanilla text resources (`text/<lang>_message_data_static/…`) and the vanilla `override/`
mechanism are still accepted; when `text/<lang>/messages.json` exists in any layer it is the base
table for that language, the vanilla resource is not read, and `override/` entries are applied on
top of it. The folder name for English is `eng` only.

## 6. Manifest — `unbound.json`

```json
{
  "format": "unbound",
  "formatVersion": 2,
  "game": "oot",
  "source": { "romHash": "0xEC7011B7", "converter": "soh Ackbar Delta (9.2.3)" },
  "features": ["scenes", "collision", "text", "paths"],
  "requires": { "formatVersion": 2 }
}
```

| Key | Required | Meaning |
|---|---|---|
| `format` | yes (writer) | the string `"unbound"`; a reader does not interpret it |
| `formatVersion` | yes (writer) | integer; this document describes version **2**. A reader treats an absent value as 1. |
| `game` | no | `"oot"` |
| `source` | no | provenance of a converted archive; free-form |
| `features` | base: yes | list of the document kinds the layer provides. A layer whose `features` contains `"scenes"` is a base archive (§1.3); a layer that does not provide every vanilla scene **must not** list it. |
| `requires.formatVersion` | no | the minimum reader version the layer needs; default = `formatVersion` |

Manifests are read per layer, not merged. A layer whose `formatVersion` or
`requires.formatVersion` is greater than the reader's version, or whose manifest is not a JSON
object, is logged as an error and does not count as an Unbound base archive; its files are **not**
removed from the layer merge.

## 7. Scene and entrance registry — `unbound/scenes.json`

One layer-merged document (§3), keyed by scene id:

```json
{
  "mymod/lava_temple": {
    "name": "Lava Temple",
    "scene": "scenes/mymod/lava_temple/scene.json",
    "sceneId": 200,
    "drawConfig": 0,
    "titleCardTexture": "textures/mymod/lava_temple_title",
    "entrances": {
      "main": { "index": 1560, "spawn": 0, "showTitleCard": true, "continueBgm": false,
                "endTransition": 2, "startTransition": 2 }
    }
  }
}
```

| Key | Required | Type / meaning |
|---|---|---|
| key | — | scene id: any unique, non-empty string that is not a vanilla scene enum name. It is also the key under which the scene's saved flags are stored. An entry that is not an object is ignored. |
| `name` | no | display name; default = the key |
| `scene` | yes | path of the scene resource: a `scene.json` (§4.2) or a vanilla-format scene resource. An entry without it is **rejected**. |
| `sceneId` | no | explicit numeric scene id, **128–32 767**; a value outside that range is **rejected**, as is an id already taken. Default: one more than the highest id registered so far (starting at 128), in registry order. |
| `drawConfig` | no | scene draw config 0–(vanilla count − 1); out of range → rejected entry |
| `titleCardTexture` | no | path of a texture shown when an entrance has `showTitleCard` |
| `entrances` | no | keyed list; the key is the entrance id, and the entrance is addressable everywhere as `"<scene id>/<entrance id>"`. A rejected entrance does not reject its scene. |
| `entrances.*.index` | no | explicit first entrance-table index of the entrance's 4-entry layer group: **≥ 1556, ≤ 32 764, and a multiple of 4**; a value outside that range or a taken index is **rejected**. Default: the group after the highest registered so far, in registry order. Needed only when a vanilla-format scene's exit list refers to the entrance by number. |
| `entrances.*.spawn` | no | index into the scene's `spawns`, 0–127; default 0 |
| `entrances.*.showTitleCard`, `continueBgm` | no | booleans/0-1; default false |
| `entrances.*.endTransition`, `startTransition` | no | transition type ints; default 2 |
| `entrances.*.layers` | reserved | not read in version 1; present → warning |

A registered entrance occupies four consecutive entrance-table entries (child-day, child-night,
adult-day, adult-night), all identical in version 1.

Entries are registered in the merged document's key order (§3.5): `$order` first, then key sort
order. Rejected entries are skipped; the remaining entries still register.

Vanilla scenes are always registered under their enum names (`SCENE_HYRULE_FIELD`); vanilla
entrances under theirs (`ENTR_HYRULE_FIELD_0`). Both name forms are valid exit values (§4.2).

## 8. Vanilla-format resources under Unbound

Every vanilla resource type still loads. These vanilla encodings are reinterpreted:

| Resource | Difference |
|---|---|
| Binary collision header | Polygon vertex words `u16`: bits 0–12 index, bits 13–15 flags — unpacked to the 29-bit form. `dist` is read as signed 16-bit. Surface types and water boxes are unpacked as in §4.4. |
| XML collision header | `VertexA/B/C` are plain indices when the element carries `XpFlags` (0–7) and/or `Conveyor` (0/1) attributes; otherwise they are the packed vanilla words. Surface `Data1`/`Data2` and water-box `Properties` are the packed words. |
| XML collision header, names | XML `Data1`/`Data2` are the vanilla `data0`/`data1` words (the XML names are off by one from the binary field names, as in vanilla SoH). |
| Binary/XML scene commands | Binary transition-actor rooms are read as signed 8-bit (`0xFF` = −1) and entrance rooms as unsigned 8-bit; XML room attributes are read as signed integers. Positions are widened to numbers. Object lists may hold up to 1024 ids; actor and room counts are 16-bit. |
| Binary `SetMesh` | Mesh entry count stays an 8-bit field (≤ 255). The XML `PolyNum` count and JSON rooms are not limited by it. |
| Binary/XML text tables | Unchanged; may be overridden per id by `override/…` as vanilla; superseded by §5 when present. |
| Matrix resources | Stored fixed-point as vanilla; unpacked to float on load. |
| Vertex resources (v0) | Stored as vanilla: 16-byte records with `s16` positions. Positions are widened to `s32` on load. See §8.1 for the wider v1 form. |

### 8.1 Vertex resource v1 — `s32` positions

Room meshes draw under the identity matrix, so their vertices are world coordinates. The vanilla
vertex record stores them as `s16`, which caps one mesh at 65 535 units across however large the
world is. Version 2 adds a second encoding of the same resource.

A **vertex resource** is registered under two versions, selected by the version field of the
resource header. A reader **must** support both.

| Version | Record | Positions |
|---|---|---|
| 0 | 16 bytes | `s16` — the vanilla form; still what the converter passes through |
| 1 | 22 bytes | `s32` |

Version 1 record, in order, little-endian, **not padded**:

| Field | Type | Bytes |
|---|---|---|
| `x`, `y`, `z` | `s32` × 3 | 12 |
| `flag` | `u16` | 2 |
| `s`, `t` | `s16` × 2 | 4 |
| `r`, `g`, `b`, `a` | `u8` × 4 | 4 |

Both versions are preceded by a `u32` vertex count, as in vanilla.

**Offsets into a vertex resource are byte offsets, in units of that resource's own record size.**
An exported display list addresses a vertex group by the byte distance from the start of the
resource, so the divisor that recovers an element index is 16 for v0 and 22 for v1 — and is
*never* the reader's in-memory vertex struct, which is padded and may be wider still. A writer
must compute offsets against the record size of the version it is emitting.

A writer **should** emit v1 only for a mesh that needs it; v0 is smaller and every vanilla mesh
fits it. A layer that emits any v1 vertex resource **must** declare `formatVersion` 2.

**Boundary.** This applies to vertices reached through a vertex resource, which is how room meshes
and object display lists are addressed. Vertices reached through a *segment* — the Skin system's
runtime buffer, and the display lists that read it — are always the vanilla 16-byte form, because
there is no resource to carry a record size. A v1 mesh therefore cannot back a skinned limb.

## 9. Limits

Limits lifted relative to vanilla (the format imposes none of these):

| Quantity | Vanilla | Unbound |
|---|---|---|
| Scenes / entrances | 110 / 1556 fixed tables | registry (§7); ids and indices ≤ 32 767 |
| Rooms per scene | 255 (127 addressable, 32 with clear flags) | 32 767 |
| Objects per room setup | 128 | 1 024 bank slots (shared with the keep objects) |
| Actors per room; live actors | 255 / 255 (wrapping) | 65 535 / 8 192 |
| Transition actors per scene | 64 | 32 767 |
| Mesh entries per room | 255 (type 2: 64 drawn) | type 0 unbounded; type 2 ≤ 1 024; type-1 images ≤ 255 |
| Collision vertices / polygons | 8 191 / 32 767 | 2²⁹ / unbounded |
| Dyna (moving-collision) actors, polys, verts | 50 / 512 / 512 | unbounded |
| Exits, cameras per scene (surface-type fields) | 31 / 255 | unbounded |
| Light settings per setup | 31 (surface field) | 255 |
| Water-box room | ≤ 63 | any room |
| World extent (any position) | ±32 760 | f32; positions within ±1 048 576 (2²⁰) keep a precision of 0.0625 or better |
| One room mesh | every vertex within ±32 767 of the room `origin`; ≤ 65 535 units across | `s32` vertices; a room mesh may span the whole world extent |
| Floor "none" sentinel | −32 000 | −2 147 483 648 |
| Fog start / far plane | ~2 500 / 12 800 | world units, unbounded (lighting entry) |
| Message ids | fixed table | unbounded; message ≤ 8 192 bytes |

Limits that remain (validation targets for tools):

| Quantity | Limit | Where it comes from |
|---|---|---|
| `cameraPositions` components | −32 768…32 767 | scene camera data is still 16-bit |
| Cutscene camera points | −32 768…32 767 | cutscene command words |
| Path points | ≤ 255 per path | the point count of a path is a byte |
| Decoded textbox | ≤ 1 024 bytes | decode buffer; not guarded |
| Light settings per setup | ≤ 255 | the light-setting count and index are bytes |
| Surface types per collision header | ≤ 65 535 | polygon `type` is 16-bit |
| Water boxes per collision header | ≤ 65 535 | count is 16-bit |
| Scene ids; entrance indices | ≤ 32 767 | entrance table and exit list are signed 16-bit |
| Transition actors per scene | ≤ 32 767 | the actor's list index is signed 16-bit |
| Mesh type-2 entries per room | ≤ 1 024 | sort buffer |
| Mesh type-1 images per room | ≤ 255 | count is a byte |
| Rooms with a minimap "visited" bit | < 32 | vanilla save layout |
| Entrance layers | 4 identical per custom entrance | `layers` reserved |
| Minimap / pause map for custom scenes | none | |
| Binary `SetMesh` entries | ≤ 255 | legacy encoding only |
| Room numbers in a vanilla-format scene | −1…127 | signed byte |

## 10. Versioning

- `formatVersion` in `unbound.json` and the `/<n>` suffix of every `$schema` are the version of
  this specification. Version 2 is described here.
- **Version 2** adds the v1 vertex resource (§8.1) and nothing else. No document changed, so every
  `/1` `$schema` remains valid and version-1 archives read identically. The bump exists so that a
  layer using the wider vertex form can say so in `requires.formatVersion`.
- A change that makes a valid version-1 archive read differently, or makes a document this text
  calls accepted be rejected, is a breaking change and requires version 2.
- Adding an optional key with a zero default, or accepting a new legacy form, is not breaking and
  is recorded here under version 1.
- A reader **must** accept every earlier `$schema` version listed as accepted in this document.
