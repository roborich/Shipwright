#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <ship/resource/Resource.h>
#include "SceneCommand.h"
// #include <libultraship/libultra/types.h>
#include "soh/resource/type/Path.h"

namespace SOH {

class SetPathways : public SceneCommand<PathData*> {
  public:
    using SceneCommand::SceneCommand;

    PathData** GetPointer();
    size_t GetPointerSize();

    // SOH [Unbound] Appends every path of `resource` to `pathList`, so a setup may list several path
    // documents; a path index is its position in the concatenation. Keeps the resource alive.
    void AddPathResource(const std::shared_ptr<Path>& resource, const std::string& fileName);
    PathData* GetPathList();

    uint32_t numPaths;
    std::vector<std::string> pathFileNames;
    std::vector<PathData*> paths;
    std::vector<PathData> pathList;                              // SOH [Unbound] all documents, in order
    std::vector<std::shared_ptr<Ship::IResource>> pathResources; // SOH [Unbound] owners of the point data
};
}; // namespace SOH
