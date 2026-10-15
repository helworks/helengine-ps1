#include "platform/ps1/modules/Ps1ModuleFramePolicy.hpp"
#include "platform/ps1/modules/Ps1DiscSceneStartup.hpp"
#include "platform/ps1/rendering/Ps1FrameContext.hpp"
#include <cstdio>

int main() {
    using namespace helengine::ps1;
    Ps1DiscSceneStartup startup;
    assert(__builtin_strcmp(startup.Begin("menu","loading"),"loading")==0);
    assert(startup.TakeTargetIfLoadingReady(false,true)==nullptr);
    assert(startup.TakeTargetIfLoadingReady(false,false)==nullptr);
    assert(startup.TakeTargetIfLoadingReady(true,true)==nullptr);
    assert(__builtin_strcmp(startup.TakeTargetIfLoadingReady(true,false),"menu")==0);
    assert(startup.TakeTargetIfLoadingReady(true,false)==nullptr);
    assert(__builtin_strcmp(startup.Begin("loading","loading"),"loading")==0);
    assert(!startup.IsWaitingForLoading());
    assert(__builtin_strcmp(startup.Begin("menu",""),"menu")==0);
    assert(!startup.IsWaitingForLoading());
    Ps1FrameContext frames[2];
    psyqo::GPU gpu;
    for (unsigned sequence=1;sequence<=60;++sequence) {
        // Previous DMA is active at frame entry in the SDK; that must not halve normal cadence.
        assert(!ShouldDeferModuleBoundary(false,false,false));
        auto& frame=frames[sequence%2];
        assert(frame.Begin(sequence));
        assert(frame.Submit(gpu));
    }
    const auto calls=gpu.Calls.size();
    bool active=false;
    const bool activeAtFrameEntry=active;
    // Authored input requests a transition during Core.Update, after frame-entry sampling.
    active=true;
    assert(!activeAtFrameEntry);
    assert(ShouldDeferModuleBoundary(active,false,false));
    assert(ShouldDeferModuleBoundary(false,true,false));
    // Sequence61 drains the previous chain without constructing new packet references.
    assert(gpu.Calls.size()==calls);
    assert(!ShouldDeferModuleBoundary(true,true,true));
    auto& resumed=frames[0];
    assert(!resumed.Begin(61)); // Missing the unsubmitted sequence would reuse this parity too early.
    assert(resumed.Begin(62));
    assert(resumed.Submit(gpu));
    assert(frames[1].Begin(63));
    assert(frames[1].Submit(gpu));
    std::puts("PS1_MODULE_FRAME_PASS steady=60 drain=1 resumed=2");
}
