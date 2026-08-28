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

    f32 dist; // Plane distance from origin along the normal. // SOH [Unbound] s16 -> f32 (world extent)
} CollisionPoly;

// SOH [Unbound] Must mirror WaterBox in soh/include/z64bgcheck.h (f32 extents, unpacked properties)
typedef struct {
    f32 xMin;
    f32 ySurface;
    f32 zMin;
    f32 xLength;
    f32 zLength;
    s32 camera;
    s32 lightSetting;
    s32 room;
    u8 notSwimmable;
} WaterBox;

typedef struct {
    /* 0x00 */ u16 cameraSType;
    /* 0x02 */ s16 numCameras;
    /* 0x04 */ Vec3s* camPosData;
} CamData;

// SOH [Unbound] Must mirror SurfaceType in soh/include/z64bgcheck.h (unpacked fields)
typedef struct {
    s32 camera;
    s32 exit;
    s32 lightSetting;
    u8 floorType;
    u8 wallFlags;
    u8 wallType;
    u8 floorProperty;
    u8 isSoft;
    u8 isHorseBlocked;
    u8 material;
    u8 floorEffect;
    u8 echo;
    u8 canHookshot;
    u8 conveyorSpeed;
    u8 conveyorDirection;
    u8 isWallDamage;
} SurfaceType;

// SOH [Unbound] Legacy archives carry the packed vanilla words; defined in CollisionHeaderFactory.cpp, which
// can see the game header (this mirror header deliberately does not include it).
SurfaceType UnpackSurfaceType(uint32_t data0, uint32_t data1);
void UnpackWaterBoxProperties(WaterBox& waterBox, uint32_t properties);

// SOH [Unbound] Must mirror CollisionHeader in soh/include/z64bgcheck.h (f32 bounds/vertices, u32 counts)
typedef struct {
    Vec3f minBounds; // minimum coordinates of poly bounding box
    Vec3f maxBounds; // maximum coordinates of poly bounding box
    u32 numVertices;
    Vec3f* vtxList;
    u32 numPolygons;
    CollisionPoly* polyList;
    SurfaceType* surfaceTypeList;
    CamData* cameraDataList;
    u16 numWaterBoxes;
    WaterBox* waterBoxes;
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

    std::vector<Vec3f> vertices;

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
