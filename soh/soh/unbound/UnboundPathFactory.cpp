// SOH [Unbound] paths/<name>.json -> SOH::Path. See unbound-docs/scene-format.md §2.4.
#include "UnboundFactories.h"
#include "UnboundJson.h"
#include "UnboundSchema.h"

#include <spdlog/spdlog.h>

#include "soh/resource/type/Path.h"

using SOH::Unbound::Json;
namespace K = SOH::Unbound::Schema;

namespace SOH {
namespace {

// PathData.count is a u8 (the vanilla struct); a longer path is cut, with a log line.
constexpr size_t kMaxPathPoints = 255;

std::vector<Vec3f> ReadPoints(const Json& path, const std::string& what) {
    std::vector<Vec3f> points; // SOH [Unbound] f32 waypoints (world extent)
    auto it = path.find(K::kPoints);
    if (it == path.end() || !it->is_array()) {
        return points;
    }
    for (const auto& p : *it) {
        if (p.is_array() && p.size() >= 3) {
            points.push_back(Unbound::ReadVec3f(p));
        }
    }
    if (points.size() > kMaxPathPoints) {
        SPDLOG_ERROR("[Unbound] {}: {} points, PathData.count holds at most {}; truncating", what, points.size(),
                     kMaxPathPoints);
        points.resize(kMaxPathPoints);
    }
    return points;
}

} // namespace

std::shared_ptr<Ship::IResource>
ResourceFactoryJsonPathV1::ReadResource(std::shared_ptr<Ship::File> file,
                                        std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }
    Json doc = Unbound::LoadMergedJson(initData->Path);
    if (!doc.is_object()) {
        SPDLOG_ERROR("[Unbound] {}: no usable document", initData->Path);
        return nullptr;
    }

    auto path = std::make_shared<Path>(initData);
    const Json& paths = doc.value(K::kPaths, Json::object());
    std::vector<std::string> keys;
    try {
        keys = Unbound::PositionalKeys(paths, initData->Path + " " + K::kPaths);
    } catch (const Unbound::DocumentError& e) {
        SPDLOG_ERROR("[Unbound] {}", e.what());
        return nullptr;
    }
    path->paths.reserve(keys.size());
    path->pathData.reserve(keys.size());
    for (const auto& k : keys) {
        path->paths.push_back(ReadPoints(paths[k], initData->Path + " " + K::kPaths + "/" + k));
        PathData data{};
        data.count = (u8)path->paths.back().size();
        data.points = path->paths.back().data();
        path->pathData.push_back(data);
    }
    path->numPaths = (uint32_t)path->pathData.size();
    return path;
}

} // namespace SOH
