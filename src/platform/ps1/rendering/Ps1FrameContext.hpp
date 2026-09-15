#pragma once

#include "platform/ps1/rendering/Ps1PacketBudget.hpp"
#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/ordering-table.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/primitives/triangles.hh"

#include <cstdint>

namespace helengine::ps1 {

/**
 * One persistent command context for one display parity.  The clear fragment,
 * triangle fragments, and ordering table remain at stable addresses until the
 * SDK completion fence has released the context.
 */
class Ps1FrameContext final {
  public:
    using TriangleFragment = psyqo::Fragments::SimpleFragment<psyqo::Prim::Triangle>;
    using ClearFragment = psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill>;

    static constexpr std::uint32_t PacketBudgetBytes = 4096;
    static constexpr unsigned TriangleCapacity =
        (PacketBudgetBytes - sizeof(ClearFragment)) / sizeof(TriangleFragment);

#ifndef PS1_PC_PORT
    static_assert(sizeof(TriangleFragment) == 20);
    static_assert(sizeof(psyqo::OrderingTable<256>) == 1028);
    static_assert(TriangleCapacity >= 12);
#endif

    bool Begin(std::uint32_t sceneSequence);
    TriangleFragment* TryReserveTriangle();
    bool Insert(TriangleFragment& fragment, std::uint16_t bucket);
    bool Submit(psyqo::GPU& gpu);
    void Cancel();

    std::uint32_t UsedPackets() const { return 1u + m_insertedCount; }
    std::uint32_t HighWaterBytes() const {
        return static_cast<std::uint32_t>(sizeof(ClearFragment) + m_reservedCount * sizeof(TriangleFragment));
    }
    std::uint32_t LastSubmissionSequence() const { return m_lastSubmissionSequence; }
    bool IsBuilding() const { return m_state == State::Building; }

  private:
    enum class State : std::uint8_t { Available, Building, Queued };

    void AssertState(bool condition, const char* message) const;

    State m_state = State::Available;
    std::uint32_t m_buildSequence = 0;
    std::uint32_t m_lastSubmissionSequence = 0;
    std::uint16_t m_reservedCount = 0;
    std::uint16_t m_insertedCount = 0;
    Ps1PacketBudget m_budget{TriangleCapacity};
    ClearFragment m_clear;
    psyqo::OrderingTable<256> m_table;
    TriangleFragment m_triangles[TriangleCapacity];
    std::uint8_t m_inserted[TriangleCapacity]{};
};

}  // namespace helengine::ps1
