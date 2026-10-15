#pragma once

namespace helengine::ps1 {

/** Keeps initial CD loading asynchronous and publishes the persistent loading scene before the target. */
class Ps1DiscSceneStartup {
public:
    const char* Begin(const char* startupSceneId,const char* loadingSceneId) {
        StartupSceneId=startupSceneId;
        LoadingSceneId=loadingSceneId;
        WaitingForLoading=loadingSceneId!=nullptr && loadingSceneId[0]!=0 &&
            __builtin_strcmp(startupSceneId,loadingSceneId)!=0;
        return WaitingForLoading?LoadingSceneId:StartupSceneId;
    }
    /** Returns the target once, only after the loading scene's complete preparation and publication. */
    const char* TakeTargetIfLoadingReady(bool loadingSceneReady,bool transitionActive) {
        if (!WaitingForLoading || !loadingSceneReady || transitionActive) return nullptr;
        WaitingForLoading=false;
        return StartupSceneId;
    }
    const char* GetLoadingSceneId() const { return LoadingSceneId; }
    bool IsWaitingForLoading() const { return WaitingForLoading; }
private:
    const char* StartupSceneId=nullptr;
    const char* LoadingSceneId=nullptr;
    bool WaitingForLoading=false;
};

} // namespace helengine::ps1
