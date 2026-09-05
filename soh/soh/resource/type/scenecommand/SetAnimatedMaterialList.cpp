#include "SetAnimatedMaterialList.h"

namespace SOH {

AnimatedMaterial* SetAnimatedMaterialList::GetPointer() {
    return entries.data();
}

size_t SetAnimatedMaterialList::GetPointerSize() {
    return entries.size() * sizeof(AnimatedMaterial);
}

static AnimatedMaterial MakeEntry(u8 segment, u8 pass, u8 type, void* params) {
    AnimatedMaterial e{};
    e.segment = segment;
    e.pass = pass;
    e.type = type;
    e.params = params;
    return e;
}

AnimatedMaterial& SetAnimatedMaterialList::AddScroll(u8 segment, u8 pass, u8 type, const ScrollStorage& storage) {
    scrolls.push_back(storage);
    entries.push_back(MakeEntry(segment, pass, type, scrolls.back().layers));
    return entries.back();
}

AnimatedMaterial& SetAnimatedMaterialList::AddColor(u8 segment, u8 pass, u8 type, ColorStorage&& storage) {
    colors.push_back(std::move(storage));
    ColorStorage& c = colors.back();
    c.params.keyFrameCount = (u16)c.keyFrames.size();
    c.params.keyFrames = c.keyFrames.data();
    c.params.primColors = c.primColors.data();
    c.params.envColors = c.envColors.empty() ? nullptr : c.envColors.data();
    entries.push_back(MakeEntry(segment, pass, type, &c.params));
    return entries.back();
}

AnimatedMaterial& SetAnimatedMaterialList::AddCycle(u8 segment, u8 pass, CycleStorage&& storage) {
    cycles.push_back(std::move(storage));
    CycleStorage& c = cycles.back();
    c.textureList.clear();
    c.textureList.reserve(c.texturePaths.size());
    for (const std::string& path : c.texturePaths) {
        c.textureList.push_back((void*)path.c_str());
    }
    c.params.keyFrameLength = (u16)c.frames.size();
    c.params.textureList = c.textureList.data();
    c.params.textureIndexList = c.frames.data();
    entries.push_back(MakeEntry(segment, pass, ANIM_MAT_TEX_CYCLE, &c.params));
    return entries.back();
}

} // namespace SOH
