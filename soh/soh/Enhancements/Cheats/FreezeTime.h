#pragma once

#include <cstdint>

// A time of day set from outside the clock (a warp) becomes the frozen one while the Freeze
// Time cheat is on, so the clock stays still, at that time. Does nothing when it is off.
void FreezeTime_Retime(int32_t dayTime);
