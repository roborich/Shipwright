#pragma once

#include <stdint.h>
#include <vector>
#include "SceneCommand.h"
#include <libultraship/libultra/types.h>

namespace SOH {
typedef struct {
    /* 0x00 */ u8 spawn;
    /* 0x01 */ s16 room; // SOH [Unbound] u8 -> s16
} EntranceEntry;

class SetEntranceList : public SceneCommand<EntranceEntry> {
  public:
    using SceneCommand::SceneCommand;

    EntranceEntry* GetPointer();
    size_t GetPointerSize();

    uint32_t numEntrances;

    std::vector<EntranceEntry> entrances;
};
}; // namespace SOH
