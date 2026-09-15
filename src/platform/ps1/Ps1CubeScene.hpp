#pragma once

#include "psyqo/fragments.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/scene.hh"

namespace helengine::ps1 {

/**
 * Root scene for Task 1.  Each persistent clear fragment belongs to one
 * display parity and remains alive until the patched GPU flip releases it.
 */
class Ps1CubeScene final : public psyqo::Scene {
  private:
    void start(StartReason reason) override;
    void frame() override;

    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    unsigned m_firstFrameAge = 0;
    bool m_firstFrameQueued = false;
    bool m_firstFrameReported = false;
};

}  // namespace helengine::ps1
