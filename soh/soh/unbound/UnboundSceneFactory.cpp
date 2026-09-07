// SOH [Unbound] scene.json / rooms/<n>.json -> SOH::Scene. Format: unbound-docs/SPEC.md §4.2, §4.3.
//
// BuildScene(doc)
//   -> for each setup: BuildSetupCommands (one SetXxx builder per key, in the vanilla execution order)
//   -> primary setup gets SetAlternateHeaders whose children are the other setups
//   -> every setup gets the top-level SetRoomList / SetCollisionHeader injected (alt headers are
//      executed instead of the primary list, so they must be self-contained)
#include "UnboundFactories.h"
#include "UnboundJson.h"
#include "UnboundSchema.h"
#include "soh/unbound/SceneDB.h"

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include <limits>

#include "z64environment.h"
#include "soh/resource/type/CollisionHeader.h"
#include "soh/resource/type/Cutscene.h"
#include "soh/resource/type/Path.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/scenecommand/EndMarker.h"
#include "soh/resource/type/scenecommand/SetActorList.h"
#include "soh/resource/type/scenecommand/SetAlternateHeaders.h"
#include "soh/resource/type/scenecommand/SetAnimatedMaterialList.h"
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
#include "UnboundAudio.h"
#include "soh/resource/type/scenecommand/SetSpecialObjects.h"
#include "soh/resource/type/scenecommand/SetStartPositionList.h"
#include "soh/resource/type/scenecommand/SetTimeSettings.h"
#include "soh/resource/type/scenecommand/SetTransitionActorList.h"
#include "soh/resource/type/scenecommand/SetWindSettings.h"

using SOH::Unbound::Field;
using SOH::Unbound::Json;
using SOH::Unbound::ListKeys;
using SOH::Unbound::NumberField;
using SOH::Unbound::PathField;
using SOH::Unbound::PositionalKeys;
using SOH::Unbound::ReadRgb;
using SOH::Unbound::ReadVec3f;
using SOH::Unbound::ReadVec3s;
using SOH::Unbound::Sub;
using SOH::Unbound::SubArray;
using SOH::Unbound::ToInt;
namespace K = SOH::Unbound::Schema;

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

ActorEntry ReadActor(const Json& a) {
    ActorEntry e{};
    e.id = (s16)Field(a, K::kId);
    e.pos = ReadVec3f(SubArray(a, K::kPos));
    e.rot = ReadVec3s(SubArray(a, K::kRot));
    e.params = (s16)Field(a, K::kParams);
    return e;
}

// Cross-setup references, shared by every setup of one document.
struct SharedRefs {
    Json rooms;            // top-level "rooms" (scene docs)
    std::string collision; // top-level "collision" (scene docs)
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
    // The scene keeps its vanilla `seq` as the theme the game QUEUES; a bound song replaces it when the
    // theme is resolved for playback (AudioCollection::GetReplacementSequence) — so the u8 seqId, the
    // sSeqFlags table and the sequence-command word never see a custom id.
    const std::string song = PathField(s, K::kSong);
    if (!song.empty()) {
        cmd->unboundSongPath = song;
        cmd->unboundSongSeqId = SOH::Unbound::SequenceIdForPath(song);
        if (cmd->unboundSongSeqId == 0) {
            SPDLOG_ERROR("[Unbound] {}: song {} is not a loaded sequence (no mounted archive provides it)", b.docPath,
                         song);
        }
    }
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
        cmd->AddPathResource(path, p.get<std::string>()); // SPEC.md §4.2: documents concatenate in order
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

// An exit is an entrance table index, or the name of a vanilla (ENTR_*) or registered custom entrance.
uint16_t ResolveExit(CommandBuilder& b, const std::string& key, const Json& value) {
    if (value.is_number_integer() && value.get<int64_t>() >= 0) {
        return (uint16_t)value.get<int64_t>();
    }
    if (value.is_string()) {
        std::string name = value.get<std::string>();
        int32_t index = EntranceDB_RetrieveIndex(name.c_str());
        if (index >= 0) {
            return (uint16_t)index;
        }
        int64_t parsed = 0;
        if (Unbound::ParseIntString(name, parsed) && parsed >= 0) { // "0x0211" is still an index, not a name
            return (uint16_t)parsed;
        }
        throw Unbound::DocumentError(b.docPath + " " + K::kExits + "/" + key + ": unknown entrance '" + name + "'");
    }
    throw Unbound::DocumentError(b.docPath + " " + K::kExits + "/" + key + ": an exit is an index or an entrance name");
}

Command BuildExitList(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetExitList>(SceneCommandID::SetExitList);
    for (const auto& k : b.Positional(list, K::kExits)) {
        cmd->exits.push_back(ResolveExit(b, k, list[k]));
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
        e.pos = ReadVec3f(SubArray(t, K::kPos));
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

LightInfo ReadLight(const CommandBuilder& b, const Json& l) {
    LightInfo info{};
    int64_t type = Field(l, K::kType);
    if (type < 0 || type > 2) { // SPEC.md §4.3: the engine binds by type; anything else is rejected
        throw Unbound::DocumentError(b.docPath + ": light type " + std::to_string(type) + " is not 0, 1 or 2");
    }
    info.type = (u8)type;
    if (info.type == 1) { // LIGHT_DIRECTIONAL
        Vec3s dir = ReadVec3s(SubArray(l, K::kDir));
        info.params.dir.x = (s8)dir.x;
        info.params.dir.y = (s8)dir.y;
        info.params.dir.z = (s8)dir.z;
        ReadRgb(SubArray(l, K::kColor), info.params.dir.color);
    } else {
        Vec3f pos = ReadVec3f(SubArray(l, K::kPos));
        info.params.point.x = pos.x;
        info.params.point.y = pos.y;
        info.params.point.z = pos.z;
        ReadRgb(SubArray(l, K::kColor), info.params.point.color);
        info.params.point.drawGlow = (u8)Field(l, K::kGlow);
        info.params.point.radius = (s16)Field(l, K::kRadius);
    }
    return info;
}

Command BuildLightList(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetLightList>(SceneCommandID::SetLightList);
    for (const auto& k : b.Positional(list, K::kLights)) {
        cmd->lightList.push_back(ReadLight(b, list[k]));
    }
    cmd->numLights = (uint32_t)cmd->lightList.size();
    return cmd;
}

// A key counts as present only when it holds a number (SPEC.md §2: a wrong type is a missing key).
bool HasNumber(const Json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return false;
    }
    const double sentinel = std::numeric_limits<double>::quiet_NaN();
    return !std::isnan(SOH::Unbound::ToNumber(*it, sentinel));
}

// SOH [Unbound] world-unit fog / draw distance (SPEC.md §4.2 lighting entry). Any of fogStart / fogEnd /
// drawDistance switches the entry to world mode; the others take defaults so a mod can set just "drawDistance".
void ReadWorldFog(const Json& s, EnvLightSettings& e) {
    if (!HasNumber(s, K::kFogStart) && !HasNumber(s, K::kFogEnd) && !HasNumber(s, K::kDrawDistance)) {
        return;
    }
    e.worldFog = 1;
    e.drawDistance = (f32)NumberField(s, K::kDrawDistance, e.fogFar > 0 ? e.fogFar : 12800);
    e.fogEnd = (f32)NumberField(s, K::kFogEnd, e.drawDistance);
    e.fogStart = (f32)NumberField(s, K::kFogStart, Environment_LegacyFogStart(e.fogNear, e.fogEnd));
    e.nearPlane = (f32)NumberField(s, K::kNearPlane, 0);
}

// The vanilla fogNear word: low 10 bits fog near (0-1000), high 6 bits blend rate (z_kankyo.c reads
// `fogNear & 0x3FF` and `fogNear >> 0xA`). The document stores the two halves separately.
s16 PackFogNear(int64_t fogNear, int64_t blendRate) {
    return (s16)(((blendRate & 0x3F) << 10) | (fogNear & 0x3FF));
}

EnvLightSettings ReadLighting(const Json& s) {
    EnvLightSettings e{};
    ReadRgb(SubArray(s, K::kAmbient), e.ambientColor);
    ReadRgb(SubArray(s, K::kLight1Dir), e.light1Dir);
    ReadRgb(SubArray(s, K::kLight1Color), e.light1Color);
    ReadRgb(SubArray(s, K::kLight2Dir), e.light2Dir);
    ReadRgb(SubArray(s, K::kLight2Color), e.light2Color);
    ReadRgb(SubArray(s, K::kFogColor), e.fogColor);
    e.fogNear = PackFogNear(Field(s, K::kFogNear), Field(s, K::kFogBlendRate));
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

// ---- material animations (SPEC.md §4.2 `materialAnims`) --------------------------------------
//
// One entry -> one AnimatedMaterial. A malformed entry is logged and dropped (EntryError); the
// document still loads, so a single bad water material never takes a scene down with it.

struct EntryError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

int64_t RangedField(const Json& obj, const char* key, int64_t min, int64_t max, int64_t fallback = 0) {
    int64_t v = Field(obj, key, fallback);
    if (v < min || v > max) {
        throw EntryError(std::string(key) + " " + std::to_string(v) + " is outside " + std::to_string(min) + ".." +
                         std::to_string(max));
    }
    return v;
}

std::string StringField(const Json& e, const char* key) {
    return e.contains(key) && e[key].is_string() ? e[key].get<std::string>() : "";
}

u8 ReadAnimPass(const Json& e) {
    if (!e.contains(K::kPass)) {
        return ANIM_MAT_PASS_OPA | ANIM_MAT_PASS_XLU;
    }
    const std::string pass = StringField(e, K::kPass);
    const int id = K::IdOf(K::kAnimPasses, pass);
    if (id < 0) {
        throw EntryError("pass '" + pass + "' is not opa, xlu or both");
    }
    return (u8)id;
}

u8 ReadAnimType(const Json& e) {
    const std::string type = StringField(e, K::kType);
    const int id = K::IdOf(K::kAnimTypes, type);
    if (id < 0) {
        throw EntryError("type '" + type + "' is not an animated-material type");
    }
    return (u8)id;
}

// Step and tile-size ranges are writer requirements (SPEC.md §2): an out-of-range value wraps in the byte.
AnimatedMatTexScrollParams ReadScrollLayer(const Json& l) {
    AnimatedMatTexScrollParams p{};
    p.xStep = (s8)Field(l, K::kXStep);
    p.yStep = (s8)Field(l, K::kYStep);
    p.width = (u8)Field(l, K::kWidth);
    p.height = (u8)Field(l, K::kHeight);
    return p;
}

// The lists inside an entry (layers, key frames, colours, textures, frames) are JSON arrays: they
// replace whole across layers (SPEC.md §3.3) — a key-frame list patched one index at a time would
// not be a key-frame list. Only the entry list itself is positional.

SetAnimatedMaterialList::ScrollStorage ReadScroll(const Json& e, u8 type) {
    const size_t want = type == ANIM_MAT_TWO_TEX_SCROLL ? 2 : 1;
    const Json& layers = SubArray(e, K::kLayers);
    if (layers.size() != want) {
        throw EntryError("layers has " + std::to_string(layers.size()) + " entries; this type takes " +
                         std::to_string(want));
    }
    SetAnimatedMaterialList::ScrollStorage s{};
    for (size_t i = 0; i < want; i++) {
        if (!layers[i].is_object()) {
            throw EntryError("layers[" + std::to_string(i) + "] is not an object");
        }
        s.layers[i] = ReadScrollLayer(layers[i]);
    }
    return s;
}

// An array of fixed-width byte tuples ([r,g,b,a,lodFrac] / [r,g,b,a]) into `out`.
template <typename T, size_t N> void ReadByteTuples(const Json& list, const char* key, std::vector<T>& out) {
    static_assert(sizeof(T) == N, "colour tuples are read byte-wise");
    for (size_t k = 0; k < list.size(); k++) {
        const Json& tuple = list[k];
        if (!tuple.is_array() || tuple.size() < N) {
            throw EntryError(std::string(key) + "[" + std::to_string(k) + "] is not a " + std::to_string(N) +
                             "-component array");
        }
        T value{};
        u8* bytes = reinterpret_cast<u8*>(&value);
        for (size_t i = 0; i < N; i++) {
            bytes[i] = (u8)ToInt(tuple[i]);
        }
        out.push_back(value);
    }
}

// The rejections here are the ones SPEC.md §4.2 states, and each guards the draw path: `length` is a modulus,
// the key-frame walk needs an ascending list starting at 0 with a colour per key frame, and the non-linear
// path works in fixed arrays of ANIM_MAT_MAX_KEY_FRAMES.
SetAnimatedMaterialList::ColorStorage ReadColor(const Json& e) {
    SetAnimatedMaterialList::ColorStorage c{};
    c.params.keyFrameLength = (u16)RangedField(e, K::kLength, 1, UINT16_MAX);
    for (const Json& f : SubArray(e, K::kKeyFrames)) {
        const u16 frame = (u16)ToInt(f);
        if (!c.keyFrames.empty() && frame <= c.keyFrames.back()) {
            throw EntryError("keyFrames must be ascending frame numbers");
        }
        c.keyFrames.push_back(frame);
    }
    if (c.keyFrames.empty() || c.keyFrames.size() > ANIM_MAT_MAX_KEY_FRAMES || c.keyFrames[0] != 0) {
        throw EntryError("keyFrames needs 1.." + std::to_string(ANIM_MAT_MAX_KEY_FRAMES) + " entries starting at 0");
    }
    ReadByteTuples<F3DPrimColor, 5>(SubArray(e, K::kPrimColors), K::kPrimColors, c.primColors);
    ReadByteTuples<F3DEnvColor, 4>(SubArray(e, K::kEnvColors), K::kEnvColors, c.envColors);
    if (c.primColors.size() != c.keyFrames.size() ||
        (!c.envColors.empty() && c.envColors.size() != c.keyFrames.size())) {
        throw EntryError("primColors / envColors must have one entry per key frame");
    }
    return c;
}

// `frames` is the cycle modulus (1..65535 entries) and each index is stored 16-bit; both are SPEC.md §4.2 rules.
SetAnimatedMaterialList::CycleStorage ReadCycle(const Json& e) {
    SetAnimatedMaterialList::CycleStorage c{};
    for (const Json& t : SubArray(e, K::kTextures)) {
        if (!t.is_string()) {
            throw EntryError("textures holds a value that is not a path");
        }
        c.texturePaths.push_back("__OTR__" + t.get<std::string>());
    }
    if (c.texturePaths.size() > UINT16_MAX + 1) {
        throw EntryError("textures holds more than 65536 entries");
    }
    for (const Json& f : SubArray(e, K::kFrames)) {
        int64_t index = ToInt(f, -1);
        if (index < 0 || index >= (int64_t)c.texturePaths.size()) {
            throw EntryError("frames holds " + std::to_string(index) + ", which is not a textures index");
        }
        c.frames.push_back((u16)index);
    }
    if (c.frames.empty() || c.frames.size() > UINT16_MAX) {
        throw EntryError("frames needs 1..65535 entries");
    }
    return c;
}

void ReadMaterialAnim(CommandBuilder& b, SetAnimatedMaterialList& cmd, const Json& e) {
    const u8 segment = (u8)RangedField(e, K::kSegment, ANIM_MAT_SEGMENT_MIN, ANIM_MAT_SEGMENT_MAX, -1);
    const u8 pass = ReadAnimPass(e);
    const u8 type = ReadAnimType(e);
    switch (type) {
        case ANIM_MAT_TEX_SCROLL:
        case ANIM_MAT_TWO_TEX_SCROLL:
            cmd.AddScroll(segment, pass, type, ReadScroll(e, type));
            break;
        case ANIM_MAT_COLOR:
        case ANIM_MAT_COLOR_LERP:
        case ANIM_MAT_COLOR_NON_LINEAR:
            cmd.AddColor(segment, pass, type, ReadColor(e));
            break;
        default:
            cmd.AddCycle(segment, pass, ReadCycle(e));
            break;
    }
}

Command BuildMaterialAnims(CommandBuilder& b, const Json& list) {
    auto cmd = b.Make<SetAnimatedMaterialList>(SceneCommandID::SetAnimatedMaterialList);
    for (const auto& k : b.Positional(list, K::kMaterialAnims)) {
        try {
            ReadMaterialAnim(b, *cmd, list[k]);
        } catch (const EntryError& err) {
            SPDLOG_ERROR("[Unbound] {} {}[{}]: {}; entry dropped", b.docPath, K::kMaterialAnims, k, err.what());
        }
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
        d.pos = ReadVec3f(SubArray(entries[k], K::kPos));
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
    int64_t format = Field(m, K::kFormat, 1);
    if (format != 1 && format != 2) { // SPEC.md §4.3: the draw handler knows only these two
        throw Unbound::DocumentError(b.docPath + ": mesh format " + std::to_string(format) + " is not 1 or 2");
    }
    p1.format = (u8)format;
    if (p1.format == 1) {
        cmd.imagePaths.reserve(1);
        cmd.SetSingleImage(ReadBgImage(cmd, Sub(m, K::kImage)));
    } else {
        const Json& images = Sub(m, K::kImages);
        auto keys = b.Positional(images, K::kImages);
        if (keys.size() > 255) { // SPEC.md §4.3 / §9: the image count is a byte
            throw Unbound::DocumentError(b.docPath + ": mesh has " + std::to_string(keys.size()) +
                                         " images (at most 255)");
        }
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

Command BuildMesh(CommandBuilder& b, const Json& m) {
    auto cmd = b.Make<SetMesh>(SceneCommandID::SetMesh);
    int64_t type = Field(m, K::kType);
    cmd->data = 0;
    cmd->meshHeaderType = (uint8_t)type;
    cmd->meshHeader.base.type = (uint8_t)type;
    if (type == 0 || type == 2) {
        ReadMeshDlists(b, *cmd, Sub(m, K::kEntries), (uint8_t)type);
    } else if (type == 1) {
        ReadMeshBackground(b, *cmd, m);
    } else { // SPEC.md §4.3: the draw handler table has three entries
        throw Unbound::DocumentError(b.docPath + ": mesh type " + std::to_string(type) + " is not 0, 1 or 2");
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
    add(K::kMaterialAnims, BuildMaterialAnims);
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
        out.push_back(BuildMesh(b, setup[K::kMesh]));
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
    std::shared_ptr<Scene> scene;
    try {
        scene = BuildScene(initData, doc);
    } catch (const Unbound::DocumentError& e) {
        SPDLOG_ERROR("[Unbound] {}", e.what());
        return nullptr;
    } catch (const std::exception& e) { // a JSON value of an unexpected shape must not take the process down
        SPDLOG_ERROR("[Unbound] {}: {}", initData->Path, e.what());
        return nullptr;
    }
    if (scene != nullptr) {
        SPDLOG_DEBUG("[Unbound] {}: built from JSON ({} setups, {} commands in setup 0)", initData->Path,
                     Sub(doc, K::kSetups).size(), scene->commands.size());
    }
    return scene;
}

} // namespace SOH
