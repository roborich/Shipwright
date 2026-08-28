// SOH [Unbound] See UnboundJson.h.
#include "UnboundJson.h"
#include "UnboundSchema.h"

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>

namespace SOH::Unbound {

static bool IsReplaceDirective(const Json& obj) {
    auto it = obj.find(Schema::kReplace);
    return it != obj.end() && it->is_boolean() && it->get<bool>();
}

void MergeJson(Json& base, const Json& overlay) {
    if (!base.is_object() || !overlay.is_object() || IsReplaceDirective(overlay)) {
        base = overlay;
        return;
    }
    for (const auto& [key, value] : overlay.items()) {
        if (value.is_null()) {
            base.erase(key);
        } else if (value.is_object() && base.contains(key) && base[key].is_object()) {
            MergeJson(base[key], value);
        } else {
            base[key] = value;
        }
    }
}

// "$replace" only steers the merge; the merged document must not carry it (§3.4).
static void StripReplaceDirectives(Json& doc) {
    if (!doc.is_object()) {
        return;
    }
    doc.erase(Schema::kReplace);
    for (auto& [key, value] : doc.items()) {
        StripReplaceDirectives(value);
    }
}

Json LoadMergedJson(const std::string& path) {
    auto layers = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->LoadFileFromAllLayers(path);
    Json merged;
    if (layers.size() > 1) {
        SPDLOG_DEBUG("[Unbound] {}: merging {} archive layers", path, layers.size());
    }
    for (const auto& file : layers) {
        Json doc;
        try {
            doc = Json::parse(file->Buffer->begin(), file->Buffer->end(), nullptr, true, true);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[Unbound] {}: invalid JSON in one layer, skipped: {}", path, e.what());
            continue;
        }
        if (merged.is_null()) {
            merged = std::move(doc);
        } else {
            MergeJson(merged, doc);
        }
    }
    StripReplaceDirectives(merged);
    return merged;
}

static bool IsIntegerKey(const std::string& key, long long& value) {
    if (key.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    value = std::strtoll(key.c_str(), &end, 10);
    return errno == 0 && end != nullptr && *end == '\0';
}

static std::vector<std::string> OrderedKeys(const Json& obj) {
    std::vector<std::string> ordered;
    auto order = obj.find(Schema::kOrder);
    if (order == obj.end() || !order->is_array()) {
        return ordered;
    }
    for (const auto& k : *order) {
        if (k.is_string() && obj.contains(k.get<std::string>())) {
            ordered.push_back(k.get<std::string>());
        }
    }
    return ordered;
}

std::vector<std::string> ListKeys(const Json& obj) {
    if (!obj.is_object()) {
        return {};
    }
    std::vector<std::string> keys = OrderedKeys(obj);
    std::vector<std::pair<bool, std::pair<long long, std::string>>> rest; // (isNotInt, (int, key))
    for (const auto& [key, value] : obj.items()) {
        if (key == Schema::kOrder || key == Schema::kReplace) {
            continue;
        }
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
            continue;
        }
        long long n = 0;
        bool isInt = IsIntegerKey(key, n);
        rest.push_back({ !isInt, { isInt ? n : 0, key } });
    }
    std::sort(rest.begin(), rest.end());
    for (const auto& r : rest) {
        keys.push_back(r.second.second);
    }
    return keys;
}

std::vector<std::string> PositionalKeys(const Json& list, const std::string& what) {
    std::vector<std::string> keys = ListKeys(list);
    for (size_t i = 0; i < keys.size(); i++) {
        if (keys[i] != std::to_string(i)) {
            SPDLOG_ERROR("[Unbound] {}: positional list has a hole at index {} (found key '{}'); truncating", what, i,
                         keys[i]);
            keys.resize(i);
            break;
        }
    }
    return keys;
}

int64_t ToInt(const Json& value, int64_t fallback) {
    if (value.is_number_integer()) {
        return value.get<int64_t>();
    }
    if (value.is_number_float()) {
        return (int64_t)value.get<double>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? 1 : 0;
    }
    if (value.is_string()) {
        try {
            return std::stoll(value.get<std::string>(), nullptr, 0);
        } catch (...) { return fallback; }
    }
    return fallback;
}

double ToNumber(const Json& value, double fallback) {
    if (value.is_number()) {
        return value.get<double>();
    }
    if (value.is_string()) {
        try {
            return std::stod(value.get<std::string>()); // accepts "0x10" as well as "1.5"
        } catch (...) { return fallback; }
    }
    return fallback;
}

int64_t Field(const Json& obj, const char* key, int64_t fallback) {
    auto it = obj.find(key);
    return it == obj.end() ? fallback : ToInt(*it, fallback);
}

double NumberField(const Json& obj, const char* key, double fallback) {
    auto it = obj.find(key);
    return it == obj.end() ? fallback : ToNumber(*it, fallback);
}

std::string PathField(const Json& obj, const char* key) {
    auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : "";
}

Vec3s ReadVec3s(const Json& v) {
    Vec3s out{ 0, 0, 0 };
    if (v.is_array() && v.size() >= 3) {
        out.x = (s16)ToInt(v[0]);
        out.y = (s16)ToInt(v[1]);
        out.z = (s16)ToInt(v[2]);
    }
    return out;
}

Vec3f ReadVec3f(const Json& v) {
    Vec3f out{ 0.0f, 0.0f, 0.0f };
    if (v.is_array() && v.size() >= 3) {
        out.x = (f32)ToNumber(v[0]);
        out.y = (f32)ToNumber(v[1]);
        out.z = (f32)ToNumber(v[2]);
    }
    return out;
}

std::vector<char> LoadBulk(const std::string& path) {
    auto file = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->LoadFile(path);
    if (file == nullptr || file->Buffer == nullptr) {
        return {};
    }
    return *file->Buffer;
}

} // namespace SOH::Unbound
