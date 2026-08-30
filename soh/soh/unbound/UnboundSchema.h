#pragma once
// SOH [Unbound] Key names and "$schema" ids of the JSON scene format, spelled out once for the loader factories
// and the exporter (both in soh/soh/unbound/). The spec is unbound-docs/SPEC.md; a key that is not listed
// here is not part of the format. Section numbers below refer to it.

namespace SOH::Unbound::Schema {

// "$schema" is "<type>/<version>"; the type half is the LUS resource type name the factory registers under.
inline constexpr const char* kSceneType = "unbound/scene";
inline constexpr const char* kRoomType = "unbound/room";
inline constexpr const char* kCollisionType = "unbound/collision";
inline constexpr const char* kPathsType = "unbound/paths";
inline constexpr const char* kTextType = "unbound/text";
inline constexpr const char* kSceneV1 = "unbound/scene/1";
inline constexpr const char* kRoomV1 = "unbound/room/1";
inline constexpr const char* kCollisionV2 = "unbound/collision/2";
inline constexpr const char* kCollisionV3 = "unbound/collision/3";
inline constexpr const char* kPathsV1 = "unbound/paths/1";
inline constexpr const char* kTextV1 = "unbound/text/1";

// Reserved keys (merge rules, §3)
inline constexpr const char* kSchema = "$schema";
inline constexpr const char* kOrder = "$order";
inline constexpr const char* kReplace = "$replace";

// Document top level (§4.2, §4.3)
inline constexpr const char* kSetups = "setups";
inline constexpr const char* kRooms = "rooms";
inline constexpr const char* kCollision = "collision";
inline constexpr const char* kOrigin = "origin";

// Setup keys, one per scene command (§4.2, §4.3)
inline constexpr const char* kSpecialObjects = "specialObjects";
inline constexpr const char* kSkybox = "skybox";
inline constexpr const char* kSound = "sound";
inline constexpr const char* kCameraSettings = "cameraSettings";
inline constexpr const char* kCutscene = "cutscene";
inline constexpr const char* kPaths = "paths";
inline constexpr const char* kLighting = "lighting";
inline constexpr const char* kEntrances = "entrances";
inline constexpr const char* kSpawns = "spawns";
inline constexpr const char* kExits = "exits";
inline constexpr const char* kTransitionActors = "transitionActors";
inline constexpr const char* kBehavior = "behavior";
inline constexpr const char* kEcho = "echo";
inline constexpr const char* kTime = "time";
inline constexpr const char* kSkyboxModifier = "skyboxModifier";
inline constexpr const char* kWind = "wind";
inline constexpr const char* kObjects = "objects";
inline constexpr const char* kLights = "lights";
inline constexpr const char* kActors = "actors";
inline constexpr const char* kMesh = "mesh";

// Entity fields
inline constexpr const char* kId = "id";
inline constexpr const char* kPos = "pos";
inline constexpr const char* kRot = "rot";
inline constexpr const char* kRotY = "rotY";
inline constexpr const char* kParams = "params";
inline constexpr const char* kType = "type";
inline constexpr const char* kColor = "color";
inline constexpr const char* kDir = "dir";
inline constexpr const char* kGlow = "glow";
inline constexpr const char* kRadius = "radius";
inline constexpr const char* kSpawn = "spawn";
inline constexpr const char* kRoom = "room";
inline constexpr const char* kFront = "front";
inline constexpr const char* kBack = "back";
inline constexpr const char* kEffects = "effects";
inline constexpr const char* kElfMessage = "elfMessage";
inline constexpr const char* kGlobalObject = "globalObject";
inline constexpr const char* kWeather = "weather";
inline constexpr const char* kIndoors = "indoors";
inline constexpr const char* kUnk = "unk";
inline constexpr const char* kSeq = "seq";
inline constexpr const char* kNatureAmbience = "natureAmbience";
inline constexpr const char* kReverb = "reverb";
inline constexpr const char* kCameraMovement = "cameraMovement";
inline constexpr const char* kWorldMapArea = "worldMapArea";
inline constexpr const char* kGameplayFlags = "gameplayFlags";
inline constexpr const char* kGameplayFlags2 = "gameplayFlags2";
inline constexpr const char* kHour = "hour";
inline constexpr const char* kMinute = "minute";
inline constexpr const char* kIncrement = "increment";
inline constexpr const char* kSkyboxDisabled = "skyboxDisabled";
inline constexpr const char* kSunMoonDisabled = "sunMoonDisabled";
inline constexpr const char* kWest = "west";
inline constexpr const char* kVertical = "vertical";
inline constexpr const char* kSouth = "south";
inline constexpr const char* kSpeed = "speed";

// Lighting entry (§4.2)
inline constexpr const char* kAmbient = "ambient";
inline constexpr const char* kLight1Dir = "light1Dir";
inline constexpr const char* kLight1Color = "light1Color";
inline constexpr const char* kLight2Dir = "light2Dir";
inline constexpr const char* kLight2Color = "light2Color";
inline constexpr const char* kFogColor = "fogColor";
inline constexpr const char* kFogNear = "fogNear";
inline constexpr const char* kFogFar = "fogFar";
inline constexpr const char* kFogStart = "fogStart";
inline constexpr const char* kFogEnd = "fogEnd";
inline constexpr const char* kDrawDistance = "drawDistance";
inline constexpr const char* kNearPlane = "nearPlane";
inline constexpr const char* kFogBlendRate = "fogBlendRate";

// Mesh (§4.3)
inline constexpr const char* kEntries = "entries";
inline constexpr const char* kOpa = "opa";
inline constexpr const char* kXlu = "xlu";
inline constexpr const char* kFormat = "format";
inline constexpr const char* kImage = "image";
inline constexpr const char* kImages = "images";
inline constexpr const char* kSource = "source";
inline constexpr const char* kTlut = "tlut";
inline constexpr const char* kWidth = "width";
inline constexpr const char* kHeight = "height";
inline constexpr const char* kFmt = "fmt";
inline constexpr const char* kSiz = "siz";
inline constexpr const char* kMode0 = "mode0";
inline constexpr const char* kTlutCount = "tlutCount";
inline constexpr const char* kUnk00 = "unk00";
inline constexpr const char* kUnk0C = "unk0C";

// Collision (§4.4)
inline constexpr const char* kBounds = "bounds";
inline constexpr const char* kMin = "min";
inline constexpr const char* kMax = "max";
inline constexpr const char* kBulk = "bulk";
inline constexpr const char* kFile = "file";
inline constexpr const char* kVertices = "vertices";
inline constexpr const char* kPolys = "polys";
inline constexpr const char* kSurfaceTypes = "surfaceTypes";
inline constexpr const char* kData0 = "data0"; // legacy packed form
inline constexpr const char* kData1 = "data1"; // legacy packed form
inline constexpr const char* kCamera = "camera";
inline constexpr const char* kExit = "exit";
inline constexpr const char* kFloorType = "floorType";
inline constexpr const char* kWallFlags = "wallFlags";
inline constexpr const char* kWallType = "wallType";
inline constexpr const char* kFloorProperty = "floorProperty";
inline constexpr const char* kIsSoft = "isSoft";
inline constexpr const char* kIsHorseBlocked = "isHorseBlocked";
inline constexpr const char* kMaterial = "material";
inline constexpr const char* kFloorEffect = "floorEffect";
inline constexpr const char* kLightSetting = "lightSetting";
inline constexpr const char* kCanHookshot = "canHookshot";
inline constexpr const char* kConveyorSpeed = "conveyorSpeed";
inline constexpr const char* kConveyorDirection = "conveyorDirection";
inline constexpr const char* kIsWallDamage = "isWallDamage";
inline constexpr const char* kNotSwimmable = "notSwimmable";
inline constexpr const char* kCameras = "cameras";
inline constexpr const char* kSType = "sType";
inline constexpr const char* kCount = "count";
inline constexpr const char* kPositionIndex = "positionIndex";
inline constexpr const char* kCameraPositions = "cameraPositions";
inline constexpr const char* kWaterBoxes = "waterBoxes";
inline constexpr const char* kXMin = "xMin";
inline constexpr const char* kYSurface = "ySurface";
inline constexpr const char* kZMin = "zMin";
inline constexpr const char* kXLength = "xLength";
inline constexpr const char* kZLength = "zLength";
inline constexpr const char* kProperties = "properties"; // legacy packed form

// Paths (§4.5)
inline constexpr const char* kPoints = "points";

// Text (§5): text/<lang>/messages.json
inline constexpr const char* kMessagesPathPrefix = "text/";
inline constexpr const char* kMessagesPathSuffix = "/messages.json";
inline constexpr const char* kMessages = "messages";
inline constexpr const char* kBox = "box";
inline constexpr const char* kYPos = "ypos";
inline constexpr const char* kText = "text";

// Scene registry (§7): unbound/scenes.json
inline constexpr const char* kRegistryPath = "unbound/scenes.json";
inline constexpr const char* kName = "name";
inline constexpr const char* kScene = "scene";
inline constexpr const char* kSceneId = "sceneId";
inline constexpr const char* kDrawConfig = "drawConfig";
inline constexpr const char* kTitleCardTexture = "titleCardTexture";
inline constexpr const char* kIndex = "index";
inline constexpr const char* kShowTitleCard = "showTitleCard";
inline constexpr const char* kContinueBgm = "continueBgm";
inline constexpr const char* kEndTransition = "endTransition";
inline constexpr const char* kStartTransition = "startTransition";
inline constexpr const char* kLayers = "layers";

// Manifest (§6)
inline constexpr const char* kManifestPath = "unbound.json";
inline constexpr const char* kFormatName = "format";
inline constexpr const char* kFormatVersion = "formatVersion";
inline constexpr const char* kGame = "game";
inline constexpr const char* kSourceInfo = "source";
inline constexpr const char* kRomHash = "romHash";
inline constexpr const char* kRomHashes = "romHashes";
inline constexpr const char* kConverter = "converter";
inline constexpr const char* kFeatures = "features";
inline constexpr const char* kRequires = "requires";
inline constexpr int kCurrentFormatVersion = 2;

} // namespace SOH::Unbound::Schema
