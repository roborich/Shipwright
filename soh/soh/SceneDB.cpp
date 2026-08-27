// SOH [Unbound] Dynamic scene + entrance registry. See unbound-docs/registries.md.
#include "SceneDB.h"
#include "global.h" // gSaveContext

#include <libultraship/libultraship.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ship/utils/StringHelper.h>

#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/SaveManager.h"
#include "soh/util.h"

extern "C" EntranceInfo* gEntranceTable = nullptr;

SceneDB* SceneDB::Instance = new SceneDB();

// ---- vanilla seeds (the X-macro tables are kept only as seed data) ------------------------------

namespace {

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

constexpr const char* kCustomSceneGlob = "unbound/scenes/*";
constexpr int32_t kEntranceLayerCount = 4; // child day/night, adult day/night

// Saved flags for custom scenes; vanilla ids live in gSaveContext.sceneFlags.
std::unordered_map<int32_t, SavedSceneFlags> sCustomSceneFlags;

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
        // Static-init safe: no cross-TU globals here. SohUtils::GetSceneName covers vanilla display names.
        entry.displayName = entry.name;
        entry.sceneFileName = sVanillaScenes[id].fileName;
        entry.drawConfig = sVanillaScenes[id].drawConfig;
        nameTable[entry.name] = id;
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
    if (id < CUSTOM_SCENE_ID_BASE) {
        SPDLOG_ERROR("[Unbound] scene '{}' requests id {:#x} below the custom base {:#x}", init.name, id,
                     CUSTOM_SCENE_ID_BASE);
        return invalid;
    }
    if (id < (int32_t)db.size() && db[id].valid) {
        SPDLOG_ERROR("[Unbound] scene '{}' requests id {:#x} already taken by '{}'", init.name, id, db[id].name);
        return invalid;
    }
    if (init.drawConfig >= SDC_MAX) {
        SPDLOG_ERROR("[Unbound] scene '{}' requests draw config {} (max {})", init.name, init.drawConfig,
                     SDC_MAX - 1);
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
    if (index < ENTR_MAX || index % kEntranceLayerCount != 0) {
        SPDLOG_ERROR("[Unbound] entrance '{}' requests index {:#x}; must be >= {:#x} and a multiple of {}",
                     init.name, index, (int)ENTR_MAX, kEntranceLayerCount);
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
    if (id < 0 || id >= (int32_t)db.size() || !db[id].valid) {
        return "";
    }
    const Entry& entry = db[id];
    if (entry.isCustom) {
        return entry.scenePath;
    }

    // Vanilla dungeons with a Master Quest variant live under mq/ or nonmq/; everything else is shared.
    bool hasMqVariant = (id >= SCENE_DEKU_TREE && id <= SCENE_ICE_CAVERN) || id == SCENE_GERUDO_TRAINING_GROUND ||
                        id == SCENE_INSIDE_GANONS_CASTLE;
    if (unboundBase) {
        // scenes/<leaf minus _scene>[_mq]/scene.json (unbound-docs/scene-format.md §1)
        std::string dir = entry.sceneFileName;
        const std::string suffix = "_scene";
        if (dir.size() > suffix.size() && dir.compare(dir.size() - suffix.size(), suffix.size(), suffix) == 0) {
            dir.resize(dir.size() - suffix.size());
        }
        if (hasMqVariant && ResourceMgr_IsGameMasterQuest()) {
            dir += "_mq";
        }
        return "scenes/" + dir + "/scene.json";
    }
    const char* sceneVersion = "shared";
    if (hasMqVariant) {
        sceneVersion = ResourceMgr_IsGameMasterQuest() ? "mq" : "nonmq";
    }
    return StringHelper::Sprintf("scenes/%s/%s/%s", sceneVersion, entry.sceneFileName.c_str(),
                                 entry.sceneFileName.c_str());
}

// ---- custom scene files ---------------------------------------------------------------------------

bool SceneDB::HasUnboundBase() const {
    return unboundBase;
}

void SceneDB::LoadCustomScenes() {
    auto archiveManager = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    unboundBase = archiveManager->HasFile("unbound.json");
    if (unboundBase) {
        SPDLOG_INFO("[Unbound] Unbound-format archive mounted; vanilla scenes load from scene.json");
    }
    auto files = archiveManager->ListFiles(kCustomSceneGlob);
    if (files == nullptr) {
        return;
    }

    size_t loaded = 0;
    for (const auto& path : *files) {
        if (!path.ends_with(".json")) {
            continue;
        }
        if (LoadCustomSceneFile(path)) {
            loaded++;
        }
    }
    if (loaded > 0) {
        SPDLOG_INFO("[Unbound] registered {} custom scene(s), {} custom entrance(s)", loaded,
                    customEntrances.size());
    }
}

bool SceneDB::LoadCustomSceneFile(const std::string& path) {
    auto archiveManager = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    auto file = archiveManager->LoadFile(path);
    if (file == nullptr || file->Buffer == nullptr) {
        SPDLOG_ERROR("[Unbound] could not read {}", path);
        return false;
    }

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(file->Buffer->begin(), file->Buffer->end(), nullptr, true, true);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Unbound] {}: invalid JSON: {}", path, e.what());
        return false;
    }

    try {
        CustomSceneInit scene;
        scene.name = doc.at("id").get<std::string>();
        scene.displayName = doc.value("name", scene.name);
        scene.scenePath = doc.at("scene").get<std::string>();
        scene.titleCardTexture = doc.value("titleCard", "");
        scene.sceneId = doc.value("sceneId", -1);
        scene.drawConfig = (uint8_t)doc.value("drawConfig", 0);

        Entry& entry = AddCustomScene(scene);
        if (!entry.valid) {
            return false;
        }

        for (const auto& e : doc.value("entrances", nlohmann::json::array())) {
            CustomEntranceInit entrance;
            entrance.name = scene.name + "/" + e.at("id").get<std::string>();
            entrance.index = e.value("index", -1);
            entrance.sceneId = entry.id;
            entrance.spawn = (int8_t)e.value("spawn", 0);
            entrance.continueBgm = e.value("continueBgm", false);
            entrance.displayTitleCard = e.value("titleCard", false);
            entrance.endTransType = (uint8_t)e.value("endTransition", 2);
            entrance.startTransType = (uint8_t)e.value("startTransition", 2);
            AddCustomEntrance(entrance);
        }
        SPDLOG_INFO("[Unbound] {}: scene '{}' -> id {:#x}", path, entry.name, entry.id);
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Unbound] {}: {}", path, e.what());
        return false;
    }
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
}

void InitUnboundSection(bool isDebug) {
    sCustomSceneFlags.clear();
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
    if (sceneNum >= 0 && sceneNum < (int32_t)ARRAY_COUNT(gSaveContext.sceneFlags)) {
        return &gSaveContext.sceneFlags[sceneNum];
    }
    return &sCustomSceneFlags[sceneNum]; // value-initialised (all zero) on first access
}
