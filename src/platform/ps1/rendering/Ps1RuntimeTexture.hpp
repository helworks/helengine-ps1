#pragma once

#include <cstdint>

#include "RuntimeTexture.hpp"

namespace helengine::ps1 {

/// <summary>
/// Runtime texture resident in VRAM. The GPU samples a texture page with an optional
/// colour lookup table, so the runtime form is VRAM locations plus the pixel size, not a
/// pixel buffer in main memory. Mutable raw RGBA textures retain a packed 16-bit CPU shadow;
/// cooked textures retain no pixels after upload.
/// </summary>
class Ps1RuntimeTexture final : public ::RuntimeTexture {
public:
    /// <summary>Native texture depths: indexed four/eight-bit or direct sixteen-bit BGR555.</summary>
    enum class Depth : std::uint8_t { FourBit = 4, EightBit = 8, SixteenBit = 16 };

    /// <summary>Releases an owned CPU shadow even when the caller only deletes the runtime texture.</summary>
    ~Ps1RuntimeTexture() override { Dispose(); }

    /// <summary>Marks disposed; the manager releases both reservations after GPU completion.</summary>
    void Dispose() override {
        if (MutablePixels != nullptr) {
            ReleaseMutablePixels(MutablePixels);
            MutablePixels = nullptr;
        }
        set_IsDisposed(true);
    }

    /// <summary>Owned BGR5551 rows retained for raw RGBA updates, or null for immutable cooked assets.</summary>
    std::uint16_t* MutablePixels = nullptr;
    /// <summary>Matching release callback installed with MutablePixels; does not own or release VRAM.</summary>
    void (*ReleaseMutablePixels)(void*) = nullptr;
    /// <summary>Borrowed renderer identity used to reject updates through a different manager.</summary>
    const void* MutableOwner = nullptr;
    /// <summary>Forces input coverage visible; otherwise alpha below 128 becomes transparent zero.</summary>
    bool MutableOpaque = false;

    /// <summary>Rejects immutable assets, disposed shadows and textures owned by a different renderer.</summary>
    /// <param name="owner">Renderer attempting the update.</param>
    /// <returns>True only while that renderer's direct-color CPU shadow is alive.</returns>
    bool CanUpdatePixels(const void* owner) {
        return owner != nullptr && owner == MutableOwner && !get_IsDisposed() && MutablePixels != nullptr &&
               ReleaseMutablePixels != nullptr && PixelDepth == Depth::SixteenBit;
    }

    /// <summary>VRAM column of the texture's top-left corner, in 16-bit words.</summary>
    std::uint16_t VramX = 0;

    /// <summary>VRAM row of the texture's top-left corner.</summary>
    std::uint16_t VramY = 0;

    /// <summary>VRAM column of the colour lookup table.</summary>
    std::uint16_t ClutX = 0;

    /// <summary>VRAM row of the colour lookup table.</summary>
    std::uint16_t ClutY = 0;

    /// <summary>Transport width used to release the texture's VRAM reservation.</summary>
    std::uint16_t WidthInWords = 0;
    /// <summary>CLUT reservation length, or zero for direct-color textures.</summary>
    std::uint16_t PaletteEntries = 0;

    /// <summary>Bit depth the GPU must sample this texture with.</summary>
    Depth PixelDepth = Depth::EightBit;

    /// <summary>Pixels packed into one 16-bit VRAM word at this depth.</summary>
    std::uint16_t PixelsPerWord() const { return 16u / static_cast<std::uint16_t>(PixelDepth); }
};

}  // namespace helengine::ps1
