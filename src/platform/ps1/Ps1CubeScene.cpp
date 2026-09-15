#include "platform/ps1/Ps1CubeScene.hpp"

#include "common/syscalls/syscalls.h"
#include "psyqo/kernel.hh"

namespace helengine::ps1 {

void Ps1CubeScene::start(StartReason reason) {
    psyqo::Kernel::assert(reason == StartReason::Create, "PS1 root scene started unexpectedly");
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

    const unsigned parity = gpu().getParity();
    auto& clear = m_clear[parity];
    gpu().getNextClear(clear.primitive, {{.r = 16, .g = 20, .b = 32}});
    gpu().chain(clear);
    m_firstFrameQueued = true;
}

}  // namespace helengine::ps1
