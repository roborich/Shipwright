#pragma once

#include <cstdint>
#include <string>
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
    // SOH [Unbound] The custom song a scene document binds with `sound.song`: the archive path it was bound
    // by (kept so an export can round-trip the binding) and its sequence id, resolved by name at load
    // (UnboundAudio.h) because custom ids are positional across the mounted archives. Empty/0 = none: every
    // custom id is above the vanilla range, and this is what a binary scene (which has no song) leaves.
    std::string unboundSongPath;
    uint16_t unboundSongSeqId = 0;
};
}; // namespace SOH
