#pragma once
// SOH [Unbound] Custom scenes on the Better Debug Warp screen (z_select.c). See unbound-docs/registries.md.
#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// The Better Debug Warp list: the `vanillaCount` entries of `vanilla`, then one entry per custom scene that has
// at least one registered entrance, each warping through `loadFunc`. The list lives until the next call; its
// length is written to `outCount`.
BetterSceneSelectEntry* UnboundSceneSelect_BuildList(const BetterSceneSelectEntry* vanilla, s32 vanillaCount,
                                                     void (*loadFunc)(struct SelectContext*, s32), s32* outCount);

#ifdef __cplusplus
}
#endif
