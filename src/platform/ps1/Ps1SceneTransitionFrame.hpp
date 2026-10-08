#pragma once
namespace helengine::ps1 {
// A single-scene transition unloads the old camera before loading the new scene.
// Stable scenes still go through the renderer's missing-camera validation.
inline bool WaitForSceneTransitionCamera(bool transitionActive, int cameraCount) {
    return transitionActive && cameraCount == 0;
}
}
