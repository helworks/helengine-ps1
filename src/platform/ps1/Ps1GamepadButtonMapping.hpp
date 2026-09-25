#pragma once

#include <cstdint>

namespace helengine::ps1 {

/// <summary>Maps the sixteen digital PS1 pad buttons to the Core gamepad button bit positions.</summary>
struct Ps1GamepadButtonMapping final {
    /// <summary>Converts a pressed-button mask indexed by psyqo::SimplePad::Button to Core's bit layout.</summary>
    static constexpr std::uint64_t ToCoreButtonMask(std::uint16_t pressedButtons) {
        std::uint64_t result = 0;
        Map(pressedButtons, 0, 15, result);  // Select
        Map(pressedButtons, 1, 6, result);   // L3
        Map(pressedButtons, 2, 7, result);   // R3
        Map(pressedButtons, 3, 14, result);  // Start
        Map(pressedButtons, 4, 10, result);  // Up
        Map(pressedButtons, 5, 13, result);  // Right
        Map(pressedButtons, 6, 11, result);  // Down
        Map(pressedButtons, 7, 12, result);  // Left
        Map(pressedButtons, 8, 8, result);   // L2
        Map(pressedButtons, 9, 9, result);   // R2
        Map(pressedButtons, 10, 4, result);  // L1
        Map(pressedButtons, 11, 5, result);  // R1
        Map(pressedButtons, 12, 3, result);  // Triangle
        Map(pressedButtons, 13, 1, result);  // Circle
        Map(pressedButtons, 14, 0, result);  // Cross
        Map(pressedButtons, 15, 2, result);  // Square
        return result;
    }

  private:
    static constexpr void Map(std::uint16_t pressedButtons, unsigned ps1Bit, unsigned coreBit, std::uint64_t& result) {
        if ((pressedButtons & (static_cast<std::uint16_t>(1u) << ps1Bit)) != 0) {
            result |= static_cast<std::uint64_t>(1u) << coreBit;
        }
    }
};

}  // namespace helengine::ps1
