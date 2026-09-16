#pragma once

#include <cstdint>

#include "RuntimeMaterial.hpp"

namespace helengine::ps1 {

/// <summary>Runtime material for the opaque unlit milestone: one base color in 8-bit channels.</summary>
class Ps1RuntimeMaterial final : public ::RuntimeMaterial {
public:
    Ps1RuntimeMaterial(std::uint8_t red, std::uint8_t green, std::uint8_t blue, bool doubleSided);

    std::uint8_t Red() const { return RedValue; }
    std::uint8_t Green() const { return GreenValue; }
    std::uint8_t Blue() const { return BlueValue; }
    bool DoubleSided() const { return DoubleSidedValue; }

private:
    std::uint8_t RedValue;
    std::uint8_t GreenValue;
    std::uint8_t BlueValue;
    bool DoubleSidedValue;
};

}  // namespace helengine::ps1
