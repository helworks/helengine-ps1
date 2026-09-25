#pragma once

#include <cstdint>

#include "psyqo/simplepad.hh"
#include "psyqo/scene.hh"
#include "platform/ps1/rendering/Ps1RasterRenderer.hpp"

class Core;
class CoreInitializationOptions;
class PlatformInfo;
class RuntimeSceneCatalog;

namespace helengine::ps1 {

class Ps1ContentStreamSource;
#if defined(PS1_CPU_PROFILE)
class Ps1CpuProfileSink;
#endif
class Ps1InputBackend;
class Ps1RenderManager2D;
class Ps1RenderManager3D;

/**
 * Root PSYQo scene that owns generated Core for the authored cube_test milestone. On start it
 * binds the runtime clock, serves the embedded archive to Core, loads the startup scene from the
 * generated catalog, and verifies one camera and one drawable. Each frame it advances Core with
 * the measured GPU time, draws through the render manager, and keeps the presentation loop alive.
 */
class Ps1AuthoredScene final : public psyqo::Scene {
  private:
    void start(StartReason reason) override;
    void frame() override;

    void InitializeCore();
    void ReportSceneState(const char* marker);

    Ps1RasterRenderer m_renderer;
    psyqo::SimplePad m_pad;
    Ps1ContentStreamSource* m_contentSource = nullptr;
    ::RuntimeSceneCatalog* m_sceneCatalog = nullptr;
    ::CoreInitializationOptions* m_options = nullptr;
    Ps1RenderManager3D* m_render3D = nullptr;
    Ps1RenderManager2D* m_render2D = nullptr;
    Ps1InputBackend* m_input = nullptr;
    ::PlatformInfo* m_platform = nullptr;
    ::Core* m_core = nullptr;
    std::uint32_t m_lastTimestamp = 0;
    std::uint32_t m_frameSequence = 0;
    std::uint32_t m_lastVblankFrame = 0;
    std::uint32_t m_steadyStartVblank = 0;
    std::uint32_t m_missedVblanks = 0;
    std::uint32_t m_worstVblankDelta = 0;
    std::uint64_t m_lastFrameEndUs = 0;
    std::uint64_t m_updateTotalUs = 0;
    std::uint64_t m_boundaryTotalUs = 0;
    std::uint64_t m_drawTotalUs = 0;
    std::uint64_t m_betweenFramesTotalUs = 0;
    std::uint32_t m_profileFrames = 0;
    std::uint64_t m_overlayWindowStartUs = 0;
    std::uint64_t m_overlayUpdateTotalUs = 0;
    std::uint32_t m_overlayFrames = 0;
#if defined(PS1_CPU_PROFILE)
    Ps1CpuProfileSink* m_cpuProfileSink = nullptr;
#endif
    bool m_initialized = false;
};

}  // namespace helengine::ps1
