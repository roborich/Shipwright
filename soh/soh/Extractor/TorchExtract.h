#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// No torch types here; TorchExtract.cpp is the only TU that includes Companion.h.
namespace SohTorch {

// Count of .yml files under a version directory. Torch's phase callback fires once per file,
// so this is the progress denominator.
size_t CountAssetFiles(const std::string& ymlDir);

// Extracts rom into destDir. rom must already be big-endian (.z64 order), since config.yml
// only lists hashes of big-endian dumps. Torch picks both the version directory under srcDir and the
// archive name (oot.o2r, oot-mq.o2r) from config.yml by hashing the ROM, so the name it chose
// is returned rather than assumed. Empty if extraction threw or produced no archive.
// Increments progress once per asset file.
std::string Extract(std::vector<uint8_t> rom, const std::string& srcDir, const std::string& destDir,
                    const std::string& portVersion, std::atomic<size_t>* progress);

// SOH [WASM] The same, reporting each asset file as it finishes parsing to a callback rather
// than a counter, for a caller with no other thread to poll a counter from. onError, if set,
// receives the reason when extraction throws.
std::string ExtractWithCallbacks(std::vector<uint8_t> rom, const std::string& srcDir, const std::string& destDir,
                                 const std::string& portVersion, const std::function<void()>& onFile,
                                 const std::function<void(const std::string&)>& onError = nullptr);

} // namespace SohTorch
