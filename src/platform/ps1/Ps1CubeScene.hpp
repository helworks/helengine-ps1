#pragma once

#include "psyqo/scene.hh"

#include "platform/ps1/rendering/Ps1RasterRenderer.hpp"
#include "platform/ps1/rendering/Ps1RotationClock.hpp"

namespace helengine::ps1 {

/**
 * Root scene for the native PS1 demo.  The renderer owns both persistent
 * parity contexts; this scene only selects the approved diagnostic mode and
 * reports bounded render evidence.
 */
class Ps1CubeScene final : public psyqo::Scene {
  private:
    void start(StartReason reason) override;
    void frame() override;

    Ps1RasterRenderer m_renderer;
    Ps1RotationClock m_rotationClock;
    unsigned m_firstFrameAge = 0;
    bool m_firstFrameQueued = false;
    bool m_firstFrameReported = false;
    bool m_diagnosticReported = false;
    bool m_depthReverseInsertion = false;
};

}  // namespace helengine::ps1
