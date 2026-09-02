#include "UnboundAudio.h"

#include <z64.h>
#include "sequence.h"
#include "soh/ResourceManagerHelpers.h"

// audio_load.c's table: sequence id -> the archive path the id was built from (vanilla and custom alike).
// The allocation over-reserves 0xF slots because custom-id assignment can push past sequenceMapSize.
extern "C" char** sequenceMap;
extern "C" size_t sequenceMapSize;
extern "C" SaveContext gSaveContext;

namespace SOH::Unbound {
uint16_t SequenceIdForPath(const std::string& path) {
    if (path.empty() || sequenceMap == nullptr) {
        return 0;
    }
    // The engine stamps the assigned id on the cached sequence resource (AudioLoad_Init). A custom sequence
    // skipped over a missing soundfont keeps a stale seqNumber, so validate the id against the table it
    // indexes before trusting it (the table is zeroed at allocation, so unassigned slots read as NULL).
    SequenceData* seq = ResourceMgr_LoadSeqPtrByName(path.c_str());
    if (seq == nullptr) {
        return 0;
    }
    size_t id = seq->seqNumber;
    if (id == 0 || id >= sequenceMapSize + 0xF || sequenceMap[id] == nullptr || path != sequenceMap[id]) {
        return 0;
    }
    return static_cast<uint16_t>(id);
}
} // namespace SOH::Unbound

extern "C" void Unbound_BindSceneSong(PlayState* play, uint16_t songSeqId) {
    // The song bound when a scene last ran its sound-settings command; a binary scene binds none.
    static uint16_t sPrevSongSeqId = 0;

    play->sequenceCtx.unboundSongSeqId = songSeqId;
    // Environment_PlaySceneSequence only queues when the vanilla u8 theme changes. Two scenes can share
    // that theme while binding different songs (or one binding none) — when only the effective song
    // changed, drop the "already playing" marker so the new scene's audio is queued (and resolved) fresh.
    if (songSeqId != sPrevSongSeqId && gSaveContext.seqId == play->sequenceCtx.seqId) {
        gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    }
    sPrevSongSeqId = songSeqId;
}
