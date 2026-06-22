# Toon Lighting — How It Works & How To Tune It

Wind Waker–style cel shading for Ship of Harkinian, built on the `toon-lighting` branch.
This document describes the **implemented** effect (not the original plan) and how to adjust
it from the in-game GUI.

> **Temporary doc.** This is a working reference for the branch, not permanent project
> documentation. Delete it before the branch is cleaned up for a PR, or fold the relevant
> parts into `docs/`.

---

## Carried upstream fix — watch for merge (libultraship PR #1121)

The libultraship side of this branch (`toon-debug-and-water-fix`, commit `922ab85d`) **cherry-picks
an open upstream PR**, not toon-related, so we can render correctly at high FPS:

- **[Kenix3/libultraship#1121](https://github.com/Kenix3/libultraship/pull/1121)** — "round
  interpolated texture tile sizes" (banteg, branch `feat/fix-interpolated-tile-size`). Fixes animated
  **water/lava textures jittering when interpolation is on** (Match Refresh Rate / high FPS), e.g.
  Lake Hylia water. Root cause: `(high - low + 4) / 4` tile-size math truncated an interpolated
  `31.9999` to `31`, so the import-clamp and draw-time texture window disagreed by a texel across
  interpolation sub-frames (alternating `32x32` / `32x31`). The fix adds `GetTileSizeFromCoordinates()`
  (rounds with `lroundf`) in `src/fast/interpreter.cpp`. It is **backend-agnostic** (interpreter only),
  so it fixes Windows/D3D11 and OpenGL too — not just macOS/Metal where it was first reported.
  Related: Kenix3/libultraship#1119, Shipwright#6666.

> ⚠️ **Action item when this PR merges upstream.** Our copy is a manual cherry of an *open* PR. When
> #1121 (or an equivalent) lands in the libultraship commit we base on, **drop our `922ab85d` commit**
> (or rebase past it) so we don't carry a duplicate / conflicting edit. Until then, any LUS bump must
> keep this fix.

---

## What it does

When enabled, every **actor/object** (Link, NPCs, enemies, items, pots, doors, …) is re-lit
with a **single dominant "key" light** and a soft two-tone ramp, giving the flat, banded,
storybook look of *The Wind Waker*. The **static world (rooms/scenery) is never touched** —
only actor draws are bracketed, so the environment keeps its normal lighting.

The look is driven by real lights in the scene: the sun/moon by default, automatically
switching to the **closest in-range point light** (a fairy, torch, bomb flash, …) when one is
near. The key light eases smoothly from one source to another rather than snapping.

---

## The big picture: two halves working together

The effect is split across the two layers of the codebase, communicating through two new
custom GBI commands.

```
  GAME SIDE  (soh/src/code/z_actor.c, runs on CPU at 20 fps)
    │  picks ONE key light per actor (closest point light, else sun/moon),
    │  eases it frame-to-frame, encodes direction+colour
    │
    ├── gSPToon(true/false) ........ brackets the whole actor draw loop
    └── gSPToonKey(dir, colour) .... one per actor, just before its geometry
    │
    ▼
  RENDERER SIDE  (libultraship Fast3D: interpreter + 3 backend shaders)
       forwards the per-vertex normal, neutralises the baked vertex shade to white,
       and re-lights every pixel with a half-Lambert smoothstep ramp in the fragment shader
```

**Why this split?** OoT's lighting is computed per-*vertex* on the CPU and baked into the
vertex colour — by the time the GPU runs, the surface normal is gone and lighting is a single
interpolated colour. That can't produce crisp cel bands on OoT's low-poly models. So we
forward the normal to the fragment shader and do the banding **per-pixel** there instead.

---

## Game side — choosing the key light (`soh/src/code/z_actor.c`)

All of this lives in a block of `// SOH [Enhancement]` functions added before `Actor_Draw`.

### Selection rule (Wind Waker–style)

For each actor, once per frame (`Actor_DrawToonKey`):

1. **`Actor_ToonClosestPointLight`** — walk the scene's point-light list
   (`play->lightCtx.listHead`) and pick the **closest** point light whose distance is within
   `radius × PointLightRange`. **Brightness is deliberately ignored** — proximity alone
   decides. This makes flickering torches rock-steady and means the nearer of two lights
   always wins. (Earlier commits debounced by brightness/hysteresis; that was replaced by
   pure closest-wins in the final commit `f0efd23a2`.)
2. **`Actor_ToonEnvKey`** — if no point light is in range, fall back to the **sun or moon**,
   whichever is currently brighter (sum of RGB), so the key tracks day/night.

A point light is converted to a direction as `normalize(light.pos − actor.pos)`; directional
lights use their direction straight.

### Smooth travel between lights

The chosen target would otherwise snap when the winner changes. To avoid that, each actor has
**persistent per-frame state** and eases toward the target:

- **Direction** uses `Actor_ToonSlerp` — an antipode-safe spherical interpolation (a cheap
  linear+renormalise fast path when nearly aligned; a perpendicular-axis rotation when nearly
  opposite, so the key glides around the sphere instead of through the centre when a light
  swings to the far side).
- **Colour** uses `Actor_ToonSmoothDamp` — Unity-style critically-damped smoothing (eases in
  and out, never overshoots).
- The easing rate comes from the **Transition Time** slider; the timestep is `1/20 s` (OoT's
  logic rate).

State is stored in a fixed 1024-entry hash table `sToonKeyStates[]`, keyed by the actor
pointer (`(actor >> 4) % 1024`). The `Actor` struct has no spare fields; pointer reuse is
detected via the stored `actor`/`valid` fields and the slot re-initialises itself, so no
cleanup is ever needed.

### Emitting to the renderer

- `Actor_DrawToonKey` encodes the eased direction as three signed bytes (`× 127`) and the
  colour as three unsigned bytes (`× 255`) and emits **`gSPToonKey(...)`** into both the
  opaque (`POLY_OPA`) and translucent (`POLY_XLU`) display lists — once per actor, right
  after `Lights_Draw`, so each actor carries its own key light.
- `func_800315AC` (the actor draw loop) brackets the whole loop with **`gSPToon(true)`**
  before and **`gSPToon(false)`** after (before effects/lens/UI). This is what scopes the
  effect to actors only.

Both are **gated by `Graphics.ToonLighting.Enabled`**, so when the feature is off there is
zero overhead — no commands are emitted at all.

### Debug viewer

When the developer CVar `ToonLighting.ShowDebug` is on, `Actor_DrawToonKey` also draws
(translucent, depth-test-free, so always visible):

- A **coloured "light ray"** from each actor toward every candidate light — tinted by the
  light's colour, length scaled by its live intensity (so torches visibly flicker).
- A thin **magenta needle** along the actor's currently-chosen key direction.
- A **cyan range ring** around every point light (drawn once, on the player's pass), sized to
  `radius × PointLightRange`, so the reach of the Point Light Range slider is visible.

Geometry is a small 4-sided spike (`sToonRayDL`) and a 12-segment ring (`sToonRingDL`)
defined inline.

---

## Renderer side — the per-pixel ramp (libultraship Fast3D)

New custom GBI commands, mirroring how SoH's existing grayscale effect works:

| Command | Opcode | Purpose |
|---------|--------|---------|
| `G_SETTOON` / `gSPToon` | `0x41` | Turns the toon shader variant on/off for the current batch. Sets `mRdp->toon`. |
| `G_SETTOONKEY` / `gSPToonKey` | `0x4a` | Supplies this object's world-space key direction + colour. Sets `mRsp->toon_key_*`. |

Shared CVar keys and default values live in the new header
`libultraship/include/fast/toon_shading.h`.

### What the interpreter does (`src/fast/interpreter.cpp`)

1. **Per-object flush.** `gfx_set_toon_key_handler_custom` calls `Flush()` **before** storing
   the new key, so each toon object is its own draw batch and gets its own key light (this
   fixed batched objects being mis-lit by a neighbour's key).
2. **Forward the normal.** A new `TOON` bit is added to `ShaderOpts` (which required bumping
   `SHADER_ID_SHIFT` from 17→18 since the opt bitfield was full). When toon + `G_LIGHTING`
   are active, `GfxSpVertex` copies the object-space vertex normal into new
   `LoadedVertex.nx/ny/nz` fields and packs them into the VBO (which grew from 32→40 floats
   per vertex).
3. **Neutralise the shade.** For toon vertices the baked Gouraud shade is forced to **white**,
   so the colour combiner outputs pure *albedo* (texture × prim/env). All real lighting then
   happens in the fragment shader — no double-lighting.
4. **Resolve one light.** `SelectToonLight()` transforms the game-supplied key direction into
   object space (`TransposedMatrixMul`) and hands direction + colour + ambient to the backend
   via `SetToonLighting()`. (It has a fallback luminance-weighted-average path for objects
   drawn without a `gSPToonKey`, but in practice the game always supplies one.)

### The fragment shader (OpenGL / Metal / D3D11 — identical math)

All three backends were wired with a new `aNormal` vertex attribute and a set of toon
uniforms, and each `default.shader.*` template applies:

```glsl
vec3  N      = normalize(vNormal);
float NL     = dot(N, normalize(toon_light_dir)) * 0.5 + 0.5;          // half-Lambert, [0,1]
float ramp   = smoothstep(center - softness, center + softness, NL);   // soft band edge
vec3  lit    = toon_ambient + toon_light_color * highlight_intensity;  // lit side
vec3  shadow = mix(lit, toon_ambient, shadow_intensity);               // shadow side
texel.rgb    = clamp(texel.rgb * mix(shadow, lit, ramp), 0.0, 1.0);    // albedo × two-tone
```

- **Half-Lambert** (`*0.5 + 0.5`) gives the soft wraparound falloff Wind Waker uses, instead
  of a harsh `N·L` cutoff at the terminator.
- **`smoothstep`** across `center ± softness` produces the soft two-tone terminator — a narrow
  band gives a crisp cel edge, a wide one gives a gradient.
- The albedo (white-shaded texture) is multiplied by an interpolation between the **shadow**
  colour and the **lit** colour.

---

## Adjusting it in the GUI

### Settings → Graphics → "Toon Lighting"

The toggle is always visible; the sliders appear only when it is enabled.

| Control | CVar (`gEnhancements.…`) | Range / default | What it does |
|---------|--------------------------|-----------------|--------------|
| **Enable Toon Lighting** | `Graphics.ToonLighting.Enabled` | off | Master switch. Off = zero overhead (no GBI commands emitted). |
| **Ramp Center** | `Graphics.ToonLighting.RampCenter` | 0–100%, **50%** | Where the dark→light transition sits in half-Lambert space. Higher = more of the surface stays in shadow. |
| **Ramp Softness** | `Graphics.ToonLighting.RampSoftness` | 0.01–0.2, **0.02** | Width of the transition band. Low = a hard cel edge; high = a softer edge. Capped at 0.2 — anything higher loses the toon look. |
| **Highlight Intensity** | `Graphics.ToonLighting.HighlightIntensity` | 0–200%, **60%** | Brightness of the lit side. >100% over-brightens highlights. |
| **Shadow Intensity** | `Graphics.ToonLighting.ShadowIntensity` | 0–100%, **60%** | How dark the shadow side gets. 0% = flat (no shadow), 100% = full shadow down to ambient. |
| **Point Light Range** | `Graphics.ToonLighting.PointLightRange` | 1.0×–4.0×, **1.5×** | Multiplier on a point light's radius **for key selection only** (the game's real lighting is unchanged). Raise it so e.g. an orbiting fairy keeps lighting nearby objects even at the far end of its swing. 1× = the light's literal range. |
| **Transition Time** | `Graphics.ToonLighting.TransitionTime` | 0.1–6.0 s, **1.0 s** | How long the key light takes to ease from one source to another. Higher = slower, more deliberate travel between the sun and a fairy/torch. |

**Where each slider acts:** Ramp Center, Ramp Softness, Highlight Intensity and Shadow
Intensity are **shader** parameters (pushed as uniforms every draw — change them and the look
updates live). Point Light Range and Transition Time are **game-side selection** parameters
(they affect which light is chosen and how fast the key moves, not the pixel math).

### Developer Tools → "Toon Lighting Viewer"

| Control | CVar | What it does |
|---------|------|--------------|
| **Toon Lighting Viewer** | `gDeveloperTools.ToonLighting.ShowDebug` | Draws the candidate-light rays, the magenta chosen-key needle, and the cyan point-light range rings described above. Requires Toon Lighting to be enabled. |
| **Toon Lighting: Highlight Lit Objects** | `gDeveloperTools.ToonLighting.HighlightBands` | Renders every toon-lit object as flat **white** on the lit side of the ramp and flat **black** in shadow, discarding the albedo. Makes it unmistakable which draws are actually being relit — use it to confirm whether large surfaces like **water or lava** are receiving toon lighting (and flickering as the ramp edge sweeps across the whole plane). Requires Toon Lighting to be enabled. |

**How the "Highlight Lit Objects" toggle is wired:** `ToonLighting.cpp` reads the CVar once per frame and passes it as the 5th `debug` argument to `SetToonRamp()`. The renderer carries it as a `toon_debug` uniform (added to the per-draw toon path in all three backends), and the toon fragment shader branches on it: `texel.rgb = vec3(toonRamp)` instead of the usual `albedo × two-tone`. No new GBI command — it rides the existing ramp plumbing.

---

## Quick tuning recipes

- **Hard, graphic-novel cel edge:** Ramp Softness → very low (≈0.02), Shadow Intensity → high.
- **Softer edge (still toon):** Ramp Softness → toward the top of its range (≈0.15–0.2).
- **Brighter, sun-bleached look:** Highlight Intensity → ~150%.
- **Subtle effect (barely-there bands):** Shadow Intensity → ~30–50%.
- **Fairy/torch keeps lighting things from across the room:** Point Light Range → 2–4×.
- **Snappier vs. dreamier light changes:** lower vs. raise Transition Time.

---

## Files touched (branch summary)

**Game side (`soh/`):**
- `soh/src/code/z_actor.c` — key-light selection, easing, `gSPToon`/`gSPToonKey` emission, debug viewer.
- `soh/soh/SohGui/SohMenuSettings.cpp` — the Toon Lighting settings section.
- `soh/soh/SohGui/SohMenuDevTools.cpp` — the debug viewer toggle.

**Renderer side (`libultraship/` submodule):**
- `include/libultraship/libultra/gbi.h`, `include/fast/lus_gbi.h` — `G_SETTOON` / `G_SETTOONKEY` commands.
- `include/fast/toon_shading.h` — shared CVar keys + defaults (new file).
- `include/fast/interpreter.h`, `src/fast/interpreter.cpp` — toon state, normal forwarding, shade neutralisation, `SelectToonLight`, per-object flush, `SHADER_ID_SHIFT` bump.
- `include/fast/backends/*.h`, `src/fast/backends/gfx_{opengl,metal,direct3d11}.cpp` — vertex attribute + uniforms in all three backends.
- `src/fast/shaders/{opengl,metal,directx}/default.shader.*` — the half-Lambert smoothstep ramp.
