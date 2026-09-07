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
constexpr int64_t kMaxEntranceIndex = INT16_MAX + 1 - kEntranceLayerCount;

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

    int32_t id = init.sceneId >= 0 ? init.sceneId : nextSceneId;
    if (id < CUSTOM_SCENE_ID_BASE || id > kMaxSceneId) {
        SPDLOG_ERROR("[Unbound] scene '{}' requests id {:#x}; must be {:#x}-{}", init.name, id, CUSTOM_SCENE_ID_BASE,
                     kMaxSceneId);
        return invalid;
    }
    if (id < (int32_t)db.size() && db[id].valid) {
        SPDLOG_ERROR("[Unbound] scene '{}' requests id {:#x} already taken by '{}'", init.name, id, db[id].name);
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

    int32_t index = init.index >= 0 ? init.index : nextEntranceIndex;
    if (index < ENTR_MAX || index > kMaxEntranceIndex || index % kEntranceLayerCount != 0) {
        SPDLOG_ERROR("[Unbound] entrance '{}' requests index {:#x}; must be {:#x}-{} and a multiple of {}", init.name,
                     index, (int)ENTR_MAX, kMaxEntranceIndex, kEntranceLayerCount);
        return -1;
    }
    if (index < (int32_t)entranceTable.size() && entranceTable[index].scene != SCENE_ID_MAX) {
        SPDLOG_ERROR("[Unbound] entrance '{}' requests index {:#x} which is already taken", init.name, index);
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

// Sentinel for "no explicit value" in the registry; every legal explicit id/index is >= 0.
constexpr int64_t kNextFree = INT64_MIN;
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
    int64_t sceneId = Field(def, K::kSceneId, kNextFree);
    int64_t drawConfig = Field(def, K::kDrawConfig);
    if (scene.scenePath.empty()) {
        SPDLOG_ERROR("[Unbound] {}: scene '{}' has no \"{}\" path", K::kRegistryPath, id, K::kScene);
        return false;
    }
    if (sceneId != kNextFree && (sceneId < CUSTOM_SCENE_ID_BASE || sceneId > kMaxSceneId)) {
        SPDLOG_ERROR("[Unbound] scene '{}' requests id {}; must be {:#x}-{} (SPEC.md §7)", id, sceneId,
                     CUSTOM_SCENE_ID_BASE, kMaxSceneId);
        return false;
    }
    if (drawConfig < 0 || drawConfig >= SDC_MAX) {
        SPDLOG_ERROR("[Unbound] scene '{}' requests draw config {} (max {})", id, drawConfig, SDC_MAX - 1);
        return false;
    }
    scene.sceneId = sceneId == kNextFree ? -1 : (int32_t)std::min<int64_t>(sceneId, INT32_MAX);
    scene.drawConfig = (uint8_t)drawConfig;

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
    int64_t index = Field(def, K::kIndex, kNextFree);
    if (index != kNextFree && (index < ENTR_MAX || index > kMaxEntranceIndex)) {
        SPDLOG_ERROR("[Unbound] entrance '{}/{}' requests index {}; must be {:#x}-{} and a multiple of {}", scene.name,
                     key, index, (int)ENTR_MAX, kMaxEntranceIndex, kEntranceLayerCount);
        return;
    }
    CustomEntranceInit entrance;
    entrance.name = scene.name + "/" + key;
    entrance.index = index == kNextFree ? -1 : (int32_t)std::min<int64_t>(index, INT32_MAX);
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

void SaveUnboundSection(SaveContext* saveContext, int sectionID, bool fullSave) {
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
