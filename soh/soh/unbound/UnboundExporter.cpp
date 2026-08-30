// SOH [Unbound] Converts the mounted vanilla archive into the Unbound layout (unbound-docs/SPEC.md).
//
// Orchestration: ExportArchive
//   -> for every registry scene (and its MQ variant): ConvertScene
//        -> BuildSceneDocument / BuildRoomDocument (one setup object per header, alternates included)
//        -> ConvertCollision, ConvertPaths
//   -> ConvertMessages (one JSON per language table)
//   -> CopyUntouchedFiles (every base-archive file we did not transform, verbatim)
//   -> manifest
// Output is a stored (uncompressed) zip written incrementally by ZipWriter.
#include "UnboundExporter.h"

#include <libultraship/libultraship.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_map>

#include "variables.h" // gBuildVersion
#include "soh/unbound/SceneDB.h"
#include "soh/unbound/UnboundJson.h"
#include "soh/unbound/UnboundSchema.h"
#include "soh/resource/type/CollisionHeader.h"
#include "soh/resource/type/Path.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/Text.h"
#include "soh/resource/type/scenecommand/SetActorList.h"
#include "soh/resource/type/scenecommand/SetAlternateHeaders.h"
#include "soh/resource/type/scenecommand/SetCameraSettings.h"
#include "soh/resource/type/scenecommand/SetCollisionHeader.h"
#include "soh/resource/type/scenecommand/SetCutscenes.h"
#include "soh/resource/type/scenecommand/SetEchoSettings.h"
#include "soh/resource/type/scenecommand/SetEntranceList.h"
#include "soh/resource/type/scenecommand/SetExitList.h"
#include "soh/resource/type/scenecommand/SetLightList.h"
#include "soh/resource/type/scenecommand/SetLightingSettings.h"
#include "soh/resource/type/scenecommand/SetMesh.h"
#include "soh/resource/type/scenecommand/SetObjectList.h"
#include "soh/resource/type/scenecommand/SetPathways.h"
#include "soh/resource/type/scenecommand/SetRoomBehavior.h"
#include "soh/resource/type/scenecommand/SetRoomList.h"
#include "soh/resource/type/scenecommand/SetSkyboxModifier.h"
#include "soh/resource/type/scenecommand/SetSkyboxSettings.h"
#include "soh/resource/type/scenecommand/SetSoundSettings.h"
#include "soh/resource/type/scenecommand/SetSpecialObjects.h"
#include "soh/resource/type/scenecommand/SetStartPositionList.h"
#include "soh/resource/type/scenecommand/SetTimeSettings.h"
#include "soh/resource/type/scenecommand/SetTransitionActorList.h"
#include "soh/resource/type/scenecommand/SetWindSettings.h"

using json = nlohmann::ordered_json;
namespace K = SOH::Unbound::Schema;

namespace SOH::Unbound {
namespace {

// ---------------------------------------------------------------------------------------------
// Stored-entry zip writer. Deterministic (fixed 1980-01-01 timestamps), no compression, no ZIP64:
// an archive past 65 535 entries or 4 GB is refused (Error()) rather than written corrupt.
// ---------------------------------------------------------------------------------------------
class ZipWriter {
  public:
    bool Open(const std::string& path) {
        mOut.open(path, std::ios::binary | std::ios::trunc);
        return mOut.good();
    }

    bool Add(const std::string& name, const uint8_t* data, size_t size) {
        if (!mError.empty()) {
            return false;
        }
        uint64_t offset = (uint64_t)mOut.tellp();
        if (mEntries.size() >= 0xFFFF || offset + size + name.size() + 30 > 0xFFFFFFFFull || name.size() > 0xFFFF) {
            mError = "zip limit exceeded at entry " + name + " (no ZIP64 support)";
            return false;
        }
        Entry e;
        e.name = name;
        e.size = (uint32_t)size;
        e.crc = Crc32(data, size);
        e.offset = (uint32_t)offset;

        Put32(0x04034b50);
        Put16(20);     // version needed
        Put16(0x0800); // utf-8 names
        Put16(0);      // stored
        Put16(0);      // time
        Put16(0x21);   // date 1980-01-01
        Put32(e.crc);
        Put32(e.size);
        Put32(e.size);
        Put16((uint16_t)name.size());
        Put16(0);
        mOut.write(name.data(), name.size());
        mOut.write((const char*)data, size);
        mEntries.push_back(std::move(e));
        return true;
    }

    bool Add(const std::string& name, const std::string& text) {
        return Add(name, (const uint8_t*)text.data(), text.size());
    }

    bool Close() {
        if (!mError.empty()) {
            mOut.close();
            return false;
        }
        uint32_t cdOffset = (uint32_t)mOut.tellp();
        for (const auto& e : mEntries) {
            Put32(0x02014b50);
            Put16(20);
            Put16(20);
            Put16(0x0800);
            Put16(0);
            Put16(0);
            Put16(0x21);
            Put32(e.crc);
            Put32(e.size);
            Put32(e.size);
            Put16((uint16_t)e.name.size());
            Put16(0);
            Put16(0);
            Put16(0);
            Put16(0);
            Put32(0);
            Put32(e.offset);
            mOut.write(e.name.data(), e.name.size());
        }
        uint32_t cdSize = (uint32_t)mOut.tellp() - cdOffset;
        Put32(0x06054b50);
        Put16(0);
        Put16(0);
        Put16((uint16_t)mEntries.size());
        Put16((uint16_t)mEntries.size());
        Put32(cdSize);
        Put32(cdOffset);
        Put16(0);
        mOut.close();
        if (mOut.fail()) {
            mError = "write failed";
        }
        return mError.empty();
    }

    const std::string& Error() const {
        return mError;
    }

    size_t Count() const {
        return mEntries.size();
    }

  private:
    struct Entry {
        std::string name;
        uint32_t crc;
        uint32_t size;
        uint32_t offset;
    };

    void Put16(uint16_t v) {
        char b[2] = { (char)(v & 0xFF), (char)(v >> 8) };
        mOut.write(b, 2);
    }
    void Put32(uint32_t v) {
        char b[4] = { (char)(v & 0xFF), (char)((v >> 8) & 0xFF), (char)((v >> 16) & 0xFF), (char)(v >> 24) };
        mOut.write(b, 4);
    }

    static uint32_t Crc32(const uint8_t* data, size_t size) {
        static uint32_t table[256];
        static bool init = false;
        if (!init) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) {
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                }
                table[i] = c;
            }
            init = true;
        }
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < size; i++) {
            c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
        }
        return c ^ 0xFFFFFFFFu;
    }

    std::ofstream mOut;
    std::vector<Entry> mEntries;
    std::string mError; // first failure; every later Add() is a no-op
};

// ---------------------------------------------------------------------------------------------
// Shared state for one export run
// ---------------------------------------------------------------------------------------------
struct ExportContext {
    ZipWriter zip;
    std::set<std::string> consumed; // base-archive paths we transformed (not copied verbatim)
    std::set<std::string> written;  // output paths already emitted (paths/collision shared across setups)
    ExportReport report;
};

// Every conversion failure is logged and counted; the export finishes so the log is complete, but reports !ok.
void Fail(ExportContext& ctx, const std::string& message) {
    SPDLOG_ERROR("[Unbound export] {}", message);
    ctx.report.failures++;
}

struct SceneRefs {
    std::string collisionPath; // base-archive path of the SetCollisionHeader target
    std::vector<std::string> roomFiles;
};

std::shared_ptr<Ship::ResourceManager> ResMgr() {
    return Ship::Context::GetInstance()->GetResourceManager();
}

template <typename T> std::shared_ptr<T> LoadAs(const std::string& path) {
    return std::static_pointer_cast<T>(ResMgr()->LoadResource(path));
}

std::string StripOtrPrefix(const char* s) {
    if (s == nullptr) {
        return "";
    }
    std::string p = s;
    const std::string prefix = "__OTR__";
    return p.rfind(prefix, 0) == 0 ? p.substr(prefix.size()) : p;
}

std::string Leaf(const std::string& path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string Hex(uint32_t v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%X", v);
    return buf;
}

// Message ids as SPEC.md §5 writes them: "0x0F12".
std::string MessageKey(uint16_t id) {
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%04X", id);
    return buf;
}

json Vec(const Vec3s& v) {
    return json::array({ v.x, v.y, v.z });
}

json Vec(const Vec3i& v) {
    return json::array({ v.x, v.y, v.z });
}

// SOH [Unbound] world positions are f32; emit integers when integral so converted vanilla data stays tidy.
json Num(f32 v) {
    if (v == (f32)(int64_t)v) {
        return json((int64_t)v);
    }
    return json(v);
}

json Vec(const Vec3f& v) {
    return json::array({ Num(v.x), Num(v.y), Num(v.z) });
}

json Rgb(const u8* c) {
    return json::array({ c[0], c[1], c[2] });
}

json Rgb(const s8* c) {
    return json::array({ c[0], c[1], c[2] });
}

std::string Key(size_t i) {
    return std::to_string(i);
}

// ---------------------------------------------------------------------------------------------
// Collision -> collision.json + collision.bin
// ---------------------------------------------------------------------------------------------
void PutS16(std::vector<uint8_t>& out, int16_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
}

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        out.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
    }
}

void PutF32(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    PutU32(out, bits);
}

// collision.bin v3 (SPEC.md §4.4.1): s32 vertices, 28-byte polys with s32 dist. v1 and v2 stay readable.
std::vector<uint8_t> BuildCollisionBin(const SOH::CollisionHeader& col) {
    std::vector<uint8_t> bin;
    const auto& d = col.collisionHeaderData;
    bin.reserve(d.numVertices * 12 + d.numPolygons * 28);
    for (uint32_t i = 0; i < d.numVertices; i++) {
        PutU32(bin, (uint32_t)d.vtxList[i].x);
        PutU32(bin, (uint32_t)d.vtxList[i].y);
        PutU32(bin, (uint32_t)d.vtxList[i].z);
    }
    for (uint32_t i = 0; i < d.numPolygons; i++) {
        const auto& p = d.polyList[i];
        PutS16(bin, (int16_t)p.type);
        PutS16(bin, 0); // pad
        PutU32(bin, p.flags_vIA);
        PutU32(bin, p.flags_vIB);
        PutU32(bin, p.vIC);
        PutS16(bin, p.normal.x);
        PutS16(bin, p.normal.y);
        PutS16(bin, p.normal.z);
        PutS16(bin, 0); // pad
        PutU32(bin, (uint32_t)p.dist);
    }
    return bin;
}

json SurfaceTypeJson(const SurfaceType& t) {
    return { { K::kCamera, t.camera },
             { K::kExit, t.exit },
             { K::kFloorType, t.floorType },
             { K::kWallFlags, t.wallFlags },
             { K::kWallType, t.wallType },
             { K::kFloorProperty, t.floorProperty },
             { K::kIsSoft, t.isSoft },
             { K::kIsHorseBlocked, t.isHorseBlocked },
             { K::kMaterial, t.material },
             { K::kFloorEffect, t.floorEffect },
             { K::kLightSetting, t.lightSetting },
             { K::kEcho, t.echo },
             { K::kCanHookshot, t.canHookshot },
             { K::kConveyorSpeed, t.conveyorSpeed },
             { K::kConveyorDirection, t.conveyorDirection },
             { K::kIsWallDamage, t.isWallDamage } };
}
json BuildCollisionJson(const SOH::CollisionHeader& col, const std::string& binPath) {
    const auto& d = col.collisionHeaderData;
    json doc;
    doc[K::kSchema] = K::kCollisionV3;
    doc[K::kBounds] = { { K::kMin, Vec(d.minBounds) }, { K::kMax, Vec(d.maxBounds) } };
    doc[K::kBulk] = { { K::kFile, binPath }, { K::kVertices, d.numVertices }, { K::kPolys, d.numPolygons } };

    json surfaces = json::object();
    for (size_t i = 0; i < col.surfaceTypes.size(); i++) {
        surfaces[Key(i)] = SurfaceTypeJson(col.surfaceTypes[i]);
    }
    doc[K::kSurfaceTypes] = surfaces;

    json cameras = json::object();
    for (size_t i = 0; i < col.camData.size(); i++) {
        json cam = { { K::kSType, col.camData[i].cameraSType }, { K::kCount, col.camData[i].numCameras } };
        int32_t idx = i < col.camPosDataIndices.size() ? col.camPosDataIndices[i] : -1;
        if (col.camPosCount > 0 && idx >= 0) {
            cam[K::kPositionIndex] = idx;
        } else {
            cam[K::kPositionIndex] = nullptr;
        }
        cameras[Key(i)] = cam;
    }
    doc[K::kCameras] = cameras;

    json positions = json::object();
    for (size_t i = 0; i < col.camPosData.size(); i++) {
        positions[Key(i)] = Vec(col.camPosData[i]);
    }
    doc[K::kCameraPositions] = positions;

    json water = json::object();
    for (size_t i = 0; i < col.waterBoxes.size(); i++) {
        const auto& w = col.waterBoxes[i];
        water[Key(i)] = { { K::kXMin, Num(w.xMin) },
                          { K::kYSurface, Num(w.ySurface) },
                          { K::kZMin, Num(w.zMin) },
                          { K::kXLength, Num(w.xLength) },
                          { K::kZLength, Num(w.zLength) },
                          { K::kCamera, w.camera },
                          { K::kLightSetting, w.lightSetting },
                          { K::kRoom, w.room },
                          { K::kNotSwimmable, w.notSwimmable } };
    }
    doc[K::kWaterBoxes] = water;
    return doc;
}

// Returns the output path of collision.json (empty on failure).
std::string ConvertCollision(ExportContext& ctx, const std::string& basePath, const std::string& sceneDir) {
    std::string jsonPath = sceneDir + "/collision.json";
    if (ctx.written.contains(jsonPath)) {
        return jsonPath;
    }
    auto col = LoadAs<SOH::CollisionHeader>(basePath);
    if (col == nullptr) {
        Fail(ctx, "collision " + basePath + " failed to load");
        return "";
    }
    std::string binPath = sceneDir + "/collision.bin";
    auto bin = BuildCollisionBin(*col);
    ctx.zip.Add(binPath, bin.data(), bin.size());
    ctx.zip.Add(jsonPath, BuildCollisionJson(*col, binPath).dump(2));
    ctx.written.insert(jsonPath);
    ctx.consumed.insert(basePath);
    return jsonPath;
}

// ---------------------------------------------------------------------------------------------
// Paths -> scenes/<dir>/paths/<leaf>.json
// ---------------------------------------------------------------------------------------------
std::string ConvertPaths(ExportContext& ctx, const std::string& basePath, const std::string& sceneDir) {
    std::string outPath = sceneDir + "/paths/" + Leaf(basePath) + ".json";
    if (ctx.written.contains(outPath)) {
        return outPath;
    }
    auto res = LoadAs<SOH::Path>(basePath);
    if (res == nullptr) {
        Fail(ctx, "path " + basePath + " failed to load");
        return "";
    }
    json doc;
    doc[K::kSchema] = K::kPathsV1;
    json paths = json::object();
    for (size_t i = 0; i < res->paths.size(); i++) {
        json points = json::array();
        for (const auto& p : res->paths[i]) {
            points.push_back(Vec(p));
        }
        paths[Key(i)] = { { K::kPoints, points } };
    }
    doc[K::kPaths] = paths;
    ctx.zip.Add(outPath, doc.dump(2));
    ctx.written.insert(outPath);
    ctx.consumed.insert(basePath);
    return outPath;
}

// ---------------------------------------------------------------------------------------------
// Scene commands -> one setup object
// ---------------------------------------------------------------------------------------------
json ActorJson(const SOH::ActorEntry& a) {
    return { { K::kId, a.id }, { K::kPos, Vec(a.pos) }, { K::kRot, Vec(a.rot) }, { K::kParams, a.params } };
}

json MeshJson(const SOH::SetMesh& mesh) {
    json m;
    uint8_t type = mesh.meshHeader.base.type;
    m[K::kType] = type;
    if (type == 0) {
        json entries = json::object();
        for (size_t i = 0; i < mesh.dlists.size(); i++) {
            std::string opa = StripOtrPrefix((const char*)mesh.dlists[i].opa);
            std::string xlu = StripOtrPrefix((const char*)mesh.dlists[i].xlu);
            entries[Key(i)] = { { K::kOpa, opa.empty() ? json(nullptr) : json(opa) },
                                { K::kXlu, xlu.empty() ? json(nullptr) : json(xlu) } };
        }
        m[K::kEntries] = entries;
    } else if (type == 2) {
        json entries = json::object();
        for (size_t i = 0; i < mesh.dlists2.size(); i++) {
            const auto& d = mesh.dlists2[i];
            std::string opa = StripOtrPrefix((const char*)d.opa);
            std::string xlu = StripOtrPrefix((const char*)d.xlu);
            entries[Key(i)] = { { K::kPos, Vec(d.pos) },
                                { K::kRadius, Num(d.unk_06) },
                                { K::kOpa, opa.empty() ? json(nullptr) : json(opa) },
                                { K::kXlu, xlu.empty() ? json(nullptr) : json(xlu) } };
        }
        m[K::kEntries] = entries;
    } else if (type == 1) {
        const auto& p1 = mesh.meshHeader.polygon1;
        m[K::kFormat] = p1.format;
        std::string opa = mesh.dlists.empty() ? "" : StripOtrPrefix((const char*)mesh.dlists[0].opa);
        std::string xlu = mesh.dlists.empty() ? "" : StripOtrPrefix((const char*)mesh.dlists[0].xlu);
        m[K::kOpa] = opa.empty() ? json(nullptr) : json(opa);
        m[K::kXlu] = xlu.empty() ? json(nullptr) : json(xlu);
        auto imageJson = [](const SOH::BgImage& img) {
            return json{ { K::kUnk00, img.unk_00 },
                         { K::kId, img.id },
                         { K::kSource, StripOtrPrefix((const char*)img.source) },
                         { K::kUnk0C, img.unk_0C },
                         { K::kTlut, img.tlut },
                         { K::kWidth, img.width },
                         { K::kHeight, img.height },
                         { K::kFmt, img.fmt },
                         { K::kSiz, img.siz },
                         { K::kMode0, img.mode0 },
                         { K::kTlutCount, img.tlutCount } };
        };
        if (p1.format == 1) {
            SOH::BgImage single{};
            single.source = p1.single.source;
            single.unk_0C = p1.single.unk_0C;
            single.tlut = (u32)(uintptr_t)p1.single.tlut;
            single.width = p1.single.width;
            single.height = p1.single.height;
            single.fmt = p1.single.fmt;
            single.siz = p1.single.siz;
            single.mode0 = p1.single.mode0;
            single.tlutCount = p1.single.tlutCount;
            m[K::kImage] = imageJson(single);
        } else {
            json images = json::object();
            for (size_t i = 0; i < mesh.images.size(); i++) {
                images[Key(i)] = imageJson(mesh.images[i]);
            }
            m[K::kImages] = images;
        }
    }
    return m;
}

json LightJson(const SOH::LightInfo& l) {
    json j;
    j[K::kType] = l.type;
    if (l.type == 1) { // LIGHT_DIRECTIONAL
        j[K::kDir] = json::array({ l.params.dir.x, l.params.dir.y, l.params.dir.z });
        j[K::kColor] = Rgb(l.params.dir.color);
    } else {
        j[K::kPos] = json::array({ Num(l.params.point.x), Num(l.params.point.y), Num(l.params.point.z) });
        j[K::kColor] = Rgb(l.params.point.color);
        j[K::kGlow] = l.params.point.drawGlow;
        j[K::kRadius] = l.params.point.radius;
    }
    return j;
}

// Positional list (SPEC.md §2): "0", "1", ... in engine order.
template <typename T, typename F> json PositionalList(const std::vector<T>& items, F toJson) {
    json list = json::object();
    for (size_t i = 0; i < items.size(); i++) {
        list[Key(i)] = toJson(items[i]);
    }
    return list;
}

template <typename T> const T& As(const std::shared_ptr<SOH::ISceneCommand>& cmd) {
    return *static_cast<const T*>(cmd.get());
}

// One emitter per scene command (the loader's builders in UnboundSceneFactory.cpp are their inverse).
json SpawnsJson(const SOH::SetStartPositionList& c) {
    return PositionalList(c.startPositions, [](const auto& a) { return ActorJson(a); });
}

json ActorsJson(const SOH::SetActorList& c) {
    return PositionalList(c.actorList, [](const auto& a) { return ActorJson(a); });
}

json WindJson(const SOH::SetWindSettings& c) {
    return { { K::kWest, c.settings.windWest },
             { K::kVertical, c.settings.windVertical },
             { K::kSouth, c.settings.windSouth },
             { K::kSpeed, c.settings.windSpeed } };
}

json EntrancesJson(const SOH::SetEntranceList& c) {
    return PositionalList(c.entrances, [](const auto& e) {
        return json{ { K::kSpawn, e.spawn }, { K::kRoom, e.room } };
    });
}

json SpecialObjectsJson(const SOH::SetSpecialObjects& c) {
    return { { K::kElfMessage, c.specialObjects.elfMessage }, { K::kGlobalObject, c.specialObjects.globalObject } };
}

json BehaviorJson(const SOH::SetRoomBehavior& c) {
    return { { K::kGameplayFlags, c.roomBehavior.gameplayFlags },
             { K::kGameplayFlags2, c.roomBehavior.gameplayFlags2 } };
}

json ObjectsJson(const SOH::SetObjectList& c) {
    return PositionalList(c.objects, [](const auto& id) { return json(id); });
}

json LightsJson(const SOH::SetLightList& c) {
    return PositionalList(c.lightList, [](const auto& l) { return LightJson(l); });
}

json PathsJson(ExportContext& ctx, const SOH::SetPathways& c, const std::string& sceneDir) {
    json files = json::array();
    for (const auto& f : c.pathFileNames) {
        std::string out = ConvertPaths(ctx, f, sceneDir);
        if (!out.empty()) {
            files.push_back(out);
        }
    }
    return files;
}

json TransitionActorsJson(const SOH::SetTransitionActorList& c) {
    return PositionalList(c.transitionActorList, [](const auto& t) {
        json front = { { K::kRoom, t.sides[0].room }, { K::kEffects, t.sides[0].effects } };
        json back = { { K::kRoom, t.sides[1].room }, { K::kEffects, t.sides[1].effects } };
        return json{ { K::kId, t.id },         { K::kPos, Vec(t.pos) }, { K::kRotY, t.rotY },
                     { K::kParams, t.params }, { K::kFront, front },    { K::kBack, back } };
    });
}

// fogNear is the vanilla packed word: blend rate in the high bits, near distance in the low ten.
json LightingJson(const SOH::SetLightingSettings& c) {
    return PositionalList(c.settings, [](const auto& s) {
        return json{ { K::kAmbient, Rgb(s.ambientColor) },
                     { K::kLight1Dir, Rgb(s.light1Dir) },
                     { K::kLight1Color, Rgb(s.light1Color) },
                     { K::kLight2Dir, Rgb(s.light2Dir) },
                     { K::kLight2Color, Rgb(s.light2Color) },
                     { K::kFogColor, Rgb(s.fogColor) },
                     { K::kFogNear, s.fogNear & 0x3FF },
                     { K::kFogBlendRate, ((u16)s.fogNear >> 10) & 0x3F },
                     { K::kFogFar, s.fogFar } };
    });
}

json TimeJson(const SOH::SetTimeSettings& c) {
    return { { K::kHour, c.settings.hour },
             { K::kMinute, c.settings.minute },
             { K::kIncrement, c.settings.timeIncrement } };
}

json SkyboxJson(const SOH::SetSkyboxSettings& c) {
    return { { K::kId, c.settings.skyboxId },
             { K::kWeather, c.settings.weather },
             { K::kIndoors, c.settings.indoors },
             { K::kUnk, c.settings.unk } };
}

json SkyboxModifierJson(const SOH::SetSkyboxModifier& c) {
    return { { K::kSkyboxDisabled, c.modifier.skyboxDisabled }, { K::kSunMoonDisabled, c.modifier.sunMoonDisabled } };
}

json ExitsJson(const SOH::SetExitList& c) {
    return PositionalList(c.exits, [](const auto& e) { return json(e); });
}

json SoundJson(const SOH::SetSoundSettings& c) {
    return { { K::kSeq, c.settings.seqId },
             { K::kNatureAmbience, c.settings.natureAmbienceId },
             { K::kReverb, c.settings.reverb } };
}

json CameraSettingsJson(const SOH::SetCameraSettings& c) {
    return { { K::kCameraMovement, c.settings.cameraMovement }, { K::kWorldMapArea, c.settings.worldMapArea } };
}

// Alternate headers become sibling setups; their header resources are consumed, not copied.
std::vector<std::shared_ptr<SOH::Scene>> AlternateHeaders(ExportContext& ctx, const SOH::SetAlternateHeaders& c) {
    for (const auto& name : c.headerFileNames) {
        ctx.consumed.insert(name);
    }
    return c.headers;
}

// Fills `setup` from one header's commands. Cross-setup references (rooms, collision) go to `refs`;
// alternate headers are returned for the caller to recurse.
std::vector<std::shared_ptr<SOH::Scene>> BuildSetup(ExportContext& ctx, json& setup, SOH::Scene& scene,
                                                    const std::string& sceneDir, SceneRefs& refs) {
    using SOH::SceneCommandID;
    std::vector<std::shared_ptr<SOH::Scene>> alternates;

    for (auto& cmd : scene.commands) {
        if (cmd == nullptr) {
            continue;
        }
        switch (cmd->cmdId) {
            case SceneCommandID::SetStartPositionList:
                setup[K::kSpawns] = SpawnsJson(As<SOH::SetStartPositionList>(cmd));
                break;
            case SceneCommandID::SetActorList:
                setup[K::kActors] = ActorsJson(As<SOH::SetActorList>(cmd));
                break;
            case SceneCommandID::SetCollisionHeader:
                refs.collisionPath = As<SOH::SetCollisionHeader>(cmd).fileName;
                break;
            case SceneCommandID::SetRoomList:
                refs.roomFiles = As<SOH::SetRoomList>(cmd).fileNames;
                break;
            case SceneCommandID::SetWind:
                setup[K::kWind] = WindJson(As<SOH::SetWindSettings>(cmd));
                break;
            case SceneCommandID::SetEntranceList:
                setup[K::kEntrances] = EntrancesJson(As<SOH::SetEntranceList>(cmd));
                break;
            case SceneCommandID::SetSpecialObjects:
                setup[K::kSpecialObjects] = SpecialObjectsJson(As<SOH::SetSpecialObjects>(cmd));
                break;
            case SceneCommandID::SetRoomBehavior:
                setup[K::kBehavior] = BehaviorJson(As<SOH::SetRoomBehavior>(cmd));
                break;
            case SceneCommandID::SetMesh:
                setup[K::kMesh] = MeshJson(As<SOH::SetMesh>(cmd));
                break;
            case SceneCommandID::SetObjectList:
                setup[K::kObjects] = ObjectsJson(As<SOH::SetObjectList>(cmd));
                break;
            case SceneCommandID::SetLightList:
                setup[K::kLights] = LightsJson(As<SOH::SetLightList>(cmd));
                break;
            case SceneCommandID::SetPathways:
                setup[K::kPaths] = PathsJson(ctx, As<SOH::SetPathways>(cmd), sceneDir);
                break;
            case SceneCommandID::SetTransitionActorList:
                setup[K::kTransitionActors] = TransitionActorsJson(As<SOH::SetTransitionActorList>(cmd));
                break;
            case SceneCommandID::SetLightingSettings:
                setup[K::kLighting] = LightingJson(As<SOH::SetLightingSettings>(cmd));
                break;
            case SceneCommandID::SetTimeSettings:
                setup[K::kTime] = TimeJson(As<SOH::SetTimeSettings>(cmd));
                break;
            case SceneCommandID::SetSkyboxSettings:
                setup[K::kSkybox] = SkyboxJson(As<SOH::SetSkyboxSettings>(cmd));
                break;
            case SceneCommandID::SetSkyboxModifier:
                setup[K::kSkyboxModifier] = SkyboxModifierJson(As<SOH::SetSkyboxModifier>(cmd));
                break;
            case SceneCommandID::SetExitList:
                setup[K::kExits] = ExitsJson(As<SOH::SetExitList>(cmd));
                break;
            case SceneCommandID::SetSoundSettings:
                setup[K::kSound] = SoundJson(As<SOH::SetSoundSettings>(cmd));
                break;
            case SceneCommandID::SetEchoSettings:
                setup[K::kEcho] = As<SOH::SetEchoSettings>(cmd).settings.echo;
                break;
            case SceneCommandID::SetCutscenes:
                setup[K::kCutscene] = As<SOH::SetCutscenes>(cmd).fileName; // copied verbatim under its path
                break;
            case SceneCommandID::SetAlternateHeaders:
                alternates = AlternateHeaders(ctx, As<SOH::SetAlternateHeaders>(cmd));
                break;
            case SceneCommandID::SetCameraSettings:
                setup[K::kCameraSettings] = CameraSettingsJson(As<SOH::SetCameraSettings>(cmd));
                break;
            default:
                break; // CsCamera, EndMarker, Unused: nothing to carry
        }
    }
    return alternates;
}

// Builds K::kSetups: primary header + each alternate, recursing into alternates' own commands.
json BuildSetups(ExportContext& ctx, SOH::Scene& primary, const std::string& sceneDir, SceneRefs& refs) {
    json setups = json::object();
    json first = json::object();
    auto alternates = BuildSetup(ctx, first, primary, sceneDir, refs);
    setups["0"] = first;
    for (size_t i = 0; i < alternates.size(); i++) {
        if (alternates[i] == nullptr) {
            continue;
        }
        json alt = json::object();
        SceneRefs altRefs;
        BuildSetup(ctx, alt, *alternates[i], sceneDir, altRefs);
        if (!altRefs.collisionPath.empty() && altRefs.collisionPath != refs.collisionPath) {
            SPDLOG_WARN("[Unbound export] {} setup {} uses a different collision header; not representable", sceneDir,
                        i + 1);
        }
        setups[Key(i + 1)] = alt;
    }
    return setups;
}

bool ConvertRoom(ExportContext& ctx, const std::string& basePath, const std::string& sceneDir, size_t roomIndex) {
    auto room = LoadAs<SOH::Scene>(basePath);
    if (room == nullptr) {
        Fail(ctx, "room " + basePath + " failed to load");
        return false;
    }
    json doc;
    doc[K::kSchema] = K::kRoomV1;
    SceneRefs unused;
    doc[K::kSetups] = BuildSetups(ctx, *room, sceneDir, unused);
    ctx.zip.Add(sceneDir + "/rooms/" + Key(roomIndex) + ".json", doc.dump(2));
    ctx.consumed.insert(basePath);
    ctx.report.rooms++;
    return true;
}

bool ConvertScene(ExportContext& ctx, const std::string& basePath, const std::string& sceneDir) {
    auto scene = LoadAs<SOH::Scene>(basePath);
    if (scene == nullptr) {
        Fail(ctx, "scene " + basePath + " failed to load");
        return false;
    }
    json doc;
    doc[K::kSchema] = K::kSceneV1;
    SceneRefs refs;
    json setups = BuildSetups(ctx, *scene, sceneDir, refs);

    if (!refs.collisionPath.empty()) {
        std::string collision = ConvertCollision(ctx, refs.collisionPath, sceneDir);
        if (!collision.empty()) {
            doc[K::kCollision] = collision; // absent = no collision (SPEC.md §4.2), never ""
        }
    }
    // rooms is positional (SPEC.md §2): one failed room would leave a hole, so the scene is not written at all.
    json rooms = json::object();
    for (size_t i = 0; i < refs.roomFiles.size(); i++) {
        if (!ConvertRoom(ctx, refs.roomFiles[i], sceneDir, i)) {
            Fail(ctx, "scene " + basePath + " not written: room " + std::to_string(i) + " failed");
            return false;
        }
        rooms[Key(i)] = sceneDir + "/rooms/" + Key(i) + ".json";
    }
    doc[K::kRooms] = rooms;
    doc[K::kSetups] = setups;

    ctx.zip.Add(sceneDir + "/scene.json", doc.dump(2));
    ctx.consumed.insert(basePath);
    ctx.report.scenes++;
    return true;
}

std::string SceneDirName(const std::string& fileLeaf, bool mq) {
    std::string name = fileLeaf;
    const std::string suffix = "_scene";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        name.resize(name.size() - suffix.size());
    }
    return "scenes/" + name + (mq ? "_mq" : "");
}

void ConvertAllScenes(ExportContext& ctx) {
    auto archives = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    for (const auto& entry : SceneDB::Instance->Entries()) {
        if (!entry.valid || entry.isCustom) {
            continue;
        }
        const std::string& f = entry.sceneFileName;
        std::string shared = "scenes/shared/" + f + "/" + f;
        std::string nonmq = "scenes/nonmq/" + f + "/" + f;
        std::string mq = "scenes/mq/" + f + "/" + f;
        if (archives->HasFile(shared)) {
            if (archives->HasFile(nonmq)) {
                SPDLOG_WARN("[Unbound export] {} exists as both shared and nonmq; converting shared", f);
            }
            ConvertScene(ctx, shared, SceneDirName(f, false));
        } else if (archives->HasFile(nonmq)) {
            ConvertScene(ctx, nonmq, SceneDirName(f, false));
        }
        if (archives->HasFile(mq)) {
            ConvertScene(ctx, mq, SceneDirName(f, true));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Messages -> text/<lang>/messages.json
// ---------------------------------------------------------------------------------------------
std::string BytesToJsonText(const std::string& bytes) {
    std::string out;
    out.reserve(bytes.size() + 16);
    for (unsigned char b : bytes) {
        if (b < 0x80) {
            out.push_back((char)b);
        } else {
            out.push_back((char)(0xC0 | (b >> 6)));
            out.push_back((char)(0x80 | (b & 0x3F)));
        }
    }
    return out;
}

void ConvertMessages(ExportContext& ctx) {
    struct Lang {
        const char* name;
        std::vector<const char*> basePaths;
    };
    const Lang langs[] = {
        { "eng",
          { "text/nes_message_data_static/nes_message_data_static",
            "text/nes_message_data_static/ntsc_nes_message_data_static" } },
        { "ger", { "text/ger_message_data_static/ger_message_data_static" } },
        { "fra", { "text/fra_message_data_static/fra_message_data_static" } },
        { "jpn", { "text/jpn_message_data_static/jpn_message_data_static" } },
        { "staff", { "text/staff_message_data_static/staff_message_data_static" } },
    };
    auto archives = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    for (const auto& lang : langs) {
        for (const char* base : lang.basePaths) {
            if (!archives->HasFile(base)) {
                continue;
            }
            auto text = LoadAs<SOH::Text>(base);
            if (text == nullptr) {
                continue;
            }
            json doc;
            doc[K::kSchema] = K::kTextV1;
            json messages = json::object();
            for (const auto& m : text->messages) {
                if (m.id == 0xFFFF) {
                    continue;
                }
                messages[MessageKey(m.id)] = { { K::kBox, m.textboxType },
                                               { K::kYPos, m.textboxYPos },
                                               { K::kText, BytesToJsonText(m.msg) } };
                ctx.report.messages++;
            }
            doc[K::kMessages] = messages;
            ctx.zip.Add(std::string(K::kMessagesPathPrefix) + lang.name + K::kMessagesPathSuffix, doc.dump(2));
            ctx.consumed.insert(base);
            break; // first available base wins for this language
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Verbatim copy of everything else in the base archive
// ---------------------------------------------------------------------------------------------
std::shared_ptr<Ship::Archive> FindBaseArchive() {
    auto archives = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    for (const auto& entry : SceneDB::Instance->Entries()) {
        if (!entry.valid || entry.isCustom) {
            continue;
        }
        std::string shared = "scenes/shared/" + entry.sceneFileName + "/" + entry.sceneFileName;
        if (archives->HasFile(shared)) {
            return archives->GetArchiveFromFile(shared);
        }
    }
    return nullptr;
}

void CopyUntouchedFiles(ExportContext& ctx, std::shared_ptr<Ship::Archive> base) {
    auto files = base->ListFiles();
    std::vector<std::string> names;
    names.reserve(files->size());
    for (const auto& [hash, name] : *files) {
        if (!ctx.consumed.contains(name)) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        auto file = base->LoadFile(name);
        if (file == nullptr || file->Buffer == nullptr) {
            // O2rArchive::LoadFile returns null for zero-length entries; keep them as empty entries
            ctx.zip.Add(name, nullptr, 0);
        } else {
            ctx.zip.Add(name, (const uint8_t*)file->Buffer->data(), file->Buffer->size());
        }
        ctx.report.copied++;
    }
}

// Provenance of a conversion: the converter build and every mounted ROM archive (SPEC.md §6 `source`).
// A base whose provenance differs from the running game is stale and is converted again.
json CurrentProvenance() {
    auto archives = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    std::vector<uint32_t> versions = archives->GetGameVersions();
    std::sort(versions.begin(), versions.end());
    versions.erase(std::unique(versions.begin(), versions.end()), versions.end());
    json source = json::object();
    if (!versions.empty()) {
        source[K::kRomHash] = Hex(versions.front());
    }
    json hashes = json::array();
    for (uint32_t v : versions) {
        hashes.push_back(Hex(v));
    }
    source[K::kRomHashes] = hashes;
    source[K::kConverter] = std::string("soh ") + gBuildVersion;
    return source;
}

void WriteManifest(ExportContext& ctx) {
    auto archives = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    json doc;
    doc[K::kFormatName] = "unbound";
    doc[K::kFormatVersion] = K::kCurrentFormatVersion;
    doc[K::kGame] = "oot";
    doc[K::kSourceInfo] = CurrentProvenance();
    doc[K::kFeatures] = json::array({ "scenes", K::kCollision, K::kText, K::kPaths });
    doc[K::kRequires] = json::object({ { K::kFormatVersion, K::kCurrentFormatVersion } });
    ctx.zip.Add(K::kManifestPath, doc.dump(2));
}

} // namespace

ExportReport ExportArchive(const std::string& outPath) {
    ExportContext ctx;
    auto base = FindBaseArchive();
    if (base == nullptr) {
        ctx.report.error = "no vanilla scene archive is mounted";
        return ctx.report;
    }
    if (!ctx.zip.Open(outPath)) {
        ctx.report.error = "cannot open " + outPath + " for writing";
        return ctx.report;
    }
    SPDLOG_INFO("[Unbound export] base archive {} -> {}", base->GetPath(), outPath);

    ConvertAllScenes(ctx);
    ConvertMessages(ctx);
    CopyUntouchedFiles(ctx, base);
    WriteManifest(ctx);

    if (!ctx.zip.Close()) {
        ctx.report.error = "failed to finish writing " + outPath + ": " + ctx.zip.Error();
        return ctx.report;
    }
    SPDLOG_INFO("[Unbound export] done: {} scenes, {} rooms, {} messages, {} files copied, {} entries, {} failure(s)",
                ctx.report.scenes, ctx.report.rooms, ctx.report.messages, ctx.report.copied, ctx.zip.Count(),
                ctx.report.failures);
    if (ctx.report.failures > 0) {
        ctx.report.error = std::to_string(ctx.report.failures) + " resource(s) failed to convert; see the log";
        return ctx.report;
    }
    ctx.report.ok = true;
    return ctx.report;
}

// ---- boot-time base archive --------------------------------------------------------------------

namespace {

// The `source` object of the manifest in the topmost mounted archive that has one.
json MountedManifestSource() {
    auto file = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->LoadFile(K::kManifestPath);
    if (file == nullptr || file->Buffer == nullptr) {
        return json::object();
    }
    json doc = json::parse(file->Buffer->begin(), file->Buffer->end(), nullptr, false, true);
    return Sub(doc, K::kSourceInfo);
}

bool ProvenanceMatches(const json& source, const json& current) {
    if (PathField(source, K::kConverter) != PathField(current, K::kConverter)) {
        return false;
    }
    return SubArray(source, K::kRomHashes) == SubArray(current, K::kRomHashes);
}

} // namespace

bool EnsureBaseArchive(const std::string& gameArchiveDir) {
    auto archives = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    if (archives->GetGameVersions().empty()) {
        return false; // no ROM archive: nothing to convert
    }
    std::string path = (std::filesystem::path(gameArchiveDir) / kBaseArchiveName).string();
    json current = CurrentProvenance();

    if (std::filesystem::exists(path)) {
        if (archives->AddArchive(path) != nullptr) {
            if (ProvenanceMatches(MountedManifestSource(), current)) {
                SPDLOG_INFO("[Unbound] {} is current; mounted", path);
                return true;
            }
            archives->RemoveArchive(path);
        }
        SPDLOG_INFO("[Unbound] {} was made by another build or from other ROM archives; converting again", path);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    } else {
        SPDLOG_INFO("[Unbound] {} not found; converting the vanilla archive", path);
    }

    ExportReport report = ExportArchive(path);
    if (!report.ok) {
        SPDLOG_ERROR("[Unbound] base archive not written: {}; vanilla scenes stay in vanilla format", report.error);
        return false;
    }
    bool mounted = archives->AddArchive(path) != nullptr;
    SPDLOG_INFO("[Unbound] {} converted and {}", path, mounted ? "mounted" : "NOT mounted");
    return mounted;
}

} // namespace SOH::Unbound

extern "C" int Unbound_Export(const char* outPath) {
    auto report = SOH::Unbound::ExportArchive(outPath != nullptr ? outPath : "oot-unbound.o2r");
    if (!report.ok) {
        SPDLOG_ERROR("[Unbound export] failed: {}", report.error);
        return 1;
    }
    return 0;
}
