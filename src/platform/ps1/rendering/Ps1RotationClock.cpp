#include "platform/ps1/rendering/Ps1RotationClock.hpp"

#include <algorithm>

namespace helengine::ps1 {

void Ps1RotationClock::Reset(std::uint32_t nowUs) {
    m_lastUs = nowUs;
    m_remainderX = 0;
    m_remainderY = 0;
    m_phaseX = 0;
    m_phaseY = 0;
}

void Ps1RotationClock::Advance(std::uint32_t nowUs) {
    const std::uint32_t elapsed = nowUs - m_lastUs;
    m_lastUs = nowUs;
    const std::uint32_t delta = std::min(elapsed, MaxDeltaUs);

    const std::uint32_t totalX = m_remainderX + delta * FullTurn;
    const std::uint32_t totalY = m_remainderY + delta * FullTurn;
    m_phaseX = static_cast<std::uint16_t>((m_phaseX + totalX / PeriodXUs) % FullTurn);
    m_phaseY = static_cast<std::uint16_t>((m_phaseY + totalY / PeriodYUs) % FullTurn);
    m_remainderX = totalX % PeriodXUs;
    m_remainderY = totalY % PeriodYUs;
}

}  // namespace helengine::ps1
