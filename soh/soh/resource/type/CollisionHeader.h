#pragma once

#include <cstdint>
#include <vector>
#include <ship/resource/Resource.h>
#include <libultraship/libultra.h>
#include "z64math.h"

namespace SOH {

// SOH [Unbound] Must mirror CollisionPoly in soh/include/z64bgcheck.h (u32 vertex words, flags in bits 29-31)
typedef struct {
    u16 type;
    union {
        u32 vtxData[3];
        struct {
            u32 flags_vIA; // bits 29-31 poly exclusion flags (xpFlags), bits 0-28 vtxId
            u32 flags_vIB; // bit 29 = poly IsConveyor surface, bits 0-28 vtxId
            u32 vIC;
        };
    };
    Vec3s normal; // Unit normal vector
                  // Value ranges from -0x7FFF to 0x7FFF, representing -1.0 to 1.0; 0x8000 is invalid

    s16 dist; // Plane distance from origin along the normal
} CollisionPoly;

typedef struct {
    /* 0x00 */ s16 xMin;
    /* 0x02 */ s16 ySurface;
    /* 0x04 */ s16 zMin;
    /* 0x06 */ s16 xLength;
    /* 0x08 */ s16 zLength;
    /* 0x0C */ u32 properties;

    // 0x0008_0000 = ?
    // 0x0007_E000 = Room Index, 0x3F = all rooms
    // 0x0000_1F00 = Lighting Settings Index
    // 0x0000_00FF = CamData index
} WaterBox; // size = 0x10

typedef struct {
    /* 0x00 */ u16 cameraSType;
    /* 0x02 */ s16 numCameras;
    /* 0x04 */ Vec3s* camPosData;
} CamData;

typedef struct {
    u32 data[2];

    // Type 1
    // 0x0800_0000 = wall damage
} SurfaceType;

typedef struct {
    /* 0x00 */ Vec3s minBounds; // minimum coordinates of poly bounding box
    /* 0x06 */ Vec3s maxBounds; // maximum coordinates of poly bounding box
    /* 0x0C */ u32 numVertices; // SOH [Unbound] widened from u16
    /* 0x10 */ Vec3s* vtxList;
    /* 0x14 */ u32 numPolygons; // SOH [Unbound] widened from u16
    /* 0x18 */ CollisionPoly* polyList;
    /* 0x1C */ SurfaceType* surfaceTypeList;
    /* 0x20 */ CamData* cameraDataList;
    /* 0x24 */ u16 numWaterBoxes;
    /* 0x28 */ WaterBox* waterBoxes;
    size_t cameraDataListLen; // OTRTODO: Added to allow for bounds checking the cameraDataList.
} CollisionHeaderData;        // original name: BGDataInfo

class CollisionHeader : public Ship::Resource<CollisionHeaderData> {
  public:
    using Resource::Resource;

    CollisionHeader() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
    }

    CollisionHeaderData* GetPointer();
    size_t GetPointerSize();

    CollisionHeaderData collisionHeaderData;

    std::vector<Vec3s> vertices;

    std::vector<CollisionPoly> polygons;

    uint32_t surfaceTypesCount;
    std::vector<SurfaceType> surfaceTypes;

    uint32_t camDataCount;
    std::vector<CamData> camData;
    std::vector<int32_t> camPosDataIndices;

    int32_t camPosCount;
    Vec3s camPosDataZero;
    std::vector<Vec3s> camPosData;

    std::vector<WaterBox> waterBoxes;
};
}; // namespace SOH
