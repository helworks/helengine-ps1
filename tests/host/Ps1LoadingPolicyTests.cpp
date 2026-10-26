#include "platform/ps1/Ps1SceneStartup.hpp"
#include "platform/ps1/Ps1TextureUploadFence.hpp"
#include "platform/ps1/rendering/Ps1FrameContext.hpp"
#include <cassert>
#include <string>
#include <vector>
#include <cstdio>
using namespace helengine::ps1;
extern "C" { volatile Ps1FrameDiagnostics helengine_ps1_frame_diagnostics{}; }
/** Models PSYQo's outstanding DMA callback contract without starting a GPU or managed scene. */
struct UploadSpy {
    bool Transferring = true;
    bool Ready = false;
    std::vector<int> Calls;
    void waitChainIdle(){assert(helengine_ps1_frame_diagnostics.Stage==50);Calls.push_back(1);Transferring=false;}
    void waitReady(){assert(!Transferring && helengine_ps1_frame_diagnostics.Stage==52);Calls.push_back(2);Ready=true;}
    void upload(){assert(!Transferring && Ready);Calls.push_back(3);}
};
/** Records the exact startup requests made by the production policy helper. */
struct SceneSpy {
    std::vector<std::string> Calls;
    void LoadScene(const char* id,SceneLoadMode mode){Calls.push_back(std::string(mode==SceneLoadMode::Single?"single:":"additive:")+id);}
    void CommitPendingOperationsAtFrameBoundary(){Calls.push_back("commit");}
    void RequestSceneTransition(const char* id){Calls.push_back(std::string("transition:")+id);}
};
int main(){
    UploadSpy upload;
    WaitForTextureUpload(upload);upload.upload();
    assert((upload.Calls==std::vector<int>{1,2,3}));
    assert(helengine_ps1_frame_diagnostics.Stage==53);
    SceneSpy direct;
    assert(!StartAuthoredScenes(direct,"main",""));
    assert((direct.Calls==std::vector<std::string>{"single:main","commit"}));
    SceneSpy same; char equivalent[]="loading";
    assert(!StartAuthoredScenes(same,"loading",equivalent));
    assert((same.Calls==std::vector<std::string>{"single:loading","commit"}));
    SceneSpy persistent;
    assert(StartAuthoredScenes(persistent,"main","loading"));
    assert((persistent.Calls==std::vector<std::string>{"additive:loading","commit","transition:main"}));
    Ps1FrameContext context; psyqo::GPU gpu;
    assert(context.Begin(1));
    assert(context.BeginCamera(true,psyqo::Color{.packed=0x123456}));
    assert(context.TryAppendUiTriangle(1));
    assert(context.BeginCamera(false,psyqo::Color{.packed=0}));
    assert(context.TryAppendTexturedQuad(1));
    assert(context.Submit(gpu));
    assert(gpu.Calls.size()==3 && gpu.Calls[0].Clear && gpu.Calls[0].Color==0x123456);
    assert(!gpu.Calls[1].Clear && !gpu.Calls[2].Clear);
    assert(gpu.Calls[1].Address!=gpu.Calls[2].Address);
    assert(gpu.Calls[1].Packets==1 && gpu.Calls[2].Packets==1);
    assert(context.UsedPackets()==4);
    assert(!context.Begin(2)); // Same-parity packets remain owned by the previous submission.
    assert(context.Begin(3));
    assert(context.BeginCamera(true,psyqo::Color{.packed=7}));
    for(unsigned camera=1;camera<Ps1FrameContext::CameraCapacity;++camera){assert(context.BeginCamera(true,psyqo::Color{.packed=camera}));}
    assert(!context.BeginCamera(false,psyqo::Color{})); // No silent camera truncation.
    gpu.Calls.clear();assert(context.Submit(gpu));
    assert(gpu.Calls.size()==2*Ps1FrameContext::CameraCapacity);
    assert(context.Begin(5));assert(context.BeginCamera(false,psyqo::Color{}));
    for(unsigned packet=0;packet<Ps1FrameContext::TexturedQuadCapacity;++packet){assert(context.TryAppendTexturedQuad(0));}
    assert(context.BeginCamera(false,psyqo::Color{}));
    assert(!context.TryAppendTexturedQuad(0)); // Capacity is cumulative across cameras.
    context.Cancel();assert(context.Begin(6));
    gpu.Calls.clear();assert(context.Submit(gpu,psyqo::Color{.packed=42}));
    assert(gpu.Calls.size()==2 && gpu.Calls[0].Color==42); // Legacy clear boot remains intact.
    // The final camera's reverse UI flush must execute its scene first, then the
    // opaque badge background, then all DEBUG glyphs. No extra camera consumes capacity.
    assert(context.Begin(8));
    assert(context.BeginCamera(false, psyqo::Color{}));
    auto* earlierCamera = context.TryAppendUiTriangle(0); assert(earlierCamera);
    assert(context.BeginCamera(false, psyqo::Color{}));
    auto* scene = context.TryReserveUiTriangle(); assert(scene);
    auto* background = context.TryReserveUiTriangle(); assert(background);
    Ps1FrameContext::TexturedQuadFragment* glyphs[5]{};
    for (auto& glyph : glyphs) { glyph = context.TryReserveTexturedQuad(); assert(glyph); }
    for (unsigned index = 5; index > 0; --index) assert(context.Insert(*glyphs[index - 1], 0));
    assert(context.InsertUiTriangle(*background, 0));
    assert(context.InsertUiTriangle(*scene, 0));
    gpu.Calls.clear(); assert(context.Submit(gpu));
    assert(gpu.Calls.size() == 2 && gpu.Calls[0].ExecutionOrder[0] == earlierCamera);
    const auto& order = gpu.Calls[1].ExecutionOrder;
    assert(order.size() == 7 && order[0] == scene && order[1] == background);
    for (unsigned index = 0; index < 5; ++index) assert(order[index + 2] == glyphs[index]);
    std::puts("PS1_LOADING_POLICY_PASS startup=3 camera_order=2 parity=1 capacity=2 legacy=1 upload_fence=1");
}
