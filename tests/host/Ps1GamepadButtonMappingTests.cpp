#include "platform/ps1/Ps1GamepadButtonMapping.hpp"

#include <cassert>
#include <cstdint>

using helengine::ps1::Ps1GamepadButtonMapping;

int main() {
    constexpr std::uint16_t everyPlayStationButton = 0xffffu;
    constexpr std::uint64_t everyCoreButton = (1ull << 16) - 1ull;
    assert(Ps1GamepadButtonMapping::ToCoreButtonMask(everyPlayStationButton) == everyCoreButton);
    assert(Ps1GamepadButtonMapping::ToCoreButtonMask(0u) == 0u);

    constexpr std::uint8_t coreBitForPlayStationButton[] = {
        15, 6, 7, 14, 10, 13, 11, 12, 8, 9, 4, 5, 3, 1, 0, 2
    };
    for (std::uint8_t index = 0; index < 16; ++index) {
        const std::uint16_t playStationButton = static_cast<std::uint16_t>(1u << index);
        const std::uint64_t coreButton = 1ull << coreBitForPlayStationButton[index];
        assert(Ps1GamepadButtonMapping::ToCoreButtonMask(playStationButton) == coreButton);
    }
}
