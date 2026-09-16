#pragma once

#include "RenderManager2D.hpp"

namespace helengine::ps1 {

/// <summary>
/// 2D render manager required by Core's scene lifecycle. The authored milestone has no 2D content,
/// so texture and text requests fail explicitly rather than hiding a retained UI dependency.
/// </summary>
class Ps1RenderManager2D final : public ::RenderManager2D {
public:
    ::RuntimeTexture* BuildTextureFromRaw(::TextureAsset* data) override;
    void DrawRoundedRect(::IRoundedRectDrawable2D* shape) override;
    void DrawSprite(::ISpriteDrawable2D* sprite) override;
    void DrawText(::ITextDrawable2D* text) override;

protected:
    void UpdateTextureRegionCore(::RuntimeTexture* texture, int32_t x, int32_t y, int32_t width, int32_t height,
                                 Array<uint8_t>* rgba8, int32_t sourceRowPitch) override;
};

}  // namespace helengine::ps1
