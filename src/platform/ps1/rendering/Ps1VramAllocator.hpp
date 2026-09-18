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
        if (widthInWords == 0 || heightInRows == 0) { return false; }
        if (widthInWords > VramWidth - FirstFreeColumn || heightInRows > VramHeight) { return false; }

        if (NextColumn + widthInWords > VramWidth) {
            // The shelf is full, so drop to a new one starting below the tallest entry on this one.
            ShelfTop = static_cast<std::uint16_t>(ShelfTop + ShelfHeight);
            ShelfHeight = 0;
            NextColumn = FirstFreeColumn;
        }
        if (ShelfTop + heightInRows > VramHeight) { return false; }

        x = NextColumn;
        y = ShelfTop;
        NextColumn = static_cast<std::uint16_t>(NextColumn + widthInWords);
        if (heightInRows > ShelfHeight) { ShelfHeight = heightInRows; }
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
