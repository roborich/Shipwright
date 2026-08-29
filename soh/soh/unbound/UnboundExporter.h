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

// The converted base archive, kept beside oot.o2r.
inline constexpr const char* kBaseArchiveName = "oot-unbound.o2r";

// Mounts <gameArchiveDir>/oot-unbound.o2r above the vanilla archives, converting it first when it is missing
// or was made from other ROM archives or by another SoH build. Call after the resource factories are
// registered and before mods are mounted. Returns true when a base archive is mounted.
bool EnsureBaseArchive(const std::string& gameArchiveDir);

} // namespace SOH::Unbound

// Console / CLI entry: returns 0 on success, logs progress through spdlog.
extern "C" int Unbound_Export(const char* outPath);
