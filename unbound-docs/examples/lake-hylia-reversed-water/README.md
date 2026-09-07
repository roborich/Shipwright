# Example: animated water from data

Rebinds the Lake Hylia (`spot06`) water scroll through `materialAnims` (`SPEC.md` §4.2) in the
child (`"0"`) and adult (`"2"`) setups: segment 8 on the opaque pass, two layers. The compiled
draw config scrolls render tile 0 by (+1, +1) texels per frame and leaves tile 1 still; the entry
steps tile 0 by (`xStep` −4, `yStep` +4), which the §4.2 sign rule turns into (−4, −4) per frame,
and keeps tile 1 still. The lake surface flows **backwards, four times faster**. The room meshes
and their segment calls are vanilla. One side effect to expect: the vanilla list also set the
water's env colour (its alpha tracks the lake level); a scroll entry replaces the whole list, so
the water takes the env colour the config sets afterwards (255, 255, 255, 128) and looks a little
lighter than vanilla in the adult setup before the lake is refilled.

It is the whole feature's test on vanilla geometry, with no editor involved:

| Build | Expected |
|---|---|
| Unbound with `materialAnims` (0.6+) | reversed, faster water; smooth at high frame rates (the list uses the interpolating scroll helpers) |
| The same build, mod removed | vanilla speed and direction (the list is cleared between scenes) |
| Unbound 0.5 | vanilla water, no error: an older reader ignores the key (§2) |

Package with any zip tool, keeping the paths, and drop the result in SoH's `mods/` folder:

    cd lake-hylia-reversed-water && zip -r ../lake-hylia-reversed-water.o2r unbound.json scenes

The debug log shows `scenes/spot06/scene.json: merging 2 archive layers` when the lake loads. A
malformed entry logs `[Unbound] … materialAnims[0]: …; entry dropped` and the lake stays vanilla.
