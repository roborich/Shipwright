#pragma once
// SOH [Unbound] Converts the mounted vanilla archive into the Unbound layout.
// Output format: unbound-docs/SPEC.md; how/why: unbound-docs/scene-format.md §3.
#include <string>

namespace SOH::Unbound {

struct ExportReport {
    bool ok = false;
    size_t failures = 0; // resources that failed to convert (the archive is still written)
    size_t scenes = 0;
    size_t rooms = 0;
    size_t copied = 0;
    size_t messages = 0;
    std::string error;
};

// Writes a complete oot-unbound.o2r (stored zip) to outPath from the currently mounted base archive.
ExportReport ExportArchive(const std::string& outPath);

// The converter's name and revision as a base's manifest records it (`source.converter`); a base written by
// another is stale.
std::string ConverterName();

// The converted base archive, kept beside oot.o2r.
inline constexpr const char* kBaseArchiveName = "oot-unbound.o2r";

enum class BaseArchiveState {
    None,      // no base archive is mounted; vanilla scenes stay in vanilla format
    Mounted,   // an existing, current base archive is mounted
    Converted, // the base archive was written this launch, then mounted
};

// Mounts <gameArchiveDir>/oot-unbound.o2r above the vanilla archives, converting it first when it is missing
// or was made from other ROM archives or by another SoH build. Call after the resource factories are
// registered and before mods are mounted.
BaseArchiveState EnsureBaseArchive(const std::string& gameArchiveDir);

// oot-unbound.o2r is a complete game archive (the converter copies every file it does not transform, the
// version files included), so it can be installed without the ROM archives it came from: the browser build
// is installed that way. Such a base cannot be converted again, only checked. Call once it is mounted as the
// game archive and the resource factories are registered. Returns why it cannot be used, or an empty string.
std::string CheckStandaloneBaseArchive();

} // namespace SOH::Unbound

// Console / CLI entry: returns 0 on success, logs progress through spdlog.
extern "C" int Unbound_Export(const char* outPath);
