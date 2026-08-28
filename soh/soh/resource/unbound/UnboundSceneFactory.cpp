// SOH [Unbound] scene.json / rooms/<n>.json -> SOH::Scene. See unbound-docs/scene-format.md §2, §4.
//
// BuildScene(doc)
//   -> for each setup: BuildSetupCommands (one SetXxx per key, in the vanilla execution order)
//   -> primary setup gets SetAlternateHeaders whose children are the other setups
//   -> every setup gets the top-level SetRoomList / SetCollisionHeader injected (alt headers are
//      executed instead of the primary list, so they must be self-contained)
#include "UnboundFactories.h"
#include "UnboundJson.h"

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>

#include "soh/resource/type/CollisionHeader.h"
#include "soh/resource/type/Cutscene.h"
#include "soh/resource/type/Path.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/scenecommand/EndMarker.h"
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

using Unbound::Json;
using Unbound::ListKeys;
using Unbound::ToInt;

namespace SOH {
namespace {

// ---- small readers ----------------------------------------------------------------------------

struct CommandBuilder {
    std::shared_ptr<Ship::ResourceInitData> sceneInit;
    std::string docPath;
    size_t nextIndex = 0;

    template <typename T> std::shared_ptr<T> Make(SceneCommandID id) {
        auto init = std::make_shared<Ship::ResourceInitData>(*sceneInit);
        init->Path = docPath + "/SceneCommand" + std::to_string(nextIndex++);
        auto cmd = std::make_shared<T>(init);
        cmd->cmdId = id;
        return cmd;
    }
};

std::shared_ptr<Ship::IResource> LoadSub(const std::string& path) {
    return Ship::Context::GetInstance()->GetResourceManager()->LoadResourceProcess(path.c_str());
}

Vec3s ReadVec(const Json& v) {
    Vec3s out{ 0, 0, 0 };
    if (v.is_array() && v.size() >= 3) {
        out.x = (s16)ToInt(v[0]);
        out.y = (s16)ToInt(v[1]);
        out.z = (s16)ToInt(v[2]);
    }
    return out;
}

// SOH [Unbound] world positions are f32 (world extent); rotations stay Vec3s via ReadVec
Vec3f ReadVecF(const Json& v) {
    Vec3f out{ 0.0f, 0.0f, 0.0f };
    if (v.is_array() && v.size() >= 3) {
        out.x = (f32)Unbound::ToNumber(v[0]);
        out.y = (f32)Unbound::ToNumber(v[1]);
        out.z = (f32)Unbound::ToNumber(v[2]);
    }
    return out;
}

template <typename T> void ReadRgb(const Json& v, T* out) {
    if (v.is_array() && v.size() >= 3) {
        out[0] = (T)ToInt(v[0]);
        out[1] = (T)ToInt(v[1]);
        out[2] = (T)ToInt(v[2]);
    }
}

int64_t Field(const Json& obj, const char* key, int64_t fallback = 0) {
    auto it = obj.find(key);
    return it == obj.end() ? fallback : ToInt(*it, fallback);
}

std::string PathField(const Json& obj, const char* key) {
    auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : "";
}

ActorEntry ReadActor(const Json& a) {
    ActorEntry e{};
    e.id = (s16)Field(a, "id");
    e.pos = ReadVecF(a.value("pos", Json::array()));
    e.rot = ReadVec(a.value("rot", Json::array()));
    e.params = (s16)Field(a, "params");
    return e;
}

// Positional lists cannot have holes; stop at the first gap with an error.
std::vector<std::string> PositionalKeys(const Json& list, const std::string& what) {
    std::vector<std::string> keys = ListKeys(list);
    for (size_t i = 0; i < keys.size(); i++) {
        if (keys[i] != std::to_string(i)) {
            SPDLOG_ERROR("[Unbound] {}: positional list has a hole at index {} (found key '{}'); truncating", what, i,
                         keys[i]);
            keys.resize(i);
            break;
        }
    }
    return keys;
}

// ---- per-command builders ----------------------------------------------------------------------

std::shared_ptr<ISceneCommand> BuildMesh(CommandBuilder& b, const Json& m) {
    auto cmd = b.Make<SetMesh>(SceneCommandID::SetMesh);
    uint8_t type = (uint8_t)Field(m, "type");
    cmd->data = 0;
    cmd->meshHeaderType = type;
    cmd->meshHeader.base.type = type;

    auto keepPath = [](std::vector<std::string>& store, const std::string& p) -> Gfx* {
        if (p.empty()) {
            return nullptr;
        }
        store.push_back("__OTR__" + p);
        return (Gfx*)store.back().c_str();
    };

    if (type == 0 || type == 2) {
        const Json& entries = m.value("entries", Json::object());
        auto keys = PositionalKeys(entries, b.docPath + " mesh.entries");
        cmd->opaPaths.reserve(keys.size());
        cmd->xluPaths.reserve(keys.size());
        if (type == 0) {
            cmd->dlists.reserve(keys.size());
            for (const auto& k : keys) {
                const Json& e = entries[k];
                PolygonDlist d{};
                d.opa = keepPath(cmd->opaPaths, PathField(e, "opa"));
                d.xlu = keepPath(cmd->xluPaths, PathField(e, "xlu"));
                cmd->dlists.push_back(d);
            }
            cmd->meshHeader.polygon0.num = (u32)cmd->dlists.size();
            cmd->meshHeader.polygon0.start = cmd->dlists.data();
        } else {
            cmd->dlists2.reserve(keys.size());
            for (const auto& k : keys) {
                const Json& e = entries[k];
                PolygonDlist2 d{};
                d.pos = ReadVecF(e.value("pos", Json::array()));
                d.unk_06 = (f32)Unbound::ToNumber(e.value("radius", Json(0)));
                d.opa = keepPath(cmd->opaPaths, PathField(e, "opa"));
                d.xlu = keepPath(cmd->xluPaths, PathField(e, "xlu"));
                cmd->dlists2.push_back(d);
            }
            cmd->meshHeader.polygon2.num = (u32)cmd->dlists2.size();
            cmd->meshHeader.polygon2.start = cmd->dlists2.data();
        }
    } else if (type == 1) {
        auto& p1 = cmd->meshHeader.polygon1;
        p1.format = (u8)Field(m, "format", 1);
        auto readImage = [&](const Json& img, BgImage& out) {
            out.unk_00 = (u16)Field(img, "unk00");
            out.id = (u8)Field(img, "id");
            cmd->imagePaths.push_back("__OTR__" + PathField(img, "source"));
            out.source = (void*)cmd->imagePaths.back().c_str();
            out.unk_0C = (u32)Field(img, "unk0C");
            out.tlut = (u32)Field(img, "tlut");
            out.width = (u16)Field(img, "width");
            out.height = (u16)Field(img, "height");
            out.fmt = (u8)Field(img, "fmt");
            out.siz = (u8)Field(img, "siz");
            out.mode0 = (u16)Field(img, "mode0");
            out.tlutCount = (u16)Field(img, "tlutCount");
        };
        if (p1.format == 1) {
            cmd->imagePaths.reserve(1);
            BgImage single{};
            readImage(m.value("image", Json::object()), single);
            p1.single.source = single.source;
            p1.single.unk_0C = single.unk_0C;
            p1.single.tlut = (void*)(uintptr_t)single.tlut;
            p1.single.width = single.width;
            p1.single.height = single.height;
            p1.single.fmt = single.fmt;
            p1.single.siz = single.siz;
            p1.single.mode0 = single.mode0;
            p1.single.tlutCount = single.tlutCount;
        } else {
            const Json& images = m.value("images", Json::object());
            auto keys = PositionalKeys(images, b.docPath + " mesh.images");
            cmd->imagePaths.reserve(keys.size());
            cmd->images.reserve(keys.size());
            for (const auto& k : keys) {
                BgImage img{};
                readImage(images[k], img);
                cmd->images.push_back(img);
            }
            p1.multi.count = (u8)cmd->images.size();
            p1.multi.list = cmd->images.data();
        }
        cmd->opaPaths.reserve(1);
        cmd->xluPaths.reserve(1);
        PolygonDlist d{};
        d.opa = keepPath(cmd->opaPaths, PathField(m, "opa"));
        d.xlu = keepPath(cmd->xluPaths, PathField(m, "xlu"));
        cmd->dlists.push_back(d);
        p1.dlist = (Gfx*)cmd->dlists.data();
    } else {
        SPDLOG_ERROR("[Unbound] {}: unknown mesh type {}", b.docPath, type);
    }
    return cmd;
}

std::shared_ptr<ISceneCommand> BuildLightList(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetLightList>(SceneCommandID::SetLightList);
    for (const auto& k : PositionalKeys(list, b.docPath + " lights")) {
        const Json& l = list[k];
        LightInfo info{};
        info.type = (u8)Field(l, "type");
        if (info.type == 1) { // LIGHT_DIRECTIONAL
            Vec3s dir = ReadVec(l.value("dir", Json::array()));
            info.params.dir.x = (s8)dir.x;
            info.params.dir.y = (s8)dir.y;
            info.params.dir.z = (s8)dir.z;
            ReadRgb(l.value("color", Json::array()), info.params.dir.color);
        } else {
            Vec3f pos = ReadVecF(l.value("pos", Json::array()));
            info.params.point.x = pos.x;
            info.params.point.y = pos.y;
            info.params.point.z = pos.z;
            ReadRgb(l.value("color", Json::array()), info.params.point.color);
            info.params.point.drawGlow = (u8)Field(l, "glow");
            info.params.point.radius = (s16)Field(l, "radius");
        }
        cmd->lightList.push_back(info);
    }
    cmd->numLights = (uint32_t)cmd->lightList.size();
    return cmd;
}

std::shared_ptr<ISceneCommand> BuildLighting(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetLightingSettings>(SceneCommandID::SetLightingSettings);
    for (const auto& k : PositionalKeys(list, b.docPath + " lighting")) {
        const Json& s = list[k];
        EnvLightSettings e{};
        ReadRgb(s.value("ambient", Json::array()), e.ambientColor);
        ReadRgb(s.value("light1Dir", Json::array()), e.light1Dir);
        ReadRgb(s.value("light1Color", Json::array()), e.light1Color);
        ReadRgb(s.value("light2Dir", Json::array()), e.light2Dir);
        ReadRgb(s.value("light2Color", Json::array()), e.light2Color);
        ReadRgb(s.value("fogColor", Json::array()), e.fogColor);
        e.fogNear = (s16)Field(s, "fogNear");
        e.fogFar = (s16)Field(s, "fogFar");
        // SOH [Unbound] world-unit fog / draw distance (extent.md). Any of the three keys switches the entry to
        // world mode; the others take sensible defaults so a mod can set just "drawDistance".
        if (s.contains("fogStart") || s.contains("fogEnd") || s.contains("drawDistance")) {
            e.worldFog = 1;
            e.drawDistance = (f32)Unbound::ToNumber(s.value("drawDistance", Json(e.fogFar > 0 ? e.fogFar : 12800)));
            e.fogEnd = (f32)Unbound::ToNumber(s.value("fogEnd", Json(e.drawDistance)));
            // default start: the vanilla fogNear converted to a distance (zNear 10): 10 * 1000 / (1000 - fogNear)
            int legacyNear = (int)(e.fogNear & 0x3FF);
            f32 legacyStart = legacyNear >= 997 ? e.fogEnd : 10000.0f / (f32)(1000 - legacyNear);
            e.fogStart = (f32)Unbound::ToNumber(s.value("fogStart", Json(legacyStart)));
            e.nearPlane = (f32)Unbound::ToNumber(s.value("nearPlane", Json(0)));
        }
        cmd->settings.push_back(e);
    }
    return cmd;
}

std::shared_ptr<ISceneCommand> BuildRoomList(CommandBuilder& b, const Json& rooms) {
    auto cmd = b.Make<SetRoomList>(SceneCommandID::SetRoomList);
    auto keys = PositionalKeys(rooms, b.docPath + " rooms");
    cmd->fileNames.reserve(keys.size());
    cmd->rooms.reserve(keys.size());
    for (const auto& k : keys) {
        cmd->fileNames.push_back(rooms[k].is_string() ? rooms[k].get<std::string>() : "");
        RomFile room{};
        room.vromStart = 0;
        room.vromEnd = 0;
        room.fileName = (char*)cmd->fileNames.back().c_str();
        cmd->rooms.push_back(room);
    }
    cmd->numRooms = (uint32_t)cmd->rooms.size();
    return cmd;
}

std::shared_ptr<ISceneCommand> BuildCollision(CommandBuilder& b, const std::string& path) {
    auto cmd = b.Make<SetCollisionHeader>(SceneCommandID::SetCollisionHeader);
    cmd->fileName = path;
    cmd->collisionHeader = std::static_pointer_cast<CollisionHeader>(LoadSub(path));
    if (cmd->collisionHeader == nullptr) {
        SPDLOG_ERROR("[Unbound] {}: collision {} failed to load", b.docPath, path);
    }
    return cmd;
}

// ---- one setup -> command list -----------------------------------------------------------------

struct SharedRefs {
    Json rooms;           // top-level "rooms" (scene docs)
    std::string collision; // top-level "collision" (scene docs)
    Vec3f origin{ 0.0f, 0.0f, 0.0f }; // top-level "origin" (room docs): world position of the mesh's local origin
};

// Order follows the vanilla headers (and Prelude's emitter): settings that seed envCtx first, lists after.
void BuildSetupCommands(CommandBuilder& b, const Json& setup, const SharedRefs& shared,
                        std::vector<std::shared_ptr<ISceneCommand>>& out) {
    auto has = [&](const char* key) { return setup.contains(key) && !setup[key].is_null(); };

    if (has("specialObjects")) {
        auto cmd = b.Make<SetSpecialObjects>(SceneCommandID::SetSpecialObjects);
        cmd->specialObjects.elfMessage = (int8_t)Field(setup["specialObjects"], "elfMessage");
        cmd->specialObjects.globalObject = (int16_t)Field(setup["specialObjects"], "globalObject");
        out.push_back(cmd);
    }
    if (!shared.collision.empty()) {
        out.push_back(BuildCollision(b, shared.collision));
    }
    if (shared.rooms.is_object() && !shared.rooms.empty()) {
        out.push_back(BuildRoomList(b, shared.rooms));
    }
    if (has("behavior")) {
        auto cmd = b.Make<SetRoomBehavior>(SceneCommandID::SetRoomBehavior);
        cmd->roomBehavior.gameplayFlags = (int8_t)Field(setup["behavior"], "gameplayFlags");
        cmd->roomBehavior.gameplayFlags2 = (int32_t)Field(setup["behavior"], "gameplayFlags2");
        out.push_back(cmd);
    }
    if (has("echo")) {
        auto cmd = b.Make<SetEchoSettings>(SceneCommandID::SetEchoSettings);
        cmd->settings.echo = (int8_t)ToInt(setup["echo"]);
        out.push_back(cmd);
    }
    if (has("time")) {
        auto cmd = b.Make<SetTimeSettings>(SceneCommandID::SetTimeSettings);
        cmd->settings.hour = (uint8_t)Field(setup["time"], "hour", 0xFF);
        cmd->settings.minute = (uint8_t)Field(setup["time"], "minute", 0xFF);
        cmd->settings.timeIncrement = (uint8_t)Field(setup["time"], "increment", 0xFF);
        out.push_back(cmd);
    }
    if (has("wind")) {
        auto cmd = b.Make<SetWindSettings>(SceneCommandID::SetWind);
        cmd->settings.windWest = (int8_t)Field(setup["wind"], "west");
        cmd->settings.windVertical = (int8_t)Field(setup["wind"], "vertical");
        cmd->settings.windSouth = (int8_t)Field(setup["wind"], "south");
        cmd->settings.windSpeed = (uint8_t)Field(setup["wind"], "speed");
        out.push_back(cmd);
    }
    if (has("skyboxModifier")) {
        auto cmd = b.Make<SetSkyboxModifier>(SceneCommandID::SetSkyboxModifier);
        cmd->modifier.skyboxDisabled = (uint8_t)Field(setup["skyboxModifier"], "skyboxDisabled");
        cmd->modifier.sunMoonDisabled = (uint8_t)Field(setup["skyboxModifier"], "sunMoonDisabled");
        out.push_back(cmd);
    }
    if (has("skybox")) {
        auto cmd = b.Make<SetSkyboxSettings>(SceneCommandID::SetSkyboxSettings);
        cmd->settings.skyboxId = (uint8_t)Field(setup["skybox"], "id");
        cmd->settings.weather = (uint8_t)Field(setup["skybox"], "weather");
        cmd->settings.indoors = (uint8_t)Field(setup["skybox"], "indoors");
        cmd->settings.unk = (uint8_t)Field(setup["skybox"], "unk");
        out.push_back(cmd);
    }
    if (has("sound")) {
        auto cmd = b.Make<SetSoundSettings>(SceneCommandID::SetSoundSettings);
        cmd->settings.seqId = (uint8_t)Field(setup["sound"], "seq");
        cmd->settings.natureAmbienceId = (uint8_t)Field(setup["sound"], "natureAmbience");
        cmd->settings.reverb = (uint8_t)Field(setup["sound"], "reverb");
        out.push_back(cmd);
    }
    if (has("cameraSettings")) {
        auto cmd = b.Make<SetCameraSettings>(SceneCommandID::SetCameraSettings);
        cmd->settings.cameraMovement = (int8_t)Field(setup["cameraSettings"], "cameraMovement");
        cmd->settings.worldMapArea = (int32_t)Field(setup["cameraSettings"], "worldMapArea");
        out.push_back(cmd);
    }
    if (has("lighting")) {
        out.push_back(BuildLighting(b, setup["lighting"]));
    }
    if (has("paths") && setup["paths"].is_array()) {
        auto cmd = b.Make<SetPathways>(SceneCommandID::SetPathways);
        for (const auto& p : setup["paths"]) {
            if (!p.is_string()) {
                continue;
            }
            auto path = std::static_pointer_cast<Path>(LoadSub(p.get<std::string>()));
            if (path == nullptr) {
                SPDLOG_ERROR("[Unbound] {}: pathway {} failed to load", b.docPath, p.get<std::string>());
                continue;
            }
            cmd->paths.push_back(path->GetPointer());
            cmd->pathFileNames.push_back(p.get<std::string>());
        }
        cmd->numPaths = (uint32_t)cmd->paths.size();
        out.push_back(cmd);
    }
    if (has("entrances")) {
        auto cmd = b.Make<SetEntranceList>(SceneCommandID::SetEntranceList);
        const Json& list = setup["entrances"];
        for (const auto& k : PositionalKeys(list, b.docPath + " entrances")) {
            EntranceEntry e{};
            e.spawn = (u8)Field(list[k], "spawn");
            e.room = (u8)Field(list[k], "room");
            cmd->entrances.push_back(e);
        }
        cmd->numEntrances = (uint32_t)cmd->entrances.size();
        out.push_back(cmd);
    }
    if (has("spawns")) {
        auto cmd = b.Make<SetStartPositionList>(SceneCommandID::SetStartPositionList);
        const Json& list = setup["spawns"];
        for (const auto& k : PositionalKeys(list, b.docPath + " spawns")) {
            cmd->startPositions.push_back(ReadActor(list[k]));
        }
        cmd->numStartPositions = (uint32_t)cmd->startPositions.size();
        out.push_back(cmd);
    }
    if (has("transitionActors")) {
        auto cmd = b.Make<SetTransitionActorList>(SceneCommandID::SetTransitionActorList);
        const Json& list = setup["transitionActors"];
        for (const auto& k : PositionalKeys(list, b.docPath + " transitionActors")) {
            const Json& t = list[k];
            TransitionActorEntry e{};
            e.id = (s16)Field(t, "id");
            e.pos = ReadVecF(t.value("pos", Json::array()));
            e.rotY = (s16)Field(t, "rotY");
            e.params = (s16)Field(t, "params");
            const Json& front = t.value("front", Json::object());
            const Json& back = t.value("back", Json::object());
            e.sides[0].room = (s16)Field(front, "room");
            e.sides[0].effects = (s8)Field(front, "effects");
            e.sides[1].room = (s16)Field(back, "room");
            e.sides[1].effects = (s8)Field(back, "effects");
            cmd->transitionActorList.push_back(e);
        }
        cmd->numTransitionActors = (uint32_t)cmd->transitionActorList.size();
        out.push_back(cmd);
    }
    if (has("objects")) {
        auto cmd = b.Make<SetObjectList>(SceneCommandID::SetObjectList);
        const Json& list = setup["objects"];
        for (const auto& k : PositionalKeys(list, b.docPath + " objects")) {
            cmd->objects.push_back((int16_t)ToInt(list[k]));
        }
        cmd->numObjects = (uint32_t)cmd->objects.size();
        out.push_back(cmd);
    }
    if (has("lights")) {
        out.push_back(BuildLightList(b, setup["lights"]));
    }
    if (has("actors")) {
        auto cmd = b.Make<SetActorList>(SceneCommandID::SetActorList);
        const Json& list = setup["actors"];
        for (const auto& k : ListKeys(list)) { // keyed: $order then numeric-first key order
            if (list[k].is_object()) {
                cmd->actorList.push_back(ReadActor(list[k]));
            }
        }
        cmd->numActors = (uint32_t)cmd->actorList.size();
        out.push_back(cmd);
    }
    if (has("exits")) {
        auto cmd = b.Make<SetExitList>(SceneCommandID::SetExitList);
        const Json& list = setup["exits"];
        for (const auto& k : PositionalKeys(list, b.docPath + " exits")) {
            cmd->exits.push_back((uint16_t)ToInt(list[k]));
        }
        cmd->numExits = (uint32_t)cmd->exits.size();
        out.push_back(cmd);
    }
    if (has("mesh")) {
        auto mesh = BuildMesh(b, setup["mesh"]);
        static_cast<SetMesh*>(mesh.get())->origin = shared.origin;
        out.push_back(mesh);
    }
    if (has("cutscene") && setup["cutscene"].is_string()) {
        auto cmd = b.Make<SetCutscenes>(SceneCommandID::SetCutscenes);
        cmd->fileName = setup["cutscene"].get<std::string>();
        cmd->cutscene = std::static_pointer_cast<Cutscene>(LoadSub(cmd->fileName));
        if (cmd->cutscene == nullptr) {
            SPDLOG_ERROR("[Unbound] {}: cutscene {} failed to load", b.docPath, cmd->fileName);
        }
        out.push_back(cmd);
    }
    out.push_back(b.Make<EndMarker>(SceneCommandID::EndMarker));
}

std::shared_ptr<Scene> BuildSetupScene(CommandBuilder& b, const Json& setup, const SharedRefs& shared) {
    auto scene = std::make_shared<Scene>(b.sceneInit);
    BuildSetupCommands(b, setup, shared, scene->commands);
    return scene;
}

std::shared_ptr<Scene> BuildScene(std::shared_ptr<Ship::ResourceInitData> initData, const Json& doc) {
    CommandBuilder b{ initData, initData->Path };
    SharedRefs shared;
    shared.rooms = doc.value("rooms", Json::object());
    shared.collision = PathField(doc, "collision");
    if (doc.contains("origin") && doc["origin"].is_array() && doc["origin"].size() >= 3) {
        shared.origin.x = doc["origin"][0].get<float>();
        shared.origin.y = doc["origin"][1].get<float>();
        shared.origin.z = doc["origin"][2].get<float>();
    }

    const Json& setups = doc.value("setups", Json::object());
    auto keys = ListKeys(setups);
    int64_t maxSetup = -1;
    for (const auto& k : keys) {
        maxSetup = std::max(maxSetup, ToInt(k, -1));
    }
    if (!setups.contains("0")) {
        SPDLOG_ERROR("[Unbound] {}: no setup \"0\"", initData->Path);
        return nullptr;
    }

    auto primary = std::make_shared<Scene>(initData);
    if (maxSetup > 0) {
        auto alt = b.Make<SetAlternateHeaders>(SceneCommandID::SetAlternateHeaders);
        alt->headers.resize((size_t)maxSetup, nullptr);
        for (const auto& k : keys) {
            int64_t index = ToInt(k, -1);
            if (index <= 0) {
                continue;
            }
            alt->headers[(size_t)index - 1] = BuildSetupScene(b, setups[k], shared);
            alt->headerFileNames.push_back(initData->Path + "#" + k);
        }
        alt->numHeaders = (uint32_t)alt->headers.size();
        primary->commands.push_back(alt);
    }
    BuildSetupCommands(b, setups["0"], shared, primary->commands);
    return primary;
}

} // namespace

std::shared_ptr<Ship::IResource>
ResourceFactoryJsonSceneV1::ReadResource(std::shared_ptr<Ship::File> file,
                                         std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }
    Json doc = Unbound::LoadMergedJson(initData->Path);
    if (!doc.is_object()) {
        SPDLOG_ERROR("[Unbound] {}: no usable document", initData->Path);
        return nullptr;
    }
    auto scene = BuildScene(initData, doc);
    if (scene != nullptr) {
        SPDLOG_INFO("[Unbound] {}: built from JSON ({} setups, {} commands in setup 0)", initData->Path,
                    doc.value("setups", Json::object()).size(), scene->commands.size());
    }
    return scene;
}

} // namespace SOH
