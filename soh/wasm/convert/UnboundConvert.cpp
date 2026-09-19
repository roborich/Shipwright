// SOH [Unbound] [WASM] oot.o2r -> oot-unbound.o2r as its own module (soh-unbound-convert), for a host that
// wants the Unbound base before the game ever runs. The module is linked from the game's own objects, so the
// conversion is SOH::Unbound::ExportArchive itself -- the same code, and the same converter string, as the
// conversion the game runs at startup -- but nothing of the game starts: no window, audio or frame loop, only
// libultraship's resource manager with the game's resource factories. Host contract: soh/wasm/HOST-API.md §7.
//
// Orchestration: Unbound_ConvertArchive
//   -> StartResourceManager (headless libultraship context, source archive mounted, factories registered)
//   -> CheckSourceArchive   (an OoT game archive from this port version)
//   -> ExportArchive        (writes the base)
// The result is read back as JSON through Unbound_ConvertResultJson.
#include <emscripten.h>
#include <libultraship/libultraship.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include "soh/OTRGlobals.h"
#include "soh/unbound/UnboundExporter.h"
#include "soh/unbound/UnboundSchema.h"

extern "C" {
#include "variables.h" // gBuildVersionMajor
}

namespace {

// Codes returned to api.js (and on to the host as Error.code).
enum ConvertCode {
    kConvertOk = 0,
    kConvertAlreadyRan = -1,   // one conversion per instance: the resource manager keeps what it loaded
    kConvertNoArchive = -2,    // the source is not a readable game archive
    kConvertWrongVersion = -3, // made by another SoH port version
    kConvertFailed = -4,       // the converter reported failures, or threw
};

struct ConvertResult {
    int code = kConvertOk;
    std::string error;
    SOH::Unbound::ExportReport report;
};

ConvertResult sResult;
bool sRan = false;
// Ship::Context keeps only a weak reference to itself; the game's is owned by OTRGlobals, this one by us.
std::shared_ptr<Ship::Context> sContext;

void StartResourceManager(const std::string& sourcePath) {
    sContext = Ship::Context::CreateUninitializedInstance("Ship of Harkinian", "soh", "shipofharkinian.json");
    sContext->InitConfiguration(); // the factories read CVars (resource logging)
    sContext->InitConsoleVariables();
    sContext->InitResourceManager({ sourcePath }, {}, 0, true);
    SOH_RegisterResourceFactories(sContext->GetResourceManager()->GetResourceLoader());
}

// portVersion is [endianness u8][major u16][minor u16][patch u16]; the game refuses an archive whose major
// differs from its own, and a base inherits the file from its source.
int ReadPortVersionMajor(std::shared_ptr<Ship::ArchiveManager> archives) {
    auto file = archives->LoadFile("portVersion");
    if (file == nullptr || file->Buffer == nullptr || file->Buffer->size() < 3) {
        return -1;
    }
    auto stream = std::make_shared<Ship::MemoryStream>(file->Buffer->data(), file->Buffer->size());
    auto reader = std::make_shared<Ship::BinaryReader>(stream);
    reader->SetEndianness((Ship::Endianness)reader->ReadUByte());
    return reader->ReadUInt16();
}

// Why the mounted source cannot be converted, as a code and a message; kConvertOk when it can.
ConvertResult CheckSourceArchive() {
    ConvertResult result;
    auto archives = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    if (!archives->IsLoaded() || archives->GetGameVersions().empty()) {
        result.code = kConvertNoArchive;
        result.error = "not an OoT game archive: supply the oot.o2r (or oot-mq.o2r) that soh-extract produced";
        return result;
    }
    if (archives->HasFile(SOH::Unbound::Schema::kManifestPath)) {
        result.code = kConvertNoArchive;
        result.error = "this is already an Unbound base (oot-unbound.o2r); convert the oot.o2r it came from";
        return result;
    }
    int major = ReadPortVersionMajor(archives);
    if (major != gBuildVersionMajor) {
        result.code = kConvertWrongVersion;
        result.error = "the archive was made by an incompatible version of SoH (port version " + std::to_string(major) +
                       ", this converter is " + std::to_string(gBuildVersionMajor) + "); extract the ROM again";
    }
    return result;
}

ConvertResult Export(const std::string& outPath) {
    ConvertResult result;
    try {
        result.report = SOH::Unbound::ExportArchive(outPath);
    } catch (const std::exception& e) { result.report.error = std::string("the converter threw: ") + e.what(); }
    if (!result.report.ok) {
        result.code = kConvertFailed;
        result.error = result.report.error;
    }
    return result;
}

} // namespace

// Converts the game archive at sourcePath into an Unbound base at outPath. Returns a ConvertCode.
extern "C" EMSCRIPTEN_KEEPALIVE int Unbound_ConvertArchive(const char* sourcePath, const char* outPath) {
    if (sRan) {
        sResult = {};
        sResult.code = kConvertAlreadyRan;
        sResult.error = "this converter instance has already run; create a new one per archive";
        return sResult.code;
    }
    sRan = true;
    StartResourceManager(sourcePath);
    sResult = CheckSourceArchive();
    if (sResult.code == kConvertOk) {
        sResult = Export(outPath);
    }
    if (sResult.code != kConvertOk) {
        SPDLOG_ERROR("[Unbound convert] {}", sResult.error);
    }
    return sResult.code;
}

// The last conversion as JSON, valid until the next call:
// { code, error, converter, scenes, rooms, messages, copied, failures }.
extern "C" EMSCRIPTEN_KEEPALIVE const char* Unbound_ConvertResultJson(void) {
    static std::string json;
    const auto& r = sResult.report;
    json = nlohmann::json{
        { "code", sResult.code }, { "error", sResult.error }, { "converter", SOH::Unbound::ConverterName() },
        { "scenes", r.scenes },   { "rooms", r.rooms },       { "messages", r.messages },
        { "copied", r.copied },   { "failures", r.failures }
    }.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    return json.c_str();
}
