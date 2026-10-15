#include "platform/ps1/rendering/Ps1RuntimeMaterial.hpp"
#include "platform/ps1/Ps1RenderManager2D.hpp"
#include "platform/ps1/rendering/Ps1RuntimeTexture.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"

namespace helengine::ps1 {

Ps1RuntimeMaterial::Ps1RuntimeMaterial(std::uint8_t red, std::uint8_t green, std::uint8_t blue, bool doubleSided, bool lit)
    : RedValue(red), GreenValue(green), BlueValue(blue), DoubleSidedValue(doubleSided), LitValue(lit) {}

Ps1RuntimeMaterial::~Ps1RuntimeMaterial() { Dispose(); }

void Ps1RuntimeMaterial::SetTexture(Ps1RenderManager2D& owner, Ps1RuntimeTexture* texture) {
    if (DisposedValue || texture == nullptr || texture->get_IsDisposed()) he_cpp_custom::Fail("PS1 material cannot own a null or disposed texture");
    if (TextureValue == texture) {
        if (TextureOwner != &owner) he_cpp_custom::Fail("PS1 material texture has a different allocation owner");
        return;
    }
    if (TextureValue != nullptr) TextureOwner->ReleaseTexture(TextureValue);
    TextureOwner = &owner;
    TextureValue = texture;
    SetPrimaryTexture(texture);
}

void Ps1RuntimeMaterial::Dispose() {
    if (DisposedValue) return;
    DisposedValue = true;
    Ps1RuntimeTexture* texture = TextureValue;
    TextureValue = nullptr;
    SetPrimaryTexture(nullptr);
    if (texture != nullptr) TextureOwner->ReleaseTexture(texture);
    TextureOwner = nullptr;
    ::RuntimeMaterial::Dispose();
}

}  // namespace helengine::ps1
