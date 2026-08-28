#pragma once
// SOH [Unbound] Layer-merged JSON documents and the shared readers of the Unbound schema.
// Merge rules: unbound-docs/SPEC.md §3. Key names: UnboundSchema.h.
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include <libultraship/libultra.h>
#include "z64math.h"

namespace SOH::Unbound {

using Json = nlohmann::json;

// A document that violates the format (SPEC.md §3): factories catch it and fail the resource.
struct DocumentError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Deep-merges `overlay` into `base`: objects key-wise (later wins), null deletes, arrays replace,
// "$replace": true discards `base` for that subtree.
void MergeJson(Json& base, const Json& overlay);

// Loads `path` from every mounted archive (lowest first), merges them and strips the merge
// directives and every null (a null is a deletion, also in a single layer, §3.2). Returns a null
// Json when no archive has the path or nothing parses.
Json LoadMergedJson(const std::string& path);

// Keys of a keyed/positional list in engine order: "$order" first (those that exist, each once),
// then the remaining keys with integer keys ascending numerically before non-integer keys
// lexically. Keys beginning with "$" are reserved (§2) and never returned.
std::vector<std::string> ListKeys(const Json& obj);

// Keys "0", "1", ... of a positional list. The engine addresses these by index, so a gap is a
// DocumentError naming `what` (§3.2); so is "$order" on the list (§3.5).
std::vector<std::string> PositionalKeys(const Json& list, const std::string& what);

// The nested object / array under `key`, or a static empty one when absent or of another type, so
// callers never index a non-object (nlohmann's value() throws there).
const Json& Sub(const Json& obj, const char* key);
const Json& SubArray(const Json& obj, const char* key);

// The top-level "$schema" string, or "" when absent or not a string.
std::string SchemaOf(const Json& doc);

// Splits "<type>/<n>" into its type half and decimal version; false when the suffix is not a
// plain decimal integer. A missing "$schema" ("") yields `type` empty and version 1 (§10).
bool ParseSchema(const std::string& schema, std::string& type, int& version);

// Integer from a JSON number or a hex/decimal string ("0x0F12", "3858").
// SPEC.md §2 string forms: decimal or 0x hex (ParseIntString), plus fraction/exponent (ParseNumberString);
// the whole string must parse.
bool ParseIntString(const std::string& text, int64_t& out);
bool ParseNumberString(const std::string& text, double& out);
int64_t ToInt(const Json& value, int64_t fallback = 0);
// Number from a JSON number or string; positions may be fractional (world extent).
double ToNumber(const Json& value, double fallback = 0.0);

// Field readers: the fallback when `key` is absent or unreadable.
int64_t Field(const Json& obj, const char* key, int64_t fallback = 0);
double NumberField(const Json& obj, const char* key, double fallback = 0.0);
std::string PathField(const Json& obj, const char* key);

// [x, y, z] arrays; rotations and camera positions stay s16, world positions are f32.
Vec3s ReadVec3s(const Json& v);
Vec3f ReadVec3f(const Json& v);

// [r, g, b] (or any 3-component u8/s8 triple) into `out[3]`; leaves `out` alone when absent.
template <typename T> void ReadRgb(const Json& v, T* out) {
    if (v.is_array() && v.size() >= 3) {
        out[0] = (T)ToInt(v[0]);
        out[1] = (T)ToInt(v[1]);
        out[2] = (T)ToInt(v[2]);
    }
}

// Raw bytes of `path` from the topmost archive that has it (bulk resources: last-wins).
std::vector<char> LoadBulk(const std::string& path);

} // namespace SOH::Unbound
