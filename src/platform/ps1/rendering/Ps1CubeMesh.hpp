#pragma once

#include <cstdint>

namespace helengine::ps1 {

/** Immutable indexed cube authored in the PS1 renderer's +Y-down convention. */
class Ps1CubeMesh final {
  public:
    struct Position {
        std::int16_t X;
        std::int16_t Y;
        std::int16_t Z;
    };

    struct Face {
        std::uint8_t A;
        std::uint8_t B;
        std::uint8_t C;
        std::uint32_t Rgb;
    };

    static const Position Positions[8];
    static const Face Faces[12];
};

}  // namespace helengine::ps1
