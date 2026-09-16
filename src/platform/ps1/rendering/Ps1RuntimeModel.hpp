#pragma once

#include <cstdint>

#include "RuntimeModel.hpp"

class ModelAsset;

namespace helengine::ps1 {

/// <summary>
/// Runtime model built from a cooked engine model asset: its positions converted to 16-bit GTE
/// units at unit scale (Ps1AuthoredProjection::UnitsPerEngineUnit), its 16-bit triangle indices,
/// and the bounding radius in engine units used to reject unsafe poses before submission.
/// </summary>
class Ps1RuntimeModel final : public ::RuntimeModel {
public:
    Ps1RuntimeModel();
    ~Ps1RuntimeModel() override;

    /// <summary>Converts the asset; returns false when it is not a bounded 16-bit triangle list in GTE range.</summary>
    bool Initialize(const ::ModelAsset* asset);

    /// <summary>Releases the converted geometry.</summary>
    void Dispose() override;

    /// <summary>Interleaved x, y, z GTE-unit positions, three per vertex.</summary>
    const std::int16_t* GteVertices() const { return VerticesValue; }
    const std::uint16_t* Indices() const { return IndicesValue; }
    std::uint32_t VertexCount() const { return VertexCountValue; }
    std::uint32_t IndexCount() const { return IndexCountValue; }

    /// <summary>Largest vertex distance from the model origin in engine units, before object scale.</summary>
    float BoundingRadius() const { return BoundingRadiusValue; }

    /// <summary>Largest vertex count the authored milestone accepts for one model.</summary>
    static constexpr std::uint32_t MaxVertices = 256;

    /// <summary>Largest index count the authored milestone accepts for one model.</summary>
    static constexpr std::uint32_t MaxIndices = 768;

private:
    std::int16_t* VerticesValue = nullptr;
    std::uint16_t* IndicesValue = nullptr;
    std::uint32_t VertexCountValue = 0;
    std::uint32_t IndexCountValue = 0;
    float BoundingRadiusValue = 0.0f;
};

}  // namespace helengine::ps1
