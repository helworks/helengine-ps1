#include "platform/ps1/rendering/Ps1RuntimeMaterial.hpp"

namespace helengine::ps1 {

Ps1RuntimeMaterial::Ps1RuntimeMaterial(std::uint8_t red, std::uint8_t green, std::uint8_t blue, bool doubleSided)
    : RedValue(red), GreenValue(green), BlueValue(blue), DoubleSidedValue(doubleSided) {}

}  // namespace helengine::ps1
