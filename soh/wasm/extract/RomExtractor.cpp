// SOH [WASM] The converter's entry point: a ROM file in the virtual filesystem becomes an
// oot.o2r / oot-mq.o2r in it. This is the desktop Extractor::CallZapd path with the UI
// removed -- the same checks (RomInfo), the same argv, the same zapd_report() -- so the
// archive it produces is the one the desktop build would have made from the same ROM.
//
// Called from api.js (--post-js), which is the host-facing side; see soh/wasm/HOST-API.md.

#include "soh/Extractor/RomInfo.h"

#include <ship/utils/binarytools/BitConverter.h>

#include <atomic>
#include <exception>
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

// Where the embedded extraction recipe lives (see CMakeLists.txt); ZAPD's configs name
// their inputs relative to it, so the run happens with this as the working directory.
constexpr const char* kWorkDir = "/work";

// The smallest prefix the checks below read: the header CRC at 0x10 and the archive
// signatures at the start. Real ROMs are checked for their full size after that.
constexpr size_t kHeaderBytes = 0x40;

// Negative so a host can tell them from ZAPD's own return value. Listed in HOST-API.md.
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
            return "The rom's version is not one this build can extract. Please find another.";
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
    std::string detail; // what ZAPD said, when it failed
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

// The desktop Extractor's ValidateRom, minus the message boxes, in its order. The bytes are
// only read here: ZAPD and the exporter both put the file into .z64 order themselves, and
// the header patch FixAndCheckCrc applies is one neither of them looks at.
Status CheckRom(std::vector<uint8_t>& rom) {
    if (rom.size() < kHeaderBytes) {
        return Status::BadSize;
    }
    BitConverter::RomToBigEndian(rom.data(), rom.size());
    if (RomInfo::LooksCompressed(rom.data())) {
        return Status::Compressed;
    }
    if (!RomInfo::IsValidSize(rom.size())) {
        return Status::BadSize;
    }
    // Not the desktop's version table (which only labels its ROM picker) but the set ZAPD has
    // an extraction recipe for, since that is what decides whether the next step can run.
    if (RomInfo::ZapdVersionString(RomInfo::HeaderCrc(rom.data())) == nullptr) {
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

// ZAPD reports a fatal problem by throwing (WarningHandler::PrintErrorAndThrow), not by
// its return value, so the failure is what was thrown.
Status RunZapd(const std::vector<std::string>& args, std::string& detail) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    std::atomic<size_t> extracted = 0, total = 0;
    try {
        zapd_report(static_cast<int>(argv.size()), argv.data(), &extracted, &total);
    } catch (const std::exception& e) {
        detail = e.what();
        return Status::ZapdFailed;
    } catch (...) {
        detail = "unknown exception";
        return Status::ZapdFailed;
    }
    return Status::Ok;
}

// Runs ZAPD in the recipe directory and moves what it wrote to outPath.
Status ExtractArchive(const std::string& romPath, const char* zapdVersion, const fs::path& outPath,
                      std::string& detail) {
    std::error_code ec;
    fs::create_directories(outPath.parent_path(), ec);
    fs::current_path(kWorkDir, ec);
    if (ec) {
        detail = "cannot enter " + std::string(kWorkDir);
        return Status::ZapdFailed;
    }
    const std::string archive = outPath.filename().string();
    fs::remove(archive, ec);

    const Status ran = RunZapd(ZapdArgs(fs::absolute(romPath).string(), zapdVersion, archive.c_str()), detail);
    if (ran != Status::Ok) {
        return ran;
    }
    if (!fs::exists(archive, ec)) {
        return Status::NoArchive;
    }
    fs::rename(archive, outPath, ec);
    if (ec) {
        // Across MEMFS mount points a rename can fail; copy instead.
        fs::copy_file(archive, outPath, fs::copy_options::overwrite_existing, ec);
        fs::remove(archive, ec);
    }
    return ec ? Status::NoArchive : Status::Ok;
}

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

int Finish(const Result& result) {
    std::string error = Describe(result.status);
    if (!result.detail.empty()) {
        error += " " + result.detail;
    }
    gLastResultJson = "{\"code\":" + std::to_string(static_cast<int>(result.status)) + ",\"error\":" + Quote(error) +
                      ",\"version\":" + Quote(result.version) + ",\"archive\":" + Quote(result.archive) + "}";
    return static_cast<int>(result.status);
}

} // namespace

extern "C" {

// Reads the ROM at `romPath` (any of .z64/.n64/.v64 byte orders), validates it as the desktop
// game does, runs ZAPD with the OTR exporter, and leaves `<outDir>/oot.o2r` or
// `<outDir>/oot-mq.o2r` behind. Returns 0 on success or a negative Status; the reason, the
// detected version and the archive name are in Extract_ResultJson() either way. Progress is
// ZAPD's own stdout: one "(i / N): <xml path>" line per file.
//
// One conversion per module instance: ZAPD keeps process-lifetime state between calls.
int Extract_RomToO2r(const char* romPath, const char* outDir) {
    Result result;

    std::vector<uint8_t> rom = ReadFile(romPath);
    if (rom.empty()) {
        result.status = Status::CannotRead;
        return Finish(result);
    }
    result.status = CheckRom(rom);
    if (result.status != Status::Ok) {
        return Finish(result);
    }

    const uint32_t headerCrc = RomInfo::HeaderCrc(rom.data());
    result.version = RomInfo::VersionName(headerCrc);
    result.archive = RomInfo::ArchiveName(headerCrc);
    const char* zapdVersion = RomInfo::ZapdVersionString(headerCrc);
    rom.clear();
    rom.shrink_to_fit();

    result.status = ExtractArchive(romPath, zapdVersion, fs::absolute(outDir) / result.archive, result.detail);
    return Finish(result);
}

// {"code":0,"error":"","version":"NTSC N64 1.0","archive":"oot.o2r"} for the last call.
const char* Extract_ResultJson() {
    return gLastResultJson.c_str();
}

} // extern "C"
