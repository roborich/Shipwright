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
2. The vanilla `version` file at the archive root is unchanged and still required by SoH's
   archive gate.
3. A **base** archive (one that provides vanilla scenes in the §4 form) **must** contain
   `unbound.json` (§6) at its root; its presence is how a reader detects the format and routes
   vanilla scenes to `scene.json`. A mod layer that only patches documents may omit it; its
   documents merge regardless.
4. Every path not listed in this specification is a vanilla resource and is read exactly as
   vanilla SoH reads it (last mounted archive wins, whole file).
5. Archives are mounted in layers: the base game archive, then mods in SoH's mod-load order. The
   *structured documents* of §4–§7 merge across layers (§3); everything else replaces whole.

## 2. Value conventions

These apply to every JSON document in this specification.

| Kind | Written by the converter | Accepted on read |
|---|---|---|
| Integer | JSON number | JSON number (a fractional number is truncated toward zero); a string that is, in full, an optionally signed decimal or `0x` hex integer (`"0x0F12"`, `"3858"`, `"-5"`); a JSON boolean (`true` = 1) |
| Number (may be fractional) | JSON number | JSON number; a string that is, in full, a decimal number with optional fraction/exponent, or a `0x` hex integer. Booleans are not numbers. |
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
- Keys beginning with `$` are reserved (§3).
- JSON comments (`//` and `/* */`) are accepted in every document.

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
   highest layer that provides `$order` wins. Keys not listed follow the listed ones in *key sort
   order*: keys that parse as integers first, ascending numerically, then the rest in byte order.
   A keyed list without `$order` is entirely in key sort order. `$order` is legal only on keyed
   lists (`actors`, `messages`); on a positional list it is an error (the document is rejected).
6. **`"$schema": "<type>/<version>"`** names the document type (§4–§7). It is required in at
   least one layer; the highest layer that provides it wins. Only a top-level `$schema` string
   counts.
7. A layer may omit any key, including `$schema`; only the merged document has to be complete.
8. Bulk files (`collision.bin`, display lists, textures, cutscenes, audio, objects) do not merge:
   the highest layer wins.

## 4. Scenes

### 4.1 Paths

```
scenes/<scene>/scene.json           $schema unbound/scene/1
scenes/<scene>/rooms/<n>.json       $schema unbound/room/1      n = room number, decimal
scenes/<scene>/collision.json       $schema unbound/collision/2 (1 accepted)
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
| `setups` | object keyed by setup index | Setup `"0"` is required; the document is **rejected** without it. `"0"`–`"3"` are child-day, child-night, adult-day, adult-night; `"4"` and up are cutscene setups. Every setup is **complete** — nothing is inherited from setup `"0"`. The setup with the highest index determines how many setups exist; a missing intermediate index behaves as vanilla's empty alternate header: the engine falls back to setup `"0"` (setup `"3"` first tries `"2"`). |

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
| `paths` | array of paths | pathway documents (§4.5) this setup uses |
| `lighting` | positional list of lighting entries (below) | indexable entries 0–255 (§9) |
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

- a non-negative integer: an index into the entrance table (vanilla numbering; custom entrances
  have the index from §7);
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
list; the vanilla packing of the index into `params` bits 10–15 is **not** required and is not
read for actors spawned from this list. There is no limit of 64.

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

**Light entry** — `type` 1: directional `{ type: 1, dir: direction vector, color }`; any other
`type` (vanilla 0 and 2): point light `{ type, pos: vec, color, glow: int 0–255, radius: int −32768…32767 }`.

**Actor entry** — `{ id: int, pos: vec, rot: vec, params: int }`. Keys are opaque identity
strings; the converter uses the vanilla list index as the key. `params` is the vanilla packed word
for that actor type. The number of actors per room is limited only by the live-actor cap (§9).

**Mesh object**

| `type` | Shape |
|---|---|
| `0` | `{ "type": 0, "entries": positional list of { "opa": path or null, "xlu": path or null } }` |
| `1` | `{ "type": 1, "format": 1 or 2, "opa": path or null, "xlu": path or null, "image": image }` (format 1) or `"images": positional list of image` (any format other than 1; ≤ 255 images). `image` = `{ source: path, tlut: int, width, height, fmt, siz, mode0, tlutCount: ints, unk0C: int, id: int, unk00: int }`; `id`/`unk00` are meaningful only for multi-image format. |
| `2` | `{ "type": 2, "entries": positional list of { "pos": vec, "radius": number, "opa": path or null, "xlu": path or null } }`. `radius` is a world-unit cull radius, unbounded. ≤ 1 024 entries; extra entries are not drawn (§9). |

Type-0 entry count is unbounded. An unknown `type` is logged and yields a mesh with no entries.

### 4.4 `collision.json` + `collision.bin`

```json
{
  "$schema": "unbound/collision/2",
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
| `cameras` | positional list of `{ sType: int, count: int, positionIndex: int or null }` | `positionIndex` indexes `cameraPositions`; `null` = none; an out-of-range index reads position 0 |
| `cameraPositions` | positional list of vectors | **integers −32768…32767** (a remaining vanilla limit, §9; the reader does not validate) |
| `waterBoxes` | positional list | |

**Surface type** — every field is an integer; all are unpacked (nothing is a bit-packed word):

| Field | Range | Field | Range |
|---|---|---|---|
| `camera` | index into `cameras`, unbounded | `material` | 0–15 |
| `exit` | **1-based** index into the setup's `exits` (`exit` N selects `exits["N−1"]`), unbounded; 0 = none | `floorEffect` | 0–3 |
| `floorType` | 0–31 | `lightSetting` | index into `lighting`, 0–255 |
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
| `lightSetting` | int 0–255 | index into `lighting` |
| `room` | int | room number the box belongs to; `-1` = every room. Default when absent: `-1`. |
| `notSwimmable` | 0/1 | vanilla property bit 19: the box is excluded from the swim-surface query and found only by the ripple-effect query |

Legacy form, **accepted with a warning**: `"properties": int` (the vanilla packed word), unpacked
on read; an explicit `room` beside it still wins.

#### 4.4.1 `collision.bin`

Little-endian, no header, exactly two arrays back to back. The `$schema` version of the
accompanying `collision.json` selects the layout:

| Version | Vertex | Polygon |
|---|---|---|
| `unbound/collision/2` (current) | `f32 x, y, z` (12 bytes) | `u16 type; u16 pad; u32 vA; u32 vB; u32 vC; s16 nx; s16 ny; s16 nz; s16 pad; f32 dist` (28 bytes) |
| `unbound/collision/1` (accepted) | `s16 x, y, z` (6 bytes), vertex block padded to a multiple of 4 | `u16 type; u32 vA; u32 vB; u32 vC; s16 nx; s16 ny; s16 nz; s16 dist; s16 pad` (24 bytes) |

Polygon fields: `type` indexes `surfaceTypes` (a header holds at most 65 535 surface types);
`vA`, `vB`, `vC` are vertex words: bits 0–28 the vertex index, bits 29–31 flags (`vA`: xpFlags;
`vB`: bit 29 = conveyor, bits 30–31 reserved, write 0; `vC`: reserved, write 0); `nx, ny, nz`
the unit normal scaled by 32767; `dist` the plane distance from the world origin. Vertex and
polygon counts are unbounded (indices are 29-bit). A `$schema` version other than 1 or 2 is
**rejected**.

### 4.5 `paths/<name>.json`

```json
{ "$schema": "unbound/paths/1", "paths": { "0": { "points": [ [x,y,z], … ] } } }
```

`paths` is a positional list; `points` is an array of vectors (numbers). A path holds at most
**255 points** (§9); longer paths are truncated and an error is logged.

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
  empty. The key is the message id (integer 0–65534, written as a decimal or `0x` hex string).
  `0xFFFF` is the table terminator and is **rejected**. A `null` value deletes the id (§3.2).
- `box`: textbox type 0–15; `ypos`: textbox y-position 0–15 (packed into one byte; the reader
  does not validate).
- `text`: required — an entry without a string `text` is skipped with an error. The raw message
  bytes as a JSON string in which each code point U+0000–U+00FF is one byte (control codes are
  written as JSON escapes). The message terminator byte `0x02` is appended if absent. Code points
  above U+00FF have no byte form; each becomes `?` and a warning is logged.
- The document carries no language key; the folder is the language. `$schema` is
  self-description for tools and is not otherwise interpreted.
- Ids may be added freely; a message table has no fixed size. A single message may be up to
  8 192 bytes; one decoded textbox is limited to 1 024 bytes (§9).

Vanilla text resources (`text/<lang>_message_data_static/…`) and the vanilla `override/`
mechanism are still accepted; when `text/<lang>/messages.json` exists in any layer it is the base
table for that language, the vanilla resource is not read, and `override/` entries are applied on
top of it. The folder name for English is `eng` only.

## 6. Manifest — `unbound.json`

```json
{
  "format": "unbound",
  "formatVersion": 1,
  "game": "oot",
  "source": { "romHash": "0xEC7011B7", "converter": "soh Ackbar Delta (9.2.3)" },
  "features": ["scenes", "collision", "text", "paths"],
  "requires": { "formatVersion": 1 }
}
```

| Key | Required | Meaning |
|---|---|---|
| `format` | yes (writer) | the string `"unbound"`; a reader does not interpret it |
| `formatVersion` | yes (writer) | integer; this document describes version **1**. A reader treats an absent value as 1. |
| `game` | no | `"oot"` |
| `source` | no | provenance of a converted archive; free-form |
| `features` | no | informational list of the document kinds present |
| `requires.formatVersion` | no | the minimum version a mod layer needs |

A layer whose `formatVersion` or `requires.formatVersion` is greater than the reader's version is
logged as an error and does not count as an Unbound base archive; its files are **not** removed
from the layer merge.

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
| key | — | scene id: any unique string. It is also the key under which the scene's saved flags are stored. |
| `name` | no | display name; default = the key |
| `scene` | yes | path of the scene resource: a `scene.json` (§4.2) or a vanilla-format scene resource |
| `sceneId` | no | explicit numeric scene id, **≥ 128** (`0x80`); ids below 128 are vanilla and **rejected**, as is an id already taken. Default: one more than the highest id registered so far (starting at 128), in load order. |
| `drawConfig` | no | scene draw config 0–(vanilla count − 1); out of range → rejected entry |
| `titleCardTexture` | no | path of a texture shown when an entrance has `showTitleCard` |
| `entrances` | no | keyed list; the key is the entrance id, and the entrance is addressable everywhere as `"<scene id>/<entrance id>"` |
| `entrances.*.index` | no | explicit first entrance-table index of the entrance's 4-entry layer group: **≥ 1556 and a multiple of 4**; a taken index is **rejected**. Default: the group after the highest registered so far, in load order. Needed only when a vanilla-format scene's exit list refers to the entrance by number. |
| `entrances.*.spawn` | no | index into the scene's `spawns`, 0–127; default 0 |
| `entrances.*.showTitleCard`, `continueBgm` | no | booleans/0-1; default false |
| `entrances.*.endTransition`, `startTransition` | no | transition type ints; default 2 |
| `entrances.*.layers` | reserved | not read in version 1; present → warning |

A registered entrance occupies four consecutive entrance-table entries (child-day, child-night,
adult-day, adult-night), all identical in version 1.

Vanilla scenes are always registered under their enum names (`SCENE_HYRULE_FIELD`); vanilla
entrances under theirs (`ENTR_HYRULE_FIELD_0`). Both name forms are valid exit values (§4.2).

## 8. Vanilla-format resources under Unbound

Every vanilla resource type still loads. These vanilla encodings are reinterpreted:

| Resource | Difference |
|---|---|
| Binary collision header | Polygon vertex words `u16`: bits 0–12 index, bits 13–15 flags — unpacked to the 29-bit form. `dist` is read as signed 16-bit. Surface types and water boxes are unpacked as in §4.4. |
| XML collision header | `VertexA/B/C` are plain indices when the element carries `XpFlags` (0–7) and/or `Conveyor` (0/1) attributes; otherwise they are the packed vanilla words. Surface `Data1`/`Data2` and water-box `Properties` are the packed words. |
| XML collision header, names | XML `Data1`/`Data2` are the vanilla `data0`/`data1` words (the XML names are off by one from the binary field names, as in vanilla SoH). |
| Binary/XML scene commands | Transition-actor rooms are read as signed 8-bit (`0xFF` = −1); entrance rooms as unsigned 8-bit. Positions are widened to numbers. Object lists may hold up to 1024 ids; actor and room counts are 16-bit. |
| Binary `SetMesh` | Mesh entry count stays an 8-bit field (≤ 255). The XML `PolyNum` count and JSON rooms are not limited by it. |
| Binary/XML text tables | Unchanged; may be overridden per id by `override/…` as vanilla; superseded by §5 when present. |
| Matrix resources | Stored fixed-point as vanilla; unpacked to float on load. |

## 9. Limits

Limits lifted relative to vanilla (the format imposes none of these):

| Quantity | Vanilla | Unbound |
|---|---|---|
| Scenes / entrances | 110 / 1556 fixed tables | unbounded (§7) |
| Rooms per scene | 255 (127 addressable, 32 with clear flags) | 32 767 |
| Objects per room setup | 128 | 1 024 bank slots (shared with the keep objects) |
| Actors per room; live actors | 255 / 255 (wrapping) | 65 535 / 8 192 |
| Transition actors per scene | 64 | 65 535 |
| Mesh entries per room | 255 | type 0 unbounded; type 2 ≤ 1 024; type-1 images ≤ 255 |
| Collision vertices / polygons | 8 191 / (byte budget) | 2²⁹ / unbounded |
| Dyna (moving-collision) actors, polys, verts | 50 / 16 384 / 16 384 | unbounded |
| Exits, cameras per scene (surface-type fields) | 31 / 255 | unbounded |
| Light settings per setup | 31 (surface field) | 255 (the current-setting index is a byte) |
| Water-box room | ≤ 63 | any room |
| World extent (any position) | ±32 767 | ±1 048 576 (2²⁰); f32 precision ≈ 0.06 at the edge |
| Floor "none" sentinel | −32 000 | −2 147 483 648 |
| Fog start / far plane | ~2 500 / 12 800 | world units, unbounded (lighting entry) |
| Message ids | fixed table | unbounded; message ≤ 8 192 bytes |

Limits that remain (validation targets for tools):

| Quantity | Limit | Where it comes from |
|---|---|---|
| `cameraPositions` components | −32 768…32 767 | scene camera data is still 16-bit |
| Cutscene camera points | −32 768…32 767 | cutscene command words |
| One room mesh | every vertex within ±32 767 of the room `origin`; ≤ 65 535 units across | vertices are 16-bit |
| Path points | ≤ 255 per path | path count is a byte |
| Decoded textbox | ≤ 1 024 bytes | decode buffer |
| Light settings per setup | ≤ 255 | the current light-setting index is a byte |
| Surface types per collision header | ≤ 65 535 | polygon `type` is 16-bit |
| Mesh type-2 entries per room | ≤ 1 024 | sort buffer |
| Mesh type-1 images per room | ≤ 255 | count is a byte |
| Rooms with a minimap "visited" bit | < 32 | vanilla save layout |
| Entrance layers | 4 identical per custom entrance | `layers` reserved |
| Minimap / pause map for custom scenes | none | |
| Binary `SetMesh` entries | ≤ 255 | legacy encoding only |
| Room numbers in a vanilla-format scene | −1…127 | signed byte |

## 10. Versioning

- `formatVersion` in `unbound.json` and the `/<n>` suffix of every `$schema` are the version of
  this specification. Version 1 is described here.
- A change that makes a valid version-1 archive read differently, or makes a document this text
  calls accepted be rejected, is a breaking change and requires version 2.
- Adding an optional key with a zero default, or accepting a new legacy form, is not breaking and
  is recorded here under version 1 with a note.
- A reader **must** accept every earlier `$schema` version listed as accepted in this document.
