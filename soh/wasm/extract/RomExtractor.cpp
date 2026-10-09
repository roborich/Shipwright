// SOH [WASM] The converter's entry point: a ROM file in the virtual filesystem becomes an
// oot.o2r / oot-mq.o2r in it. This is the desktop Extractor::CallTorch path with the UI
// removed -- the same checks (RomInfo), the same recipe, the same SohTorch extraction -- so
// the archive it produces is the one the desktop build would have made from the same ROM.
//
// Called from api.js (--post-js), which is the host-facing side; see soh/wasm/HOST-API.md.

#include "soh/Extractor/RomInfo.h"
#include "soh/Extractor/TorchExtract.h"

#include <emscripten.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Where the embedded extraction recipe lives (see CMakeLists.txt): config.yml and one
// directory of asset ymls per ROM version.
constexpr const char* kRecipeDir = "/work/assets";

// The smallest prefix the checks below read: the header CRC at 0x10 and the archive
// signatures at the start. Real ROMs are checked for their full size after that.
constexpr size_t kHeaderBytes = 0x40;

// Negative so a host can tell them from success. Listed in HOST-API.md.
enum class Status : int {
    Ok = 0,
    CannotRead = -1,
    BadSize = -2,
    Compressed = -3,
    UnknownVersion = -4,
    BadCrc = -5,
    ExtractFailed = -6,
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
        case Status::ExtractFailed:
            return "Extraction failed.";
        case Status::NoArchive:
            return "Extraction finished but produced no archive.";
    }
    return "";
}

struct Result {
    Status status = Status::Ok;
    std::string detail; // what Torch said, when it failed
    std::string version;
    std::string archive;
};

std::string gLastResultJson;

// Torch logs through spdlog's default logger. Warnings and errors go to stderr, where api.js
// collects them for a failed run's message; the rest stays on stdout, which `quiet` drops.
// That includes `critical`, which Torch uses for its always-shown banner and timing lines.
// Under `quiet` the sink's own level drops info and below before they are formatted or cross
// into JS; it has to be the sink's, because Torch's Init resets every logger to debug.
class SplitConsoleSink final : public spdlog::sinks::base_sink<std::mutex> {
  protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        const bool problem = msg.level == spdlog::level::warn || msg.level == spdlog::level::err;
        FILE* out = problem ? stderr : stdout;
        fwrite(formatted.data(), 1, formatted.size(), out);
    }
    void flush_() override {
        fflush(stdout);
        fflush(stderr);
    }
};

void InstallLogger(bool quiet) {
    auto sink = std::make_shared<SplitConsoleSink>();
    if (quiet) {
        sink->set_level(spdlog::level::warn);
    }
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("soh-extract", sink));
}

void ReportProgress(size_t done, size_t total) {
    // clang-format off
    EM_ASM({ if (Module['_sohExtractProgress']) Module['_sohExtractProgress']($0, $1); }, (double)done, (double)total);
    // clang-format on
}

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

// The desktop Extractor's ValidateRom (compressed, size, CRC) minus the message boxes, plus
// one check it does not make: that Torch has a recipe for this version, which the desktop
// only discovers when its picker asks for a version directory that does not exist. Leaves
// the bytes in .z64 order, which is what Torch hashes against config.yml.
Status CheckRom(std::vector<uint8_t>& rom) {
    if (rom.size() < kHeaderBytes) {
        return Status::BadSize;
    }
    // Before the byte swap: a 7z starts with '7' (0x37), which is also the .v64 marker.
    if (RomInfo::LooksCompressed(rom.data(), rom.size())) {
        return Status::Compressed;
    }
    RomInfo::ToBigEndian(rom.data(), rom.size());
    if (!RomInfo::IsValidSize(rom.size())) {
        return Status::BadSize;
    }
    // Not the desktop's version table (which only labels its ROM picker) but the set Torch
    // has a recipe for, since that is what decides whether the next step can run.
    if (RomInfo::TorchVersionDir(RomInfo::HeaderCrc(rom.data(), rom.size())) == nullptr) {
        return Status::UnknownVersion;
    }
    if (!RomInfo::MatchesKnownDump(rom.data(), rom.size())) {
        return Status::BadCrc;
    }
    return Status::Ok;
}

// Runs Torch over the version's recipe, reporting each asset file as it is parsed, and
// leaves the archive at outDir/<archive>. Torch names the archive from config.yml; the name
// it chose is checked against the one this module reported up front.
Status ExtractArchive(std::vector<uint8_t> rom, const char* versionDir, const fs::path& outDir,
                      const std::string& expectedArchive, std::string& detail) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    // Torch skips files whose hashes match a previous run's; start from none.
    fs::remove(outDir / "torch.hash.yml", ec);

    const size_t total = SohTorch::CountAssetFiles(std::string(kRecipeDir) + "/" + versionDir);
    size_t done = 0;
    ReportProgress(0, total);

    const std::string archive = SohTorch::ExtractWithCallbacks(
        std::move(rom), kRecipeDir, outDir.string(), SOH_EXTRACT_PORT_VERSION,
        [&done, total]() { ReportProgress(++done, total); }, [&detail](const std::string& what) { detail = what; });

    fs::remove(outDir / "torch.hash.yml", ec);
    if (archive.empty()) {
        return detail.empty() ? Status::NoArchive : Status::ExtractFailed;
    }
    if (archive != expectedArchive) {
        detail = "Torch wrote " + archive + ", expected " + expectedArchive + ".";
        return Status::NoArchive;
    }
    // Torch reports every recipe file, so this only fires if a file was skipped.
    if (done != total) {
        ReportProgress(total, total);
    }
    return Status::Ok;
}

std::string Quote(const std::string& s) {
    static const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c < 0x20 || c == 0x7f) {
            // JSON forbids raw control characters; log text can carry escape codes.
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 0xf];
        } else {
            out += static_cast<char>(c);
        }
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
// game does, runs Torch over the embedded recipe, and leaves `<outDir>/oot.o2r` or
// `<outDir>/oot-mq.o2r` behind. Returns 0 on success or a negative Status; the reason, the
// detected version and the archive name are in Extract_ResultJson() either way. Progress goes
// to Module._sohExtractProgress(done, total), once per recipe file as Torch finishes parsing it.
// Nonzero `quiet` keeps log lines below warn from being written at all. The ROM file is removed
// once read, so the run does not hold a second copy of it.
//
// One conversion per module instance, as with the ZAPD converter this replaced.
int Extract_RomToO2r(const char* romPath, const char* outDir, int quiet) {
    InstallLogger(quiet != 0);
    Result result;

    std::vector<uint8_t> rom = ReadFile(romPath);
    std::error_code ec;
    fs::remove(romPath, ec);
    if (rom.empty()) {
        result.status = Status::CannotRead;
        return Finish(result);
    }
    result.status = CheckRom(rom);
    if (result.status != Status::Ok) {
        return Finish(result);
    }

    const uint32_t headerCrc = RomInfo::HeaderCrc(rom.data(), rom.size());
    result.version = RomInfo::VersionName(headerCrc);
    result.archive = RomInfo::ArchiveName(headerCrc);
    result.status = ExtractArchive(std::move(rom), RomInfo::TorchVersionDir(headerCrc), fs::absolute(outDir),
                                   result.archive, result.detail);
    return Finish(result);
}

// {"code":0,"error":"","version":"NTSC N64 1.0","archive":"oot.o2r"} for the last call.
const char* Extract_ResultJson() {
    return gLastResultJson.c_str();
}

} // extern "C"
