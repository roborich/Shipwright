// SOH [Unbound] See UnboundJson.h.
#include "UnboundJson.h"

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>

namespace Unbound {

void MergeJson(Json& base, const Json& overlay) {
    if (!base.is_object() || !overlay.is_object()) {
        base = overlay;
        return;
    }
    auto replace = overlay.find("$replace");
    if (replace != overlay.end() && replace->is_boolean() && replace->get<bool>()) {
        base = overlay;
        base.erase("$replace");
        return;
    }
    for (const auto& [key, value] : overlay.items()) {
        if (key == "$replace") {
            continue;
        }
        if (value.is_null()) {
            base.erase(key);
        } else if (value.is_object() && base.contains(key) && base[key].is_object()) {
            MergeJson(base[key], value);
        } else {
            base[key] = value;
        }
    }
}

Json LoadMergedJson(const std::string& path) {
    auto layers = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->LoadFileFromAllLayers(path);
    Json merged;
    if (layers.size() > 1) {
        SPDLOG_INFO("[Unbound] {}: merging {} archive layers", path, layers.size());
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

std::vector<std::string> ListKeys(const Json& obj) {
    std::vector<std::string> keys;
    if (!obj.is_object()) {
        return keys;
    }
    std::vector<std::string> ordered;
    auto order = obj.find("$order");
    if (order != obj.end() && order->is_array()) {
        for (const auto& k : *order) {
            if (k.is_string() && obj.contains(k.get<std::string>())) {
                ordered.push_back(k.get<std::string>());
            }
        }
    }
    std::vector<std::pair<bool, std::pair<long long, std::string>>> rest; // (isInt, (int, key))
    for (const auto& [key, value] : obj.items()) {
        if (key == "$order" || key == "$replace") {
            continue;
        }
        if (std::find(ordered.begin(), ordered.end(), key) != ordered.end()) {
            continue;
        }
        long long n = 0;
        bool isInt = IsIntegerKey(key, n);
        rest.push_back({ !isInt, { isInt ? n : 0, key } });
    }
    std::sort(rest.begin(), rest.end());
    keys = ordered;
    for (const auto& r : rest) {
        keys.push_back(r.second.second);
    }
    return keys;
}

double ToNumber(const Json& value, double fallback) {
    if (value.is_number()) {
        return value.get<double>();
    }
    if (value.is_string()) {
        try {
            return std::stod(value.get<std::string>());
        } catch (...) {
            return (double)ToInt(value, (int64_t)fallback);
        }
    }
    return fallback;
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
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

std::vector<char> LoadBulk(const std::string& path) {
    auto file = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->LoadFile(path);
    if (file == nullptr || file->Buffer == nullptr) {
        return {};
    }
    return *file->Buffer;
}

} // namespace Unbound
