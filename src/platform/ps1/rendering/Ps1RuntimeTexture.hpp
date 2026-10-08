#pragma once

#include <cstdint>

#include "RuntimeTexture.hpp"

namespace helengine::ps1 {

/// <summary>
/// Runtime texture resident in VRAM. The GPU samples a paletted texture from a texture page and a
/// colour lookup table, so the runtime form is a pair of VRAM locations plus the pixel size, not a
/// pixel buffer in main memory. Nothing is retained in RAM after the upload.
/// </summary>
class Ps1RuntimeTexture final : public ::RuntimeTexture {
public:
    /// <summary>Bits per pixel the GPU samples with; the PS1 supports 4 and 8 for paletted textures.</summary>
    enum class Depth : std::uint8_t { FourBit = 4, EightBit = 8 };

    /// <summary>Marks disposed; the manager releases both reservations after GPU completion.</summary>
    void Dispose() override {
        set_IsDisposed(true);
    }

    /// <summary>VRAM column of the texture's top-left corner, in 16-bit words.</summary>
    std::uint16_t VramX = 0;

    /// <summary>VRAM row of the texture's top-left corner.</summary>
    std::uint16_t VramY = 0;

    /// <summary>VRAM column of the colour lookup table.</summary>
    std::uint16_t ClutX = 0;

    /// <summary>VRAM row of the colour lookup table.</summary>
    std::uint16_t ClutY = 0;

    std::uint16_t WidthInWords = 0;
    std::uint16_t PaletteEntries = 0;

    /// <summary>Bit depth the GPU must sample this texture with.</summary>
    Depth PixelDepth = Depth::EightBit;

    /// <summary>Pixels packed into one 16-bit VRAM word at this depth.</summary>
    std::uint16_t PixelsPerWord() const { return PixelDepth == Depth::FourBit ? 4u : 2u; }
};

}  // namespace helengine::ps1
