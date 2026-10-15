#pragma once
namespace helengine::ps1 {
/** Steady frames never drain; queued scene mutation waits for the previous GPU references. */
inline bool ShouldDeferModuleBoundary(bool transitionActive, bool pendingSceneOperations, bool gpuIdle) {
    return (transitionActive || pendingSceneOperations) && !gpuIdle;
}
}
