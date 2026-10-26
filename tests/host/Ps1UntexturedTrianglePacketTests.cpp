#include "platform/ps1/rendering/Ps1UntexturedTrianglePacket.hpp"
#include <cstdio>

using namespace helengine::ps1;

int main() {
    Ps1FrameContext frame;
    assert(frame.Begin(1));
    // A genuine gradient consumes only Gouraud packets, including a one-bit channel difference.
    for (unsigned i = 0; i < Ps1FrameContext::GouraudCapacity; ++i) {
        auto packet = Ps1UntexturedTrianglePacket::Reserve(frame, 0x00543210, 0x00543211, 0x00543210);
        assert(packet.Flat == nullptr && packet.Gouraud != nullptr);
        assert(frame.Insert(*packet.Gouraud, 12));
    }
    auto exhausted = Ps1UntexturedTrianglePacket::Reserve(frame, 0x00543210, 0x00543210, 0x00543211);
    assert(exhausted.Flat == nullptr && exhausted.Gouraud == nullptr);
    // Equal NCCT output remains drawable after Gouraud is full; no fallback to material colour.
    // Ignore the high GPU command byte when comparing final RGB channels.
    for (unsigned i = 0; i < Ps1FrameContext::TriangleCapacity; ++i) {
        auto packet = Ps1UntexturedTrianglePacket::Reserve(frame, 0x30543210, 0x00543210, 0x00543210);
        if (packet.Flat == nullptr || packet.Gouraud != nullptr) {
            std::puts("Equal final RGB incorrectly consumed/exhausted Gouraud instead of the available flat pool");
            return 1;
        }
        assert(frame.Insert(*packet.Flat, 12));
    }
    exhausted = Ps1UntexturedTrianglePacket::Reserve(frame, 0x00543210, 0x00543210, 0x00543210);
    assert(exhausted.Flat == nullptr && exhausted.Gouraud == nullptr);
    assert(frame.GuardsIntact());
    assert(frame.UsedPackets() == 1 + Ps1FrameContext::GouraudCapacity + Ps1FrameContext::TriangleCapacity);
    frame.Cancel();
    assert(frame.Begin(2));
    const std::uint32_t colours[] = {0x00000000, 0x00ffffff, 0x00543210};
    for (auto colour : colours) {
        auto packet = Ps1UntexturedTrianglePacket::Reserve(frame, colour, colour, colour);
        assert(packet.Flat != nullptr && packet.Gouraud == nullptr);
    }
    auto gradient = Ps1UntexturedTrianglePacket::Reserve(frame, 0x00543210, 0x00553310, 0x00543210);
    assert(gradient.Flat == nullptr && gradient.Gouraud != nullptr);
    frame.Cancel();
    assert(frame.Begin(3));
    for (unsigned i = 0; i < Ps1FrameContext::TriangleCapacity; ++i) {
        auto packet = Ps1UntexturedTrianglePacket::Reserve(frame, 0x00543210, 0x00543210, 0x00543210);
        assert(packet.Flat != nullptr && packet.Gouraud == nullptr);
    }
    // Exhaustion remains explicit even when the other pool still has room.
    exhausted = Ps1UntexturedTrianglePacket::Reserve(frame, 0x00543210, 0x00543210, 0x00543210);
    assert(exhausted.Flat == nullptr && exhausted.Gouraud == nullptr);
    gradient = Ps1UntexturedTrianglePacket::Reserve(frame, 0x00543210, 0x00543211, 0x00543210);
    assert(gradient.Flat == nullptr && gradient.Gouraud != nullptr);
    assert(frame.GuardsIntact());
    std::puts("PS1_UNTEXTURED_TRIANGLE_PACKET_PASS flat=376 gouraud=64 exact_rgb=1 explicit_capacity=3");
}
