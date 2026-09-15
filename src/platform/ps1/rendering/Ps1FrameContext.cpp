#include "platform/ps1/rendering/Ps1FrameContext.hpp"

#include "psyqo/kernel.hh"

#include <cstdint>

namespace helengine::ps1 {

void Ps1FrameContext::AssertState(bool condition, const char* message) const {
    psyqo::Kernel::assert(condition, message);
}

bool Ps1FrameContext::Begin(std::uint32_t sceneSequence) {
    if (m_state == State::Building) {
        AssertState(false, "PS1_FRAME_BEGIN_WHILE_BUILDING");
        return false;
    }
    if (m_state == State::Queued) {
        const std::uint32_t distance = sceneSequence - m_lastSubmissionSequence;
        if (distance < 2u) return false;
    }

    m_table.clear();
    m_budget.Reset();
    m_reservedCount = 0;
    m_insertedCount = 0;
    for (unsigned i = 0; i < TriangleCapacity; ++i) m_inserted[i] = 0;
    m_buildSequence = sceneSequence;
    m_state = State::Building;
    return true;
}

Ps1FrameContext::TriangleFragment* Ps1FrameContext::TryReserveTriangle() {
    if (m_state != State::Building) {
        AssertState(false, "PS1_TRIANGLE_RESERVE_WRONG_STATE");
        return nullptr;
    }
    std::uint32_t slot = 0;
    if (!m_budget.TryReserve(slot)) return nullptr;
    ++m_reservedCount;
    return &m_triangles[slot];
}

bool Ps1FrameContext::Insert(TriangleFragment& fragment, std::uint16_t bucket) {
    if (m_state != State::Building) {
        AssertState(false, "PS1_TRIANGLE_INSERT_WRONG_STATE");
        return false;
    }
    if (bucket >= 256u) return false;

    const auto first = reinterpret_cast<std::uintptr_t>(&m_triangles[0]);
    const auto last = reinterpret_cast<std::uintptr_t>(&m_triangles[TriangleCapacity]);
    const auto address = reinterpret_cast<std::uintptr_t>(&fragment);
    if (address < first || address >= last || ((address - first) % sizeof(TriangleFragment)) != 0) return false;
    const unsigned index = static_cast<unsigned>((address - first) / sizeof(TriangleFragment));
    if (index >= m_reservedCount || m_inserted[index] != 0) return false;

    m_table.insert(fragment, bucket);
    m_inserted[index] = 1;
    ++m_insertedCount;
    return true;
}

bool Ps1FrameContext::Submit(psyqo::GPU& gpu) {
    if (m_state != State::Building) {
        AssertState(false, "PS1_FRAME_SUBMIT_WRONG_STATE");
        return false;
    }
    gpu.getNextClear(m_clear.primitive, {{.r = 16, .g = 20, .b = 32}});
    gpu.chain(m_clear);
    gpu.chain(m_table);
    m_lastSubmissionSequence = m_buildSequence;
    m_state = State::Queued;
    return true;
}

void Ps1FrameContext::Cancel() {
    if (m_state != State::Building) {
        AssertState(false, "PS1_FRAME_CANCEL_WRONG_STATE");
        return;
    }
    m_table.clear();
    m_budget.Reset();
    m_reservedCount = 0;
    m_insertedCount = 0;
    m_state = State::Available;
}

}  // namespace helengine::ps1
