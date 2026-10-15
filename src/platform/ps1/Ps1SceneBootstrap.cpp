#include "platform/ps1/Ps1SceneBootstrap.hpp"

#include <cstddef>
#include <cstdint>

#include "RuntimeSceneCatalog.hpp"
#include "RuntimeSceneCatalogEntry.hpp"
#include "ps1_runtime_scene_catalog.hpp"
#include "runtime/array.hpp"
#if defined(PS1_SCENE_MODULES)
#include "ps1_scene_module_metadata.hpp"
#endif

namespace helengine::ps1 {

::RuntimeSceneCatalog* BuildPs1RuntimeSceneCatalog() {
    std::size_t sceneCount = 0;
    const HEPs1RuntimeSceneCatalogEntry* entries = he_ps1_runtime_scene_catalog_entries(&sceneCount);
    if (entries == nullptr || sceneCount == 0) {
        return nullptr;
    }
    Array<::RuntimeSceneCatalogEntry*>* catalogEntries = new Array<::RuntimeSceneCatalogEntry*>(static_cast<int32_t>(sceneCount));
    for (std::size_t index = 0; index < sceneCount; ++index) {
        const HEPs1RuntimeSceneCatalogEntry& sourceEntry = entries[index];
#if defined(PS1_SCENE_MODULES)
        auto* requiredIds = Ps1GeneratedRequiredModules(sourceEntry.SceneId);
        (*catalogEntries)[static_cast<int32_t>(index)] = new ::RuntimeSceneCatalogEntry(sourceEntry.SceneId,
            sourceEntry.CookedRelativePath, requiredIds);
        delete requiredIds;
#else
        (*catalogEntries)[static_cast<int32_t>(index)] = new ::RuntimeSceneCatalogEntry(sourceEntry.SceneId, sourceEntry.CookedRelativePath);
#endif
    }
    auto* catalog = new ::RuntimeSceneCatalog(catalogEntries);
    for (std::size_t index = 0; index < sceneCount; ++index) {
        (*catalogEntries)[static_cast<int32_t>(index)]->Dispose();
        delete (*catalogEntries)[static_cast<int32_t>(index)];
    }
    delete catalogEntries;
    return catalog;
}

const char* Ps1StartupSceneId() {
    const char* startupSceneId = he_ps1_runtime_startup_scene_id();
    return startupSceneId == nullptr || startupSceneId[0] == '\0' ? nullptr : startupSceneId;
}

}  // namespace helengine::ps1
