#pragma once

#include "SceneLoadMode.hpp"

namespace helengine::ps1 {
/** Executes the generated startup policy without discarding the persistent loading scene. */
template<class SceneManagerType>
bool StartAuthoredScenes(SceneManagerType& manager, const char* startupId, const char* loadingId) {
    bool sameScene = loadingId != nullptr;
    if (sameScene) {
        unsigned index = 0;
        while (startupId[index] != '\0' && startupId[index] == loadingId[index]) ++index;
        sameScene = startupId[index] == loadingId[index];
    }
    if (loadingId != nullptr && loadingId[0] != '\0' && !sameScene) {
        manager.LoadScene(loadingId, ::SceneLoadMode::Additive);
        manager.CommitPendingOperationsAtFrameBoundary();
        manager.RequestSceneTransition(startupId);
        return true;
    }
    manager.LoadScene(startupId, ::SceneLoadMode::Single);
    manager.CommitPendingOperationsAtFrameBoundary();
    return false;
}
}
