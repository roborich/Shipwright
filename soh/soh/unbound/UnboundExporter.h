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

// The release that converted a base, as its manifest records it (`source.converter`). Informational only: a
// base is usable when its `baseVersion` matches (SPEC.md §6), whichever release wrote it.
std::string ConverterName();

// A game archive's `portVersion` file: the SoH release that extracted it. A base inherits the file from the
// archive it was converted from.
struct PortVersion {
    int major = -1; // -1: no portVersion file
    int minor = -1;
    int patch = -1;
    bool operator==(const PortVersion& other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }
};

// The topmost mounted `portVersion` file.
PortVersion MountedPortVersion();

// The converted base archive, kept beside oot.o2r.
inline constexpr const char* kBaseArchiveName = "oot-unbound.o2r";

enum class BaseArchiveState {
    None,      // no base archive is mounted; vanilla scenes stay in vanilla format
    Mounted,   // an existing, current base archive is mounted
    Converted, // the base archive was written this launch, then mounted
};

// Mounts <gameArchiveDir>/oot-unbound.o2r above the vanilla archives, converting it first when it is missing,
// was converted from other ROM archives or from another extraction of them (its copied game files would
// shadow the newer ones), or is incompatible with this build (SPEC.md §6, §10.1). Call after the resource
// factories are registered and before mods are mounted.
BaseArchiveState EnsureBaseArchive(const std::string& gameArchiveDir);

// oot-unbound.o2r is a complete game archive (the converter copies every file it does not transform, the
// version files included), so it can be installed without the ROM archives it came from: the browser build
// is installed that way. Such a base cannot be converted again, only checked for compatibility with this
// build (SPEC.md §10.1), whichever release converted it. Call once it is mounted as the game archive and the
// resource factories are registered. Returns why it cannot be used, or an empty string.
std::string CheckStandaloneBaseArchive();

} // namespace SOH::Unbound

// Console / CLI entry: returns 0 on success, logs progress through spdlog.
extern "C" int Unbound_Export(const char* outPath);
