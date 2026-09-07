#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <ship/resource/Resource.h>
#include "SceneCommand.h"
#include <libultraship/libultra/types.h>

namespace SOH {
// SOH [Unbound] must stay identical to ::LightPoint (z64light.h): Scene_CommandLightList casts one to the other
typedef struct {
    f32 x;
    f32 y;
    f32 z;
    u8 color[3];
    u8 drawGlow;
    s16 radius;
} LightPoint;

typedef struct {
    /* 0x0 */ s8 x;
    /* 0x1 */ s8 y;
    /* 0x2 */ s8 z;
    /* 0x3 */ u8 color[3];
} LightDirectional; // size = 0x6

typedef union {
    LightPoint point;
    LightDirectional dir;
} LightParams; // size = 0xC

typedef struct {
    /* 0x0 */ u8 type;
    /* 0x2 */ LightParams params;
} LightInfo; // size = 0xE

class SetLightList final : public SceneCommand<LightInfo> {
  public:
    using SceneCommand::SceneCommand;

    LightInfo* GetPointer();
    size_t GetPointerSize();

    uint32_t numLights;
    std::vector<LightInfo> lightList;
};
}; // namespace SOH
