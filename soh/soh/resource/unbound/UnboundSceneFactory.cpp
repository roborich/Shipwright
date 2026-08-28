// SOH [Unbound] scene.json / rooms/<n>.json -> SOH::Scene. See unbound-docs/scene-format.md §2, §4.
//
// BuildScene(doc)
//   -> for each setup: BuildSetupCommands (one SetXxx builder per key, in the vanilla execution order)
//   -> primary setup gets SetAlternateHeaders whose children are the other setups
//   -> every setup gets the top-level SetRoomList / SetCollisionHeader injected (alt headers are
//      executed instead of the primary list, so they must be self-contained)
#include "UnboundFactories.h"
#include "UnboundJson.h"
#include "UnboundSchema.h"

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>

#include "z64environment.h"
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

using Unbound::Field;
using Unbound::Json;
using Unbound::ListKeys;
using Unbound::NumberField;
using Unbound::PathField;
using Unbound::PositionalKeys;
using Unbound::ReadRgb;
using Unbound::ReadVec3f;
using Unbound::ReadVec3s;
using Unbound::ToInt;
namespace K = Unbound::Schema;

namespace SOH {
namespace {

// ---- helpers -----------------------------------------------------------------------------------

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

    std::vector<std::string> Positional(const Json& list, const char* key) const {
        return PositionalKeys(list, docPath + " " + key);
    }
};

std::shared_ptr<Ship::IResource> LoadSub(const std::string& path) {
    return Ship::Context::GetInstance()->GetResourceManager()->LoadResourceProcess(path.c_str());
}

const Json& Sub(const Json& obj, const char* key) {
    static const Json empty = Json::object();
    auto it = obj.find(key);
    return it == obj.end() ? empty : *it;
}

ActorEntry ReadActor(const Json& a) {
    ActorEntry e{};
    e.id = (s16)Field(a, K::kId);
    e.pos = ReadVec3f(Sub(a, K::kPos));
    e.rot = ReadVec3s(Sub(a, K::kRot));
    e.params = (s16)Field(a, K::kParams);
    return e;
}

// Cross-setup references and the room origin, shared by every setup of one document.
struct SharedRefs {
    Json rooms;                       // top-level "rooms" (scene docs)
    std::string collision;            // top-level "collision" (scene docs)
    Vec3f origin{ 0.0f, 0.0f, 0.0f }; // top-level "origin" (room docs): world position of the mesh's local origin
};

using Command = std::shared_ptr<ISceneCommand>;

// ---- scalar settings ---------------------------------------------------------------------------

Command BuildSpecialObjects(CommandBuilder& b, const Json& s) {
    auto cmd = b.Make<SetSpecialObjects>(SceneCommandID::SetSpecialObjects);
    cmd->specialObjects.elfMessage = (int8_t)Field(s, K::kElfMessage);
    cmd->specialObjects.globalObject = (int16_t)Field(s, K::kGlobalObject);
    return cmd;
}

Command BuildRoomBehavior(CommandBuilder& b, const Json& s) {
    auto cmd = b.Make<SetRoomBehavior>(SceneCommandID::SetRoomBehavior);
    cmd->roomBehavior.gameplayFlags = (int8_t)Field(s, K::kGameplayFlags);
    cmd->roomBehavior.gameplayFlags2 = (int32_t)Field(s, K::kGameplayFlags2);
    return cmd;
}

Command BuildEcho(CommandBuilder& b, const Json& s) {
    auto cmd = b.Make<SetEchoSettings>(SceneCommandID::SetEchoSettings);
    cmd->settings.echo = (int8_t)ToInt(s);
    return cmd;
}

Command BuildTime(CommandBuilder& b, const Json& s) {
    auto cmd = b.Make<SetTimeSettings>(SceneCommandID::SetTimeSettings);
    cmd->settings.hour = (uint8_t)Field(s, K::kHour, 0xFF);
    cmd->settings.minute = (uint8_t)Field(s, K::kMinute, 0xFF);
    cmd->settings.timeIncrement = (uint8_t)Field(s, K::kIncrement, 0xFF);
    return cmd;
}

Command BuildWind(CommandBuilder& b, const Json& s) {
    auto cmd = b.Make<SetWindSettings>(SceneCommandID::SetWind);
    cmd->settings.windWest = (int8_t)Field(s, K::kWest);
    cmd->settings.windVertical = (int8_t)Field(s, K::kVertical);
    cmd->settings.windSouth = (int8_t)Field(s, K::kSouth);
    cmd->settings.windSpeed = (uint8_t)Field(s, K::kSpeed);
    return cmd;
}

Command BuildSkyboxModifier(CommandBuilder& b, const Json& s) {
    auto cmd = b.Make<SetSkyboxModifier>(SceneCommandID::SetSkyboxModifier);
    cmd->modifier.skyboxDisabled = (uint8_t)Field(s, K::kSkyboxDisabled);
    cmd->modifier.sunMoonDisabled = (uint8_t)Field(s, K::kSunMoonDisabled);
    return cmd;
}

Command BuildSkybox(CommandBuilder& b, const Json& s) {
    auto cmd = b.Make<SetSkyboxSettings>(SceneCommandID::SetSkyboxSettings);
    cmd->settings.skyboxId = (uint8_t)Field(s, K::kId);
    cmd->settings.weather = (uint8_t)Field(s, K::kWeather);
    cmd->settings.indoors = (uint8_t)Field(s, K::kIndoors);
    cmd->settings.unk = (uint8_t)Field(s, K::kUnk);
    return cmd;
}

Command BuildSound(CommandBuilder& b, const Json& s) {
    auto cmd = b.Make<SetSoundSettings>(SceneCommandID::SetSoundSettings);
    cmd->settings.seqId = (uint8_t)Field(s, K::kSeq);
    cmd->settings.natureAmbienceId = (uint8_t)Field(s, K::kNatureAmbience);
    cmd->settings.reverb = (uint8_t)Field(s, K::kReverb);
    return cmd;
}

Command BuildCameraSettings(CommandBuilder& b, const Json& s) {
    auto cmd = b.Make<SetCameraSettings>(SceneCommandID::SetCameraSettings);
    cmd->settings.cameraMovement = (int8_t)Field(s, K::kCameraMovement);
    cmd->settings.worldMapArea = (int32_t)Field(s, K::kWorldMapArea);
    return cmd;
}

Command BuildCutscene(CommandBuilder& b, const Json& s) {
    auto cmd = b.Make<SetCutscenes>(SceneCommandID::SetCutscenes);
    cmd->fileName = s.get<std::string>();
    cmd->cutscene = std::static_pointer_cast<Cutscene>(LoadSub(cmd->fileName));
    if (cmd->cutscene == nullptr) {
        SPDLOG_ERROR("[Unbound] {}: cutscene {} failed to load", b.docPath, cmd->fileName);
    }
    return cmd;
}

// ---- sub-resources -----------------------------------------------------------------------------

Command BuildCollision(CommandBuilder& b, const std::string& path) {
    auto cmd = b.Make<SetCollisionHeader>(SceneCommandID::SetCollisionHeader);
    cmd->fileName = path;
    cmd->collisionHeader = std::static_pointer_cast<CollisionHeader>(LoadSub(path));
    if (cmd->collisionHeader == nullptr) {
        SPDLOG_ERROR("[Unbound] {}: collision {} failed to load", b.docPath, path);
    }
    return cmd;
}

Command BuildRoomList(CommandBuilder& b, const Json& rooms) {
    auto cmd = b.Make<SetRoomList>(SceneCommandID::SetRoomList);
    auto keys = b.Positional(rooms, K::kRooms);
    cmd->fileNames.reserve(keys.size());
    cmd->rooms.reserve(keys.size());
    for (const auto& k : keys) {
        cmd->fileNames.push_back(rooms[k].is_string() ? rooms[k].get<std::string>() : "");
        RomFile room{};
        room.fileName = (char*)cmd->fileNames.back().c_str();
        cmd->rooms.push_back(room);
    }
    cmd->numRooms = (uint32_t)cmd->rooms.size();
    return cmd;
}

Command BuildPathways(CommandBuilder& b, const Json& files) {
    auto cmd = b.Make<SetPathways>(SceneCommandID::SetPathways);
    for (const auto& p : files) {
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
    return cmd;
}

// ---- positional lists --------------------------------------------------------------------------

Command BuildEntranceList(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetEntranceList>(SceneCommandID::SetEntranceList);
    for (const auto& k : b.Positional(list, K::kEntrances)) {
        EntranceEntry e{};
        e.spawn = (u8)Field(list[k], K::kSpawn);
        e.room = (s16)Field(list[k], K::kRoom);
        cmd->entrances.push_back(e);
    }
    cmd->numEntrances = (uint32_t)cmd->entrances.size();
    return cmd;
}

Command BuildStartPositions(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetStartPositionList>(SceneCommandID::SetStartPositionList);
    for (const auto& k : b.Positional(list, K::kSpawns)) {
        cmd->startPositions.push_back(ReadActor(list[k]));
    }
    cmd->numStartPositions = (uint32_t)cmd->startPositions.size();
    return cmd;
}

Command BuildExitList(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetExitList>(SceneCommandID::SetExitList);
    for (const auto& k : b.Positional(list, K::kExits)) {
        cmd->exits.push_back((uint16_t)ToInt(list[k]));
    }
    cmd->numExits = (uint32_t)cmd->exits.size();
    return cmd;
}

Command BuildTransitionActors(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetTransitionActorList>(SceneCommandID::SetTransitionActorList);
    for (const auto& k : b.Positional(list, K::kTransitionActors)) {
        const Json& t = list[k];
        TransitionActorEntry e{};
        e.id = (s16)Field(t, K::kId);
        e.pos = ReadVec3f(Sub(t, K::kPos));
        e.rotY = (s16)Field(t, K::kRotY);
        e.params = (s16)Field(t, K::kParams);
        e.sides[0].room = (s16)Field(Sub(t, K::kFront), K::kRoom);
        e.sides[0].effects = (s8)Field(Sub(t, K::kFront), K::kEffects);
        e.sides[1].room = (s16)Field(Sub(t, K::kBack), K::kRoom);
        e.sides[1].effects = (s8)Field(Sub(t, K::kBack), K::kEffects);
        cmd->transitionActorList.push_back(e);
    }
    cmd->numTransitionActors = (uint32_t)cmd->transitionActorList.size();
    return cmd;
}

Command BuildObjectList(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetObjectList>(SceneCommandID::SetObjectList);
    for (const auto& k : b.Positional(list, K::kObjects)) {
        cmd->objects.push_back((int16_t)ToInt(list[k]));
    }
    cmd->numObjects = (uint32_t)cmd->objects.size();
    return cmd;
}

LightInfo ReadLight(const Json& l) {
    LightInfo info{};
    info.type = (u8)Field(l, K::kType);
    if (info.type == 1) { // LIGHT_DIRECTIONAL
        Vec3s dir = ReadVec3s(Sub(l, K::kDir));
        info.params.dir.x = (s8)dir.x;
        info.params.dir.y = (s8)dir.y;
        info.params.dir.z = (s8)dir.z;
        ReadRgb(Sub(l, K::kColor), info.params.dir.color);
    } else {
        Vec3f pos = ReadVec3f(Sub(l, K::kPos));
        info.params.point.x = pos.x;
        info.params.point.y = pos.y;
        info.params.point.z = pos.z;
        ReadRgb(Sub(l, K::kColor), info.params.point.color);
        info.params.point.drawGlow = (u8)Field(l, K::kGlow);
        info.params.point.radius = (s16)Field(l, K::kRadius);
    }
    return info;
}

Command BuildLightList(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetLightList>(SceneCommandID::SetLightList);
    for (const auto& k : b.Positional(list, K::kLights)) {
        cmd->lightList.push_back(ReadLight(list[k]));
    }
    cmd->numLights = (uint32_t)cmd->lightList.size();
    return cmd;
}

// SOH [Unbound] world-unit fog / draw distance (extent.md). Any of fogStart / fogEnd / drawDistance switches
// the entry to world mode; the others take sensible defaults so a mod can set just "drawDistance".
void ReadWorldFog(const Json& s, EnvLightSettings& e) {
    if (!s.contains(K::kFogStart) && !s.contains(K::kFogEnd) && !s.contains(K::kDrawDistance)) {
        return;
    }
    e.worldFog = 1;
    e.drawDistance = (f32)NumberField(s, K::kDrawDistance, e.fogFar > 0 ? e.fogFar : 12800);
    e.fogEnd = (f32)NumberField(s, K::kFogEnd, e.drawDistance);
    e.fogStart = (f32)NumberField(s, K::kFogStart, Environment_LegacyFogStart(e.fogNear, e.fogEnd));
    e.nearPlane = (f32)NumberField(s, K::kNearPlane, 0);
}

EnvLightSettings ReadLighting(const Json& s) {
    EnvLightSettings e{};
    ReadRgb(Sub(s, K::kAmbient), e.ambientColor);
    ReadRgb(Sub(s, K::kLight1Dir), e.light1Dir);
    ReadRgb(Sub(s, K::kLight1Color), e.light1Color);
    ReadRgb(Sub(s, K::kLight2Dir), e.light2Dir);
    ReadRgb(Sub(s, K::kLight2Color), e.light2Color);
    ReadRgb(Sub(s, K::kFogColor), e.fogColor);
    e.fogNear = (s16)Field(s, K::kFogNear);
    e.fogFar = (s16)Field(s, K::kFogFar);
    ReadWorldFog(s, e);
    return e;
}

Command BuildLighting(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetLightingSettings>(SceneCommandID::SetLightingSettings);
    for (const auto& k : b.Positional(list, K::kLighting)) {
        cmd->settings.push_back(ReadLighting(list[k]));
    }
    return cmd;
}

// ---- keyed list --------------------------------------------------------------------------------

Command BuildActorList(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetActorList>(SceneCommandID::SetActorList);
    for (const auto& k : ListKeys(list)) { // keyed: $order then numeric-first key order
        if (list[k].is_object()) {
            cmd->actorList.push_back(ReadActor(list[k]));
        }
    }
    cmd->numActors = (uint32_t)cmd->actorList.size();
    return cmd;
}

// ---- mesh --------------------------------------------------------------------------------------

// type 0 / type 2: positional { opa, xlu [, pos, radius] } entries
void ReadMeshDlists(CommandBuilder& b, SetMesh& cmd, const Json& entries, uint8_t type) {
    auto keys = b.Positional(entries, K::kEntries);
    cmd.opaPaths.reserve(keys.size());
    cmd.xluPaths.reserve(keys.size());
    if (type == 0) {
        cmd.dlists.reserve(keys.size());
        for (const auto& k : keys) {
            PolygonDlist d{};
            d.opa = SetMesh::KeepDlistPath(cmd.opaPaths, PathField(entries[k], K::kOpa));
            d.xlu = SetMesh::KeepDlistPath(cmd.xluPaths, PathField(entries[k], K::kXlu));
            cmd.dlists.push_back(d);
        }
        cmd.meshHeader.polygon0.num = (u32)cmd.dlists.size();
        cmd.meshHeader.polygon0.start = cmd.dlists.data();
        return;
    }
    cmd.dlists2.reserve(keys.size());
    for (const auto& k : keys) {
        PolygonDlist2 d{};
        d.pos = ReadVec3f(Sub(entries[k], K::kPos));
        d.unk_06 = (f32)NumberField(entries[k], K::kRadius);
        d.opa = SetMesh::KeepDlistPath(cmd.opaPaths, PathField(entries[k], K::kOpa));
        d.xlu = SetMesh::KeepDlistPath(cmd.xluPaths, PathField(entries[k], K::kXlu));
        cmd.dlists2.push_back(d);
    }
    cmd.meshHeader.polygon2.num = (u32)cmd.dlists2.size();
    cmd.meshHeader.polygon2.start = cmd.dlists2.data();
}

BgImage ReadBgImage(SetMesh& cmd, const Json& img) {
    BgImage out{};
    out.unk_00 = (u16)Field(img, K::kUnk00);
    out.id = (u8)Field(img, K::kId);
    out.source = (void*)SetMesh::KeepDlistPath(cmd.imagePaths, PathField(img, K::kSource));
    out.unk_0C = (u32)Field(img, K::kUnk0C);
    out.tlut = (u32)Field(img, K::kTlut);
    out.width = (u16)Field(img, K::kWidth);
    out.height = (u16)Field(img, K::kHeight);
    out.fmt = (u8)Field(img, K::kFmt);
    out.siz = (u8)Field(img, K::kSiz);
    out.mode0 = (u16)Field(img, K::kMode0);
    out.tlutCount = (u16)Field(img, K::kTlutCount);
    return out;
}

// type 1: a pre-rendered background ("image" for format 1, positional "images" for format 2) plus one dlist
void ReadMeshBackground(CommandBuilder& b, SetMesh& cmd, const Json& m) {
    auto& p1 = cmd.meshHeader.polygon1;
    p1.format = (u8)Field(m, K::kFormat, 1);
    if (p1.format == 1) {
        cmd.imagePaths.reserve(1);
        cmd.SetSingleImage(ReadBgImage(cmd, Sub(m, K::kImage)));
    } else {
        const Json& images = Sub(m, K::kImages);
        auto keys = b.Positional(images, K::kImages);
        cmd.imagePaths.reserve(keys.size());
        cmd.images.reserve(keys.size());
        for (const auto& k : keys) {
            cmd.images.push_back(ReadBgImage(cmd, images[k]));
        }
        p1.multi.count = (u8)cmd.images.size();
        p1.multi.list = cmd.images.data();
    }
    cmd.opaPaths.reserve(1);
    cmd.xluPaths.reserve(1);
    PolygonDlist d{};
    d.opa = SetMesh::KeepDlistPath(cmd.opaPaths, PathField(m, K::kOpa));
    d.xlu = SetMesh::KeepDlistPath(cmd.xluPaths, PathField(m, K::kXlu));
    cmd.dlists.push_back(d);
    p1.dlist = (Gfx*)cmd.dlists.data();
}

Command BuildMesh(CommandBuilder& b, const Json& m, const Vec3f& origin) {
    auto cmd = b.Make<SetMesh>(SceneCommandID::SetMesh);
    uint8_t type = (uint8_t)Field(m, K::kType);
    cmd->data = 0;
    cmd->meshHeaderType = type;
    cmd->meshHeader.base.type = type;
    cmd->origin = origin;
    if (type == 0 || type == 2) {
        ReadMeshDlists(b, *cmd, Sub(m, K::kEntries), type);
    } else if (type == 1) {
        ReadMeshBackground(b, *cmd, m);
    } else {
        SPDLOG_ERROR("[Unbound] {}: unknown mesh type {}", b.docPath, type);
    }
    return cmd;
}

// ---- one setup -> command list -----------------------------------------------------------------

// Order follows the vanilla headers (and Prelude's emitter): settings that seed envCtx first, lists after.
void BuildSetupCommands(CommandBuilder& b, const Json& setup, const SharedRefs& shared, std::vector<Command>& out) {
    auto has = [&](const char* key) { return setup.contains(key) && !setup[key].is_null(); };
    auto add = [&](const char* key, auto&& build) {
        if (has(key)) {
            out.push_back(build(b, setup[key]));
        }
    };

    add(K::kSpecialObjects, BuildSpecialObjects);
    if (!shared.collision.empty()) {
        out.push_back(BuildCollision(b, shared.collision));
    }
    if (shared.rooms.is_object() && !shared.rooms.empty()) {
        out.push_back(BuildRoomList(b, shared.rooms));
    }
    add(K::kBehavior, BuildRoomBehavior);
    add(K::kEcho, BuildEcho);
    add(K::kTime, BuildTime);
    add(K::kWind, BuildWind);
    add(K::kSkyboxModifier, BuildSkyboxModifier);
    add(K::kSkybox, BuildSkybox);
    add(K::kSound, BuildSound);
    add(K::kCameraSettings, BuildCameraSettings);
    add(K::kLighting, BuildLighting);
    if (has(K::kPaths) && setup[K::kPaths].is_array()) {
        out.push_back(BuildPathways(b, setup[K::kPaths]));
    }
    add(K::kEntrances, BuildEntranceList);
    add(K::kSpawns, BuildStartPositions);
    add(K::kTransitionActors, BuildTransitionActors);
    add(K::kObjects, BuildObjectList);
    add(K::kLights, BuildLightList);
    add(K::kActors, BuildActorList);
    add(K::kExits, BuildExitList);
    if (has(K::kMesh)) {
        out.push_back(BuildMesh(b, setup[K::kMesh], shared.origin));
    }
    if (has(K::kCutscene) && setup[K::kCutscene].is_string()) {
        out.push_back(BuildCutscene(b, setup[K::kCutscene]));
    }
    out.push_back(b.Make<EndMarker>(SceneCommandID::EndMarker));
}

std::shared_ptr<Scene> BuildSetupScene(CommandBuilder& b, const Json& setup, const SharedRefs& shared) {
    auto scene = std::make_shared<Scene>(b.sceneInit);
    BuildSetupCommands(b, setup, shared, scene->commands);
    return scene;
}

// Setups "1".."N" become children of a leading SetAlternateHeaders, exactly as the binary command produces.
Command BuildAlternateHeaders(CommandBuilder& b, const Json& setups, const std::vector<std::string>& keys,
                              int64_t maxSetup, const SharedRefs& shared) {
    auto alt = b.Make<SetAlternateHeaders>(SceneCommandID::SetAlternateHeaders);
    alt->headers.resize((size_t)maxSetup, nullptr);
    for (const auto& k : keys) {
        int64_t index = ToInt(k, -1);
        if (index <= 0) {
            continue;
        }
        alt->headers[(size_t)index - 1] = BuildSetupScene(b, setups[k], shared);
        alt->headerFileNames.push_back(b.docPath + "#" + k);
    }
    alt->numHeaders = (uint32_t)alt->headers.size();
    return alt;
}

std::shared_ptr<Scene> BuildScene(std::shared_ptr<Ship::ResourceInitData> initData, const Json& doc) {
    CommandBuilder b{ initData, initData->Path };
    SharedRefs shared;
    shared.rooms = Sub(doc, K::kRooms);
    shared.collision = PathField(doc, K::kCollision);
    shared.origin = ReadVec3f(Sub(doc, K::kOrigin));

    const Json& setups = Sub(doc, K::kSetups);
    if (!setups.contains("0")) {
        SPDLOG_ERROR("[Unbound] {}: no setup \"0\"", initData->Path);
        return nullptr;
    }
    auto keys = ListKeys(setups);
    int64_t maxSetup = 0;
    for (const auto& k : keys) {
        maxSetup = std::max(maxSetup, ToInt(k, -1));
    }

    auto primary = std::make_shared<Scene>(initData);
    if (maxSetup > 0) {
        primary->commands.push_back(BuildAlternateHeaders(b, setups, keys, maxSetup, shared));
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
        SPDLOG_DEBUG("[Unbound] {}: built from JSON ({} setups, {} commands in setup 0)", initData->Path,
                     Sub(doc, K::kSetups).size(), scene->commands.size());
    }
    return scene;
}

} // namespace SOH
