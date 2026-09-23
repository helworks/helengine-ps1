#include "platform/ps1/rendering/Ps1CubeMesh.hpp"
#include "platform/ps1/rendering/Ps1DepthBucket.hpp"
#include "platform/ps1/rendering/Ps1PacketBudget.hpp"
#include "platform/ps1/rendering/Ps1RotationClock.hpp"
#include "platform/ps1/rendering/Ps1TextLayout.hpp"

#include <cassert>
#include <cstdint>

namespace {

void TestPacketBudget() {
    helengine::ps1::Ps1PacketBudget budget(2);
    std::uint32_t slot = 99;
    assert(budget.TryReserve(slot) && slot == 0);
    assert(budget.TryReserve(slot) && slot == 1);
    slot = 99;
    assert(!budget.TryReserve(slot) && slot == 99 && budget.Used() == 2);
    budget.Reset();
    assert(budget.TryReserve(slot) && slot == 0);

    helengine::ps1::Ps1PacketBudget zero(0);
    assert(!zero.TryReserve(slot));

    using Limits = helengine::ps1::Ps1FramePacketLimits;
    assert(Limits::PacketBudgetBytes == 16384);
    assert(Limits::GouraudTriangles == 64);
    assert(Limits::UiTriangles == 96);
    assert(Limits::TexturedTriangles == 256);
    assert(Limits::FixedBytes == 11920);
    assert(Limits::SceneTriangles == 223);
    assert(Limits::FixedBytes + Limits::SceneTriangles * Limits::TriangleFragmentBytes <= Limits::PacketBudgetBytes);
    assert(Limits::FixedBytes + (Limits::SceneTriangles + 1) * Limits::TriangleFragmentBytes > Limits::PacketBudgetBytes);
    helengine::ps1::Ps1PacketBudget ui(Limits::UiTriangles);
    for (std::uint32_t i = 0; i < Limits::UiTriangles; ++i) assert(ui.TryReserve(slot));
    assert(!ui.TryReserve(slot) && ui.Used() == Limits::UiTriangles);
    ui.Reset(Limits::UiTriangles);
    assert(ui.TryReserve(slot) && slot == 0);
}

void TestTextLayout() {
    using namespace helengine::ps1;
    struct Emitted { char letter; float x; float y; } glyphs[8]{};
    unsigned count = 0;
    auto lookup = [](char ch, Ps1TextGlyph& glyph) {
        if (ch < 'A' || ch > 'Z') return false;
        glyph = {4.0f, 6.0f, 1.0f, 5.0f};
        return true;
    };
    auto emit = [&](char ch, const Ps1TextGlyph&, float x, float y) {
        glyphs[count++] = {ch, x, y};
        return true;
    };
    assert(LayoutPs1Text("AB C\nD", 6, 10.0f, 20.0f, 16.0f, 2.0f, 2.0f, 40.0f,
                         Ps1TextAlignment::Right, lookup, emit));
    assert(count == 4);
    assert(glyphs[0].x == 18.0f && glyphs[0].y == 22.0f);
    assert(glyphs[1].x == 28.0f && glyphs[1].y == 22.0f);
    assert(glyphs[2].x == 42.0f && glyphs[2].y == 22.0f);
    assert(glyphs[3].x == 42.0f && glyphs[3].y == 38.0f);
}

void TestDepthBuckets() {
    std::uint16_t bucket = 999;
    assert(helengine::ps1::Ps1DepthBucket::ToCombinedSceneBucket(0) == 256);
    assert(helengine::ps1::Ps1DepthBucket::ToCombinedSceneBucket(255) == 511);
    assert(helengine::ps1::Ps1DepthBucket::CombinedBucketCount == 512);
    assert(helengine::ps1::Ps1DepthBucket::TryMap(160, 160, 160, bucket) && bucket == 0);
    assert(helengine::ps1::Ps1DepthBucket::TryMap(1183, 1183, 1183, bucket) && bucket == 255);
    assert(!helengine::ps1::Ps1DepthBucket::TryMap(159, 512, 512, bucket));
    assert(!helengine::ps1::Ps1DepthBucket::TryMap(512, 1184, 512, bucket));
    assert(!helengine::ps1::Ps1DepthBucket::TryMap(-1, 512, 512, bucket));
}

struct Vec3i {
    int x;
    int y;
    int z;
};

Vec3i Sub(const helengine::ps1::Ps1CubeMesh::Position& a, const helengine::ps1::Ps1CubeMesh::Position& b) {
    return {a.X - b.X, a.Y - b.Y, a.Z - b.Z};
}

Vec3i Cross(const Vec3i& a, const Vec3i& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

void TestCubeMesh() {
    for (const auto& face : helengine::ps1::Ps1CubeMesh::Faces) {
        assert(face.A < 8 && face.B < 8 && face.C < 8);
        const auto& a = helengine::ps1::Ps1CubeMesh::Positions[face.A];
        const auto& b = helengine::ps1::Ps1CubeMesh::Positions[face.B];
        const auto& c = helengine::ps1::Ps1CubeMesh::Positions[face.C];
        const Vec3i normal = Cross(Sub(b, a), Sub(c, a));
        const int dot = normal.x * (a.X + b.X + c.X) + normal.y * (a.Y + b.Y + c.Y) +
                        normal.z * (a.Z + b.Z + c.Z);
        assert(dot > 0);
    }
    for (unsigned i = 0; i < 6; ++i) {
        assert(helengine::ps1::Ps1CubeMesh::Faces[i * 2].Rgb ==
               helengine::ps1::Ps1CubeMesh::Faces[i * 2 + 1].Rgb);
    }
}

void TestRotationClock() {
    helengine::ps1::Ps1RotationClock a;
    helengine::ps1::Ps1RotationClock b;
    a.Reset(0);
    b.Reset(0);
    for (std::uint32_t t = 10000; t <= 1000000; t += 10000) a.Advance(t);
    for (std::uint32_t t = 20000; t <= 1000000; t += 20000) b.Advance(t);
    assert(a.PhaseX() == b.PhaseX() && a.PhaseY() == b.PhaseY());
    assert(a.PhaseX() == 170 && a.PhaseY() == 256);

    helengine::ps1::Ps1RotationClock paused;
    paused.Reset(0);
    paused.Advance(1000000);
    assert(paused.PhaseX() == 17 && paused.PhaseY() == 25);

    helengine::ps1::Ps1RotationClock zero;
    zero.Reset(42);
    zero.Advance(42);
    assert(zero.PhaseX() == 0 && zero.PhaseY() == 0);

    helengine::ps1::Ps1RotationClock wrapped;
    wrapped.Reset(0xffffff00u);
    wrapped.Advance(0x00002610u);
    assert(wrapped.PhaseX() == 1 && wrapped.PhaseY() == 2);
}

}  // namespace

int main() {
    TestPacketBudget();
    TestTextLayout();
    TestDepthBuckets();
    TestCubeMesh();
    TestRotationClock();
    return 0;
}
