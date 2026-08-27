#pragma once
// SOH [Unbound] Converts the mounted vanilla archive into the Unbound layout.
// See unbound-docs/scene-format.md §5.
#include <string>

namespace Unbound {

struct ExportReport {
    bool ok = false;
    size_t scenes = 0;
    size_t rooms = 0;
    size_t copied = 0;
    size_t messages = 0;
    std::string error;
};

// Writes a complete oot-unbound.o2r (stored zip) to outPath from the currently mounted base archive.
ExportReport ExportArchive(const std::string& outPath);

} // namespace Unbound

#ifdef __cplusplus
extern "C" {
#endif
// Console / CLI entry: returns 0 on success, logs progress through spdlog.
int Unbound_Export(const char* outPath);
#ifdef __cplusplus
}
#endif
