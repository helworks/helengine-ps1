#include "platform/ps1/Ps1RenderManager2D.hpp"

#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "Entity.hpp"
#include "FontAsset.hpp"
#include "FontChar.hpp"
#include "FontInfo.hpp"
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
#include "TextLayoutUtils.hpp"
#include "byte4.hpp"
#include "common/syscalls/syscalls.h"
#include "float3.hpp"
#include "float4.hpp"
#include "int2.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "platform/ps1/rendering/Ps1RuntimeTexture.hpp"
#include "platform/ps1/rendering/Ps1TextLayout.hpp"
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
    rotation = rightX == 1.0f && rightY == 0.0f ? 0.0f : ui_geometry_detail::Atan2(rightY, rightX);
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
    VisitedTexts = 0;
    QueuedGlyphQuads = 0;
    PendingCount = 0;
    for (CachedText& cached : TextCache) cached.InFrame = false;
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
    Pending[PendingCount] = drawable;
    PendingOrder[PendingCount] = &Pending[PendingCount];
    ++PendingCount;
    return true;
}

bool Ps1RenderManager2D::FlushPendingInVisitOrder() {
    for (std::uint16_t i = PendingCount; i > 0; --i) {
        const PendingUiDrawable& pending = *PendingOrder[i - 1u];
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
    if (!VramAllocator.TryAllocateWithinHorizontalPage(widthInWords, height, vramX, vramY)) {
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
    if (!VramAllocator.TryAllocateAligned(paletteEntries, 1, Ps1VramAllocator::ClutXAlignment, clutX, clutY)) {
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
    GlyphCache.Invalidate();
    for (CachedText& cached : TextCache) if (cached.Texture == texture) cached.Drawable = nullptr;
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
    const Ps1UiTriangle* triangles = nullptr;
    std::uint16_t count = 0;
    std::uint16_t fillCount = 0;
    bool reused = false;
    if (!RoundedRectCache.TryGet(position, {static_cast<float>(size.X), static_cast<float>(size.Y)}, scale,
                                 rotation + shape->get_Rotation(), shape->get_Radius(),
                                 shape->get_BorderThickness(), corners, ActiveViewport, triangles,
                                 count, fillCount, reused)) {
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

void Ps1RenderManager2D::DrawText(::ITextDrawable2D* text) {
    if (text == nullptr || text->get_Parent() == nullptr || !QueueOpen) return;
    ++VisitedTexts;
    ::FontAsset* font = text->get_Font();
    auto* texture = font == nullptr ? nullptr : dynamic_cast<Ps1RuntimeTexture*>(font->get_Texture());
    if (texture == nullptr || font->get_Characters() == nullptr || font->get_AtlasWidth() <= 0 ||
        font->get_AtlasHeight() <= 0) {
        Reject("PS1_UI_FONT_TEXTURE_UNSUPPORTED");
        return;
    }
    const std::uint16_t width = static_cast<std::uint16_t>(texture->get_Width());
    const std::uint16_t height = static_cast<std::uint16_t>(texture->get_Height());
    const std::uint16_t pageOffsetU = static_cast<std::uint16_t>((texture->VramX % 64u) * texture->PixelsPerWord());
    const std::uint16_t pageOffsetV = static_cast<std::uint16_t>(texture->VramY % 256u);
    if (pageOffsetU + width > 256u || pageOffsetV + height > 256u) {
        Reject("PS1_UI_FONT_CROSSES_PAGE");
        return;
    }
    Ps1UiPoint position{};
    Ps1UiPoint parentScale{};
    float rotation = 0.0f;
    if (!TryScreenTransform(text->get_Parent(), position, parentScale, rotation)) {
        Reject("PS1_UI_TEXT_TRANSFORM_REJECTED");
        return;
    }
    const float fontScale = text->get_FontScale() > 0.0001f ? text->get_FontScale() : 0.0001f;
    const ::int2 size = text->get_Size();
    HeCppString content = text->get_Text();
    if (text->get_WrapText()) {
        const int32_t wrapWidth = static_cast<int32_t>(ui_geometry_detail::Round(static_cast<float>(size.X) / fontScale));
        content = ::TextLayoutUtils::WrapText(content, font, wrapWidth > 0 ? wrapWidth : 1);
    }
    const float lineHeight = font->get_LineHeight() * fontScale > 1.0f ? font->get_LineHeight() * fontScale : 1.0f;
    const float spaceWidth = font->get_FontInfo() != nullptr && font->get_FontInfo()->get_SpaceWidth() * fontScale > 1.0f
        ? font->get_FontInfo()->get_SpaceWidth()
        : (font->get_LineHeight() * 0.25f * fontScale > 1.0f ? font->get_LineHeight() * 0.25f : 1.0f / fontScale);
    std::uint16_t bucket = 0;
    if (!TryMapRenderOrder(text->get_RenderOrder2D(), bucket)) {
        Reject("PS1_UI_TEXT_ORDER_REJECTED");
        return;
    }
    const ::float2 shadow = text->get_ShadowOffset();
    struct GlyphPass { float x; float y; ::byte4 color; psyqo::Color gpuColor; bool semiTransparent; } passes[6]{};
    unsigned passCount = 0;
    if (shadow.X != 0.0f || shadow.Y != 0.0f) passes[passCount++] = {shadow.X, shadow.Y, text->get_ShadowColor()};
    const float outline = text->get_OutlineScale();
    if (outline > 0.0f) {
        const ::byte4 color = text->get_OutlineColor();
        passes[passCount++] = {-outline, 0.0f, color};
        passes[passCount++] = {outline, 0.0f, color};
        passes[passCount++] = {0.0f, -outline, color};
        passes[passCount++] = {0.0f, outline, color};
    }
    passes[passCount++] = {0.0f, 0.0f, text->get_Color()};
    for (unsigned i = 0; i < passCount; ++i) {
        TryColor(passes[i].color, true, passes[i].gpuColor);
        TryMapAlphaToBlend(passes[i].color.W, passes[i].semiTransparent);
    }
    Ps1TextAlignment alignment = Ps1TextAlignment::Left;
    if (text->get_Alignment() == ::TextAlignment::Center) alignment = Ps1TextAlignment::Center;
    if (text->get_Alignment() == ::TextAlignment::Right) alignment = Ps1TextAlignment::Right;
    const auto lookup = [&](char character, Ps1TextGlyph& out) {
        return GlyphCache.Lookup(character, font, out, [&](char key, Ps1TextGlyph& cached) {
            ::FontChar glyph{};
            if (!font->get_Characters()->TryGetValue(key, glyph)) return false;
            const ::float4 source = glyph.SourceRect;
            Ps1GlyphTexelRect texels{};
            const bool sourceValid = TryResolveGlyphTexels({source.X, source.Y, source.Z, source.W},
                                                           {static_cast<float>(width), static_cast<float>(height)}, texels);
            cached = {source.Z * font->get_AtlasWidth(), source.W * font->get_AtlasHeight(),
                      glyph.OffsetY, glyph.AdvanceWidth, texels.x, texels.y, texels.width, texels.height, sourceValid};
            return true;
        });
    };
    // A text component usually keeps the same string for many frames. Cache the exact quads so
    // the PS1 does not redo software floating-point layout and glyph geometry on every VBlank.
    std::uint32_t key[33]{};
    const auto floatBits = [](float value) {
        std::uint32_t bits = 0;
        __builtin_memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    key[0] = static_cast<std::uint16_t>(ActiveViewport.x);
    key[1] = static_cast<std::uint16_t>(ActiveViewport.y);
    key[2] = static_cast<std::uint16_t>(ActiveViewport.width);
    key[3] = static_cast<std::uint16_t>(ActiveViewport.height);
    key[4] = static_cast<std::uint32_t>(ui_geometry_detail::Round(position.x));
    key[5] = static_cast<std::uint32_t>(ui_geometry_detail::Round(position.y));
    key[6] = floatBits(fontScale);
    key[7] = static_cast<std::uint32_t>(size.X);
    key[8] = floatBits(lineHeight);
    key[9] = floatBits(spaceWidth);
    key[10] = bucket;
    key[11] = static_cast<std::uint32_t>(alignment);
    key[12] = passCount;
    key[13] = width;
    key[14] = height;
    for (unsigned i = 0; i < passCount; ++i) {
        key[15 + i * 3] = floatBits(passes[i].x);
        key[16 + i * 3] = floatBits(passes[i].y);
        const ::byte4 c = passes[i].color;
        key[17 + i * 3] = static_cast<std::uint32_t>(c.X) | (static_cast<std::uint32_t>(c.Y) << 8u) |
                             (static_cast<std::uint32_t>(c.Z) << 16u) | (static_cast<std::uint32_t>(c.W) << 24u);
    }
    CachedText* cached = nullptr;
    for (CachedText& entry : TextCache) {
        if (entry.Drawable == text) { cached = &entry; break; }
    }
    if (cached == nullptr) {
        for (CachedText& entry : TextCache) {
            if (entry.Drawable == nullptr && !entry.InFrame) { cached = &entry; break; }
        }
    }
    if (cached == nullptr) {
        for (std::uint8_t i = 0; i < TextCacheCapacity; ++i) {
            CachedText& entry = TextCache[(NextTextCache + i) % TextCacheCapacity];
            if (!entry.InFrame) {
                cached = &entry;
                NextTextCache = static_cast<std::uint8_t>((NextTextCache + i + 1u) % TextCacheCapacity);
                break;
            }
        }
    }
    const bool baseMatches = cached != nullptr && cached->Drawable == text && cached->Font == font && cached->Texture == texture &&
                             __builtin_memcmp(cached->Key, key, sizeof(key)) == 0;
    bool contentMatches = baseMatches && cached->Content.size() == content.size() &&
                          __builtin_memcmp(cached->Content.data(), content.data(), content.size()) == 0;
    if (baseMatches && !contentMatches && !cached->InFrame && cached->Content.size() == content.size()) {
        unsigned visiblePasses = 0;
        for (unsigned pass = 0; pass < passCount; ++pass) if (passes[pass].color.W != 0) ++visiblePasses;
        std::uint16_t quad = 0;
        bool compatible = visiblePasses != 0;
        // Validate every changed glyph before editing the retained quads.
        for (std::size_t i = 0; compatible && i < content.size(); ++i) {
            const char oldChar = cached->Content[i];
            const char newChar = content[i];
            if (oldChar == ' ' || oldChar == '\n' || newChar == ' ' || newChar == '\n') {
                if (oldChar != newChar) compatible = false;
                continue;
            }
            Ps1TextGlyph oldGlyph{}, newGlyph{};
            if (!lookup(oldChar, oldGlyph) || !lookup(newChar, newGlyph)) { compatible = false; break; }
            if (oldChar != newChar && (oldGlyph.width != newGlyph.width || oldGlyph.height != newGlyph.height ||
                oldGlyph.offsetY != newGlyph.offsetY || oldGlyph.advance != newGlyph.advance || !newGlyph.sourceValid)) {
                compatible = false;
                break;
            }
            quad = static_cast<std::uint16_t>(quad + visiblePasses);
        }
        if (compatible && quad == cached->Count) {
            quad = 0;
            for (std::size_t i = 0; i < content.size(); ++i) {
                const char oldChar = cached->Content[i];
                const char newChar = content[i];
                if (oldChar == ' ' || oldChar == '\n') continue;
                if (oldChar != newChar) {
                    Ps1TextGlyph glyph{};
                    lookup(newChar, glyph);
                    const std::uint8_t u0 = static_cast<std::uint8_t>(glyph.sourceX);
                    const std::uint8_t v0 = static_cast<std::uint8_t>(glyph.sourceY);
                    const std::uint8_t u1 = static_cast<std::uint8_t>(glyph.sourceX + glyph.sourceWidth < 256 ?
                                                                      glyph.sourceX + glyph.sourceWidth : 255);
                    const std::uint8_t v1 = static_cast<std::uint8_t>(glyph.sourceY + glyph.sourceHeight < 256 ?
                                                                      glyph.sourceY + glyph.sourceHeight : 255);
                    for (unsigned pass = 0; pass < visiblePasses; ++pass) {
                        Ps1UiVertex* vertices = cached->Quads[quad + pass].Vertices;
                        vertices[0].u = vertices[2].u = u0;
                        vertices[1].u = vertices[3].u = u1;
                        vertices[0].v = vertices[1].v = v0;
                        vertices[2].v = vertices[3].v = v1;
                    }
                }
                quad = static_cast<std::uint16_t>(quad + visiblePasses);
            }
            cached->Content = content;
            contentMatches = true;
        }
    }
    if (baseMatches && contentMatches) {
        if (PendingCount + cached->Count > PendingUiCapacity) {
            Reject("PS1_UI_QUEUE_CAPACITY");
            return;
        }
        if (PendingSprites + cached->Count > Ps1FramePacketLimits::TexturedTriangles / 2u) {
            Reject("PS1_UI_SPRITE_CAPACITY");
            return;
        }
        for (std::uint16_t i = 0; i < cached->Count; ++i) PendingOrder[PendingCount + i] = &cached->Quads[i];
        PendingCount = static_cast<std::uint16_t>(PendingCount + cached->Count);
        PendingSprites = static_cast<std::uint16_t>(PendingSprites + cached->Count);
        QueuedGlyphQuads = static_cast<std::uint16_t>(QueuedGlyphQuads + cached->Count);
        cached->InFrame = true;
        return;
    }
    const std::uint16_t pendingStart = PendingCount;
    const auto emit = [&](char, const Ps1TextGlyph& glyph, float x, float y) {
        const Ps1GlyphTexelRect source{glyph.sourceX, glyph.sourceY, glyph.sourceWidth, glyph.sourceHeight};
        for (unsigned passIndex = 0; passIndex < passCount; ++passIndex) {
            const GlyphPass& pass = passes[passIndex];
            if (pass.color.W == 0) continue;
            PendingUiDrawable pending{};
            if (!glyph.sourceValid || !TryBuildGlyphQuadTexels({x + pass.x, y + pass.y}, {glyph.width * fontScale, glyph.height * fontScale},
                                    source,
                                    width, height, ActiveViewport,
                                    pending.Vertices)) {
                Reject("PS1_UI_GLYPH_GEOMETRY_REJECTED");
                return false;
            }
            pending.Texture = texture;
            pending.Color = pass.gpuColor;
            pending.Bucket = bucket;
            pending.SemiTransparent = pass.semiTransparent;
            pending.IsSprite = true;
            if (!AppendPending(pending)) return false;
            ++QueuedGlyphQuads;
        }
        return true;
    };
    const bool laidOut = LayoutPs1Text(content.data(), content.size(), static_cast<float>(ui_geometry_detail::Round(position.x)),
                       static_cast<float>(ui_geometry_detail::Round(position.y)), lineHeight, spaceWidth,
                       fontScale, static_cast<float>(size.X), alignment, lookup, emit);
    if (cached != nullptr && !cached->InFrame && laidOut && FailureMarker == nullptr && PendingCount - pendingStart <= 96u) {
        cached->Drawable = text;
        cached->Font = font;
        cached->Texture = texture;
        __builtin_memcpy(cached->Key, key, sizeof(key));
        cached->Content = content;
        cached->Count = static_cast<std::uint16_t>(PendingCount - pendingStart);
        for (std::uint16_t i = 0; i < cached->Count; ++i) cached->Quads[i] = Pending[pendingStart + i];
        cached->InFrame = true;
    }
    if (!laidOut && FailureMarker == nullptr) {
        Reject("PS1_UI_TEXT_LAYOUT_REJECTED");
    }
}

void Ps1RenderManager2D::UpdateTextureRegionCore(::RuntimeTexture*, int32_t, int32_t, int32_t, int32_t, Array<uint8_t>*, int32_t) {
    he_cpp_custom::Fail("PS1 does not update textures after upload");
}

}  // namespace helengine::ps1
