# Example: water slower than a quarter-texel per frame

The same rebind as [`lake-hylia-reversed-water`](../lake-hylia-reversed-water/README.md) — Lake
Hylia (`spot06`), segment 8 on the opaque pass, child (`"0"`) and adult (`"2"`) setups — with the
motion carried by the fractional keys instead of the integer steps (`SPEC.md` §4.2). Layer 0 has
`xStep`/`yStep` 0 and `xSpeed` 0.1, `ySpeed` −0.1: a tenth of a quarter-texel per gameplay frame
in the vanilla direction, where the compiled draw config moves a whole quarter-texel. The lake
surface drifts at **a tenth of vanilla speed**. Layer 1 stays still, as in vanilla. The
env-colour side effect described in the other example applies here too.

Mount it alone: the two examples patch the same entry.

| Build | Expected |
|---|---|
| Unbound with `xSpeed`/`ySpeed` (0.7+) | slow, even drift with no jump, smooth at high frame rates |
| Unbound 0.6 | the water stands still: that reader ignores the speeds (§2) and the steps are 0 |
| Unbound 0.5 | vanilla water, no error: that reader ignores `materialAnims` |

Package with any zip tool, keeping the paths, and drop the result in SoH's `mods/` folder:

    cd lake-hylia-slow-water && zip -r ../lake-hylia-slow-water.o2r unbound.json scenes
