# Play-testing Unbound

Every Unbound change is a *widening* of something vanilla stored small, and the bugs it produces are
all of one shape: a reader that still decodes the old size. Two of the three found in 0.3 were in
code paths that only run when a moving (dyna) actor is involved, which the title-screen smoke test
never exercises. This is the shortest walk that does.

Run `scripts/unbound-pointer-drift.sh` first (README › Working on the fork); it catches the
pointer-level version of this class at compile time. Then play:

| Exercise | Widened thing under test | Pass |
|---|---|---|
| Push a block (Deku Tree / Dodongo's), grab it from both sides | dyna wall lists, wall line test (`Math3D_PointInSph`), push flags | grabs, moves, stops at walls |
| Roll into a large crate (`Obj_Kibako2`, Kakariko / Market) | dyna line test end-inside-actor | bonk + crate breaks |
| Stand on the Hyrule Field drawbridge; ride a moving platform (Shadow Temple lift, Fire Temple elevator) | dyna floor raycast, `minY/maxY`, bounding sphere | carried, no fall-through |
| Swim in Lake Hylia / Zora's Domain; enter and leave water; dive | water boxes (s32 extents, unpacked `room`) | surface height right, camera + lighting switch |
| Plant a magic bean and ride it; watch Zelda's escape (Ganon's Castle collapse) | f32 path points (`Obj_Bean`, `En_Zl3`) | plant follows its path |
| Epona: ride, save, reload | `HorseData.pos` f32 in the save | same spot |
| Torch-lit room at night (Kakariko, Market) | point lights f32 | glow at the torch |
| Hookshot a target; climb a vine wall; kick a wall | surface types unpacked (`canHookshot`, `wallFlags`) | as vanilla |
| Bomb a bombable wall (Dodongo's Cavern) | dyna delete + node list growth | opens, no crash |
| Walk the far edge of the Lake Hylia test mod | s32 mesh vertices, f32 matrices, grid scaling | draws, collides, no wrap |

Anything that "stops but doesn't react" (blocks but no bonk, water but no swim) is the signature:
one half of the pair reads the widened bytes correctly and the other half does not.
