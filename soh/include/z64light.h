#ifndef Z64LIGHT_H
#define Z64LIGHT_H

#include <libultraship/libultra.h>
#include <libultraship/libultra/gbi.h>
#include "z64math.h"
#include <libultraship/color.h>

typedef struct {
    f32 x; // SOH [Unbound] s16 -> f32 (world extent)
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

typedef struct Lights {
    /* 0x00 */ u8 numLights;
    /* 0x08 */ Lightsn l;
} Lights; // size = 0x80

typedef struct LightNode {
    /* 0x0 */ LightInfo* info;
    /* 0x4 */ struct LightNode* prev;
    /* 0x8 */ struct LightNode* next;
} LightNode; // size = 0xC

typedef struct {
    /* 0x0 */ LightNode* listHead;
    /* 0x4 */ u8 ambientColor[3];
    /* 0x7 */ u8 fogColor[3];
    /* 0xA */ s16 fogNear; // how close until fog starts taking effect. range 0 - 1000
    /* 0xC */ s16 fogFar; // far plane / room cull distance (vanilla max 12800); clamped to s16 when worldFog is set
    // SOH [Unbound] world-unit fog + draw distance; see EnvLightSettings. zNear/zFar drive the projection and
    // room culling in every scene (vanilla: 10 / fogFar).
    u8 worldFog;
    f32 fogStart;
    f32 fogEnd;
    f32 zNear;
    f32 zFar;
} LightContext;

typedef enum {
    /* 0x00 */ LIGHT_POINT_NOGLOW,
    /* 0x01 */ LIGHT_DIRECTIONAL,
    /* 0x02 */ LIGHT_POINT_GLOW
} LightType;

typedef void (*LightsBindFunc)(Lights* lights, LightParams* params, Vec3f* vec);

#endif
