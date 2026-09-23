#include "platform/ps1/rendering/Ps1AuthoredUiGeometry.hpp"
#include "platform/ps1/rendering/Ps1VramAllocator.hpp"

#include <cassert>
#include <cmath>

using namespace helengine::ps1;

int main() {
    Ps1VramAllocator allocator;
    std::uint16_t textureX = 0;
    std::uint16_t textureY = 0;
    std::uint16_t paletteX = 0;
    std::uint16_t paletteY = 0;
    assert(allocator.TryAllocate(14, 27, textureX, textureY));
    assert(textureX == 320 && textureY == 0);
    assert(allocator.TryAllocateAligned(256, 1, 16, paletteX, paletteY));
    assert(paletteX == 336 && paletteY == 0);
    assert(paletteX >= textureX + 14 && paletteX % 16 == 0);
    assert(paletteX / 16 == 21);

    Ps1UiVertex quad[4]{};
    const Ps1ScreenRect full{0, 0, 320, 240};
    assert(TryBuildSpriteQuad({20.0f, 30.0f}, {26.0f, 27.0f}, {1.0f, 1.0f}, 0.0f,
                              {0.0f, 0.0f, 1.0f, 1.0f}, {26.0f, 27.0f}, full, quad));
    assert(quad[0].x == 20 && quad[0].y == 30);
    assert(quad[1].x == 46 && quad[1].y == 30);
    assert(quad[2].x == 20 && quad[2].y == 57);
    assert(quad[0].u == 0 && quad[0].v == 0);
    assert(quad[1].u == 25 && quad[2].v == 26);
    assert(TryBuildSpriteQuad({0.0f, 0.0f}, {26.0f, 27.0f}, {1.0f, 1.0f}, 0.0f,
                              {0.25f, 0.25f, 0.5f, 0.5f}, {26.0f, 27.0f}, full, quad));
    assert(quad[0].u == 6 && quad[0].v == 7 && quad[1].u == 19 && quad[2].v == 20);

    const Ps1ScreenRect offset{160, 120, 160, 120};
    assert(TryBuildSpriteQuad({1.0f, 2.0f}, {2.0f, 2.0f}, {1.0f, 1.0f}, 0.0f,
                              {0.0f, 0.0f, 1.0f, 1.0f}, {2.0f, 2.0f}, offset, quad));
    assert(quad[0].x == 161 && quad[0].y == 122);
    assert(!TryBuildSpriteQuad({0.0f, 0.0f}, {0.0f, 2.0f}, {1.0f, 1.0f}, 0.0f,
                               {0.0f, 0.0f, 1.0f, 1.0f}, {2.0f, 2.0f}, full, quad));

    assert(TryBuildSpriteQuad({10.0f, 10.0f}, {2.0f, 2.0f}, {1.0f, 1.0f}, 1.57079632679f,
                              {0.0f, 0.0f, 1.0f, 1.0f}, {2.0f, 2.0f}, full, quad));
    assert(std::abs(quad[0].x - 12) <= 1 && std::abs(quad[0].y - 10) <= 1);

    Ps1UiTriangle triangles[64]{};
    std::uint16_t count = 0;
    std::uint16_t fillCount = 0;
    assert(TryBuildRoundedRectTriangles({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 0.0f, 0.0f, 0x0f,
                                        full, triangles, 64, count, fillCount));
    assert(count == 2);
    assert(fillCount == count);
    assert(triangles[0].a.x == 10 && triangles[0].a.y == 10);
    assert(TryBuildRoundedRectTriangles({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 100.0f, 0.0f, 0x0f,
                                        full, triangles, 64, count, fillCount));
    assert(count > 2);
    assert(TryBuildRoundedRectTriangles({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 4.0f, 0.0f, 0x01,
                                        full, triangles, 64, count, fillCount));
    assert(count > 2);
    assert(TryBuildRoundedRectTriangles({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 6.0f, 2.0f, 0x0f,
                                        full, triangles, 64, count, fillCount));
    assert(count == 46 && fillCount == 14);
    assert(TryBuildRoundedRectTriangles({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 6.0f, 0.0f, 0x0f,
                                        full, triangles, 64, count, fillCount));
    assert(count == 14 && triangles[0].a.x == 10 && triangles[0].a.y == 16);
    assert(triangles[0].b.x == 11 && triangles[0].b.y == 13);
    assert(triangles[0].c.x == 13 && triangles[0].c.y == 11);
    assert(!TryBuildRoundedRectTriangles({10.0f, 10.0f}, {0.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 0.0f, 0.0f, 0x0f,
                                         full, triangles, 64, count, fillCount));

    std::uint16_t first = 0;
    std::uint16_t second = 0;
    assert(TryMapRenderOrder(1, first));
    assert(TryMapRenderOrder(2, second));
    assert(first > second);
    bool semiTransparent = true;
    assert(TryMapAlphaToBlend(0, semiTransparent) && !semiTransparent);
    assert(TryMapAlphaToBlend(1, semiTransparent) && semiTransparent);
    assert(TryMapAlphaToBlend(128, semiTransparent) && semiTransparent);
    assert(TryMapAlphaToBlend(254, semiTransparent) && semiTransparent);
    assert(TryMapAlphaToBlend(255, semiTransparent) && !semiTransparent);
    return 0;
}
