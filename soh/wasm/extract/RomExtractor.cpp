// SOH [WASM] The converter's entry point: a ROM file in the virtual filesystem becomes an
// oot.o2r / oot-mq.o2r in it. This is the desktop Extractor::CallZapd path with the UI
// removed -- the same checks (RomInfo), the same argv, the same zapd_report() -- so the
// archive it produces is the one the desktop build would have made from the same ROM.
//
// Called from api.js (--post-js), which is the host-facing side; see soh/wasm/HOST-API.md.

#include "soh/Extractor/RomInfo.h"

#include <ship/utils/binarytools/BitConverter.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" int zapd_report(int argc, char** argv, std::atomic<size_t>* extractCount, std::atomic<size_t>* totalExtract);

// ZAPD's crash handler prints a native backtrace via execinfo.h, which Emscripten does not
// have, so CrashHandler.cpp is not in this build (see CMakeLists.txt). A browser gives a
// better stack trace on its own when the module traps.
void CrashHandler_Init() {
}

namespace {

namespace fs = std::filesystem;

// Where the preloaded extraction recipe lives (see CMakeLists.txt); ZAPD's configs name
// their inputs relative to it, so the run happens with this as the working directory.
constexpr const char* kWorkDir = "/work";

enum class Status : int {
    Ok = 0,
    CannotRead = -1,
    BadSize = -2,
    Compressed = -3,
    UnknownVersion = -4,
    BadCrc = -5,
    ZapdFailed = -6,
    NoArchive = -7,
};

const char* Describe(Status status) {
    switch (status) {
        case Status::Ok:
            return "";
        case Status::CannotRead:
            return "The ROM file could not be read.";
        case Status::BadSize:
            return "The rom file was not a valid size. Expecting 32, 54, or 64MB.";
        case Status::Compressed:
            return "The selected file appears to be compressed. Please extract before using.";
        case Status::UnknownVersion:
            return "Rom CRC did not match the list of known compatible roms. Please find another.";
        case Status::BadCrc:
            return "Rom CRC did not match the list of known compatible roms. Please find another.";
        case Status::ZapdFailed:
            return "Extraction failed.";
        case Status::NoArchive:
            return "Extraction finished but produced no archive.";
    }
    return "";
}

struct Result {
    Status status = Status::Ok;
    std::string version;
    std::string archive;
};

std::string gLastResultJson;

std::vector<uint8_t> ReadFile(const char* path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return {};
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    return bytes;
}

bool WriteFile(const char* path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return out.good();
}

// Puts the ROM into .z64 byte order in place, and back on disk if that changed anything,
// so both our checks and ZAPD's read see the same bytes.
bool NormalizeByteOrder(const char* romPath, std::vector<uint8_t>& rom) {
    const uint32_t before = RomInfo::HeaderCrc(rom.data());
    BitConverter::RomToBigEndian(rom.data(), rom.size());
    if (RomInfo::HeaderCrc(rom.data()) == before) {
        return true;
    }
    return WriteFile(romPath, rom);
}

// The desktop Extractor's ValidateRom, minus the message boxes.
Status CheckRom(std::vector<uint8_t>& rom) {
    if (!RomInfo::IsValidSize(rom.size())) {
        return Status::BadSize;
    }
    if (RomInfo::LooksCompressed(rom.data())) {
        return Status::Compressed;
    }
    if (!RomInfo::IsKnownVersion(RomInfo::HeaderCrc(rom.data()))) {
        return Status::UnknownVersion;
    }
    if (!RomInfo::FixAndCheckCrc(rom.data(), rom.size())) {
        return Status::BadCrc;
    }
    return Status::Ok;
}

// Identical to the argv Extractor::CallZapd builds, so the two stay comparable.
std::vector<std::string> ZapdArgs(const std::string& romPath, const char* version, const char* archive) {
    return {
        "ZAPD",      "ed",
        "-i",        std::string("assets/xml/") + version,
        "-b",        romPath,
        "-fl",       "assets/filelists",
        "-gsf",      "0",
        "-rconf",    std::string("assets/Config_") + version + ".xml",
        "-se",       "OTR",
        "--otrfile", archive,
        "--portVer", SOH_EXTRACT_PORT_VERSION,
        "-o",        "placeholder",
        "-osf",      "placeholder",
    };
}

int RunZapd(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    std::atomic<size_t> extracted = 0, total = 0;
    return zapd_report(static_cast<int>(argv.size()), argv.data(), &extracted, &total);
}

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

void RememberResult(const Result& result) {
    gLastResultJson = "{\"code\":" + std::to_string(static_cast<int>(result.status)) +
                      ",\"error\":" + Quote(Describe(result.status)) + ",\"version\":" + Quote(result.version) +
                      ",\"archive\":" + Quote(result.archive) + "}";
}

} // namespace

extern "C" {

// Reads the ROM at `romPath` (any of .z64/.n64/.v64 byte orders), validates it exactly as
// the desktop game does, runs ZAPD with the OTR exporter, and leaves `<outDir>/oot.o2r` or
// `<outDir>/oot-mq.o2r` behind. Returns 0 on success or a negative code; the reason, the
// detected version and the archive name are in Extract_ResultJson() either way. Progress
// is ZAPD's own stdout: one "(i / N): <xml path>" line per file.
//
// One conversion per module instance: ZAPD keeps process-lifetime state between calls.
int Extract_RomToO2r(const char* romPath, const char* outDir) {
    Result result;
    std::vector<uint8_t> rom = ReadFile(romPath);
    if (rom.empty()) {
        result.status = Status::CannotRead;
        RememberResult(result);
        return static_cast<int>(result.status);
    }
    // Size first: the byte-order and header reads below assume at least a header's worth.
    if (!RomInfo::IsValidSize(rom.size())) {
        result.status = Status::BadSize;
        RememberResult(result);
        return static_cast<int>(result.status);
    }
    if (!NormalizeByteOrder(romPath, rom)) {
        result.status = Status::CannotRead;
        RememberResult(result);
        return static_cast<int>(result.status);
    }

    const uint8_t headerByte3E = rom[0x3E];
    result.status = CheckRom(rom);
    if (result.status != Status::Ok) {
        RememberResult(result);
        return static_cast<int>(result.status);
    }

    const uint32_t headerCrc = RomInfo::HeaderCrc(rom.data());
    result.version = RomInfo::VersionName(headerCrc);
    result.archive = RomInfo::ArchiveName(headerCrc);
    const char* zapdVersion = RomInfo::ZapdVersionString(headerCrc);
    // FixAndCheckCrc may have patched the MQ debug header byte; ZAPD reads the file, so keep it current.
    if (rom[0x3E] != headerByte3E) {
        WriteFile(romPath, rom);
    }
    rom.clear();
    rom.shrink_to_fit();

    const std::string absoluteRom = fs::absolute(romPath).string();
    const fs::path outPath = fs::absolute(outDir) / result.archive;
    std::error_code ec;
    fs::create_directories(outPath.parent_path(), ec);
    fs::current_path(kWorkDir, ec);
    fs::remove(result.archive, ec);

    RunZapd(ZapdArgs(absoluteRom, zapdVersion, result.archive.c_str()));

    if (!fs::exists(result.archive)) {
        result.status = Status::NoArchive;
        RememberResult(result);
        return static_cast<int>(result.status);
    }
    fs::rename(result.archive, outPath, ec);
    if (ec) {
        // Across MEMFS mount points a rename can fail; copy instead.
        fs::copy_file(result.archive, outPath, fs::copy_options::overwrite_existing, ec);
        fs::remove(result.archive);
    }
    result.status = ec ? Status::NoArchive : Status::Ok;
    RememberResult(result);
    return static_cast<int>(result.status);
}

// {"code":0,"error":"","version":"NTSC N64 1.0","archive":"oot.o2r"} for the last call.
const char* Extract_ResultJson() {
    return gLastResultJson.c_str();
}

} // extern "C"
