#pragma once

#include <cstdint>

namespace helengine::ps1 {

struct Ps1FramePacketLimits final {
    static constexpr std::uint32_t PacketBudgetBytes = 32768;
    static constexpr std::uint32_t GouraudTriangles = 64;
    // The authored menu uses about 291 textured quads (287 glyphs and four background tiles).
    // Keep 29 slots of headroom while preserving the scene-triangle budget inside the 32 KiB arena.
    static constexpr std::uint32_t UiTriangles = 256;
    static constexpr std::uint32_t TexturedTriangles = 640;
    static constexpr std::uint32_t TexturedQuads = TexturedTriangles / 2u;
    static constexpr std::uint32_t ClearFragmentBytes = 16;
    static constexpr std::uint32_t TriangleFragmentBytes = 20;
    static constexpr std::uint32_t GouraudFragmentBytes = 28;
    static constexpr std::uint32_t UiFragmentBytes = 20;
    static constexpr std::uint32_t TexturedFragmentBytes = 40;
    static constexpr std::uint32_t FixedBytes = ClearFragmentBytes + GouraudTriangles * GouraudFragmentBytes +
                                                UiTriangles * UiFragmentBytes +
                                                TexturedQuads * TexturedFragmentBytes;
    // Preserve the established 3D capacity instead of growing it with the UI budget.
    static constexpr std::uint32_t SceneTriangles = 376;
    static_assert(FixedBytes + SceneTriangles * TriangleFragmentBytes <= PacketBudgetBytes);
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
