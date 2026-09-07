#include "Array.h"
namespace SOH {
Array::Array() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
}

void* Array::GetPointer() {
    return Scalars.data();
}

size_t Array::GetPointerSize() {
    size_t typeSize = 0;
    switch (ArrayScalarType) {
        case ScalarType::ZSCALAR_S16:
            typeSize = sizeof(int16_t);
            break;
        case ScalarType::ZSCALAR_U16:
            typeSize = sizeof(uint16_t);
            break;
        default:
            // OTRTODO: IMPLEMENT OTHER TYPES!
            break;
    }
    return ArrayCount * typeSize;
}
} // namespace SOH
