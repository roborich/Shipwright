# Handoff to Prelude: changelog

For the Prelude of Light agent. A dated list of what changed in SoH: Unbound (branch `unbound`)
that Prelude has to emit or read differently, and what it may now
let the user build. **The contract is [`SPEC.md`](./SPEC.md)**; every entry below cites the SPEC
section that defines it, and when this page and SPEC disagree, SPEC wins. Everything in the code
is tagged `SOH [Unbound]`.

## 2026-10-10 — a base outlives the release that converted it: `baseVersion`

9.3.0-unbound0.10 refused every `oot-unbound.o2r` converted by 0.9 ("was converted by soh … (9.2.3)
unbound r1, not soh … (9.3.0) unbound r1"), although nothing a base holds had changed: the check
compared the converter's release name. That check is gone. On `unbound`; ships in the release after 0.10.

| Change | SPEC | Prelude must |
|---|---|---|
| A base is used when its `formatVersion`, its new `baseVersion` and its SoH major version match the game's, whichever release converted it. `baseVersion` is absent from every base written so far and reads as 1, so 0.9 and 0.10 bases work again. It goes up only when an older base would no longer play correctly; the release notes say when. | §6, §10.1 | Keep a project's base across Unbound updates. Ask for a new conversion only when the game reports one (the `error` event) or the release notes announce a `baseVersion` bump; never because `source.converter` differs. |
| `source.converter` is now the Unbound release (`soh 9.3.0-unbound0.11`) and is never compared. The converter's report carries the same string. | §6 | Show it, don't parse it. |

## 2026-09-28 — custom actor types: one file per type, `unbound/actors/<name>.json`

Asked for by Prelude, which keeps each actor as its own asset and had to rewrite, and track its
share of, one shared `unbound/actors.json`. The single document was never released, so it is
simply gone: SoH no longer reads it. Commits `6e6d2ac6d` and `407dc984b`, on `unbound`; not in a release.
Everything inside a type (the keys, defaults and rejections below) is unchanged.

| Change | SPEC | Prelude must |
|---|---|---|
| Each type is its own document: every `unbound/actors/**.json` declares one type, and the path between `unbound/actors/` and `.json` is its name: `unbound/actors/mymod/old_man.json` declares `mymod/old_man`. The document is what used to be the entry's value (no wrapping key). `unbound/actors.json` is not read. | §7.2 | Write one file per type; drop the shared-document writer and its bookkeeping. |
| Name rules are unchanged: not an integer string, not a name ActorDB already knows (`unbound/actors/En_Kanban.json` is rejected). A folder is **recommended, not required**: a name like `old_man` works. | §7.2 | Put a project's types under `unbound/actors/<project>/` once projects have a name; until then a bare name is valid. Placements use the full name, folder included. |
| Each path is layer-merged on its own (§3): a higher mod's file at the same path patches the type key by key, and a file that is `null` removes it. A broken or non-object file loses only its own type. | §7.2, §3 | Nothing, unless Prelude wants to offer patching another mod's type. |
| Types are numbered from 0x1000 in byte order of their names; `$order` no longer applies. | §7.2 | Nothing: the number is never written. |

## 2026-09-26 — custom actor `look` axes: `turnAxis` and `nodAxis`

Asked for by Prelude for rigs imported from `.glb` (MM Skull Kid / `Dm_Stk`, whose head X axis
points forward, so the old fixed X turn rolled the head). Commit `7119522d4`, merged
into `unbound` (`12b0b18e6`); not in a release yet; boot-verified by log, not visually checked.

| Change | SPEC | Prelude must |
|---|---|---|
| `look.turnAxis` `[x, y, z]`, default `[1, 0, 0]`: the axis in the head limb's own space the head turns about to follow the player left and right (positive `headRot.y` by the right-hand rule). `look.nodAxis`, default `[0, 0, 1]`: the axis it nods about (`headRot.x`). The head turns, then nods. `pivot` now lies along `turnAxis` (normalized), for both the turn and the focus point. | §7.2 | Measure the axes from the rest pose and write them **only when they differ from the defaults**: a build without these keys rejects the whole type (unknown key). Absent keys draw exactly as before. |
| Each axis is normalized. An axis that is not an array of three numbers, or has zero length, or two axes less than about 0.06° apart (after defaults apply), leaves the actor **without head tracking** and log an error; the type still registers and spawns, as with a bad `look.limb`. | §7.2 | Validate at export: three finite numbers, nonzero, not parallel. |

## 2026-09-26 — custom actor types: declared in `unbound/actors.json`, placed by name

A mod can now add actor types without C code, and place them, and any actor ActorDB knows, **by
name** in a room's `actors`. A **declared** type is a new actor drawn and run by the game (a
model, a collision cylinder, talking, head tracking). On `unbound`, play-tested with a Prelude
export; **not in a release yet**. Design and engine notes:
[`actors.md`](./actors.md). Reference export:
[`examples/custom-actors`](./examples/custom-actors/README.md) (every key, and every rejection).

| Change | SPEC | Prelude must |
|---|---|---|
| ~~New layer-merged registry `unbound/actors.json`, keyed by type name~~ — superseded 2026-09-28: one `unbound/actors/<name>.json` per type. The game numbers each type from 0x1000; the number is never written. A name that is an integer string or an existing actor name is rejected. | §7.2 | Author types; offer registered types in the actor palette by name. Never write a custom type's number. |
| A room actor's `id` may be a **name**: a registered type, or any name ActorDB knows (`En_Kanban`, a fork's C actor). Unknown names are skipped with an error. Names only in room `actors`; spawns and transition actors keep integers. An integer `id` must be 0–`0xFFF`: anything else is skipped. | §4.3 | Write `"id": "<name>"` for custom types; this replaces typing raw ids for fork actors too. A vanilla actor placed by name still needs its object in the room's `objects`, as by number. |
| `model`: exactly one of `skeleton` + required `animation`, or `displayList`; `frame` to hold a pose, `speed`, `scale` (must be positive), `yOffset`, `translucent`, `shadow`, `cullRadius`, `drawDistance`, `segments` 8–12, `hideLimbs`. Optional `collision` (cylinder), `talk` (`message`, `range`), `look` (`limb`, `pivot`, `range`). A declared type needs no object in the room: assets load by path. | §7.2 | Preview the skeleton in the chosen frame, or the display list. Let the user pick `look.limb` and `hideLimbs` from the skeleton's limb list, numbered from **1 at the root** in skeleton order; limb names can mislead (Ganondorf's "Jewel" limb is his head root). Set `cullRadius` to the model's reach from its origin for anything larger than an NPC. |
| A skeleton must be normal or flex with standard or LOD limbs, and its animation must have frames and cover every limb; otherwise no actor of the type spawns. | §7.2 | Validate at export, so the user is not left with an invisible type. |
| A talking type's placement `params` is its **message id** (non-zero and not 0xFFFF), else `talk.message`. `params` as an object is reserved and skips the placement. | §7.2, §4.3 | Show a message picker for `params` on talking types. Write `params` as an integer. |
| `shadow` draws on scene collision only, not on a moving platform's. | §7.2 | Nothing; mention it if the user places a shadowed actor on a platform. |
| Distances: a negative `cullRadius`, `talk.range` or `look.range` reads as 0; a zero or negative `drawDistance` reads as its default (1000). A `hideLimbs` entry past the skeleton's last limb is ignored with an error. Stated 2026-09-26 after the first draft of §7.2; nothing changes for non-negative values. | §7.2, §10 | Write non-negative distances. Offer `hideLimbs` only from the skeleton's limb list. |
| Unknown keys, at the top level or inside `model`, `collision`, `talk`, `look`, reject a type (`base`, `params`, `script` are reserved). | §7.2 | Emit only the keys listed. |
| Mod-supplied display lists, skeletons, animations and textures are ordinary resources at any path (`objects/<mod>/…`, never `alt/`). An actor model is in **model space**: `scale` converts to world units (a room-style world-unit export needs `scale` 1). | §7.2 | Export actor models centred on their origin, base on the ground or offset with `yOffset`. |

**Older builds place an extra Link.** Every release up to `unbound0.7` reads a string `id` as missing,
which is id 0, the player actor, and `requires.formatVersion` cannot stop it (SPEC §10 note). When an
export uses a name, tell the user it needs the release that adds custom actors.

## 2026-09-20 — `sound` sentinels: none is `natureAmbience: 19`, no music is `seq: 127`

From a player's crash log: four identical audio-thread crashes at the first in-game sunset in
Prelude-exported scenes. Every crashing scene had `"natureAmbience": 255`; the engine's table has ids
0–19 and reads garbage past it. `"seq": 255` appeared alongside and is equally wrong (no music is 127).

| Change | SPEC | Prelude must |
|---|---|---|
| `sound.seq` is `0..109` or `127` (none); `sound.natureAmbience` is `0..19`, `19` = none. The reader now maps anything else to the "none" value and logs it; the engine clamps too. Older readers crash on the bad byte. | §4.2 | Emit `19` for no nature ambience and `127` for no music; never `255`. A bound `song` still sits on a real vanilla `seq` (2, Hyrule Field, is a fine default). Reject out-of-range values at the UI and exporter. |

## 2026-09-19 — a custom entrance is its name; numbers never leave the game

From a report of two Prelude exports that both pinned `"index": 1556` — the first free custom
entrance number, so the obvious one for any exporter to pick. The second mod's entrance did not
register at all, leaving its scene with no way in. The same collision existed for `sceneId`, where
the loser's whole scene failed to load.

The number a custom scene or entrance gets is assigned at load and depends on which mods are mounted
and in what order. It is therefore never stable between players, and now never appears outside the
running game.

| Change | SPEC | Prelude must |
|---|---|---|
| `sceneId` and `entrances.*.index` are **ignored**, with a warning naming the scene or entrance. The game assigns both, in registry order. | §7 | Stop emitting them (already done on `unbound-entrance-numbers`). Old exports keep loading. |
| A numeric exit is **rejected** when it falls between `ENTR_MAX` (1556) and 0x7FF8 — the custom range. Vanilla indices below 1556 and the dynamic return entrances 0x7FF9–0x7FFF are unchanged. | §4.2 | Write every custom exit as `"<scene id>/<entrance id>"`. Vanilla scene exports are unaffected: grottos and fairy fountains keep their 0x7FFF. |
| The auto-assigned ceiling is 0x7FF4, not 0x7FFC: a group any higher reached into the return-entrance range, where `z_player.c` intercepts an exit before it ever indexes the table. | §7 | Nothing. |
| A save file stores `savedScene` and the three entrance fields by **name** in the `"unbound"` section. Adding or removing a mod no longer moves a saved player into another mod's scene, and a Farore's Wind warp into a scene the player no longer has is cleared rather than repointed. | — | Nothing; save-side only. |

**No format-version bump and no re-export.** An old export with `index`/`sceneId` still loads; only a
numeric custom exit — which nothing Prelude ships has ever written — is newly rejected.

## 2026-09-17 — Epona in custom scenes: what the two `horse` controls actually do

From a play-test of a three-scene mod where Epona's Song did nothing in any of them. Two separate
causes, one on each side, and both are worth knowing about before the next mod that allows her.
**No format change and no re-export**: the engine-side fix is in `EnHorse_SpawnNearPlayer`, so a mod
exported before it works as soon as the user updates SoH.

The root confusion is that `horse` reads like one feature with an optional extra, and it is not.
The song **calls a horse that already exists in the scene** — `DREG(53)` is a flag an already-spawned
`EnHorse` polls in `EnHorse_Inactive`; nothing anywhere creates one. So the permission and the idle
spot are two different features, and a scene with the permission alone has nothing to call.

| Change | SPEC | Prelude must |
|---|---|---|
| Nothing in the format. This entry is what the existing `horse` key already means, written out because the two controls look independent and are not. | §7 | Present them as one feature with two parts, not two toggles. The permission alone means "she may be ridden in, parked here, and found here again"; the idle spot is what makes **Epona's Song** work in the scene. A control that reads as an optional coordinate on an already-enabled feature will be left off by users who wanted the song. |
| `horse.pos` is a real standing position, not just a marker: when she is *parked* in the scene she is spawned there as a live actor (`params = 1`), not as the invisible placeholder. | §7 | Validate the point at export, with the same four tests `EnHorse_CalcFloorHeight` applies: a floor poly exists under it, its surface type has `isHorseBlocked == 0`, it is not below a water box, and the floor normal has `y >= 0.819` (slope ≤ 35°). A point test, at most padded by her collider radius (**20** units). Previously this page said `horse.pos` was "taken as given"; this replaces that. |
| A room with no objects must still emit `"objects": {}`. | §4.2 | Keep emitting the empty object list. The engine appends `OBJECT_HORSE` inside the `SetObjectList` command handler, and a setup key that is *absent* emits no command at all — so omitting it silently skips the injection. Low severity in practice (her bank index falls back to 0 and she survives), but it is a free thing to get right. |
| Riding through an exit into a scene without `horse` loses her at the boundary. | §7 | Warn at export when a scene with `horse` has an exit into a custom scene without it. `func_8006DC68` bails on the destination's gate, so she simply does not arrive and stays parked in the scene behind. Scenes meant to be ridden between all need the key. |

Nothing to author for call points. Where she arrives when summoned is generated at runtime from the
player's position and the camera, has no representation in the scene file, and imposes no clearance
requirement on the scene — see "A call point" in [`registries.md`](./registries.md) for why the
earlier implementation appeared to demand one.

## 2026-09-17 — scroll layers may move slower than a quarter-texel: `xSpeed` / `ySpeed`

The slowest `materialAnims` scroll was `xStep` 1, a quarter-texel per gameplay frame — too fast for
a large texture drifting over terrain. A scroll layer may now carry `xSpeed` and `ySpeed`: numbers
(may be fractional, default 0) in the same unit, **added** to `xStep`/`yStep`. Version-2 addition,
no manifest change.

| Change | SPEC | Prelude must |
|---|---|---|
| Optional per-layer `xSpeed`/`ySpeed` numbers; the layer's rate is `xStep + xSpeed`, `yStep + ySpeed` quarter-texels per gameplay frame (20 per second). `ySpeed` follows the `yStep` sign rule. | §4.2 | Let the authoring field take a decimal rate. Simplest emit: integer part in `xStep`, remainder in `xSpeed` (or `xStep` 0 and the whole rate in `xSpeed` — the same motion). Omit a speed that is 0. `xStep`/`yStep` stay ints: a fractional value there is truncated (§2). |
| The offset wraps at 32 768 quarter-texels (8192 texels) instead of 2048. | §4.2 | Nothing to emit. Power-of-two textures up to 8192 texels scroll without a jump; warn on a scrolled texture whose size is not a power of two. |
| Older readers degrade silently. | §10 | Note in the export summary that fractional scroll needs Unbound 0.7+; on 0.6 the layer moves at its integer step only (still, if that is 0). Do **not** raise `requires.formatVersion`. |
| Preview. | — | Use `xStep + xSpeed` as the rate in the `ScrollLayers` seam. |

Test data: `examples/lake-hylia-slow-water/`.

## 2026-09-16 — a scene may allow Epona: the registry's `horse` key

Vanilla hardcoded five horse scenes in `func_8006CFC0`, and that list gated the *whole* horse-spawn
pass: in any other scene Epona's Song did nothing, no idle horse appeared, and riding her through
an exit dropped her at the far side. Hand-placing an `EnHorse` worked around the first two and
never the third. The list is now seed data in the scene registry and the gate is a registry
lookup, so a custom scene opts in with `horse` in `unbound/scenes.json`. Version-2 addition, no
manifest change; an older reader ignores the key and refuses her as before.

| Change | SPEC | Prelude must |
|---|---|---|
| Optional `horse` on a scene registry entry: an object whose *presence* is the permission, with optional `pos` (`[x, y, z]`, where she waits) and `angle` (her facing there, default 0). A bare `true` allows her with no idle spot. | §7 | Add a per-scene "allow Epona" authoring toggle plus an optional idle spot; write the key only when the scene allows her. A malformed `pos` is ignored by the reader with a log line, so validate it at export. |
| `pos` is what makes Epona's Song usable: the song calls a horse that is already in the scene rather than creating one, so a scene with `"horse": true` and no `pos` only ever has her when the player rode or parked her there. | §7 | Prompt for an idle spot on any scene the user expects to summon her in; a bare `true` is for scenes you only ride through. |
| Her object is handled by the engine: a custom scene that allows her gets `OBJECT_HORSE` appended to every room's object list. | §7, §9 | Do **not** add `OBJECT_HORSE` to room object lists — a room that already lists it is left alone, but an unnecessary entry costs a bank slot in every room. |
| The parked-horse scene is saved by *name* in the `unbound` save section, so it survives id reassignment. | §7 | Nothing to emit. Warn that removing a mod parks her back in Hyrule Field. |
| Older readers degrade silently. | §10 | Note in the export summary that `horse` needs Unbound 0.7+. Do **not** raise `requires.formatVersion`. |

She stays adult-only, as in vanilla, and the idle spot should be flat, dry ground: a generated call
point is rejected for water, a slope past 35°, a horse-blocked surface or a big drop. `horse.pos`
itself is taken as given by the reader — see the 2026-09-17 entry for the checks Prelude should run
on it at export.

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
5. Validation updates per SPEC §9; keep the camera-data and per-room `Vtx` warnings. Add the
   `horse.pos` standability check and the horse-exit warning (2026-09-17).
6. Regenerate test fixtures from a fresh `soh --export-unbound` (works headless).

## How to verify against this build

- Build `unbound` (`cmake --build build-cmake --target soh -j8`), export `oot-unbound.o2r`,
  place it beside `oot.o2r`. Loader errors are prefixed `[Unbound]` and name the document and key.
- Float-matrix rendering parity has had a title-screen run only; the first thing to eyeball is
  that vanilla scenes look identical.
