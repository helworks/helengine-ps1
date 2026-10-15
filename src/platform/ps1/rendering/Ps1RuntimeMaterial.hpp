#pragma once

#include <cstdint>

#include "RuntimeMaterial.hpp"

namespace helengine::ps1 {
class Ps1RenderManager2D;
class Ps1RuntimeTexture;

/// <summary>
/// Runtime material for the opaque milestone: one base color in 8-bit channels, plus whether the
/// cooked material asked for per-vertex directional shading on the GTE instead of a flat color.
/// </summary>
class Ps1RuntimeMaterial final : public ::RuntimeMaterial {
public:
    Ps1RuntimeMaterial(std::uint8_t red, std::uint8_t green, std::uint8_t blue, bool doubleSided, bool lit);
    ~Ps1RuntimeMaterial() override;
    /** Owns one texture and releases it through the renderer's fenced VRAM lifecycle. */
    void SetTexture(Ps1RenderManager2D& owner, Ps1RuntimeTexture* texture);
    Ps1RuntimeTexture* Texture() const { return TextureValue; }
    void Dispose() override;

    std::uint8_t Red() const { return RedValue; }
    std::uint8_t Green() const { return GreenValue; }
    std::uint8_t Blue() const { return BlueValue; }
    bool DoubleSided() const { return DoubleSidedValue; }

    /// <summary>True when the cooked material requested GTE per-vertex directional shading.</summary>
    bool Lit() const { return LitValue; }

private:
    std::uint8_t RedValue;
    std::uint8_t GreenValue;
    std::uint8_t BlueValue;
    bool DoubleSidedValue;
    bool LitValue;
    Ps1RenderManager2D* TextureOwner = nullptr;
    Ps1RuntimeTexture* TextureValue = nullptr;
    bool DisposedValue = false;
};

}  // namespace helengine::ps1
