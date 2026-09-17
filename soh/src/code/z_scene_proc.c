// SOH [Unbound] Data-driven animated materials — a port of Majora's Mask's z_scene_proc.c (via 2Ship).
//
// A scene document's `materialAnims` (SPEC.md §4.2, types in z64scene.h) is a list of per-segment
// recipes. Every frame, after the scene draw config has run, Scene_DrawMaterialAnims walks the
// list and binds a freshly generated display list into each entry's runtime segment (8-13) on the
// passes it names. A room display list that calls that segment right before its triangles picks
// up the scrolled tile, the interpolated colours, or the current flipbook texture.
//
// Differences from MM: segments are absolute and the list carries a count (no negative-segment
// terminator); each entry chooses OPA/XLU itself; the scroll lists follow SoH's *Ex helpers, so the
// motion interpolates between game frames at high frame rates; a scroll rate may be fractional and
// the offset wraps at 8192 texels instead of 512.

#include "global.h"
#include "soh/OTRGlobals.h"

typedef struct {
    GraphicsContext* gfxCtx;
    s32 step; // gameplay frame counter
    u8 pass;  // ANIM_MAT_PASS_* bits of the entry being drawn
} MatAnimDraw;

/**
 * Writes the bind into the pass buffers the entry names. Scene_DrawMaterialAnims holds the DISPS
 * open for the whole list (one open/close per frame, as a vanilla draw config does), so this
 * appends to the buffer heads directly.
 */
static void MatAnim_BindSegment(MatAnimDraw* d, s32 segment, void* data) {
    if (d->pass & ANIM_MAT_PASS_OPA) {
        gSPSegment(d->gfxCtx->polyOpa.p++, segment, data);
    }
    if (d->pass & ANIM_MAT_PASS_XLU) {
        gSPSegment(d->gfxCtx->polyXlu.p++, segment, data);
    }
}

// The tile offset wraps at this many quarter-texels (8192 texels). A wrap is invisible only on a texture whose
// size divides the period, so it is a power of two far above MM's 2048 (512 texels), which jumps on a larger
// texture. At this magnitude an f32 offset still resolves 1/512 of a quarter-texel.
#define MAT_ANIM_SCROLL_PERIOD 32768.0

/**
 * A layer's tile offset along one axis, in quarter-texels within [0, period), at a (possibly
 * fractional) gameplay frame. Stateless and in f64, so a slow fractional rate neither drifts nor
 * loses precision however long the session runs.
 */
static f32 MatAnim_ScrollOffset(f64 rate, f64 frame) {
    f64 offset = fmod(rate * frame, MAT_ANIM_SCROLL_PERIOD);

    return (f32)(offset < 0.0 ? offset + MAT_ANIM_SCROLL_PERIOD : offset);
}

/**
 * Writes one layer's tile size at a gameplay frame. The rate is the integer step plus the
 * fractional speed; y runs the other way (SPEC.md §4.2).
 */
static Gfx* MatAnim_WriteScrollTile(Gfx* gfx, s32 tile, const AnimatedMatTexScrollParams* p, f64 frame) {
    f32 x = MatAnim_ScrollOffset(p->xStep + p->xSpeed, frame);
    f32 y = MatAnim_ScrollOffset(-(p->yStep + p->ySpeed), frame);

    gDPSetTileSizeInterp(gfx, tile, x, y, x + ((p->width - 1) << 2), y + ((p->height - 1) << 2));
    return gfx + 3; // the interpolated tile size is a three-word command
}

/**
 * Generates the scroll list for `layerCount` layers on render tiles 0..n. As in SoH's *Ex scroll
 * helpers, there is one set of tile sizes per interpolated frame, each evaluated at its own
 * fraction of the gameplay frame, so the motion stays smooth at high frame rates.
 */
static Gfx* MatAnim_ScrollList(MatAnimDraw* d, const AnimatedMatTexScrollParams* layers, s32 layerCount) {
    s32 interpFrames = Ship_GetInterpolationFrameCount();
    Gfx* list = Graph_Alloc(d->gfxCtx, (2 + interpFrames * (1 + 3 * layerCount)) * sizeof(Gfx));
    Gfx* gfx = list;
    s32 i;
    s32 tile;

    gDPTileSync(gfx++);
    for (i = 0; i < interpFrames; i++) {
        f64 frame = d->step + (f64)i / interpFrames;

        gDPSetInterpolation(gfx++, i);
        for (tile = 0; tile < layerCount; tile++) {
            gfx = MatAnim_WriteScrollTile(gfx, tile, &layers[tile], frame);
        }
    }
    gSPEndDisplayList(gfx);

    return list;
}

/**
 * Type 0: scrolls a single layer texture.
 */
static void MatAnim_DrawTexScroll(MatAnimDraw* d, s32 segment, void* params) {
    MatAnim_BindSegment(d, segment, MatAnim_ScrollList(d, params, 1));
}

/**
 * Type 1: scrolls two texture layers (render tiles 0 and 1).
 */
static void MatAnim_DrawTwoTexScroll(MatAnimDraw* d, s32 segment, void* params) {
    MatAnim_BindSegment(d, segment, MatAnim_ScrollList(d, params, 2));
}

/**
 * Generates a display list that sets the prim colour (and env colour when given) and binds it.
 */
static void MatAnim_SetColor(MatAnimDraw* d, s32 segment, F3DPrimColor* prim, F3DEnvColor* env) {
    Gfx* gfx = Graph_Alloc(d->gfxCtx, 3 * sizeof(Gfx));
    Gfx* head = gfx;

    gDPSetPrimColor(gfx++, 0, prim->lodFrac, prim->r, prim->g, prim->b, prim->a);
    if (env != NULL) {
        gDPSetEnvColor(gfx++, env->r, env->g, env->b, env->a);
    }
    gSPEndDisplayList(gfx++);

    MatAnim_BindSegment(d, segment, head);
}

/**
 * Index of the last key frame at or before curFrame. The reader guarantees keyFrames[0] == 0 and an
 * ascending list, so this is always a valid index; frames past the last key frame map to it.
 */
static s32 MatAnim_CurKeyFrame(AnimatedMatColorParams* p, s32 curFrame) {
    s32 k = 0;

    while ((k + 1 < p->keyFrameCount) && (curFrame >= p->keyFrames[k + 1])) {
        k++;
    }
    return k;
}

/**
 * Type 2: colour key frames without interpolation. Each key frame's colour holds until the next one
 * (SPEC.md §4.2: one colour per key frame, not per frame as in MM).
 */
static void MatAnim_DrawColor(MatAnimDraw* d, s32 segment, void* params) {
    AnimatedMatColorParams* p = params;
    s32 k = MatAnim_CurKeyFrame(p, d->step % p->keyFrameLength);
    F3DPrimColor* prim = p->primColors + k;
    F3DEnvColor* env = (p->envColors != NULL) ? p->envColors + k : NULL;

    MatAnim_SetColor(d, segment, prim, env);
}

static s32 MatAnim_Lerp(s32 min, s32 max, f32 norm) {
    return (s32)((max - min) * norm) + min;
}

/**
 * Type 3: colour key frames with linear interpolation. Past the last key frame the last colour holds
 * (the pair collapses to a single key frame and norm is 0), so the lookup never runs off the lists.
 */
static void MatAnim_DrawColorLerp(MatAnimDraw* d, s32 segment, void* params) {
    AnimatedMatColorParams* p = params;
    s32 curFrame = d->step % p->keyFrameLength;
    s32 k = MatAnim_CurKeyFrame(p, curFrame);
    s32 next = (k + 1 < p->keyFrameCount) ? k + 1 : k;
    s32 startFrame = p->keyFrames[k];
    s32 endFrame = p->keyFrames[next] - startFrame;
    f32 norm = (endFrame != 0) ? (f32)(curFrame - startFrame) / (f32)endFrame : 0.0f;
    F3DPrimColor* primMin = p->primColors + k;
    F3DPrimColor* primMax = p->primColors + next;
    F3DPrimColor primResult;
    F3DEnvColor envResult;

    primResult.r = MatAnim_Lerp(primMin->r, primMax->r, norm);
    primResult.g = MatAnim_Lerp(primMin->g, primMax->g, norm);
    primResult.b = MatAnim_Lerp(primMin->b, primMax->b, norm);
    primResult.a = MatAnim_Lerp(primMin->a, primMax->a, norm);
    primResult.lodFrac = MatAnim_Lerp(primMin->lodFrac, primMax->lodFrac, norm);

    if (p->envColors != NULL) {
        F3DEnvColor* envMin = p->envColors + k;
        F3DEnvColor* envMax = p->envColors + next;

        envResult.r = MatAnim_Lerp(envMin->r, envMax->r, norm);
        envResult.g = MatAnim_Lerp(envMin->g, envMax->g, norm);
        envResult.b = MatAnim_Lerp(envMin->b, envMax->b, norm);
        envResult.a = MatAnim_Lerp(envMin->a, envMax->a, norm);
    }

    MatAnim_SetColor(d, segment, &primResult, (p->envColors != NULL) ? &envResult : NULL);
}

/**
 * Lagrange interpolation of n samples (x[i], fx[i]) at xp. Evaluated in double: the basis products
 * run over up to ANIM_MAT_MAX_KEY_FRAMES - 1 frame differences (each up to 65535), which overflows
 * f32 long before the key-frame limit and would turn the result into NaN.
 */
static f64 MatAnim_LagrangeInterp(s32 n, f64 x[], f64 fx[], f64 xp) {
    f64 weights[ANIM_MAT_MAX_KEY_FRAMES];
    f64 intp = 0.0;
    s32 i;
    s32 j;

    for (i = 0; i < n; i++) {
        f64 m = 1.0;

        for (j = 0; j < n; j++) {
            if (j != i) {
                m *= x[i] - x[j];
            }
        }
        weights[i] = fx[i] / m;
    }

    for (i = 0; i < n; i++) {
        f64 m = 1.0;

        for (j = 0; j < n; j++) {
            if (j != i) {
                m *= xp - x[j];
            }
        }
        intp += weights[i] * m;
    }

    return intp;
}

static u8 MatAnim_LagrangeInterpColor(s32 n, f64 x[], f64 fx[], f64 xp) {
    f64 intp = MatAnim_LagrangeInterp(n, x, fx, xp);

    // clamp in floating point: a value outside s32 (or NaN) is undefined behaviour to convert
    return (intp >= 255.0) ? 255 : (intp > 0.0) ? (u8)intp : 0;
}

/**
 * Type 4: colour key frames with non-linear (Lagrange) interpolation.
 */
static void MatAnim_DrawColorNonLinearInterp(MatAnimDraw* d, s32 segment, void* params) {
    AnimatedMatColorParams* p = params;
    f64 curFrame = d->step % p->keyFrameLength;
    s32 n = p->keyFrameCount;
    f64 x[ANIM_MAT_MAX_KEY_FRAMES];
    f64 fxPrim[5][ANIM_MAT_MAX_KEY_FRAMES];
    f64 fxEnv[4][ANIM_MAT_MAX_KEY_FRAMES];
    F3DPrimColor primResult;
    F3DEnvColor envResult;
    s32 i;

    for (i = 0; i < n; i++) {
        x[i] = p->keyFrames[i];
        fxPrim[0][i] = p->primColors[i].r;
        fxPrim[1][i] = p->primColors[i].g;
        fxPrim[2][i] = p->primColors[i].b;
        fxPrim[3][i] = p->primColors[i].a;
        fxPrim[4][i] = p->primColors[i].lodFrac;
        if (p->envColors != NULL) {
            fxEnv[0][i] = p->envColors[i].r;
            fxEnv[1][i] = p->envColors[i].g;
            fxEnv[2][i] = p->envColors[i].b;
            fxEnv[3][i] = p->envColors[i].a;
        }
    }

    primResult.r = MatAnim_LagrangeInterpColor(n, x, fxPrim[0], curFrame);
    primResult.g = MatAnim_LagrangeInterpColor(n, x, fxPrim[1], curFrame);
    primResult.b = MatAnim_LagrangeInterpColor(n, x, fxPrim[2], curFrame);
    primResult.a = MatAnim_LagrangeInterpColor(n, x, fxPrim[3], curFrame);
    primResult.lodFrac = MatAnim_LagrangeInterpColor(n, x, fxPrim[4], curFrame);

    if (p->envColors != NULL) {
        envResult.r = MatAnim_LagrangeInterpColor(n, x, fxEnv[0], curFrame);
        envResult.g = MatAnim_LagrangeInterpColor(n, x, fxEnv[1], curFrame);
        envResult.b = MatAnim_LagrangeInterpColor(n, x, fxEnv[2], curFrame);
        envResult.a = MatAnim_LagrangeInterpColor(n, x, fxEnv[3], curFrame);
    }

    MatAnim_SetColor(d, segment, &primResult, (p->envColors != NULL) ? &envResult : NULL);
}

/**
 * Type 5: cycles through a list of textures (a flipbook). The segment is bound to the texture's
 * resource path; the interpreter resolves it when the material's SETTIMG reads the segment.
 */
static void MatAnim_DrawTexCycle(MatAnimDraw* d, s32 segment, void* params) {
    AnimatedMatTexCycleParams* p = params;
    s32 curFrame = d->step % p->keyFrameLength;

    MatAnim_BindSegment(d, segment, p->textureList[p->textureIndexList[curFrame]]);
}

/**
 * Binds every entry of the current scene setup's `materialAnims`. Runs from Scene_Draw after the
 * scene draw config, so an entry that names a segment the config also bound is the one that
 * stays in effect when the rooms draw.
 */
void Scene_DrawMaterialAnims(PlayState* play) {
    static void (*sHandlers[ANIM_MAT_TYPE_MAX])(MatAnimDraw*, s32, void*) = {
        MatAnim_DrawTexScroll, MatAnim_DrawTwoTexScroll,         MatAnim_DrawColor,
        MatAnim_DrawColorLerp, MatAnim_DrawColorNonLinearInterp, MatAnim_DrawTexCycle,
    };
    MatAnimDraw d;
    u32 i;

    if (play->sceneMaterialAnims == NULL || play->sceneMaterialAnimCount == 0) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    d.gfxCtx = play->state.gfxCtx;
    d.step = play->gameplayFrames;

    for (i = 0; i < play->sceneMaterialAnimCount; i++) {
        AnimatedMaterial* anim = &play->sceneMaterialAnims[i];

        if (anim->type >= ANIM_MAT_TYPE_MAX || anim->params == NULL) {
            continue; // the loader rejects these; belt and braces for a hand-built list
        }
        d.pass = anim->pass;
        sHandlers[anim->type](&d, anim->segment, anim->params);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
