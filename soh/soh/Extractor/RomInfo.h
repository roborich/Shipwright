#ifndef ROMINFO_H
#define ROMINFO_H

#include <stddef.h>
#include <stdint.h>

// What every ROM extractor needs to know about a ROM, and nothing about how it was found or
// who to tell when something is wrong: which dump it is, whether it is one we can extract,
// and the names Torch and the archive take from that. Pure functions over the ROM bytes, so
// the desktop Extractor (with its message boxes) and the wasm converter share one truth.
//
// The bytes are expected in big-endian (.z64) order; see ToBigEndian.
namespace RomInfo {

static constexpr size_t MB_BASE = 1024 * 1024;
static constexpr size_t MB32 = 32 * MB_BASE;
static constexpr size_t MB54 = 54 * MB_BASE;
static constexpr size_t MB64 = 64 * MB_BASE;

// Puts a dump in any byte order (.z64, .v64, .n64) into big-endian (.z64) order, in place,
// judged by its first byte. Anything else is left alone, for the checks below to refuse.
void ToBigEndian(uint8_t* rom, size_t romSize);

// The header CRC word at 0x10, which identifies the game version. 0 for a file too short
// to hold one.
uint32_t HeaderCrc(const uint8_t* rom, size_t romSize);

// Whether the header CRC names a version we know how to extract.
bool IsKnownVersion(uint32_t headerCrc);

// Human-readable version name ("NTSC N64 1.0"), or an empty string for an unknown CRC.
const char* VersionName(uint32_t headerCrc);

// Master Quest dumps get their own archive. False for an unknown CRC.
bool IsMasterQuest(uint32_t headerCrc);

// The version's directory in the asset yml tree ("ntsc_1-0"), matching the `path` Torch
// resolves each ROM hash to in config.yml, or nullptr for a version Torch cannot extract.
const char* TorchVersionDir(uint32_t headerCrc);

// "oot.o2r" or "oot-mq.o2r".
const char* ArchiveName(uint32_t headerCrc);

bool IsValidSize(size_t romSize);

// A ROM picked by typing a path can be anything; catch the common archive formats.
bool LooksCompressed(const uint8_t* rom, size_t romSize);

// Checks the whole file against the list of known-good dumps. Some MQ debug dumps have the
// header patched to look like a US ROM; the CRC is taken as if it were changed back, but the
// bytes are left as the original dump for Torch.
bool MatchesKnownDump(uint8_t* rom, size_t romSize);

} // namespace RomInfo

#endif
