// SOH [Unbound] Converts the mounted vanilla archive into the Unbound layout (unbound-docs/scene-format.md).
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
#include <fstream>
#include <set>
#include <unordered_map>

#include "soh/SceneDB.h"
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

namespace Unbound {
namespace {

// ---------------------------------------------------------------------------------------------
// Stored-entry zip writer. Deterministic (fixed 1980-01-01 timestamps), no compression, no ZIP64
// (the vanilla archive is ~38k entries / ~51 MB raw, well inside the classic limits).
// ---------------------------------------------------------------------------------------------
class ZipWriter {
  public:
    bool Open(const std::string& path) {
        mOut.open(path, std::ios::binary | std::ios::trunc);
        return mOut.good();
    }

    void Add(const std::string& name, const uint8_t* data, size_t size) {
        Entry e;
        e.name = name;
        e.size = (uint32_t)size;
        e.crc = Crc32(data, size);
        e.offset = (uint32_t)mOut.tellp();

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
    }

    void Add(const std::string& name, const std::string& text) {
        Add(name, (const uint8_t*)text.data(), text.size());
    }

    bool Close() {
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
        return mOut.good() || mOut.eof();
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

json Vec(const Vec3s& v) {
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

// collision.bin v2 (scene-format.md §2.3): f32 vertices, 28-byte polys with f32 dist. v1 stays readable.
std::vector<uint8_t> BuildCollisionBin(const SOH::CollisionHeader& col) {
    std::vector<uint8_t> bin;
    const auto& d = col.collisionHeaderData;
    bin.reserve(d.numVertices * 12 + d.numPolygons * 28);
    for (uint32_t i = 0; i < d.numVertices; i++) {
        PutF32(bin, d.vtxList[i].x);
        PutF32(bin, d.vtxList[i].y);
        PutF32(bin, d.vtxList[i].z);
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
        PutF32(bin, p.dist);
    }
    return bin;
}

json BuildCollisionJson(const SOH::CollisionHeader& col, const std::string& binPath) {
    const auto& d = col.collisionHeaderData;
    json doc;
    doc["$schema"] = "unbound/collision/2";
    doc["bounds"] = { { "min", Vec(d.minBounds) }, { "max", Vec(d.maxBounds) } };
    doc["bulk"] = { { "file", binPath }, { "vertices", d.numVertices }, { "polys", d.numPolygons } };

    json surfaces = json::object();
    for (size_t i = 0; i < col.surfaceTypes.size(); i++) {
        surfaces[Key(i)] = { { "data0", Hex(col.surfaceTypes[i].data[0]) },
                             { "data1", Hex(col.surfaceTypes[i].data[1]) } };
    }
    doc["surfaceTypes"] = surfaces;

    json cameras = json::object();
    for (size_t i = 0; i < col.camData.size(); i++) {
        json cam = { { "sType", col.camData[i].cameraSType }, { "count", col.camData[i].numCameras } };
        int32_t idx = i < col.camPosDataIndices.size() ? col.camPosDataIndices[i] : -1;
        if (col.camPosCount > 0 && idx >= 0) {
            cam["positionIndex"] = idx;
        } else {
            cam["positionIndex"] = nullptr;
        }
        cameras[Key(i)] = cam;
    }
    doc["cameras"] = cameras;

    json positions = json::object();
    for (size_t i = 0; i < col.camPosData.size(); i++) {
        positions[Key(i)] = Vec(col.camPosData[i]);
    }
    doc["cameraPositions"] = positions;

    json water = json::object();
    for (size_t i = 0; i < col.waterBoxes.size(); i++) {
        const auto& w = col.waterBoxes[i];
        water[Key(i)] = { { "xMin", Num(w.xMin) },       { "ySurface", Num(w.ySurface) },
                          { "zMin", Num(w.zMin) },       { "xLength", Num(w.xLength) },
                          { "zLength", Num(w.zLength) }, { "properties", Hex(w.properties) },
                          { "room", w.room } };
    }
    doc["waterBoxes"] = water;
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
        SPDLOG_ERROR("[Unbound export] collision {} failed to load", basePath);
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
        SPDLOG_ERROR("[Unbound export] path {} failed to load", basePath);
        return "";
    }
    json doc;
    doc["$schema"] = "unbound/paths/1";
    json paths = json::object();
    for (size_t i = 0; i < res->paths.size(); i++) {
        json points = json::array();
        for (const auto& p : res->paths[i]) {
            points.push_back(Vec(p));
        }
        paths[Key(i)] = { { "points", points } };
    }
    doc["paths"] = paths;
    ctx.zip.Add(outPath, doc.dump(2));
    ctx.written.insert(outPath);
    ctx.consumed.insert(basePath);
    return outPath;
}

// ---------------------------------------------------------------------------------------------
// Scene commands -> one setup object
// ---------------------------------------------------------------------------------------------
json ActorJson(const SOH::ActorEntry& a) {
    return { { "id", a.id }, { "pos", Vec(a.pos) }, { "rot", Vec(a.rot) }, { "params", a.params } };
}

json MeshJson(const SOH::SetMesh& mesh) {
    json m;
    uint8_t type = mesh.meshHeader.base.type;
    m["type"] = type;
    if (type == 0) {
        json entries = json::object();
        for (size_t i = 0; i < mesh.dlists.size(); i++) {
            std::string opa = StripOtrPrefix((const char*)mesh.dlists[i].opa);
            std::string xlu = StripOtrPrefix((const char*)mesh.dlists[i].xlu);
            entries[Key(i)] = { { "opa", opa.empty() ? json(nullptr) : json(opa) },
                                { "xlu", xlu.empty() ? json(nullptr) : json(xlu) } };
        }
        m["entries"] = entries;
    } else if (type == 2) {
        json entries = json::object();
        for (size_t i = 0; i < mesh.dlists2.size(); i++) {
            const auto& d = mesh.dlists2[i];
            std::string opa = StripOtrPrefix((const char*)d.opa);
            std::string xlu = StripOtrPrefix((const char*)d.xlu);
            entries[Key(i)] = { { "pos", Vec(d.pos) },
                                { "radius", Num(d.unk_06) },
                                { "opa", opa.empty() ? json(nullptr) : json(opa) },
                                { "xlu", xlu.empty() ? json(nullptr) : json(xlu) } };
        }
        m["entries"] = entries;
    } else if (type == 1) {
        const auto& p1 = mesh.meshHeader.polygon1;
        m["format"] = p1.format;
        std::string opa = mesh.dlists.empty() ? "" : StripOtrPrefix((const char*)mesh.dlists[0].opa);
        std::string xlu = mesh.dlists.empty() ? "" : StripOtrPrefix((const char*)mesh.dlists[0].xlu);
        m["opa"] = opa.empty() ? json(nullptr) : json(opa);
        m["xlu"] = xlu.empty() ? json(nullptr) : json(xlu);
        auto imageJson = [](const SOH::BgImage& img) {
            return json{ { "unk00", img.unk_00 },   { "id", img.id },
                         { "source", StripOtrPrefix((const char*)img.source) },
                         { "unk0C", img.unk_0C },   { "tlut", img.tlut },
                         { "width", img.width },   { "height", img.height },
                         { "fmt", img.fmt },       { "siz", img.siz },
                         { "mode0", img.mode0 },   { "tlutCount", img.tlutCount } };
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
            m["image"] = imageJson(single);
        } else {
            json images = json::object();
            for (size_t i = 0; i < mesh.images.size(); i++) {
                images[Key(i)] = imageJson(mesh.images[i]);
            }
            m["images"] = images;
        }
    }
    return m;
}

json LightJson(const SOH::LightInfo& l) {
    json j;
    j["type"] = l.type;
    if (l.type == 1) { // LIGHT_DIRECTIONAL
        j["dir"] = json::array({ l.params.dir.x, l.params.dir.y, l.params.dir.z });
        j["color"] = Rgb(l.params.dir.color);
    } else {
        j["pos"] = json::array({ Num(l.params.point.x), Num(l.params.point.y), Num(l.params.point.z) });
        j["color"] = Rgb(l.params.point.color);
        j["glow"] = l.params.point.drawGlow;
        j["radius"] = l.params.point.radius;
    }
    return j;
}

// Fills `setup` from one header's commands. Cross-setup references (rooms, collision) go to `refs`;
// alternate headers are returned for the caller to recurse.
std::vector<std::shared_ptr<SOH::Scene>> BuildSetup(ExportContext& ctx, json& setup, SOH::Scene& scene,
                                                    const std::string& sceneDir, SceneRefs& refs) {
    std::vector<std::shared_ptr<SOH::Scene>> alternates;

    for (auto& cmd : scene.commands) {
        if (cmd == nullptr) {
            continue;
        }
        switch (cmd->cmdId) {
            case SOH::SceneCommandID::SetStartPositionList: {
                auto* c = (SOH::SetStartPositionList*)cmd.get();
                json list = json::object();
                for (size_t i = 0; i < c->startPositions.size(); i++) {
                    list[Key(i)] = ActorJson(c->startPositions[i]);
                }
                setup["spawns"] = list;
                break;
            }
            case SOH::SceneCommandID::SetActorList: {
                auto* c = (SOH::SetActorList*)cmd.get();
                json list = json::object();
                for (size_t i = 0; i < c->actorList.size(); i++) {
                    list[Key(i)] = ActorJson(c->actorList[i]);
                }
                setup["actors"] = list;
                break;
            }
            case SOH::SceneCommandID::SetCollisionHeader: {
                auto* c = (SOH::SetCollisionHeader*)cmd.get();
                refs.collisionPath = c->fileName;
                break;
            }
            case SOH::SceneCommandID::SetRoomList: {
                auto* c = (SOH::SetRoomList*)cmd.get();
                refs.roomFiles = c->fileNames;
                break;
            }
            case SOH::SceneCommandID::SetWind: {
                auto* c = (SOH::SetWindSettings*)cmd.get();
                setup["wind"] = { { "west", c->settings.windWest },
                                  { "vertical", c->settings.windVertical },
                                  { "south", c->settings.windSouth },
                                  { "speed", c->settings.windSpeed } };
                break;
            }
            case SOH::SceneCommandID::SetEntranceList: {
                auto* c = (SOH::SetEntranceList*)cmd.get();
                json list = json::object();
                for (size_t i = 0; i < c->entrances.size(); i++) {
                    list[Key(i)] = { { "spawn", c->entrances[i].spawn }, { "room", c->entrances[i].room } };
                }
                setup["entrances"] = list;
                break;
            }
            case SOH::SceneCommandID::SetSpecialObjects: {
                auto* c = (SOH::SetSpecialObjects*)cmd.get();
                setup["specialObjects"] = { { "elfMessage", c->specialObjects.elfMessage },
                                            { "globalObject", c->specialObjects.globalObject } };
                break;
            }
            case SOH::SceneCommandID::SetRoomBehavior: {
                auto* c = (SOH::SetRoomBehavior*)cmd.get();
                setup["behavior"] = { { "gameplayFlags", c->roomBehavior.gameplayFlags },
                                      { "gameplayFlags2", c->roomBehavior.gameplayFlags2 } };
                break;
            }
            case SOH::SceneCommandID::SetMesh: {
                setup["mesh"] = MeshJson(*(SOH::SetMesh*)cmd.get());
                break;
            }
            case SOH::SceneCommandID::SetObjectList: {
                auto* c = (SOH::SetObjectList*)cmd.get();
                json list = json::object();
                for (size_t i = 0; i < c->objects.size(); i++) {
                    list[Key(i)] = c->objects[i];
                }
                setup["objects"] = list;
                break;
            }
            case SOH::SceneCommandID::SetLightList: {
                auto* c = (SOH::SetLightList*)cmd.get();
                json list = json::object();
                for (size_t i = 0; i < c->lightList.size(); i++) {
                    list[Key(i)] = LightJson(c->lightList[i]);
                }
                setup["lights"] = list;
                break;
            }
            case SOH::SceneCommandID::SetPathways: {
                auto* c = (SOH::SetPathways*)cmd.get();
                json files = json::array();
                for (const auto& f : c->pathFileNames) {
                    std::string out = ConvertPaths(ctx, f, sceneDir);
                    if (!out.empty()) {
                        files.push_back(out);
                    }
                }
                setup["paths"] = files;
                break;
            }
            case SOH::SceneCommandID::SetTransitionActorList: {
                auto* c = (SOH::SetTransitionActorList*)cmd.get();
                json list = json::object();
                for (size_t i = 0; i < c->transitionActorList.size(); i++) {
                    const auto& t = c->transitionActorList[i];
                    list[Key(i)] = { { "id", t.id },
                                     { "pos", Vec(t.pos) },
                                     { "rotY", t.rotY },
                                     { "params", t.params },
                                     { "front", { { "room", t.sides[0].room }, { "effects", t.sides[0].effects } } },
                                     { "back", { { "room", t.sides[1].room }, { "effects", t.sides[1].effects } } } };
                }
                setup["transitionActors"] = list;
                break;
            }
            case SOH::SceneCommandID::SetLightingSettings: {
                auto* c = (SOH::SetLightingSettings*)cmd.get();
                json list = json::object();
                for (size_t i = 0; i < c->settings.size(); i++) {
                    const auto& s = c->settings[i];
                    list[Key(i)] = { { "ambient", Rgb(s.ambientColor) },     { "light1Dir", Rgb(s.light1Dir) },
                                     { "light1Color", Rgb(s.light1Color) }, { "light2Dir", Rgb(s.light2Dir) },
                                     { "light2Color", Rgb(s.light2Color) }, { "fogColor", Rgb(s.fogColor) },
                                     { "fogNear", s.fogNear },              { "fogFar", s.fogFar } };
                }
                setup["lighting"] = list;
                break;
            }
            case SOH::SceneCommandID::SetTimeSettings: {
                auto* c = (SOH::SetTimeSettings*)cmd.get();
                setup["time"] = { { "hour", c->settings.hour },
                                  { "minute", c->settings.minute },
                                  { "increment", c->settings.timeIncrement } };
                break;
            }
            case SOH::SceneCommandID::SetSkyboxSettings: {
                auto* c = (SOH::SetSkyboxSettings*)cmd.get();
                setup["skybox"] = { { "id", c->settings.skyboxId },
                                    { "weather", c->settings.weather },
                                    { "indoors", c->settings.indoors },
                                    { "unk", c->settings.unk } };
                break;
            }
            case SOH::SceneCommandID::SetSkyboxModifier: {
                auto* c = (SOH::SetSkyboxModifier*)cmd.get();
                setup["skyboxModifier"] = { { "skyboxDisabled", c->modifier.skyboxDisabled },
                                            { "sunMoonDisabled", c->modifier.sunMoonDisabled } };
                break;
            }
            case SOH::SceneCommandID::SetExitList: {
                auto* c = (SOH::SetExitList*)cmd.get();
                json list = json::object();
                for (size_t i = 0; i < c->exits.size(); i++) {
                    list[Key(i)] = c->exits[i];
                }
                setup["exits"] = list;
                break;
            }
            case SOH::SceneCommandID::SetSoundSettings: {
                auto* c = (SOH::SetSoundSettings*)cmd.get();
                setup["sound"] = { { "seq", c->settings.seqId },
                                   { "natureAmbience", c->settings.natureAmbienceId },
                                   { "reverb", c->settings.reverb } };
                break;
            }
            case SOH::SceneCommandID::SetEchoSettings: {
                auto* c = (SOH::SetEchoSettings*)cmd.get();
                setup["echo"] = c->settings.echo;
                break;
            }
            case SOH::SceneCommandID::SetCutscenes: {
                auto* c = (SOH::SetCutscenes*)cmd.get();
                setup["cutscene"] = c->fileName; // copied verbatim under its original path
                break;
            }
            case SOH::SceneCommandID::SetAlternateHeaders: {
                auto* c = (SOH::SetAlternateHeaders*)cmd.get();
                alternates = c->headers;
                for (const auto& name : c->headerFileNames) {
                    ctx.consumed.insert(name);
                }
                break;
            }
            case SOH::SceneCommandID::SetCameraSettings: {
                auto* c = (SOH::SetCameraSettings*)cmd.get();
                setup["cameraSettings"] = { { "cameraMovement", c->settings.cameraMovement },
                                            { "worldMapArea", c->settings.worldMapArea } };
                break;
            }
            default:
                break; // CsCamera, EndMarker, Unused: nothing to carry
        }
    }
    return alternates;
}

// Builds "setups": primary header + each alternate, recursing into alternates' own commands.
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
            SPDLOG_WARN("[Unbound export] {} setup {} uses a different collision header; not representable",
                        sceneDir, i + 1);
        }
        setups[Key(i + 1)] = alt;
    }
    return setups;
}

bool ConvertRoom(ExportContext& ctx, const std::string& basePath, const std::string& sceneDir, size_t roomIndex) {
    auto room = LoadAs<SOH::Scene>(basePath);
    if (room == nullptr) {
        SPDLOG_ERROR("[Unbound export] room {} failed to load", basePath);
        return false;
    }
    json doc;
    doc["$schema"] = "unbound/room/1";
    SceneRefs unused;
    doc["setups"] = BuildSetups(ctx, *room, sceneDir, unused);
    ctx.zip.Add(sceneDir + "/rooms/" + Key(roomIndex) + ".json", doc.dump(2));
    ctx.consumed.insert(basePath);
    ctx.report.rooms++;
    return true;
}

bool ConvertScene(ExportContext& ctx, const std::string& basePath, const std::string& sceneDir) {
    auto scene = LoadAs<SOH::Scene>(basePath);
    if (scene == nullptr) {
        SPDLOG_ERROR("[Unbound export] scene {} failed to load", basePath);
        return false;
    }
    json doc;
    doc["$schema"] = "unbound/scene/1";
    SceneRefs refs;
    json setups = BuildSetups(ctx, *scene, sceneDir, refs);

    if (!refs.collisionPath.empty()) {
        doc["collision"] = ConvertCollision(ctx, refs.collisionPath, sceneDir);
    }
    json rooms = json::object();
    for (size_t i = 0; i < refs.roomFiles.size(); i++) {
        if (ConvertRoom(ctx, refs.roomFiles[i], sceneDir, i)) {
            rooms[Key(i)] = sceneDir + "/rooms/" + Key(i) + ".json";
        }
    }
    doc["rooms"] = rooms;
    doc["setups"] = setups;

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
            ConvertScene(ctx, shared, SceneDirName(f, false));
        }
        if (archives->HasFile(nonmq)) {
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
            doc["$schema"] = "unbound/text/1";
            doc["language"] = lang.name;
            json messages = json::object();
            for (const auto& m : text->messages) {
                if (m.id == 0xFFFF) {
                    continue;
                }
                messages[Hex(m.id)] = { { "box", m.textboxType }, { "ypos", m.textboxYPos },
                                        { "text", BytesToJsonText(m.msg) } };
                ctx.report.messages++;
            }
            doc["messages"] = messages;
            ctx.zip.Add(std::string("text/") + lang.name + "/messages.json", doc.dump(2));
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

void WriteManifest(ExportContext& ctx) {
    auto archives = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    json doc;
    doc["format"] = "unbound";
    doc["formatVersion"] = 1;
    doc["game"] = "oot";
    json source = json::object();
    auto versions = archives->GetGameVersions();
    if (!versions.empty()) {
        source["romHash"] = Hex(versions.front());
    }
    source["converter"] = "soh-unbound";
    doc["source"] = source;
    doc["features"] = json::array({ "scenes", "collision", "text", "paths" });
    ctx.zip.Add("unbound.json", doc.dump(2));
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
        ctx.report.error = "failed to finish writing " + outPath;
        return ctx.report;
    }
    ctx.report.ok = true;
    SPDLOG_INFO("[Unbound export] done: {} scenes, {} rooms, {} messages, {} files copied, {} entries",
                ctx.report.scenes, ctx.report.rooms, ctx.report.messages, ctx.report.copied, ctx.zip.Count());
    return ctx.report;
}

} // namespace Unbound

extern "C" int Unbound_Export(const char* outPath) {
    auto report = Unbound::ExportArchive(outPath != nullptr ? outPath : "oot-unbound.o2r");
    if (!report.ok) {
        SPDLOG_ERROR("[Unbound export] failed: {}", report.error);
        return 1;
    }
    return 0;
}
