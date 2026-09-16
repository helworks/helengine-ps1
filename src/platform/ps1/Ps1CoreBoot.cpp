#include "platform/ps1/Ps1CoreBoot.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "Core.hpp"
#include "CoreInitializationOptions.hpp"
#include "RuntimeSceneCatalog.hpp"
#include "PlatformInfo.hpp"
#include "IContentStreamSource.hpp"
#include "common/syscalls/syscalls.h"

namespace {
/// <summary>Rejects asset access during the Core-only boot checkpoint.</summary>
class BootContentSource final : public ::IContentStreamSource {
public:
    Stream* OpenRead(HeCppString assetPath) override {
        ramsyscall_printf("PS1_CORE_UNEXPECTED_ASSET %s\n", assetPath.c_str());
        he_cpp_custom::Fail("Core boot must not load assets");
    }
};
::Core* EngineCore = nullptr;
}

void helengine::ps1::InitializeCoreBoot(psyqo::GPU& gpu) {
    InitializeRuntimeClock(gpu);
    ramsyscall_printf("PS1_CORE_INITIALIZING\n");
    auto* options = new ::CoreInitializationOptions();
    options->set_ContentStreamSource(new BootContentSource());
    // The boot checkpoint intentionally has no packaged scenes. Supplying an
    // explicit empty catalog still exercises creation of Core's scene manager.
    Array<::RuntimeSceneCatalogEntry*> emptyScenes(0);
    options->set_SceneCatalog(new ::RuntimeSceneCatalog(&emptyScenes));
    options->set_UpdateOrderLayers(1);
    options->set_RenderOrderLayers3D(1);
    options->set_UpdateListInitialCapacity(0);
    options->set_RenderList2DInitialCapacity(0);
    options->set_RenderList3DInitialCapacity(0);
    options->set_CommitPendingSceneOperationsDuringDraw(false);
    auto* platform = new ::PlatformInfo("ps1", "core-boot");
    EngineCore = new ::Core(options);
    ramsyscall_printf("PS1_CORE_CONSTRUCTED core=%p\n", EngineCore);
    EngineCore->Initialize(nullptr, nullptr, nullptr, platform, options);
    if (EngineCore->get_ObjectManager() == nullptr || EngineCore->get_SceneManager() == nullptr) {
        he_cpp_custom::Fail("Core initialization did not create required managers");
    }
    ramsyscall_printf("PS1_CORE_READY core=%p\n", EngineCore);
}


