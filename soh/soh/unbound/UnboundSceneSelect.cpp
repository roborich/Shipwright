// SOH [Unbound] Custom scenes on the Better Debug Warp screen. See unbound-docs/registries.md.
#include "UnboundSceneSelect.h"

#include <deque>
#include <spdlog/spdlog.h>
#include <string>
#include <type_traits>
#include <vector>

#include "soh/unbound/SceneDB.h"

namespace {
struct WarpEntrance {
    std::string label;
    int32_t index;
};

struct WarpScene {
    std::string name;
    std::vector<WarpEntrance> entrances;
};

// The list's strings; a deque so the char* handed to the screen stay put while it grows.
struct WarpList {
    std::vector<BetterSceneSelectEntry> entries;
    std::deque<std::string> strings;

    char* Own(std::string text) {
        strings.push_back(std::move(text));
        return strings.back().data();
    }
};

WarpList sWarpList;

constexpr size_t kMaxEntrances = std::extent_v<decltype(BetterSceneSelectEntry::entrancePairs)>;

// "mymod/lava_temple/main" -> "main": the scene is already named on the list line above.
std::string EntranceLabel(const std::string& entranceName, const std::string& sceneName) {
    const std::string prefix = sceneName + "/";
    return entranceName.rfind(prefix, 0) == 0 ? entranceName.substr(prefix.size()) : entranceName;
}

// Custom scenes in scene-id order, each with its entrances in registration order. A scene without an entrance
// has nothing to warp to and is left out.
std::vector<WarpScene> CollectCustomScenes(const SceneDB& db) {
    std::vector<WarpScene> scenes;
    for (const SceneDB::Entry& entry : db.Entries()) {
        if (!entry.valid || !entry.isCustom) {
            continue;
        }
        WarpScene scene{ entry.displayName, {} };
        for (const SceneDB::EntranceEntry& entrance : db.CustomEntrances()) {
            if (entrance.sceneId == entry.id) {
                scene.entrances.push_back({ EntranceLabel(entrance.name, entry.name), entrance.index });
            }
        }
        if (!scene.entrances.empty()) {
            scenes.push_back(std::move(scene));
        }
    }
    return scenes;
}

// The screen has room for kMaxEntrances per scene; the rest stay reachable from the console.
void TruncateEntrances(WarpScene& scene) {
    if (scene.entrances.size() > kMaxEntrances) {
        SPDLOG_WARN("[Unbound] debug warp screen lists the first {} of scene '{}''s {} entrances", kMaxEntrances,
                    scene.name, scene.entrances.size());
        scene.entrances.resize(kMaxEntrances);
    }
}

// Numbered like the vanilla lines ("50:Debug"); custom scenes have one name for every language.
BetterSceneSelectEntry ToSelectEntry(WarpList& list, const WarpScene& scene, s32 number,
                                     void (*loadFunc)(struct SelectContext*, s32)) {
    BetterSceneSelectEntry entry = {};
    char* name = list.Own(std::to_string(number) + ":" + scene.name);
    entry.japaneseName = entry.englishName = entry.germanName = entry.frenchName = name;
    entry.loadFunc = loadFunc;
    entry.entranceCount = (u8)scene.entrances.size();
    for (size_t i = 0; i < scene.entrances.size(); i++) {
        BetterSceneSelectEntrancePair& pair = entry.entrancePairs[i];
        char* label = list.Own(scene.entrances[i].label);
        pair.japaneseName = pair.englishName = pair.germanName = pair.frenchName = label;
        pair.entranceIndex = scene.entrances[i].index;
        pair.canBeMQ = 0;
    }
    return entry;
}
} // namespace

extern "C" BetterSceneSelectEntry* UnboundSceneSelect_BuildList(const BetterSceneSelectEntry* vanilla, s32 vanillaCount,
                                                                void (*loadFunc)(struct SelectContext*, s32),
                                                                s32* outCount) {
    sWarpList.entries.assign(vanilla, vanilla + vanillaCount);
    sWarpList.strings.clear();

    for (WarpScene& scene : CollectCustomScenes(*SceneDB::Instance)) {
        TruncateEntrances(scene);
        s32 number = (s32)sWarpList.entries.size() + 1;
        sWarpList.entries.push_back(ToSelectEntry(sWarpList, scene, number, loadFunc));
    }

    *outCount = (s32)sWarpList.entries.size();
    return sWarpList.entries.data();
}
