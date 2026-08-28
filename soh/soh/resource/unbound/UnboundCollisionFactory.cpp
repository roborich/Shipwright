// SOH [Unbound] collision.json + collision.bin -> SOH::CollisionHeader. See unbound-docs/scene-format.md §2.3.
#include "UnboundFactories.h"
#include "UnboundJson.h"

#include <spdlog/spdlog.h>
#include <cstring>

#include "soh/resource/type/CollisionHeader.h"

using Unbound::Json;
using Unbound::ListKeys;
using Unbound::ToInt;

namespace SOH {
namespace {

struct BinReader {
    const std::vector<char>& data;
    size_t pos = 0;
    bool ok = true;

    template <typename T> T Read() {
        T v{};
        if (pos + sizeof(T) > data.size()) {
            ok = false;
            return v;
        }
        std::memcpy(&v, data.data() + pos, sizeof(T)); // little-endian host assumed (all SoH targets)
        pos += sizeof(T);
        return v;
    }
    void Align4() {
        pos = (pos + 3) & ~(size_t)3;
    }
};

Vec3s ReadVec(const Json& v) {
    Vec3s out{ 0, 0, 0 };
    if (v.is_array() && v.size() >= 3) {
        out.x = (s16)ToInt(v[0]);
        out.y = (s16)ToInt(v[1]);
        out.z = (s16)ToInt(v[2]);
    }
    return out;
}

Vec3f ReadVecF(const Json& v) {
    Vec3f out{ 0.0f, 0.0f, 0.0f };
    if (v.is_array() && v.size() >= 3) {
        out.x = (f32)Unbound::ToNumber(v[0]);
        out.y = (f32)Unbound::ToNumber(v[1]);
        out.z = (f32)Unbound::ToNumber(v[2]);
    }
    return out;
}

// collision.bin layouts (scene-format.md §2.3):
//   v1: vertices s16 x3 (6 B, padded to 4), polys 24 B { u16 type, u32 vA, vB, vC, s16 nx, ny, nz, s16 dist, s16 pad }
//   v2: vertices f32 x3 (12 B),             polys 28 B { u16 type, u16 pad, u32 vA, vB, vC, s16 nx, ny, nz, s16 pad, f32 dist }
bool ReadBulk(CollisionHeader& col, const Json& bulk, int version, const std::string& docPath) {
    std::string binPath = bulk.value("file", "");
    auto bytes = Unbound::LoadBulk(binPath);
    if (bytes.empty()) {
        SPDLOG_ERROR("[Unbound] {}: bulk file {} missing", docPath, binPath);
        return false;
    }
    uint32_t numVertices = (uint32_t)ToInt(bulk.value("vertices", 0));
    uint32_t numPolys = (uint32_t)ToInt(bulk.value("polys", 0));
    BinReader r{ bytes };

    col.vertices.reserve(numVertices);
    for (uint32_t i = 0; i < numVertices && r.ok; i++) {
        Vec3f v;
        if (version >= 2) {
            v.x = r.Read<float>();
            v.y = r.Read<float>();
            v.z = r.Read<float>();
        } else {
            v.x = r.Read<int16_t>();
            v.y = r.Read<int16_t>();
            v.z = r.Read<int16_t>();
        }
        col.vertices.push_back(v);
    }
    r.Align4();
    col.polygons.reserve(numPolys);
    for (uint32_t i = 0; i < numPolys && r.ok; i++) {
        CollisionPoly p{};
        p.type = r.Read<uint16_t>();
        if (version >= 2) {
            r.Read<uint16_t>(); // pad
        }
        p.flags_vIA = r.Read<uint32_t>();
        p.flags_vIB = r.Read<uint32_t>();
        p.vIC = r.Read<uint32_t>();
        p.normal.x = r.Read<int16_t>();
        p.normal.y = r.Read<int16_t>();
        p.normal.z = r.Read<int16_t>();
        if (version >= 2) {
            r.Read<int16_t>(); // pad
            p.dist = r.Read<float>();
        } else {
            p.dist = r.Read<int16_t>();
            r.Read<int16_t>(); // pad
        }
        col.polygons.push_back(p);
    }
    if (!r.ok) {
        SPDLOG_ERROR("[Unbound] {}: bulk file {} is shorter than its declared counts", docPath, binPath);
        return false;
    }
    col.collisionHeaderData.numVertices = (u32)col.vertices.size();
    col.collisionHeaderData.vtxList = col.vertices.data();
    col.collisionHeaderData.numPolygons = (u32)col.polygons.size();
    col.collisionHeaderData.polyList = col.polygons.data();
    return true;
}

void ReadSurfaceTypes(CollisionHeader& col, const Json& list) {
    for (const auto& k : ListKeys(list)) {
        SurfaceType s{};
        s.data[0] = (u32)ToInt(list[k].value("data0", Json(0)));
        s.data[1] = (u32)ToInt(list[k].value("data1", Json(0)));
        col.surfaceTypes.push_back(s);
    }
    col.surfaceTypesCount = (uint32_t)col.surfaceTypes.size();
    col.collisionHeaderData.surfaceTypeList = col.surfaceTypes.data();
}

void ReadCameras(CollisionHeader& col, const Json& cameras, const Json& positions) {
    for (const auto& k : ListKeys(positions)) {
        col.camPosData.push_back(ReadVec(positions[k]));
    }
    col.camPosCount = (int32_t)col.camPosData.size();
    col.camPosDataZero = Vec3s{ 0, 0, 0 };

    for (const auto& k : ListKeys(cameras)) {
        const Json& c = cameras[k];
        CamData cam{};
        cam.cameraSType = (u16)ToInt(c.value("sType", Json(0)));
        cam.numCameras = (s16)ToInt(c.value("count", Json(0)));
        int32_t idx = c.contains("positionIndex") && !c["positionIndex"].is_null()
                          ? (int32_t)ToInt(c["positionIndex"])
                          : -1;
        col.camPosDataIndices.push_back(idx);
        col.camData.push_back(cam);
    }
    col.camDataCount = (uint32_t)col.camData.size();
    for (size_t i = 0; i < col.camData.size(); i++) {
        int32_t idx = col.camPosDataIndices[i];
        col.camData[i].camPosData =
            (idx >= 0 && idx < col.camPosCount) ? &col.camPosData[(size_t)idx] : &col.camPosDataZero;
    }
    col.collisionHeaderData.cameraDataList = col.camData.data();
    col.collisionHeaderData.cameraDataListLen = col.camDataCount;
}

void ReadWaterBoxes(CollisionHeader& col, const Json& list) {
    for (const auto& k : ListKeys(list)) {
        const Json& w = list[k];
        WaterBox box{};
        box.xMin = (f32)Unbound::ToNumber(w.value("xMin", Json(0)));
        box.ySurface = (f32)Unbound::ToNumber(w.value("ySurface", Json(0)));
        box.zMin = (f32)Unbound::ToNumber(w.value("zMin", Json(0)));
        box.xLength = (f32)Unbound::ToNumber(w.value("xLength", Json(0)));
        box.zLength = (f32)Unbound::ToNumber(w.value("zLength", Json(0)));
        box.properties = (u32)ToInt(w.value("properties", Json(0)));
        // Explicit "room" (-1 = all) overrides the 6-bit packed field, lifting the 63-room cap.
        box.room = w.contains("room") ? (s32)ToInt(w["room"]) : WATERBOX_UNPACK_ROOM(box.properties);
        col.waterBoxes.push_back(box);
    }
    col.collisionHeaderData.numWaterBoxes = (u16)col.waterBoxes.size();
    col.collisionHeaderData.waterBoxes = col.waterBoxes.data();
}

} // namespace

std::shared_ptr<Ship::IResource>
ResourceFactoryJsonCollisionHeaderV1::ReadResource(std::shared_ptr<Ship::File> file,
                                                   std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }
    Json doc = Unbound::LoadMergedJson(initData->Path);
    if (!doc.is_object()) {
        SPDLOG_ERROR("[Unbound] {}: no usable document", initData->Path);
        return nullptr;
    }

    auto col = std::make_shared<CollisionHeader>(initData);
    const Json& bounds = doc.value("bounds", Json::object());
    col->collisionHeaderData.minBounds = ReadVecF(bounds.value("min", Json::array()));
    col->collisionHeaderData.maxBounds = ReadVecF(bounds.value("max", Json::array()));

    // "$schema": "unbound/collision/<n>" selects the collision.bin layout; missing = v1.
    int version = 1;
    std::string schema = doc.value("$schema", "");
    if (auto slash = schema.rfind('/'); slash != std::string::npos) {
        try {
            version = std::stoi(schema.substr(slash + 1));
        } catch (...) {
            version = 1;
        }
    }
    if (!ReadBulk(*col, doc.value("bulk", Json::object()), version, initData->Path)) {
        return nullptr;
    }
    ReadSurfaceTypes(*col, doc.value("surfaceTypes", Json::object()));
    ReadCameras(*col, doc.value("cameras", Json::object()), doc.value("cameraPositions", Json::object()));
    ReadWaterBoxes(*col, doc.value("waterBoxes", Json::object()));
    return col;
}

} // namespace SOH
