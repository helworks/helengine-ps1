#include "platform/ps1/rendering/Ps1FrameContext.hpp"
#include <cstdio>
using namespace helengine::ps1;
int main() {
    Ps1FrameContext frames[2]; psyqo::GPU gpu;
    int source;
    for (unsigned sequence = 1; sequence <= 6; ++sequence) {
        auto& frame = frames[sequence % 2];
        assert(frame.Begin(sequence));
        assert(frame.BeginCamera(true, psyqo::Color{}));
        auto* triangle = frame.TryReserveTexturedTriangle(); assert(triangle);
        assert(frame.Insert(*triangle, 4)); assert(!frame.Insert(*triangle, 4));
        auto* quad = frame.TryAppendTexturedQuad(0); assert(quad);
        frame.RememberTexturedQuad(*quad, &source, 1);
        assert(frame.ReuseTexturedQuad(*quad, &source, 1));
        assert(frame.BeginCamera(false, psyqo::Color{}));
        assert(frame.TryAppendTexturedQuad(0));
        assert(frame.Submit(gpu)); assert(!frame.Begin(sequence + 1));
        assert(frame.UsedPackets() == 5 && frame.GuardsIntact());
    }
    auto& frame = frames[1]; assert(frame.Begin(7));
    auto* quad = frame.TryReserveTexturedQuad(); assert(quad);
    assert(!frame.ReuseTexturedQuad(*quad, &source, 1)); // Previous slot was a triangle.
    frame.RememberTexturedQuad(*quad, &source, 1); assert(frame.Insert(*quad, 0));
    assert(frame.Submit(gpu)); assert(frame.Begin(9));
    auto* triangle = frame.TryReserveTexturedTriangle(); assert(triangle); assert(frame.Insert(*triangle, 0));
    assert(frame.Submit(gpu)); assert(frame.Begin(11));
    quad = frame.TryReserveTexturedQuad(); assert(quad); assert(!frame.ReuseTexturedQuad(*quad, &source, 1));
    for (unsigned i = 1; i < Ps1FrameContext::TexturedQuadCapacity; ++i) assert(frame.TryReserveTexturedTriangle());
    assert(!frame.TryReserveTexturedTriangle() && !frame.TryReserveTexturedQuad());
    assert(frame.GuardsIntact()); frame.Cancel();
    std::puts("PS1_TEXTURED_FRAME_PASS mixed_camera=6 parity=6 typed_cache=3 capacity=1");
}
