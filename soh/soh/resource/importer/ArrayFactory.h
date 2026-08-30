#pragma once

#include <ship/resource/Resource.h>
#include <ship/resource/ResourceFactoryBinary.h>

namespace SOH {
// The generic Array resource (OARR). An array whose type is Vertex loads as a Fast::Vertex - that is how every
// vertex in the game is stored, and the interpreter asks that resource for its record size - every other type
// loads as SOH::Array.
class ResourceFactoryBinaryArrayV0 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

// SOH [Unbound] v1: a Vertex array with s32 positions (SPEC.md §8.1). Only the Vertex type is defined at v1;
// any other type is refused.
class ResourceFactoryBinaryArrayV1 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};
} // namespace SOH
