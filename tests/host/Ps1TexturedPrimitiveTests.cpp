#include "platform/ps1/rendering/Ps1TexturedPrimitive.hpp"
#include <cassert>
#include <cstring>
#include <limits>
#include <cstdio>
using namespace helengine::ps1;
int main() {
    const psyqo::Vertex points[3] = {{{.x = 1, .y = 2}}, {{.x = 3, .y = 4}}, {{.x = 5, .y = 6}}};
    const Ps1MeshUv uv[3] = {{0, 0}, {1, 0}, {0, 1}};
    const psyqo::Color colors[3] = {{{.r = 255, .g = 255, .b = 255}}, {{.r = 128, .g = 64, .b = 0}}, {{.r = 0, .g = 255, .b = 1}}};
    for (unsigned ppw : {1u, 2u, 4u}) {
        Ps1MeshTexture texture{64, 64, 64, 256, 32, 480, static_cast<std::uint16_t>(ppw)};
        psyqo::Prim::GouraudTexturedTriangle primitive;
        assert(BuildPs1TexturedTriangle(primitive, points, uv, colors, texture));
        std::uint32_t words[9]; std::memcpy(words, &primitive, sizeof(words));
        assert(words[0] == 0x34808080); assert((words[3] & 0xffffff) == 0x002040);
        assert((words[6] & 0xffffff) == 0x018000);
        assert((words[2] & 0xffff) == 0 && (words[5] & 0xffff) == 63 && (words[8] & 0xffff) == (63u << 8));
        const unsigned depth = ppw == 4 ? 0 : ppw == 2 ? 1 : 2;
        assert((words[5] >> 16) == (1u | 16u | (depth << 7)));
        assert((words[2] >> 16) == (2u | (480u << 6)));
        assert((words[1] & 0xffff) == 1 && (words[7] & 0xffff) == 5);
    }
    Ps1MeshTexture full8{256, 256, 128, 0, 0, 480, 2};
    std::uint8_t u, v; assert(TryBuildPs1MeshUv({1, 1}, full8, u, v) && u == 255 && v == 255);
    full8.VramX = 129; assert(!IsPs1MeshTextureValid(full8));
    Ps1MeshTexture full16{256, 256, 64, 256, 0, 0, 1}; assert(IsPs1MeshTextureValid(full16));
    full16.VramY = 257; assert(!IsPs1MeshTextureValid(full16));
    assert(!IsPs1MeshTextureValid({25, 1, 1000, 0, 0, 0, 1}));
    assert(!IsPs1MeshTextureValid({16, 1, 0, 0, 1008, 480, 2}));
    assert(!TryBuildPs1MeshUv({1.1f, 0}, {16, 16, 0, 0, 0, 480, 2}, u, v));
    assert(!TryBuildPs1MeshUv({-0.1f, 0}, full8, u, v));
    assert(!TryBuildPs1MeshUv({std::numeric_limits<float>::quiet_NaN(), 0}, full16, u, v));
    assert(Ps1TextureModulation(255) == 128 && Ps1TextureModulation(0) == 0);
    std::puts("PS1_TEXTURED_PRIMITIVE_PASS depths=3 packet_goldens=7 uv_bounds=5 modulation=2");
}
