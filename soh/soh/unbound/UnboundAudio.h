#pragma once
// SOH [Unbound] Name-keyed lookups over the audio engine's sequence table.
//
// A custom sequence's numeric id is assigned at AudioLoad_Init, positionally over the sorted union of every
// mounted archive's `custom/music/*` — so a document can only refer to one by PATH. This resolves that path
// to the id the running game gave it.
#include <cstdint>
#include <string>

namespace Unbound {
// The sequence id the engine assigned to `path` (e.g. "custom/music/Skyward"), or 0 when no mounted archive
// provides it. Valid after AudioLoad_Init; scene documents load later, so BuildSound may call it.
uint16_t SequenceIdForPath(const std::string& path);
} // namespace Unbound
