#include "platform/ps1/rendering/Ps1CubeMesh.hpp"
#include "platform/ps1/rendering/Ps1DepthBucket.hpp"
#include "platform/ps1/rendering/Ps1PacketBudget.hpp"

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
}

void TestDepthBuckets() {
    std::uint16_t bucket = 999;
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

}  // namespace

int main() {
    TestPacketBudget();
    TestDepthBuckets();
    TestCubeMesh();
    return 0;
}
