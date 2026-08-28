// SOH [Unbound] collision.json + collision.bin -> SOH::CollisionHeader. See unbound-docs/scene-format.md §2.3.
#include "UnboundFactories.h"
#include "UnboundJson.h"
#include "UnboundSchema.h"

#include <spdlog/spdlog.h>
#include <ship/utils/binarytools/BinaryReader.h>

#include "soh/resource/type/CollisionHeader.h"

using Unbound::Json;
using Unbound::ListKeys;
using Unbound::ToInt;
namespace K = Unbound::Schema;

namespace SOH {
namespace {

// collision.bin layouts (scene-format.md §2.3): little-endian, vertices then polys, no header.
//   v1: vertex { s16 x, y, z } (6 B), block padded to 4;
//       poly { u16 type, u32 vA, vB, vC, s16 nx, ny, nz, s16 dist, s16 pad } (24 B)
//   v2: vertex { f32 x, y, z } (12 B);
//       poly { u16 type, u16 pad, u32 vA, vB, vC, s16 nx, ny, nz, s16 pad, f32 dist } (28 B)
size_t BulkVertexBlockSize(int version, uint32_t numVertices) {
    size_t bytes = (size_t)numVertices * (version >= 2 ? 12 : 6);
    return version >= 2 ? bytes : (bytes + 3) & ~(size_t)3;
}

size_t BulkSize(int version, uint32_t numVertices, uint32_t numPolys) {
    return BulkVertexBlockSize(version, numVertices) + (size_t)numPolys * (version >= 2 ? 28 : 24);
}

Vec3f ReadVertex(Ship::BinaryReader& r, int version) {
    Vec3f v;
    if (version >= 2) {
        v.x = r.ReadFloat();
        v.y = r.ReadFloat();
        v.z = r.ReadFloat();
    } else {
        v.x = r.ReadInt16();
        v.y = r.ReadInt16();
        v.z = r.ReadInt16();
    }
    return v;
}

CollisionPoly ReadPoly(Ship::BinaryReader& r, int version) {
    CollisionPoly p{};
    p.type = r.ReadUInt16();
    if (version >= 2) {
        r.ReadUInt16(); // pad
    }
    p.flags_vIA = r.ReadUInt32();
    p.flags_vIB = r.ReadUInt32();
    p.vIC = r.ReadUInt32();
    p.normal.x = r.ReadInt16();
    p.normal.y = r.ReadInt16();
    p.normal.z = r.ReadInt16();
    if (version >= 2) {
        r.ReadInt16(); // pad
        p.dist = r.ReadFloat();
    } else {
        p.dist = r.ReadInt16();
        r.ReadInt16(); // pad
    }
    return p;
}

bool ReadBulk(CollisionHeader& col, const Json& bulk, int version, const std::string& docPath) {
    std::string binPath = bulk.value(K::kFile, "");
    std::vector<char> bytes = Unbound::LoadBulk(binPath);
    if (bytes.empty()) {
        SPDLOG_ERROR("[Unbound] {}: bulk file {} missing", docPath, binPath);
        return false;
    }
    uint32_t numVertices = (uint32_t)ToInt(bulk.value(K::kVertices, 0));
    uint32_t numPolys = (uint32_t)ToInt(bulk.value(K::kPolys, 0));
    if (bytes.size() < BulkSize(version, numVertices, numPolys)) {
        SPDLOG_ERROR("[Unbound] {}: bulk file {} is shorter than its declared counts", docPath, binPath);
        return false;
    }

    Ship::BinaryReader r(bytes.data(), bytes.size());
    r.SetEndianness(Ship::Endianness::Little);
    col.vertices.reserve(numVertices);
    for (uint32_t i = 0; i < numVertices; i++) {
        col.vertices.push_back(ReadVertex(r, version));
    }
    r.Seek((int32_t)BulkVertexBlockSize(version, numVertices), Ship::SeekOffsetType::Start);
    col.polygons.reserve(numPolys);
    for (uint32_t i = 0; i < numPolys; i++) {
        col.polygons.push_back(ReadPoly(r, version));
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
        s.data[0] = (u32)ToInt(list[k].value(K::kData0, Json(0)));
        s.data[1] = (u32)ToInt(list[k].value(K::kData1, Json(0)));
        col.surfaceTypes.push_back(s);
    }
    col.surfaceTypesCount = (uint32_t)col.surfaceTypes.size();
    col.collisionHeaderData.surfaceTypeList = col.surfaceTypes.data();
}

void ReadCameras(CollisionHeader& col, const Json& cameras, const Json& positions) {
    // Camera positions stay s16: CamData packs them into Vec3s (README, "Known remaining limits").
    for (const auto& k : ListKeys(positions)) {
        col.camPosData.push_back(Unbound::ReadVec3s(positions[k]));
    }
    col.camPosCount = (int32_t)col.camPosData.size();
    col.camPosDataZero = Vec3s{ 0, 0, 0 };

    for (const auto& k : ListKeys(cameras)) {
        const Json& c = cameras[k];
        CamData cam{};
        cam.cameraSType = (u16)ToInt(c.value(K::kSType, Json(0)));
        cam.numCameras = (s16)ToInt(c.value(K::kCount, Json(0)));
        int32_t idx = c.contains(K::kPositionIndex) && !c[K::kPositionIndex].is_null()
                          ? (int32_t)ToInt(c[K::kPositionIndex])
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
        box.xMin = (f32)Unbound::ToNumber(w.value(K::kXMin, Json(0)));
        box.ySurface = (f32)Unbound::ToNumber(w.value(K::kYSurface, Json(0)));
        box.zMin = (f32)Unbound::ToNumber(w.value(K::kZMin, Json(0)));
        box.xLength = (f32)Unbound::ToNumber(w.value(K::kXLength, Json(0)));
        box.zLength = (f32)Unbound::ToNumber(w.value(K::kZLength, Json(0)));
        box.properties = (u32)ToInt(w.value(K::kProperties, Json(0)));
        // Explicit K::kRoom (-1 = all) overrides the 6-bit packed field, lifting the 63-room cap.
        box.room = w.contains(K::kRoom) ? (s32)ToInt(w[K::kRoom]) : WATERBOX_UNPACK_ROOM(box.properties);
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
    const Json& bounds = doc.value(K::kBounds, Json::object());
    col->collisionHeaderData.minBounds = Unbound::ReadVec3f(bounds.value(K::kMin, Json::array()));
    col->collisionHeaderData.maxBounds = Unbound::ReadVec3f(bounds.value(K::kMax, Json::array()));

    // K::kSchema: "unbound/collision/<n>" selects the collision.bin layout; missing = v1.
    int version = 1;
    std::string schema = doc.value(K::kSchema, "");
    if (auto slash = schema.rfind('/'); slash != std::string::npos) {
        try {
            version = std::stoi(schema.substr(slash + 1));
        } catch (...) { version = 1; }
    }
    if (!ReadBulk(*col, doc.value(K::kBulk, Json::object()), version, initData->Path)) {
        return nullptr;
    }
    ReadSurfaceTypes(*col, doc.value(K::kSurfaceTypes, Json::object()));
    ReadCameras(*col, doc.value(K::kCameras, Json::object()), doc.value(K::kCameraPositions, Json::object()));
    ReadWaterBoxes(*col, doc.value(K::kWaterBoxes, Json::object()));
    return col;
}

} // namespace SOH
