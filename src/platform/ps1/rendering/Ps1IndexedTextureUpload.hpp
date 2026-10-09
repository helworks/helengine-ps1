#pragma once
#include <cstdint>

namespace helengine::ps1 {

/// <summary>Repackages one row from the engine's continuous Indexed4 stream into zeroed VRAM word storage.</summary>
/// <param name="source">Validated complete Indexed4 pixel buffer.</param>
/// <param name="firstPixel">Absolute texel offset of this row in the source image.</param>
/// <param name="width">Logical row width, which may be odd.</param>
/// <param name="destination">Zeroed row padded to a complete 16-bit VRAM word.</param>
inline void PackPs1Indexed4UploadRow(const std::uint8_t* source, std::uint32_t firstPixel,
                                    std::uint16_t width, std::uint8_t* destination) {
    for (std::uint16_t column = 0; column < width; ++column) {
        const std::uint32_t pixel = firstPixel + column;
        const std::uint8_t index = (source[pixel / 2u] >> ((pixel % 2u) * 4u)) & 15u;
        destination[column / 2u] |= static_cast<std::uint8_t>(index << ((column % 2u) * 4u));
    }
}

} // namespace helengine::ps1
