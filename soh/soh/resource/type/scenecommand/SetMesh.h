#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <ship/resource/Resource.h>
#include "SceneCommand.h"
#include "libultraship/libultra.h"
#include "z64math.h"

namespace SOH {
typedef struct {
    /* 0x00 */ u8 type;
} PolygonBase;

typedef struct {
    /* 0x00 */ PolygonBase base;
    /* 0x01 */ u32 num; // number of dlist entries -- SOH [Unbound] mirrors z64.h
    /* 0x04 */ void* start;
    /* 0x08 */ void* end;
} PolygonType0; // size = 0xC

typedef struct {
    /* 0x00 */ u16 unk_00;
    /* 0x02 */ u8 id;
    /* 0x04 */ void* source;
    /* 0x08 */ u32 unk_0C;
    /* 0x0C */ u32 tlut;
    /* 0x10 */ u16 width;
    /* 0x12 */ u16 height;
    /* 0x14 */ u8 fmt;
    /* 0x15 */ u8 siz;
    /* 0x16 */ u16 mode0;
    /* 0x18 */ u16 tlutCount;
} BgImage; // size = 0x1C

typedef struct {
    /* 0x00 */ PolygonBase base;
    /* 0x01 */ u8 format; // 1 = single, 2 = multi
    /* 0x04 */ Gfx* dlist;
    union {
        struct {
            /* 0x08 */ void* source;
            /* 0x0C */ u32 unk_0C;
            /* 0x10 */ void* tlut;
            /* 0x14 */ u16 width;
            /* 0x16 */ u16 height;
            /* 0x18 */ u8 fmt;
            /* 0x19 */ u8 siz;
            /* 0x1A */ u16 mode0;
            /* 0x1C */ u16 tlutCount;
        } single;
        struct {
            /* 0x08 */ u8 count;
            /* 0x0C */ BgImage* list;
        } multi;
    };
} PolygonType1;

typedef struct {
    /* 0x00 */ PolygonBase base;
    /* 0x01 */ u32 num; // number of dlist entries -- SOH [Unbound] mirrors z64.h
    /* 0x04 */ void* start;
    /* 0x08 */ void* end;
} PolygonType2; // size = 0xC

typedef union {
    PolygonBase base;
    PolygonType0 polygon0;
    PolygonType1 polygon1;
    PolygonType2 polygon2;
} MeshHeader; // "Ground Shape"

typedef struct {
    /* 0x00 */ Vec3f pos;  // SOH [Unbound] s16 -> f32 (world extent)
    /* 0x06 */ f32 unk_06; // SOH [Unbound] cull radius, s16 -> f32
    /* 0x08 */ Gfx* opa;
    /* 0x0C */ Gfx* xlu;
} PolygonDlist2; // size = 0x8

typedef struct {
    /* 0x00 */ Gfx* opa;
    /* 0x04 */ Gfx* xlu;
} PolygonDlist; // size = 0x8

class SetMesh : public SceneCommand<MeshHeader> {
  public:
    using SceneCommand::SceneCommand;

    MeshHeader* GetPointer();
    size_t GetPointerSize();

    uint32_t numPoly;
    uint8_t data;
    uint8_t meshHeaderType;

    std::vector<std::string> opaPaths;
    std::vector<std::string> xluPaths;
    std::vector<PolygonDlist> dlists;
    std::vector<PolygonDlist2> dlists2;
    std::vector<std::string> imagePaths;
    std::vector<BgImage> images;
    MeshHeader meshHeader;

    // SOH [Unbound] Shared by the loaders. The game treats the returned pointer as the display list / image; the
    // interpreter resolves "__OTR__<path>" when the room is drawn, so the string must outlive the command: it is
    // kept in `store` (opaPaths / xluPaths / imagePaths), which the caller must `reserve()` before the first call
    // so the c_str() pointers stay valid. Returns nullptr for an empty path.
    static Gfx* KeepDlistPath(std::vector<std::string>& store, const std::string& path) {
        if (path.empty()) {
            return nullptr;
        }
        store.push_back("__OTR__" + path);
        return (Gfx*)store.back().c_str();
    }

    // PolygonType1.single mirrors BgImage minus the id / unk_00 pair.
    void SetSingleImage(const BgImage& image) {
        auto& single = meshHeader.polygon1.single;
        single.source = image.source;
        single.unk_0C = image.unk_0C;
        single.tlut = (void*)(uintptr_t)image.tlut; // OTRTODO: type of bgimage.tlut should be uintptr_t
        single.width = image.width;
        single.height = image.height;
        single.fmt = image.fmt;
        single.siz = image.siz;
        single.mode0 = image.mode0;
        single.tlutCount = image.tlutCount;
    }
};
}; // namespace SOH
