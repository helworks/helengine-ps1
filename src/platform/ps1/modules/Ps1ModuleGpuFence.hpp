#pragma once
#include <cstdint>
#include "psyqo/gpu.hh"
#include "psyqo/hardware/gpu.hh"

namespace helengine::ps1 {
/** Polls the previous DMA and command stream without waiting inside a scene-loading step. */
inline bool IsModuleGpuIdle(psyqo::GPU& gpu) {
    return !gpu.isChainTransferring() &&
        (psyqo::Hardware::GPU::Ctrl & std::uint32_t(0x04000000)) != 0;
}
}
