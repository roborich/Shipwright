#ifndef Z_BGCHECK_H
#define Z_BGCHECK_H

struct PlayState;
struct Actor;
struct DynaPolyActor;

#define COLPOLY_NORMAL_FRAC (1.0f / SHT_MAX)
#define COLPOLY_SNORMAL(x) ((s16)((x) * SHT_MAX))
#define COLPOLY_GET_NORMAL(n) ((n)*COLPOLY_NORMAL_FRAC)
// SOH [Unbound] Vertex indices are 32-bit words: bits 0-28 hold the index, bits 29-31 hold the
// poly exclusion flags (vIA) / conveyor flag (vIB). The N64 packed a 13-bit index into a u16.
#define COLPOLY_VTX_INDEX_MASK 0x1FFFFFFFu
#define COLPOLY_VIA_FLAGS_SHIFT 29
#define COLPOLY_VIA_FLAGS_MASK 0xE0000000u
#define COLPOLY_VIB_CONVEYOR (1u << 29)
#define COLPOLY_VIA_FLAG_TEST(vIA, flags) ((vIA) & (((flags)&7) << COLPOLY_VIA_FLAGS_SHIFT))
#define COLPOLY_VTX_INDEX(vI) ((vI)&COLPOLY_VTX_INDEX_MASK)

#define DYNAPOLY_INVALIDATE_LOOKUP (1 << 0)

// BGACTOR_NEG_ONE is the "no dyna actor" value an actor stores in its own bgId / floorBgId / wallBgId.
#define BGACTOR_NEG_ONE -1
// SOH [Unbound] The dyna actor table is heap-allocated and grows on demand (DynaCollisionContext.bgActorMax), so
// the sentinels are fixed constants instead of the table size. BGCHECK_SCENE is the bgId of the scene's static
// collision. BGACTOR_INVALID is the "could not allocate" value the overlay `== BGACTOR_INVALID` tests compare
// against; vanilla returned BG_ACTOR_MAX there, the same number. DynaPoly_SetBgActor now grows the table instead.
#define BGCHECK_SCENE 0x7FFF
#define BGACTOR_INVALID BGCHECK_SCENE
#define BGACTOR_INITIAL_MAX 64
// Superseded dyna poly/vtx buffers parked per scene (see DynaCollisionContext.retiredBuffers). Growth doubles
// from 16 384, so a list retires one buffer per doubling; 32 covers both lists past 2^31 entries.
#define DYNA_RETIRED_BUFFERS_MAX 32
// SOH [Unbound] World extent: positions are f32 end to end; 2^20 keeps ~0.06-unit precision at the edge.
// BGCHECK_Y_MIN is the "no floor" sentinel (exactly representable in f32).
#define BGCHECK_Y_MIN -2147483648.0f
#define BGCHECK_XYZ_ABSMAX 1048576.0f
#define BGCHECK_SUBDIV_OVERLAP 50
#define BGCHECK_SUBDIV_MIN 150.0f

#define FUNC_80041EA4_RESPAWN 5
#define FUNC_80041EA4_MOUNT_WALL 6
#define FUNC_80041EA4_STOP 8
#define FUNC_80041EA4_VOID_OUT 12


typedef struct {
    Vec3f scale;
    Vec3s rot;
    Vec3f pos;
} ScaleRotPos;

// SOH [Unbound] Widened from the N64 0x10-byte layout; see unbound-docs/collision.md
typedef struct {
    u16 type;
    union {
        u32 vtxData[3];
        struct {
            u32 flags_vIA; // COLPOLY_VIA_FLAGS_MASK is poly exclusion flags (xpFlags), COLPOLY_VTX_INDEX_MASK is vtxId
            u32 flags_vIB; // COLPOLY_VIB_CONVEYOR = poly IsConveyor surface, COLPOLY_VTX_INDEX_MASK is vtxId
            u32 vIC;
        };
    };
    Vec3s normal; // Unit normal vector
                  // Value ranges from -0x7FFF to 0x7FFF, representing -1.0 to 1.0; 0x8000 is invalid

    s32 dist; // Plane distance from origin along the normal. // SOH [Unbound] s16 -> s32 (world extent).
              // Derived from a unit normal, so it is fractional even when every vertex is integral; the writer
              // rounds it (vanilla truncated it into an s16).
} CollisionPoly;

typedef struct {
    /* 0x00 */ u16 cameraSType;
    /* 0x02 */ s16 numCameras;
    /* 0x04 */ Vec3s* camPosData;
} CamData;

// SOH [Unbound] Widened from the N64 0x10-byte layout: extents are s32 (world extent) and the packed
// `properties` word is unpacked into fields (WaterBox_UnpackProperties for legacy data).
typedef struct {
    s32 xMin;
    s32 ySurface;
    s32 zMin;
    s32 xLength;
    s32 zLength;
    s32 camera;       // CamData index (was 8 bits)
    s32 lightSetting; // lighting settings index (was 5 bits)
    s32 room;         // -1 = all rooms (was 6 bits, 0x3F)
    u8 notSwimmable;        // vanilla bit 19: box is only found by func_800425B0, not WaterBox_GetSurface*
} WaterBox;

// SOH [Unbound] The two packed vanilla words are unpacked into named fields (SurfaceType_Unpack for legacy
// data), so `camera`, `exit` and `lightSetting` are no longer capped at 8 / 5 / 5 bits.
typedef struct {
    s32 camera;        // CamData index
    s32 exit;          // scene exit index, 0 = none
    s32 lightSetting;  // lighting settings index
    u8 floorType;      // 0-31
    u8 wallFlags;      // 0-7 (vanilla "unk18")
    u8 wallType;       // 0-31, index into D_80119D90
    u8 floorProperty;  // 0-15
    u8 isSoft;         // "floor minus 1"
    u8 isHorseBlocked;
    u8 material;       // 0-15, walk sfx index
    u8 floorEffect;    // 0-3, slope
    u8 echo;           // 0-63
    u8 canHookshot;
    u8 conveyorSpeed;     // 0-7
    u8 conveyorDirection; // 0-63, 360 / 64 degrees
    u8 isWallDamage;
} SurfaceType;

// SOH [Unbound] Legacy (packed) collision data is unpacked at the loader boundary with these.
#ifdef __cplusplus
extern "C" {
#endif
SurfaceType SurfaceType_Unpack(u32 data0, u32 data1);
void WaterBox_UnpackProperties(WaterBox* waterBox, u32 properties);
#ifdef __cplusplus
}
#endif

// SOH [Unbound] Widened from the N64 layout: bounds and vertices are s32 (world extent), counts are u32
typedef struct {
    Vec3i minBounds; // minimum coordinates of poly bounding box
    Vec3i maxBounds; // maximum coordinates of poly bounding box
    u32 numVertices;
    Vec3i* vtxList;
    u32 numPolygons;
    CollisionPoly* polyList;
    SurfaceType* surfaceTypeList;
    CamData* cameraDataList;
    u16 numWaterBoxes;
    WaterBox* waterBoxes;
    size_t cameraDataListLen; // OTRTODO: Added to allow for bounds checking the cameraDataList.
} CollisionHeader; // original name: BGDataInfo

// SOH [Unbound] SSNode/SSList indices widened from 16 to 32 bits (SS_NULL is 0xFFFFFFFF)
typedef struct {
    s32 polyId;
    u32 next; // next SSNode index
} SSNode;

typedef struct {
    u32 head; // first SSNode index
} SSList;

typedef struct {
    u32 max;          // original name: short_slist_node_size
    u32 count;        // original name: short_slist_node_last_index
    SSNode* tbl;      // original name: short_slist_node_tbl
    u8* polyCheckTbl; // points to an array of bytes, one per static poly. Zero initialized when starting a
                      // bg check, and set to 1 if that poly has already been tested.
} SSNodeList;

typedef struct {
    SSNode* tbl;
    s32 count;
    s32 max;
} DynaSSNodeList;

typedef struct {
    SSList floor;
    SSList wall;
    SSList ceiling;
} StaticLookup;

typedef struct {
    u32 polyStartIndex; // SOH [Unbound] widened from u16
    SSList ceiling;
    SSList wall;
    SSList floor;
} DynaLookup;

typedef struct {
    struct Actor* actor;
    CollisionHeader* colHeader;
    DynaLookup dynaLookup;
    u32 vtxStartIndex; // SOH [Unbound] widened from u16
    ScaleRotPos prevTransform;
    ScaleRotPos curTransform;
    Sphere16 boundingSphere;
    f32 minY;
    f32 maxY;
} BgActor;

// SOH [Unbound] Every table here is heap-allocated, grows on demand and is released by BgCheck_Free
typedef struct {
    u8 bitFlag;
    BgActor* bgActors;  // bgActorMax slots; grown by DynaPoly_SetBgActor
    u16* bgActorFlags; // & 0x0008 = no dyna ceiling
    s32 bgActorMax;
    // polyList/vtxList grow in DynaPoly_Setup; superseded buffers are parked here until BgCheck_Free because
    // actors keep CollisionPoly* into them (already stale each frame, but they must stay readable).
    void* retiredBuffers[DYNA_RETIRED_BUFFERS_MAX];
    s32 retiredCount;
    CollisionPoly* polyList;
    Vec3i* vtxList; // s16 -> s32 (world extent). Baked world-space, so these quantise to whole units
                    // each frame as they did in vanilla.
    DynaSSNodeList polyNodes;
    s32 polyNodesMax;
    s32 polyListMax;
    s32 vtxListMax;
} DynaCollisionContext;

typedef struct CollisionContext {
    CollisionHeader* colHeader; // scene's static collision
    Vec3f minBounds;            // minimum coordinates of collision bounding box
    Vec3f maxBounds;            // maximum coordinates of collision bounding box
    Vec3i subdivAmount;         // x, y, z subdivisions of the scene's static collision
    Vec3f subdivLength;         // x, y, z subdivision worldspace lengths
    Vec3f subdivLengthInv;      // inverse of subdivision length
    StaticLookup* lookupTbl;    // 3d array of length subdivAmount
    SSNodeList polyNodes;
    DynaCollisionContext dyna;
} CollisionContext; // SOH [Unbound] memSize (the N64 byte budget) is gone; see BgCheck_Allocate

typedef struct {
    /* 0x00 */ struct PlayState* play;
    /* 0x04 */ struct CollisionContext* colCtx;
    /* 0x08 */ u16 xpFlags;
    /* 0x0C */ CollisionPoly** resultPoly;
    /* 0x10 */ f32 yIntersect;
    /* 0x14 */ Vec3f* pos;
    /* 0x18 */ s32* bgId;
    /* 0x1C */ struct Actor* actor;
    /* 0x20 */ u32 unk_20;
    /* 0x24 */ f32 chkDist;
    /* 0x28 */ DynaCollisionContext* dyna;
    /* 0x2C */ SSList* ssList;
} DynaRaycast;

typedef struct {
    /* 0x00 */ struct CollisionContext* colCtx;
    /* 0x04 */ u16 xpFlags;
    /* 0x08 */ DynaCollisionContext* dyna;
    /* 0x0C */ SSList* ssList;
    /* 0x10 */ Vec3f* posA;
    /* 0x14 */ Vec3f* posB;
    /* 0x18 */ Vec3f* posResult;
    /* 0x1C */ CollisionPoly** resultPoly;
    /* 0x20 */ s32 chkOneFace; // bccFlags & 0x8
    /* 0x24 */ f32* distSq;    // distance from posA to poly squared
    /* 0x28 */ f32 chkDist;    // distance from poly
} DynaLineTest;

#endif
