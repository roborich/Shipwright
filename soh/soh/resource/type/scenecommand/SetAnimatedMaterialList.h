#pragma once
// SOH [Unbound] The `materialAnims` scene command (SPEC.md §4.2): the Majora's Mask animated-material list, built
// only from a scene document (no binary form exists in OoT). The command owns every byte the C list points into.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include <memory>
#include <ship/resource/Resource.h>
#include "SceneCommand.h"
#include "z64scene.h"

namespace SOH {

class SetAnimatedMaterialList : public SceneCommand<AnimatedMaterial> {
  public:
    using SceneCommand::SceneCommand;

    AnimatedMaterial* GetPointer();
    size_t GetPointerSize();

    // Per-entry parameter storage. `entries[i].params` points at one element of the deque for its type; a
    // std::deque never moves an element on push_back, and the inner vectors are filled before the C params
    // struct is pointed at them and never resized after.
    struct ScrollStorage {
        AnimatedMatTexScrollParams layers[2]; // [0] alone for texScroll, both for twoTexScroll
    };
    struct ColorStorage {
        AnimatedMatColorParams params;
        std::vector<u16> keyFrames;
        std::vector<F3DPrimColor> primColors;
        std::vector<F3DEnvColor> envColors; // empty = no env colors
    };
    struct CycleStorage {
        AnimatedMatTexCycleParams params;
        std::vector<std::string> texturePaths; // "__OTR__<path>"; the interpreter resolves the name at draw
        std::vector<void*> textureList;        // c_str() of the above
        std::vector<u8> frames;
    };

    AnimatedMaterial& AddScroll(u8 segment, u8 pass, u8 type, const ScrollStorage& storage);
    AnimatedMaterial& AddColor(u8 segment, u8 pass, u8 type, ColorStorage&& storage);
    AnimatedMaterial& AddCycle(u8 segment, u8 pass, CycleStorage&& storage);

    std::vector<AnimatedMaterial> entries;
    std::deque<ScrollStorage> scrolls;
    std::deque<ColorStorage> colors;
    std::deque<CycleStorage> cycles;
};

} // namespace SOH
