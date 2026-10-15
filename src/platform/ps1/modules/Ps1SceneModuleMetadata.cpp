#include "platform/ps1/modules/Ps1SceneModuleMetadata.hpp"
#include "RuntimeCodeModuleManifest.hpp"
#include "RuntimeCodeModuleManifestEntry.hpp"
#include "RuntimeCodeModuleLoadState.hpp"

namespace helengine::ps1 {

::RuntimeCodeModuleManifestEntry* Ps1CreateDiscModuleEntry(const HeCppString& moduleId, Array<HeCppString>* dependencies) {
    return new ::RuntimeCodeModuleManifestEntry(moduleId, "ps1-overlay-v1", ::RuntimeCodeModuleLoadState::Unloadable, dependencies);
}

::RuntimeCodeModuleManifest* Ps1CreateDiscModuleManifest(Array<::RuntimeCodeModuleManifestEntry*>* entries) {
    return new ::RuntimeCodeModuleManifest(entries);
}

}  // namespace helengine::ps1
