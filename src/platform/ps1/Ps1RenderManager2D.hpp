#pragma once

#include <cstdint>

#include "RenderManager2D.hpp"
#include "platform/ps1/rendering/Ps1RuntimeTexture.hpp"
#include "platform/ps1/rendering/Ps1VramAllocator.hpp"
#include "psyqo/gpu.hh"

class TextureAsset;

namespace helengine::ps1 {

/// <summary>
/// 2D render manager backing Core's scene lifecycle on PS1. It uploads cooked paletted textures into
/// the VRAM the display buffers leave free and hands back their page and palette locations. Drawing
/// 2D content is not implemented yet, so sprite, text and shape requests still fail explicitly
/// rather than quietly drawing nothing.
/// </summary>
class Ps1RenderManager2D final : public ::RenderManager2D {
public:
    /// <summary>Binds the PSYQo GPU that owns VRAM.</summary>
    /// <param name="gpu">GPU used for the VRAM uploads.</param>
    explicit Ps1RenderManager2D(psyqo::GPU& gpu);

    ::RuntimeTexture* BuildTextureFromCooked(HeCppString cookedAssetPath, ::IContentStreamSource* contentStreamSource) override;
    ::RuntimeTexture* BuildTextureFromRaw(::TextureAsset* data) override;
    void DrawRoundedRect(::IRoundedRectDrawable2D* shape) override;
    void DrawSprite(::ISpriteDrawable2D* sprite) override;
    void DrawText(::ITextDrawable2D* text) override;
    void ReleaseTexture(::RuntimeTexture* texture) override;

    /// <summary>Textures uploaded to VRAM since construction.</summary>
    std::uint32_t UploadedTextureCount() const { return UploadedTextures; }

    /// <summary>VRAM words handed out to textures and palettes.</summary>
    std::uint32_t UsedVramWords() const { return VramAllocator.UsedWords(); }

protected:
    void UpdateTextureRegionCore(::RuntimeTexture* texture, int32_t x, int32_t y, int32_t width, int32_t height,
                                 Array<uint8_t>* rgba8, int32_t sourceRowPitch) override;

private:
    /// <summary>Uploads one decoded paletted texture asset into VRAM.</summary>
    /// <param name="asset">Cooked texture asset carrying indexed pixels and a palette.</param>
    /// <returns>Runtime texture naming the VRAM page and palette.</returns>
    ::RuntimeTexture* UploadPalettedTexture(::TextureAsset* asset);

    psyqo::GPU& Gpu;
    Ps1VramAllocator VramAllocator;
    std::uint32_t UploadedTextures = 0;
};

}  // namespace helengine::ps1
