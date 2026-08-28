#include "SetPathways.h"

namespace SOH {
PathData** SetPathways::GetPointer() {
    return paths.data();
}

size_t SetPathways::GetPointerSize() {
    return paths.size() * sizeof(PathData*);
}

void SetPathways::AddPathResource(const std::shared_ptr<Path>& resource, const std::string& fileName) {
    paths.push_back(resource->GetPointer());
    pathFileNames.push_back(fileName);
    pathResources.push_back(resource);
    pathList.insert(pathList.end(), resource->pathData.begin(), resource->pathData.end());
}

PathData* SetPathways::GetPathList() {
    return pathList.data();
}
} // namespace SOH
