#include "platform/ps1/Ps1CubeScene.hpp"

#include "common/syscalls/syscalls.h"
#include "psyqo/kernel.hh"

#include <cstdint>

namespace helengine::ps1 {

void Ps1CubeScene::start(StartReason reason) {
    psyqo::Kernel::assert(reason == StartReason::Create, "PS1 root scene started unexpectedly");
    m_renderer.Initialize();
    m_rotationClock.Reset(gpu().now());
}

void Ps1CubeScene::frame() {
    // Application calls frame before flip.  Therefore this callback observes
    // the previous frame only after its chain, marker, and VBlank completed.
    if (m_firstFrameQueued && !m_firstFrameReported) {
        if (m_firstFrameAge == 0) {
            ++m_firstFrameAge;
        } else {
            ramsyscall_printf("PS1_FIRST_FRAME_PRESENTED parity=%u frame=%u\n", gpu().getParity(), gpu().getFrameCount());
            m_firstFrameReported = true;
        }
    }

    bool queued = false;
#if defined(PS1_DEMO_MODE_STATIC)
    queued = m_renderer.QueueCube(gpu(), 192, 256);
#elif defined(PS1_DEMO_MODE_ROTATE)
    m_rotationClock.Advance(gpu().now());
    queued = m_renderer.QueueCube(gpu(), m_rotationClock.PhaseX(), m_rotationClock.PhaseY());
#elif defined(PS1_DEMO_MODE_DEPTHORDER)
    queued = m_renderer.QueueDepthDiagnostic(gpu(), m_depthReverseInsertion);
    m_depthReverseInsertion = !m_depthReverseInsertion;
#elif defined(PS1_DEMO_MODE_INVALIDPOSE)
    queued = m_renderer.QueueInvalidPose(gpu());
#elif defined(PS1_DEMO_MODE_CAPACITY)
    queued = m_renderer.QueueCapacityDiagnostic(gpu());
#elif defined(PS1_DEMO_MODE_COMPLETIONSTRESS)
    queued = m_renderer.QueueCompletionStress(gpu());
#else
    queued = m_renderer.QueueClear(gpu());
#endif
    const auto& stats = m_renderer.Stats();
    const std::uint32_t frame = gpu().getFrameCount();
    if (!queued || !m_diagnosticReported || (frame != 0 && (frame % 120u) == 0)) {
        ramsyscall_printf(
            "PS1_RENDER_DIAGNOSTIC phase=%u,%u seq=%u submit=%u backface=%u degenerate=%u invalid=%u capacity=%u "
            "ownership=%u guards=%u guardfail=%u capacitylimit=%u flags=%08x packets=%u highwater=%u peak_packets=%u peak_highwater=%u last=%u reverse=%u failure=%u\n",
            static_cast<unsigned>(stats.phaseX), static_cast<unsigned>(stats.phaseY),
            static_cast<unsigned>(stats.sceneSequence), static_cast<unsigned>(stats.submittedTriangles),
            static_cast<unsigned>(stats.backfaceRejected), static_cast<unsigned>(stats.degenerate),
            static_cast<unsigned>(stats.invalid), static_cast<unsigned>(stats.capacityFailures),
            static_cast<unsigned>(stats.ownershipFailures), stats.guardsIntact ? 1u : 0u,
            static_cast<unsigned>(stats.guardFailures), static_cast<unsigned>(stats.capacityLimit),
            static_cast<unsigned>(stats.projectionFlags), static_cast<unsigned>(stats.packetCount),
            static_cast<unsigned>(stats.highWaterBytes), static_cast<unsigned>(stats.peakPacketCount),
            static_cast<unsigned>(stats.peakHighWaterBytes), static_cast<unsigned>(stats.lastSubmissionSequence),
            stats.reverseInsertion ? 1u : 0u, static_cast<unsigned>(stats.failure));
        m_diagnosticReported = true;
    }

#if defined(PS1_DEMO_MODE_COMPLETIONSTRESS)
    if (frame != 0 && (frame % 120u) == 0) {
        ramsyscall_printf("PS1_GPU_COMPLETION transfer=%u marker=%u presented=%u frame=%u parity=%u\n",
                          static_cast<unsigned>(gpu().getTransferSequence()),
                          static_cast<unsigned>(gpu().getMarkerSequence()),
                          static_cast<unsigned>(gpu().getPresentedSequence()), static_cast<unsigned>(frame),
                          static_cast<unsigned>(gpu().getParity()));
    }
#endif

    if (!queued) {
        switch (stats.failure) {
            case Ps1RenderFailure::Projection: psyqo::Kernel::assert(false, "PS1_RENDER_PROJECTION_FAILED"); break;
            case Ps1RenderFailure::Depth: psyqo::Kernel::assert(false, "PS1_RENDER_DEPTH_FAILED"); break;
            case Ps1RenderFailure::Capacity: psyqo::Kernel::assert(false, "PS1_RENDER_CAPACITY_FAILED"); break;
            case Ps1RenderFailure::Ownership: psyqo::Kernel::assert(false, "PS1_RENDER_OWNERSHIP_FAILED"); break;
            case Ps1RenderFailure::Guard: psyqo::Kernel::assert(false, "PS1_RENDER_GUARD_FAILED"); break;
            case Ps1RenderFailure::Submission: psyqo::Kernel::assert(false, "PS1_RENDER_SUBMISSION_FAILED"); break;
            default: psyqo::Kernel::assert(false, "PS1_RENDER_QUEUE_FAILED"); break;
        }
    }
    m_firstFrameQueued = queued;
}

}  // namespace helengine::ps1
