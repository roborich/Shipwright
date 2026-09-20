#include "Warping.h"
#include <libultraship/bridge.h>
#include "soh/Enhancements/Cheats/FreezeTime.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "soh/SohGui/MenuTypes.h"
#include "soh/util.h"
#include "soh/unbound/SceneDB.h"
#include <spdlog/spdlog.h>

extern "C" {
#include "z64.h"
#include "global.h"
#include "soh/Enhancements/enhancementTypes.h"
void Sram_InitDebugSave(void);
void Select_LoadGame(SelectContext* selectContext, s32 entranceIndex);
}

#define CVAR_BOOTSEQUENCE_NAME CVAR_SETTING("BootSequence")
#define CVAR_BOOTSEQUENCE_DEFAULT BOOTSEQUENCE_DEFAULT
#define CVAR_BOOTSEQUENCE_VALUE CVarGetInteger(CVAR_BOOTSEQUENCE_NAME, CVAR_BOOTSEQUENCE_DEFAULT)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec3f, x, y, z)
// Written out rather than NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT: the Linux release builds on
// Ubuntu 22.04, whose nlohmann-json (3.10) predates that macro. A point saved before linkAge and dayTime
// existed loads with the struct's defaults, which are what the boot warp always used (adult, noon).
void to_json(nlohmann::json& j, const WarpPoint& p) {
    j = nlohmann::json{
        { "entranceId", p.entranceId },   { "roomNum", p.roomNum }, { "pos", p.pos },        { "rotY", p.rotY },
        { "bootToPoint", p.bootToPoint }, { "linkAge", p.linkAge }, { "dayTime", p.dayTime }
    };
    if (!p.entranceName.empty()) {
        j["entranceName"] = p.entranceName;
    }
}

void from_json(const nlohmann::json& j, WarpPoint& p) {
    const WarpPoint defaults;
    p.entranceId = j.value("entranceId", defaults.entranceId);
    p.entranceName = j.value("entranceName", defaults.entranceName);
    p.roomNum = j.value("roomNum", defaults.roomNum);
    p.pos = j.value("pos", defaults.pos);
    p.rotY = j.value("rotY", defaults.rotY);
    p.bootToPoint = j.value("bootToPoint", defaults.bootToPoint);
    p.linkAge = j.value("linkAge", defaults.linkAge);
    p.dayTime = j.value("dayTime", defaults.dayTime);
}
std::map<std::string, WarpPoint> warpPoints;

void LoadConfig() {
    auto allConfig = Ship::Context::GetInstance()->GetConfig()->GetNestedJson();
    if (allConfig.find("WarpPoints") == allConfig.end() || !allConfig["WarpPoints"].is_object()) {
        allConfig["WarpPoints"] = nlohmann::json::object();
    }
    warpPoints = allConfig["WarpPoints"];
}

void SaveConfig() {
    auto allConfig = Ship::Context::GetInstance()->GetConfig()->GetNestedJson();
    allConfig["WarpPoints"] = warpPoints;
    Ship::Context::GetInstance()->GetConfig()->SetBlock("WarpPoints", warpPoints);
    Ship::Context::GetInstance()->GetConfig()->Save();
}

// Outside gameplay (the title screen, its attract demo, file select) a warp starts a fresh
// game on the debug save, the way the boot warp does; in gameplay it is a scene transition.
static bool NeedsFreshGame() {
    return gPlayState == NULL || gSaveContext.gameMode != GAMEMODE_NORMAL;
}

// The debug save equips the sword and shield for the age the save holds at the time, so the
// age is set first. A file already loaded (the Debug Warp Screen opened from a game keeps
// fileNum 0..2) is kept, as Select_LoadGame keeps it.
static void InitDebugSaveAs(s32 linkAge) {
    gSaveContext.linkAge = linkAge;
    if (gSaveContext.fileNum <= 2) {
        return;
    }
    gSaveContext.fileNum = 0xFE; // temporary file so that this will respect debug save file option
    Sram_InitDebugSave();
    gSaveContext.magicFillTarget = gSaveContext.magic;
    gSaveContext.magic = 0;
    gSaveContext.magicCapacity = 0;
    gSaveContext.magicLevel = gSaveContext.magic;
    gSaveContext.fileNum = 0xFF;
}

static void StartFreshGame(s32 entranceId, s32 linkAge) {
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    InitDebugSaveAs(linkAge);
    gSaveContext.sceneSetupIndex = 0;
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.respawnFlag = 0;

    // Copied from Select_LoadGame
    for (int buttonIndex = 0; buttonIndex < ARRAY_COUNT(gSaveContext.buttonStatus); buttonIndex++) {
        gSaveContext.buttonStatus[buttonIndex] = BTN_ENABLED;
    }
    gSaveContext.forceRisingButtonAlphas = gSaveContext.unk_13E8 = gSaveContext.unk_13EA = gSaveContext.unk_13EC = 0;
    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_STOP);
    gSaveContext.entranceIndex = entranceId;

    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.showTitleCard = true;
    gWeatherMode = 0;
    gGameState->running = false;
    SET_NEXT_GAMESTATE(gGameState, Play_Init, PlayState);
    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);
}

static void TransitionInPlay(s32 entranceId) {
    gPlayState->nextEntranceIndex = entranceId;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_INSTANT;
}

// In gameplay the age changes the way the time-travel cutscenes change it: the new age goes
// into linkAgeOnLoad, Play_Destroy swaps the equipment when that differs from the save's age,
// and Player_Destroy then writes it into the save. Writing the save's age directly would be
// undone by that last step. A fresh game already holds the age (InitDebugSaveAs), so setting
// the two equal means no swap, and the attract demo's Player_Destroy puts nothing else back.
static void ApplyAgeOnLoad(s32 linkAge) {
    if (gPlayState != NULL) {
        gPlayState->linkAgeOnLoad = linkAge;
    }
}

// Play_Init derives the night flag, and with it the scene layer, from dayTime; skyboxTime
// follows it. (nextDayTime, the game's own way to arrive at a time, also plays the rooster
// or the dog for the new day or night; a warp should not.)
static void ApplyDayTime(s32 dayTime) {
    gSaveContext.skyboxTime = gSaveContext.dayTime = (u16)dayTime;
    FreezeTime_Retime(dayTime); // the cheat would otherwise put its own time back next frame
}

// Spawn standing at the point instead of at the entrance's spawn, through the void-out
// respawn, without the void damage it normally inflicts.
static void RespawnAtPoint(int32_t entranceId, const WarpPoint& point) {
    gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = entranceId;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex = point.roomNum;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].pos = point.pos;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw = point.rotY;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams = 0xDFF;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;
    gSaveContext.respawnFlag = 1;
    static HOOK_ID hookId = 0;
    hookId = REGISTER_VB_SHOULD(VB_INFLICT_VOID_DAMAGE, {
        *should = false;
        GameInteractor::Instance->UnregisterGameHookForID<GameInteractor::OnVanillaBehavior>(hookId);
    });
}

void Warping_WarpToEntrance(int32_t entranceId, int32_t linkAge, int32_t dayTime) {
    if (NeedsFreshGame()) {
        StartFreshGame(entranceId, linkAge);
    } else {
        TransitionInPlay(entranceId);
    }
    ApplyAgeOnLoad(linkAge);
    ApplyDayTime(dayTime);
}

int32_t Warping_ResolveEntrance(const WarpPoint& point) {
    int32_t entranceId =
        point.entranceName.empty() ? point.entranceId : EntranceDB_RetrieveIndex(point.entranceName.c_str());
    return entranceId >= 0 && entranceId < EntranceDB_GetEntryCount() ? entranceId : -1;
}

bool Warping_WarpToPoint(const WarpPoint& point) {
    int32_t entranceId = Warping_ResolveEntrance(point);
    if (entranceId < 0) {
        SPDLOG_WARN("[Unbound] warp point's entrance '{}' ({:#x}) is not registered; is its mod loaded?",
                    point.entranceName, point.entranceId);
        return false;
    }
    Warping_WarpToEntrance(entranceId, point.linkAge, point.dayTime);
    RespawnAtPoint(entranceId, point);
    return true;
}

// Play_Init's threshold for the night flag.
static bool IsNight(s32 dayTime) {
    return dayTime > 0xC000 || dayTime < 0x4555;
}

static std::string warpNameInput = "";

void WarpPointsWidget(WidgetInfo& info) {
    ImGui::SeparatorText("Warp Points");
    if (gPlayState != NULL && GET_PLAYER(gPlayState) != NULL) {
        UIWidgets::InputString("##WarpPointNameInput", &warpNameInput,
                               {
                                   .size = ImVec2(ImGui::GetContentRegionAvail().x - 50.0f, 0.0f),
                                   .placeholder = "Enter warp point name...",
                               });

        ImGui::SameLine();
        bool isEmpty = warpNameInput.empty();
        if (isEmpty) {
            ImGui::BeginDisabled();
        }

        if (UIWidgets::Button(ICON_FA_PLUS)) {
            Player* player = GET_PLAYER(gPlayState);

            std::string warpName = SohUtils::GetSceneName(gPlayState->sceneNum);
            if (gPlayState->roomCtx.curRoom.num != 0) {
                warpName += " (" + std::to_string(gPlayState->roomCtx.curRoom.num) + ")";
            }

            warpPoints[warpNameInput] = WarpPoint{
                .entranceId = gSaveContext.entranceIndex,
                .entranceName = EntranceDB_RetrieveName(gSaveContext.entranceIndex), // "" for vanilla
                .roomNum = gPlayState->roomCtx.curRoom.num,
                .pos = player->actor.world.pos,
                .rotY = player->actor.shape.rot.y,
                .linkAge = gSaveContext.linkAge,
                .dayTime = gSaveContext.dayTime,
            };
            SaveConfig();
            warpNameInput = "";
        }
        if (isEmpty) {
            ImGui::EndDisabled();
        }
    }
    // List of warp points, showing just their name, a button to warp and a button to delete
    for (auto it = warpPoints.begin(); it != warpPoints.end();) {
        ImGui::PushID(it->first.c_str());

        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", it->first.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s, %s", it->second.linkAge == LINK_AGE_CHILD ? "child" : "adult",
                            IsNight(it->second.dayTime) ? "night" : "day");
        if (it->second.bootToPoint) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.0f, 1.0f), "[Boot]");
        }
        if (Warping_ResolveEntrance(it->second) < 0) { // SOH [Unbound] a custom entrance whose mod is not loaded
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "[Mod not loaded]");
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 115.0f);
        if (UIWidgets::Button(ICON_FA_PLANE, { .size = UIWidgets::Sizes::Inline })) {
            Warping_WarpToPoint(it->second);
        }
        ImGui::SameLine();
        if (UIWidgets::Button(ICON_FA_REFRESH,
                              { .size = UIWidgets::Sizes::Inline, .color = UIWidgets::Colors::Orange })) {
            for (auto& wp : warpPoints) {
                wp.second.bootToPoint = false;
            }
            it->second.bootToPoint = true;
            SaveConfig();
        }
        ImGui::SameLine();
        if (UIWidgets::Button(ICON_FA_TRASH, { .size = UIWidgets::Sizes::Inline, .color = UIWidgets::Colors::Red })) {
            it = warpPoints.erase(it);
            SaveConfig();
            ImGui::PopID();
            continue;
            ;
        }
        ImGui::PopID();

        ++it;
    }
}

static void OpenDebugWarpScreen(TitleContext* titleContext) {
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    titleContext->state.running = false;
    SET_NEXT_GAMESTATE(&titleContext->state, Select_Init, SelectContext);
}

void RegisterWarping() {
    static bool loadedConfig = false;
    if (!loadedConfig) {
        LoadConfig();
        loadedConfig = true;
    }

    COND_HOOK(OnZTitleUpdate, CVAR_BOOTSEQUENCE_VALUE == BOOTSEQUENCE_DEBUGWARPSCREEN,
              [](void* gameState) { OpenDebugWarpScreen((TitleContext*)gameState); });

    // The Debug Warp Screen when no point is marked to boot to, or the marked one names an
    // entrance no loaded mod registers.
    COND_HOOK(OnZTitleUpdate, CVAR_BOOTSEQUENCE_VALUE == BOOTSEQUENCE_WARPPOINT, [](void* gameState) {
        for (auto& wp : warpPoints) {
            if (wp.second.bootToPoint && Warping_WarpToPoint(wp.second)) {
                return;
            }
        }
        OpenDebugWarpScreen((TitleContext*)gameState);
    });
}

static RegisterShipInitFunc initFunc(RegisterWarping, { CVAR_BOOTSEQUENCE_NAME });
