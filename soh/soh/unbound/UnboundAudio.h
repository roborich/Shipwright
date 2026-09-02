#pragma once
// SOH [Unbound] Scene-bound custom music (`sound.song`, SPEC §4.2).
//
// A custom sequence's numeric id is assigned at AudioLoad_Init, positionally over the sorted union of every
// mounted archive's `custom/music/*` — so a document can only refer to one by PATH, resolved here to the id
// the running game gave it.
#include <stdint.h>

#ifdef __cplusplus
#include <string>

namespace SOH::Unbound {
// The sequence id the engine assigned to `path` (e.g. "custom/music/Skyward"), or 0 when no mounted archive
// provides a loaded sequence there (also when the sequence was skipped over a missing soundfont). Valid
// after AudioLoad_Init; scene documents load later, so BuildSound may call it.
uint16_t SequenceIdForPath(const std::string& path);
} // namespace SOH::Unbound

extern "C" {
#endif

struct PlayState;
// Installs `songSeqId` (0 = none) as the scene's bound song on `play` and, when the vanilla theme id did
// not change across the scene transition but the effective song did, forces the next
// Environment_PlaySceneSequence to queue — its same-seq gate compares vanilla u8 ids and would otherwise
// keep the previous scene's audio playing.
void Unbound_BindSceneSong(struct PlayState* play, uint16_t songSeqId);

#ifdef __cplusplus
}
#endif
