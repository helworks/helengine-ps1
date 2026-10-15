#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/ps1/rendering/Ps1VramAllocator.hpp"
#include "platform/ps1/Ps1TextureUploadFence.hpp"
#include "psyqo/primitives/common.hh"

namespace helengine::ps1 {

/// <summary>Converts coverage to the same opaque/binary BGR555 texels as the PS1 cooker, including visible black.</summary>
inline std::uint16_t PackPs1MutableTexel(const std::uint8_t* rgba, bool opaque) {
    if (!opaque && rgba[3] < 128u) return 0;
    return static_cast<std::uint16_t>((rgba[0] >> 3) | ((rgba[1] >> 3) << 5) |
                                      ((rgba[2] >> 3) << 10) | 0x8000u);
}

/// <summary>Checks source stride/length and destination bounds before a GPU fence or pixel mutation.</summary>
inline bool IsPs1MutableRegionValid(unsigned textureWidth, unsigned textureHeight, unsigned strideWords,
                                    int x, int y, int width, int height, const std::uint8_t* rgba,
                                    std::size_t sourceBytes, int sourceRowPitch) {
    if (!rgba || !textureWidth || !textureHeight || textureWidth > 256u || textureHeight > 256u ||
        strideWords != ((textureWidth + 1u) & ~1u) || x < 0 || y < 0 || width <= 0 || height <= 0 ||
        static_cast<unsigned>(x) >= textureWidth || static_cast<unsigned>(y) >= textureHeight ||
        static_cast<unsigned>(width) > textureWidth - static_cast<unsigned>(x) ||
        static_cast<unsigned>(height) > textureHeight - static_cast<unsigned>(y) || sourceRowPitch < width * 4) return false;
    // Keep this arithmetic wider than size_t on MIPS so a malicious stride cannot wrap the length check.
    const std::uint64_t required = static_cast<std::uint64_t>(height - 1) * static_cast<unsigned>(sourceRowPitch) +
                                    static_cast<unsigned>(width) * 4u;
    return required <= sourceBytes;
}

/// <summary>Reserves direct-color VRAM and a retained aligned shadow, rolling back nullable allocation failure.</summary>
template<class Allocate>
bool AllocatePs1MutableTexture(Ps1VramAllocator& allocator, unsigned width, unsigned height,
                               Allocate allocate, std::uint16_t*& pixels, std::uint16_t& strideWords,
                               std::uint16_t& vramX, std::uint16_t& vramY) {
    pixels = nullptr;
    if (!width || !height || width > 256u || height > 256u) return false;
    strideWords = static_cast<std::uint16_t>((width + 1u) & ~1u);
    if (!allocator.TryAllocateWithinHorizontalPage(strideWords, static_cast<std::uint16_t>(height), 1, vramX, vramY)) return false;
    pixels = reinterpret_cast<std::uint16_t*>(allocate(static_cast<std::size_t>(strideWords) * height * 2u));
    if (!pixels) {
        allocator.Release(vramX, vramY, strideWords, static_cast<std::uint16_t>(height));
        return false;
    }
    for (unsigned index = 0; index < strideWords * height; ++index) pixels[index] = 0;
    return true;
}

/// <summary>Fences submitted draws, patches retained rows and uploads even-word spans without reallocating VRAM.</summary>
template<class Gpu>
void UpdatePs1MutableTexture(Gpu& gpu, std::uint16_t* pixels, unsigned strideWords,
                             std::uint16_t vramX, std::uint16_t vramY, int x, int y, int width, int height,
                             const std::uint8_t* rgba, int sourceRowPitch, bool opaque) {
    WaitForTextureUpload(gpu);
    for (int row = 0; row < height; ++row) {
        auto* destination = pixels + static_cast<unsigned>(y + row) * strideWords + static_cast<unsigned>(x);
        const auto* source = rgba + static_cast<std::size_t>(row) * static_cast<unsigned>(sourceRowPitch);
        for (int column = 0; column < width; ++column) destination[column] = PackPs1MutableTexel(source + column * 4, opaque);
    }
    const unsigned start = static_cast<unsigned>(x) & ~1u;
    const unsigned end = (static_cast<unsigned>(x + width) + 1u) & ~1u;
    MarkFrameStage(Ps1FrameStage::TexturePackAfter);
    if (start == 0u && end == strideWords) {
        psyqo::Rect region;
        region.pos = {{.x = static_cast<std::int16_t>(vramX), .y = static_cast<std::int16_t>(vramY + y)}};
        region.size = {{.w = static_cast<std::int16_t>(strideWords), .h = static_cast<std::int16_t>(height)}};
        MarkFrameStage(Ps1FrameStage::UploadBefore, 3);
        gpu.uploadToVRAM(pixels + static_cast<unsigned>(y) * strideWords, region);
        MarkFrameStage(Ps1FrameStage::UploadAfter, 3);
        return;
    }
    for (int row = 0; row < height; ++row) {
        psyqo::Rect region;
        region.pos = {{.x = static_cast<std::int16_t>(vramX + start), .y = static_cast<std::int16_t>(vramY + y + row)}};
        region.size = {{.w = static_cast<std::int16_t>(end - start), .h = 1}};
        MarkFrameStage(Ps1FrameStage::UploadBefore, 3);
        gpu.uploadToVRAM(pixels + static_cast<unsigned>(y + row) * strideWords + start, region);
        MarkFrameStage(Ps1FrameStage::UploadAfter, 3);
    }
}

} // namespace helengine::ps1
