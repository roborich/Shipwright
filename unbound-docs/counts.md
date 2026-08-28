# Unbound: object, actor, room and mesh counts

The small width lifts: how many objects a scene may list, how many actors a room header and the
live world may hold, how many rooms a scene has, and how many display lists a room mesh may carry.
Overview in [`README.md`](./README.md).

## What was capped, and what actually enforced it

| Cap | Where | Value | Real effect on SoH |
|---|---|---|---|
| Objects per scene | `OBJECT_EXCHANGE_BANK_MAX` (`z64object.h`), `ObjectContext.num` u8, `Actor.objBankIndex` s8 | 128 / 255 / 127 | Extra objects were **silently dropped** in `Scene_CommandObjectList` (`z_scene_otr.cpp`). |
| Object "space" | fixed ~1 MB arena in `Object_InitBank` | 1 024 000 B | **Vestigial.** On PC objects are resolved by asset name; `OTRfunc_800982FC` only records the id, `Object_UpdateBank` only flips its sign, and every `gObjectTable` size is 0. Nothing is copied into the arena. |
| Object ids | `gObjectTable[OBJECT_ID_MAX]` static | 0x192 | `Object_Spawn` / `func_800981B8` read `gObjectTable[id]` unguarded, so an id past the table was an out-of-bounds read. That is the *only* thing that stopped a mod from using a new object id. |
| Actors per room header | `PlayState.numSetupActors` u8 (importer already read u32, then truncated) | 255 | Silent truncation. |
| Live actors | `ACTOR_NUMBER_MAX` 2000, but `ActorContext.total` was **u8** | 255, wrapping | The `> ACTOR_NUMBER_MAX` check in `Actor_Spawn` could never fire; the count wrapped at 256. |
| Rooms per scene | `PlayState.numRooms` u8 | 255 | `assert(roomNum < numRooms)`. |
| DLs per room mesh | `PolygonType0/2.num` u8 | 255 | Binary format stores a u8. |
| Sorted (type-2) mesh entries | `SHAPE_SORT_MAX` in `z_room.c` | 64 | `assert` |
| Texture cache | `TEXTURE_CACHE_MAX_SIZE` in libultraship `interpreter.cpp` | 1 024 | Thrash, not crash. |

## What changes

- `OBJECT_EXCHANGE_BANK_MAX` 128 → **1024**; `ObjectContext.num/unk_09/mainKeepIndex/subKeepIndex`
  u8 → u16; `Actor.objBankIndex` s8 → s16. Overflow now logs (`[Unbound] object list exceeds the
  bank`) instead of dropping silently.
- `gObjectTable[id]` reads in `Object_Spawn` / `func_800981B8` are guarded by
  `id < gObjectTableSize`; ids beyond the vanilla table get size 0, which is what every vanilla
  object already has on PC. **A mod can use any object id ≥ 0x192 today**: put it in the scene's
  object list, reference it from `ActorDB` entries, and ship its assets under any path the actor
  code names. There is no object registry file because nothing on the game side needs one.
- `numSetupActors`, `numRooms` u8 → u16 (`z64.h`).
- `ActorContext.total` u8 → u16 and `ACTOR_NUMBER_MAX` 2000 → **8192**, so the cap is real and
  generous.
- `PolygonType0/2.num` u8 → u32 (`z64.h` and the `SetMesh` resource mirror). The binary `SetMesh`
  reader still reads a u8 count (cast, no sign bug); the XML reader (`PolyNum` attribute) can
  exceed 255 now. The binary format limit goes away with the scene-format redesign.
- `SHAPE_SORT_MAX` 64 → 1024 (stack array in `func_80095D04`, ~24 KB; fine on PC).
- `TEXTURE_CACHE_MAX_SIZE` 1024 → 8192 (libultraship fork).

## Not changed

- Room numbers are now `s16` (`Room.num`, `Actor.room`, `TransitionActorEntry.sides[].room`,
  `EntranceEntry.room`, the save-side `RespawnData.roomIndex` / `SohStats.roomNum` /
  `SceneTimestamp.room`, and the narrow locals in `z_play.c`, `z_actor.c`, En_Ru1, Bg_Relay_Objects,
  Door_Shutter, Bg_Mori_Idomizu, plus the port-layer readers — `valueViewer` type tag, Anchor
  `TeleportTo`, `EnemyRandomizer`, `Warping`). The binary transition/entrance loaders read the room
  byte **signed** so vanilla's `0xFF` "no room" still arrives as `-1` (it used to be an accident of
  the s8 truncation). The `s8` sweep alone was not enough — room-keyed *state* capped earlier:
  - **Clear / temp-clear flags** were `1 << room` on a `u32`. Rooms ≥ 32 now go through
    `SceneFlagsExt_*` (`SceneDB.cpp`): a growable per-scene bitset for any scene id, persisted by
    scene name in the `unbound` save section (`roomClearExt`), temp flags reset on scene init.
    Rooms < 32 are untouched so vanilla saves keep their layout.
  - **Waterbox room** was a 6-bit field in `properties` (`0x3F` = all). `WaterBox.room` is unpacked
    at load by every loader; `collision.json` accepts an explicit `"room"` (`-1` = all) that overrides
    the packed bits, so a mod can put water in room 70.
  - **Transition actors** were capped at 64 by the index packed into `params` (`i << 10`).
    `Actor.transitionIndex` (set by `Actor_SpawnTransitionActors`) carries it; readers use
    `TRANSITION_ACTOR_INDEX(actor)`, which falls back to the params packing for actors spawned any
    other way. `TransitionActorContext.numActors` is u16.
  - **Minimap visited bits** (`sceneFlags[].rooms`, `gBitFlags[room]`) are guarded to rooms < 32;
    custom scenes have no minimap yet anyway (README limits).
- The 1 MB object arena is still allocated from the play-state heap for vanilla parity
  (`Object_InitBank`). It could be dropped entirely; left for the scene-format pass.
- `OBJECT_ID_MAX` and the `ObjectID` enum are untouched — they name vanilla objects only.

## Status

Implemented on the `unbound` branch. See the README status table for build/verification state.

## Verification

1. Vanilla parity: Hyrule Field (many actors/objects), Market (object-heavy), Forest Temple
   (type-2 sorted mesh), Ganon's Tower collapse.
2. A Prelude scene listing > 127 objects loads with all of them resolvable (`Object_GetIndex`
   returns an index for the last one); one listing > 1023 logs the overflow line.
3. A room header with > 255 actors spawns all of them; `actorCtx.total` reads correctly past 255
   in the actor viewer.
4. A room mesh with > 64 sorted entries draws without the assert.
