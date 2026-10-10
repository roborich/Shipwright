# Unbound: custom actors

Custom actor types declared in data and placed by name. The format is [`SPEC.md`](./SPEC.md) §7.2
and §4.3; this file covers why and how. Overview in [`README.md`](./README.md).

**Status:** on `unbound`, not yet in a release. Play-tested 2026-09-25 (every item of the
verification plan, in game and through a Prelude export). The test fixture is
[`examples/custom-actors`](./examples/custom-actors/README.md).

## Why

Modders want actors the game does not have. Today the only way is C code in a fork of SoH:

- Every fork re-syncs with Unbound after each release before it can use the scene features Prelude
  exports, and until it does, Prelude output and the fork drift apart.
- Prelude lets a user type a raw actor id to place a fork's actor. That number is only meaningful
  in one build: ActorDB hands out custom ids in registration order, so adding a built-in actor to
  Unbound shifts every fork's ids and silently changes what existing scenes spawn.
- Most of what people want first is small: an NPC that stands in a pose and says something, a
  prop with a model the game does not use. That should not need a C compiler.

The long-term goal is **scriptable custom actors**. Scripting needs a lot of design and is
deliberately not part of this work. This is the first step toward it: custom actor
*types*, declared in data, with a few built-in behaviors, addressed by name. The later steps are
listed at the end so the format leaves room for them now.

## Decisions

1. **New actor types, not per-instance properties.** A type is declared once in a registry and
   placed any number of times. The alternative, extra properties on individual placements of
   existing actors, was rejected: it duplicates data at every placement, adds a third channel
   beside the actor id and `params`, and gives nothing a one-off type cannot. Types are also where
   scripts will attach, as in every engine modders know and as OoT itself works (C code per actor
   id, `params` per placement).
2. **Types are referenced by name.** A scene's actor entry may name its actor
   (`"id": "mymod/old_man"`). The numeric id is assigned at load and never written to a file, the
   same rule scenes and entrances already follow (SPEC §7.1). This also fixes the fork problem: a
   fork's C actor registered in ActorDB under a name is placed by that name. Any name ActorDB
   knows is accepted, spelled as ActorDB spells it, vanilla included (`"En_Kanban"`). The
   converter keeps writing vanilla actors as numbers.
3. **Per-placement setup stays in `params`.** As in vanilla, the type says what the actor is and
   `params` says how this placement is set up. For the first version `params` has one meaning (the
   message id, below).
4. **First version: declared types only.** One C driver runs every declared type, reading the
   type's settings from the registry: a model (animated or static), a collision cylinder,
   talking, and head tracking. Types that extend a vanilla actor (`base`) are a later phase;
   nothing in this version depends on them.

## What a modder writes

A type's model is one of two kinds:

- **Animated:** a skeleton, posed by an animation, looping or held on one frame. NPCs, animals,
  anything with limbs.
- **Static:** a single display list. Trees, rocks, lanterns, fences, signs, statues: most of the
  scenery a modder wants to add.

Every behavior works with either kind, so a static signpost can talk, and a static statue can
block the player. Only head tracking (`look`) needs a skeleton, because it turns a limb.

Each type is one file, and its path is the type's name. `unbound/actors/mymod/old_man.json`
declares `mymod/old_man`:

```json
{
  "name": "Old Man (sitting)",
  "model": {
    "skeleton": "objects/object_xxx/gOldManSkel",
    "animation": "objects/object_xxx/gOldManSitAnim",
    "scale": 0.01,
    "segments": { "8": "objects/object_xxx/gOldManEyeOpenTex" },
    "shadow": 20
  },
  "collision": { "radius": 20, "height": 50 },
  "talk": { "message": "0xA001" },
  "look": { "limb": 15 }
}
```

Three more types, one file each:

```json
// unbound/actors/mymod/pine_tree.json
{
  "model": { "displayList": "objects/mymod_props/gPineTreeDL", "scale": 0.1, "shadow": 40 },
  "collision": { "radius": 25, "height": 200 }
}

// unbound/actors/mymod/signpost.json
{
  "model": { "displayList": "objects/mymod_props/gSignpostDL", "scale": 0.1 },
  "collision": { "radius": 12, "height": 40 },
  "talk": {}
}

// unbound/actors/mymod/ghost_lantern.json
{
  "model": { "displayList": "objects/mymod_props/gGhostLanternDL", "scale": 0.1, "translucent": true }
}
```

The signpost has no default message, so each placement sets its own text through `params`.

A room places it by name. Two placements of one type can say different things through `params`:

```json
"actors": {
  "10": { "id": "mymod/old_man", "pos": [120, 0, -40], "rot": [0, 16384, 0], "params": 0 },
  "11": { "id": "mymod/old_man", "pos": [300, 0, 80],  "rot": [0, 0, 0],     "params": "0xA002" }
}
```

The first says message `0xA001` (the type's default); the second says `0xA002`. Both messages are
ordinary entries in `text/<lang>/messages.json` (SPEC §5).

## Format

The contract is [`SPEC.md`](./SPEC.md): §7.2 is the actor registry, §4.3 says a room actor's `id`
may be a name, and §9 and §10 record the limit and the addition. This file explains why the
format is shaped that way and how the engine implements it.

### Compatibility with older builds

Every Unbound release before this one reads a string `id` as the wrong type, which SPEC §2
treats as missing: id 0, the player actor. A scene that places a custom actor by name therefore
spawns an extra Link on those builds instead of being refused, and `requires.formatVersion`
cannot prevent it, because a layer that fails the version check is still merged (SPEC §6). Mods
that use custom actors need the release that adds them. Prelude should say so when it exports
one, and the release notes should say it too. Builds with this change skip any name they do not
know, so the problem does not recur for later additions.

## Engine design

### Registry

- `soh/soh/unbound/ActorRegistry.{h,cpp}` reads every `unbound/actors/<name>.json`, validates
  each into a `DeclaredActorType` struct, and registers it with ActorDB. It walks the files with
  `Unbound::ForEachRegistryFile`: every mounted path under the folder, sorted by name, each
  layer-merged on its own path and read in its own `try`, so one bad file is logged and skipped
  and the rest load.
- **One file per type, not one shared document.** The registry started as a single layer-merged
  `unbound/actors.json`. A tool that keeps each type as its own asset (Prelude) had to rewrite
  that shared file and work out which of its keys it owned; one file per type maps one asset to
  one path, and a broken file loses only its own type. Layer merging still works per path, so a
  mod can still patch another mod's type. A name comes from a path, so no two types can claim
  one name; two mods that pick the same path merge instead, which is why writers keep to a
  folder of their own. The folder is recommended, not required: a name without one works, and
  the vanilla-name check still refuses one that clashes with an actor in code.
- **Load point: `SOH::Unbound::LoadCustomActors()` in `OTRGlobals.cpp`**, after the mods are
  mounted and after the command-line export. On 9.2.3 it had to follow
  `ActorDB::AddBuiltInCustomActors()` so SoH's own `En_Partner` took its id first; 9.3.0 dropped
  that function and Ivan Co-op registers `En_Partner` at its fixed id (`ACTOR_EN_PARTNER`) the first
  time Ivan spawns. Until then the name `En_Partner` is unknown, so a room actor naming it does not
  resolve and the vanilla-name check does not refuse a mod type that claims it.
  Mods are only mounted at startup (`EnableMod` is marked "TODO: runtime changes"), so the registry
  loads once and never has to unregister.
- **Ids start at `kCustomActorIdBase` (0x1000)**, like custom scenes start at 128. That keeps
  them clear of the vanilla table, of `ACTOR_ID_MAX` (a "no actor" sentinel in randomizer code),
  of SoH's built-in custom actors, and of forks that took fixed ids just past the vanilla table.
  The registry registers each type with `ActorDB::TryAddEntry(init, id)`, at the next id after the
  types already registered.
- **Duplicate names:** `ActorDB::AddEntry` `assert`s on a name or id it already has, which release
  builds drop. The registry rejects a name `RetrieveId` already knows, and `TryAddEntry` checks the
  name and the id again in every build, adding nothing when either is taken.
- Every registered type is its own ActorDB entry, with its own id, name, description and flags,
  pointing at the shared driver functions. Flags: `ACTOR_FLAG_ATTENTION_ENABLED |
  ACTOR_FLAG_FRIENDLY` when the type talks. Category: `ACTORCAT_NPC` when it talks, otherwise
  `ACTORCAT_PROP`. Object: `OBJECT_GAMEPLAY_KEEP`, which is always loaded, so a scene never has to
  list an object for a declared actor. The driver loads its assets by path instead.
- The driver finds its type with `GetDeclaredActorType(actor->id)`, an index into a vector by
  `id - kCustomActorIdBase`.

### Name resolution in scenes

`BuildActorList` in `UnboundSceneFactory.cpp` resolves a string `id` through
`ActorDB::RetrieveId` (`ResolveRoomActor`). `ReadActor` is also used for spawns
(`BuildStartPositions`), which must stay integer, so resolution happens in `BuildActorList` and not
in `ReadActor`. An unknown name logs the room, the entry key and the name, and the entry is not
added to the list. The same step skips an integer `id` outside 0–`0xFFF` and an object `params`
(SPEC §4.3); `BuildTransitionActors` turns a name or an out-of-range number into `-1`.

Scene resources are parsed when a scene loads, long after the registry is built, so there is no
ordering problem. Vanilla-format (binary) scenes store a numeric id and cannot name a custom
actor. That matches custom entrances, which a binary scene cannot exit into.

### The driver

`soh/soh/unbound/DeclaredActor.{h,cpp}` holds the instance struct and the four ActorDB functions.
Each behavior is its own small function taking the instance and its type, so the orchestration
functions read as a list of steps, and so a script can later call the same functions:

```
Init:    ResolveType → CanSpawn (CheckedType: once per type) → InitModel (InitShape → InitFloor
         → InitCulling → InitMesh) → InitCollision → InitTalk → InitLook → InitFocus
Update:  UpdateTalk (KeepUpdatingWhileTalking) → UpdateLook → UpdateCollision → UpdateAnimation
Draw:    DrawSegments → DrawSkeleton or DrawDisplayList (limb callbacks: TurnHead, RecordHeadFocus)
Destroy: free the skeleton and the collider, if they were set up
```

- **Asset paths.** SoH resolves an asset path only when it carries the `__OTR__` signature
  (`ResourceMgr_OTRSigCheck`). The type stores each path with the prefix added once at
  registration, and those strings live as long as the registry, so the driver passes them
  anywhere vanilla code passes an asset symbol.
- **Checking assets.** Before a type's first actor uses its paths, the driver loads each
  resource and checks its type: a skeleton must be normal or flex (`SOH::Skeleton::type`) with
  standard or LOD limbs (`limbType`: a skin limb has no display list where the skeleton drawer
  reads one), an animation must be a normal one (not Link's) with at least one frame and at least
  one joint entry per joint-table entry (`rotationIndices.size() >= limbCount + 1`, the
  skeleton's limbs plus the root position: `SkelAnime_GetFrameData` reads that many), a display
  list and a segment texture must be one. The same pass checks `look.limb` and `hideLimbs`
  against the limb count.
  `CheckedType` keeps the result for the session, keyed by the type's address: mods are mounted
  only at startup, so neither the registry nor its assets change, and a bad path is logged once
  rather than for every placement. A wrong or missing asset kills every actor of the type instead
  of handing the game a bad pointer.
- **Model.** The skeleton resource records its type and limb count, so `InitModel` chooses
  `SkelAnime_InitFlex` or `SkelAnime_Init` at runtime and lets it
  allocate the joint tables (`SkelAnime_Free` in destroy). Looping is
  `Animation_Change(..., ANIMMODE_LOOP, ...)` at `speed`, clamped to the animation's length:
  `SkelAnime_LoopFull` wraps the frame once per update, so a longer step would leave the
  animation's data. A pose is the same call with speed 0, starting and ending on `frame`.
- **Shadow.** `ActorShape_Init` applies `yOffset` and the circle shadow. `ActorShadow_Draw`
  scales the shadow by the actor's scale and draws only over `actor->floorPoly`, which vanilla
  actors get from `Actor_UpdateBgCheckInfo`. The driver never runs that (it would move the actor
  onto the floor), so `InitFloor` does one `BgCheck_EntityRaycastFloor5` from 50 units above the
  position, as the vanilla check does, and records the floor only. It keeps a scene floor only:
  `DynaPoly_Setup` renumbers the moving-collision polygons as dyna actors come and go, so a
  stored pointer to one would drift to another polygon, and the shadow would take its tilt. The
  shadow scale is `shadow × 0.01 / scale`, so `shadow` means the same at any `scale`; the
  registry rejects a scale that is not positive.
- **Culling.** `Actor_Init` gives every actor a zone of 1 000 forward, 350 to the sides and up,
  700 down. `InitCulling` raises the side, up and down margins to `cullRadius` and sets the
  forward distance to `drawDistance`, as vanilla scenery does by hand (`EnWood02`: 4 000 / 2 000
  / 2 400).
- **Draw pass.** A static model is `Gfx_DrawDListOpa` or, with `translucent`, `Gfx_DrawDListXlu`.
  An animated model is `SkelAnime_DrawOpa`/`SkelAnime_DrawFlexOpa`, or with `translucent` the
  `Gfx*`-returning `SkelAnime_Draw`/`SkelAnime_DrawFlex` writing into `POLY_XLU_DISP`, as vanilla
  translucent actors do. `BindSegments` writes to the same pass.
- **Segments.** `BindSegments` first binds every segment 8–12 to `gEmptyDL`, then issues
  `gSPSegment` for each entry, exactly as vanilla NPC draw code does for eyes and mouths, and sets
  the env colour to opaque black. The empty default is bound in the translucent pass too: vanilla
  binds its translucent render mode (`D_80116280`) there only while fading an actor out through
  env alpha, which the driver has no use for, so a character model drawn translucent keeps its
  opaque render mode. Character models such as adult Ruto's, adult Zelda's and
  Darunia's call a segment to set their render mode; vanilla binds `&D_80116280[2]` there, which
  is an end-of-list (entries 0–1 are the translucent mode used while fading). Without a default
  the segment would hold whatever the previous actor bound. Segment 13 is excluded because flex
  skeletons use it for their matrices.
- **Hidden limbs.** The limb-draw callback clears the limb's display list; the limb's transform
  still applies, so its children draw where they should.
- **Collision.** One `ColliderCylinder`, OC only (`OC1_ON | OC1_TYPE_ALL`, `OC2_TYPE_2`),
  `colChkInfo.mass = MASS_IMMOVABLE`, submitted each frame with `CollisionCheck_SetOC`. No
  gravity or floor check: the actor stays exactly where it was placed.
- **Talk.** The standard vanilla sequence. When not talking, offer to talk with
  `func_8002F2CC(actor, play, range)` and keep `actor->textId` set. `Actor_ProcessTalkRequest`
  starts talking; the Player actor opens the textbox. Whether the actor is talking is read from
  the Player every frame (`PLAYER_STATE1_TALKING` and `player->talkActor`), not latched on
  `Actor_TextboxIsClosing`: that is true for one frame only, and an actor culled on that frame
  never updates to see it. Multi-box text and follow-up messages chained by control codes need
  nothing extra. A choice box closes the conversation whatever the answer, because there is no
  behavior to branch to yet; `Message_Update` closes it. A box that ends in an event (or is
  persistent) waits for its actor, so the driver closes it when the player advances
  (`TEXT_STATE_EVENT` and `Message_ShouldAdvance`), as vanilla actors do. That close runs in the
  actor's update, which a culled actor skips, so while talking the actor sets
  `ACTOR_FLAG_UPDATE_CULLING_DISABLED` on itself and clears it afterwards (no type sets it). These
  limits are intentional.
- **Look.** The same pattern as vanilla NPCs, which all hard-code it per actor (`EnKo`, `EnMa1`,
  `EnToryo`, … usually on limb 15). `UpdateLook` calls `Npc_TrackPoint` (preset 0: 60° of head
  yaw) in `NPC_TRACKING_HEAD` mode while the player is within `range` or talking, and in
  `NPC_TRACKING_NONE` otherwise, so the head eases back to rest. The head's limb-draw callback
  applies the limb's own transform, then turns about the point `pivot` along `turnAxis`:
  `headRot.y` about `turnAxis` (default X), then `headRot.x` about `nodAxis` (default Z). The
  default X turn is the same as `EnToryo_OverrideLimbDraw` adding `headRot.y` to the limb's X
  rotation. The default axes keep `Matrix_RotateX`/`Matrix_RotateZ`; any other axis goes through
  `Matrix_RotateAxis`, which has the same sign convention. Rigs built unlike vanilla's (an
  imported MM Skull Kid's head X points forward, so an X turn rolls it) set the axes; Prelude
  measures them from the rest pose. The registry normalizes both and turns tracking off, with an
  error, for a malformed, zero or parallel axis. (`EnMa1`/`EnKo` translate 1 200–1 400 units *before* the
  limb's transform, in the parent's space, to reach the same neck point; after the transform that
  point is the limb's origin, hence `pivot` 0.) The post-limb-draw callback writes the pivot's
  world position to `actor->focus.pos`, which the next frame's tracking uses for its height. Torso
  tracking is left out: vanilla rigs disagree on the sign of the torso turn, so it would need a
  per-rig setting.
- **Focus without a head:** the top of the collision cylinder, or the actor's position.
- **Targeting:** a placement that talks uses target mode 6 (100 units, as vanilla NPCs). The
  type's ActorDB flags make it targetable; a placement with no message clears them on itself.

### Actor-id audit

Nothing in SoH sizes or indexes a table by actor id: every lookup goes through ActorDB, whose
`RetrieveEntry` is bounds-checked. `ACTOR_ID_MAX` is only a "no actor" value in randomizer tables,
keyed by (id, scene, params), which a custom id cannot collide with. Enemy randomizer scans by id and
passes unknown ids through. Anchor packets carry no actor ids; Sail writes them as JSON ints.
Save states copy the heap wholesale and hold no id tables. Fixed along the way:

- `Actor_Spawn` only `assert`ed that the id had an actor. Release builds drop asserts, so an id
  with no actor (a gap below `0x1000`, a typo in a scene, a debug-console or Crowd Control spawn)
  allocated a zero-size actor and wrote past it. It now logs and spawns nothing.
- `Actor_Spawn` also `assert`ed fewer than 255 live actors of one type, a count left from
  vanilla's 8-bit overlay counter. `numLoaded` is an `s32`, and a room of declared props can
  hold hundreds of one type, which aborted Debug builds. The assert is gone.
- `ActorDB::AddEntry` `assert`s on a duplicate id or name, which also vanish in release. The
  registry registers through `ActorDB::TryAddEntry`, which checks both in release builds and adds
  nothing when either is taken.
- Actor Viewer: *Spawn as Child* refused every id past the vanilla table (`En_Partner`'s too), and
  the search-result list looped forever at 256+ results because of a `u8` index. Its search also
  skips the empty ids below the custom types.

Transition actors mask their id with `0x1FFF` (`z_actor.c`); names are accepted in room actors
only, and the scene reader turns a name or a number outside 0–`0xFFF` in a transition actor into
`-1`, which the spawn loop skips (it treats a negative id as already spawned), so custom types
never reach that path. Room actors with a number from `0x1000` up are skipped for the same
reason the numbers are never written: they depend on the mounted mods. A negative number is
skipped too: it names no actor, and one below −32 768 would wrap into the custom range when
stored in the 16-bit id.

## Later phases (not in this version)

Recorded so the format leaves room for them. The unknown-key rule above is what keeps that room:
an older build rejects a type it cannot run instead of placing an actor that does nothing.

1. **More built-in behaviors:** follow a path (scene `paths`), switch animation while talking,
   blinking (a list of eye textures cycled on a segment), torso tracking.
2. **Mesh collision for static models.** Decided against for v1. A cylinder is enough for trees,
   signs and statues, but a rock the player can stand on, a bridge or a platform needs its model's
   shape as collision: `collision.mesh` naming a collision resource, registered as a dynamic
   collision actor (`DynaPolyActor`, the way vanilla's movable blocks and platforms work). Prelude
   would have to export a collision resource per model.
3. **Named params.** A type declares named fields packed into `params`
   (`"params": { "message": { "bits": "0-15" } }`) and Prelude shows a form field for each. Scripts
   will need per-placement arguments; this is how they get them without a separate property
   channel. v1's "`params` is the message id" rule is the one-field case of this.
4. **Types that extend a vanilla actor (`base`).** A new type that runs a vanilla actor's code
   under a new id, with a fixed set of overrides that need no code for a particular actor:
   - fixed `params` bits (`value` + `mask`; the placement supplies the rest)
   - asset swaps: asset path → asset path, active while the actor's own functions run, applied
     where SoH resolves paths (`GbiWrap.cpp`, `ResourceManagerHelpers.cpp`)
   - text remap: vanilla message id → mod message id, swapping the content while
     `msgCtx->textId` keeps the vanilla id so the actor's state machine is unaffected

   Known traps: effects the actor spawns draw later outside its functions (a pot's
   `EffectSsKakera` shards) and need to inherit the swaps; skeleton swaps need a matching rig; code
   that finds the vanilla actor by its id does not find the variant; the base's object must still
   be loaded. Behavior values (speeds, collider sizes, state logic) are *not* overridable this
   way. Each would need a hook written for that actor, and that is where scripting takes over.
5. **Scripting.** A `script` key on a type. The driver's split into separate behaviors, and the
   wrapper points `base` introduces, are where script callbacks (`onInit`, `onUpdate`, `onTalk`)
   attach.

## Open questions

1. **`params` for a type that does not talk.** v1 ignores it. Fine until named params exist.
2. **Culling.** Resolved: `model.cullRadius` and `model.drawDistance`.

## What Prelude needs

- Author one `unbound/actors/<project>/<type>.json` per type: model (an animated skeleton or a static
  display list), collision, talk, look.
- Place declared types (and named fork actors) by name in the actor palette, writing
  `"id": "<name>"`. This replaces typing raw ids. A vanilla actor placed by name still needs its
  object in the room's `objects`, exactly as when placed by number.
- Preview: draw the skeleton in the chosen animation frame, or the display list.
- For `look`: let the user pick the head limb from the skeleton's limb list, numbered from 1 at
  the root (vanilla limb-draw numbering), since the index differs between rigs. For a rig not
  built like vanilla's, write `turnAxis`/`nodAxis` in the head limb's space, and only when they
  differ from `[1, 0, 0]`/`[0, 0, 1]`: older builds reject a type with keys they do not know.
- Validate: skeleton is normal or flex with standard or LOD limbs, animation limb count matches
  the skeleton, the talk message exists in the mod's text, `params` for a talking type is a
  message id.
- Offer `cullRadius` (from the model's bounds) and `drawDistance` for large props.
- Record the change in [`prelude-handoff.md`](./prelude-handoff.md) when it lands.

## Verification plan

1. A mod declaring one skeleton type and one display-list type loads; both appear in the actor
   viewer under their names, with ids from 0x1000.
2. Placed by name in a custom room: the model draws in the right pose, a looping animation loops,
   and a `translucent` model blends.
3. Collision blocks the player; a type without `collision` does not.
4. Talking: Z-target, talk, the textbox shows the type's message; a second placement with
   `params` shows its own; a chained multi-message conversation plays through and returns to idle.
5. Look: a vanilla NPC skeleton with limb 15 turns its head to follow the player
   within `range` and while talking, returns to rest outside it, and the targeting arrow sits
   over the head.
6. Errors: unknown name in a room (entry skipped, room loads); duplicate of a vanilla name
   (entry rejected); a bad skeleton path (that actor does not spawn, the rest of the room does);
   an unknown key such as `base`, or `model.lod` inside an object (entry rejected); `look` on a
   static model (entry rejected); a skeleton with no animation (entry rejected); a skin-limb
   skeleton, or an animation for fewer limbs (that actor does not spawn); a room actor with id
   `0x1000` or object `params` (entry skipped).
7. Shadows: a type with `shadow` draws a round shadow on the ground under it, the same size at
   any `scale`.
8. An event-ended message closes when advanced; after a conversation the actor can be talked to
   again, also after walking away mid-close.
9. Files: a type in a nested folder registers under its full path name; a file with invalid JSON
   or a top-level array loses only its own type; a second mod carrying only
   `{ "model": { "scale": 0.02 } }` at the same path rescales the type and keeps the rest; a
   second mod whose file is `null` removes it.
10. No-mod parity: with no `unbound/actors/` files, ActorDB, `En_Partner`'s id and vanilla rooms are
   unchanged.
