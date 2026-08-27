#pragma once
// SOH [Unbound] Dynamic scene + entrance registry replacing the static gSceneTable / gEntranceTable.
// See unbound-docs/registries.md.
#include "z64.h"

#ifdef __cplusplus
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class SceneDB {
  public:
    static SceneDB* Instance;

    // Custom scene ids start here so they never collide with SCENE_ID_MAX (0x6E), which the vanilla
    // entrance table and several enhancements use as an "unused / no scene" sentinel.
    static constexpr int32_t CUSTOM_SCENE_ID_BASE = 0x80;

    struct Entry {
        int32_t id = -1;
        bool valid = false;
        bool isCustom = false;
        std::string name;             // stable string id: "SCENE_DEKU_TREE" for vanilla, mod-chosen for custom
        std::string displayName;      // human-readable, for menus / crash logs
        std::string sceneFileName;    // vanilla only: the o2r leaf (ydan_scene); path derived per MQ policy
        std::string scenePath;        // custom only: full o2r path of the scene resource
        std::string titleCardTexture; // custom only, optional: o2r path of a title card texture
        uint8_t drawConfig = 0;       // SDC_* index
    };

    struct EntranceEntry {
        std::string name;
        int32_t index; // first of the 4-entry scene-layer group in gEntranceTable
        int32_t sceneId;
    };

    struct CustomSceneInit {
        std::string name;
        std::string displayName;
        std::string scenePath;
        std::string titleCardTexture;
        int32_t sceneId = -1; // -1: next free
        uint8_t drawConfig = 0;
    };

    struct CustomEntranceInit {
        std::string name;
        int32_t index = -1; // -1: next free; otherwise must be >= ENTR_MAX and a multiple of 4
        int32_t sceneId = -1;
        int8_t spawn = 0;
        bool continueBgm = false;
        bool displayTitleCard = false;
        uint8_t endTransType = 2;   // TRANS_TYPE_FADE_BLACK
        uint8_t startTransType = 2; // TRANS_TYPE_FADE_BLACK
    };

    SceneDB();

    Entry& AddCustomScene(const CustomSceneInit& init);
    int32_t AddCustomEntrance(const CustomEntranceInit& init);

    Entry& RetrieveEntry(int32_t id);
    int32_t RetrieveId(const std::string& name) const;
    size_t GetEntryCount() const;
    const std::vector<Entry>& Entries() const;

    int32_t RetrieveEntranceIndex(const std::string& name) const;
    size_t GetEntranceCount() const;
    const std::vector<EntranceEntry>& CustomEntrances() const;

    // Full o2r path of a scene's resource, applying the vanilla MQ policy for vanilla dungeons.
    std::string GetScenePath(int32_t id) const;

    // Scans every loaded archive for unbound/scenes/*.json and registers what it finds.
    void LoadCustomScenes();

  private:
    void SeedVanillaScenes();
    void SeedVanillaEntrances();
    void RefreshEntranceTablePointer();
    void AddEntranceLayerGroup(int32_t index, const EntranceInfo& info);
    bool LoadCustomSceneFile(const std::string& path);

    std::vector<Entry> db;
    std::unordered_map<std::string, int32_t> nameTable;
    int32_t nextSceneId = CUSTOM_SCENE_ID_BASE;

    std::vector<EntranceInfo> entranceTable;
    std::unordered_map<std::string, int32_t> entranceNameTable;
    std::vector<EntranceEntry> customEntrances;
    int32_t nextEntranceIndex = 0;
};

class SaveManager;
void SceneDB_RegisterSaveFunctions(SaveManager& saveManager); // called from the SaveManager ctor

extern "C" {
#endif

int32_t SceneDB_IsValid(int32_t id);
int32_t SceneDB_GetEntryCount(void);
uint8_t SceneDB_GetDrawConfig(int32_t id);
const char* SceneDB_GetDisplayName(int32_t id);
const char* SceneDB_GetTitleCardTexture(int32_t id); // NULL when the scene has none registered
int32_t EntranceDB_GetEntryCount(void);
int32_t EntranceDB_RetrieveIndex(const char* name); // -1 when unknown

// Saved scene flags for any scene id: vanilla ids map into gSaveContext.sceneFlags, custom ids into
// registry-owned storage that SaveManager persists by scene name.
SavedSceneFlags* SceneFlags_Get(int32_t sceneNum);

#ifdef __cplusplus
}
#endif
