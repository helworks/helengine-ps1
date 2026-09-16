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
    /** Per-vertex lit triangles used only by the authored scene path. */
    using GouraudFragment = psyqo::Fragments::SimpleFragment<psyqo::Prim::GouraudTriangle>;

    /** Authored scenes submit far fewer triangles than the diagnostic stress modes. */
    static constexpr unsigned GouraudCapacity = 64;

    static constexpr std::uint32_t PacketBudgetBytes = 4096;
    static constexpr unsigned TriangleCapacity =
        (PacketBudgetBytes - sizeof(ClearFragment)) / sizeof(TriangleFragment);

#ifndef PS1_PC_PORT
    static_assert(sizeof(TriangleFragment) == 20);
    static_assert(sizeof(psyqo::OrderingTable<256>) == 1028);
    static_assert(TriangleCapacity >= 12);
#endif

    /** Clear color used by the diagnostic scenes; authored scenes pass the camera's clear color. */
    static constexpr psyqo::Color DefaultClearColor{{.r = 16, .g = 20, .b = 32}};

    bool Begin(std::uint32_t sceneSequence, std::uint32_t triangleCapacity = TriangleCapacity);
    TriangleFragment* TryReserveTriangle();
    bool Insert(TriangleFragment& fragment, std::uint16_t bucket);
    GouraudFragment* TryReserveGouraud();
    bool Insert(GouraudFragment& fragment, std::uint16_t bucket);
    bool Submit(psyqo::GPU& gpu, psyqo::Color clearColor = DefaultClearColor);
    void Cancel();

    std::uint32_t UsedPackets() const { return 1u + m_insertedCount + m_gouraudInsertedCount; }
    std::uint32_t HighWaterBytes() const {
        return static_cast<std::uint32_t>(sizeof(ClearFragment) + m_reservedCount * sizeof(TriangleFragment) +
                                          m_gouraudReservedCount * sizeof(GouraudFragment));
    }
    std::uint32_t LastSubmissionSequence() const { return m_lastSubmissionSequence; }
    std::uint32_t Capacity() const { return m_budgetCapacity; }
    bool GuardsIntact() const {
        return m_packetGuardBefore == PacketGuardValue && m_packetGuardAfter == PacketGuardValue &&
               m_gouraudGuardAfter == PacketGuardValue;
    }
    bool IsBuilding() const { return m_state == State::Building; }

  private:
    enum class State : std::uint8_t { Available, Building, Queued };

    void AssertState(bool condition, const char* message) const;

    static constexpr std::uint32_t PacketGuardValue = 0x50534744;

    State m_state = State::Available;
    std::uint32_t m_buildSequence = 0;
    std::uint32_t m_lastSubmissionSequence = 0;
    std::uint16_t m_reservedCount = 0;
    std::uint16_t m_insertedCount = 0;
    std::uint32_t m_budgetCapacity = TriangleCapacity;
    Ps1PacketBudget m_budget{TriangleCapacity};
    ClearFragment m_clear;
    psyqo::OrderingTable<256> m_table;
    std::uint32_t m_packetGuardBefore = PacketGuardValue;
    TriangleFragment m_triangles[TriangleCapacity];
    std::uint32_t m_packetGuardAfter = PacketGuardValue;
    GouraudFragment m_gouraud[GouraudCapacity];
    std::uint32_t m_gouraudGuardAfter = PacketGuardValue;
    std::uint16_t m_gouraudReservedCount = 0;
    std::uint16_t m_gouraudInsertedCount = 0;
    std::uint8_t m_inserted[TriangleCapacity]{};
    std::uint8_t m_gouraudInserted[GouraudCapacity]{};
};

}  // namespace helengine::ps1
