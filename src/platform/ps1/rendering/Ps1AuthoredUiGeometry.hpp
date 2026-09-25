#pragma once

#include <cstdint>
#include "platform/ps1/Ps1Math.hpp"

namespace helengine::ps1 {

struct Ps1ScreenRect {
    std::int16_t x;
    std::int16_t y;
    std::int16_t width;
    std::int16_t height;
};

struct Ps1UiPoint {
    float x;
    float y;
};

struct Ps1UiSize {
    float width;
    float height;
};

struct Ps1UiRectF {
    float x;
    float y;
    float width;
    float height;
};

struct Ps1UiVertex {
    std::int16_t x;
    std::int16_t y;
    std::uint8_t u;
    std::uint8_t v;
};

struct Ps1UiTriangle {
    Ps1UiVertex a;
    Ps1UiVertex b;
    Ps1UiVertex c;
};

namespace ui_geometry_detail {

inline bool Finite(float value) {
    return value == value && value <= 3.402823466e+38F && value >= -3.402823466e+38F;
}

inline long Round(float value) { return static_cast<long>(value >= 0.0f ? value + 0.5f : value - 0.5f); }

inline float Sin(float value) {
    return static_cast<float>(::sin(static_cast<double>(value)));
}

inline float Cos(float value) {
    return static_cast<float>(::cos(static_cast<double>(value)));
}

inline float Atan2(float y, float x) {
    return static_cast<float>(::atan2(static_cast<double>(y), static_cast<double>(x)));
}

inline bool ToScreen(float x, float y, const Ps1ScreenRect& viewport, std::int16_t& outX, std::int16_t& outY) {
    x += static_cast<float>(viewport.x);
    y += static_cast<float>(viewport.y);
    if (!Finite(x) || !Finite(y) || x < -32768.0f || x > 32767.0f || y < -32768.0f || y > 32767.0f) return false;
    outX = static_cast<std::int16_t>(Round(x));
    outY = static_cast<std::int16_t>(Round(y));
    return true;
}

inline bool ValidViewport(const Ps1ScreenRect& viewport) {
    return viewport.width > 0 && viewport.height > 0;
}

}  // namespace ui_geometry_detail

/** Builds a rotated sprite quad. Position and size are already in logical pixels; viewport only offsets them. */
inline bool TryBuildSpriteQuad(Ps1UiPoint position, Ps1UiSize size, Ps1UiPoint scale, float rotationRadians,
                               Ps1UiRectF sourceRect, Ps1UiSize textureSize, Ps1ScreenRect viewport,
                               Ps1UiVertex (&vertices)[4]) {
    using namespace ui_geometry_detail;
    if (!ValidViewport(viewport) || !Finite(position.x) || !Finite(position.y) || !Finite(size.width) ||
        !Finite(size.height) || !Finite(scale.x) || !Finite(scale.y) || !Finite(rotationRadians) ||
        !Finite(sourceRect.x) || !Finite(sourceRect.y) || !Finite(sourceRect.width) || !Finite(sourceRect.height) ||
        !Finite(textureSize.width) || !Finite(textureSize.height) || size.width <= 0.0f || size.height <= 0.0f ||
        scale.x <= 0.0f || scale.y <= 0.0f || textureSize.width <= 0.0f || textureSize.height <= 0.0f ||
        sourceRect.x < 0.0f || sourceRect.y < 0.0f || sourceRect.width <= 0.0f || sourceRect.height <= 0.0f ||
        sourceRect.x + sourceRect.width > 1.00001f || sourceRect.y + sourceRect.height > 1.00001f) return false;

    const float width = size.width * scale.x;
    const float height = size.height * scale.y;
    const float centerX = position.x + width * 0.5f;
    const float centerY = position.y + height * 0.5f;
    const bool unrotated = rotationRadians == 0.0f;
    const float cosine = unrotated ? 1.0f : Cos(rotationRadians);
    const float sine = unrotated ? 0.0f : Sin(rotationRadians);
    const float localX[4] = {0.0f, width, 0.0f, width};
    const float localY[4] = {0.0f, 0.0f, height, height};
    const float sourceU[4] = {sourceRect.x, sourceRect.x + sourceRect.width, sourceRect.x,
                              sourceRect.x + sourceRect.width};
    const float sourceV[4] = {sourceRect.y, sourceRect.y, sourceRect.y + sourceRect.height,
                              sourceRect.y + sourceRect.height};
    Ps1UiVertex result[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        float x = position.x + localX[i];
        float y = position.y + localY[i];
        if (!unrotated) {
            const float dx = localX[i] - width * 0.5f;
            const float dy = localY[i] - height * 0.5f;
            x = centerX + dx * cosine - dy * sine;
            y = centerY + dx * sine + dy * cosine;
        }
        if (!ToScreen(x, y, viewport, result[i].x, result[i].y)) return false;
        const float u = sourceU[i] * (textureSize.width - 1.0f);
        const float v = sourceV[i] * (textureSize.height - 1.0f);
        if (u < 0.0f || u > 255.0f || v < 0.0f || v > 255.0f) return false;
        result[i].u = static_cast<std::uint8_t>(Round(u));
        result[i].v = static_cast<std::uint8_t>(Round(v));
    }
    for (unsigned i = 0; i < 4; ++i) vertices[i] = result[i];
    return true;
}

struct Ps1GlyphTexelRect {
    std::int16_t x;
    std::int16_t y;
    std::int16_t width;
    std::int16_t height;
};

/// Resolves normalized font metrics once so repeated glyphs do not redo the atlas arithmetic.
inline bool TryResolveGlyphTexels(Ps1UiRectF sourceRect, Ps1UiSize textureSize, Ps1GlyphTexelRect& texels) {
    using namespace ui_geometry_detail;
    if (!Finite(sourceRect.x) || !Finite(sourceRect.y) || !Finite(sourceRect.width) ||
        !Finite(sourceRect.height) || sourceRect.x < 0.0f || sourceRect.y < 0.0f ||
        sourceRect.width <= 0.0f || sourceRect.height <= 0.0f ||
        sourceRect.x + sourceRect.width > 1.00001f || sourceRect.y + sourceRect.height > 1.00001f ||
        !Finite(textureSize.width) || !Finite(textureSize.height) || textureSize.width <= 0.0f ||
        textureSize.height <= 0.0f || textureSize.width > 256.0f || textureSize.height > 256.0f) return false;
    const long sourceX = Round(sourceRect.x * textureSize.width);
    const long sourceY = Round(sourceRect.y * textureSize.height);
    const long sourceWidth = Round(sourceRect.width * textureSize.width);
    const long sourceHeight = Round(sourceRect.height * textureSize.height);
    if (sourceWidth <= 0 || sourceHeight <= 0 || sourceX < 0 || sourceY < 0 ||
        sourceX + sourceWidth > static_cast<long>(textureSize.width) ||
        sourceY + sourceHeight > static_cast<long>(textureSize.height)) return false;
    texels = {static_cast<std::int16_t>(sourceX), static_cast<std::int16_t>(sourceY),
              static_cast<std::int16_t>(sourceWidth), static_cast<std::int16_t>(sourceHeight)};
    return true;
}

/// Builds a glyph from already resolved atlas texels, including the exclusive far UV edge.
inline bool TryBuildGlyphQuadTexels(Ps1UiPoint position, Ps1UiSize size, Ps1GlyphTexelRect source,
                                     std::uint16_t textureWidth, std::uint16_t textureHeight,
                                     Ps1ScreenRect viewport, Ps1UiVertex (&vertices)[4]) {
    using namespace ui_geometry_detail;
    if (!ValidViewport(viewport) || !Finite(position.x) || !Finite(position.y) ||
        !Finite(size.width) || !Finite(size.height) || size.width <= 0.0f || size.height <= 0.0f ||
        textureWidth == 0 || textureHeight == 0 || textureWidth > 256 || textureHeight > 256 ||
        source.x < 0 || source.y < 0 || source.width <= 0 || source.height <= 0 ||
        source.x + source.width > textureWidth || source.y + source.height > textureHeight) return false;
    std::int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!ToScreen(position.x, position.y, viewport, x0, y0) ||
        !ToScreen(position.x + size.width, position.y + size.height, viewport, x1, y1)) return false;
    if (x1 <= x0) ++x1;
    if (y1 <= y0) ++y1;
    const auto u0 = static_cast<std::uint8_t>(source.x);
    const auto v0 = static_cast<std::uint8_t>(source.y);
    // Screen vertices mark the exclusive destination edge. UV 256 wraps to zero.
    const auto u1 = static_cast<std::uint8_t>(source.x + source.width < 256 ? source.x + source.width : 255);
    const auto v1 = static_cast<std::uint8_t>(source.y + source.height < 256 ? source.y + source.height : 255);
    vertices[0] = {x0, y0, u0, v0};
    vertices[1] = {x1, y0, u1, v0};
    vertices[2] = {x0, y1, u0, v1};
    vertices[3] = {x1, y1, u1, v1};
    return true;
}

/** Builds an unrotated glyph using exact atlas texel bounds and no trigonometry per character. */
inline bool TryBuildGlyphQuad(Ps1UiPoint position, Ps1UiSize size, Ps1UiRectF sourceRect,
                              Ps1UiSize textureSize, Ps1ScreenRect viewport, Ps1UiVertex (&vertices)[4]) {
    Ps1GlyphTexelRect texels{};
    return TryResolveGlyphTexels(sourceRect, textureSize, texels) &&
           TryBuildGlyphQuadTexels(position, size, texels, static_cast<std::uint16_t>(textureSize.width),
                                   static_cast<std::uint16_t>(textureSize.height), viewport, vertices);
}

/** Builds a square-cornered rectangle with an optional border, using at most ten triangles. */
inline bool TryBuildRectTriangles(Ps1UiPoint position, Ps1UiSize size, Ps1UiPoint scale,
                                 float rotationRadians, float borderThickness, Ps1ScreenRect viewport,
                                 Ps1UiTriangle* triangles, std::uint16_t capacity, std::uint16_t& count,
                                 std::uint16_t& fillCount) {
    using namespace ui_geometry_detail;
    count = 0;
    fillCount = 0;
    if (!ValidViewport(viewport) || triangles == nullptr || !Finite(position.x) || !Finite(position.y) ||
        !Finite(size.width) || !Finite(size.height) || !Finite(scale.x) || !Finite(scale.y) ||
        !Finite(rotationRadians) || !Finite(borderThickness) || size.width <= 0.0f || size.height <= 0.0f ||
        scale.x <= 0.0f || scale.y <= 0.0f || borderThickness < 0.0f) return false;
    const float width = size.width * scale.x;
    const float height = size.height * scale.y;
    if (!Finite(width) || !Finite(height) || width <= 0.0f || height <= 0.0f ||
        borderThickness * 2.0f >= width || borderThickness * 2.0f >= height) return false;

    const Ps1UiPoint outer[4] = {{position.x, position.y}, {position.x + width, position.y},
                                 {position.x + width, position.y + height}, {position.x, position.y + height}};
    const Ps1UiPoint inner[4] = {{position.x + borderThickness, position.y + borderThickness},
                                 {position.x + width - borderThickness, position.y + borderThickness},
                                 {position.x + width - borderThickness, position.y + height - borderThickness},
                                 {position.x + borderThickness, position.y + height - borderThickness}};
    const bool unrotated = rotationRadians == 0.0f;
    const float cosine = unrotated ? 1.0f : Cos(rotationRadians);
    const float sine = unrotated ? 0.0f : Sin(rotationRadians);
    const float centerX = position.x + width * 0.5f;
    const float centerY = position.y + height * 0.5f;
    const auto transform = [&](Ps1UiPoint point, Ps1UiVertex& output) {
        if (unrotated) return ToScreen(point.x, point.y, viewport, output.x, output.y);
        const float dx = point.x - centerX;
        const float dy = point.y - centerY;
        return ToScreen(centerX + dx * cosine - dy * sine, centerY + dx * sine + dy * cosine,
                        viewport, output.x, output.y);
    };
    Ps1UiVertex transformedOuter[4]{};
    Ps1UiVertex transformedInner[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        if (!transform(outer[i], transformedOuter[i]) ||
            !transform(inner[i], transformedInner[i])) return false;
    }

    const unsigned needed = borderThickness > 0.0f ? 10u : 2u;
    if (needed > capacity) return false;
    const Ps1UiVertex* fill = borderThickness > 0.0f ? transformedInner : transformedOuter;
    Ps1UiTriangle staged[10]{};
    staged[0] = {fill[0], fill[1], fill[2]};
    staged[1] = {fill[0], fill[2], fill[3]};
    fillCount = 2;
    unsigned stagedCount = 2;
    if (borderThickness > 0.0f) {
        for (unsigned i = 0; i < 4; ++i) {
            const unsigned next = (i + 1u) % 4u;
            staged[stagedCount++] = {transformedOuter[i], transformedOuter[next], transformedInner[next]};
            staged[stagedCount++] = {transformedOuter[i], transformedInner[next], transformedInner[i]};
        }
    }
    for (unsigned i = 0; i < stagedCount; ++i) triangles[i] = staged[i];
    count = static_cast<std::uint16_t>(stagedCount);
    return true;
}

/** Builds a convex fill fan and optional border ring; 0x01, 0x02, 0x04, 0x08 select TL, TR, BR, BL. */
inline bool TryBuildRoundedRectTriangles(Ps1UiPoint position, Ps1UiSize size, Ps1UiPoint scale,
                                         float rotationRadians, float radius, float borderThickness,
                                         std::uint8_t roundedCorners, Ps1ScreenRect viewport,
                                         Ps1UiTriangle* triangles, std::uint16_t capacity, std::uint16_t& count,
                                         std::uint16_t& fillCount) {
    using namespace ui_geometry_detail;
    count = 0;
    fillCount = 0;
    if (!ValidViewport(viewport) || triangles == nullptr || !Finite(position.x) || !Finite(position.y) ||
        !Finite(size.width) || !Finite(size.height) || !Finite(scale.x) || !Finite(scale.y) ||
        !Finite(rotationRadians) || !Finite(radius) || !Finite(borderThickness) || size.width <= 0.0f ||
        size.height <= 0.0f || scale.x <= 0.0f || scale.y <= 0.0f || radius < 0.0f || borderThickness < 0.0f) return false;
    const float width = size.width * scale.x;
    const float height = size.height * scale.y;
    const float border = borderThickness;
    if (!Finite(width) || !Finite(height) || border * 2.0f >= width || border * 2.0f >= height) return false;
    const float clampedRadius = radius < (width < height ? width : height) * 0.5f ? radius : (width < height ? width : height) * 0.5f;
    // Three chords per quarter-circle retain the rounded silhouette at 320x240 and keep the
    // scene's two bordered panels inside the fixed UI packet reservation.
    constexpr unsigned SegmentsPerCorner = 3;
    constexpr float QuarterCosine[SegmentsPerCorner + 1] = {1.0f, 0.86602540378f, 0.5f, 0.0f};
    constexpr float QuarterSine[SegmentsPerCorner + 1] = {0.0f, 0.5f, 0.86602540378f, 1.0f};
    const auto arcPoint = [&](unsigned corner, unsigned segment, float centerX, float centerY, float cornerRadius) {
        const float cosine = QuarterCosine[segment];
        const float sine = QuarterSine[segment];
        float x = 0.0f;
        float y = 0.0f;
        switch (corner) {
            case 0: x = -cosine; y = -sine; break;  // top-left: 180..270 degrees
            case 1: x = sine; y = -cosine; break;   // top-right: 270..360 degrees
            case 2: x = cosine; y = sine; break;     // bottom-right: 0..90 degrees
            default: x = -sine; y = cosine; break;   // bottom-left: 90..180 degrees
        }
        return Ps1UiPoint{centerX + cornerRadius * x, centerY + cornerRadius * y};
    };
    Ps1UiPoint outer[20]{};
    Ps1UiPoint inner[20]{};
    unsigned vertexCount = 0;
    for (unsigned corner = 0; corner < 4; ++corner) {
        const bool rounded = (roundedCorners & (1u << corner)) != 0 && clampedRadius > 0.0f;
        const float r = rounded ? clampedRadius : 0.0f;
        const float cx[4] = {position.x + r, position.x + width - r,
                             position.x + width - r, position.x + r};
        const float cy[4] = {position.y + r, position.y + r,
                             position.y + height - r, position.y + height - r};
        if (rounded) {
            for (unsigned segment = 0; segment <= SegmentsPerCorner; ++segment) {
                outer[vertexCount++] = arcPoint(corner, segment, cx[corner], cy[corner], r);
            }
        } else {
            const Ps1UiPoint corners[4] = {{position.x, position.y}, {position.x + width, position.y},
                                           {position.x + width, position.y + height}, {position.x, position.y + height}};
            outer[vertexCount++] = corners[corner];
        }
    }
    const float innerWidth = width - 2.0f * border;
    const float innerHeight = height - 2.0f * border;
    const float innerRadius = clampedRadius > border ? clampedRadius - border : 0.0f;
    unsigned innerCount = 0;
    for (unsigned corner = 0; corner < 4; ++corner) {
        const bool rounded = (roundedCorners & (1u << corner)) != 0 && clampedRadius > 0.0f;
        const float r = rounded ? innerRadius : 0.0f;
        const float cx[4] = {position.x + border + r, position.x + border + innerWidth - r,
                             position.x + border + innerWidth - r, position.x + border + r};
        const float cy[4] = {position.y + border + r, position.y + border + r,
                             position.y + border + innerHeight - r, position.y + border + innerHeight - r};
        if (rounded) {
            for (unsigned segment = 0; segment <= SegmentsPerCorner; ++segment) {
                inner[innerCount++] = arcPoint(corner, segment, cx[corner], cy[corner], r);
            }
        } else {
            const Ps1UiPoint corners[4] = {{position.x + border, position.y + border},
                                           {position.x + width - border, position.y + border},
                                           {position.x + width - border, position.y + height - border},
                                           {position.x + border, position.y + height - border}};
            inner[innerCount++] = corners[corner];
        }
    }
    if (innerCount != vertexCount) return false;
    const unsigned needed = border > 0.0f ? vertexCount * 2u + (vertexCount - 2u) : vertexCount - 2u;
    if (needed > capacity || needed > 65535u) return false;
    const bool unrotated = rotationRadians == 0.0f;
    const float cosine = unrotated ? 1.0f : Cos(rotationRadians);
    const float sine = unrotated ? 0.0f : Sin(rotationRadians);
    const float centerX = position.x + width * 0.5f;
    const float centerY = position.y + height * 0.5f;
    auto vertex = [&](Ps1UiPoint point, Ps1UiVertex& output) {
        if (unrotated) return ToScreen(point.x, point.y, viewport, output.x, output.y);
        const float dx = point.x - centerX;
        const float dy = point.y - centerY;
        return ToScreen(centerX + dx * cosine - dy * sine, centerY + dx * sine + dy * cosine,
                        viewport, output.x, output.y);
    };
    Ps1UiTriangle staged[46]{};
    unsigned stagedCount = 0;
    Ps1UiVertex transformedOuter[20]{};
    Ps1UiVertex transformedInner[20]{};
    for (unsigned i = 0; i < vertexCount; ++i) {
        if (!vertex(outer[i], transformedOuter[i]) || !vertex(inner[i], transformedInner[i])) return false;
    }
    for (unsigned i = 1; i + 1 < vertexCount; ++i) {
        const Ps1UiVertex& a = border > 0.0f ? transformedInner[0] : transformedOuter[0];
        const Ps1UiVertex& b = border > 0.0f ? transformedInner[i] : transformedOuter[i];
        const Ps1UiVertex& c = border > 0.0f ? transformedInner[i + 1] : transformedOuter[i + 1];
        staged[stagedCount++] = {a, b, c};
    }
    fillCount = static_cast<std::uint16_t>(stagedCount);
    if (border > 0.0f) {
        for (unsigned i = 0; i < vertexCount; ++i) {
            const unsigned next = (i + 1u) % vertexCount;
            staged[stagedCount++] = {transformedOuter[i], transformedOuter[next], transformedInner[next]};
            staged[stagedCount++] = {transformedOuter[i], transformedInner[next], transformedInner[i]};
        }
    }
    for (unsigned i = 0; i < stagedCount; ++i) triangles[i] = staged[i];
    count = static_cast<std::uint16_t>(stagedCount);
    return true;
}

/// Keeps immutable rounded-panel geometry until any input affecting its vertices changes.
class Ps1RoundedRectGeometryCache {
public:
    bool TryGet(Ps1UiPoint position, Ps1UiSize size, Ps1UiPoint scale, float rotationRadians,
                float radius, float borderThickness, std::uint8_t roundedCorners, Ps1ScreenRect viewport,
                const Ps1UiTriangle*& triangles, std::uint16_t& count, std::uint16_t& fillCount, bool& reused) {
        const Key key{position, size, scale, rotationRadians, radius, borderThickness, roundedCorners, viewport};
        for (Entry& entry : Entries) {
            if (entry.valid && Same(entry.key, key)) {
                triangles = entry.triangles;
                count = entry.count;
                fillCount = entry.fillCount;
                reused = true;
                return true;
            }
        }
        Entry& entry = Entries[NextReplace];
        entry.valid = false;
        if (!TryBuildRoundedRectTriangles(position, size, scale, rotationRadians, radius, borderThickness,
                                          roundedCorners, viewport, entry.triangles, 46, entry.count,
                                          entry.fillCount)) return false;
        entry.key = key;
        entry.valid = true;
        NextReplace = (NextReplace + 1u) % 4u;
        triangles = entry.triangles;
        count = entry.count;
        fillCount = entry.fillCount;
        reused = false;
        return true;
    }

private:
    struct Key {
        Ps1UiPoint position;
        Ps1UiSize size;
        Ps1UiPoint scale;
        float rotation;
        float radius;
        float border;
        std::uint8_t corners;
        Ps1ScreenRect viewport;
    };
    struct Entry {
        Key key{};
        Ps1UiTriangle triangles[46]{};
        std::uint16_t count = 0;
        std::uint16_t fillCount = 0;
        bool valid = false;
    };
    static bool Same(const Key& a, const Key& b) {
        return a.position.x == b.position.x && a.position.y == b.position.y &&
               a.size.width == b.size.width && a.size.height == b.size.height &&
               a.scale.x == b.scale.x && a.scale.y == b.scale.y &&
               a.rotation == b.rotation && a.radius == b.radius && a.border == b.border &&
               a.corners == b.corners &&
               a.viewport.x == b.viewport.x && a.viewport.y == b.viewport.y &&
               a.viewport.width == b.viewport.width && a.viewport.height == b.viewport.height;
    }
    Entry Entries[4]{};
    unsigned NextReplace = 0;
};

/** The PS1 OT is traversed from high to low Z; UI orders increase toward the front-most bucket. */
inline bool TryMapRenderOrder(std::uint8_t renderOrder, std::uint16_t& bucket) {
    bucket = static_cast<std::uint16_t>(255u - renderOrder);
    return true;
}

/** The PS1 GPU provides binary coverage and a fixed half-back/half-front blend mode. */
inline bool TryMapAlphaToBlend(std::uint8_t alpha, bool& semiTransparent) {
    semiTransparent = alpha != 0 && alpha != 255;
    return true;
}

}  // namespace helengine::ps1
