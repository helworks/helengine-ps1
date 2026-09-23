#pragma once

#include <cstdint>

namespace helengine::ps1 {

struct Ps1FramePacketLimits final {
    static constexpr std::uint32_t PacketBudgetBytes = 16384;
    static constexpr std::uint32_t GouraudTriangles = 64;
    static constexpr std::uint32_t UiTriangles = 96;
    static constexpr std::uint32_t TexturedTriangles = 256;
    static constexpr std::uint32_t TexturedQuads = TexturedTriangles / 2u;
    static constexpr std::uint32_t ClearFragmentBytes = 16;
    static constexpr std::uint32_t TriangleFragmentBytes = 20;
    static constexpr std::uint32_t GouraudFragmentBytes = 28;
    static constexpr std::uint32_t UiFragmentBytes = 20;
    static constexpr std::uint32_t TexturedFragmentBytes = 40;
    static constexpr std::uint32_t FixedBytes = ClearFragmentBytes + GouraudTriangles * GouraudFragmentBytes +
                                                UiTriangles * UiFragmentBytes +
                                                TexturedQuads * TexturedFragmentBytes;
    static constexpr std::uint32_t SceneTriangles = (PacketBudgetBytes - FixedBytes) / TriangleFragmentBytes;
    static_assert(FixedBytes < PacketBudgetBytes);
};

/**
 * Fixed-capacity reservation counter used by packet builders.  It owns no
 * storage; the caller owns the corresponding typed packet array.
 */
class Ps1PacketBudget final {
  public:
    explicit constexpr Ps1PacketBudget(std::uint32_t capacity) : m_capacity(capacity) {}

    constexpr void Reset() { m_used = 0; }

    constexpr void Reset(std::uint32_t capacity) {
        m_capacity = capacity;
        m_used = 0;
    }

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
