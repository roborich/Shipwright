#include "RomInfo.h"

#include <array>
#include <unordered_map>

extern "C" uint32_t CRC32C(unsigned char* data, size_t dataSize);

namespace RomInfo {

static constexpr uint32_t OOT_PAL_GC = 0x09465AC3;
static constexpr uint32_t OOT_PAL_MQ = 0x1D4136F3;
static constexpr uint32_t OOT_PAL_GC_DBG1 = 0x871E1C92; // 03-21-2002 build
static constexpr uint32_t OOT_PAL_GC_DBG2 = 0x87121EFE; // 03-13-2002 build
static constexpr uint32_t OOT_PAL_GC_MQ_DBG = 0x917D18F6;
static constexpr uint32_t OOT_PAL_10 = 0xB044B569;
static constexpr uint32_t OOT_PAL_11 = 0xB2055FBD;
static constexpr uint32_t OOT_NTSC_US_GC = 0xF3DD35BA;
static constexpr uint32_t OOT_NTSC_JP_GC = 0xF611F4BA;
static constexpr uint32_t OOT_NTSC_JP_GC_CE = 0xF7F52DB8;
static constexpr uint32_t OOT_NTSC_US_MQ = 0xF034001A;
static constexpr uint32_t OOT_NTSC_JP_MQ = 0xF43B45BA;
static constexpr uint32_t OOT_NTSC_10 = 0xEC7011B7;
static constexpr uint32_t OOT_NTSC_11 = 0xD43DA81F;
static constexpr uint32_t OOT_NTSC_12 = 0x693BA2AE;

static const std::unordered_map<uint32_t, const char*> verMap = {
    { OOT_PAL_GC, "PAL Gamecube" },         { OOT_PAL_MQ, "PAL MQ" },
    { OOT_PAL_GC_DBG1, "PAL Debug 1" },     { OOT_PAL_GC_DBG2, "PAL Debug 2" },
    { OOT_PAL_GC_MQ_DBG, "PAL MQ Debug" },  { OOT_PAL_10, "PAL N64 1.0" },
    { OOT_PAL_11, "PAL N64 1.1" },          { OOT_NTSC_US_GC, "NTSC Gamecube US" },
    { OOT_NTSC_JP_GC, "NTSC Gamecube JP" }, { OOT_NTSC_JP_GC_CE, "NTSC Gamecube JP (Collector's Edition)" },
    { OOT_NTSC_US_GC, "NTSC MQ US" },       { OOT_NTSC_JP_GC, "NTSC MQ JP" },
    { OOT_NTSC_10, "NTSC N64 1.0" },        { OOT_NTSC_11, "NTSC N64 1.1" },
    { OOT_NTSC_12, "NTSC N64 1.2" },
};

// TODO only check the first 54MB of the rom.
static constexpr std::array<const uint32_t, 21> goodCrcs = {
    0xfa8c0555, // MQ DBG 64MB (Original overdump)
    0x8652ac4c, // MQ DBG 64MB
    0x5B8A1EB7, // MQ DBG 64MB (Empty overdump)
    0x1f731ffe, // MQ DBG 54MB
    0x044b3982, // NMQ DBG 54MB
    0xEB15D7B9, // NMQ DBG 64MB
    0xDA8E61BF, // GC PAL
    0x7A2FAE68, // GC MQ PAL
    0xFD9913B1, // N64 PAL 1.0
    0xE033FBBA, // N64 PAL 1.1
    0x460C938C, // N64 NTSC US 1.0
    0xD0C76FA9, // N64 NTSC JP 1.0
    0x3496EE47, // N64 NTSC US 1.1
    0xA25D1262, // N64 NTSC JP 1.1
    0x15736A58, // N64 NTSC US 1.2
    0x83B8967D, // N64 NTSC JP 1.2
    0xD61453DE, // GC NTSC US
    0x4129C825, // GC MQ NTSC US
    0x11A4BE61, // GC NTSC JP
    0x2BC6C6FD, // GC NTSC JP Collector's Edition
    0x02CD974C, // GC MQ NTSC JP
};

uint32_t HeaderCrc(const uint8_t* rom) {
    return ((uint32_t)rom[0x10] << 24) | ((uint32_t)rom[0x11] << 16) | ((uint32_t)rom[0x12] << 8) | (uint32_t)rom[0x13];
}

bool IsKnownVersion(uint32_t headerCrc) {
    return verMap.contains(headerCrc);
}

const char* VersionName(uint32_t headerCrc) {
    auto it = verMap.find(headerCrc);
    return it == verMap.end() ? "" : it->second;
}

bool IsMasterQuest(uint32_t headerCrc) {
    switch (headerCrc) {
        case OOT_PAL_MQ:
        case OOT_PAL_GC_MQ_DBG:
        case OOT_NTSC_US_MQ:
        case OOT_NTSC_JP_MQ:
            return true;
        default:
            return false;
    }
}

const char* ZapdVersionString(uint32_t headerCrc) {
    switch (headerCrc) {
        case OOT_PAL_GC:
            return "GC_NMQ_PAL_F";
        case OOT_PAL_MQ:
            return "GC_MQ_PAL_F";
        case OOT_PAL_GC_DBG1:
            return "GC_NMQ_D";
        case OOT_PAL_GC_MQ_DBG:
            return "GC_MQ_D";
        case OOT_PAL_10:
            return "N64_PAL_10";
        case OOT_PAL_11:
            return "N64_PAL_11";
        case OOT_NTSC_US_GC:
            return "GC_NMQ_NTSC_U";
        case OOT_NTSC_JP_GC:
            return "GC_NMQ_NTSC_J";
        case OOT_NTSC_JP_GC_CE:
            return "GC_NMQ_NTSC_J_CE";
        case OOT_NTSC_US_MQ:
            return "GC_MQ_NTSC_U";
        case OOT_NTSC_JP_MQ:
            return "GC_MQ_NTSC_J";
        case OOT_NTSC_10:
            return "N64_NTSC_10";
        case OOT_NTSC_11:
            return "N64_NTSC_11";
        case OOT_NTSC_12:
            return "N64_NTSC_12";
        default:
            return nullptr;
    }
}

const char* ArchiveName(uint32_t headerCrc) {
    return IsMasterQuest(headerCrc) ? "oot-mq.o2r" : "oot.o2r";
}

bool IsValidSize(size_t romSize) {
    return romSize == MB32 || romSize == MB54 || romSize == MB64;
}

bool LooksCompressed(const uint8_t* rom) {
    // ZIP file header
    if (rom[0] == 'P' && rom[1] == 'K' && rom[2] == 0x03 && rom[3] == 0x04) {
        return true;
    }
    // RAR file header. Only the first 4 bytes.
    if (rom[0] == 'R' && rom[1] == 'a' && rom[2] == 'r' && rom[3] == 0x21) {
        return true;
    }
    // 7z file header. 37 7A BC AF 27 1C
    if (rom[0] == '7' && rom[1] == 'z' && rom[2] == 0xBC && rom[3] == 0xAF && rom[4] == 0x27 && rom[5] == 0x1C) {
        return true;
    }
    return false;
}

bool FixAndCheckCrc(uint8_t* rom, size_t romSize) {
    // The MQ debug rom sometimes has the header patched to look like a US rom. Change it back
    if (HeaderCrc(rom) == OOT_PAL_GC_MQ_DBG) {
        rom[0x3E] = 'P';
    }

    const uint32_t actualCrc = CRC32C(rom, romSize);

    for (const uint32_t crc : goodCrcs) {
        if (actualCrc == crc) {
            return true;
        }
    }
    return false;
}

} // namespace RomInfo
