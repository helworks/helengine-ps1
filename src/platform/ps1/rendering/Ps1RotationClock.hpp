#pragma once

#include <cstdint>

namespace helengine::ps1 {

/**
 * Wrap-safe elapsed-time accumulator for the cube's two raw PS1 angle phases.
 * The clock owns no hardware timer; callers provide the SDK timestamp in
 * microseconds so debugger pauses can be bounded deterministically.
 */
class Ps1RotationClock final {
  public:
    void Reset(std::uint32_t nowUs);
    void Advance(std::uint32_t nowUs);
    std::uint16_t PhaseX() const { return m_phaseX; }
    std::uint16_t PhaseY() const { return m_phaseY; }

  private:
    static constexpr std::uint32_t FullTurn = 2048;
    static constexpr std::uint32_t MaxDeltaUs = 100000;
    static constexpr std::uint32_t PeriodXUs = 12000000;
    static constexpr std::uint32_t PeriodYUs = 8000000;

    std::uint32_t m_lastUs = 0;
    std::uint32_t m_remainderX = 0;
    std::uint32_t m_remainderY = 0;
    std::uint16_t m_phaseX = 0;
    std::uint16_t m_phaseY = 0;
};

}  // namespace helengine::ps1
