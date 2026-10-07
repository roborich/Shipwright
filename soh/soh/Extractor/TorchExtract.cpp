#include "TorchExtract.h"

#include <exception>
#include <filesystem>
#include <memory>
#include <utility>

#include "spdlog/spdlog.h"

#include "Companion.h"
#include "factories/BaseFactory.h"

namespace fs = std::filesystem;

namespace SohTorch {

size_t CountAssetFiles(const std::string& ymlDir) {
    std::error_code ec;
    if (!fs::is_directory(ymlDir, ec)) {
        return 0;
    }

    size_t count = 0;
    for (fs::recursive_directory_iterator it(ymlDir, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (it->is_regular_file(ec) && it->path().extension() == ".yml") {
            count++;
        }
    }
    return count;
}

std::string Extract(std::vector<uint8_t> rom, const std::string& srcDir, const std::string& destDir,
                    const std::string& portVersion, std::atomic<size_t>* progress) {
    return ExtractWithCallbacks(std::move(rom), srcDir, destDir, portVersion, [progress]() {
        if (progress != nullptr) {
            (*progress)++;
        }
    });
}

std::string ExtractWithCallbacks(std::vector<uint8_t> rom, const std::string& srcDir, const std::string& destDir,
                                 const std::string& portVersion, const std::function<void()>& onFile,
                                 const std::function<void(const std::string&)>& onError) {
    std::string archiveName;

    try {
        // Companion::Instance is a raw global with no getter; factories dereference it.
        auto companion = std::make_unique<Companion>(std::move(rom), ArchiveType::O2R, false, srcDir, destDir);
        Companion::Instance = companion.get();
        companion->SetVersion(portVersion);
        companion->SetPhaseCallback([&onFile](int) {
            if (onFile) {
                onFile();
            }
        });

        // Init is the whole run; it calls Process() internally.
        companion->Init(ExportType::Binary);
#ifdef __EMSCRIPTEN__
        // SOH [WASM] ...except in an Emscripten build, where Torch leaves Process() for its own
        // web API to call.
        std::atomic<size_t> assetCount{ 0 };
        companion->Process(assetCount);
#endif

        // config.yml names the archive per rom; ask rather than guess, and ask before the
        // companion goes away.
        archiveName = fs::path(companion->GetOutputPath()).filename().string();

        // Companion holds every parsed asset, so don't leak it into the game's lifetime.
        companion.reset();
        Companion::Instance = nullptr;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Torch extraction failed: {}", e.what());
        Companion::Instance = nullptr;
        if (onError) {
            onError(e.what());
        }
        return "";
    } catch (...) {
        SPDLOG_ERROR("Torch extraction failed with an unknown exception");
        Companion::Instance = nullptr;
        if (onError) {
            onError("unknown exception");
        }
        return "";
    }

    // Process() returns void and several fatal paths only log and return, so confirm the
    // archive is really there rather than trusting the run.
    std::error_code ec;
    if (archiveName.empty() || !fs::exists(fs::path(destDir) / archiveName, ec)) {
        SPDLOG_ERROR("Torch produced no archive in {}", destDir);
        return "";
    }

    return archiveName;
}

} // namespace SohTorch
