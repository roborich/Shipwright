// SOH [Unbound] Data-driven animated materials — a port of Majora's Mask's z_scene_proc.c (via 2Ship).
//
// A scene document's `materialAnims` (SPEC.md §4.2, types in z64scene.h) is a list of per-segment
// recipes. Every frame, after the scene draw config has run, Scene_DrawMaterialAnims walks the
// list and binds a freshly generated display list into each entry's runtime segment (8-13) on the
// passes it names. A room display list that calls that segment right before its triangles picks
// up the scrolled tile, the interpolated colours, or the current flipbook texture.
//
// Differences from MM: segments are absolute and the list carries a count (no negative-segment
// terminator); each entry chooses OPA/XLU itself; the scroll lists come from SoH's *Ex helpers so
// the motion interpolates between game frames at high frame rates.

#include "global.h"

typedef struct {
    PlayState* play;
    s32 step; // gameplay frame counter
    u8 pass;  // ANIM_MAT_PASS_* bits of the entry being drawn
} MatAnimDraw;

static void MatAnim_BindSegment(MatAnimDraw* d, s32 segment, void* data) {
    OPEN_DISPS(d->play->state.gfxCtx);

    if (d->pass & ANIM_MAT_PASS_OPA) {
        gSPSegment(POLY_OPA_DISP++, segment, data);
    }
    if (d->pass & ANIM_MAT_PASS_XLU) {
        gSPSegment(POLY_XLU_DISP++, segment, data);
    }

    CLOSE_DISPS(d->play->state.gfxCtx);
}

/**
 * Type 0: scrolls a single layer texture.
 */
static void MatAnim_DrawTexScroll(MatAnimDraw* d, s32 segment, void* params) {
    AnimatedMatTexScrollParams* p = params;
    Gfx* dl = Gfx_TexScrollEx(d->play->state.gfxCtx, p->xStep * d->step, -(p->yStep * d->step), p->width, p->height,
                              p->xStep, -p->yStep);

    MatAnim_BindSegment(d, segment, dl);
}

/**
 * Type 1: scrolls two texture layers (render tiles 0 and 1).
 */
static void MatAnim_DrawTwoTexScroll(MatAnimDraw* d, s32 segment, void* params) {
    AnimatedMatTexScrollParams* p = params;
    Gfx* dl = Gfx_TwoTexScrollEx(d->play->state.gfxCtx, 0, p[0].xStep * d->step, -(p[0].yStep * d->step), p[0].width,
                                 p[0].height, 1, p[1].xStep * d->step, -(p[1].yStep * d->step), p[1].width, p[1].height,
                                 p[0].xStep, -p[0].yStep, p[1].xStep, -p[1].yStep);

    MatAnim_BindSegment(d, segment, dl);
}

/**
 * Generates a display list that sets the prim colour (and env colour when given) and binds it.
 */
static void MatAnim_SetColor(MatAnimDraw* d, s32 segment, F3DPrimColor* prim, F3DEnvColor* env) {
    Gfx* gfx = Graph_Alloc(d->play->state.gfxCtx, 3 * sizeof(Gfx));
    Gfx* head = gfx;

    gDPSetPrimColor(gfx++, 0, prim->lodFrac, prim->r, prim->g, prim->b, prim->a);
    if (env != NULL) {
        gDPSetEnvColor(gfx++, env->r, env->g, env->b, env->a);
    }
    gSPEndDisplayList(gfx++);

    MatAnim_BindSegment(d, segment, head);
}

/**
 * Type 2: colour key frames without interpolation.
 */
static void MatAnim_DrawColor(MatAnimDraw* d, s32 segment, void* params) {
    AnimatedMatColorParams* p = params;
    s32 curFrame = d->step % p->keyFrameLength;
    F3DPrimColor* prim = p->primColors + curFrame;
    F3DEnvColor* env = (p->envColors != NULL) ? p->envColors + curFrame : NULL;

    MatAnim_SetColor(d, segment, prim, env);
}

static s32 MatAnim_Lerp(s32 min, s32 max, f32 norm) {
    return (s32)((max - min) * norm) + min;
}

/**
 * Type 3: colour key frames with linear interpolation.
 */
static void MatAnim_DrawColorLerp(MatAnimDraw* d, s32 segment, void* params) {
    AnimatedMatColorParams* p = params;
    u16* keyFrames = p->keyFrames;
    s32 curFrame = d->step % p->keyFrameLength;
    s32 i = 1;
    s32 startFrame;
    s32 endFrame;
    f32 norm;
    F3DPrimColor* primMin;
    F3DPrimColor* primMax;
    F3DPrimColor primResult;
    F3DEnvColor envResult;

    keyFrames++;
    while (p->keyFrameCount > i) {
        if (curFrame < *keyFrames) {
            break;
        }
        i++;
        keyFrames++;
    }

    startFrame = keyFrames[-1];
    endFrame = keyFrames[0] - startFrame;
    norm = (endFrame != 0) ? (f32)(curFrame - startFrame) / (f32)endFrame : 0.0f;

    primMax = p->primColors + i;
    primMin = primMax - 1;
    primResult.r = MatAnim_Lerp(primMin->r, primMax->r, norm);
    primResult.g = MatAnim_Lerp(primMin->g, primMax->g, norm);
    primResult.b = MatAnim_Lerp(primMin->b, primMax->b, norm);
    primResult.a = MatAnim_Lerp(primMin->a, primMax->a, norm);
    primResult.lodFrac = MatAnim_Lerp(primMin->lodFrac, primMax->lodFrac, norm);

    if (p->envColors != NULL) {
        F3DEnvColor* envMax = p->envColors + i;
        F3DEnvColor* envMin = envMax - 1;

        envResult.r = MatAnim_Lerp(envMin->r, envMax->r, norm);
        envResult.g = MatAnim_Lerp(envMin->g, envMax->g, norm);
        envResult.b = MatAnim_Lerp(envMin->b, envMax->b, norm);
        envResult.a = MatAnim_Lerp(envMin->a, envMax->a, norm);
    }

    MatAnim_SetColor(d, segment, &primResult, (p->envColors != NULL) ? &envResult : NULL);
}

/**
 * Lagrange interpolation of n samples (x[i], fx[i]) at xp.
 */
static f32 MatAnim_LagrangeInterp(s32 n, f32 x[], f32 fx[], f32 xp) {
    f32 weights[ANIM_MAT_MAX_KEY_FRAMES];
    f32 intp = 0.0f;
    s32 i;
    s32 j;

    for (i = 0; i < n; i++) {
        f32 m = 1.0f;

        for (j = 0; j < n; j++) {
            if (j != i) {
                m *= x[i] - x[j];
            }
        }
        weights[i] = fx[i] / m;
    }

    for (i = 0; i < n; i++) {
        f32 m = 1.0f;

        for (j = 0; j < n; j++) {
            if (j != i) {
                m *= xp - x[j];
            }
        }
        intp += weights[i] * m;
    }

    return intp;
}

static u8 MatAnim_LagrangeInterpColor(s32 n, f32 x[], f32 fx[], f32 xp) {
    s32 intp = MatAnim_LagrangeInterp(n, x, fx, xp);

    return CLAMP(intp, 0, 255);
}

/**
 * Type 4: colour key frames with non-linear (Lagrange) interpolation.
 */
static void MatAnim_DrawColorNonLinearInterp(MatAnimDraw* d, s32 segment, void* params) {
    AnimatedMatColorParams* p = params;
    f32 curFrame = d->step % p->keyFrameLength;
    s32 n = p->keyFrameCount;
    f32 x[ANIM_MAT_MAX_KEY_FRAMES];
    f32 fxPrim[5][ANIM_MAT_MAX_KEY_FRAMES];
    f32 fxEnv[4][ANIM_MAT_MAX_KEY_FRAMES];
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

    d.play = play;
    d.step = play->gameplayFrames;

    for (i = 0; i < play->sceneMaterialAnimCount; i++) {
        AnimatedMaterial* anim = &play->sceneMaterialAnims[i];

        if (anim->type >= ANIM_MAT_TYPE_MAX || anim->params == NULL) {
            continue; // the loader rejects these; belt and braces for a hand-built list
        }
        d.pass = anim->pass;
        sHandlers[anim->type](&d, anim->segment, anim->params);
    }
}
