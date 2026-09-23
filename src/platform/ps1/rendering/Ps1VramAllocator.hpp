#pragma once

#include <cstdint>

namespace helengine::ps1 {

/// <summary>
/// Places textures and palettes in the VRAM the display buffers do not use.
///
/// VRAM is 1024 by 512 sixteen-bit words. PSYQo puts the two 320 by 240 display buffers in the left
/// 320 columns, so everything from column 320 rightwards is free. Allocation is a bump over that
/// region in shelves: a request takes space on the current shelf, and a request that does not fit
/// starts a new shelf below. Nothing is ever freed, because the authored milestone loads one scene
/// and keeps it; a scene that unloads textures needs a real allocator instead.
/// </summary>
class Ps1VramAllocator {
public:
    /// <summary>First VRAM column not used by the display buffers.</summary>
    static constexpr std::uint16_t FirstFreeColumn = 320;

    /// <summary>VRAM width in 16-bit words.</summary>
    static constexpr std::uint16_t VramWidth = 1024;

    /// <summary>VRAM height in rows.</summary>
    static constexpr std::uint16_t VramHeight = 512;

    /// <summary>CLUT X coordinates are encoded by the GPU in 16-word units.</summary>
    static constexpr std::uint16_t ClutXAlignment = 16;

    /// <summary>A PS1 texture page spans 64 sixteen-bit VRAM words horizontally.</summary>
    static constexpr std::uint16_t TexturePageWidthWords = 64;

    /// <summary>Returns the allocator to its empty state, releasing every placement at once.</summary>
    void Reset() {
        NextColumn = FirstFreeColumn;
        ShelfTop = 0;
        ShelfHeight = 0;
    }

    /// <summary>
    /// Reserves a rectangle measured in 16-bit VRAM words.
    /// </summary>
    /// <param name="widthInWords">Width in 16-bit words; a paletted texture packs several pixels per word.</param>
    /// <param name="heightInRows">Height in VRAM rows, which equals the texture's pixel height.</param>
    /// <param name="x">Receives the VRAM column of the placement.</param>
    /// <param name="y">Receives the VRAM row of the placement.</param>
    /// <returns>False when the request does not fit in the remaining region, leaving x and y untouched.</returns>
    bool TryAllocate(std::uint16_t widthInWords, std::uint16_t heightInRows, std::uint16_t& x, std::uint16_t& y) {
        return TryAllocateAligned(widthInWords, heightInRows, 1, x, y);
    }

    /// <summary>Places a texture without crossing a horizontal GPU texture-page boundary.</summary>
    bool TryAllocateWithinHorizontalPage(std::uint16_t widthInWords, std::uint16_t heightInRows,
                                         std::uint16_t& x, std::uint16_t& y) {
        if (widthInWords == 0 || widthInWords > TexturePageWidthWords) return false;
        Ps1VramAllocator trial = *this;
        std::uint16_t trialX = 0, trialY = 0;
        if (trial.TryAllocate(widthInWords, heightInRows, trialX, trialY) &&
            trialX % TexturePageWidthWords + widthInWords <= TexturePageWidthWords) {
            *this = trial;
            x = trialX;
            y = trialY;
            return true;
        }
        return TryAllocateAligned(widthInWords, heightInRows, TexturePageWidthWords, x, y);
    }

    /// <summary>
    /// Reserves an aligned rectangle, for VRAM structures such as CLUTs whose GPU address has
    /// fewer horizontal bits than a raw VRAM word coordinate.
    /// </summary>
    /// <param name="widthInWords">Width in 16-bit VRAM words.</param>
    /// <param name="heightInRows">Height in VRAM rows.</param>
    /// <param name="horizontalAlignment">Required X-coordinate alignment in words.</param>
    /// <param name="x">Receives the aligned VRAM column.</param>
    /// <param name="y">Receives the VRAM row of the placement.</param>
    /// <returns>False when the request does not fit, leaving outputs and allocator state untouched.</returns>
    bool TryAllocateAligned(std::uint16_t widthInWords, std::uint16_t heightInRows,
                            std::uint16_t horizontalAlignment, std::uint16_t& x, std::uint16_t& y) {
        if (widthInWords == 0 || heightInRows == 0 || horizontalAlignment == 0) { return false; }
        if (widthInWords > VramWidth - FirstFreeColumn || heightInRows > VramHeight) { return false; }

        std::uint32_t shelfTop = ShelfTop;
        std::uint32_t shelfHeight = ShelfHeight;
        std::uint32_t nextColumn = NextColumn;
        const auto alignColumn = [horizontalAlignment](std::uint32_t column) {
            const std::uint32_t remainder = column % horizontalAlignment;
            return remainder == 0 ? column : column + horizontalAlignment - remainder;
        };
        std::uint32_t alignedColumn = alignColumn(nextColumn);
        if (alignedColumn + widthInWords > VramWidth) {
            // The aligned request does not fit this shelf, so try a fresh one below it.
            shelfTop += shelfHeight;
            shelfHeight = 0;
            nextColumn = FirstFreeColumn;
            alignedColumn = alignColumn(nextColumn);
        }
        if (shelfTop + heightInRows > VramHeight || alignedColumn + widthInWords > VramWidth) return false;

        ShelfTop = static_cast<std::uint16_t>(shelfTop);
        ShelfHeight = static_cast<std::uint16_t>(heightInRows > shelfHeight ? heightInRows : shelfHeight);
        NextColumn = static_cast<std::uint16_t>(alignedColumn + widthInWords);
        x = static_cast<std::uint16_t>(alignedColumn);
        y = ShelfTop;
        return true;
    }

    /// <summary>Number of VRAM words already handed out, for budget diagnostics.</summary>
    std::uint32_t UsedWords() const {
        return static_cast<std::uint32_t>(ShelfTop) * (VramWidth - FirstFreeColumn) +
               static_cast<std::uint32_t>(NextColumn - FirstFreeColumn) * ShelfHeight;
    }

private:
    /// <summary>Next free column on the current shelf.</summary>
    std::uint16_t NextColumn = FirstFreeColumn;

    /// <summary>Row where the current shelf begins.</summary>
    std::uint16_t ShelfTop = 0;

    /// <summary>Height of the tallest entry placed on the current shelf.</summary>
    std::uint16_t ShelfHeight = 0;
};

}  // namespace helengine::ps1
