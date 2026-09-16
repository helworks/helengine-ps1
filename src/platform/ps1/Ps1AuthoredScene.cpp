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
    m_options->set_RenderList2DInitialCapacity(0);
    m_options->set_RenderList3DInitialCapacity(16);
    m_options->set_CommitPendingSceneOperationsDuringDraw(false);
    m_options->set_DefaultUpdateDeltaSeconds(1.0 / 60.0);
    m_render3D = new Ps1RenderManager3D();
    m_render2D = new Ps1RenderManager2D();
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
    if (cameraCount != 1 || drawableCount != 1) {
        he_cpp_custom::Fail("Authored scene must register exactly one camera and one drawable");
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
    ramsyscall_printf("%s seq=%u cameras=%d drawables=%d updateables=%d cube_orientation_milli=%d,%d,%d,%d elapsed_ms=%d\n", marker,
                      static_cast<unsigned>(m_frameSequence), static_cast<int>(cameraCount), static_cast<int>(drawableCount),
                      static_cast<int>(updateableCount), yawX, yawY, yawZ, yawW, static_cast<int>(m_core->get_TotalElapsedSeconds() * 1000.0));
}

void Ps1AuthoredScene::frame() {
    const std::uint32_t now = gpu().now();
    double deltaSeconds = static_cast<double>(static_cast<std::uint32_t>(now - m_lastTimestamp)) / 1000000.0;
    m_lastTimestamp = now;
    if (deltaSeconds > kMaxDeltaSeconds) {
        deltaSeconds = kMaxDeltaSeconds;
    }
    m_core->Update(deltaSeconds);
    m_core->CompleteFrameBoundary();
    m_core->Draw();
    ++m_frameSequence;

    // Keep the approved presentation loop (clear, GPU completion marker, fresh-VBlank swap) alive
    // until the loaded camera and cube are submitted through the GTE renderer.
    if (!m_renderer.QueueClear(gpu())) {
        psyqo::Kernel::assert(false, "PS1_RENDER_QUEUE_FAILED");
    }
    if (m_frameSequence == 1 || (m_frameSequence % 120u) == 0) {
        ReportSceneState(m_frameSequence == 1 ? "PS1_AUTHORED_FIRST_FRAME" : "PS1_AUTHORED_FRAME");
        ramsyscall_printf("PS1_AUTHORED_DRAW visited=%u\n", static_cast<unsigned>(m_render3D->LastVisitedDrawables()));
    }
}

}  // namespace helengine::ps1
