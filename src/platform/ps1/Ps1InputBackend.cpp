#include "platform/ps1/Ps1InputBackend.hpp"

#include "InputFrameState.hpp"
#include "InputGamepadState.hpp"
#include "runtime/native_list.hpp"
#include "platform/ps1/Ps1GamepadButtonMapping.hpp"

namespace helengine::ps1 {

Ps1InputBackend::Ps1InputBackend(psyqo::SimplePad& pad) : m_pad(pad) {
    m_gamepadFrames[0] = new ::Array<::InputGamepadState>(2);
    m_gamepadFrames[1] = new ::Array<::InputGamepadState>(2);
}

::InputFrameState Ps1InputBackend::CaptureFrame() {
    ::Array<::InputGamepadState>* states = m_gamepadFrames[m_nextFrame];
    for (int32_t padIndex = 0; padIndex < 2; ++padIndex) {
        const auto pad = static_cast<psyqo::SimplePad::Pad>(padIndex);
        ::InputGamepadState state;
        state.set_Connected(m_pad.isPadConnected(pad));
        state.set_Buttons(Ps1GamepadButtonMapping::ToCoreButtonMask(ReadPressedButtons(pad)));
        (*states)[padIndex] = state;
    }

    ::InputFrameState frame;
    frame.set_Gamepads(states);
    frame.set_GamepadCount(2);
    m_nextFrame ^= 1u;
    return frame;
}

std::uint16_t Ps1InputBackend::ReadPressedButtons(psyqo::SimplePad::Pad pad) const {
    std::uint16_t buttons = 0;
    for (std::uint8_t index = 0; index < 16; ++index) {
        if (m_pad.isButtonPressed(pad, static_cast<psyqo::SimplePad::Button>(index))) {
            buttons |= static_cast<std::uint16_t>(1u << index);
        }
    }
    return buttons;
}

}  // namespace helengine::ps1
