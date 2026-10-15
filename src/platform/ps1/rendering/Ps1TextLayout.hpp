#pragma once

#include <cstddef>
#include <cstdint>

namespace helengine::ps1 {

struct Ps1TextGlyph {
    float width;
    float height;
    float offsetY;
    float advance;
    std::int16_t sourceX = 0;
    std::int16_t sourceY = 0;
    std::int16_t sourceWidth = 0;
    std::int16_t sourceHeight = 0;
    bool sourceValid = false;
    float bearingX = 0.0f;
};

/// Geometry must match before a cached text quad can change only its atlas UVs.
inline bool CanReusePs1TextGlyphGeometry(const Ps1TextGlyph& original, const Ps1TextGlyph& replacement) {
    return original.width == replacement.width && original.height == replacement.height &&
           original.offsetY == replacement.offsetY && original.advance == replacement.advance &&
           original.bearingX == replacement.bearingX &&
           replacement.sourceValid;
}

/// Reuses cooked font metrics across frames until their texture is released.
class Ps1TextGlyphCache {
public:
    void Invalidate() {
        if (++Generation == 0) {
            for (Entry& entry : Entries) entry.generation = 0;
            Generation = 1;
        }
    }

    template <typename Fetch>
    bool Lookup(char character, const void* font, Ps1TextGlyph& out, Fetch fetch) {
        Entry& entry = Entries[static_cast<unsigned char>(character)];
        if (entry.generation != Generation || entry.font != font) {
            entry.found = fetch(character, entry.glyph);
            entry.font = font;
            entry.generation = Generation;
        }
        if (entry.found) out = entry.glyph;
        return entry.found;
    }

private:
    struct Entry {
        const void* font = nullptr;
        Ps1TextGlyph glyph{};
        std::uint32_t generation = 0;
        bool found = false;
    };
    Entry Entries[256]{};
    std::uint32_t Generation = 1;
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
        for (std::size_t i = lineStart; alignment != Ps1TextAlignment::Left && i < lineEnd; ++i) {
            if (content[i] == ' ') {
                cursor += spaceWidth * scale;
                if (cursor > visibleWidth) visibleWidth = cursor;
                continue;
            }
            Ps1TextGlyph glyph{};
            if (!lookup(content[i], glyph)) continue;
            const float width = glyph.width * scale;
            const float rightEdge = cursor + glyph.bearingX * scale + width;
            if (rightEdge > visibleWidth) visibleWidth = rightEdge;
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
            if (!emit(content[i], glyph, lineX + cursor + glyph.bearingX * scale,
                      baseY + lineY + glyph.offsetY * scale)) return false;
            cursor += (glyph.advance > 0.0f ? glyph.advance : glyph.width) * scale;
        }
        lineStart = lineEnd + 1u;
        lineY += lineHeight;
    }
    return true;
}

}  // namespace helengine::ps1
