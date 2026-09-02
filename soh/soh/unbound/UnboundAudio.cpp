#include "UnboundAudio.h"
#include <cstring>

// audio_load.c's table: sequence id -> the archive path the id was built from (vanilla and custom alike).
extern "C" char** sequenceMap;
extern "C" size_t sequenceMapSize;

namespace Unbound {
uint16_t SequenceIdForPath(const std::string& path) {
    if (sequenceMap == nullptr || path.empty()) {
        return 0;
    }
    for (size_t id = 1; id < sequenceMapSize; id++) {
        if (sequenceMap[id] != nullptr && path == sequenceMap[id]) {
            return static_cast<uint16_t>(id);
        }
    }
    return 0;
}
} // namespace Unbound
