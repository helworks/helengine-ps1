#include "platform/ps1/Ps1AuthoredScene.hpp"

#include "Core.hpp"
#include "CoreInitializationOptions.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IDrawable3D.hpp"
#include "ObjectManager.hpp"
#include "PlatformInfo.hpp"
#include "RuntimeSceneCatalog.hpp"
#include "SceneLoadMode.hpp"
#include "SceneManager.hpp"
#include "common/syscalls/syscalls.h"
#include "float4.hpp"
#include "platform/ps1/Ps1ContentStreamSource.hpp"
#include "platform/ps1/Ps1InputBackend.hpp"
#include "platform/ps1/Ps1RenderManager2D.hpp"
#include "platform/ps1/Ps1RenderManager3D.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "platform/ps1/Ps1SceneBootstrap.hpp"
#if defined(PS1_CPU_PROFILE)
#include "platform/ps1/profiling/Ps1CpuProfileSink.hpp"
#endif
#include "platform/ps1/rendering/Ps1FrameOverlayText.hpp"
#include "psyqo/alloc.h"
#include "psyqo/kernel.hh"
#include "runtime/native_list.hpp"

extern "C" const std::uint8_t he_ps1_authored_archive[];
extern "C" const std::uint8_t he_ps1_authored_archive_end[];

namespace helengine::ps1 {
namespace {

/// <summary>Scales a unit-range float to thousandths so it can be printed without float formatting.</summary>
int Milli(float value) {
    return static_cast<int>(value * 1000.0f);
}

/// <summary>Longest update step accepted after a pause so one stalled frame cannot spin the scene.</summary>
constexpr double kMaxDeltaSeconds = 0.1;

}  // namespace

void Ps1AuthoredScene::start(StartReason) {
    if (m_initialized) {
        return;
    }
    m_initialized = true;
    m_renderer.Initialize();
    InitializeCore();
    m_lastTimestamp = gpu().now();
    m_overlayWindowStartUs = he_cpp_custom::MonotonicMicroseconds();
}

void Ps1AuthoredScene::InitializeCore() {
    InitializeRuntimeClock(gpu());
    ramsyscall_printf("PS1_CORE_INITIALIZING\n");
    const std::size_t archiveLength = static_cast<std::size_t>(he_ps1_authored_archive_end - he_ps1_authored_archive);
    m_contentSource = new Ps1ContentStreamSource(he_ps1_authored_archive, archiveLength);
    m_sceneCatalog = BuildPs1RuntimeSceneCatalog();
    if (m_sceneCatalog == nullptr) {
        he_cpp_custom::Fail("Generated scene catalog is empty");
    }
    m_options = new ::CoreInitializationOptions();
    m_options->set_ContentStreamSource(m_contentSource);
    m_options->set_SceneCatalog(m_sceneCatalog);
    m_options->set_UpdateOrderLayers(4);
    m_options->set_RenderOrderLayers3D(4);
    m_options->set_UpdateListInitialCapacity(16);
    m_options->set_RenderList2DInitialCapacity(16);
    m_options->set_RenderList3DInitialCapacity(16);
    m_options->set_CommitPendingSceneOperationsDuringDraw(false);
    m_options->set_DefaultUpdateDeltaSeconds(1.0 / 60.0);
#if defined(PS1_CPU_PROFILE)
    m_cpuProfileSink = new Ps1CpuProfileSink();
    m_options->set_CpuProfileSink(m_cpuProfileSink);
#endif
    m_render2D = new Ps1RenderManager2D(gpu(), m_renderer);
    m_render3D = new Ps1RenderManager3D(gpu(), m_renderer, *m_render2D);
#if defined(PS1_CPU_PROFILE)
    m_render2D->SetCpuProfileSink(m_cpuProfileSink);
    m_render3D->SetCpuProfileSink(m_cpuProfileSink);
#endif
    // Core's screen-bound ViewportComponent reads MainWindowSize to fit authored reference
    // canvases. Register the native display before the scene's first layout update.
    m_render3D->AddWindow(0, 320, 240);
    m_input = new Ps1InputBackend();
    m_platform = new ::PlatformInfo("ps1", "authored-cube-test");
    m_core = new ::Core(m_options);
    ramsyscall_printf("PS1_CORE_CONSTRUCTED core=%p\n", m_core);
    m_core->Initialize(m_render3D, m_render2D, m_input, m_platform, m_options);
    if (m_core->get_ObjectManager() == nullptr || m_core->get_SceneManager() == nullptr) {
        he_cpp_custom::Fail("Core initialization did not create required managers");
    }
    ramsyscall_printf("PS1_CORE_READY core=%p\n", m_core);

    const char* startupSceneId = Ps1StartupSceneId();
    if (startupSceneId == nullptr) {
        he_cpp_custom::Fail("Generated catalog has no startup scene");
    }
    ramsyscall_printf("PS1_AUTHORED_SCENE_LOADING id=%s\n", startupSceneId);
    m_core->get_SceneManager()->LoadScene(startupSceneId, ::SceneLoadMode::Single);
    m_core->get_SceneManager()->CommitPendingOperationsAtFrameBoundary();
    ReportSceneState("PS1_AUTHORED_SCENE_READY");

    ::ObjectManager* objectManager = m_core->get_ObjectManager();
    const int32_t cameraCount = objectManager->get_Cameras() == nullptr ? 0 : objectManager->get_Cameras()->get_Count();
    const int32_t drawableCount = objectManager->get_Drawables3D() == nullptr ? 0 : objectManager->get_Drawables3D()->get_Count();
    // A 2D authored scene may have no 3D drawables, but it still needs a camera
    // to drive its viewport and UI render queue.
    if (cameraCount == 0) {
        he_cpp_custom::Fail("Authored scene registered no camera");
    }
}

void Ps1AuthoredScene::ReportSceneState(const char* marker) {
    ::ObjectManager* objectManager = m_core->get_ObjectManager();
    const int32_t cameraCount = objectManager->get_Cameras() == nullptr ? 0 : objectManager->get_Cameras()->get_Count();
    const int32_t drawableCount = objectManager->get_Drawables3D() == nullptr ? 0 : objectManager->get_Drawables3D()->get_Count();
    const int32_t updateableCount = objectManager->get_Updateables() == nullptr ? 0 : objectManager->get_Updateables()->get_Count();
    int yawX = 0, yawY = 0, yawZ = 0, yawW = 1000;
    if (drawableCount > 0 && objectManager->get_Drawables3D()->get_Item(0) != nullptr &&
        objectManager->get_Drawables3D()->get_Item(0)->get_Parent() != nullptr) {
        const ::float4 orientation = objectManager->get_Drawables3D()->get_Item(0)->get_Parent()->get_Orientation();
        yawX = Milli(orientation.X);
        yawY = Milli(orientation.Y);
        yawZ = Milli(orientation.Z);
        yawW = Milli(orientation.W);
    }
    ramsyscall_printf("%s seq=%u cameras=%d drawables=%d updateables=%d orientation_milli=%d,%d,%d,%d elapsed_ms=%d\n", marker,
                      static_cast<unsigned>(m_frameSequence), static_cast<int>(cameraCount), static_cast<int>(drawableCount),
                      static_cast<int>(updateableCount), yawX, yawY, yawZ, yawW, static_cast<int>(m_core->get_TotalElapsedSeconds() * 1000.0));
}

void Ps1AuthoredScene::frame() {
    const std::uint64_t frameStartUs = he_cpp_custom::MonotonicMicroseconds();
    const std::uint64_t betweenFrameUs = m_frameSequence != 0 ? frameStartUs - m_lastFrameEndUs : 0u;
    m_betweenFramesTotalUs += betweenFrameUs;
    // Each frame() should follow exactly one VBlank; count every extra VBlank as a missed frame.
    const std::uint32_t vblankFrame = gpu().getFrameCount();
    if (m_frameSequence != 0) {
        const std::uint32_t delta = vblankFrame - m_lastVblankFrame;
        if (delta > 1u) m_missedVblanks += delta - 1u;
        if (delta > m_worstVblankDelta) m_worstVblankDelta = delta;
    }
    m_lastVblankFrame = vblankFrame;
#if defined(PS1_CPU_PROFILE)
    const bool hadPendingReport = m_cpuProfileSink->HasPendingReport();
    m_cpuProfileSink->EmitPendingReport();
    const bool profileCapture = m_frameSequence >= 600u && !hadPendingReport;
    if (profileCapture) {
        m_cpuProfileSink->BeginFrame(static_cast<std::uint64_t>(m_frameSequence) + 1u, vblankFrame, Ps1StartupSceneId());
    }
#endif
    const std::uint32_t now = gpu().now();
    double deltaSeconds = static_cast<double>(static_cast<std::uint32_t>(now - m_lastTimestamp)) / 1000000.0;
    m_lastTimestamp = now;
    if (deltaSeconds > kMaxDeltaSeconds) {
        deltaSeconds = kMaxDeltaSeconds;
    }
    m_core->Update(deltaSeconds);
    const std::uint64_t updateEndUs = he_cpp_custom::MonotonicMicroseconds();
    m_core->CompleteFrameBoundary();
    const std::uint64_t boundaryEndUs = he_cpp_custom::MonotonicMicroseconds();
    // Core::Draw drives the render manager, which converts the loaded camera and drawable into GTE
    // poses and submits the frame through the shared renderer with the camera's clear color.
    m_core->Draw();
    m_lastFrameEndUs = he_cpp_custom::MonotonicMicroseconds();
#if defined(PS1_CPU_PROFILE)
    if (profileCapture) {
        m_cpuProfileSink->RecordHostTimings(updateEndUs - frameStartUs, boundaryEndUs - updateEndUs,
                                           m_lastFrameEndUs - boundaryEndUs, betweenFrameUs);
        m_cpuProfileSink->EndFrame(static_cast<std::uint64_t>(m_frameSequence) + 1u, vblankFrame);
    }
#endif
    m_updateTotalUs += updateEndUs - frameStartUs;
    m_boundaryTotalUs += boundaryEndUs - updateEndUs;
    m_drawTotalUs += m_lastFrameEndUs - boundaryEndUs;
    m_overlayUpdateTotalUs += updateEndUs - frameStartUs;
    ++m_overlayFrames;
    m_core->SetHostFrameTimings(static_cast<double>(updateEndUs - frameStartUs) / 1000.0,
                                static_cast<double>(m_lastFrameEndUs - boundaryEndUs) / 1000.0);
    if (m_lastFrameEndUs - m_overlayWindowStartUs >= 500000u) {
        const double drawMilliseconds = m_core->get_LastRenderManager3DDrawMilliseconds();
        Ps1FrameOverlayText overlay{};
        const Ps1FrameOverlaySample sample{m_overlayFrames, m_lastFrameEndUs - m_overlayWindowStartUs,
                                           m_overlayUpdateTotalUs,
                                           static_cast<std::uint64_t>(drawMilliseconds * 1000.0 + 0.5)};
        if (TryFormatFrameOverlayText(sample, overlay)) {
            m_core->SetPerformanceOverlayTextRows(true, HeCppString(overlay.update),
                                                  HeCppString(overlay.render), HeCppString(), HeCppString());
        }
        m_overlayWindowStartUs = m_lastFrameEndUs;
        m_overlayUpdateTotalUs = 0;
        m_overlayFrames = 0;
    }
    ++m_profileFrames;
    ++m_frameSequence;
    if (m_frameSequence == 1800) m_steadyStartVblank = vblankFrame;
    if (m_frameSequence == 2400) {
        ramsyscall_printf("PS1_AUTHORED_STEADY_CADENCE frames=600 vblanks=%u\n",
                          static_cast<unsigned>(vblankFrame - m_steadyStartVblank));
    }

    const Ps1RenderStats& stats = m_renderer.Stats();
    // BIOS text output costs VBlanks, so steady-state reporting is opt-in like the diagnostic
    // scenes. The default build reports the first frame and one cadence checkpoint after 600
    // updates, whose VBlank count is unaffected by earlier printing.
#if defined(PS1_PERIODIC_DIAGNOSTICS)
    const bool periodicReport = (m_frameSequence % 120u) == 0;
#else
    const bool periodicReport = m_frameSequence == 1200;
#endif
    if (m_frameSequence == 600 || m_frameSequence == 1200) {
        // Report 600-frame averages in microseconds; the between-frame phase includes VBlank/GPU waits.
        ramsyscall_printf("PS1_AUTHORED_PHASE_AVG seq=%u update_us=%u boundary_us=%u draw_us=%u between_us=%u\n",
                          static_cast<unsigned>(m_frameSequence), static_cast<unsigned>(m_updateTotalUs / m_profileFrames),
                          static_cast<unsigned>(m_boundaryTotalUs / m_profileFrames),
                          static_cast<unsigned>(m_drawTotalUs / m_profileFrames),
                          static_cast<unsigned>(m_betweenFramesTotalUs / m_profileFrames));
        m_updateTotalUs = m_boundaryTotalUs = m_drawTotalUs = m_betweenFramesTotalUs = 0;
        m_profileFrames = 0;
        // One short line per checkpoint keeps the measured interval nearly free of BIOS output.
        ramsyscall_printf("PS1_AUTHORED_CADENCE seq=%u vblank_frame=%u missed_vblanks=%u worst_delta=%u heap_end=%p\n",
                          static_cast<unsigned>(m_frameSequence), static_cast<unsigned>(vblankFrame),
                          static_cast<unsigned>(m_missedVblanks), static_cast<unsigned>(m_worstVblankDelta), psyqo_heap_end());
    }
    if (m_frameSequence == 1 || periodicReport || !m_render3D->LastFrameSubmitted()) {
        ReportSceneState(m_frameSequence == 1 ? "PS1_AUTHORED_FIRST_FRAME" : "PS1_AUTHORED_FRAME");
        ramsyscall_printf(
            "PS1_AUTHORED_DRAW visited=%u submitted=%u backface=%u degenerate=%u invalid=%u flags=%08x packets=%u highwater=%u "
            "guards=%u failure=%u\n",
            static_cast<unsigned>(m_render3D->LastVisitedDrawables()), static_cast<unsigned>(stats.submittedTriangles),
            static_cast<unsigned>(stats.backfaceRejected), static_cast<unsigned>(stats.degenerate), static_cast<unsigned>(stats.invalid),
            static_cast<unsigned>(stats.projectionFlags), static_cast<unsigned>(stats.packetCount),
            static_cast<unsigned>(stats.highWaterBytes), stats.guardsIntact ? 1u : 0u, static_cast<unsigned>(stats.failure));
        ramsyscall_printf("PS1_AUTHORED_UI sprites=%u shapes=%u texts=%u glyph_quads=%u skipped_text=%u submitted_sprites=%u submitted_triangles=%u failure=%s\n",
                          static_cast<unsigned>(m_render2D->LastVisitedSprites()),
                          static_cast<unsigned>(m_render2D->LastVisitedShapes()),
                          static_cast<unsigned>(m_render2D->LastVisitedTexts()),
                          static_cast<unsigned>(m_render2D->LastQueuedGlyphQuads()),
                          static_cast<unsigned>(m_render2D->LastSkippedTexts()), static_cast<unsigned>(stats.uiSprites),
                          static_cast<unsigned>(stats.uiTriangles),
                          m_render2D->LastFailureMarker() == nullptr ? "none" : m_render2D->LastFailureMarker());
        // The pose proves the GTE inputs come from the loaded camera and cube transforms; the VBlank
        // frame count against the scene sequence shows cadence; the heap end shows steady-state allocation.
        const Ps1GtePose& pose = m_render3D->LastPose();
        ramsyscall_printf("PS1_AUTHORED_FOV milliradians=%d\n",
                          static_cast<int>(m_render3D->LastFieldOfViewRadians() * 1000.0f));
        ramsyscall_printf("PS1_AUTHORED_POSE trx=%d try=%d trz=%d h=%d r0=%d,%d,%d r1=%d,%d,%d r2=%d,%d,%d vblank_frame=%u heap_end=%p\n",
                          static_cast<int>(pose.Translation[0]), static_cast<int>(pose.Translation[1]), static_cast<int>(pose.Translation[2]),
                          static_cast<int>(pose.ProjectionDistance), static_cast<int>(pose.Rotation[0][0]), static_cast<int>(pose.Rotation[0][1]),
                          static_cast<int>(pose.Rotation[0][2]), static_cast<int>(pose.Rotation[1][0]), static_cast<int>(pose.Rotation[1][1]),
                          static_cast<int>(pose.Rotation[1][2]), static_cast<int>(pose.Rotation[2][0]), static_cast<int>(pose.Rotation[2][1]),
                          static_cast<int>(pose.Rotation[2][2]), static_cast<unsigned>(gpu().getFrameCount()), psyqo_heap_end());
        // The light rows and colour columns prove the authored directional light reaches the GTE
        // rather than the renderer inventing a shading term.
        const Ps1GteLighting& lighting = m_render3D->LastLighting();
        ramsyscall_printf("PS1_AUTHORED_LIGHT count=%u l0=%d,%d,%d l1=%d,%d,%d l2=%d,%d,%d c0=%d,%d,%d bg=%d,%d,%d\n",
                          static_cast<unsigned>(m_render3D->LastLightCount()), static_cast<int>(lighting.LightMatrix[0][0]),
                          static_cast<int>(lighting.LightMatrix[0][1]), static_cast<int>(lighting.LightMatrix[0][2]),
                          static_cast<int>(lighting.LightMatrix[1][0]), static_cast<int>(lighting.LightMatrix[1][1]),
                          static_cast<int>(lighting.LightMatrix[1][2]), static_cast<int>(lighting.LightMatrix[2][0]),
                          static_cast<int>(lighting.LightMatrix[2][1]), static_cast<int>(lighting.LightMatrix[2][2]),
                          static_cast<int>(lighting.ColorMatrix[0][0]), static_cast<int>(lighting.ColorMatrix[1][0]),
                          static_cast<int>(lighting.ColorMatrix[2][0]), static_cast<int>(lighting.BackgroundColor[0]),
                          static_cast<int>(lighting.BackgroundColor[1]), static_cast<int>(lighting.BackgroundColor[2]));
        ramsyscall_printf("PS1_AUTHORED_SHADE sampled=%u v0=%06x v1=%06x v2=%06x\n", stats.litSampled ? 1u : 0u,
                          static_cast<unsigned>(stats.litVertexColors[0]), static_cast<unsigned>(stats.litVertexColors[1]),
                          static_cast<unsigned>(stats.litVertexColors[2]));
    }
    if (!m_render3D->LastFrameSubmitted()) {
        const char* marker = m_render3D->LastFailureMarker();
        psyqo::Kernel::assert(false, marker == nullptr ? "PS1_RENDER_FRAME_REJECTED" : marker);
    }
}

}  // namespace helengine::ps1
