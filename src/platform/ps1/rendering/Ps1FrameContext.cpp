#include "platform/ps1/rendering/Ps1FrameContext.hpp"
#include "platform/ps1/rendering/Ps1DepthBucket.hpp"

#include "psyqo/kernel.hh"

#include <cstdint>

namespace helengine::ps1 {

void Ps1FrameContext::AssertState(bool condition, const char* message) const {
    psyqo::Kernel::assert(condition, message);
}

bool Ps1FrameContext::Begin(std::uint32_t sceneSequence, std::uint32_t triangleCapacity) {
    if (!GuardsIntact()) {
        AssertState(false, "PS1_FRAME_GUARD_CORRUPTED");
        return false;
    }
    if (m_state == State::Building) {
        AssertState(false, "PS1_FRAME_BEGIN_WHILE_BUILDING");
        return false;
    }
    if (m_state == State::Queued) {
        const std::uint32_t distance = sceneSequence - m_lastSubmissionSequence;
        if (distance < 2u) return false;
    }

    m_table.clear();
    m_budgetCapacity = triangleCapacity < TriangleCapacity ? triangleCapacity : TriangleCapacity;
    m_budget.Reset(m_budgetCapacity);
    m_reservedCount = 0;
    m_insertedCount = 0;
    for (unsigned i = 0; i < TriangleCapacity; ++i) m_inserted[i] = 0;
    m_gouraudReservedCount = 0;
    m_gouraudInsertedCount = 0;
    for (unsigned i = 0; i < GouraudCapacity; ++i) m_gouraudInserted[i] = 0;
    m_uiReservedCount = 0;
    m_uiInsertedCount = 0;
    for (unsigned i = 0; i < UiTriangleCapacity; ++i) m_uiInserted[i] = 0;
    m_texturedReservedCount = 0;
    m_texturedInsertedCount = 0;
    for (unsigned i = 0; i < TexturedQuadCapacity; ++i) m_texturedInserted[i] = 0;
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

    m_table.insert(fragment, Ps1DepthBucket::ToCombinedSceneBucket(bucket));
    m_inserted[index] = 1;
    ++m_insertedCount;
    return true;
}

Ps1FrameContext::GouraudFragment* Ps1FrameContext::TryReserveGouraud() {
    if (m_state != State::Building) {
        AssertState(false, "PS1_GOURAUD_RESERVE_WRONG_STATE");
        return nullptr;
    }
    if (m_gouraudReservedCount >= GouraudCapacity) return nullptr;
    return &m_gouraud[m_gouraudReservedCount++];
}

bool Ps1FrameContext::Insert(GouraudFragment& fragment, std::uint16_t bucket) {
    if (m_state != State::Building) {
        AssertState(false, "PS1_GOURAUD_INSERT_WRONG_STATE");
        return false;
    }
    if (bucket >= 256u) return false;

    const auto first = reinterpret_cast<std::uintptr_t>(&m_gouraud[0]);
    const auto last = reinterpret_cast<std::uintptr_t>(&m_gouraud[GouraudCapacity]);
    const auto address = reinterpret_cast<std::uintptr_t>(&fragment);
    if (address < first || address >= last || ((address - first) % sizeof(GouraudFragment)) != 0) return false;
    const unsigned index = static_cast<unsigned>((address - first) / sizeof(GouraudFragment));
    if (index >= m_gouraudReservedCount || m_gouraudInserted[index] != 0) return false;

    m_table.insert(fragment, Ps1DepthBucket::ToCombinedSceneBucket(bucket));
    m_gouraudInserted[index] = 1;
    ++m_gouraudInsertedCount;
    return true;
}

Ps1FrameContext::UiTriangleFragment* Ps1FrameContext::TryReserveUiTriangle() {
    if (m_state != State::Building) {
        AssertState(false, "PS1_UI_TRIANGLE_RESERVE_WRONG_STATE");
        return nullptr;
    }
    if (m_uiReservedCount >= UiTriangleCapacity) return nullptr;
    return &m_uiTriangles[m_uiReservedCount++];
}

bool Ps1FrameContext::InsertUiTriangle(UiTriangleFragment& fragment, std::uint16_t bucket) {
    if (m_state != State::Building) {
        AssertState(false, "PS1_UI_TRIANGLE_INSERT_WRONG_STATE");
        return false;
    }
    if (bucket >= 256u) return false;
    const auto first = reinterpret_cast<std::uintptr_t>(&m_uiTriangles[0]);
    const auto last = reinterpret_cast<std::uintptr_t>(&m_uiTriangles[UiTriangleCapacity]);
    const auto address = reinterpret_cast<std::uintptr_t>(&fragment);
    if (address < first || address >= last || ((address - first) % sizeof(UiTriangleFragment)) != 0) return false;
    const unsigned index = static_cast<unsigned>((address - first) / sizeof(UiTriangleFragment));
    if (index >= m_uiReservedCount || m_uiInserted[index] != 0) return false;
    m_table.insert(fragment, bucket);
    m_uiInserted[index] = 1;
    ++m_uiInsertedCount;
    return true;
}

Ps1FrameContext::TexturedQuadFragment* Ps1FrameContext::TryReserveTexturedQuad() {
    if (m_state != State::Building) {
        AssertState(false, "PS1_TEXTURED_TRIANGLE_RESERVE_WRONG_STATE");
        return nullptr;
    }
    if (m_texturedReservedCount >= TexturedQuadCapacity) return nullptr;
    return &m_texturedQuads[m_texturedReservedCount++];
}

bool Ps1FrameContext::ReuseTexturedQuad(const TexturedQuadFragment& fragment, const void* source,
                                        std::uint32_t revision) const {
    if (source == nullptr || revision == 0 || m_state != State::Building || m_texturedReservedCount == 0) return false;
    const unsigned index = m_texturedReservedCount - 1u;
    return &m_texturedQuads[index] == &fragment && m_texturedCacheSources[index] == source &&
           m_texturedCacheRevisions[index] == revision;
}

void Ps1FrameContext::RememberTexturedQuad(const TexturedQuadFragment& fragment, const void* source,
                                           std::uint32_t revision) {
    if (m_state != State::Building || m_texturedReservedCount == 0) {
        AssertState(false, "PS1_TEXTURED_QUAD_CACHE_WRONG_STATE");
        return;
    }
    const unsigned index = m_texturedReservedCount - 1u;
    if (&m_texturedQuads[index] != &fragment) {
        AssertState(false, "PS1_TEXTURED_QUAD_CACHE_NOT_LAST_RESERVED");
        return;
    }
    m_texturedCacheSources[index] = source;
    m_texturedCacheRevisions[index] = source == nullptr ? 0u : revision;
}

bool Ps1FrameContext::Insert(TexturedQuadFragment& fragment, std::uint16_t bucket) {
    if (m_state != State::Building) {
        AssertState(false, "PS1_TEXTURED_TRIANGLE_INSERT_WRONG_STATE");
        return false;
    }
    if (bucket >= 256u) return false;
    unsigned index = 0;
    // The authored UI path inserts the slot it just reserved. Avoid per-packet address division
    // there, while retaining the full ownership check for other insertion orders.
    if (m_texturedReservedCount != 0u && &m_texturedQuads[m_texturedReservedCount - 1u] == &fragment) {
        index = m_texturedReservedCount - 1u;
    } else {
        const auto first = reinterpret_cast<std::uintptr_t>(&m_texturedQuads[0]);
        const auto last = reinterpret_cast<std::uintptr_t>(&m_texturedQuads[TexturedQuadCapacity]);
        const auto address = reinterpret_cast<std::uintptr_t>(&fragment);
        if (address < first || address >= last || ((address - first) % sizeof(TexturedQuadFragment)) != 0) return false;
        index = static_cast<unsigned>((address - first) / sizeof(TexturedQuadFragment));
    }
    if (index >= m_texturedReservedCount || m_texturedInserted[index] != 0) return false;
    m_table.insert(fragment, bucket);
    m_texturedInserted[index] = 1;
    ++m_texturedInsertedCount;
    return true;
}

bool Ps1FrameContext::Submit(psyqo::GPU& gpu, psyqo::Color clearColor) {
    if (m_state != State::Building) {
        AssertState(false, "PS1_FRAME_SUBMIT_WRONG_STATE");
        return false;
    }
    if (!GuardsIntact()) {
        AssertState(false, "PS1_FRAME_GUARD_CORRUPTED");
        return false;
    }
    gpu.getNextClear(m_clear.primitive, clearColor);
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
    m_budget.Reset(TriangleCapacity);
    m_budgetCapacity = TriangleCapacity;
    m_reservedCount = 0;
    m_insertedCount = 0;
    m_gouraudReservedCount = 0;
    m_gouraudInsertedCount = 0;
    m_uiReservedCount = 0;
    m_uiInsertedCount = 0;
    m_texturedReservedCount = 0;
    m_texturedInsertedCount = 0;
    m_state = State::Available;
}

}  // namespace helengine::ps1
