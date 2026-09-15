#ifndef ROMINFO_H
#define ROMINFO_H

#include <stddef.h>
#include <stdint.h>

// What every ROM extractor needs to know about a ROM, and nothing about how it was found or
// who to tell when something is wrong: which dump it is, whether it is one we can extract,
// and the names ZAPD and the archive take from that. Pure functions over the ROM bytes, so
// the desktop Extractor (with its message boxes) and the wasm converter share one truth.
//
// The bytes are expected in big-endian (.z64) order; see BitConverter::RomToBigEndian.
namespace RomInfo {

static constexpr size_t MB_BASE = 1024 * 1024;
static constexpr size_t MB32 = 32 * MB_BASE;
static constexpr size_t MB54 = 54 * MB_BASE;
static constexpr size_t MB64 = 64 * MB_BASE;

// The header CRC word at 0x10, which identifies the game version.
uint32_t HeaderCrc(const uint8_t* rom);

// Whether the header CRC names a version we know how to extract.
bool IsKnownVersion(uint32_t headerCrc);

// Human-readable version name ("NTSC N64 1.0"), or an empty string for an unknown CRC.
const char* VersionName(uint32_t headerCrc);

// Master Quest dumps get their own archive. False for an unknown CRC.
bool IsMasterQuest(uint32_t headerCrc);

// The name of the ZAPD asset set for this version ("N64_NTSC_10"), or nullptr if unknown.
const char* ZapdVersionString(uint32_t headerCrc);

// "oot.o2r" or "oot-mq.o2r".
const char* ArchiveName(uint32_t headerCrc);

bool IsValidSize(size_t romSize);

// A ROM picked by typing a path can be anything; catch the common archive formats.
bool LooksCompressed(const uint8_t* rom);

// Applies the header fix some MQ debug dumps need, in place, then checks the whole file
// against the list of known-good dumps.
bool FixAndCheckCrc(uint8_t* rom, size_t romSize);

} // namespace RomInfo

#endif
