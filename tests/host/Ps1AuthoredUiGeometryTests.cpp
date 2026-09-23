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

    Ps1VramAllocator pageAllocator;
    std::uint16_t x = 0, y = 0;
    assert(pageAllocator.TryAllocate(14, 27, x, y) && x == 320);
    assert(pageAllocator.TryAllocateAligned(256, 1, 16, x, y) && x == 336);
    assert(pageAllocator.TryAllocate(14, 27, x, y) && x == 592);
    assert(pageAllocator.TryAllocateAligned(256, 1, 16, x, y) && x == 608);
    assert(pageAllocator.TryAllocateWithinHorizontalPage(64, 128, x, y));
    assert(x == 896 && y == 0 && x % 64 == 0);
    assert(pageAllocator.TryAllocateAligned(16, 1, 16, x, y) && x == 960);

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

    // The font's L occupies texels (41..45, 33..41). At 1:1 size, each
    // destination pixel must sample its matching texel, including the last row
    // and column. The GPU starts UV interpolation at a half-texel and truncates.
    assert(TryBuildGlyphQuad({10.0f, 20.0f}, {5.0f, 9.0f},
                             {41.0f / 256.0f, 33.0f / 128.0f, 5.0f / 256.0f, 9.0f / 128.0f},
                             {256.0f, 128.0f}, full, quad));
    assert(quad[0].x == 10 && quad[0].y == 20 && quad[3].x == 15 && quad[3].y == 29);
    for (int pixel = 0; pixel < 5; ++pixel) {
        const int sampledU = static_cast<int>(quad[0].u + 0.5f +
            (quad[1].u - quad[0].u) * static_cast<float>(pixel) / 5.0f);
        assert(sampledU == 41 + pixel);
    }
    for (int pixel = 0; pixel < 9; ++pixel) {
        const int sampledV = static_cast<int>(quad[0].v + 0.5f +
            (quad[2].v - quad[0].v) * static_cast<float>(pixel) / 9.0f);
        assert(sampledV == 33 + pixel);
    }

    // Cached texel bounds must produce the same destination and UV vertices as normalized metrics.
    Ps1GlyphTexelRect cachedGlyph{};
    assert(TryResolveGlyphTexels({41.0f / 256.0f, 33.0f / 128.0f, 5.0f / 256.0f, 9.0f / 128.0f},
                                 {256.0f, 128.0f}, cachedGlyph));
    Ps1UiVertex cachedQuad[4]{};
    assert(TryBuildGlyphQuadTexels({10.0f, 20.0f}, {5.0f, 9.0f}, cachedGlyph, 256, 128, full, cachedQuad));
    for (unsigned i = 0; i < 4; ++i) {
        assert(cachedQuad[i].x == quad[i].x && cachedQuad[i].y == quad[i].y);
        assert(cachedQuad[i].u == quad[i].u && cachedQuad[i].v == quad[i].v);
    }

    // UV 256 wraps to zero on the GPU; a one-texel edge glyph must stay on 255.
    assert(TryBuildGlyphQuad({0.0f, 0.0f}, {1.0f, 1.0f},
                             {255.0f / 256.0f, 127.0f / 128.0f, 1.0f / 256.0f, 1.0f / 128.0f},
                             {256.0f, 128.0f}, full, quad));
    assert(quad[0].u == 255 && quad[1].u == 255 && quad[0].v == 127 && quad[2].v == 128);
    assert(TryResolveGlyphTexels({255.0f / 256.0f, 127.0f / 128.0f, 1.0f / 256.0f, 1.0f / 128.0f},
                                 {256.0f, 128.0f}, cachedGlyph));
    assert(TryBuildGlyphQuadTexels({0.0f, 0.0f}, {1.0f, 1.0f}, cachedGlyph, 256, 128, full, cachedQuad));
    for (unsigned i = 0; i < 4; ++i) {
        assert(cachedQuad[i].x == quad[i].x && cachedQuad[i].y == quad[i].y);
        assert(cachedQuad[i].u == quad[i].u && cachedQuad[i].v == quad[i].v);
    }
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

    Ps1RoundedRectGeometryCache shapeCache;
    const Ps1UiTriangle* cachedTriangles = nullptr;
    bool reused = true;
    assert(shapeCache.TryGet({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 6.0f, 2.0f,
                             0x0f, full, cachedTriangles, count, fillCount, reused));
    assert(!reused && count == 46 && fillCount == 14);
    const Ps1UiTriangle* firstShape = cachedTriangles;
    assert(shapeCache.TryGet({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 6.0f, 2.0f,
                             0x0f, full, cachedTriangles, count, fillCount, reused));
    assert(reused && cachedTriangles == firstShape);
    assert(shapeCache.TryGet({12.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 6.0f, 2.0f,
                             0x0f, full, cachedTriangles, count, fillCount, reused));
    assert(!reused && cachedTriangles[0].a.x == firstShape[0].a.x + 2);
    assert(shapeCache.TryGet({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 6.0f, 2.0f,
                             0x0f, offset, cachedTriangles, count, fillCount, reused));
    assert(!reused && cachedTriangles[0].a.x == firstShape[0].a.x + offset.x);

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
