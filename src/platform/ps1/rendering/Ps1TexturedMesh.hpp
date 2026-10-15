#pragma once

#include <cstdint>

namespace helengine::ps1 {

/** Authored normalized UV, retained separately from the GTE's fixed-point positions. */
struct Ps1MeshUv { float U; float V; };

/** Borrowed texture-page description. Its owning material retains the VRAM allocation. */
struct Ps1MeshTexture {
    std::uint16_t Width, Height, VramX, VramY, ClutX, ClutY, PixelsPerWord;
};

/** Validates the complete image against the GPU's 256 by 256 sampling page. */
inline bool IsPs1MeshTextureValid(const Ps1MeshTexture& texture) {
    if (texture.Width == 0 || texture.Height == 0 || texture.VramX >= 1024 || texture.VramY >= 512 ||
        (texture.PixelsPerWord != 1 && texture.PixelsPerWord != 2 && texture.PixelsPerWord != 4)) return false;
    const unsigned baseU = (texture.VramX % 64u) * texture.PixelsPerWord;
    const unsigned baseV = texture.VramY % 256u;
    const unsigned widthInWords = (texture.Width + texture.PixelsPerWord - 1u) / texture.PixelsPerWord;
    const unsigned paletteEntries = texture.PixelsPerWord == 4 ? 16u : 256u;
    return baseU + texture.Width <= 256u && baseV + texture.Height <= 256u &&
           texture.VramX + widthInWords <= 1024u && texture.VramY + texture.Height <= 512u &&
           (texture.PixelsPerWord == 1 || (texture.ClutX % 16u == 0 && texture.ClutX + paletteEntries <= 1024u && texture.ClutY < 512));
}

/** Maps image endpoints to valid texel centers, never wrapping UV 1 onto an adjacent allocation. */
inline bool TryBuildPs1MeshUv(const Ps1MeshUv& uv, const Ps1MeshTexture& texture,
                             std::uint8_t& u, std::uint8_t& v) {
    if (!IsPs1MeshTextureValid(texture) || !__builtin_isfinite(uv.U) || !__builtin_isfinite(uv.V) ||
        uv.U < 0.0f || uv.U > 1.0f || uv.V < 0.0f || uv.V > 1.0f) return false;
    u = static_cast<std::uint8_t>((texture.VramX % 64u) * texture.PixelsPerWord +
                                  static_cast<unsigned>(uv.U * (texture.Width - 1u) + 0.5f));
    v = static_cast<std::uint8_t>(texture.VramY % 256u +
                                  static_cast<unsigned>(uv.V * (texture.Height - 1u) + 0.5f));
    return true;
}

/** PS1 modulation has unity at 128, so authored/GTE 255 must be halved for textured primitives. */
inline std::uint8_t Ps1TextureModulation(std::uint8_t channel) {
    return static_cast<std::uint8_t>((static_cast<unsigned>(channel) + 1u) / 2u);
}

}  // namespace helengine::ps1
