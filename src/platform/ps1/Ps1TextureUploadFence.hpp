#pragma once
#include "platform/ps1/Ps1FrameDiagnostics.hpp"
namespace helengine::ps1 {
/** Retires the previous display chain before PSYQo installs its single synchronous upload callback. */
template<class GpuType>
void WaitForTextureUpload(GpuType& gpu) {
    MarkFrameStage(Ps1FrameStage::UploadChainBefore);
    gpu.waitChainIdle();
    MarkFrameStage(Ps1FrameStage::UploadChainAfter);
    MarkFrameStage(Ps1FrameStage::UploadReadyBefore);
    gpu.waitReady();
    MarkFrameStage(Ps1FrameStage::UploadReadyAfter);
}
}
