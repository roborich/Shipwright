#include "soh/resource/importer/ArrayFactory.h"
#include "soh/resource/type/Array.h"
#include "spdlog/spdlog.h"
#include <libultraship/libultra/gbi.h> // full Vtx: Vertex.h only forward-declares it
#include <fast/resource/type/Vertex.h>
#include <fast/resource/factory/VertexFactory.h>

namespace SOH {
namespace {

// Layout after the resource header: u32 arrayType, u32 count, then the records.
std::shared_ptr<Fast::Vertex> ReadVertexArray(Ship::BinaryReader& reader, uint32_t count,
                                              std::shared_ptr<Ship::ResourceInitData> initData, bool s32Positions) {
    auto vertex = std::make_shared<Fast::Vertex>(initData);
    Fast::ReadVertexRecords(reader, *vertex, count, s32Positions);
    return vertex;
}

std::shared_ptr<Array> ReadScalarArray(Ship::BinaryReader& reader, ArrayResourceType arrayType, uint32_t count,
                                       std::shared_ptr<Ship::ResourceInitData> initData) {
    auto array = std::make_shared<Array>(initData);
    array->ArrayType = arrayType;
    array->ArrayCount = count;

    for (uint32_t i = 0; i < count; i++) {
        array->ArrayScalarType = (ScalarType)reader.ReadUInt32();

        int iter = 1;

        if (array->ArrayType == ArrayResourceType::Vector) {
            iter = reader.ReadUInt32();
        }

        for (int k = 0; k < iter; k++) {
            ScalarData data;

            switch (array->ArrayScalarType) {
                case ScalarType::ZSCALAR_S16:
                    data.s16 = reader.ReadInt16();
                    break;
                case ScalarType::ZSCALAR_U16:
                    data.u16 = reader.ReadUInt16();
                    break;
                default:
                    // OTRTODO: IMPLEMENT OTHER TYPES!
                    break;
            }

            array->Scalars.push_back(data);
        }
    }

    return array;
}

} // namespace

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryArrayV0::ReadResource(std::shared_ptr<Ship::File> file,
                                           std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);
    auto arrayType = (ArrayResourceType)reader->ReadUInt32();
    uint32_t count = reader->ReadUInt32();

    if (arrayType == ArrayResourceType::Vertex) {
        return ReadVertexArray(*reader, count, initData, false);
    }
    return ReadScalarArray(*reader, arrayType, count, initData);
}

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryArrayV1::ReadResource(std::shared_ptr<Ship::File> file,
                                           std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);
    auto arrayType = (ArrayResourceType)reader->ReadUInt32();
    uint32_t count = reader->ReadUInt32();

    if (arrayType != ArrayResourceType::Vertex) {
        SPDLOG_ERROR("[Unbound] {}: Array version 1 defines only the Vertex type (25); this array is type {}",
                     initData->Path, (uint32_t)arrayType);
        return nullptr;
    }
    return ReadVertexArray(*reader, count, initData, true);
}
} // namespace SOH
