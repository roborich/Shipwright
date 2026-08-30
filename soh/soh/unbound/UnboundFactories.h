#pragma once
// SOH [Unbound] Resource factories for the JSON scene format (unbound-docs/SPEC.md §4).
// Each reads its document merged across every mounted archive and builds the same in-memory
// resource the binary/XML factories build, so nothing downstream changes. They register under the
// existing SOH resource types (Room, CollisionHeader, Path) with RESOURCE_FORMAT_JSON and the
// "$schema" version as the discriminator; the "$schema" type names are in UnboundSchema.h.
#include <ship/resource/ResourceFactoryJson.h>

namespace SOH {

// "unbound/scene/1" and "unbound/room/1" -> SOH::Scene (a command list, alternates as child scenes)
class ResourceFactoryJsonSceneV1 final : public Ship::ResourceFactoryJson {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

// "unbound/collision/3" (+ collision.bin) -> SOH::CollisionHeader
class ResourceFactoryJsonCollisionHeaderV3 final : public Ship::ResourceFactoryJson {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

// "unbound/paths/1" -> SOH::Path
class ResourceFactoryJsonPathV1 final : public Ship::ResourceFactoryJson {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

} // namespace SOH
