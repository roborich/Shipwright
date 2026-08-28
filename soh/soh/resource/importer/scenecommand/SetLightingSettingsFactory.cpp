#include "soh/resource/importer/scenecommand/SetLightingSettingsFactory.h"
#include "soh/resource/type/scenecommand/SetLightingSettings.h"
#include "soh/resource/logging/SceneCommandLoggers.h"
#include "spdlog/spdlog.h"
#include <tinyxml2.h>
#include "z64environment.h"

// SOH [Unbound] SOH::EnvLightSettings mirrors the game struct field for field; Scene_CommandLightingSettings
// hands the vector's storage straight to the game.
static_assert(sizeof(SOH::EnvLightSettings) == sizeof(::EnvLightSettings), "EnvLightSettings mirror out of sync");
static_assert(offsetof(SOH::EnvLightSettings, worldFog) == offsetof(::EnvLightSettings, worldFog),
              "EnvLightSettings mirror out of sync");

namespace SOH {
std::shared_ptr<Ship::IResource>
SetLightingSettingsFactory::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                         std::shared_ptr<Ship::BinaryReader> reader) {
    auto setLightingSettings = std::make_shared<SetLightingSettings>(initData);

    ReadCommandId(setLightingSettings, reader);

    uint32_t count = reader->ReadInt32();
    setLightingSettings->settings.reserve(count);

    for (uint32_t i = 0; i < count; i++) {
        EnvLightSettings lightSettings{}; // SOH [Unbound] value-init: the world-fog fields have no binary source
        lightSettings.ambientColor[0] = reader->ReadInt8();
        lightSettings.ambientColor[1] = reader->ReadInt8();
        lightSettings.ambientColor[2] = reader->ReadInt8();

        lightSettings.light1Dir[0] = reader->ReadInt8();
        lightSettings.light1Dir[1] = reader->ReadInt8();
        lightSettings.light1Dir[2] = reader->ReadInt8();

        lightSettings.light1Color[0] = reader->ReadInt8();
        lightSettings.light1Color[1] = reader->ReadInt8();
        lightSettings.light1Color[2] = reader->ReadInt8();

        lightSettings.light2Dir[0] = reader->ReadInt8();
        lightSettings.light2Dir[1] = reader->ReadInt8();
        lightSettings.light2Dir[2] = reader->ReadInt8();

        lightSettings.light2Color[0] = reader->ReadInt8();
        lightSettings.light2Color[1] = reader->ReadInt8();
        lightSettings.light2Color[2] = reader->ReadInt8();

        lightSettings.fogColor[0] = reader->ReadInt8();
        lightSettings.fogColor[1] = reader->ReadInt8();
        lightSettings.fogColor[2] = reader->ReadInt8();

        lightSettings.fogNear = reader->ReadInt16();
        lightSettings.fogFar = reader->ReadUInt16();
        setLightingSettings->settings.push_back(lightSettings);
    }

    if (CVarGetInteger(CVAR_DEVELOPER_TOOLS("ResourceLogging"), 0)) {
        LogLightingSettingsAsXML(setLightingSettings);
    }

    return setLightingSettings;
}

std::shared_ptr<Ship::IResource>
SetLightingSettingsFactoryXML::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                            tinyxml2::XMLElement* reader) {
    auto setLightingSettings = std::make_shared<SetLightingSettings>(initData);

    setLightingSettings->cmdId = SceneCommandID::SetLightingSettings;

    auto child = reader->FirstChildElement();

    while (child != nullptr) {
        std::string childName = child->Name();
        if (childName == "LightingSetting") {
            EnvLightSettings lightSettings{}; // SOH [Unbound] value-init: the world-fog fields have no XML source
            lightSettings.ambientColor[0] = child->IntAttribute("AmbientColorR");
            lightSettings.ambientColor[1] = child->IntAttribute("AmbientColorG");
            lightSettings.ambientColor[2] = child->IntAttribute("AmbientColorB");

            lightSettings.light1Dir[0] = child->IntAttribute("Light1DirX");
            lightSettings.light1Dir[1] = child->IntAttribute("Light1DirY");
            lightSettings.light1Dir[2] = child->IntAttribute("Light1DirZ");
            lightSettings.light1Color[0] = child->IntAttribute("Light1ColorR");
            lightSettings.light1Color[1] = child->IntAttribute("Light1ColorG");
            lightSettings.light1Color[2] = child->IntAttribute("Light1ColorB");

            lightSettings.light2Dir[0] = child->IntAttribute("Light2DirX");
            lightSettings.light2Dir[1] = child->IntAttribute("Light2DirY");
            lightSettings.light2Dir[2] = child->IntAttribute("Light2DirZ");
            lightSettings.light2Color[0] = child->IntAttribute("Light2ColorR");
            lightSettings.light2Color[1] = child->IntAttribute("Light2ColorG");
            lightSettings.light2Color[2] = child->IntAttribute("Light2ColorB");

            lightSettings.fogColor[0] = child->IntAttribute("FogColorR");
            lightSettings.fogColor[1] = child->IntAttribute("FogColorG");
            lightSettings.fogColor[2] = child->IntAttribute("FogColorB");
            lightSettings.fogNear = child->IntAttribute("FogNear");
            lightSettings.fogFar = child->IntAttribute("FogFar");
            setLightingSettings->settings.push_back(lightSettings);
        }

        child = child->NextSiblingElement();
    }

    return setLightingSettings;
}
} // namespace SOH
