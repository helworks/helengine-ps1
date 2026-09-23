#pragma once

#include <cstddef>

namespace helengine::ps1 {

struct Ps1TextGlyph {
    float width;
    float height;
    float offsetY;
    float advance;
};

enum class Ps1TextAlignment { Left, Center, Right };

// Layout is kept independent of Core's native containers so the same glyph positions can be
// checked on the host. The caller supplies cooked font metrics and receives visible glyphs.
template <typename LookupGlyph, typename EmitGlyph>
bool LayoutPs1Text(const char* content, std::size_t length, float baseX, float baseY,
                   float lineHeight, float spaceWidth, float scale, float boxWidth,
                   Ps1TextAlignment alignment, LookupGlyph lookup, EmitGlyph emit) {
    if (content == nullptr || scale <= 0.0f || lineHeight <= 0.0f) return false;
    std::size_t lineStart = 0;
    float lineY = 0.0f;
    while (lineStart < length) {
        std::size_t lineEnd = lineStart;
        while (lineEnd < length && content[lineEnd] != '\n') ++lineEnd;
        float cursor = 0.0f;
        float visibleWidth = 0.0f;
        for (std::size_t i = lineStart; i < lineEnd; ++i) {
            if (content[i] == ' ') {
                cursor += spaceWidth * scale;
                if (cursor > visibleWidth) visibleWidth = cursor;
                continue;
            }
            Ps1TextGlyph glyph{};
            if (!lookup(content[i], glyph)) continue;
            const float width = glyph.width * scale;
            if (cursor + width > visibleWidth) visibleWidth = cursor + width;
            cursor += (glyph.advance > 0.0f ? glyph.advance : glyph.width) * scale;
        }
        float lineX = baseX;
        if (boxWidth > visibleWidth && visibleWidth > 0.0f) {
            if (alignment == Ps1TextAlignment::Center) lineX += (boxWidth - visibleWidth) * 0.5f;
            if (alignment == Ps1TextAlignment::Right) lineX += boxWidth - visibleWidth;
        }
        cursor = 0.0f;
        for (std::size_t i = lineStart; i < lineEnd; ++i) {
            if (content[i] == ' ') {
                cursor += spaceWidth * scale;
                continue;
            }
            Ps1TextGlyph glyph{};
            if (!lookup(content[i], glyph)) continue;
            if (!emit(content[i], glyph, lineX + cursor, baseY + lineY + glyph.offsetY * scale)) return false;
            cursor += (glyph.advance > 0.0f ? glyph.advance : glyph.width) * scale;
        }
        lineStart = lineEnd + 1u;
        lineY += lineHeight;
    }
    return true;
}

}  // namespace helengine::ps1
