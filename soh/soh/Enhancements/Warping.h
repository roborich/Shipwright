#pragma once

#include <cstdint>

extern "C" {
#include "z64math.h"
}

// A place to warp to, and the save state that decides which scene layer is loaded there.
// Play_Init picks layers 0..3 from linkAge and the night flag, and the night flag from
// dayTime (night is above 0xC000 or below 0x4555), so those two fields are the whole
// "setup" story. Also the record stored under "WarpPoints" in the config.
typedef struct WarpPoint {
    int32_t entranceId = 0;
    int16_t roomNum = 0; // SOH [Unbound] s8 -> s16
    Vec3f pos = { 0.0f, 0.0f, 0.0f };
    int16_t rotY = 0;
    bool bootToPoint = false;
    int32_t linkAge = 0;      // LINK_AGE_ADULT
    int32_t dayTime = 0x8000; // noon
} WarpPoint;

// Warp to an entrance, spawning at the entrance's own spawn point.
void Warping_WarpToEntrance(int32_t entranceId, int32_t linkAge, int32_t dayTime);

// Warp to an entrance and stand at the point's room, position and yaw.
void Warping_WarpToPoint(const WarpPoint& point);
