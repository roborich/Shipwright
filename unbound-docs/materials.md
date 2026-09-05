# Animated materials

How a scene document animates water, lava, glowing runes and flipbook textures without code, and
why it is shaped the way it is. The format is SPEC.md §4.2 (`materialAnims`) and §9; this page is
the engine side.

## The mechanism, in both games

An N64 display list never holds a real address. `0x08000040` means "segment 8, offset 0x40": the
RSP keeps a 16-entry table of base pointers and resolves the address through it, and libultraship's
interpreter keeps the same table (`MAX_SEGMENT_POINTERS`, `include/fast/interpreter.h`). A
`gSPSegment` command rebinds one slot for the commands that follow it in the same buffer.

Ocarina reserves the low slots (scene, room, keep objects, the current actor's object) and treats
8–13 as a per-frame window. Every frame `Scene_Draw` runs the scene's compiled *draw config*
(`sSceneDrawHandlers`, `z_scene_table.c`), which generates a two-command tile-size list with
`Gfx_TexScroll` and binds it into, say, segment 8. The room's water display list loads its texture,
sets up its tile, then calls `gsSPDisplayList(0x08000000)` right before its triangles — and inherits
the scrolled tile. The call *is* the animation. Nothing in the room mesh changes per frame.

Majora's Mask does the same thing but moved the recipe out of C: scene command 0x1A points at a
list of `{ segment, type, params }`, and draw config 1 replays it (`z_scene_proc.c`,
`AnimatedMat_DrawMain`). Six recipe types: single- and two-layer texture scroll, three
colour-key-frame shapes (stepped, linear, Lagrange), and a texture flipbook.

## What Unbound adds

`materialAnims` on a scene setup is that list in JSON. The pieces, in the order the data flows:

| Step | Where |
|---|---|
| Key names | `soh/soh/unbound/UnboundSchema.h` (`kMaterialAnims`, `kSegment`, `kPass`, …) |
| C types (`AnimatedMaterial`, the three param structs, `ANIM_MAT_*`) | `soh/include/z64scene.h` |
| Command resource that owns the list and everything it points into | `soh/soh/resource/type/scenecommand/SetAnimatedMaterialList.{h,cpp}`; command id `SceneCommandID::SetAnimatedMaterialList` (0x1A, MM's number) |
| JSON reader: one entry → one `AnimatedMaterial`, per-entry validation | `UnboundSceneFactory.cpp`, `BuildMaterialAnims` and the `Read*` helpers above it |
| Handler that stores the list on `PlayState` (`sceneMaterialAnims`, `sceneMaterialAnimCount`) | `soh/soh/z_scene_otr.cpp`, `Scene_CommandAnimatedMaterials`; the dispatch guard is now the table size, not `0x19` |
| Reset between scenes | `Play_Init`, beside the song reset |
| The per-frame binds | `soh/src/code/z_scene_proc.c`, `Scene_DrawMaterialAnims`, called at the end of `Scene_Draw` |

There is no binary form and no converter change: vanilla OoT has no such data, so the converted
base is untouched and every existing archive is unaffected.

### Differences from MM, and why

- **Absolute segments and a count.** MM stores `abs(n) + 7` and marks the last live entry with a
  negative segment. JSON has an explicit list length; the reader stores 8–13 as written.
- **A `pass` per entry.** MM's scene-level list always binds both buffers. OoT's hand-written
  configs bind per buffer, and the difference is load-bearing: Goron City binds segment 8 to a
  scroll on OPA and to a window *texture* on XLU. The opaque and translucent buffers are separate
  command streams with separate segment tables while they run, so a scene has six slots on each.
- **Interpolating scroll lists.** The scroll handlers call SoH's `Gfx_TexScrollEx` /
  `Gfx_TwoTexScrollEx`, which take the per-frame step and emit one tile-size command per
  interpolation sub-frame, so the motion is smooth above 20 fps. 2Ship does the same.
- **Runs after the draw config, always.** A custom scene keeps `drawConfig` 0 (which only resets
  8–13 to the empty list) and the list rebinds what it needs. A vanilla scene may carry a list
  too; an entry naming a segment the config also bound wins, because it is written later in the
  same buffer. There is no new draw-config index.
- **Per-entry rejection.** A bad entry (segment out of range, unknown type, wrong layer count,
  mismatched colour counts, a flipbook index past its texture list) is logged and dropped; the
  document still loads. One broken water material should not take a scene down.

### What the material must do

The generated list only sets tile sizes (scroll), prim/env colour (colour types), or a texture
pointer (flipbook). The room display list has to do the rest: load the texture with wrap
addressing, set up render tile 0 (and tile 1 for `twoTexScroll`), then call the segment
immediately before the triangles. A flipbook material samples the segment itself:
`gsDPSetTextureImage(fmt, siz, width, 0x0B000000)`; the reader binds each frame's `__OTR__<path>`
string into the slot, which is how the Kakariko day/night window textures already work.

### The six-per-pass window

Six distinct recipes per pass, not six materials: any number of materials may call the same
segment and share its motion. Widening the window is not a format change — the segment byte of
an address already holds 256 values — but it touches `MAX_SEGMENT_POINTERS` in the libultraship
fork, the `G_MW_SEGMENT_INTERP` encoding (`offset % 16`, unused by game code), the default
display list that resets 8–13, and the accepted range in SPEC §4.2. Slots 14 and 15 are never
bound by game code; whether the port layer reserves them is unverified. Held at 16 until someone
needs more.

## Testing

`examples/lake-hylia-reversed-water/` rebinds Lake Hylia's water (segment 8, OPA) with `yStep`
−4 in the child and adult setups. With the mod the lake flows backwards four times faster;
without it, vanilla; on Unbound 0.5, vanilla with no error. See `testing.md`.
