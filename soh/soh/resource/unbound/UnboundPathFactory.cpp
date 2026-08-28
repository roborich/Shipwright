// SOH [Unbound] paths/<name>.json -> SOH::Path. See unbound-docs/scene-format.md §2.4.
#include "UnboundFactories.h"
#include "UnboundJson.h"

#include <spdlog/spdlog.h>

#include "soh/resource/type/Path.h"

using Unbound::Json;
using Unbound::ListKeys;
using Unbound::ToInt;

namespace SOH {

std::shared_ptr<Ship::IResource> ResourceFactoryJsonPathV1::ReadResource(std::shared_ptr<Ship::File> file,
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
    const Json& paths = doc.value("paths", Json::object());
    auto keys = ListKeys(paths);
    path->paths.reserve(keys.size());
    path->pathData.reserve(keys.size());
    for (const auto& k : keys) {
        std::vector<Vec3f> points; // SOH [Unbound] f32 waypoints (world extent)
        for (const auto& p : paths[k].value("points", Json::array())) {
            if (p.is_array() && p.size() >= 3) {
                points.push_back(Vec3f{ (f32)Unbound::ToNumber(p[0]), (f32)Unbound::ToNumber(p[1]), (f32)Unbound::ToNumber(p[2]) });
            }
        }
        path->paths.push_back(std::move(points));
        PathData data{};
        data.count = (u8)path->paths.back().size();
        data.points = path->paths.back().data();
        path->pathData.push_back(data);
    }
    path->numPaths = (uint32_t)path->pathData.size();
    return path;
}

} // namespace SOH
