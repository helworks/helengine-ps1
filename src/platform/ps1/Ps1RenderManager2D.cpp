#include "platform/ps1/Ps1RenderManager2D.hpp"

#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "IContentStreamSource.hpp"
#include "TextureAsset.hpp"
#include "TextureAssetColorFormat.hpp"
#include "common/syscalls/syscalls.h"
#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "runtime/array.hpp"
#include "runtime/native_cast.hpp"
#include "system/io/stream.hpp"

namespace helengine::ps1 {
namespace {

/// <summary>Palette entries the GPU reads for each supported depth.</summary>
constexpr std::uint16_t FourBitPaletteEntries = 16;
constexpr std::uint16_t EightBitPaletteEntries = 256;

/// <summary>
/// Converts one straight RGBA8888 palette entry to the GPU's 16-bit BGR555 with its semi-transparency
/// bit. A fully transparent entry becomes the all-zero value, which the GPU treats as "do not draw".
/// </summary>
/// <param name="red">Red channel.</param>
/// <param name="green">Green channel.</param>
/// <param name="blue">Blue channel.</param>
/// <param name="alpha">Alpha channel; only fully transparent is representable.</param>
/// <returns>Packed BGR555 palette entry.</returns>
std::uint16_t PackPaletteEntry(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
    if (alpha == 0) { return 0; }
    const std::uint16_t r = static_cast<std::uint16_t>(red >> 3);
    const std::uint16_t g = static_cast<std::uint16_t>(green >> 3);
    const std::uint16_t b = static_cast<std::uint16_t>(blue >> 3);
    const std::uint16_t packed = static_cast<std::uint16_t>(r | (g << 5) | (b << 10));
    // Black would otherwise pack to zero and be treated as transparent, so lift it to the darkest
    // value the GPU still draws.
    return packed == 0 ? static_cast<std::uint16_t>(0x8000) : packed;
}

}  // namespace

Ps1RenderManager2D::Ps1RenderManager2D(psyqo::GPU& gpu) : Gpu(gpu) {}

::RuntimeTexture* Ps1RenderManager2D::BuildTextureFromCooked(HeCppString cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
    if (contentStreamSource == nullptr) {
        he_cpp_custom::Fail("PS1 texture creation received a null content stream source");
    }
    ::Stream* stream = contentStreamSource->OpenRead(cookedAssetPath);
    if (stream == nullptr) {
        he_cpp_custom::Fail("PS1 content source returned a null stream for a cooked texture");
    }
    ::Asset* rawAsset = ::AssetSerializer::Deserialize(stream);
    delete stream;
    ::TextureAsset* asset = he_cpp_try_cast<::TextureAsset>(rawAsset);
    if (asset == nullptr) {
        he_cpp_custom::Fail("PS1 cooked texture asset has the wrong type");
    }
    ::RuntimeTexture* texture = UploadPalettedTexture(asset);
    asset->Dispose();
    delete asset;
    return texture;
}

::RuntimeTexture* Ps1RenderManager2D::BuildTextureFromRaw(::TextureAsset* data) {
    if (data == nullptr) {
        he_cpp_custom::Fail("PS1 texture creation received a null texture asset");
    }
    return UploadPalettedTexture(data);
}

::RuntimeTexture* Ps1RenderManager2D::UploadPalettedTexture(::TextureAsset* asset) {
    const bool fourBit = asset->ColorFormat == ::TextureAssetColorFormat::Indexed4;
    const bool eightBit = asset->ColorFormat == ::TextureAssetColorFormat::Indexed8;
    if (!fourBit && !eightBit) {
        ramsyscall_printf("PS1_TEXTURE_UNSUPPORTED_FORMAT format=%d\n", static_cast<int>(asset->ColorFormat));
        he_cpp_custom::Fail("PS1 textures must be cooked to Indexed4 or Indexed8");
    }
    if (asset->Colors == nullptr || asset->PaletteColors == nullptr) {
        he_cpp_custom::Fail("PS1 cooked texture is missing its pixels or its palette");
    }

    const std::uint16_t pixelsPerWord = fourBit ? 4u : 2u;
    const std::uint16_t width = asset->Width;
    const std::uint16_t height = asset->Height;
    if (width == 0 || height == 0 || (width % pixelsPerWord) != 0) {
        ramsyscall_printf("PS1_TEXTURE_UNSUPPORTED_SIZE width=%u height=%u\n", static_cast<unsigned>(width),
                          static_cast<unsigned>(height));
        he_cpp_custom::Fail("PS1 textures must have a width that packs whole 16-bit words");
    }

    const std::uint16_t widthInWords = static_cast<std::uint16_t>(width / pixelsPerWord);
    std::uint16_t vramX = 0;
    std::uint16_t vramY = 0;
    if (!VramAllocator.TryAllocate(widthInWords, height, vramX, vramY)) {
        ramsyscall_printf("PS1_TEXTURE_VRAM_EXHAUSTED words=%u used=%u\n",
                          static_cast<unsigned>(widthInWords) * static_cast<unsigned>(height),
                          static_cast<unsigned>(VramAllocator.UsedWords()));
        he_cpp_custom::Fail("PS1 ran out of VRAM for a cooked texture");
    }

    // Indexed pixel bytes are already packed for the GPU at 8 bits; at 4 bits the cook packs two
    // pixels per byte, so in both cases the byte stream is the VRAM word stream.
    const std::uint32_t expectedBytes = static_cast<std::uint32_t>(widthInWords) * height * 2u;
    if (static_cast<std::uint32_t>(asset->Colors->Length) != expectedBytes) {
        ramsyscall_printf("PS1_TEXTURE_PIXEL_LENGTH expected=%u actual=%u\n", static_cast<unsigned>(expectedBytes),
                          static_cast<unsigned>(asset->Colors->Length));
        he_cpp_custom::Fail("PS1 cooked texture pixel length does not match its declared size");
    }
    psyqo::Rect pixelRegion;
    pixelRegion.pos = {{.x = static_cast<std::int16_t>(vramX), .y = static_cast<std::int16_t>(vramY)}};
    pixelRegion.size = {{.w = static_cast<std::int16_t>(widthInWords), .h = static_cast<std::int16_t>(height)}};
    Gpu.uploadToVRAM(reinterpret_cast<const std::uint16_t*>(&(*asset->Colors)[0]), pixelRegion);

    const std::uint16_t paletteEntries = fourBit ? FourBitPaletteEntries : EightBitPaletteEntries;
    const std::uint32_t suppliedEntries = static_cast<std::uint32_t>(asset->PaletteColors->Length) / 4u;
    std::uint16_t clutX = 0;
    std::uint16_t clutY = 0;
    if (!VramAllocator.TryAllocate(paletteEntries, 1, clutX, clutY)) {
        he_cpp_custom::Fail("PS1 ran out of VRAM for a texture palette");
    }
    std::uint16_t palette[EightBitPaletteEntries];
    for (std::uint16_t entry = 0; entry < paletteEntries; ++entry) {
        if (entry < suppliedEntries) {
            const std::uint32_t offset = static_cast<std::uint32_t>(entry) * 4u;
            palette[entry] = PackPaletteEntry((*asset->PaletteColors)[offset], (*asset->PaletteColors)[offset + 1],
                                              (*asset->PaletteColors)[offset + 2], (*asset->PaletteColors)[offset + 3]);
        } else {
            palette[entry] = 0;
        }
    }
    psyqo::Rect paletteRegion;
    paletteRegion.pos = {{.x = static_cast<std::int16_t>(clutX), .y = static_cast<std::int16_t>(clutY)}};
    paletteRegion.size = {{.w = static_cast<std::int16_t>(paletteEntries), .h = 1}};
    Gpu.uploadToVRAM(palette, paletteRegion);

    Ps1RuntimeTexture* texture = new Ps1RuntimeTexture();
    texture->set_Width(width);
    texture->set_Height(height);
    texture->VramX = vramX;
    texture->VramY = vramY;
    texture->ClutX = clutX;
    texture->ClutY = clutY;
    texture->PixelDepth = fourBit ? Ps1RuntimeTexture::Depth::FourBit : Ps1RuntimeTexture::Depth::EightBit;
    ++UploadedTextures;
    ramsyscall_printf("PS1_TEXTURE_UPLOADED w=%u h=%u bpp=%u vram=%u,%u clut=%u,%u used_words=%u\n",
                      static_cast<unsigned>(width), static_cast<unsigned>(height), fourBit ? 4u : 8u,
                      static_cast<unsigned>(vramX), static_cast<unsigned>(vramY), static_cast<unsigned>(clutX),
                      static_cast<unsigned>(clutY), static_cast<unsigned>(VramAllocator.UsedWords()));
    return texture;
}

void Ps1RenderManager2D::ReleaseTexture(::RuntimeTexture* texture) {
    if (texture == nullptr) { return; }
    texture->Dispose();
    delete texture;
}

void Ps1RenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D*) {
    he_cpp_custom::Fail("PS1 does not draw 2D shapes yet");
}

void Ps1RenderManager2D::DrawSprite(::ISpriteDrawable2D*) {
    he_cpp_custom::Fail("PS1 does not draw sprites yet");
}

void Ps1RenderManager2D::DrawText(::ITextDrawable2D*) {
    he_cpp_custom::Fail("PS1 does not draw text yet");
}

void Ps1RenderManager2D::UpdateTextureRegionCore(::RuntimeTexture*, int32_t, int32_t, int32_t, int32_t, Array<uint8_t>*, int32_t) {
    he_cpp_custom::Fail("PS1 does not update textures after upload");
}

}  // namespace helengine::ps1
