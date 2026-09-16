#pragma once

class RuntimeSceneCatalog;

namespace helengine::ps1 {

/// <summary>Builds Core's scene catalog from the generated ps1_runtime_scene_catalog table.</summary>
/// <returns>A new catalog, or null when the generated table is empty.</returns>
::RuntimeSceneCatalog* BuildPs1RuntimeSceneCatalog();

/// <summary>Returns the generated startup scene id, or null when none was recorded.</summary>
const char* Ps1StartupSceneId();

}  // namespace helengine::ps1
