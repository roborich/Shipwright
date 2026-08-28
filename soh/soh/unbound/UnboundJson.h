#pragma once
// SOH [Unbound] Layer-merged JSON documents and the shared readers of the Unbound schema.
// Merge rules: unbound-docs/scene-format.md §3. Key names: UnboundSchema.h.
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include <libultraship/libultra.h>
#include "z64math.h"

namespace SOH::Unbound {

using Json = nlohmann::json;

// Deep-merges `overlay` into `base`: objects key-wise (later wins), null deletes, arrays replace,
// "$replace": true discards `base` for that subtree.
void MergeJson(Json& base, const Json& overlay);

// Loads `path` from every mounted archive (lowest first), merges them and strips the merge
// directives. Returns a null Json when no archive has the path or nothing parses.
Json LoadMergedJson(const std::string& path);

// Keys of a keyed/positional list in engine order: "$order" first (those that exist), then the
// remaining keys with integer keys ascending numerically before non-integer keys lexically.
// "$order" and "$replace" are never returned.
std::vector<std::string> ListKeys(const Json& obj);

// Keys "0", "1", ... of a positional list. The engine addresses these by index, so the list stops
// at the first gap, with an error naming `what`.
std::vector<std::string> PositionalKeys(const Json& list, const std::string& what);

// Integer from a JSON number or a hex/decimal string ("0x0F12", "3858").
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
