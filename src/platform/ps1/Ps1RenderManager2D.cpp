#include "platform/ps1/Ps1RenderManager2D.hpp"

#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IDrawable2D.hpp"
#include "IContentStreamSource.hpp"
#include "IRenderQueue2D.hpp"
#include "IRoundedRectDrawable2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "RoundedRectCorners.hpp"
#include "TextureAsset.hpp"
#include "TextureAssetColorFormat.hpp"
#include "byte4.hpp"
#include "common/syscalls/syscalls.h"
#include "float3.hpp"
#include "float4.hpp"
#include "int2.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "platform/ps1/rendering/Ps1RuntimeTexture.hpp"
#include "runtime/array.hpp"
#include "runtime/native_cast.hpp"
#include "system/io/stream.hpp"


namespace helengine::ps1 {
namespace {

/// <summary>Palette entries the GPU reads for each supported depth.</summary>
constexpr std::uint16_t FourBitPaletteEntries = 16;
constexpr std::uint16_t EightBitPaletteEntries = 256;
constexpr std::int16_t DisplayWidth = 320;
constexpr std::int16_t DisplayHeight = 240;
constexpr unsigned MaximumRoundedTriangles = 46;

/// <summary>
/// Converts one straight RGBA8888 palette entry to the GPU's 16-bit BGR555 with its semi-transparency
/// bit. A fully transparent entry becomes the all-zero value, which the GPU treats as "do not draw".
/// </summary>
/// <param name="red">Red channel.</param>
/// <param name="green">Green channel.</param>
/// <param name="blue">Blue channel.</param>
/// <param name="alpha">Alpha channel; zero skips the entry and visible entries set the PS1 STP bit.</param>
/// <returns>Packed BGR555 palette entry.</returns>
std::uint16_t PackPaletteEntry(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
    if (alpha == 0) { return 0; }
    const std::uint16_t r = static_cast<std::uint16_t>(red >> 3);
    const std::uint16_t g = static_cast<std::uint16_t>(green >> 3);
    const std::uint16_t b = static_cast<std::uint16_t>(blue >> 3);
    const std::uint16_t packed = static_cast<std::uint16_t>(r | (g << 5) | (b << 10));
    // Black would otherwise pack to zero and be treated as transparent, so lift it to the darkest
    // value the GPU still draws.
    return static_cast<std::uint16_t>(packed | 0x8000u);
}

bool TryScreenTransform(::Entity* entity, Ps1UiPoint& position, Ps1UiPoint& scale, float& rotation) {
    if (entity == nullptr) return false;
    const ::float3 worldPosition = entity->get_Position();
    const ::float3 worldScale = entity->get_Scale();
    const ::float4 orientation = entity->get_Orientation();
    const float rightX = 1.0f - 2.0f * (orientation.Y * orientation.Y + orientation.Z * orientation.Z);
    const float rightY = 2.0f * (orientation.X * orientation.Y + orientation.W * orientation.Z);
    position = {worldPosition.X, worldPosition.Y};
    scale = {worldScale.X, worldScale.Y};
    rotation = ui_geometry_detail::Atan2(rightY, rightX);
    return ui_geometry_detail::Finite(position.x) && ui_geometry_detail::Finite(position.y) &&
           ui_geometry_detail::Finite(scale.x) && ui_geometry_detail::Finite(scale.y) &&
           ui_geometry_detail::Finite(rotation);
}

std::uint8_t TextureModulation(std::uint8_t channel) {
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(channel) * 128u + 127u) / 255u);
}

void TryColor(::byte4 color, bool modulation, psyqo::Color& gpuColor) {
    gpuColor = {{.r = modulation ? TextureModulation(color.X) : color.X,
                 .g = modulation ? TextureModulation(color.Y) : color.Y,
                 .b = modulation ? TextureModulation(color.Z) : color.Z}};
}

std::uint8_t MapCoreRoundedCorners(std::uint8_t coreCorners) {
    // Core enum values are TL=1, TR=2, BL=4, BR=8; geometry walks TL, TR, BR, BL.
    return static_cast<std::uint8_t>((coreCorners & 0x03u) | ((coreCorners & 0x04u) << 1u) |
                                     ((coreCorners & 0x08u) >> 1u));
}

}  // namespace

Ps1RenderManager2D::Ps1RenderManager2D(psyqo::GPU& gpu, Ps1RasterRenderer& renderer)
    : Gpu(gpu), Renderer(renderer) {}

bool Ps1RenderManager2D::ResolveViewport(::ICamera* camera, Ps1ScreenRect& viewport) const {
    if (camera == nullptr) return false;
    const ::float4 authored = camera->get_Viewport();
    if (!ui_geometry_detail::Finite(authored.X) || !ui_geometry_detail::Finite(authored.Y) ||
        !ui_geometry_detail::Finite(authored.Z) || !ui_geometry_detail::Finite(authored.W) ||
        authored.Z <= 0.0f || authored.W <= 0.0f) return false;
    float x = authored.X;
    float y = authored.Y;
    float width = authored.Z;
    float height = authored.W;
    if (width <= 1.0f && height <= 1.0f) {
        x *= DisplayWidth;
        y *= DisplayHeight;
        width *= DisplayWidth;
        height *= DisplayHeight;
    }
    const long ix = ui_geometry_detail::Round(x);
    const long iy = ui_geometry_detail::Round(y);
    const long iw = ui_geometry_detail::Round(width);
    const long ih = ui_geometry_detail::Round(height);
    if (iw <= 0 || ih <= 0 || ix < 0 || iy < 0 || ix + iw > DisplayWidth || iy + ih > DisplayHeight) return false;
    viewport = {static_cast<std::int16_t>(ix), static_cast<std::int16_t>(iy), static_cast<std::int16_t>(iw),
                static_cast<std::int16_t>(ih)};
    return true;
}

void Ps1RenderManager2D::Reject(const char* marker) {
    if (FailureMarker == nullptr) FailureMarker = marker;
}

bool Ps1RenderManager2D::DrawQueue(::ICamera* camera) {
    VisitedSprites = 0;
    VisitedShapes = 0;
    SkippedTexts = 0;
    PendingCount = 0;
    PendingShapes = 0;
    PendingSprites = 0;
    FailureMarker = nullptr;
    if (!ResolveViewport(camera, ActiveViewport)) {
        Reject("PS1_UI_VIEWPORT_UNSUPPORTED");
        return false;
    }
    if (camera->get_RenderQueue2D() == nullptr) {
        Reject("PS1_UI_QUEUE_MISSING");
        return false;
    }
    QueueOpen = true;
    camera->get_RenderQueue2D()->VisitOrdered(this);
    QueueOpen = false;
    return FailureMarker == nullptr && FlushPendingInVisitOrder();
}

bool Ps1RenderManager2D::AppendPending(const PendingUiDrawable& drawable) {
    if (PendingCount >= PendingUiCapacity) {
        Reject("PS1_UI_QUEUE_CAPACITY");
        return false;
    }
    if (drawable.IsSprite) {
        if (PendingSprites >= Ps1FramePacketLimits::TexturedTriangles / 2u) {
            Reject("PS1_UI_SPRITE_CAPACITY");
            return false;
        }
        ++PendingSprites;
    } else {
        if (PendingShapes >= Ps1FramePacketLimits::UiTriangles) {
            Reject("PS1_UI_SHAPE_CAPACITY");
            return false;
        }
        ++PendingShapes;
    }
    Pending[PendingCount++] = drawable;
    return true;
}

bool Ps1RenderManager2D::FlushPendingInVisitOrder() {
    for (std::uint16_t i = PendingCount; i > 0; --i) {
        const PendingUiDrawable& pending = Pending[i - 1u];
        bool queued = false;
        if (pending.IsSprite && pending.Texture != nullptr) {
            const Ps1RuntimeTexture& texture = *pending.Texture;
            queued = Renderer.QueueAuthoredUiSprite(pending.Vertices, texture.VramX, texture.VramY, texture.ClutX,
                                                     texture.ClutY, texture.PixelsPerWord(),
                                                     texture.PixelDepth == Ps1RuntimeTexture::Depth::FourBit,
                                                     pending.Color, pending.SemiTransparent, pending.Bucket);
        } else {
            const Ps1UiTriangle triangle{pending.Vertices[0], pending.Vertices[1], pending.Vertices[2]};
            queued = Renderer.QueueAuthoredUiTriangle(triangle, pending.Color, pending.SemiTransparent, pending.Bucket);
        }
        if (!queued) {
            Reject(Renderer.Stats().failure == Ps1RenderFailure::Capacity ? "PS1_UI_PACKET_CAPACITY"
                                                                          : "PS1_UI_FRAME_OWNERSHIP");
            return false;
        }
    }
    return true;
}

void Ps1RenderManager2D::Visit(::IDrawable2D* drawable) {
    if (drawable == nullptr || !QueueOpen) return;
    if (dynamic_cast<::ISpriteDrawable2D*>(drawable) != nullptr ||
        dynamic_cast<::IRoundedRectDrawable2D*>(drawable) != nullptr ||
        dynamic_cast<::ITextDrawable2D*>(drawable) != nullptr) {
        drawable->Draw();
        return;
    }
    Reject("PS1_UI_DRAWABLE_UNSUPPORTED");
}

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
    if (width == 0 || height == 0) {
        ramsyscall_printf("PS1_TEXTURE_UNSUPPORTED_SIZE width=%u height=%u\n", static_cast<unsigned>(width),
                          static_cast<unsigned>(height));
        he_cpp_custom::Fail("PS1 textures must have positive dimensions");
    }

    // Indexed rows are transferred as 16-bit VRAM words. Keep the authored logical width (which
    // may be odd for a pre-scaled icon) and round only the transport width up to a complete word.
    const std::uint16_t widthInWords = static_cast<std::uint16_t>((width + pixelsPerWord - 1u) / pixelsPerWord);
    std::uint16_t vramX = 0;
    std::uint16_t vramY = 0;
    if (!VramAllocator.TryAllocate(widthInWords, height, vramX, vramY)) {
        ramsyscall_printf("PS1_TEXTURE_VRAM_EXHAUSTED words=%u used=%u\n",
                          static_cast<unsigned>(widthInWords) * static_cast<unsigned>(height),
                          static_cast<unsigned>(VramAllocator.UsedWords()));
        he_cpp_custom::Fail("PS1 ran out of VRAM for a cooked texture");
    }

    // Indexed pixels are stored row-major by the cook. Pad each row to a complete 16-bit VRAM
    // word for odd authored widths while keeping the runtime texture's logical width unchanged.
    const std::uint32_t expectedBytes = static_cast<std::uint32_t>(widthInWords) * height * 2u;
    const std::uint32_t sourceRowBytes = fourBit
        ? (static_cast<std::uint32_t>(width) + 1u) / 2u
        : static_cast<std::uint32_t>(width);
    const std::uint32_t sourceBytes = sourceRowBytes * height;
    if (static_cast<std::uint32_t>(asset->Colors->Length) != sourceBytes) {
        ramsyscall_printf("PS1_TEXTURE_PIXEL_LENGTH expected=%u actual=%u\n", static_cast<unsigned>(sourceBytes),
                          static_cast<unsigned>(asset->Colors->Length));
        he_cpp_custom::Fail("PS1 cooked texture pixel length does not match its declared size");
    }
    std::uint8_t* uploadBytes = new std::uint8_t[expectedBytes];
    __builtin_memset(uploadBytes, 0, expectedBytes);
    const std::uint32_t uploadRowBytes = static_cast<std::uint32_t>(widthInWords) * 2u;
    for (std::uint16_t row = 0; row < height; ++row) {
        __builtin_memcpy(uploadBytes + static_cast<std::uint32_t>(row) * uploadRowBytes,
                         &(*asset->Colors)[static_cast<std::uint32_t>(row) * sourceRowBytes], sourceRowBytes);
    }
    psyqo::Rect pixelRegion;
    pixelRegion.pos = {{.x = static_cast<std::int16_t>(vramX), .y = static_cast<std::int16_t>(vramY)}};
    pixelRegion.size = {{.w = static_cast<std::int16_t>(widthInWords), .h = static_cast<std::int16_t>(height)}};
    Gpu.uploadToVRAM(reinterpret_cast<const std::uint16_t*>(uploadBytes), pixelRegion);
    delete[] uploadBytes;

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

void Ps1RenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D* shape) {
    if (shape == nullptr || shape->get_Parent() == nullptr || !QueueOpen) return;
    ++VisitedShapes;
    Ps1UiPoint position{};
    Ps1UiPoint scale{};
    float rotation = 0.0f;
    if (!TryScreenTransform(shape->get_Parent(), position, scale, rotation)) {
        Reject("PS1_UI_SHAPE_TRANSFORM_REJECTED");
        return;
    }
    const ::int2 size = shape->get_Size();
    const std::uint8_t corners = MapCoreRoundedCorners(static_cast<std::uint8_t>(shape->get_Corners()));
    Ps1UiTriangle triangles[MaximumRoundedTriangles]{};
    std::uint16_t count = 0;
    std::uint16_t fillCount = 0;
    if (!TryBuildRoundedRectTriangles(position, {static_cast<float>(size.X), static_cast<float>(size.Y)}, scale,
                                      rotation + shape->get_Rotation(), shape->get_Radius(),
                                      shape->get_BorderThickness(), corners, ActiveViewport, triangles,
                                      MaximumRoundedTriangles, count, fillCount)) {
        Reject("PS1_UI_SHAPE_GEOMETRY_REJECTED");
        return;
    }
    const ::byte4 fill = shape->get_FillColor();
    const ::byte4 border = shape->get_BorderColor();
    psyqo::Color fillColor;
    psyqo::Color borderColor;
    TryColor(fill, false, fillColor);
    TryColor(border, false, borderColor);
    bool fillSemiTransparent = false;
    bool borderSemiTransparent = false;
    TryMapAlphaToBlend(fill.W, fillSemiTransparent);
    TryMapAlphaToBlend(border.W, borderSemiTransparent);
    std::uint16_t bucket = 0;
    if (!TryMapRenderOrder(shape->get_RenderOrder2D(), bucket)) {
        Reject("PS1_UI_SHAPE_ORDER_REJECTED");
        return;
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        const bool isFill = i < fillCount;
        const std::uint8_t alpha = isFill ? fill.W : border.W;
        if (alpha == 0) continue;
        PendingUiDrawable pending{};
        pending.Vertices[0] = triangles[i].a;
        pending.Vertices[1] = triangles[i].b;
        pending.Vertices[2] = triangles[i].c;
        pending.Color = isFill ? fillColor : borderColor;
        pending.Bucket = bucket;
        pending.SemiTransparent = isFill ? fillSemiTransparent : borderSemiTransparent;
        if (!AppendPending(pending)) return;
    }
}

void Ps1RenderManager2D::DrawSprite(::ISpriteDrawable2D* sprite) {
    if (sprite == nullptr || sprite->get_Parent() == nullptr || !QueueOpen) return;
    ++VisitedSprites;
    auto* texture = dynamic_cast<Ps1RuntimeTexture*>(sprite->get_Texture());
    if (texture == nullptr) {
        Reject("PS1_UI_SPRITE_TEXTURE_UNSUPPORTED");
        return;
    }
    const std::uint16_t width = static_cast<std::uint16_t>(texture->get_Width());
    const std::uint16_t height = static_cast<std::uint16_t>(texture->get_Height());
    ::int2 size = sprite->get_Size();
    if (size.X <= 0 || size.Y <= 0) size = ::int2(width, height);
    Ps1UiPoint position{};
    Ps1UiPoint scale{};
    float rotation = 0.0f;
    if (!TryScreenTransform(sprite->get_Parent(), position, scale, rotation)) {
        Reject("PS1_UI_SPRITE_TRANSFORM_REJECTED");
        return;
    }
    const ::float4 source = sprite->get_SourceRect();
    Ps1UiVertex vertices[4]{};
    if (!TryBuildSpriteQuad(position, {static_cast<float>(size.X), static_cast<float>(size.Y)}, scale, rotation,
                            {source.X, source.Y, source.Z, source.W}, {static_cast<float>(width), static_cast<float>(height)},
                            ActiveViewport, vertices)) {
        Reject("PS1_UI_SPRITE_GEOMETRY_REJECTED");
        return;
    }
    const std::uint16_t pageOffsetU = static_cast<std::uint16_t>((texture->VramX % 64u) * texture->PixelsPerWord());
    const std::uint16_t pageOffsetV = static_cast<std::uint16_t>(texture->VramY % 256u);
    if (pageOffsetU + width > 256u || pageOffsetV + height > 256u) {
        Reject("PS1_UI_TEXTURE_CROSSES_PAGE");
        return;
    }
    // Keep UVs relative to the texture. QueueAuthoredUiSprite applies the page-local VRAM
    // offset when it builds the GPU packet; adding it here too makes non-aligned textures
    // sample a second, unrelated offset within the same texture page.
    const ::byte4 tint = sprite->get_Color();
    if (tint.W == 0) return;
    psyqo::Color modulation;
    TryColor(tint, true, modulation);
    std::uint16_t bucket = 0;
    if (!TryMapRenderOrder(sprite->get_RenderOrder2D(), bucket)) {
        Reject("PS1_UI_SPRITE_ORDER_REJECTED");
        return;
    }
    PendingUiDrawable pending{};
    for (unsigned i = 0; i < 4; ++i) pending.Vertices[i] = vertices[i];
    pending.Texture = texture;
    pending.Color = modulation;
    pending.Bucket = bucket;
    TryMapAlphaToBlend(tint.W, pending.SemiTransparent);
    pending.IsSprite = true;
    AppendPending(pending);
}

void Ps1RenderManager2D::DrawText(::ITextDrawable2D*) {
    if (QueueOpen) ++SkippedTexts;
}

void Ps1RenderManager2D::UpdateTextureRegionCore(::RuntimeTexture*, int32_t, int32_t, int32_t, int32_t, Array<uint8_t>*, int32_t) {
    he_cpp_custom::Fail("PS1 does not update textures after upload");
}

}  // namespace helengine::ps1
