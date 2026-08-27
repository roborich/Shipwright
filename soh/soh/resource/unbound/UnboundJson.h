#pragma once
// SOH [Unbound] Layer-merged JSON documents and the shared conventions of the Unbound schema.
// Merge rules: unbound-docs/scene-format.md §3.
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Unbound {

using Json = nlohmann::json;

// Deep-merges `overlay` into `base`: objects key-wise (later wins), null deletes, arrays replace,
// "$replace": true discards `base` for that subtree.
void MergeJson(Json& base, const Json& overlay);

// Loads `path` from every mounted archive (lowest first) and merges them. Returns a null Json when
// no archive has the path or nothing parses.
Json LoadMergedJson(const std::string& path);

// Keys of a keyed/positional list in engine order: "$order" first (those that exist), then the
// remaining keys with integer keys ascending numerically before non-integer keys lexically.
// "$order" and "$replace" are never returned.
std::vector<std::string> ListKeys(const Json& obj);

// Integer from a JSON number or a hex/decimal string ("0x0F12", "3858").
int64_t ToInt(const Json& value, int64_t fallback = 0);

// Raw bytes of `path` from the topmost archive that has it (bulk resources: last-wins).
std::vector<char> LoadBulk(const std::string& path);

} // namespace Unbound
