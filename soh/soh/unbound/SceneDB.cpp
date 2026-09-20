// SOH [Unbound] Dynamic scene + entrance registry. See unbound-docs/registries.md.
#include "SceneDB.h"
#include "global.h" // gSaveContext

#include <libultraship/libultraship.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <ship/utils/StringHelper.h>

#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/SaveManager.h"
#include "soh/unbound/UnboundJson.h"
#include "soh/unbound/UnboundSchema.h"
#include "soh/util.h"

extern "C" {
EntranceInfo* gEntranceTable = nullptr;
}

SceneDB* SceneDB::Instance = new SceneDB();

// ---- vanilla seeds (the X-macro tables are kept only as seed data) ------------------------------

namespace {
// Scene ids and entrance indices travel in s16 engine fields (EntranceInfo.scene, exit lists): SPEC.md §9.
constexpr int64_t kMaxSceneId = INT16_MAX;

struct VanillaScene {
    const char* fileName;
    const char* enumName;
    uint8_t drawConfig;
};

#define none ""
#define DEFINE_SCENE(name, title, enumValue, config, unk_10, unk_12) { #name, #enumValue, config },
const VanillaScene sVanillaScenes[] = {
#include "tables/scene_table.h"
};
#undef DEFINE_SCENE
#undef none

// SOH [Unbound] Vanilla's horse scenes, formerly the allow-list inside func_8006CFC0 in z_horse.c. Seeding
// them here makes SceneDB_HorseAllowed the single answer for vanilla and custom scenes alike.
constexpr int16_t sVanillaHorseScenes[] = { SCENE_HYRULE_FIELD, SCENE_LAKE_HYLIA, SCENE_GERUDO_VALLEY,
                                            SCENE_GERUDOS_FORTRESS, SCENE_LON_LON_RANCH };

struct VanillaEntrance {
    const char* name;
    int16_t scene;
    int8_t spawn;
    bool continueBgm;
    bool displayTitleCard;
    uint8_t endTransType;
    uint8_t startTransType;
};

#define DEFINE_ENTRANCE(enumValue, sceneId, spawn, continueBgm, displayTitleCard, endTransType, startTransType) \
    { #enumValue, sceneId, spawn, continueBgm, displayTitleCard, endTransType, startTransType },
const VanillaEntrance sVanillaEntrances[] = {
#include "tables/entrance_table.h"
};
#undef DEFINE_ENTRANCE

uint16_t PackEntranceField(bool continueBgm, bool displayTitleCard, uint8_t endTransType, uint8_t startTransType) {
    return (continueBgm ? ENTRANCE_INFO_CONTINUE_BGM_FLAG : 0) |
           (displayTitleCard ? ENTRANCE_INFO_DISPLAY_TITLE_CARD_FLAG : 0) |
           ((endTransType << ENTRANCE_INFO_END_TRANS_TYPE_SHIFT) & ENTRANCE_INFO_END_TRANS_TYPE_MASK) |
           ((startTransType << ENTRANCE_INFO_START_TRANS_TYPE_SHIFT) & ENTRANCE_INFO_START_TRANS_TYPE_MASK);
}

constexpr int32_t kEntranceLayerCount = 4; // child day/night, adult day/night
// The last group that is wholly below ENTR_RETURN_YOUSEI_IZUMI_YOKO (0x7FF9). z_player.c reads any exit
// value from there up as a dynamic return entrance before it ever indexes the table, so a group that
// reached into that range would hand out entrances no exit could name.
constexpr int64_t kMaxEntranceIndex =
    (ENTR_RETURN_YOUSEI_IZUMI_YOKO - kEntranceLayerCount) / kEntranceLayerCount * kEntranceLayerCount;

// Saved flags for custom scenes; vanilla ids live in gSaveContext.sceneFlags.
std::unordered_map<int32_t, SavedSceneFlags> sCustomSceneFlags;

// Room-keyed flags for rooms >= 32 (see SceneFlagsExt_* in SceneDB.h). Bit n of the bitset is room n;
// bits below 32 are never used here (they live in the u32 masks).
// Clear flags are staged the way ActorContext.flags stages the u32 masks: SceneFlagsExt_LoadClear copies the
// persisted bits into the live map on scene init and SceneFlagsExt_SaveClear commits them back, so a game over or
// save-state restore discards unsaved flags for every room number alike.
using ExtBitset = std::vector<uint32_t>;
std::unordered_map<int32_t, ExtBitset> sExtClearFlags;     // persisted
std::unordered_map<int32_t, ExtBitset> sExtLiveClearFlags; // current play state
std::unordered_map<int32_t, ExtBitset> sExtTempClearFlags; // live-only

std::unordered_map<int32_t, ExtBitset>& ExtFlagMap(SceneFlagsExtKind kind) {
    return kind == SCENE_FLAGS_EXT_TEMP_CLEAR ? sExtTempClearFlags : sExtLiveClearFlags;
}

bool ExtBitTest(const ExtBitset& bits, int32_t bit) {
    size_t word = (size_t)bit / 32;
    return word < bits.size() && (bits[word] & (1u << (bit % 32)));
}

void ExtBitWrite(ExtBitset& bits, int32_t bit, bool value) {
    size_t word = (size_t)bit / 32;
    if (word >= bits.size()) {
        if (!value) {
            return;
        }
        bits.resize(word + 1, 0);
    }
    if (value) {
        bits[word] |= (1u << (bit % 32));
    } else {
        bits[word] &= ~(1u << (bit % 32));
    }
}

} // namespace

// ---- construction ---------------------------------------------------------------------------------

SceneDB::SceneDB() {
    SeedVanillaScenes();
    SeedVanillaEntrances();
}

void SceneDB::SeedVanillaScenes() {
    db.resize(SCENE_ID_MAX);
    for (int32_t id = 0; id < SCENE_ID_MAX; id++) {
        Entry& entry = db[id];
        entry.id = id;
        entry.valid = true;
        entry.isCustom = false;
        entry.name = sVanillaScenes[id].enumName;
        entry.displayName = entry.name; // replaced by the pretty name in SeedVanillaDisplayNames
        entry.sceneFileName = sVanillaScenes[id].fileName;
        entry.drawConfig = sVanillaScenes[id].drawConfig;
        entry.horse.allowed = std::find(std::begin(sVanillaHorseScenes), std::end(sVanillaHorseScenes), id) !=
                              std::end(sVanillaHorseScenes);
        nameTable[entry.name] = id;
    }
}

// The constructor runs during static initialisation, before SohUtils' name table is guaranteed to exist,
// so the pretty names are filled in from LoadCustomScenes (the first runtime entry point).
void SceneDB::SeedVanillaDisplayNames() {
    for (int32_t id = 0; id < SCENE_ID_MAX; id++) {
        db[id].displayName = SohUtils::GetSceneName(id);
    }
}

void SceneDB::SeedVanillaEntrances() {
    entranceTable.reserve(ENTR_MAX + 256);
    for (int32_t index = 0; index < ENTR_MAX; index++) {
        const VanillaEntrance& v = sVanillaEntrances[index];
        EntranceInfo info;
        info.scene = v.scene;
        info.spawn = v.spawn;
        info.field = PackEntranceField(v.continueBgm, v.displayTitleCard, v.endTransType, v.startTransType);
        entranceTable.push_back(info);
        entranceNameTable[v.name] = index;
    }
    nextEntranceIndex = (ENTR_MAX + kEntranceLayerCount - 1) / kEntranceLayerCount * kEntranceLayerCount;
    RefreshEntranceTablePointer();
}

void SceneDB::RefreshEntranceTablePointer() {
    gEntranceTable = entranceTable.data();
}

// ---- registration ---------------------------------------------------------------------------------

SceneDB::Entry& SceneDB::AddCustomScene(const CustomSceneInit& init) {
    static Entry invalid;

    if (init.name.empty() || nameTable.contains(init.name)) {
        SPDLOG_ERROR("[Unbound] scene '{}' is unnamed or already registered", init.name);
        return invalid;
    }

    int32_t id = nextSceneId;
    if (id > kMaxSceneId) {
        SPDLOG_ERROR("[Unbound] scene '{}' has no free scene id left; {} is the last one", init.name, kMaxSceneId);
        return invalid;
    }
    if (init.drawConfig >= SDC_MAX) {
        SPDLOG_ERROR("[Unbound] scene '{}' requests draw config {} (max {})", init.name, init.drawConfig, SDC_MAX - 1);
        return invalid;
    }

    if (id >= (int32_t)db.size()) {
        db.resize(id + 1);
    }
    Entry& entry = db[id];
    entry.id = id;
    entry.valid = true;
    entry.isCustom = true;
    entry.name = init.name;
    entry.displayName = init.displayName.empty() ? init.name : init.displayName;
    entry.scenePath = init.scenePath;
    entry.titleCardTexture = init.titleCardTexture;
    entry.drawConfig = init.drawConfig;
    entry.horse = init.horse;
    nameTable[entry.name] = id;
    nextSceneId = std::max(nextSceneId, id + 1);
    return entry;
}

void SceneDB::AddEntranceLayerGroup(int32_t index, const EntranceInfo& info) {
    if ((int32_t)entranceTable.size() < index + kEntranceLayerCount) {
        EntranceInfo unused = { SCENE_ID_MAX, 0, 0 };
        entranceTable.resize(index + kEntranceLayerCount, unused);
    }
    for (int32_t layer = 0; layer < kEntranceLayerCount; layer++) {
        entranceTable[index + layer] = info;
    }
    RefreshEntranceTablePointer();
}

int32_t SceneDB::AddCustomEntrance(const CustomEntranceInit& init) {
    if (init.name.empty() || entranceNameTable.contains(init.name)) {
        SPDLOG_ERROR("[Unbound] entrance '{}' is unnamed or already registered", init.name);
        return -1;
    }
    if (init.sceneId < 0 || init.sceneId >= (int32_t)db.size() || !db[init.sceneId].valid) {
        SPDLOG_ERROR("[Unbound] entrance '{}' targets unknown scene id {}", init.name, init.sceneId);
        return -1;
    }

    int32_t index = nextEntranceIndex;
    if (index > kMaxEntranceIndex) {
        SPDLOG_ERROR("[Unbound] entrance '{}' has no free entrance-table group left; {:#x} is the last one", init.name,
                     kMaxEntranceIndex);
        return -1;
    }

    EntranceInfo info;
    info.scene = (s16)init.sceneId;
    info.spawn = init.spawn;
    info.field = PackEntranceField(init.continueBgm, init.displayTitleCard, init.endTransType, init.startTransType);
    AddEntranceLayerGroup(index, info);

    entranceNameTable[init.name] = index;
    customEntrances.push_back({ init.name, index, init.sceneId });
    nextEntranceIndex = std::max(nextEntranceIndex, index + kEntranceLayerCount);
    return index;
}

// ---- lookup ---------------------------------------------------------------------------------------

SceneDB::Entry& SceneDB::RetrieveEntry(int32_t id) {
    static Entry invalid;
    if (id < 0 || id >= (int32_t)db.size() || !db[id].valid) {
        return invalid;
    }
    return db[id];
}

const SceneDB::Entry& SceneDB::RetrieveEntry(int32_t id) const {
    return const_cast<SceneDB*>(this)->RetrieveEntry(id);
}

int32_t SceneDB::RetrieveId(const std::string& name) const {
    auto it = nameTable.find(name);
    return it == nameTable.end() ? -1 : it->second;
}

size_t SceneDB::GetEntryCount() const {
    return db.size();
}

const std::vector<SceneDB::Entry>& SceneDB::Entries() const {
    return db;
}

int32_t SceneDB::RetrieveEntranceIndex(const std::string& name) const {
    auto it = entranceNameTable.find(name);
    return it == entranceNameTable.end() ? -1 : it->second;
}

size_t SceneDB::GetEntranceCount() const {
    return entranceTable.size();
}

const std::vector<SceneDB::EntranceEntry>& SceneDB::CustomEntrances() const {
    return customEntrances;
}

std::string SceneDB::GetScenePath(int32_t id) const {
    return GetScenePath(id, ResourceMgr_IsGameMasterQuest());
}

std::string SceneDB::GetScenePath(int32_t id, bool masterQuest) const {
    const Entry& entry = RetrieveEntry(id);
    if (!entry.valid) {
        return "";
    }
    if (entry.isCustom) {
        return entry.scenePath;
    }

    // Vanilla dungeons with a Master Quest variant live under mq/ or nonmq/; everything else is shared.
    bool hasMqVariant = (id >= SCENE_DEKU_TREE && id <= SCENE_ICE_CAVERN) || id == SCENE_GERUDO_TRAINING_GROUND ||
                        id == SCENE_INSIDE_GANONS_CASTLE;
    bool useMq = hasMqVariant && masterQuest;
    if (unboundBase) {
        // scenes/<leaf minus _scene>[_mq]/scene.json (unbound-docs/SPEC.md §4.1)
        std::string dir = entry.sceneFileName;
        const std::string suffix = "_scene";
        if (dir.ends_with(suffix)) {
            dir.resize(dir.size() - suffix.size());
        }
        return "scenes/" + dir + (useMq ? "_mq" : "") + "/scene.json";
    }
    const char* sceneVersion = hasMqVariant ? (useMq ? "mq" : "nonmq") : "shared";
    return StringHelper::Sprintf("scenes/%s/%s/%s", sceneVersion, entry.sceneFileName.c_str(),
                                 entry.sceneFileName.c_str());
}

// ---- custom scene files ---------------------------------------------------------------------------

bool SceneDB::HasUnboundBase() const {
    return unboundBase;
}

namespace {

namespace K = SOH::Unbound::Schema;
using SOH::Unbound::Field;
using SOH::Unbound::Json;

// A pinned scene id or entrance index is no longer read: the number a custom scene or entrance gets
// depends on which mods are mounted and in what order, so two mods that pinned the same one used to
// collide and the loser vanished. Everything addresses them by name now (SPEC.md §7), and the game
// hands out the numbers itself. Older mods still carry the keys, so say plainly that they do nothing.
void WarnNumberIgnored(const Json& def, const char* key, const std::string& owner) {
    if (def.contains(key)) {
        SPDLOG_WARN("[Unbound] '{}': \"{}\" is deprecated and ignored; the game assigns the number and "
                    "everything addresses this by name (SPEC.md §7)",
                    owner, key);
    }
}
// The manifest "features" entry that marks a base layer (SPEC.md §1.3).
constexpr const char* kFeatureScenes = "scenes";

// unbound.json (SPEC.md §6): a layer's manifest must name the one format version this build reads — older
// (pre-release) and newer archives alike are refused. A layer is a base — the one that provides vanilla
// scenes as scene.json — when its "features" list "scenes" (§1.3).
bool ManifestVersionIsReadable(const Json& doc) {
    int64_t version = Field(doc, K::kFormatVersion, K::kCurrentFormatVersion);
    int64_t required = Field(SOH::Unbound::Sub(doc, K::kRequires), K::kFormatVersion, version);
    if (version != K::kCurrentFormatVersion || required != K::kCurrentFormatVersion) {
        SPDLOG_ERROR("[Unbound] {}: format version {} (requires {}); this build reads only {}; layer ignored",
                     K::kManifestPath, version, required, K::kCurrentFormatVersion);
        return false;
    }
    return true;
}

bool ManifestProvidesScenes(const Json& doc) {
    for (const auto& feature : SOH::Unbound::SubArray(doc, K::kFeatures)) {
        if (feature.is_string() && feature.get<std::string>() == kFeatureScenes) {
            return true;
        }
    }
    return false;
}

// Returns true when at least one readable base manifest is present.
bool DetectUnboundBase() {
    auto archiveManager = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    bool base = false;
    for (const auto& file : archiveManager->LoadFileFromAllLayers(K::kManifestPath)) {
        Json doc;
        try {
            doc = Json::parse(file->Buffer->begin(), file->Buffer->end(), nullptr, true, true);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[Unbound] {}: invalid JSON: {}", K::kManifestPath, e.what());
            continue;
        }
        if (!doc.is_object()) {
            SPDLOG_ERROR("[Unbound] {}: manifest is not a JSON object", K::kManifestPath);
            continue;
        }
        if (ManifestVersionIsReadable(doc) && ManifestProvidesScenes(doc)) {
            base = true;
        }
    }
    return base;
}

// The registry's "horse" key (SPEC.md §7): an object whose presence lets Epona into the scene and whose
// optional "pos"/"angle" is where she waits, or a bare boolean for "allowed, with nowhere to wait".
SceneDB::Horse ReadHorse(const std::string& id, const Json& def) {
    SceneDB::Horse horse;
    if (!def.contains(K::kHorse)) {
        return horse;
    }
    const Json& value = def[K::kHorse];
    if (!value.is_object()) {
        horse.allowed = SOH::Unbound::ToInt(value) != 0;
        return horse;
    }
    horse.allowed = true;
    horse.angle = (int16_t)Field(value, K::kAngle);
    if (value.contains(K::kPos)) {
        const Json& pos = value[K::kPos];
        if (!pos.is_array() || pos.size() < 3) {
            // ReadVec3f would hand back {0, 0, 0} and park her at the origin; no idle spot is better than a
            // wrong one, and the scene still allows her.
            SPDLOG_ERROR("[Unbound] scene '{}': \"{}\".\"{}\" is not an [x, y, z] array; ignoring it", id, K::kHorse,
                         K::kPos);
        } else {
            horse.pos = SOH::Unbound::ReadVec3f(pos);
            horse.hasSpawn = true;
        }
    }
    return horse;
}

} // namespace

void SceneDB::LoadCustomScenes() {
    SeedVanillaDisplayNames();
    unboundBase = DetectUnboundBase();
    if (unboundBase) {
        SPDLOG_INFO("[Unbound] Unbound-format archive mounted; vanilla scenes load from scene.json");
    }

    Json registry = SOH::Unbound::LoadMergedJson(K::kRegistryPath);
    if (!registry.is_object()) {
        return;
    }
    size_t loaded = 0;
    for (const auto& id : SOH::Unbound::ListKeys(registry)) {
        try {
            if (registry[id].is_object() && RegisterScene(id, registry[id])) {
                loaded++;
            }
        } catch (const nlohmann::json::exception& e) {
            SPDLOG_ERROR("[Unbound] {}: scene '{}': {}", K::kRegistryPath, id, e.what());
        }
    }
    SPDLOG_INFO("[Unbound] {}: registered {} custom scene(s), {} custom entrance(s)", K::kRegistryPath, loaded,
                customEntrances.size());
}

// One entry of unbound/scenes.json (SPEC.md §7), keyed by the scene id.
bool SceneDB::RegisterScene(const std::string& id, const nlohmann::json& def) {
    CustomSceneInit scene;
    scene.name = id;
    scene.displayName = def.contains(K::kName) && def[K::kName].is_string() ? def[K::kName].get<std::string>() : id;
    scene.scenePath = SOH::Unbound::PathField(def, K::kScene);
    scene.titleCardTexture = SOH::Unbound::PathField(def, K::kTitleCardTexture);
    int64_t drawConfig = Field(def, K::kDrawConfig);
    if (scene.scenePath.empty()) {
        SPDLOG_ERROR("[Unbound] {}: scene '{}' has no \"{}\" path", K::kRegistryPath, id, K::kScene);
        return false;
    }
    WarnNumberIgnored(def, K::kSceneId, id);
    if (drawConfig < 0 || drawConfig >= SDC_MAX) {
        SPDLOG_ERROR("[Unbound] scene '{}' requests draw config {} (max {})", id, drawConfig, SDC_MAX - 1);
        return false;
    }
    scene.drawConfig = (uint8_t)drawConfig;
    scene.horse = ReadHorse(id, def);

    Entry& entry = AddCustomScene(scene);
    if (!entry.valid) {
        return false;
    }
    const Json& entrances = SOH::Unbound::Sub(def, K::kEntrances);
    for (const auto& key : SOH::Unbound::ListKeys(entrances)) {
        if (entrances[key].is_object()) {
            RegisterEntrance(entry, key, entrances[key]);
        }
    }
    SPDLOG_INFO("[Unbound] scene '{}' -> id {:#x}", entry.name, entry.id);
    return true;
}

void SceneDB::RegisterEntrance(const Entry& scene, const std::string& key, const nlohmann::json& def) {
    if (def.contains(K::kLayers)) {
        SPDLOG_WARN("[Unbound] {}/{}: \"{}\" is reserved and not read yet; all four layers are identical", scene.name,
                    key, K::kLayers);
    }
    CustomEntranceInit entrance;
    entrance.name = scene.name + "/" + key;
    WarnNumberIgnored(def, K::kIndex, entrance.name);
    entrance.sceneId = scene.id;
    entrance.spawn = (int8_t)Field(def, K::kSpawn);
    entrance.continueBgm = Field(def, K::kContinueBgm) != 0;
    entrance.displayTitleCard = Field(def, K::kShowTitleCard) != 0;
    entrance.endTransType = (uint8_t)Field(def, K::kEndTransition, 2);
    entrance.startTransType = (uint8_t)Field(def, K::kStartTransition, 2);
    AddCustomEntrance(entrance);
}

// ---- save integration -----------------------------------------------------------------------------

namespace {

// horseData.scene is a numeric id, and a custom one is only stable across sessions when the registry
// assigned it explicitly. The name is saved alongside it and wins on load, the same way the scene flags
// below are keyed by name. It reads the snapshot SaveSection took rather than gSaveContext: the write runs
// on a worker thread, and a name that disagreed with the id the "base" section wrote from the same snapshot
// would win on load and move her.
void SaveHorseScene(const SaveContext* saveContext) {
    const SceneDB::Entry& entry = SceneDB::Instance->RetrieveEntry(saveContext->horseData.scene);
    SaveManager::Instance->SaveData("horseScene", entry.valid && entry.isCustom ? entry.name : std::string());
}

// Runs after the numeric id the "base" section loaded: save sections live in a std::map keyed by name, so
// "base" is always read before "unbound".
void LoadHorseScene() {
    std::string name;
    SaveManager::Instance->LoadData("horseScene", name);
    if (name.empty()) {
        return;
    }
    int32_t id = SceneDB::Instance->RetrieveId(name);
    if (id < 0) {
        // The mod that owned the scene is gone; z_horse.c moves her back to her Hyrule Field default.
        SPDLOG_WARN("[Unbound] the horse is parked in scene '{}', which is not registered", name);
        return;
    }
    gSaveContext.horseData.scene = (s16)id;
}

void SaveUnboundSection(SaveContext* saveContext, int sectionID, bool fullSave) {
    SaveHorseScene(saveContext);
    SaveManager::Instance->SaveStruct("sceneFlags", []() {
        for (const auto& [id, flags] : sCustomSceneFlags) {
            const SceneDB::Entry& entry = SceneDB::Instance->RetrieveEntry(id);
            if (!entry.valid) {
                continue;
            }
            SaveManager::Instance->SaveStruct(entry.name, [&flags]() {
                SaveManager::Instance->SaveData("chest", flags.chest);
                SaveManager::Instance->SaveData("swch", flags.swch);
                SaveManager::Instance->SaveData("clear", flags.clear);
                SaveManager::Instance->SaveData("collect", flags.collect);
                SaveManager::Instance->SaveData("unk", flags.unk);
                SaveManager::Instance->SaveData("rooms", flags.rooms);
                SaveManager::Instance->SaveData("floors", flags.floors);
            });
        }
    });
    // Rooms >= 32: one word array per scene, keyed by name so it survives id reassignment.
    SaveManager::Instance->SaveStruct("roomClearExt", []() {
        for (const auto& [id, bits] : sExtClearFlags) {
            const SceneDB::Entry& entry = SceneDB::Instance->RetrieveEntry(id);
            if (!entry.valid || bits.empty()) {
                continue;
            }
            SaveManager::Instance->SaveStruct(entry.name, [&bits]() {
                SaveManager::Instance->SaveData("words", (uint32_t)bits.size());
                SaveManager::Instance->SaveArray("bits", bits.size(),
                                                 [&bits](size_t i) { SaveManager::Instance->SaveData("", bits[i]); });
            });
        }
    });
}

void LoadExtClearFlags() {
    SaveManager::Instance->LoadStruct("roomClearExt", []() {
        for (const auto& entry : SceneDB::Instance->Entries()) {
            if (!entry.valid) {
                continue;
            }
            ExtBitset bits;
            SaveManager::Instance->LoadStruct(entry.name, [&bits]() {
                uint32_t words = 0;
                SaveManager::Instance->LoadData("words", words);
                bits.assign(words, 0);
                SaveManager::Instance->LoadArray("bits", words,
                                                 [&bits](size_t i) { SaveManager::Instance->LoadData("", bits[i]); });
            });
            if (!bits.empty()) {
                sExtClearFlags[entry.id] = std::move(bits);
            }
        }
    });
}

void LoadUnboundSection() {
    SaveManager::Instance->LoadStruct("sceneFlags", []() {
        for (const auto& entry : SceneDB::Instance->Entries()) {
            if (!entry.valid || !entry.isCustom) {
                continue;
            }
            SavedSceneFlags& flags = sCustomSceneFlags[entry.id];
            SaveManager::Instance->LoadStruct(entry.name, [&flags]() {
                SaveManager::Instance->LoadData("chest", flags.chest);
                SaveManager::Instance->LoadData("swch", flags.swch);
                SaveManager::Instance->LoadData("clear", flags.clear);
                SaveManager::Instance->LoadData("collect", flags.collect);
                SaveManager::Instance->LoadData("unk", flags.unk);
                SaveManager::Instance->LoadData("rooms", flags.rooms);
                SaveManager::Instance->LoadData("floors", flags.floors);
            });
        }
    });
    LoadExtClearFlags();
    LoadHorseScene();
}

void InitUnboundSection(bool isDebug) {
    sCustomSceneFlags.clear();
    sExtClearFlags.clear();
    sExtLiveClearFlags.clear();
    sExtTempClearFlags.clear();
}

} // namespace

void SceneDB_RegisterSaveFunctions(SaveManager& saveManager) {
    // Called from SaveManager's constructor, so SaveManager::Instance is not set yet; use the reference.
    saveManager.AddLoadFunction("unbound", 1, LoadUnboundSection);
    saveManager.AddSaveFunction("unbound", 1, SaveUnboundSection, true, SECTION_PARENT_NONE);
    saveManager.AddInitFunction(InitUnboundSection);
}

// ---- C API ------------------------------------------------------------------------------------------

extern "C" int32_t SceneDB_IsValid(int32_t id) {
    return SceneDB::Instance->RetrieveEntry(id).valid;
}

extern "C" int32_t SceneDB_IsCustom(int32_t id) {
    const SceneDB::Entry& entry = SceneDB::Instance->RetrieveEntry(id);
    return entry.valid && entry.isCustom;
}

extern "C" int32_t SceneDB_HorseAllowed(int32_t id) {
    const SceneDB::Entry& entry = SceneDB::Instance->RetrieveEntry(id);
    return entry.valid && entry.horse.allowed;
}

extern "C" int32_t SceneDB_GetHorseSpawn(int32_t id, Vec3f* pos, int16_t* angle) {
    const SceneDB::Entry& entry = SceneDB::Instance->RetrieveEntry(id);
    if (!entry.valid || !entry.horse.allowed || !entry.horse.hasSpawn) {
        return false;
    }
    *pos = entry.horse.pos;
    *angle = entry.horse.angle;
    return true;
}

extern "C" int32_t SceneDB_GetEntryCount(void) {
    return (int32_t)SceneDB::Instance->GetEntryCount();
}

extern "C" uint8_t SceneDB_GetDrawConfig(int32_t id) {
    return SceneDB::Instance->RetrieveEntry(id).drawConfig;
}

extern "C" const char* SceneDB_GetDisplayName(int32_t id) {
    return SceneDB::Instance->RetrieveEntry(id).displayName.c_str();
}

extern "C" const char* SceneDB_GetTitleCardTexture(int32_t id) {
    const SceneDB::Entry& entry = SceneDB::Instance->RetrieveEntry(id);
    if (!entry.valid || entry.titleCardTexture.empty()) {
        return nullptr;
    }
    return entry.titleCardTexture.c_str();
}

extern "C" int32_t EntranceDB_GetEntryCount(void) {
    return (int32_t)SceneDB::Instance->GetEntranceCount();
}

extern "C" int32_t EntranceDB_RetrieveIndex(const char* name) {
    return SceneDB::Instance->RetrieveEntranceIndex(name);
}

extern "C" SavedSceneFlags* SceneFlags_Get(int32_t sceneNum) {
    if (sceneNum >= 0 && sceneNum < SCENE_ID_MAX) {
        return &gSaveContext.sceneFlags[sceneNum];
    }
    if (SceneDB::Instance->RetrieveEntry(sceneNum).valid) {
        return &sCustomSceneFlags[sceneNum]; // value-initialised (all zero) on first access
    }
    // Unregistered id: hand back scratch storage rather than minting a save entry nothing can name.
    static SavedSceneFlags scratch;
    SPDLOG_ERROR("[Unbound] SceneFlags_Get: scene id {} is not registered", sceneNum);
    scratch = SavedSceneFlags{};
    return &scratch;
}

extern "C" int32_t SceneFlagsExt_Get(int32_t sceneNum, SceneFlagsExtKind kind, int32_t bit) {
    auto& map = ExtFlagMap(kind);
    auto it = map.find(sceneNum);
    return it != map.end() && ExtBitTest(it->second, bit);
}

extern "C" void SceneFlagsExt_Set(int32_t sceneNum, SceneFlagsExtKind kind, int32_t bit) {
    ExtBitWrite(ExtFlagMap(kind)[sceneNum], bit, true);
}

extern "C" void SceneFlagsExt_Unset(int32_t sceneNum, SceneFlagsExtKind kind, int32_t bit) {
    ExtBitWrite(ExtFlagMap(kind)[sceneNum], bit, false);
}

extern "C" void SceneFlagsExt_LoadClear(int32_t sceneNum) {
    sExtLiveClearFlags.clear();
    sExtTempClearFlags.clear();
    auto it = sExtClearFlags.find(sceneNum);
    if (it != sExtClearFlags.end()) {
        sExtLiveClearFlags[sceneNum] = it->second;
    }
}

extern "C" void SceneFlagsExt_SaveClear(int32_t sceneNum) {
    auto it = sExtLiveClearFlags.find(sceneNum);
    if (it != sExtLiveClearFlags.end()) {
        sExtClearFlags[sceneNum] = it->second;
    } else {
        sExtClearFlags.erase(sceneNum);
    }
}
