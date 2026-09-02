#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <ship/resource/Resource.h>
#include "SceneCommand.h"
#include <libultraship/libultra/types.h>

namespace SOH {
typedef struct {
    uint8_t seqId;
    uint8_t natureAmbienceId;
    uint8_t reverb;
} SoundSettings;

class SetSoundSettings : public SceneCommand<SoundSettings> {
  public:
    using SceneCommand::SceneCommand;

    SoundSettings* GetPointer();
    size_t GetPointerSize();

    SoundSettings settings;
    // SOH [Unbound] The sequence id of the custom song a scene document binds with `sound.song`, resolved by
    // name at load (UnboundAudio.h) because custom ids are positional across the mounted archives. 0 = none:
    // every custom id is above the vanilla range, and 0 is what a binary scene (which has no song) leaves.
    uint16_t unboundSongSeqId = 0;
};
}; // namespace SOH
