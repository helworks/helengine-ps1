#pragma once

#include <cmath>
#include <cstdint>

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

inline bool Finite(float value) { return std::isfinite(value); }

inline bool ToScreen(float x, float y, const Ps1ScreenRect& viewport, std::int16_t& outX, std::int16_t& outY) {
    x += static_cast<float>(viewport.x);
    y += static_cast<float>(viewport.y);
    if (!Finite(x) || !Finite(y) || x < -32768.0f || x > 32767.0f || y < -32768.0f || y > 32767.0f) return false;
    outX = static_cast<std::int16_t>(std::lround(x));
    outY = static_cast<std::int16_t>(std::lround(y));
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
    const float cosine = std::cos(rotationRadians);
    const float sine = std::sin(rotationRadians);
    const float localX[4] = {0.0f, width, 0.0f, width};
    const float localY[4] = {0.0f, 0.0f, height, height};
    const float sourceU[4] = {sourceRect.x, sourceRect.x + sourceRect.width, sourceRect.x,
                              sourceRect.x + sourceRect.width};
    const float sourceV[4] = {sourceRect.y, sourceRect.y, sourceRect.y + sourceRect.height,
                              sourceRect.y + sourceRect.height};
    Ps1UiVertex result[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        const float dx = localX[i] - width * 0.5f;
        const float dy = localY[i] - height * 0.5f;
        const float x = centerX + dx * cosine - dy * sine;
        const float y = centerY + dx * sine + dy * cosine;
        if (!ToScreen(x, y, viewport, result[i].x, result[i].y)) return false;
        const float u = sourceU[i] * (textureSize.width - 1.0f);
        const float v = sourceV[i] * (textureSize.height - 1.0f);
        if (u < 0.0f || u > 255.0f || v < 0.0f || v > 255.0f) return false;
        result[i].u = static_cast<std::uint8_t>(std::lround(u));
        result[i].v = static_cast<std::uint8_t>(std::lround(v));
    }
    for (unsigned i = 0; i < 4; ++i) vertices[i] = result[i];
    return true;
}

/** Builds a convex fill fan and optional border ring; 0x01, 0x02, 0x04, 0x08 select TL, TR, BR, BL. */
inline bool TryBuildRoundedRectTriangles(Ps1UiPoint position, Ps1UiSize size, Ps1UiPoint scale,
                                         float rotationRadians, float radius, float borderThickness,
                                         std::uint8_t roundedCorners, Ps1ScreenRect viewport,
                                         Ps1UiTriangle* triangles, std::uint16_t capacity, std::uint16_t& count) {
    using namespace ui_geometry_detail;
    count = 0;
    if (!ValidViewport(viewport) || triangles == nullptr || !Finite(position.x) || !Finite(position.y) ||
        !Finite(size.width) || !Finite(size.height) || !Finite(scale.x) || !Finite(scale.y) ||
        !Finite(rotationRadians) || !Finite(radius) || !Finite(borderThickness) || size.width <= 0.0f ||
        size.height <= 0.0f || scale.x <= 0.0f || scale.y <= 0.0f || radius < 0.0f || borderThickness < 0.0f) return false;
    const float width = size.width * scale.x;
    const float height = size.height * scale.y;
    const float border = borderThickness;
    if (!Finite(width) || !Finite(height) || border * 2.0f >= width || border * 2.0f >= height) return false;
    const float clampedRadius = radius < (width < height ? width : height) * 0.5f ? radius : (width < height ? width : height) * 0.5f;
    constexpr unsigned SegmentsPerCorner = 4;
    Ps1UiPoint outer[20]{};
    Ps1UiPoint inner[20]{};
    unsigned vertexCount = 0;
    const float starts[4] = {180.0f, 270.0f, 0.0f, 90.0f};
    for (unsigned corner = 0; corner < 4; ++corner) {
        const bool rounded = (roundedCorners & (1u << corner)) != 0 && clampedRadius > 0.0f;
        const float r = rounded ? clampedRadius : 0.0f;
        const float cx[4] = {position.x + r, position.x + width - r,
                             position.x + width - r, position.x + r};
        const float cy[4] = {position.y + r, position.y + r,
                             position.y + height - r, position.y + height - r};
        if (rounded) {
            for (unsigned segment = 0; segment <= SegmentsPerCorner; ++segment) {
                const float angle = (starts[corner] + 90.0f * static_cast<float>(segment) / SegmentsPerCorner) *
                                    0.01745329251994329577f;
                outer[vertexCount++] = {cx[corner] + r * std::cos(angle), cy[corner] + r * std::sin(angle)};
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
                const float angle = (starts[corner] + 90.0f * static_cast<float>(segment) / SegmentsPerCorner) *
                                    0.01745329251994329577f;
                inner[innerCount++] = {cx[corner] + r * std::cos(angle), cy[corner] + r * std::sin(angle)};
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
    const float cosine = std::cos(rotationRadians);
    const float sine = std::sin(rotationRadians);
    const float centerX = position.x + width * 0.5f;
    const float centerY = position.y + height * 0.5f;
    auto vertex = [&](Ps1UiPoint point, Ps1UiVertex& output) {
        const float dx = point.x - centerX;
        const float dy = point.y - centerY;
        return ToScreen(centerX + dx * cosine - dy * sine, centerY + dx * sine + dy * cosine,
                        viewport, output.x, output.y);
    };
    Ps1UiTriangle staged[58]{};
    unsigned stagedCount = 0;
    Ps1UiVertex transformedOuter[20]{};
    Ps1UiVertex transformedInner[20]{};
    for (unsigned i = 0; i < vertexCount; ++i) {
        if (!vertex(outer[i], transformedOuter[i]) || !vertex(inner[i], transformedInner[i])) return false;
    }
    if (border > 0.0f) {
        for (unsigned i = 0; i < vertexCount; ++i) {
            const unsigned next = (i + 1u) % vertexCount;
            staged[stagedCount++] = {transformedOuter[i], transformedOuter[next], transformedInner[next]};
            staged[stagedCount++] = {transformedOuter[i], transformedInner[next], transformedInner[i]};
        }
    }
    for (unsigned i = 1; i + 1 < vertexCount; ++i) {
        const Ps1UiVertex& a = border > 0.0f ? transformedInner[0] : transformedOuter[0];
        const Ps1UiVertex& b = border > 0.0f ? transformedInner[i] : transformedOuter[i];
        const Ps1UiVertex& c = border > 0.0f ? transformedInner[i + 1] : transformedOuter[i + 1];
        staged[stagedCount++] = {a, b, c};
    }
    for (unsigned i = 0; i < stagedCount; ++i) triangles[i] = staged[i];
    count = static_cast<std::uint16_t>(stagedCount);
    return true;
}

/** The PS1 OT is traversed from high to low Z; UI orders increase toward the front-most bucket. */
inline bool TryMapRenderOrder(std::uint8_t renderOrder, std::uint16_t& bucket) {
    bucket = static_cast<std::uint16_t>(255u - renderOrder);
    return true;
}

}  // namespace helengine::ps1
