#include "platform/ps1/rendering/Ps1RuntimeModel.hpp"

#include "ModelAsset.hpp"
#include "float3.hpp"
#include "float2.hpp"
#include "platform/ps1/Ps1Math.hpp"
#include "platform/ps1/rendering/Ps1AuthoredLighting.hpp"
#include "platform/ps1/rendering/Ps1AuthoredProjection.hpp"
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
    if (asset->TexCoords != nullptr && asset->TexCoords->Length != 0 && asset->TexCoords->Length != vertexCount) return false;
    if (asset->TexCoords != nullptr) {
        for (int32_t index = 0; index < asset->TexCoords->Length; ++index) {
            if (!__builtin_isfinite(asset->TexCoords->Data[index].X) || !__builtin_isfinite(asset->TexCoords->Data[index].Y)) return false;
        }
    }
    std::int16_t* vertices = new std::int16_t[static_cast<std::uint32_t>(vertexCount) * 3u];
    float radius = 0.0f;
    for (int32_t index = 0; index < vertexCount; ++index) {
        const ::float3& position = asset->Positions->Data[index];
        std::int16_t converted[3];
        if (!Ps1AuthoredProjection::TryConvertVertex(Ps1Vector3{position.X, position.Y, position.Z}, converted)) {
            delete[] vertices;
            return false;
        }
        vertices[index * 3] = converted[0];
        vertices[index * 3 + 1] = converted[1];
        vertices[index * 3 + 2] = converted[2];
        const float length = sqrt(position.X * position.X + position.Y * position.Y + position.Z * position.Z);
        if (length > radius) radius = length;
    }
    std::int16_t* normals = nullptr;
    if (asset->Normals != nullptr && asset->Normals->Length == vertexCount) {
        normals = new std::int16_t[static_cast<std::uint32_t>(vertexCount) * 3u];
        for (int32_t index = 0; index < vertexCount; ++index) {
            const ::float3& normal = asset->Normals->Data[index];
            std::int16_t converted[3];
            if (!Ps1AuthoredLighting::TryConvertNormal(Ps1Vector3{normal.X, normal.Y, normal.Z}, converted)) {
                delete[] vertices;
                delete[] normals;
                return false;
            }
            normals[index * 3] = converted[0];
            normals[index * 3 + 1] = converted[1];
            normals[index * 3 + 2] = converted[2];
        }
    }
    IndicesValue = new std::uint16_t[static_cast<std::uint32_t>(indexCount)];
    for (int32_t index = 0; index < indexCount; ++index) {
        IndicesValue[index] = asset->Indices16->Data[index];
    }
    VerticesValue = vertices;
    NormalsValue = normals;
    if (asset->TexCoords != nullptr && asset->TexCoords->Length == vertexCount) {
        TextureCoordinatesValue = new Ps1MeshUv[static_cast<std::uint32_t>(vertexCount)];
        for (int32_t index = 0; index < vertexCount; ++index) {
            TextureCoordinatesValue[index] = Ps1MeshUv{asset->TexCoords->Data[index].X, asset->TexCoords->Data[index].Y};
        }
    }
    VertexCountValue = static_cast<std::uint32_t>(vertexCount);
    IndexCountValue = static_cast<std::uint32_t>(indexCount);
    BoundingRadiusValue = radius;
    return true;
}

void Ps1RuntimeModel::Dispose() {
    delete[] VerticesValue;
    delete[] NormalsValue;
    delete[] IndicesValue;
    delete[] TextureCoordinatesValue;
    VerticesValue = nullptr;
    NormalsValue = nullptr;
    IndicesValue = nullptr;
    TextureCoordinatesValue = nullptr;
    VertexCountValue = 0;
    IndexCountValue = 0;
    BoundingRadiusValue = 0.0f;
}

}  // namespace helengine::ps1
