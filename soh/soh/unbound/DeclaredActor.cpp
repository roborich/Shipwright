// SOH [Unbound] The declared-actor driver (unbound-docs/actors.md, "The driver"). Each behavior is its own function
// taking the actor and its type, so the four ActorDB functions read as a list of steps, and so a script can later
// call the same functions.
#include "DeclaredActor.h"

#include <algorithm>
#include <unordered_map>
#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>

#include "soh/ResourceManagerHelpers.h"
#include "soh/frame_interpolation.h" // gives OPEN_DISPS's block-scope declarations their C linkage
#include "soh/resource/type/Animation.h"
#include "soh/resource/type/Skeleton.h"
#include "soh/resource/type/SohResourceType.h"
#include "soh/unbound/ActorRegistry.h"
#include <fast/resource/ResourceType.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

namespace {

typedef struct DeclaredActor {
    Actor actor;
    const DeclaredActorType* type;
    SkelAnime skelAnime;
    ColliderCylinder collider;
    NpcInteractInfo interactInfo;
    u8 hasSkeleton;
    u8 hasCollision;
    u8 talkable;
    u8 talking;
    u8 looking;
} DeclaredActor;

ColliderCylinderInit sCylinderInit = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_NONE,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_2,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0x00000000, 0x00, 0x00 },
        { 0x00000000, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_NONE,
        OCELEM_ON,
    },
    { 0, 0, 0, { 0, 0, 0 } },
};

constexpr s8 kTalkTargetMode = 6;       // 100-unit targeting range, as vanilla NPCs
constexpr s16 kLookTrackingPreset = 0;  // sNpcTrackingPresets: 60 degrees of head yaw
constexpr f32 kVanillaNpcScale = 0.01f; // the scale `shadow` is given at

// ---- assets -------------------------------------------------------------------------------------------------------

std::shared_ptr<Ship::IResource> LoadAsset(const std::string& otrPath) {
    return ResourceMgr_GetResourceByNameHandlingMQ(otrPath.c_str() + DeclaredActorType::kOtrPrefixLength);
}

uint32_t AssetType(const std::shared_ptr<Ship::IResource>& resource) {
    return resource != nullptr ? resource->GetInitData()->Type : 0;
}

// The resource at `path` if it is a skeleton the driver can animate, else nullptr: normal or flex, with standard or
// LOD limbs, at least one. Skin limbs (Epona's) have no display list where the skeleton drawer reads one.
std::shared_ptr<SOH::Skeleton> LoadSkeleton(const std::string& path) {
    auto resource = LoadAsset(path);
    if (AssetType(resource) != (uint32_t)SOH::ResourceType::SOH_Skeleton) {
        return nullptr;
    }
    auto skeleton = std::static_pointer_cast<SOH::Skeleton>(resource);
    bool supportedType = skeleton->type == SOH::SkeletonType::Normal || skeleton->type == SOH::SkeletonType::Flex;
    bool supportedLimbs = skeleton->limbType == SOH::LimbType::Standard || skeleton->limbType == SOH::LimbType::LOD;
    return supportedType && supportedLimbs && skeleton->limbCount > 0 ? skeleton : nullptr;
}

// The resource at `path` if it is a normal (not Link's) animation with at least one frame, else nullptr.
std::shared_ptr<SOH::Animation> LoadAnimation(const std::string& path) {
    auto resource = LoadAsset(path);
    if (AssetType(resource) != (uint32_t)SOH::ResourceType::SOH_Animation) {
        return nullptr;
    }
    auto animation = std::static_pointer_cast<SOH::Animation>(resource);
    bool normal = animation->type == SOH::AnimationType::Normal;
    return normal && animation->animationData.animationHeader.common.frameCount > 0 ? animation : nullptr;
}

bool IsDisplayList(const std::string& path) {
    return AssetType(LoadAsset(path)) == (uint32_t)Fast::ResourceType::DisplayList;
}

bool IsTexture(const std::string& path) {
    return AssetType(LoadAsset(path)) == (uint32_t)Fast::ResourceType::Texture;
}

// ---- pure rules ---------------------------------------------------------------------------------------------------

// A placement's message: its params when they name one, otherwise the type's default (0 = cannot talk).
u16 PlacementMessage(s16 params, const DeclaredActorType& type) {
    u16 fromParams = (u16)params;
    return fromParams != 0 && fromParams != 0xFFFF ? fromParams : type.message;
}

// A looping animation's playback rate. The skeleton code wraps the frame once per update, so a step longer than
// the animation would carry it out of the animation's data.
f32 LoopSpeed(f32 speed, f32 length) {
    return CLAMP(speed, -length, length);
}

// The shadow scale that draws `shadow` at the same size whatever the model's scale: vanilla scales a shadow by the
// actor's scale, and `shadow` is given at the scale of vanilla NPCs. The registry accepts only a positive scale.
f32 ShadowScale(const DeclaredActorType& type) {
    return type.shadow * kVanillaNpcScale / type.scale;
}

// The height of the focus point above the actor's position when there is no head to put it on.
f32 FocusHeight(const DeclaredActorType& type) {
    return type.HasCollision() ? (f32)(type.yShift + type.height) : 0.0f;
}

// ---- type checks --------------------------------------------------------------------------------------------------

// What the first spawn of a type found about its assets.
struct TypeCheck {
    bool spawns = false; // every asset path names an asset the driver can use
    bool flex = false;   // the skeleton is a flex skeleton
    bool looks = false;  // `look.limb` is a limb of the skeleton
};

void LogUnusable(const DeclaredActorType& type, const char* what, const std::string& path) {
    SPDLOG_ERROR("[Unbound] actor type '{}': {} '{}'; actors of this type do not spawn", type.name, what,
                 path.substr(DeclaredActorType::kOtrPrefixLength));
}

// Every segment texture must load: an unresolved path would reach the renderer as raw bytes.
bool SegmentsUsable(const DeclaredActorType& type) {
    for (const auto& [segment, texture] : type.segments) {
        if (!IsTexture(texture)) {
            LogUnusable(type, "no texture at", texture);
            return false;
        }
    }
    return true;
}

// The skeleton, or nullptr, logged, when the skeleton or its animation cannot be used.
std::shared_ptr<SOH::Skeleton> UsableSkeleton(const DeclaredActorType& type) {
    auto skeleton = LoadSkeleton(type.skeleton);
    if (skeleton == nullptr) {
        LogUnusable(type, "no normal or flex skeleton with standard or LOD limbs at", type.skeleton);
        return nullptr;
    }
    auto animation = LoadAnimation(type.animation);
    if (animation == nullptr) {
        LogUnusable(type, "no animation with frames at", type.animation);
        return nullptr;
    }
    // One entry per limb plus the root position, as the skeleton's joint table: fewer would be read past the end.
    if (animation->rotationIndices.size() < (size_t)skeleton->limbCount + 1) {
        LogUnusable(type, "an animation for fewer limbs than the skeleton at", type.animation);
        return nullptr;
    }
    return skeleton;
}

// Limb-draw numbering: the root is 1 and the last limb is limbCount.
bool LookLimbValid(const DeclaredActorType& type, s32 limbCount) {
    if (!type.looks) {
        return false;
    }
    if (type.limb < 1 || type.limb > limbCount) {
        SPDLOG_ERROR("[Unbound] actor type '{}': look limb {} is not a limb of its skeleton (1-{}); the head will not "
                     "turn",
                     type.name, type.limb, limbCount);
        return false;
    }
    return true;
}

// Logs each hidden limb past the skeleton's last limb. Such an entry hides nothing; the registry already refused
// entries below 1.
void CheckHiddenLimbs(const DeclaredActorType& type, s32 limbCount) {
    for (s32 limb : type.hideLimbs) {
        if (limb > limbCount) {
            SPDLOG_ERROR("[Unbound] actor type '{}': hideLimbs entry {} is not a limb of its skeleton (1-{}); ignored",
                         type.name, limb, limbCount);
        }
    }
}

TypeCheck CheckType(const DeclaredActorType& type) {
    TypeCheck check;
    if (!SegmentsUsable(type)) {
        return check;
    }
    if (type.skeleton.empty()) {
        check.spawns = IsDisplayList(type.displayList);
        if (!check.spawns) {
            LogUnusable(type, "no display list at", type.displayList);
        }
        return check;
    }
    auto skeleton = UsableSkeleton(type);
    if (skeleton != nullptr) {
        check.spawns = true;
        check.flex = skeleton->type == SOH::SkeletonType::Flex;
        check.looks = LookLimbValid(type, skeleton->limbCount);
        CheckHiddenLimbs(type, skeleton->limbCount);
    }
    return check;
}

// Checks a type on its first spawn and remembers the result for the session, so its assets are looked up, and a
// problem logged, once rather than for every placement. Mods are mounted only at startup, so neither the registry nor
// the assets its paths name change later, and the registry never moves a type, so its address is a stable key.
const TypeCheck& CheckedType(const DeclaredActorType& type) {
    static std::unordered_map<const DeclaredActorType*, TypeCheck> sChecked;
    auto it = sChecked.find(&type);
    if (it == sChecked.end()) {
        it = sChecked.emplace(&type, CheckType(type)).first;
    }
    return it->second;
}

// ---- init ---------------------------------------------------------------------------------------------------------

bool ResolveType(DeclaredActor* self) {
    self->type = SOH::Unbound::GetDeclaredActorType(self->actor.id);
    if (self->type == nullptr) {
        SPDLOG_ERROR("[Unbound] actor id {:#x} is not a declared actor type", self->actor.id);
        Actor_Kill(&self->actor);
        return false;
    }
    return true;
}

// Kills the actor when its type's assets cannot be used (CheckType logged why).
bool CanSpawn(DeclaredActor* self) {
    if (!CheckedType(*self->type).spawns) {
        Actor_Kill(&self->actor);
        return false;
    }
    return true;
}

void InitShape(DeclaredActor* self) {
    const DeclaredActorType& type = *self->type;
    f32 shadowScale = ShadowScale(type);
    ActorShape_Init(&self->actor.shape, type.yOffset, shadowScale > 0.0f ? ActorShadow_DrawCircle : NULL, shadowScale);
    Actor_SetScale(&self->actor, type.scale);
}

// The ground the round shadow is drawn on: the shadow draws only over a known floor. The actor never moves, so one
// raycast at spawn serves, and it records the floor without moving the actor onto it. Only scene collision is kept:
// a moving floor's polygons are renumbered as dyna actors come and go, so a stored pointer would drift to another
// polygon.
void InitFloor(DeclaredActor* self, PlayState* play) {
    if (self->actor.shape.shadowDraw == NULL) {
        return;
    }
    Vec3f checkPos = self->actor.world.pos;
    checkPos.y += 50.0f; // as the vanilla floor check, so a floor at the actor's feet is found
    CollisionPoly* floorPoly = NULL;
    s32 bgId = BGCHECK_SCENE;
    f32 floorHeight = BgCheck_EntityRaycastFloor5(play, &play->colCtx, &floorPoly, &bgId, &self->actor, &checkPos);
    if (bgId != BGCHECK_SCENE) {
        return;
    }
    self->actor.floorPoly = floorPoly;
    self->actor.floorHeight = floorHeight;
    self->actor.floorBgId = bgId;
}

// Vanilla's culling zone fits an NPC: a model reaching further from its origin widens it, and a draw distance moves
// its far edge.
void InitCulling(DeclaredActor* self) {
    const DeclaredActorType& type = *self->type;
    self->actor.uncullZoneScale = std::max(self->actor.uncullZoneScale, type.cullRadius);
    self->actor.uncullZoneDownward = std::max(self->actor.uncullZoneDownward, type.cullRadius);
    if (type.drawDistance > 0.0f) {
        self->actor.uncullZoneForward = type.drawDistance;
    }
}

// The animation is loaded and checked by CheckType.
void InitAnimation(DeclaredActor* self) {
    const DeclaredActorType& type = *self->type;
    AnimationHeader* animation = (AnimationHeader*)type.animation.c_str();
    f32 lastFrame = Animation_GetLastFrame(animation);
    if (type.holdFrame) {
        f32 frame = CLAMP(type.frame, 0.0f, lastFrame);
        Animation_Change(&self->skelAnime, animation, 0.0f, frame, frame, ANIMMODE_ONCE, 0.0f);
    } else {
        f32 speed = LoopSpeed(type.speed, Animation_GetLength(animation));
        Animation_Change(&self->skelAnime, animation, speed, 0.0f, lastFrame, ANIMMODE_LOOP, 0.0f);
    }
}

// The skeleton is checked by CheckType; a display-list model needs no setup.
void InitMesh(DeclaredActor* self, PlayState* play) {
    const DeclaredActorType& type = *self->type;
    if (type.skeleton.empty()) {
        return;
    }
    if (CheckedType(type).flex) {
        SkelAnime_InitFlex(play, &self->skelAnime, (FlexSkeletonHeader*)type.skeleton.c_str(), NULL, NULL, NULL, 0);
    } else {
        SkelAnime_Init(play, &self->skelAnime, (SkeletonHeader*)type.skeleton.c_str(), NULL, NULL, NULL, 0);
    }
    self->hasSkeleton = true;
    InitAnimation(self);
}

void InitModel(DeclaredActor* self, PlayState* play) {
    InitShape(self);
    InitFloor(self, play);
    InitCulling(self);
    InitMesh(self, play);
}

void InitCollision(DeclaredActor* self, PlayState* play) {
    const DeclaredActorType& type = *self->type;
    if (!type.HasCollision()) {
        return;
    }
    Collider_InitCylinder(play, &self->collider);
    Collider_SetCylinder(play, &self->collider, &self->actor, &sCylinderInit);
    self->collider.dim.radius = type.radius;
    self->collider.dim.height = type.height;
    self->collider.dim.yShift = type.yShift;
    self->actor.colChkInfo.mass = MASS_IMMOVABLE;
    self->actor.colChkInfo.cylRadius = type.radius;
    self->actor.colChkInfo.cylHeight = type.height;
    self->hasCollision = true;
}

void InitTalk(DeclaredActor* self) {
    u16 message = self->type->talks ? PlacementMessage(self->actor.params, *self->type) : 0;
    self->talkable = message != 0;
    if (self->talkable) {
        self->actor.textId = message;
        self->actor.targetMode = kTalkTargetMode;
    } else {
        // The type's flags make every placement targetable; this one has nothing to say.
        self->actor.flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY);
    }
}

void InitLook(DeclaredActor* self) {
    self->looking = CheckedType(*self->type).looks;
}

void InitFocus(DeclaredActor* self) {
    self->actor.focus.pos = self->actor.world.pos;
    self->actor.focus.pos.y += FocusHeight(*self->type);
}

// ---- update -------------------------------------------------------------------------------------------------------

// Whether the player is talking to this actor. Read from the Player every frame rather than latched: the actor does
// not update while culled, so a latch could miss the one frame the conversation closes on and stay set.
bool IsTalkingTo(DeclaredActor* self, PlayState* play) {
    Player* player = GET_PLAYER(play);
    return (player->stateFlags1 & PLAYER_STATE1_TALKING) && player->talkActor == &self->actor;
}

// A box that ends in an event waits for its actor to close it, as vanilla actors do in their talk state. With no
// behavior to run, the driver closes it when the player advances, so no message can hold the player in the textbox.
void CloseEventBox(PlayState* play) {
    if (Message_GetState(&play->msgCtx) == TEXT_STATE_EVENT && Message_ShouldAdvance(play)) {
        Message_CloseTextbox(play);
    }
}

// A culled actor does not update, and CloseEventBox runs in its update: while talking, it updates wherever the
// camera is. The type's flags never include this one, so clearing it restores them.
void KeepUpdatingWhileTalking(DeclaredActor* self) {
    if (self->talking) {
        self->actor.flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    } else {
        self->actor.flags &= ~ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    }
}

void UpdateTalk(DeclaredActor* self, PlayState* play) {
    if (!self->talkable) {
        return;
    }
    self->talking = IsTalkingTo(self, play);
    KeepUpdatingWhileTalking(self);
    if (self->talking) {
        CloseEventBox(play);
    } else if (!Actor_ProcessTalkRequest(&self->actor, play)) {
        Actor_OfferTalk(&self->actor, play, self->type->talkRange);
    }
}

void UpdateLook(DeclaredActor* self, PlayState* play) {
    if (!self->looking) {
        return;
    }
    Player* player = GET_PLAYER(play);
    f32 range = self->type->lookRange;
    bool follow = self->talking || self->actor.xyzDistToPlayerSq < SQ(range);
    self->interactInfo.trackPos = player->actor.focus.pos;
    self->interactInfo.yOffset = self->actor.focus.pos.y - self->actor.world.pos.y;
    Npc_TrackPoint(&self->actor, &self->interactInfo, kLookTrackingPreset,
                   follow ? NPC_TRACKING_HEAD : NPC_TRACKING_NONE);
}

void UpdateCollision(DeclaredActor* self, PlayState* play) {
    if (!self->hasCollision) {
        return;
    }
    Collider_UpdateCylinder(&self->actor, &self->collider);
    CollisionCheck_SetOC(play, &play->colChkCtx, &self->collider.base);
}

void UpdateAnimation(DeclaredActor* self) {
    if (self->hasSkeleton && !self->type->holdFrame) {
        SkelAnime_Update(&self->skelAnime);
    }
}

// ---- draw ---------------------------------------------------------------------------------------------------------

// The point the head turns about, in the head limb's space: `pivot` along the turn axis.
Vec3f HeadPivot(const DeclaredActorType& type) {
    return { type.pivot * type.turnAxis.x, type.pivot * type.turnAxis.y, type.pivot * type.turnAxis.z };
}

// Rotates the current matrix about a unit axis in its own space. The X and Z axes keep Matrix_RotateX/Z, so the
// default look axes turn a head exactly as before the axes could be set.
void RotateAbout(f32 angle, const Vec3f& axis) {
    if (axis.x == 1.0f && axis.y == 0.0f && axis.z == 0.0f) {
        Matrix_RotateX(angle, MTXMODE_APPLY);
    } else if (axis.x == 0.0f && axis.y == 0.0f && axis.z == 1.0f) {
        Matrix_RotateZ(angle, MTXMODE_APPLY);
    } else {
        Vec3f unit = axis;
        Matrix_RotateAxis(angle, &unit, MTXMODE_APPLY);
    }
}

// Turns the head about the head limb's own axes, around HeadPivot: left and right about `turnAxis`, then up and down
// about `nodAxis` (by default X and Z, as vanilla character rigs are built). The limb's transform is applied here and
// zeroed so the skeleton drawer applies nothing further.
void TurnHead(DeclaredActor* self, s32 limbIndex, Vec3f* pos, Vec3s* rot) {
    if (!self->looking || limbIndex != self->type->limb) {
        return;
    }
    Vec3f pivot = HeadPivot(*self->type);
    Matrix_TranslateRotateZYX(pos, rot);
    Matrix_Translate(pivot.x, pivot.y, pivot.z, MTXMODE_APPLY);
    RotateAbout((f32)BINANG_TO_RAD(self->interactInfo.headRot.y), self->type->turnAxis);
    RotateAbout((f32)BINANG_TO_RAD(self->interactInfo.headRot.x), self->type->nodAxis);
    Matrix_Translate(-pivot.x, -pivot.y, -pivot.z, MTXMODE_APPLY);
    *pos = { 0.0f, 0.0f, 0.0f };
    *rot = { 0, 0, 0 };
}

void RecordHeadFocus(DeclaredActor* self, s32 limbIndex) {
    if (!self->looking || limbIndex != self->type->limb) {
        return;
    }
    Vec3f pivot = HeadPivot(*self->type);
    Matrix_MultVec3f(&pivot, &self->actor.focus.pos);
}

void HideLimb(DeclaredActor* self, s32 limbIndex, Gfx** dList) {
    const std::vector<s32>& hidden = self->type->hideLimbs;
    if (std::find(hidden.begin(), hidden.end(), limbIndex) != hidden.end()) {
        *dList = NULL; // children still draw
    }
}

// The limb callbacks, shared by both passes; the per-pass signatures below only forward to them.
s32 OverrideLimb(void* arg, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot) {
    HideLimb((DeclaredActor*)arg, limbIndex, dList);
    TurnHead((DeclaredActor*)arg, limbIndex, pos, rot);
    return false;
}

s32 OverrideLimbOpa(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg) {
    return OverrideLimb(arg, limbIndex, dList, pos, rot);
}

void PostLimbOpa(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* arg) {
    RecordHeadFocus((DeclaredActor*)arg, limbIndex);
}

s32 OverrideLimbXlu(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg, Gfx** gfx) {
    return OverrideLimb(arg, limbIndex, dList, pos, rot);
}

void PostLimbXlu(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* arg, Gfx** gfx) {
    RecordHeadFocus((DeclaredActor*)arg, limbIndex);
}

// The draw state vanilla character draws set before their skeleton. Every segment 8-12 the type does not name gets
// an empty display list: character models call one of them to set their render mode (vanilla binds
// &D_80116280[2], an end-of-list, there), and an unbound segment would be whatever the last actor left in it. The
// env colour is opaque black, which models that fade through env alpha read as fully visible.
Gfx* BindSegments(const DeclaredActorType& type, Gfx* gfx) {
    for (u8 segment = DeclaredActorType::kSegmentMin; segment <= DeclaredActorType::kSegmentMax; segment++) {
        gSPSegment(gfx++, segment, (uintptr_t)gEmptyDL);
    }
    for (const auto& [segment, texture] : type.segments) {
        gSPSegment(gfx++, segment, (uintptr_t)texture.c_str());
    }
    gDPSetEnvColor(gfx++, 0, 0, 0, 255);
    return gfx;
}

void DrawDisplayList(DeclaredActor* self, PlayState* play) {
    Gfx* dList = (Gfx*)self->type->displayList.c_str();
    if (self->type->translucent) {
        Gfx_DrawDListXlu(play, dList);
    } else {
        Gfx_DrawDListOpa(play, dList);
    }
}

} // namespace

// ---- display lists and ActorDB functions --------------------------------------------------------------------------

// OPEN_DISPS declares the frame-interpolation hooks at block scope. Inside an anonymous namespace that declaration
// names a function of the namespace, which nothing defines, so the functions that open the display lists, and the
// ActorDB functions that call them, are file-local statics outside it.
static void DrawSkeleton(DeclaredActor* self, PlayState* play) {
    SkelAnime* skel = &self->skelAnime;
    if (!self->type->translucent) {
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        SkelAnime_DrawSkeletonOpa(play, skel, OverrideLimbOpa, PostLimbOpa, self);
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    if (skel->skeletonHeader->skeletonType == SKELANIME_TYPE_FLEX) {
        POLY_XLU_DISP = SkelAnime_DrawFlex(play, skel->skeleton, skel->jointTable, skel->dListCount, OverrideLimbXlu,
                                           PostLimbXlu, self, POLY_XLU_DISP);
    } else {
        POLY_XLU_DISP =
            SkelAnime_Draw(play, skel->skeleton, skel->jointTable, OverrideLimbXlu, PostLimbXlu, self, POLY_XLU_DISP);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

static void DrawSegments(DeclaredActor* self, PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    if (self->type->translucent) {
        POLY_XLU_DISP = BindSegments(*self->type, POLY_XLU_DISP);
    } else {
        POLY_OPA_DISP = BindSegments(*self->type, POLY_OPA_DISP);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

static void DeclaredActor_Init(Actor* thisx, PlayState* play) {
    DeclaredActor* self = (DeclaredActor*)thisx;
    if (!ResolveType(self) || !CanSpawn(self)) {
        return;
    }
    InitModel(self, play);
    InitCollision(self, play);
    InitTalk(self);
    InitLook(self);
    InitFocus(self);
}

static void DeclaredActor_Destroy(Actor* thisx, PlayState* play) {
    DeclaredActor* self = (DeclaredActor*)thisx;
    if (self->hasSkeleton) {
        SkelAnime_Free(&self->skelAnime, play);
    }
    if (self->hasCollision) {
        Collider_DestroyCylinder(play, &self->collider);
    }
}

static void DeclaredActor_Update(Actor* thisx, PlayState* play) {
    DeclaredActor* self = (DeclaredActor*)thisx;
    UpdateTalk(self, play);
    UpdateLook(self, play);
    UpdateCollision(self, play);
    UpdateAnimation(self);
}

static void DeclaredActor_Draw(Actor* thisx, PlayState* play) {
    DeclaredActor* self = (DeclaredActor*)thisx;
    DrawSegments(self, play);
    if (self->hasSkeleton) {
        DrawSkeleton(self, play);
    } else {
        DrawDisplayList(self, play);
    }
}

ActorDBInit DeclaredActor_DBInit(const DeclaredActorType& type) {
    ActorDBInit init;
    init.name = type.name;
    init.desc = type.displayName;
    init.category = type.talks ? ACTORCAT_NPC : ACTORCAT_PROP;
    init.flags = type.talks ? (ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY) : 0;
    init.objectId = OBJECT_GAMEPLAY_KEEP;
    init.instanceSize = sizeof(DeclaredActor);
    init.init = DeclaredActor_Init;
    init.destroy = DeclaredActor_Destroy;
    init.update = DeclaredActor_Update;
    init.draw = DeclaredActor_Draw;
    return init;
}
