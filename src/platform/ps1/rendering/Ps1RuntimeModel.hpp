#pragma once

#include <cstdint>

#include "RuntimeModel.hpp"

class ModelAsset;
class float3;

namespace helengine::ps1 {

/// <summary>
/// Runtime model built from a cooked engine model asset: a copy of its positions and 16-bit
/// triangle indices in engine units. The GTE renderer converts these at submission time.
/// </summary>
class Ps1RuntimeModel final : public ::RuntimeModel {
public:
    Ps1RuntimeModel();
    ~Ps1RuntimeModel() override;

    /// <summary>Copies the asset's positions and indices; returns false when the asset is not a bounded 16-bit triangle list.</summary>
    bool Initialize(const ::ModelAsset* asset);

    /// <summary>Releases the copied geometry.</summary>
    void Dispose() override;

    const ::float3* Positions() const { return PositionsValue; }
    const std::uint16_t* Indices() const { return IndicesValue; }
    std::uint32_t VertexCount() const { return VertexCountValue; }
    std::uint32_t IndexCount() const { return IndexCountValue; }

    /// <summary>Largest vertex count the authored milestone accepts for one model.</summary>
    static constexpr std::uint32_t MaxVertices = 256;

    /// <summary>Largest index count the authored milestone accepts for one model.</summary>
    static constexpr std::uint32_t MaxIndices = 768;

private:
    ::float3* PositionsValue = nullptr;
    std::uint16_t* IndicesValue = nullptr;
    std::uint32_t VertexCountValue = 0;
    std::uint32_t IndexCountValue = 0;
};

}  // namespace helengine::ps1
