#include "platform/ps1/Ps1RenderManager2D.hpp"

#include "platform/ps1/Ps1RuntimeProvider.hpp"

namespace helengine::ps1 {

::RuntimeTexture* Ps1RenderManager2D::BuildTextureFromRaw(::TextureAsset*) {
    he_cpp_custom::Fail("PS1 authored scenes do not use textures");
}

void Ps1RenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D*) {
    he_cpp_custom::Fail("PS1 authored scenes do not draw 2D shapes");
}

void Ps1RenderManager2D::DrawSprite(::ISpriteDrawable2D*) {
    he_cpp_custom::Fail("PS1 authored scenes do not draw sprites");
}

void Ps1RenderManager2D::DrawText(::ITextDrawable2D*) {
    he_cpp_custom::Fail("PS1 authored scenes do not draw text");
}

void Ps1RenderManager2D::UpdateTextureRegionCore(::RuntimeTexture*, int32_t, int32_t, int32_t, int32_t, Array<uint8_t>*, int32_t) {
    he_cpp_custom::Fail("PS1 authored scenes do not update textures");
}

}  // namespace helengine::ps1
