#pragma once

#include <cstdint>
#include <string>

extern "C" {
#include "z64math.h"
}

// A place to warp to, and the save state that decides which scene layer is loaded there.
// Play_Init picks layers 0..3 from linkAge and the night flag, and the night flag from
// dayTime (night is above 0xC000 or below 0x4555), so those two fields are the whole
// "setup" story. Also the record stored under "WarpPoints" in the config.
//
// SOH [Unbound] entranceName, when set, names the entrance instead of entranceId: a vanilla
// enum name or a custom scene's `<scene id>/<entrance id>`. It is resolved when the warp runs,
// after the mods have registered their scenes, because a custom entrance's number is handed
// out as the mods load and so changes with the mod set.
typedef struct WarpPoint {
    int32_t entranceId = 0;
    std::string entranceName;
    int16_t roomNum = 0; // SOH [Unbound] s8 -> s16
    Vec3f pos = { 0.0f, 0.0f, 0.0f };
    int16_t rotY = 0;
    bool bootToPoint = false;
    int32_t linkAge = 0;      // LINK_AGE_ADULT
    int32_t dayTime = 0x8000; // noon
} WarpPoint;

// Warp to an entrance, spawning at the entrance's own spawn point.
void Warping_WarpToEntrance(int32_t entranceId, int32_t linkAge, int32_t dayTime);

// The entrance the point means: entranceName when set, else entranceId. -1 when the name is
// not registered (its mod is not loaded) or the index is off the table.
int32_t Warping_ResolveEntrance(const WarpPoint& point);

// Warp to an entrance and stand at the point's room, position and yaw. False, with nothing
// done, when the point's entrance does not resolve.
bool Warping_WarpToPoint(const WarpPoint& point);
