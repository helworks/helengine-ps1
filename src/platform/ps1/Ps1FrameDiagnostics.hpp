#pragma once
#include <cstdint>

namespace helengine::ps1 {
/** Last native boundary reached; fixed RAM storage permits sampling without BIOS output. */
enum class Ps1FrameStage : std::uint32_t {
    Initializing = 1, StartupBefore = 2, StartupAfter = 3,
    UpdateBefore = 10, UpdateAfter = 11, BoundaryBefore = 12, BoundaryAfter = 13,
    DrawBefore = 14, DrawAfter = 15, FrameCompleted = 16,
    UploadBefore = 30, UploadAfter = 31,
    ReleaseChainBefore = 40, ReleaseChainAfter = 41,
    ReleaseReadyBefore = 42, ReleaseReadyAfter = 43, ReleaseAfter = 44,
    UploadChainBefore = 50, UploadChainAfter = 51, UploadReadyBefore = 52, UploadReadyAfter = 53,
    TextureOpenBefore = 60, TextureOpenAfter = 61,
    TextureDeserializeBefore = 62, TextureDeserializeAfter = 63,
    TextureUploadEnter = 64, TextureVramBefore = 65, TextureVramAfter = 66,
    TextureBufferBefore = 67, TextureBufferAfter = 68, TexturePackAfter = 69,
    AssetLogBefore = 70, AssetLogAfter = 71, AssetStreamBefore = 72, AssetStreamAfter = 73,
    FatalBeforeLog = 90
};
/** Bounded diagnostic record; sequence changes around writes allow detecting torn samples. */
struct Ps1FrameDiagnostics {
    std::uint32_t Magic;
    std::uint32_t Sequence;
    std::uint32_t Stage;
    std::uint32_t CompletedFrames;
    std::uint32_t VBlank;
    std::uint32_t TransitionActive;
    std::uint32_t CameraCount;
    std::uint32_t Detail;
};
}
extern "C" volatile helengine::ps1::Ps1FrameDiagnostics helengine_ps1_frame_diagnostics;
namespace helengine::ps1 {
/** Records a wait boundary without allocations, logging, or changing the sampled frame state. */
inline void MarkFrameStage(Ps1FrameStage stage, std::uint32_t detail = 0) {
    helengine_ps1_frame_diagnostics.Sequence = helengine_ps1_frame_diagnostics.Sequence + 1;
    helengine_ps1_frame_diagnostics.Stage = static_cast<std::uint32_t>(stage);
    helengine_ps1_frame_diagnostics.Detail = detail;
    helengine_ps1_frame_diagnostics.Sequence = helengine_ps1_frame_diagnostics.Sequence + 1;
}
}
