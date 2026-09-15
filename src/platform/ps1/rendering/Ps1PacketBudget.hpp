#pragma once

#include <cstdint>

namespace helengine::ps1 {

/**
 * Fixed-capacity reservation counter used by packet builders.  It owns no
 * storage; the caller owns the corresponding typed packet array.
 */
class Ps1PacketBudget final {
  public:
    explicit constexpr Ps1PacketBudget(std::uint32_t capacity) : m_capacity(capacity) {}

    constexpr void Reset() { m_used = 0; }

    constexpr bool TryReserve(std::uint32_t& slot) {
        if (m_used >= m_capacity) return false;
        slot = m_used++;
        return true;
    }

    constexpr std::uint32_t Used() const { return m_used; }

  private:
    std::uint32_t m_capacity = 0;
    std::uint32_t m_used = 0;
};

}  // namespace helengine::ps1
