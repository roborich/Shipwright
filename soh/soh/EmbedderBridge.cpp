#ifdef __EMSCRIPTEN__
#include "EmbedderBridge.h"

#include <emscripten.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include <z64.h>
#include <variables.h>
extern PlayState* gPlayState;
int AudioPlayer_Buffered(void);
}

// ---- outbound ----------------------------------------------------------------------------

// Hands one event to the page. JSON is parsed on the JS side so the detail object is a
// plain object the listener can destructure, whoever the listener is.
static void EmitEvent(const std::string& json) {
    EM_ASM({ window.dispatchEvent(new CustomEvent('soh', { detail : JSON.parse(UTF8ToString($0)) })); }, json.c_str());
}

static void EmitLoadGame(int32_t fileNum) {
    EmitEvent(fmt::format(R"({{"type":"load-game","fileNum":{}}})", fileNum));
}

static void EmitScene(int16_t sceneNum) {
    EmitEvent(
        fmt::format(R"({{"type":"scene","sceneNum":{},"entranceIndex":{}}})", sceneNum, gSaveContext.entranceIndex));
}

// The file's bytes travel as a Uint8Array copy, so the host owns them outright and the
// event carries the same thing whether the file is JSON (config, saves) or not.
static void EmitFileSaved(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string absolute = std::filesystem::absolute(path).lexically_normal().generic_string();
    EM_ASM(
        {
            window.dispatchEvent(new CustomEvent(
                'soh',
                { detail : { type : 'file-saved', path : UTF8ToString($0), bytes : HEAPU8.slice($1, $1 + $2) } }));
        },
        absolute.c_str(), bytes.data(), bytes.size());
}

// ---- saved files -------------------------------------------------------------------------
// The game writes its config and saves into the in-memory filesystem through several
// unrelated paths (SaveManager's temp-and-rename, its metadata rewrite, SaveGlobal, LUS's
// Config::Save). Rather than hook each one, watch the files the host handed over -- the
// config and everything under Save/ -- and report whichever changed since the last frame.

struct FileStamp {
    std::filesystem::file_time_type mtime;
    uintmax_t size;
    bool operator!=(const FileStamp& o) const {
        return mtime != o.mtime || size != o.size;
    }
};

static std::map<std::string, FileStamp> sSeenFiles;
static bool sSeenFilesPrimed = false;

static std::vector<std::filesystem::path> WatchedFiles() {
    std::vector<std::filesystem::path> files;
    files.push_back(Ship::Context::GetPathRelativeToAppDirectory("shipofharkinian.json"));
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(Ship::Context::GetPathRelativeToAppDirectory("Save"), ec)) {
        // SaveManager writes file<N>.temp and renames it over file<N>.sav; only the result counts.
        if (entry.is_regular_file(ec) && entry.path().extension() != ".temp") {
            files.push_back(entry.path());
        }
    }
    return files;
}

// Reports every watched file whose timestamp or size changed since the last call. The
// first call only records what the host supplied, so nothing is reported that the game
// did not itself write.
static void ReportSavedFiles() {
    for (const auto& path : WatchedFiles()) {
        std::error_code ec;
        FileStamp now{ std::filesystem::last_write_time(path, ec), std::filesystem::file_size(path, ec) };
        if (ec) {
            continue;
        }
        const std::string key = path.generic_string();
        auto seen = sSeenFiles.find(key);
        bool changed = seen == sSeenFiles.end() || seen->second != now;
        sSeenFiles[key] = now;
        if (changed && sSeenFilesPrimed) {
            EmitFileSaved(path);
        }
    }
    sSeenFilesPrimed = true;
}

// ---- diagnostics -------------------------------------------------------------------------
// Counters for soh/wasm/tests and for profiling. Not part of the host contract: the fields
// can change without notice.

static uint64_t sTicks = 0;
static uint64_t sDraws = 0;
static uint64_t sAudioDrops = 0;

void Soh_EmbedderCountDraws(size_t draws) {
    sDraws += draws;
}

void Soh_EmbedderCountAudioDrop(void) {
    sAudioDrops++;
}

// Returns a JSON object, valid until the next call. updateRate is R_UPDATE_RATE, the game's
// vsync divisor; sceneNum is -1 outside a play state.
extern "C" EMSCRIPTEN_KEEPALIVE const char* Soh_GetStats(void) {
    static std::string json;
    json = fmt::format(R"({{"ticks":{},"draws":{},"updateRate":{},"audioBuffered":{},"audioDrops":{},"sceneNum":{}}})",
                       sTicks, sDraws, R_UPDATE_RATE, AudioPlayer_Buffered(), sAudioDrops,
                       gPlayState != nullptr ? gPlayState->sceneNum : -1);
    return json.c_str();
}

// ---- entry points ------------------------------------------------------------------------

void Soh_InitEmbedderBridge(void) {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>(EmitLoadGame);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(EmitScene);
    ReportSavedFiles(); // prime with the host's files
}

void Soh_EmbedderAfterFrame(void) {
    sTicks++;
    ReportSavedFiles();
}

void Soh_EmbedderQuit(void) {
    ReportSavedFiles(); // anything written on the way out goes first
    EmitEvent(R"({"type":"quit"})");
}

void Soh_EmbedderError(const char* message) {
    EmitEvent(fmt::format(R"({{"type":"error","message":{}}})", nlohmann::json(message).dump()));
}

// Inbound. Called from JS between frames (the build is single-threaded, so a ccall can
// only land while no frame is running); handlers that queue work for the next frame, such
// as `entrance`, behave exactly as they do when typed into the console.
//
// Returns the handler's own result (0 is success by convention), -1 before the console
// exists, -2 for a command the console does not know. The last case is separate because
// Console::Run reports "unknown command" as 0, the same value as success.
extern "C" EMSCRIPTEN_KEEPALIVE int32_t Soh_RunConsoleCommand(const char* command) {
    auto context = Ship::Context::GetInstance();
    if (context == nullptr || context->GetConsole() == nullptr) {
        SPDLOG_WARN("Soh_RunConsoleCommand(\"{}\") before the console exists; ignored", command);
        return -1;
    }
    auto console = context->GetConsole();
    std::string line = command;
    std::string name = line.substr(0, line.find(' '));
    if (!console->HasCommand(name)) {
        SPDLOG_WARN("Soh_RunConsoleCommand: unknown command \"{}\"", name);
        return -2;
    }
    std::string output;
    return console->Run(line, &output);
}
#endif // __EMSCRIPTEN__
