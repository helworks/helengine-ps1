#pragma once

#include <cstdint>

#include "IInputBackend.hpp"
#include "psyqo/simplepad.hh"

template <typename T>
class Array;

class InputGamepadState;

namespace helengine::ps1 {

/// <summary>Captures both PS1 controller ports into Core's frame-based gamepad input.</summary>
class Ps1InputBackend final : public ::IInputBackend {
public:
    explicit Ps1InputBackend(psyqo::SimplePad& pad);

    /// <summary>Returns the current pad states using alternating preallocated snapshots.</summary>
    ::InputFrameState CaptureFrame() override;

private:
    std::uint16_t ReadPressedButtons(psyqo::SimplePad::Pad pad) const;
    ::Array<::InputGamepadState>* m_gamepadFrames[2]{};
    psyqo::SimplePad& m_pad;
    std::uint8_t m_nextFrame = 0;
};

}  // namespace helengine::ps1
