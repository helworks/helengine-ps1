#include "platform/ps1/rendering/Ps1RuntimeModel.hpp"

#include "ModelAsset.hpp"
#include "float3.hpp"
#include "runtime/array.hpp"

namespace helengine::ps1 {

Ps1RuntimeModel::Ps1RuntimeModel() = default;

Ps1RuntimeModel::~Ps1RuntimeModel() {
    Dispose();
}

bool Ps1RuntimeModel::Initialize(const ::ModelAsset* asset) {
    Dispose();
    if (asset == nullptr || asset->Positions == nullptr || asset->Indices16 == nullptr) {
        return false;
    }
    const int32_t vertexCount = asset->Positions->Length;
    const int32_t indexCount = asset->Indices16->Length;
    if (vertexCount <= 0 || indexCount <= 0 || indexCount % 3 != 0 ||
        static_cast<std::uint32_t>(vertexCount) > MaxVertices || static_cast<std::uint32_t>(indexCount) > MaxIndices) {
        return false;
    }
    for (int32_t index = 0; index < indexCount; ++index) {
        if (asset->Indices16->Data[index] >= static_cast<std::uint16_t>(vertexCount)) {
            return false;
        }
    }
    PositionsValue = new ::float3[static_cast<std::uint32_t>(vertexCount)];
    IndicesValue = new std::uint16_t[static_cast<std::uint32_t>(indexCount)];
    for (int32_t index = 0; index < vertexCount; ++index) {
        PositionsValue[index] = asset->Positions->Data[index];
    }
    for (int32_t index = 0; index < indexCount; ++index) {
        IndicesValue[index] = asset->Indices16->Data[index];
    }
    VertexCountValue = static_cast<std::uint32_t>(vertexCount);
    IndexCountValue = static_cast<std::uint32_t>(indexCount);
    return true;
}

void Ps1RuntimeModel::Dispose() {
    delete[] PositionsValue;
    delete[] IndicesValue;
    PositionsValue = nullptr;
    IndicesValue = nullptr;
    VertexCountValue = 0;
    IndexCountValue = 0;
}

}  // namespace helengine::ps1
