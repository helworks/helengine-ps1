#include "platform/ps1/Ps1RenderManager2D.hpp"
#include "Batch2DGeometry.hpp"
#include "SpriteQuad2D.hpp"

#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "Entity.hpp"
#include "FontAsset.hpp"
#include "FontChar.hpp"
#include "FontInfo.hpp"
#include "ICamera.hpp"
#include "IClipRegion2D.hpp"
#include "Component.hpp"
#include "IDrawable2D.hpp"
#include "IContentStreamSource.hpp"
#include "IRenderQueue2D.hpp"
#include "IRoundedRectDrawable2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "TextureAsset.hpp"
#include "TextureAssetColorFormat.hpp"
#include "TextureAssetAlphaPrecision.hpp"
#include "TextLayoutUtils.hpp"
#include "byte4.hpp"
#include "common/syscalls/syscalls.h"
#include "psyqo/alloc.h"
#include "float3.hpp"
#include "float4.hpp"
#include "int2.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "platform/ps1/Ps1FrameDiagnostics.hpp"
#include "platform/ps1/Ps1TextureUploadFence.hpp"
#if defined(PS1_CPU_PROFILE)
#include "platform/ps1/profiling/Ps1CpuProfileSink.hpp"
#endif
#include "platform/ps1/rendering/Ps1RuntimeTexture.hpp"
#include "platform/ps1/rendering/Ps1IndexedTextureUpload.hpp"
#include "platform/ps1/rendering/Ps1TextureTransportBuffer.hpp"
#include "platform/ps1/rendering/Ps1MutableTexture.hpp"
#include "platform/ps1/rendering/Ps1VramDiagnostics.hpp"
#include "platform/ps1/rendering/Ps1TextLayout.hpp"
#include "runtime/array.hpp"
#include "runtime/native_cast.hpp"
#include "system/io/stream.hpp"


namespace helengine::ps1 {
extern "C" {
volatile Ps1VramDiagnostics helengine_ps1_vram_diagnostics;
volatile Ps1TextureTransportDiagnostics helengine_ps1_texture_transport_diagnostics{0x48455442u, 0, 0, 0, 0, 0, 0, 0};
}
namespace {

/// <summary>Allocates an eight-byte aligned retained shadow through PSYQo's nullable allocator.</summary>
/// <param name="bytes">Packed direct-color storage size validated before allocation.</param>
/// <returns>Owned storage, or null so the caller can roll back its VRAM reservation.</returns>
void* AllocateMutableTexturePixels(std::size_t bytes) { return psyqo_malloc(bytes); }

/// <summary>Releases the retained shadow after its synchronous upload has finished.</summary>
/// <param name="pixels">Storage allocated by AllocateMutableTexturePixels.</param>
void ReleaseMutableTexturePixels(void* pixels) { psyqo_free(pixels); }

void publishVram(const Ps1VramAllocator& allocator) {
    auto& d=helengine_ps1_vram_diagnostics;
    d.UsedWords=allocator.UsedWords(); d.LiveRegions=allocator.LiveCount();
    for(std::uint16_t i=0;i<Ps1VramAllocator::Capacity;++i) {
        const auto& r=allocator.GetRegion(i);
        d.Regions[i].x=r.x;d.Regions[i].y=r.y;d.Regions[i].width=r.width;d.Regions[i].height=r.height;
    }
    d.Serial=d.Serial+1;
}

/// <summary>Palette entries the GPU reads for each supported depth.</summary>
constexpr std::uint16_t FourBitPaletteEntries = 16;
constexpr std::uint16_t EightBitPaletteEntries = 256;
constexpr std::int16_t DisplayWidth = 320;
constexpr std::int16_t DisplayHeight = 240;

#if defined(PS1_CPU_PROFILE)
class ScopedUiCpuSample {
public:
    ScopedUiCpuSample(Ps1CpuProfileSink* sink, std::uint64_t& total)
        : Sink(sink), Total(total), Start(sink == nullptr ? 0u : sink->ReadMicroseconds()) {}
    ~ScopedUiCpuSample() {
        if (Sink == nullptr) return;
        const std::uint64_t end = Sink->ReadMicroseconds();
        if (end < Start) Sink->ClockFault();
        else Total += end - Start;
    }

private:
    Ps1CpuProfileSink* Sink;
    std::uint64_t& Total;
    std::uint64_t Start;
};
class ScopedNativeStage {
public:
    ScopedNativeStage(Ps1CpuProfileSink* sink, std::uint32_t stage)
        : Sink(sink), Stage(stage), Start(sink == nullptr ? 0u : sink->ReadMicroseconds()) {}
    ~ScopedNativeStage() {
        if (Sink == nullptr) return;
        const auto end = Sink->ReadMicroseconds();
        if (end >= Start) Sink->RecordNativeStage(Stage, end - Start);
        else Sink->ClockFault();
    }
private:
    Ps1CpuProfileSink* Sink;
    std::uint32_t Stage;
    std::uint64_t Start;
};
#endif

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

bool TryScreenTransform(const ::float3& worldPosition, const ::float3& worldScale,
                        const ::float4& orientation, Ps1UiPoint& position,
                        Ps1UiPoint& scale, float& rotation) {
    const float rightX = 1.0f - 2.0f * (orientation.Y * orientation.Y + orientation.Z * orientation.Z);
    const float rightY = 2.0f * (orientation.X * orientation.Y + orientation.W * orientation.Z);
    position = {worldPosition.X, worldPosition.Y};
    scale = {worldScale.X, worldScale.Y};
    rotation = rightX == 1.0f && rightY == 0.0f ? 0.0f : ui_geometry_detail::Atan2(rightY, rightX);
    return ui_geometry_detail::Finite(position.x) && ui_geometry_detail::Finite(position.y) &&
           ui_geometry_detail::Finite(scale.x) && ui_geometry_detail::Finite(scale.y) &&
           ui_geometry_detail::Finite(rotation);
}

bool TryScreenTransform(::Entity* entity, Ps1UiPoint& position, Ps1UiPoint& scale, float& rotation) {
    if (entity == nullptr) return false;
    return TryScreenTransform(entity->get_Position(), entity->get_Scale(), entity->get_Orientation(),
                              position, scale, rotation);
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
#if defined(PS1_CPU_PROFILE)
    ScopedNativeStage totalSample(CpuProfile, 111);
#endif
    VisitedSprites = 0;
    VisitedShapes = 0;
    SkippedTexts = 0;
    VisitedTexts = 0;
    QueuedGlyphQuads = 0;
    RequestedShapeTriangles = 0;
    PendingCount = 0;
    HasUiBounds = false;
    UiMinY = 0;
    UiMaxY = 0;
    HasSpriteBounds = HasShapeBounds = HasTextBounds = false;
    SpriteMinY = SpriteMaxY = ShapeMinY = ShapeMaxY = TextMinY = TextMaxY = 0;
#if defined(PS1_CPU_PROFILE)
    UiTextDrawUs = UiSpriteDrawUs = UiShapeDrawUs = UiSpriteGeometryUs = 0;
    UiSpritePacketUs = UiShapePacketUs = 0;
#endif
    for (CachedText& cached : TextCache) cached.InFrame = false;
    PendingShapes = 0;
    PendingSprites = 0;
    FailureMarker = nullptr;
    if (!ResolveViewport(camera, ActiveViewport)) {
        Reject("PS1_UI_VIEWPORT_UNSUPPORTED");
        return false;
    }
    ::IRenderQueue2D* queue = camera->get_RenderQueue2D();
    if (queue == nullptr) {
        Reject("PS1_UI_QUEUE_MISSING");
        return false;
    }
    const std::int32_t version = queue->get_Version();
    UseValidatedQueue = queue == ValidatedQueue && version == ValidatedQueueVersion;
    QueueOpen = true;
#if defined(PS1_CPU_PROFILE)
    const std::uint64_t visitStartUs = CpuProfile == nullptr ? 0u : CpuProfile->ReadMicroseconds();
#endif
    if (!UseValidatedQueue) {
        // Font atlases can be 256 rows tall and must start at a vertical texture-page boundary.
        // Load them before sprites consume VRAM shelves; this prepass runs only when the queue's
        // membership changes, and the actual draw pass below keeps the original visit order.
        PreloadingTextTextures = true;
        queue->VisitOrdered(this);
        PreloadingTextTextures = false;
    }
    queue->VisitOrdered(this);
    HasLoggedFirstUiGeometry = true;
#if defined(PS1_CPU_PROFILE)
    if (CpuProfile != nullptr) {
        const std::uint64_t visitEndUs = CpuProfile->ReadMicroseconds();
        if (visitEndUs >= visitStartUs) CpuProfile->RecordNativeStage(Ps1CpuProfileSink::UiQueueVisit, visitEndUs - visitStartUs);
        else CpuProfile->ClockFault();
    }
#endif
    QueueOpen = false;
#if defined(PS1_CPU_PROFILE)
    if (CpuProfile != nullptr) {
        CpuProfile->RecordNativeStage(Ps1CpuProfileSink::UiTextDraw, UiTextDrawUs);
        CpuProfile->RecordNativeStage(Ps1CpuProfileSink::UiSpriteDraw, UiSpriteDrawUs);
        CpuProfile->RecordNativeStage(Ps1CpuProfileSink::UiSpriteGeometry, UiSpriteGeometryUs);
        CpuProfile->RecordNativeStage(Ps1CpuProfileSink::UiShapeDraw, UiShapeDrawUs);
    }
#endif
    if (queue->get_Version() != version) {
        Reject("PS1_UI_QUEUE_CHANGED_DURING_DRAW");
    } else if (FailureMarker == nullptr && !UseValidatedQueue) {
        // RenderList2D advances its version whenever membership changes. A stable list contains
        // the same concrete drawables, so later frames can skip repeated RTTI checks.
        ValidatedQueue = queue;
        ValidatedQueueVersion = version;
    }
    const bool flushed = FailureMarker == nullptr && FlushPendingInVisitOrder();
#if defined(PS1_CPU_PROFILE)
    if (CpuProfile != nullptr) {
        CpuProfile->RecordNativeStage(Ps1CpuProfileSink::UiSpritePacket, UiSpritePacketUs);
        CpuProfile->RecordNativeStage(Ps1CpuProfileSink::UiShapePacket, UiShapePacketUs);
    }
#endif
    return flushed;
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
    IncludeBounds(drawable);
    ++PendingCount;
    return true;
}

void Ps1RenderManager2D::IncludeBounds(PendingKind kind, std::int16_t minimum, std::int16_t maximum) {
    if (!HasUiBounds) { UiMinY = minimum; UiMaxY = maximum; HasUiBounds = true; }
    else { if (minimum < UiMinY) UiMinY = minimum; if (maximum > UiMaxY) UiMaxY = maximum; }
    bool* has = kind == PendingKind::Text ? &HasTextBounds : kind == PendingKind::Sprite ? &HasSpriteBounds : &HasShapeBounds;
    auto* low = kind == PendingKind::Text ? &TextMinY : kind == PendingKind::Sprite ? &SpriteMinY : &ShapeMinY;
    auto* high = kind == PendingKind::Text ? &TextMaxY : kind == PendingKind::Sprite ? &SpriteMaxY : &ShapeMaxY;
    if (!*has) { *low = minimum; *high = maximum; *has = true; }
    else { if (minimum < *low) *low = minimum; if (maximum > *high) *high = maximum; }
}

void Ps1RenderManager2D::IncludeBounds(const PendingUiDrawable& drawable) {
    std::int16_t minimum = drawable.Vertices[0].y, maximum = minimum;
    const unsigned count = drawable.Kind == PendingKind::Shape ? 3u : 4u;
    for (unsigned i = 1; i < count; ++i) {
        const auto y = drawable.Vertices[i].y;
        if (y < minimum) minimum = y;
        if (y > maximum) maximum = y;
    }
    IncludeBounds(drawable.Kind, minimum, maximum);
}

bool Ps1RenderManager2D::AppendCachedText(CachedText& cached) {
#if defined(PS1_CPU_PROFILE)
    ScopedNativeStage cacheSample(CpuProfile, 115);
#endif
    if (PendingCount + cached.Count > PendingUiCapacity) {
        Reject("PS1_UI_QUEUE_CAPACITY");
        return false;
    }
    if (PendingSprites + cached.Count > Ps1FramePacketLimits::TexturedTriangles / 2u) {
        Reject("PS1_UI_SPRITE_CAPACITY");
        return false;
    }
    for (std::uint16_t i = 0; i < cached.Count; ++i) PendingOrder[PendingCount + i] = &cached.Quads[i];
    PendingCount = static_cast<std::uint16_t>(PendingCount + cached.Count);
    PendingSprites = static_cast<std::uint16_t>(PendingSprites + cached.Count);
    QueuedGlyphQuads = static_cast<std::uint16_t>(QueuedGlyphQuads + cached.Count);
    cached.InFrame = true;
    if (cached.Count != 0) IncludeBounds(PendingKind::Text, cached.MinY, cached.MaxY);
    return true;
}

std::uint32_t Ps1RenderManager2D::AllocatePacketRevision() {
    if (++NextPacketRevision == 0) ++NextPacketRevision;
    return NextPacketRevision;
}

bool Ps1RenderManager2D::FlushPendingInVisitOrder() {
#if defined(PS1_CPU_PROFILE)
    const std::uint64_t flushStartUs = CpuProfile == nullptr ? 0u : CpuProfile->ReadMicroseconds();
    bool runActive = false;
    bool runIsSprite = false;
    std::uint64_t runStartUs = 0;
    const auto closeRun = [&]() {
        if (CpuProfile == nullptr || !runActive) return;
        const std::uint64_t endUs = CpuProfile->ReadMicroseconds();
        if (endUs < runStartUs) CpuProfile->ClockFault();
        else if (runIsSprite) UiSpritePacketUs += endUs - runStartUs;
        else UiShapePacketUs += endUs - runStartUs;
        runActive = false;
    };
#endif
    for (std::uint16_t i = PendingCount; i > 0; --i) {
        const PendingUiDrawable& pending = *PendingOrder[i - 1u];
#if defined(PS1_CPU_PROFILE)
        const bool packetIsSprite = pending.IsSprite && pending.Texture != nullptr;
        if (CpuProfile != nullptr && (!runActive || runIsSprite != packetIsSprite)) {
            closeRun();
            runStartUs = CpuProfile->ReadMicroseconds();
            runIsSprite = packetIsSprite;
            runActive = true;
        }
#endif
        bool queued = false;
        if (pending.IsSprite && pending.Texture != nullptr) {
            const Ps1RuntimeTexture& texture = *pending.Texture;
            queued = Renderer.QueueAuthoredUiSprite(pending.Vertices, texture.VramX, texture.VramY, texture.ClutX,
                                                     texture.ClutY, texture.PixelsPerWord(),
                                                     pending.Color, pending.SemiTransparent, pending.Bucket,
                                                     pending.PacketRevision == 0 ? nullptr : &pending,
                                                     pending.PacketRevision);
        } else {
            const Ps1UiTriangle triangle{pending.Vertices[0], pending.Vertices[1], pending.Vertices[2]};
            queued = Renderer.QueueAuthoredUiTriangle(triangle, pending.Color, pending.SemiTransparent, pending.Bucket);
        }
        if (!queued) {
#if defined(PS1_CPU_PROFILE)
            closeRun();
#endif
            Reject(Renderer.Stats().failure == Ps1RenderFailure::Capacity ? "PS1_UI_PACKET_CAPACITY"
                                                                          : "PS1_UI_FRAME_OWNERSHIP");
#if defined(PS1_CPU_PROFILE)
            if (CpuProfile != nullptr) {
                const std::uint64_t flushEndUs = CpuProfile->ReadMicroseconds();
                if (flushEndUs >= flushStartUs) CpuProfile->RecordNativeStage(Ps1CpuProfileSink::UiPacketFlush, flushEndUs - flushStartUs);
                else CpuProfile->ClockFault();
            }
#endif
            return false;
        }
    }
#if defined(PS1_CPU_PROFILE)
    closeRun();
    if (CpuProfile != nullptr) {
        const std::uint64_t flushEndUs = CpuProfile->ReadMicroseconds();
        if (flushEndUs >= flushStartUs) CpuProfile->RecordNativeStage(Ps1CpuProfileSink::UiPacketFlush, flushEndUs - flushStartUs);
        else CpuProfile->ClockFault();
    }
#endif
    return true;
}

void Ps1RenderManager2D::Visit(::IDrawable2D* drawable) {
    if (drawable == nullptr || !QueueOpen || drawable->get_Parent() == nullptr ||
        !drawable->get_Parent()->get_IsHierarchyEnabled()) return;
    if (PreloadingTextTextures) {
        auto* text = dynamic_cast<::ITextDrawable2D*>(drawable);
        if (text != nullptr) {
            ::FontAsset* font = text->get_Font();
            if (font != nullptr) (void)font->get_Texture();
        }
        return;
    }
    if (UseValidatedQueue || dynamic_cast<::ISpriteDrawable2D*>(drawable) != nullptr ||
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
    MarkFrameStage(Ps1FrameStage::TextureOpenBefore);
    ::Stream* stream = contentStreamSource->OpenRead(cookedAssetPath);
    MarkFrameStage(Ps1FrameStage::TextureOpenAfter);
    if (stream == nullptr) {
        he_cpp_custom::Fail("PS1 content source returned a null stream for a cooked texture");
    }
    MarkFrameStage(Ps1FrameStage::TextureDeserializeBefore);
    ::Asset* rawAsset = ::AssetSerializer::Deserialize(stream);
    MarkFrameStage(Ps1FrameStage::TextureDeserializeAfter);
    delete stream;
    ::TextureAsset* asset = he_cpp_try_cast<::TextureAsset>(rawAsset);
    if (asset == nullptr) {
        he_cpp_custom::Fail("PS1 cooked texture asset has the wrong type");
    }
    ::RuntimeTexture* texture = UploadTexture(asset);
    asset->Dispose();
    delete asset;
    return texture;
}

::RuntimeTexture* Ps1RenderManager2D::BuildTextureFromRaw(::TextureAsset* data) {
    if (data == nullptr) {
        he_cpp_custom::Fail("PS1 texture creation received a null texture asset");
    }
    if (data->ColorFormat == ::TextureAssetColorFormat::Rgba32) return BuildMutableRgbaTexture(data);
    return UploadTexture(data);
}

::RuntimeTexture* Ps1RenderManager2D::BuildMutableRgbaTexture(::TextureAsset* asset) {
    MarkFrameStage(Ps1FrameStage::TextureUploadEnter);
    const unsigned width = asset->Width;
    const unsigned height = asset->Height;
    const bool opaque = asset->AlphaPrecision == ::TextureAssetAlphaPrecision::Opaque;
    if (!width || !height || width > 256u || height > 256u || asset->Colors == nullptr ||
        asset->Colors->Length != static_cast<int32_t>(width * height * 4u) ||
        (!opaque && asset->AlphaPrecision != ::TextureAssetAlphaPrecision::Binary &&
         asset->AlphaPrecision != ::TextureAssetAlphaPrecision::A8)) {
        he_cpp_custom::Fail("PS1 mutable RGBA texture requires 1..256 texels and exact RGBA32 opaque/binary/A8 pixels");
    }
    auto* texture = new Ps1RuntimeTexture();
    std::uint16_t* pixels = nullptr;
    std::uint16_t stride = 0, vramX = 0, vramY = 0;
    if (!AllocatePs1MutableTexture(VramAllocator, width, height,
            AllocateMutableTexturePixels, pixels, stride, vramX, vramY)) {
        delete texture;
        publishVram(VramAllocator);
        he_cpp_custom::Fail("PS1 mutable texture allocation failed");
    }
    texture->set_Width(static_cast<std::uint16_t>(width));
    texture->set_Height(static_cast<std::uint16_t>(height));
    texture->VramX = vramX;
    texture->VramY = vramY;
    texture->WidthInWords = stride;
    texture->PixelDepth = Ps1RuntimeTexture::Depth::SixteenBit;
    texture->MutablePixels = pixels;
    texture->MutableOwner = this;
    texture->MutableOpaque = opaque;
    texture->ReleaseMutablePixels = ReleaseMutableTexturePixels;
    UpdatePs1MutableTexture(Gpu, pixels, stride, vramX, vramY, 0, 0, static_cast<int>(width),
                           static_cast<int>(height), asset->Colors->Data, static_cast<int>(width * 4u), opaque);
    ++UploadedTextures;
    publishVram(VramAllocator);
    return texture;
}

::RuntimeTexture* Ps1RenderManager2D::UploadTexture(::TextureAsset* asset) {
    MarkFrameStage(Ps1FrameStage::TextureUploadEnter);
    const bool fourBit = asset->ColorFormat == ::TextureAssetColorFormat::Indexed4;
    const bool eightBit = asset->ColorFormat == ::TextureAssetColorFormat::Indexed8;
    const bool directColor = asset->ColorFormat == ::TextureAssetColorFormat::Ps1Bgr555;
    if (!fourBit && !eightBit && !directColor) {
        ramsyscall_printf("PS1_TEXTURE_UNSUPPORTED_FORMAT format=%d\n", static_cast<int>(asset->ColorFormat));
        he_cpp_custom::Fail("PS1 textures must be cooked to Indexed4, Indexed8 or Ps1Bgr555");
    }
    if (asset->Colors == nullptr || (!directColor && asset->PaletteColors == nullptr)) {
        he_cpp_custom::Fail("PS1 cooked texture is missing its pixels or its palette");
    }

    const std::uint16_t pixelsPerWord = fourBit ? 4u : eightBit ? 2u : 1u;
    const std::uint16_t width = asset->Width;
    const std::uint16_t height = asset->Height;
    if (width == 0 || height == 0 || width > 256 || height > 256) {
        ramsyscall_printf("PS1_TEXTURE_UNSUPPORTED_SIZE width=%u height=%u\n", static_cast<unsigned>(width),
                          static_cast<unsigned>(height));
        he_cpp_custom::Fail("PS1 textures must have dimensions from 1 to 256 texels");
    }

    // Indexed rows are transferred as 16-bit VRAM words. Keep the authored logical width (which
    // may be odd for a pre-scaled icon) and round only the transport width up to a complete word.
    const std::uint16_t widthInWords = static_cast<std::uint16_t>((width + pixelsPerWord - 1u) / pixelsPerWord);
    std::uint16_t vramX = 0;
    std::uint16_t vramY = 0;
    MarkFrameStage(Ps1FrameStage::TextureVramBefore, static_cast<std::uint32_t>(widthInWords) * height);
    if (!VramAllocator.TryAllocateWithinHorizontalPage(widthInWords, height, pixelsPerWord, vramX, vramY)) {
        helengine_ps1_vram_diagnostics.FailedWidth=widthInWords;
        helengine_ps1_vram_diagnostics.FailedHeight=height;
        publishVram(VramAllocator);
        ramsyscall_printf("PS1_TEXTURE_VRAM_EXHAUSTED words=%u used=%u\n",
                          static_cast<unsigned>(widthInWords) * static_cast<unsigned>(height),
                          static_cast<unsigned>(VramAllocator.UsedWords()));
        he_cpp_custom::Fail("PS1 ran out of VRAM for a cooked texture");
    }

    MarkFrameStage(Ps1FrameStage::TextureVramAfter);
    // Indexed pixels are stored row-major by the cook. Pad each row to a complete 16-bit VRAM
    // word for odd authored widths while keeping the runtime texture's logical width unchanged.
    const std::uint32_t expectedBytes = static_cast<std::uint32_t>(widthInWords) * height * 2u;
    const std::uint32_t sourceRowBytes = static_cast<std::uint32_t>(width) * (directColor ? 2u : 1u);
    const std::uint32_t pixelCount = static_cast<std::uint32_t>(width) * height;
    const std::uint32_t sourceBytes = fourBit ? (pixelCount + 1u) / 2u : sourceRowBytes * height;
    if (static_cast<std::uint32_t>(asset->Colors->Length) != sourceBytes) {
        ramsyscall_printf("PS1_TEXTURE_PIXEL_LENGTH expected=%u actual=%u\n", static_cast<unsigned>(sourceBytes),
                          static_cast<unsigned>(asset->Colors->Length));
        he_cpp_custom::Fail("PS1 cooked texture pixel length does not match its declared size");
    }
    MarkFrameStage(Ps1FrameStage::TextureBufferBefore, expectedBytes);
    auto& transport = helengine_ps1_texture_transport_diagnostics;
    transport.Sequence = transport.Sequence + 1;
    transport.ExpectedBytes = expectedBytes;
    transport.AssetAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(asset));
    transport.BufferAddress = 0;
    transport.AllocationResult = static_cast<std::uint32_t>(Ps1TextureTransportAllocation::Pending);
    transport.VramWordsBefore = VramAllocator.UsedWords();
    transport.VramWordsAfter = transport.VramWordsBefore;
    transport.Sequence = transport.Sequence + 1;
    Ps1TextureTransportBuffer upload;
    const auto allocation = PreparePs1TextureTransportBuffer(
        VramAllocator, vramX, vramY, widthInWords, height, width, pixelsPerWord,
        &(*asset->Colors)[0], sourceBytes, expectedBytes,
        [](std::size_t bytes) { return static_cast<std::uint8_t*>(psyqo_malloc(bytes)); }, upload);
    transport.Sequence = transport.Sequence + 1;
    transport.BufferAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(upload.Data));
    transport.AllocationResult = static_cast<std::uint32_t>(allocation);
    transport.VramWordsAfter = VramAllocator.UsedWords();
    transport.Sequence = transport.Sequence + 1;
    MarkFrameStage(Ps1FrameStage::TextureBufferAfter, expectedBytes);
    if (allocation != Ps1TextureTransportAllocation::Allocated && allocation != Ps1TextureTransportAllocation::Borrowed) {
        publishVram(VramAllocator);
        if (allocation == Ps1TextureTransportAllocation::RollbackFailed) {
            he_cpp_custom::Fail("PS1 texture transport allocation rollback failed");
        }
        he_cpp_custom::Fail("PS1 texture transport buffer allocation failed");
    }
    std::uint8_t* uploadBytes = upload.Data;
    const std::uint32_t uploadRowBytes = static_cast<std::uint32_t>(widthInWords) * 2u;
    if (upload.OwnsData) {
        __builtin_memset(uploadBytes, 0, expectedBytes);
        for (std::uint16_t row = 0; row < height; ++row) {
            auto* destination = uploadBytes + static_cast<std::uint32_t>(row) * uploadRowBytes;
            if (fourBit) {
                // Indexed4 is packed continuously, even across odd-width rows.
                PackPs1Indexed4UploadRow(&(*asset->Colors)[0], static_cast<std::uint32_t>(row) * width, width, destination);
            } else {
                __builtin_memcpy(destination, &(*asset->Colors)[static_cast<std::uint32_t>(row) * sourceRowBytes], sourceRowBytes);
            }
        }
    }
    MarkFrameStage(Ps1FrameStage::TexturePackAfter);
    psyqo::Rect pixelRegion;
    pixelRegion.pos = {{.x = static_cast<std::int16_t>(vramX), .y = static_cast<std::int16_t>(vramY)}};
    pixelRegion.size = {{.w = static_cast<std::int16_t>(widthInWords), .h = static_cast<std::int16_t>(height)}};
    // flip() can still be transferring the previous frame when a newly loaded scene
    // asks for textures. PSYQo permits only one GPU DMA callback at a time.
    WaitForTextureUpload(Gpu);
    MarkFrameStage(Ps1FrameStage::UploadBefore, 1);
    Gpu.uploadToVRAM(reinterpret_cast<const std::uint16_t*>(uploadBytes), pixelRegion);
    MarkFrameStage(Ps1FrameStage::UploadAfter, 1);
    ReleasePs1TextureTransportBuffer(upload, [](std::uint8_t* bytes) { psyqo_free(bytes); });

    const std::uint16_t paletteEntries = directColor ? 0u : fourBit ? FourBitPaletteEntries : EightBitPaletteEntries;
    const std::uint32_t suppliedEntries = directColor ? 0u : static_cast<std::uint32_t>(asset->PaletteColors->Length) / 4u;
    std::uint16_t clutX = 0;
    std::uint16_t clutY = 0;
    if (!directColor && !VramAllocator.TryAllocateAligned(paletteEntries, 1, Ps1VramAllocator::ClutXAlignment, clutX, clutY)) {
        VramAllocator.Release(vramX,vramY,widthInWords,height);
        helengine_ps1_vram_diagnostics.FailedWidth=paletteEntries;
        helengine_ps1_vram_diagnostics.FailedHeight=1;
        publishVram(VramAllocator);
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
    if (!directColor) {
        MarkFrameStage(Ps1FrameStage::UploadBefore, 2);
        Gpu.uploadToVRAM(palette, paletteRegion);
        MarkFrameStage(Ps1FrameStage::UploadAfter, 2);
    }

    Ps1RuntimeTexture* texture = new Ps1RuntimeTexture();
    texture->set_Width(width);
    texture->set_Height(height);
    texture->VramX = vramX;
    texture->VramY = vramY;
    texture->ClutX = clutX;
    texture->ClutY = clutY;
    texture->WidthInWords=widthInWords;
    texture->PaletteEntries=paletteEntries;
    texture->PixelDepth = fourBit ? Ps1RuntimeTexture::Depth::FourBit : eightBit ? Ps1RuntimeTexture::Depth::EightBit : Ps1RuntimeTexture::Depth::SixteenBit;
    ++UploadedTextures;
    publishVram(VramAllocator);
    ramsyscall_printf("PS1_TEXTURE_UPLOADED w=%u h=%u bpp=%u vram=%u,%u clut=%u,%u used_words=%u\n",
                      static_cast<unsigned>(width), static_cast<unsigned>(height), fourBit ? 4u : eightBit ? 8u : 16u,
                      static_cast<unsigned>(vramX), static_cast<unsigned>(vramY), static_cast<unsigned>(clutX),
                      static_cast<unsigned>(clutY), static_cast<unsigned>(VramAllocator.UsedWords()));
    return texture;
}

void Ps1RenderManager2D::ReleaseTexture(::RuntimeTexture* texture) {
    if (texture == nullptr) { return; }
    auto* native=dynamic_cast<Ps1RuntimeTexture*>(texture);
    if(native==nullptr || QueueOpen) he_cpp_custom::Fail("PS1 texture release outside safe frame boundary");
    // Complete prior DMA and GPU commands before another resource can upload
    // into these regions. This waits through PSYQo's normal callback pump.
    MarkFrameStage(Ps1FrameStage::ReleaseChainBefore);
    Gpu.waitChainIdle();
    MarkFrameStage(Ps1FrameStage::ReleaseChainAfter);
    MarkFrameStage(Ps1FrameStage::ReleaseReadyBefore);
    Gpu.waitReady();
    MarkFrameStage(Ps1FrameStage::ReleaseReadyAfter);
    auto& diagnostics=helengine_ps1_vram_diagnostics;
    diagnostics.BeforeReleaseWords=VramAllocator.UsedWords();
    if(!VramAllocator.Release(native->VramX,native->VramY,native->WidthInWords,native->get_Height()) ||
       (native->PaletteEntries != 0 && !VramAllocator.Release(native->ClutX,native->ClutY,native->PaletteEntries,1)))
        he_cpp_custom::Fail("PS1 texture VRAM reservation ownership mismatch");
    diagnostics.AfterReleaseWords=VramAllocator.UsedWords();
    diagnostics.Releases=diagnostics.Releases+1;
    publishVram(VramAllocator);
    GlyphCache.Invalidate();
    for (CachedText& cached : TextCache) if (cached.Texture == texture) cached.Drawable = nullptr;
    texture->Dispose();
    delete texture;
    MarkFrameStage(Ps1FrameStage::ReleaseAfter);
}

void Ps1RenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D* shape) {
    if (shape == nullptr || shape->get_Parent() == nullptr || !QueueOpen) return;
#if defined(PS1_CPU_PROFILE)
    ScopedUiCpuSample cpuSample(CpuProfile, UiShapeDrawUs);
#endif
    ++VisitedShapes;
    Ps1UiPoint position{};
    Ps1UiPoint scale{};
    float rotation = 0.0f;
    if (!TryScreenTransform(shape->get_Parent(), position, scale, rotation)) {
        Reject("PS1_UI_SHAPE_TRANSFORM_REJECTED");
        return;
    }
    const ::int2 size = shape->get_Size();
    Ps1UiTriangle triangles[10]{};
    std::uint16_t count = 0;
    std::uint16_t fillCount = 0;
    if (!TryBuildRectTriangles(position, {static_cast<float>(size.X), static_cast<float>(size.Y)}, scale,
                               rotation + shape->get_Rotation(), shape->get_BorderThickness(), ActiveViewport,
                               triangles, 10, count, fillCount)) {
        Reject("PS1_UI_SHAPE_GEOMETRY_REJECTED");
        return;
    }
    if (count == 0) return;
    if (!HasLoggedFirstUiGeometry) {
        std::int16_t minimumY = 32767;
        std::int16_t maximumY = -32768;
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::int16_t ys[3] = {triangles[i].a.y, triangles[i].b.y, triangles[i].c.y};
            for (const std::int16_t y : ys) {
                if (y < minimumY) minimumY = y;
                if (y > maximumY) maximumY = y;
            }
        }
        ramsyscall_printf("PS1_UI_SHAPE y=%d..%d size=%d,%d screen_y=%d scale_y=%d order=%u\n",
                          static_cast<int>(minimumY), static_cast<int>(maximumY), size.X, size.Y,
                          static_cast<int>(position.y), static_cast<int>(scale.y),
                          static_cast<unsigned>(PendingCount));
    }
    const ::byte4 fill = shape->get_FillColor();
    const ::byte4 border = shape->get_BorderColor();
    const std::uint32_t requestedTriangles =
        (fill.W == 0 ? 0u : static_cast<std::uint32_t>(fillCount)) +
        (border.W == 0 ? 0u : static_cast<std::uint32_t>(count - fillCount));
    RequestedShapeTriangles = requestedTriangles > 0xFFFFFFFFu - RequestedShapeTriangles
        ? 0xFFFFFFFFu : RequestedShapeTriangles + requestedTriangles;
    psyqo::Color fillColor;
    psyqo::Color borderColor;
    TryColor(fill, false, fillColor);
    TryColor(border, false, borderColor);
    bool fillSemiTransparent = false;
    bool borderSemiTransparent = false;
    TryMapAlphaToBlend(fill.W, fillSemiTransparent);
    TryMapAlphaToBlend(border.W, borderSemiTransparent);
    // Core's VisitOrdered supplies world-depth and hierarchy order. All UI packets
    // share one OT bucket and FlushPendingInVisitOrder reverses insertion to retain it.
    const std::uint16_t bucket = 0;
    for (std::uint16_t i = 0; i < count; ++i) {
        const bool isFill = i < fillCount;
        const std::uint8_t alpha = isFill ? fill.W : border.W;
        if (alpha == 0) continue;
        PendingUiDrawable pending{};
        pending.Kind = PendingKind::Shape;
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
    ::Entity* parent = sprite == nullptr ? nullptr : sprite->get_Parent();
    if (parent == nullptr || !QueueOpen) return;
#if defined(PS1_CPU_PROFILE)
    ScopedUiCpuSample cpuSample(CpuProfile, UiSpriteDrawUs);
#endif
    ++VisitedSprites;
    auto* texture = dynamic_cast<Ps1RuntimeTexture*>(sprite->get_Texture());
    if (texture == nullptr) {
        Reject("PS1_UI_SPRITE_TEXTURE_UNSUPPORTED");
        return;
    }
    const std::uint16_t width = static_cast<std::uint16_t>(texture->get_Width());
    const std::uint16_t height = static_cast<std::uint16_t>(texture->get_Height());
#if defined(PS1_CPU_PROFILE)
    const std::uint64_t geometryStartUs = CpuProfile == nullptr ? 0u : CpuProfile->ReadMicroseconds();
#endif
    const ::SpriteQuad2D quad = ::Batch2DGeometry::ResolveSpriteQuad(sprite);
    const ::float4 source = sprite->get_SourceRect();
    Ps1SpriteGeometryKey geometryKey{};
    geometryKey.Drawable = sprite;
    geometryKey.Texture = texture;
    geometryKey.Source = {source.X, source.Y, source.Z, source.W};
    geometryKey.TextureWidth = width;
    geometryKey.TextureHeight = height;
    geometryKey.Viewport = ActiveViewport;
    geometryKey.Corners[0] = {quad.TopLeft.X, quad.TopLeft.Y};
    geometryKey.Corners[1] = {quad.TopRight.X, quad.TopRight.Y};
    geometryKey.Corners[2] = {quad.BottomLeft.X, quad.BottomLeft.Y};
    geometryKey.Corners[3] = {quad.BottomRight.X, quad.BottomRight.Y};
    Ps1UiVertex vertices[4]{};
    if (!SpriteGeometryCache.TryGet(geometryKey, vertices)) {
        if (!TryBuildSpriteQuadFromCorners(geometryKey.Corners, geometryKey.Source,
            {static_cast<float>(width), static_cast<float>(height)}, ActiveViewport, vertices)) {
            Reject("PS1_UI_SPRITE_GEOMETRY_REJECTED");
            return;
        }
        SpriteGeometryCache.Remember(geometryKey, vertices);
    }
#if defined(PS1_CPU_PROFILE)
    if (CpuProfile != nullptr) {
        const std::uint64_t geometryEndUs = CpuProfile->ReadMicroseconds();
        if (geometryEndUs < geometryStartUs) CpuProfile->ClockFault();
        else UiSpriteGeometryUs += geometryEndUs - geometryStartUs;
    }
#endif
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
    const std::uint16_t bucket = 0;
    PendingUiDrawable pending{};
    pending.Kind = PendingKind::Sprite;
    for (unsigned i = 0; i < 4; ++i) pending.Vertices[i] = vertices[i];
    pending.Texture = texture;
    pending.Color = modulation;
    pending.Bucket = bucket;
    TryMapAlphaToBlend(tint.W, pending.SemiTransparent);
    pending.IsSprite = true;
    Ps1UiVertex safeQuads[16][4]{};
    std::uint16_t safeCount = 0;
    if (!TrySplitGpuSafeSpriteQuad(vertices, safeQuads, safeCount)) {
        Reject("PS1_UI_SPRITE_GPU_EXTENT_REJECTED");
        return;
    }
    if (PendingCount + safeCount > PendingUiCapacity ||
        PendingSprites + safeCount > Ps1FramePacketLimits::TexturedQuads) {
        Reject("PS1_UI_SPRITE_CAPACITY");
        return;
    }
    for (std::uint16_t i = 0; i < safeCount; ++i) {
        for (unsigned vertex = 0; vertex < 4; ++vertex) pending.Vertices[vertex] = safeQuads[i][vertex];
        if (!AppendPending(pending)) return;
    }
}

void Ps1RenderManager2D::DrawText(::ITextDrawable2D* text) {
    if (text == nullptr || text->get_Parent() == nullptr || !QueueOpen) return;
#if defined(PS1_CPU_PROFILE)
    ScopedUiCpuSample cpuSample(CpuProfile, UiTextDrawUs);
#endif
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
    bool transformValid = false;
    {
#if defined(PS1_CPU_PROFILE)
        ScopedNativeStage transformSample(CpuProfile, 113);
#endif
        transformValid = TryScreenTransform(text->get_Parent(), position, parentScale, rotation);
    }
    if (!transformValid) {
        Reject("PS1_UI_TEXT_TRANSFORM_REJECTED");
        return;
    }
    const std::uint16_t bucket = 0;
    CachedText* cached = nullptr;
    for (CachedText& entry : TextCache) {
        if (entry.Drawable == text) { cached = &entry; break; }
    }
    const ::Component* clipComponents[64]{};
    ::IClipRegion2D* clipRegions[8]{};
    unsigned componentCount = 0, clipCount = 0;
    bool clipCacheable = true;
    {
#if defined(PS1_CPU_PROFILE)
        ScopedNativeStage clipSample(CpuProfile, 114);
#endif
    for (::Entity* current = text->get_Parent(); current != nullptr; current = current->get_Parent()) {
        auto* components = current->get_Components();
        if (components == nullptr) continue;
        for (int32_t i = 0; i < components->get_Count(); ++i) {
            if (componentCount == 64) { clipCacheable = false; break; }
            clipComponents[componentCount++] = components->get_Item(i);
        }
        if (!clipCacheable) break;
    }
    bool clipCacheMatches = clipCacheable && cached != nullptr && cached->ClipCacheValid &&
                            cached->ClipComponentCount == componentCount;
    for (unsigned i = 0; clipCacheMatches && i < componentCount; ++i)
        clipCacheMatches = cached->ClipComponents[i] == clipComponents[i];
    if (clipCacheMatches) {
        clipCount = cached->ClipRegionCount;
        for (unsigned i = 0; i < clipCount; ++i) clipRegions[i] = cached->ClipRegions[i];
    } else if (clipCacheable) {
        for (unsigned i = 0; i < componentCount; ++i) {
            auto* region = dynamic_cast<::IClipRegion2D*>(const_cast<::Component*>(clipComponents[i]));
            if (region == nullptr) continue;
            if (clipCount == 8) { clipCacheable = false; break; }
            clipRegions[clipCount++] = region;
        }
    }
    }
    Ps1ScreenRect clipRect = ActiveViewport;
    const auto applyClip = [&](::IClipRegion2D* clip) {
            const ::float4 r = clip->GetClipRect();
            const int left = ui_geometry_detail::Round(r.X) + ActiveViewport.x;
            const int top = ui_geometry_detail::Round(r.Y) + ActiveViewport.y;
            const int right = ui_geometry_detail::Round(r.X + r.Z) + ActiveViewport.x;
            const int bottom = ui_geometry_detail::Round(r.Y + r.W) + ActiveViewport.y;
            const int x0 = left > clipRect.x ? left : clipRect.x;
            const int y0 = top > clipRect.y ? top : clipRect.y;
            const int x1 = right < clipRect.x + clipRect.width ? right : clipRect.x + clipRect.width;
            const int y1 = bottom < clipRect.y + clipRect.height ? bottom : clipRect.y + clipRect.height;
            if (x1 <= x0 || y1 <= y0) return false;
            clipRect = {static_cast<std::int16_t>(x0), static_cast<std::int16_t>(y0),
                        static_cast<std::int16_t>(x1 - x0), static_cast<std::int16_t>(y1 - y0)};
            return true;
    };
    if (clipCacheable) {
        for (unsigned i = 0; i < clipCount; ++i) if (!applyClip(clipRegions[i])) return;
    } else {
        for (::Entity* current = text->get_Parent(); current != nullptr; current = current->get_Parent()) {
            auto* components = current->get_Components();
            if (components == nullptr) continue;
            for (int32_t i = 0; i < components->get_Count(); ++i) {
                auto* region = dynamic_cast<::IClipRegion2D*>(components->get_Item(i));
                if (region != nullptr && !applyClip(region)) return;
            }
        }
    }
    const std::int32_t renderStateVersion = text->get_TextRenderStateVersion();
    const ::int2 size = text->get_Size();
    HeCppString diagnosticContent;
    if (!HasLoggedFirstUiGeometry) diagnosticContent = text->get_Text();
    const std::uint32_t positionX = static_cast<std::uint32_t>(ui_geometry_detail::Round(position.x));
    const std::uint32_t positionY = static_cast<std::uint32_t>(ui_geometry_detail::Round(position.y));
    // TextComponent advances its version for text and style changes. Keep the independently
    // authored viewport, entity position, render order and texture checks on this fast path.
    if (cached != nullptr && cached->Font == font && cached->Texture == texture &&
        cached->RenderStateVersion == renderStateVersion &&
        cached->Key[0] == static_cast<std::uint16_t>(ActiveViewport.x) &&
        cached->Key[1] == static_cast<std::uint16_t>(ActiveViewport.y) &&
        cached->Key[2] == static_cast<std::uint16_t>(ActiveViewport.width) &&
        cached->Key[3] == static_cast<std::uint16_t>(ActiveViewport.height) &&
        cached->Key[4] == positionX && cached->Key[5] == positionY &&
        cached->Key[10] == bucket && cached->Key[13] == width && cached->Key[14] == height &&
        cached->Key[33] == static_cast<std::uint16_t>(clipRect.x) &&
        cached->Key[34] == static_cast<std::uint16_t>(clipRect.y) &&
        cached->Key[35] == static_cast<std::uint16_t>(clipRect.width) &&
        cached->Key[36] == static_cast<std::uint16_t>(clipRect.height)) {
        if (!HasLoggedFirstUiGeometry) {
            char label[64]{};
            const std::size_t length = diagnosticContent.size() < sizeof(label) - 1u ? diagnosticContent.size() : sizeof(label) - 1u;
            for (std::size_t i = 0; i < length; ++i) label[i] = diagnosticContent[i];
            ramsyscall_printf("PS1_UI_TEXT y=%d size=%d,%d scale_y=%d order=%u text=%s\n",
                              static_cast<int>(position.y), size.X, size.Y, static_cast<int>(parentScale.y * 1000.0f),
                              static_cast<unsigned>(PendingCount), label);
        }
        AppendCachedText(*cached);
        return;
    }
    if (cached != nullptr && !cached->InFrame && cached->RawReady && cached->Font == font && cached->Texture == texture &&
        cached->RenderStateVersion == renderStateVersion &&
        cached->Key[0] == static_cast<std::uint16_t>(ActiveViewport.x) &&
        cached->Key[1] == static_cast<std::uint16_t>(ActiveViewport.y) &&
        cached->Key[2] == static_cast<std::uint16_t>(ActiveViewport.width) &&
        cached->Key[3] == static_cast<std::uint16_t>(ActiveViewport.height) &&
        cached->Key[13] == width && cached->Key[14] == height) {
        const auto dx = static_cast<std::int32_t>(positionX - cached->Key[4]);
        const auto dy = static_cast<std::int32_t>(positionY - cached->Key[5]);
        bool translated = true;
        for (unsigned i = 0; translated && i < cached->RawCount; ++i) {
            for (const auto& vertex : cached->RawQuads[i].Vertices) {
                const auto x = static_cast<std::int32_t>(vertex.x) + dx;
                const auto y = static_cast<std::int32_t>(vertex.y) + dy;
                if (x < -32768 || x > 32767 || y < -32768 || y > 32767) { translated = false; break; }
            }
        }
        if (translated) {
            cached->Count = 0;
            cached->MinY = 32767; cached->MaxY = -32768;
            const auto revision = AllocatePacketRevision();
            for (unsigned i = 0; i < cached->RawCount; ++i) {
                auto& raw = cached->RawQuads[i];
                for (auto& vertex : raw.Vertices) {
                    vertex.x = static_cast<std::int16_t>(vertex.x + dx);
                    vertex.y = static_cast<std::int16_t>(vertex.y + dy);
                }
                auto quad = raw;
                if (!ClipPs1GlyphQuad(quad.Vertices, clipRect)) continue;
                quad.PacketRevision = revision;
                for (const auto& vertex : quad.Vertices) {
                    if (vertex.y < cached->MinY) cached->MinY = vertex.y;
                    if (vertex.y > cached->MaxY) cached->MaxY = vertex.y;
                }
                cached->Quads[cached->Count++] = quad;
            }
            cached->Key[4] = positionX; cached->Key[5] = positionY;
            cached->Key[33] = static_cast<std::uint16_t>(clipRect.x);
            cached->Key[34] = static_cast<std::uint16_t>(clipRect.y);
            cached->Key[35] = static_cast<std::uint16_t>(clipRect.width);
            cached->Key[36] = static_cast<std::uint16_t>(clipRect.height);
            AppendCachedText(*cached);
            return;
        }
    }
    const float fontScale = text->get_FontScale() > 0.0001f ? text->get_FontScale() : 0.0001f;
    HeCppString content = text->get_Text();
    if (text->get_WrapText()) {
        const int32_t wrapWidth = static_cast<int32_t>(ui_geometry_detail::Round(static_cast<float>(size.X) / fontScale));
        content = ::TextLayoutUtils::WrapText(content, font, wrapWidth > 0 ? wrapWidth : 1);
    }
    const float lineHeight = font->get_LineHeight() * fontScale > 1.0f ? font->get_LineHeight() * fontScale : 1.0f;
    const float spaceWidth = font->get_FontInfo() != nullptr && font->get_FontInfo()->get_SpaceWidth() * fontScale > 1.0f
        ? font->get_FontInfo()->get_SpaceWidth()
        : (font->get_LineHeight() * 0.25f * fontScale > 1.0f ? font->get_LineHeight() * 0.25f : 1.0f / fontScale);
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
                      glyph.OffsetY, glyph.AdvanceWidth, texels.x, texels.y, texels.width, texels.height, sourceValid,
                      glyph.BearingX};
            return true;
        });
    };
    // A text component usually keeps the same string for many frames. Cache the exact quads so
    // the PS1 does not redo software floating-point layout and glyph geometry on every VBlank.
    std::uint32_t key[37]{};
    const auto floatBits = [](float value) {
        std::uint32_t bits = 0;
        __builtin_memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    key[0] = static_cast<std::uint16_t>(ActiveViewport.x);
    key[1] = static_cast<std::uint16_t>(ActiveViewport.y);
    key[2] = static_cast<std::uint16_t>(ActiveViewport.width);
    key[3] = static_cast<std::uint16_t>(ActiveViewport.height);
    key[4] = positionX;
    key[5] = positionY;
    key[6] = floatBits(fontScale);
    key[7] = static_cast<std::uint32_t>(size.X);
    key[8] = floatBits(lineHeight);
    key[9] = floatBits(spaceWidth);
    key[10] = bucket;
    key[11] = static_cast<std::uint32_t>(alignment);
    key[12] = passCount;
    key[13] = width;
    key[14] = height;
    key[33] = static_cast<std::uint16_t>(clipRect.x);
    key[34] = static_cast<std::uint16_t>(clipRect.y);
    key[35] = static_cast<std::uint16_t>(clipRect.width);
    key[36] = static_cast<std::uint16_t>(clipRect.height);
    for (unsigned i = 0; i < passCount; ++i) {
        key[15 + i * 3] = floatBits(passes[i].x);
        key[16 + i * 3] = floatBits(passes[i].y);
        const ::byte4 c = passes[i].color;
        key[17 + i * 3] = static_cast<std::uint32_t>(c.X) | (static_cast<std::uint32_t>(c.Y) << 8u) |
                             (static_cast<std::uint32_t>(c.Z) << 16u) | (static_cast<std::uint32_t>(c.W) << 24u);
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
    const bool unclippedViewport = clipRect.x == ActiveViewport.x && clipRect.y == ActiveViewport.y &&
                                   clipRect.width == ActiveViewport.width && clipRect.height == ActiveViewport.height;
    // Retained clipped quads have cropped UVs. A replacement glyph must go through layout
    // again rather than overwriting those cropped endpoints with its full source rectangle.
    if (baseMatches && !contentMatches && unclippedViewport && !cached->InFrame && cached->Content.size() == content.size()) {
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
            if (oldChar != newChar && !CanReusePs1TextGlyphGeometry(oldGlyph, newGlyph)) {
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
                    const std::uint32_t revision = AllocatePacketRevision();
                    for (unsigned pass = 0; pass < visiblePasses; ++pass) {
                        Ps1UiVertex* vertices = cached->Quads[quad + pass].Vertices;
                        cached->Quads[quad + pass].PacketRevision = revision;
                        vertices[0].u = vertices[2].u = u0;
                        vertices[1].u = vertices[3].u = u1;
                        vertices[0].v = vertices[1].v = v0;
                        vertices[2].v = vertices[3].v = v1;
                    }
                }
                quad = static_cast<std::uint16_t>(quad + visiblePasses);
            }
            cached->Content = content;
            cached->RawReady = false;
            contentMatches = true;
        }
    }
    if (baseMatches && contentMatches) {
        cached->RenderStateVersion = renderStateVersion;
        AppendCachedText(*cached);
        return;
    }
    const std::uint16_t pendingStart = PendingCount;
    unsigned rawCount = 0;
    bool rawReady = cached != nullptr && !cached->InFrame;
    if (rawReady) cached->RawReady = false;
    const auto emit = [&](char, const Ps1TextGlyph& glyph, float x, float y) {
        const Ps1GlyphTexelRect source{glyph.sourceX, glyph.sourceY, glyph.sourceWidth, glyph.sourceHeight};
        for (unsigned passIndex = 0; passIndex < passCount; ++passIndex) {
            const GlyphPass& pass = passes[passIndex];
            if (pass.color.W == 0) continue;
            PendingUiDrawable pending{};
            pending.Kind = PendingKind::Text;
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
            if (rawReady) {
                if (rawCount == 96) rawReady = false;
                else cached->RawQuads[rawCount++] = pending;
            }
            if (!ClipPs1GlyphQuad(pending.Vertices, clipRect)) continue;
            if (!AppendPending(pending)) return false;
            ++QueuedGlyphQuads;
        }
        return true;
    };
    const bool laidOut = LayoutPs1Text(content.data(), content.size(), static_cast<float>(ui_geometry_detail::Round(position.x)),
                       static_cast<float>(ui_geometry_detail::Round(position.y)), lineHeight, spaceWidth,
                       fontScale, static_cast<float>(size.X), alignment, lookup, emit);
    if (!HasLoggedFirstUiGeometry) {
        char label[64]{};
        const std::size_t length = diagnosticContent.size() < sizeof(label) - 1u ? diagnosticContent.size() : sizeof(label) - 1u;
        for (std::size_t i = 0; i < length; ++i) label[i] = diagnosticContent[i];
        ramsyscall_printf("PS1_UI_TEXT y=%d size=%d,%d scale_y=%d order=%u text=%s\n",
                          static_cast<int>(position.y), size.X, size.Y, static_cast<int>(parentScale.y * 1000.0f),
                          static_cast<unsigned>(pendingStart), label);
    }
    if (cached != nullptr && !cached->InFrame && laidOut && FailureMarker == nullptr && PendingCount - pendingStart <= 96u) {
        cached->Drawable = text;
        cached->Font = font;
        cached->Texture = texture;
        cached->RenderStateVersion = renderStateVersion;
        cached->ClipCacheValid = clipCacheable;
        cached->ClipComponentCount = static_cast<std::uint8_t>(componentCount);
        cached->ClipRegionCount = static_cast<std::uint8_t>(clipCount);
        for (unsigned i = 0; i < componentCount; ++i) cached->ClipComponents[i] = clipComponents[i];
        for (unsigned i = 0; i < clipCount; ++i) cached->ClipRegions[i] = clipRegions[i];
        __builtin_memcpy(cached->Key, key, sizeof(key));
        cached->Content = content;
        cached->Count = static_cast<std::uint16_t>(PendingCount - pendingStart);
        cached->RawCount = static_cast<std::uint16_t>(rawCount);
        cached->RawReady = rawReady;
        cached->MinY = 32767; cached->MaxY = -32768;
        const std::uint32_t revision = AllocatePacketRevision();
        for (std::uint16_t i = 0; i < cached->Count; ++i) {
            for (const auto& vertex : PendingOrder[pendingStart + i]->Vertices) {
                if (vertex.y < cached->MinY) cached->MinY = vertex.y;
                if (vertex.y > cached->MaxY) cached->MaxY = vertex.y;
            }
            cached->Quads[i] = Pending[pendingStart + i];
            cached->Quads[i].PacketRevision = revision;
        }
        cached->InFrame = true;
    }
    if (!laidOut && FailureMarker == nullptr) {
        Reject("PS1_UI_TEXT_LAYOUT_REJECTED");
    }
}

void Ps1RenderManager2D::UpdateTextureRegionCore(::RuntimeTexture* texture, int32_t x, int32_t y, int32_t width,
                                              int32_t height, Array<uint8_t>* rgba8, int32_t sourceRowPitch) {
    auto* native = dynamic_cast<Ps1RuntimeTexture*>(texture);
    if (native == nullptr || !native->CanUpdatePixels(this) ||
        rgba8 == nullptr || rgba8->Length < 0 ||
        !IsPs1MutableRegionValid(native->get_Width(), native->get_Height(), native->WidthInWords,
                                x, y, width, height, rgba8->Data, static_cast<std::size_t>(rgba8->Length), sourceRowPitch)) {
        he_cpp_custom::Fail("PS1 RGBA region update requires a live mutable texture and valid source/destination bounds");
    }
    MarkFrameStage(Ps1FrameStage::TextureUploadEnter);
    UpdatePs1MutableTexture(Gpu, native->MutablePixels, native->WidthInWords, native->VramX, native->VramY,
                           x, y, width, height, rgba8->Data, sourceRowPitch, native->MutableOpaque);
}

}  // namespace helengine::ps1
