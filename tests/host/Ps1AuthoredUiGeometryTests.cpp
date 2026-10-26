#include "platform/ps1/rendering/Ps1AuthoredUiGeometry.hpp"
#include "platform/ps1/rendering/Ps1SpriteGeometryCache.hpp"
#include "platform/ps1/rendering/Ps1VramAllocator.hpp"
#include "platform/ps1/rendering/Ps1IndexedTextureUpload.hpp"
#include "platform/ps1/rendering/Ps1TextLayout.hpp"

#include <cassert>
#include <cmath>

using namespace helengine::ps1;

static void TestTextRebuildAfterScrollAndClipChanges() {
    const auto lookup = [](char, Ps1TextGlyph& glyph) {
        glyph = {10, 10, 0, 10, 40, 50, 20, 20, true};
        return true;
    };
    Ps1UiVertex finalQuad[4]{};
    const auto rebuild = [&](float y, Ps1ScreenRect clip) {
        unsigned visible = 0;
        assert(LayoutPs1Text("A", 1, 10, y, 10, 1, 1, 10, Ps1TextAlignment::Left, lookup,
            [&](char, const Ps1TextGlyph& glyph, float x, float py) {
                assert(TryBuildGlyphQuadTexels({x, py}, {glyph.width, glyph.height},
                    {glyph.sourceX, glyph.sourceY, glyph.sourceWidth, glyph.sourceHeight},
                    256, 256, {0, 0, 320, 240}, finalQuad));
                if (ClipPs1GlyphQuad(finalQuad, clip)) ++visible;
                return true;
            }));
        return visible;
    };
    // Rebuild from font metrics after cropping, leaving the clip, and returning.
    // A retained cropped quad cannot restore the missing source texels.
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        assert(rebuild(20, {12, 22, 6, 6}) == 1);
        assert(finalQuad[0].y == 22 && finalQuad[0].v == 54);
        assert(finalQuad[3].y == 28 && finalQuad[3].v == 66);
        assert(rebuild(0, {12, 22, 6, 6}) == 0);
        assert(rebuild(24, {12, 22, 6, 6}) == 1);
        assert(finalQuad[0].y == 24 && finalQuad[0].v == 50);
        assert(finalQuad[3].y == 28 && finalQuad[3].v == 58);
        assert(rebuild(24, {12, 22, 6, 20}) == 1);
        assert(finalQuad[0].y == 24 && finalQuad[0].v == 50);
        assert(finalQuad[3].y == 34 && finalQuad[3].v == 70);
        assert(finalQuad[0].x == 12 && finalQuad[0].u == 44);
        assert(finalQuad[3].x == 18 && finalQuad[3].u == 56);
    }
}

int main() {
    // The final physical badge ignores the preceding camera's partial viewport and clip.
    {
        const Ps1ScreenRect partial{160, 120, 160, 120};
        const Ps1ScreenRect physical{0, 0, 320, 240};
        Ps1UiVertex sceneGlyph[4]{}, badgeGlyph[4]{};
        assert(TryBuildGlyphQuad({6, 6}, {4, 8}, {0, 0, 0.125f, 0.25f}, {128, 128}, partial, sceneGlyph));
        assert(sceneGlyph[0].x == 166 && sceneGlyph[0].y == 126);
        assert(TryBuildGlyphQuad({6, 6}, {4, 8}, {0, 0, 0.125f, 0.25f}, {128, 128}, physical, badgeGlyph));
        assert(badgeGlyph[0].x == 6 && badgeGlyph[0].y == 6);
        assert(badgeGlyph[2].y - badgeGlyph[0].y == 8);
        assert(!ClipPs1GlyphQuad(badgeGlyph, partial));
        assert(TryBuildGlyphQuad({6, 6}, {4, 8}, {0, 0, 0.125f, 0.25f}, {128, 128}, physical, badgeGlyph));
        assert(ClipPs1GlyphQuad(badgeGlyph, physical));
        Ps1UiTriangle background[2]{};
        std::uint16_t count = 0, fillCount = 0;
        assert(TryBuildRectTriangles({4, 4}, {30, 12}, {1, 1}, 0, 0, physical, background, 2, count, fillCount));
        assert(count == 2 && fillCount == 2);
        assert(background[0].a.x == 4 && background[0].a.y == 4);
    }
    TestTextRebuildAfterScrollAndClipChanges();
    // A mirrored, sheared quad keeps UVs on authored corners and the viewport offset.
    Ps1UiPoint affineCorners[4]{{30,20},{10,25},{38,60},{18,65}};
    Ps1UiVertex affineVertices[4]{};
    assert(TryBuildSpriteQuadFromCorners(affineCorners, {0,0,1,1}, {64,64}, {5,7,320,240}, affineVertices));
    assert(affineVertices[0].x == 35 && affineVertices[0].y == 27);
    assert(affineVertices[1].x == 15 && affineVertices[1].y == 32);
    assert(affineVertices[2].x == 43 && affineVertices[2].y == 67);
    assert(affineVertices[3].x == 23 && affineVertices[3].y == 72);
    assert(affineVertices[0].u == 0 && affineVertices[1].u == 63);
    assert(affineVertices[2].u == 0 && affineVertices[2].v == 63);
    Ps1SpriteGeometryCache affineCache;
    Ps1SpriteGeometryKey affineKey{
        nullptr, nullptr, {-20.0f, 5.0f, 8.0f, 40.0f, 30.0f, 20.0f}, {0.0f, 0.0f}, 0.0f,
        1, 1, {0.0f, 0.0f, 1.0f, 1.0f}, 64, 64, {5, 7, 320, 240}
    };
    affineKey.Drawable = &affineKey;
    affineCache.Remember(affineKey, affineVertices);
    assert(affineCache.TryGet(affineKey, affineVertices));
    affineKey.Affine[1] += 1.0f;
    assert(!affineCache.TryGet(affineKey, affineVertices));
    // Three-pixel rows start on alternating nibble boundaries in the shared payload.
    const std::uint8_t indices[] = {0x21, 0x43, 0x65};
    std::uint8_t firstRow[2]{}, secondRow[2]{};
    PackPs1Indexed4UploadRow(indices, 0, 3, firstRow);
    PackPs1Indexed4UploadRow(indices, 3, 3, secondRow);
    assert(firstRow[0] == 0x21 && firstRow[1] == 0x03);
    assert(secondRow[0] == 0x54 && secondRow[1] == 0x06);
    // The rotating authored backdrop previously crossed the GPU's vertical edge limit.
    unsigned oversizedFrames = 0;
    for (unsigned degrees = 0; degrees < 360; ++degrees) {
        const float angle = degrees * 3.14159265358979323846f / 180.0f;
        const float c = std::cos(angle), s = std::sin(angle);
        Ps1UiVertex backdrop[4]{}, safe[16][4]{};
        std::uint16_t count = 0;
        assert(TryBuildSpriteQuad({160.0f - 192.0f*c + 256.0f*s, 120.0f - 192.0f*s - 256.0f*c},
            {384.0f, 512.0f}, {1.0f, 1.0f}, angle, {0,0,1,1}, {256,256}, {0,0,320,240}, backdrop));
        assert(TrySplitGpuSafeSpriteQuad(backdrop, safe, count));
        if (count > 1) ++oversizedFrames;
        assert(count == 1 || count == 4);
        for (unsigned q = 0; q < count; ++q) for (unsigned a = 0; a < 4; ++a) for (unsigned b = a+1; b < 4; ++b) {
            assert(std::abs(safe[q][a].x-safe[q][b].x) <= 1023);
            assert(std::abs(safe[q][a].y-safe[q][b].y) <= 511);
        }
        assert(safe[0][0].x == backdrop[0].x && safe[0][0].y == backdrop[0].y);
        assert(safe[count-1][3].x == backdrop[3].x && safe[count-1][3].y == backdrop[3].y);
        assert(safe[0][0].u == 0 && safe[count-1][3].u == 255);
        if (count == 4) {
            assert(safe[0][1].x == safe[1][0].x && safe[0][1].y == safe[1][0].y);
            assert(safe[0][1].u == safe[1][0].u && safe[0][1].v == safe[1][0].v);
            assert(safe[0][2].x == safe[2][0].x && safe[0][2].v == safe[2][0].v);
        }
    }
    assert(oversizedFrames > 0 && oversizedFrames < 360);
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
    assert(pageAllocator.TryAllocateAligned(16, 1, 16, x, y) && x % 16 == 0);

    Ps1VramAllocator fontAtlasAllocator;
    // Direct-color pages reserve only their 16-bit words, without a CLUT.
    Ps1VramAllocator directColorAllocator;
    assert(directColorAllocator.TryAllocateWithinHorizontalPage(256, 256, 1, textureX, textureY));
    assert(textureX == 320 && textureY == 0);
    assert(directColorAllocator.UsedWords() == 256u * 256u && directColorAllocator.LiveCount() == 1);
    assert(!directColorAllocator.TryAllocateWithinHorizontalPage(257, 1, 1, x, y));
    assert(!directColorAllocator.TryAllocateWithinHorizontalPage(1, 257, 1, x, y));
    assert(directColorAllocator.Release(textureX, textureY, 256, 256));
    assert(directColorAllocator.UsedWords() == 0 && directColorAllocator.LiveCount() == 0);

    assert(fontAtlasAllocator.TryAllocateWithinHorizontalPage(16, 64, x, y));
    assert(fontAtlasAllocator.TryAllocateWithinHorizontalPage(16, 64, x, y));
    assert(fontAtlasAllocator.TryAllocateWithinHorizontalPage(64, 256, x, y));
    assert(x % 64 == 0 && y % 256 == 0);

    // 256 Indexed8 texels occupy 128 words, while TPAGE bases remain 64-word aligned.
    Ps1VramAllocator indexed8Allocator;
    assert(indexed8Allocator.TryAllocateWithinHorizontalPage(128, 256, 2, textureX, textureY));
    assert(textureX == 320 && textureY == 0 && textureX % 64 == 0);
    assert(textureX % 64 + 128 <= 128 && textureY % 256 + 256 <= 256);
    assert(indexed8Allocator.TryAllocateAligned(256, 1, 16, paletteX, paletteY));
    assert(indexed8Allocator.TryAllocateWithinHorizontalPage(64, 256, x, y));
    const auto neighbor = indexed8Allocator.GetRegion(2);
    for (unsigned i = 0; i < Ps1VramAllocator::Capacity; ++i) {
        const auto& a = indexed8Allocator.GetRegion(i);
        if (!a.width) continue;
        assert(a.x >= Ps1VramAllocator::FirstFreeColumn && a.x + a.width <= Ps1VramAllocator::VramWidth);
        assert(a.y + a.height <= Ps1VramAllocator::VramHeight);
        for (unsigned j = i + 1; j < Ps1VramAllocator::Capacity; ++j) {
            const auto& b = indexed8Allocator.GetRegion(j);
            if (b.width) assert(a.x + a.width <= b.x || b.x + b.width <= a.x ||
                                a.y + a.height <= b.y || b.y + b.height <= a.y);
        }
    }
    assert(indexed8Allocator.UsedWords() == 128u * 256u + 256u + 64u * 256u);
    assert(indexed8Allocator.Release(textureX, textureY, 128, 256));
    assert(indexed8Allocator.Release(paletteX, paletteY, 256, 1));
    assert(indexed8Allocator.TryAllocateWithinHorizontalPage(128, 256, 2, textureX, textureY));
    assert(textureX == 320 && textureY == 0);
    assert(indexed8Allocator.GetRegion(2).x == neighbor.x && indexed8Allocator.GetRegion(2).y == neighbor.y);
    const auto beforeInvalid = indexed8Allocator.UsedWords();
    assert(!indexed8Allocator.TryAllocateWithinHorizontalPage(129, 1, 2, x, y));
    assert(!indexed8Allocator.TryAllocateWithinHorizontalPage(65, 1, 4, x, y));
    assert(!indexed8Allocator.TryAllocateWithinHorizontalPage(65, 1, x, y));
    assert(!indexed8Allocator.TryAllocateWithinHorizontalPage(1, 257, 2, x, y));
    assert(!indexed8Allocator.TryAllocateWithinHorizontalPage(1, 1, 3, x, y));
    assert(indexed8Allocator.UsedWords() == beforeInvalid);

    // A partial 8bpp texture can cross the 64-word subdivision if U stays below 256.
    Ps1VramAllocator exactPageEdge;
    assert(exactPageEdge.TryAllocate(28, 256, x, y));
    assert(exactPageEdge.TryAllocateWithinHorizontalPage(100, 256, 2, x, y));
    assert(x == 348 && y == 0 && (x % 64 + 100) * 2 == 256);
    Ps1VramAllocator beyondPageEdge;
    assert(beyondPageEdge.TryAllocate(29, 256, x, y));
    assert(beyondPageEdge.TryAllocateWithinHorizontalPage(100, 256, 2, x, y));
    assert(x == 384 && y == 0 && x % 64 + 100 <= 128);
    Ps1VramAllocator verticalPageEdge;
    assert(verticalPageEdge.TryAllocate(704, 1, x, y));
    assert(verticalPageEdge.TryAllocateWithinHorizontalPage(128, 256, 2, x, y));
    assert(x == 320 && y == 256);

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
    assert(quad[0].x == 10 && quad[0].y == 10);
    assert(quad[1].x == 10 && quad[1].y == 12);
    assert(quad[2].x == 8 && quad[2].y == 10);
    // Parent center (640,360), child local (-768,-768), rotated 90 degrees.
    assert(TryBuildSpriteQuad({1408.0f, -408.0f}, {1536.0f, 1536.0f}, {1.0f, 1.0f}, 1.57079632679f,
                              {0.0f, 0.0f, 1.0f, 1.0f}, {256.0f, 256.0f}, full, quad));
    assert((quad[0].x + quad[3].x) / 2 == 640);
    assert((quad[0].y + quad[3].y) / 2 == 360);
    assert(TryBuildGlyphQuadTexels({10, 20}, {10, 10}, {40, 50, 20, 20}, 256, 256, full, quad));
    assert(ClipPs1GlyphQuad(quad, {12, 22, 6, 6}));
    assert(quad[0].x == 12 && quad[0].y == 22 && quad[0].u == 44 && quad[0].v == 54);
    assert(quad[3].x == 18 && quad[3].y == 28 && quad[3].u == 56 && quad[3].v == 66);
    assert(!ClipPs1GlyphQuad(quad, {0, 0, 5, 5}));

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
    assert(TryBuildRectTriangles({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 0.0f,
                                 full, triangles, 64, count, fillCount));
    assert(count == 2 && fillCount == 2);
    assert(triangles[0].a.x == 10 && triangles[0].a.y == 10);
    assert(triangles[0].b.x == 30 && triangles[0].b.y == 10);
    assert(triangles[0].c.x == 30 && triangles[0].c.y == 22);
    assert(triangles[1].c.x == 10 && triangles[1].c.y == 22);
    assert(TryBuildRectTriangles({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 2.0f,
                                 full, triangles, 64, count, fillCount));
    assert(count == 10 && fillCount == 2);
    assert(triangles[0].a.x == 12 && triangles[0].a.y == 12);
    assert(triangles[0].b.x == 28 && triangles[0].b.y == 12);
    assert(triangles[0].c.x == 28 && triangles[0].c.y == 20);
    assert(triangles[2].a.x == 10 && triangles[2].a.y == 10);
    assert(triangles[2].b.x == 30 && triangles[2].b.y == 10);
    assert(TryBuildRectTriangles({10.0f, 10.0f}, {20.0f, 12.0f}, {2.0f, 0.5f}, 0.0f, 0.0f,
                                 offset, triangles, 64, count, fillCount));
    assert(triangles[0].a.x == 170 && triangles[0].a.y == 130);
    assert(triangles[0].b.x == 210 && triangles[0].b.y == 130);
    assert(triangles[0].c.x == 210 && triangles[0].c.y == 136);
    assert(TryBuildRectTriangles({10.0f, 10.0f}, {20.0f, 12.0f}, {1.0f, 1.0f}, 1.57079632679f, 0.0f,
                                 full, triangles, 64, count, fillCount));
    assert(std::abs(triangles[0].a.x - 26) <= 1 && std::abs(triangles[0].a.y - 6) <= 1);
    assert(std::abs(triangles[0].b.x - 26) <= 1 && std::abs(triangles[0].b.y - 26) <= 1);
    assert(TryBuildRectTriangles({10.0f, 10.0f}, {0.0f, 12.0f}, {1.0f, 1.0f}, 0.0f, 0.0f,
                                 full, triangles, 64, count, fillCount));
    assert(count == 0 && fillCount == 0);
    // The persistent loading fill starts empty, then reaches one and two pixels with a
    // visible one-pixel border. Every intermediate progress value is valid geometry.
    for (unsigned progressWidth = 0; progressWidth <= 256; ++progressWidth) {
        assert(TryBuildRectTriangles({32.0f, 224.0f}, {static_cast<float>(progressWidth), 7.0f},
            {1.0f, 1.0f}, 0.0f, 1.0f, full, triangles, 64, count, fillCount));
        assert(count == (progressWidth == 0 ? 0 : progressWidth <= 2 ? 2 : 10));
        assert(fillCount == (progressWidth <= 2 ? 0 : 2));
        if (progressWidth == 1 || progressWidth == 2) {
            assert(triangles[0].a.x == 32 && triangles[0].a.y == 224);
            assert(triangles[0].b.x == 32 + progressWidth && triangles[0].c.y == 231);
            assert(triangles[1].a.x == triangles[0].a.x && triangles[1].c.x == 32);
        }
    }
    assert(TryBuildRectTriangles({10, 10}, {12, 1}, {1, 1}, 0, 1,
        full, triangles, 64, count, fillCount));
    assert(count == 2 && fillCount == 0);
    assert(TryBuildRectTriangles({10, 10}, {12, 0}, {1, 1}, 0, 1,
        full, triangles, 64, count, fillCount));
    assert(count == 0 && fillCount == 0);
    // Empty shapes still reject bad parameters rather than bypassing validation.
    assert(!TryBuildRectTriangles({10, 10}, {-1, 0}, {1, 1}, 0, 1,
        full, triangles, 64, count, fillCount));
    assert(!TryBuildRectTriangles({10, 10}, {0, 12}, {0, 1}, 0, 1,
        full, triangles, 64, count, fillCount));
    assert(!TryBuildRectTriangles({10, 10}, {0, 12}, {1, 1}, 0, -1,
        full, triangles, 64, count, fillCount));
    assert(!TryBuildRectTriangles({NAN, 10}, {0, 12}, {1, 1}, 0, 1,
        full, triangles, 64, count, fillCount));
    assert(!TryBuildRectTriangles({10, 10}, {0, 12}, {1, 1}, NAN, 1,
        full, triangles, 64, count, fillCount));
    assert(!TryBuildRectTriangles({10, 10}, {0, 12}, {1, 1}, 0, 1,
        {0, 0, 0, 240}, triangles, 64, count, fillCount));
    assert(!TryBuildRectTriangles({10, 10}, {1, 12}, {1, 1}, 0, 1,
        full, triangles, 1, count, fillCount));
    assert(count == 0 && fillCount == 0);
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

    Ps1SpriteGeometryCache spriteCache;
    const Ps1SpriteGeometryKey spriteKey{
        reinterpret_cast<const void*>(1), reinterpret_cast<const void*>(2),
        {1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f}, {0.5f, 0.5f}, 0.0f,
        26, 27, {0.0f, 0.0f, 1.0f, 1.0f}, 14, 27, full
    };
    Ps1UiVertex spriteQuad[4]{};
    assert(TryBuildSpriteQuad({10.0f, 20.0f}, {26.0f, 27.0f}, {1.0f, 1.0f}, 0.0f,
                              spriteKey.Source, {14.0f, 27.0f}, full, spriteQuad));
    Ps1UiVertex reusedSprite[4]{};
    assert(!spriteCache.TryGet(spriteKey, reusedSprite));
    spriteCache.Remember(spriteKey, spriteQuad);
    assert(spriteCache.TryGet(spriteKey, reusedSprite));
    for (unsigned i = 0; i < 4; ++i) {
        assert(reusedSprite[i].x == spriteQuad[i].x && reusedSprite[i].y == spriteQuad[i].y);
        assert(reusedSprite[i].u == spriteQuad[i].u && reusedSprite[i].v == spriteQuad[i].v);
    }
    Ps1SpriteGeometryKey changed = spriteKey;
    // Every affine coefficient participates, including ancestor shear and mirrored axes.
    for (unsigned index = 0; index < 6; ++index) {
        changed = spriteKey;
        changed.Affine[index] += 0.25f;
        assert(!spriteCache.TryGet(changed, reusedSprite));
    }
    for (unsigned index = 0; index < 2; ++index) {
        changed = spriteKey;
        changed.Pivot[index] += 0.25f;
        assert(!spriteCache.TryGet(changed, reusedSprite));
    }
    changed = spriteKey;
    changed.RotationRadians = 0.5f;
    assert(!spriteCache.TryGet(changed, reusedSprite));
    changed = spriteKey;
    changed.Source.x = 0.25f;
    assert(!spriteCache.TryGet(changed, reusedSprite));
    changed = spriteKey;
    changed.SizeX = 25;
    assert(!spriteCache.TryGet(changed, reusedSprite));
    changed = spriteKey;
    changed.SizeY = 28;
    assert(!spriteCache.TryGet(changed, reusedSprite));
    changed = spriteKey;
    changed.Viewport.x = 160;
    assert(!spriteCache.TryGet(changed, reusedSprite));
    changed = spriteKey;
    changed.TextureWidth = 28;
    assert(!spriteCache.TryGet(changed, reusedSprite));
    assert(spriteCache.TryGet(spriteKey, reusedSprite));
    spriteCache.Remember(changed, spriteQuad);
    assert(!spriteCache.TryGet(spriteKey, reusedSprite));
    assert(spriteCache.TryGet(changed, reusedSprite));
    return 0;
}
