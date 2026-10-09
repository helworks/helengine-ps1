#pragma once

#include <cstdint>

#include "IRenderVisitor2D.hpp"
#include "RenderManager2D.hpp"
#include "platform/ps1/rendering/Ps1AuthoredUiGeometry.hpp"
#include "platform/ps1/rendering/Ps1SpriteGeometryCache.hpp"
#include "platform/ps1/rendering/Ps1TextLayout.hpp"
#include "platform/ps1/rendering/Ps1RasterRenderer.hpp"
#include "platform/ps1/rendering/Ps1VramAllocator.hpp"
#include "psyqo/gpu.hh"

class TextureAsset;
class ICamera;
class IDrawable2D;
class IRenderQueue2D;
class Component;
class IClipRegion2D;

namespace helengine::ps1 {

class Ps1RuntimeTexture;
#if defined(PS1_CPU_PROFILE)
class Ps1CpuProfileSink;
#endif

/// <summary>
/// 2D render manager backing Core's scene lifecycle on PS1. It uploads cooked paletted textures into
/// the VRAM the display buffers leave free and hands back their page and palette locations. Drawables
/// are staged in Core queue order and submitted in reverse to preserve that order in the PS1 OT.
/// </summary>
class Ps1RenderManager2D final : public ::RenderManager2D, public ::IRenderVisitor2D {
public:
    /// <summary>Binds the PSYQo GPU that owns VRAM.</summary>
    /// <param name="gpu">GPU used for the VRAM uploads.</param>
    Ps1RenderManager2D(psyqo::GPU& gpu, Ps1RasterRenderer& renderer);

    bool DrawQueue(::ICamera* camera);
#if defined(PS1_CPU_PROFILE)
    void SetCpuProfileSink(Ps1CpuProfileSink* sink) { CpuProfile = sink; }
#endif
    void Visit(::IDrawable2D* drawable) override;

    ::RuntimeTexture* BuildTextureFromCooked(HeCppString cookedAssetPath, ::IContentStreamSource* contentStreamSource) override;
    ::RuntimeTexture* BuildTextureFromRaw(::TextureAsset* data) override;
    void DrawRoundedRect(::IRoundedRectDrawable2D* shape) override;
    void DrawSprite(::ISpriteDrawable2D* sprite) override;
    void DrawText(::ITextDrawable2D* text) override;
    void ReleaseTexture(::RuntimeTexture* texture) override;

    /// <summary>Textures uploaded to VRAM since construction.</summary>
    std::uint32_t UploadedTextureCount() const { return UploadedTextures; }

    /// <summary>VRAM words handed out to textures and palettes.</summary>
    std::uint32_t UsedVramWords() const { return VramAllocator.UsedWords(); }
    std::uint16_t LastVisitedSprites() const { return VisitedSprites; }
    std::uint16_t LastVisitedShapes() const { return VisitedShapes; }
    std::uint16_t LastSkippedTexts() const { return SkippedTexts; }
    std::uint16_t LastVisitedTexts() const { return VisitedTexts; }
    std::uint16_t LastQueuedGlyphQuads() const { return QueuedGlyphQuads; }
    std::uint32_t LastRequestedShapeTriangles() const { return RequestedShapeTriangles; }
    bool HasLastUiBounds() const { return HasUiBounds; }
    std::int16_t LastUiMinY() const { return UiMinY; }
    std::int16_t LastUiMaxY() const { return UiMaxY; }
    bool HasLastSpriteBounds() const { return HasSpriteBounds; }
    std::int16_t LastSpriteMinY() const { return SpriteMinY; }
    std::int16_t LastSpriteMaxY() const { return SpriteMaxY; }
    bool HasLastShapeBounds() const { return HasShapeBounds; }
    std::int16_t LastShapeMinY() const { return ShapeMinY; }
    std::int16_t LastShapeMaxY() const { return ShapeMaxY; }
    bool HasLastTextBounds() const { return HasTextBounds; }
    std::int16_t LastTextMinY() const { return TextMinY; }
    std::int16_t LastTextMaxY() const { return TextMaxY; }
    const Ps1ScreenRect& LastViewport() const { return ActiveViewport; }
    const char* LastFailureMarker() const { return FailureMarker; }

protected:
    void UpdateTextureRegionCore(::RuntimeTexture* texture, int32_t x, int32_t y, int32_t width, int32_t height,
                                 Array<uint8_t>* rgba8, int32_t sourceRowPitch) override;

private:
    enum class PendingKind : std::uint8_t {
        Sprite,
        Shape,
        Text,
    };

    struct PendingUiDrawable {
        Ps1UiVertex Vertices[4]{};
        const Ps1RuntimeTexture* Texture = nullptr;
        psyqo::Color Color{};
        std::uint16_t Bucket = 0;
        PendingKind Kind = PendingKind::Shape;
        bool SemiTransparent = false;
        bool IsSprite = false;
        std::uint32_t PacketRevision = 0;
    };

    struct CachedText {
        const ::ITextDrawable2D* Drawable = nullptr;
        const void* Font = nullptr;
        const Ps1RuntimeTexture* Texture = nullptr;
        std::int32_t RenderStateVersion = 0;
        std::uint32_t Key[37]{};
        HeCppString Content;
        std::uint16_t Count = 0;
        // PendingOrder may still point into this entry until the current frame is flushed.
        bool InFrame = false;
        PendingUiDrawable Quads[96]{};
        // Compare exact ancestor component membership before reusing interface
        // casts. Clip geometry still resolves every frame as scroll offsets move.
        const ::Component* ClipComponents[64]{};
        ::IClipRegion2D* ClipRegions[8]{};
        std::uint8_t ClipComponentCount = 0;
        std::uint8_t ClipRegionCount = 0;
        bool ClipCacheValid = false;
        // Keep unclipped glyphs so authored scrolling changes only integer screen geometry.
        PendingUiDrawable RawQuads[96]{};
        std::uint16_t RawCount = 0;
        bool RawReady = false;
        std::int16_t MinY = 0;
        std::int16_t MaxY = 0;
    };
    static constexpr std::uint8_t TextCacheCapacity = 8;
    static constexpr std::uint16_t PendingUiCapacity = Ps1FramePacketLimits::UiTriangles +
                                                        Ps1FramePacketLimits::TexturedTriangles / 2u;

    /// <summary>Uploads one indexed or direct-color texture asset into VRAM.</summary>
    /// <param name="asset">Cooked pixels with a palette only when the format is indexed.</param>
    /// <returns>Runtime texture naming the VRAM page and its optional CLUT.</returns>
    ::RuntimeTexture* UploadTexture(::TextureAsset* asset);

    bool ResolveViewport(::ICamera* camera, Ps1ScreenRect& viewport) const;
    void Reject(const char* marker);
    bool AppendPending(const PendingUiDrawable& drawable);
    bool AppendCachedText(CachedText& cached);
    void IncludeBounds(PendingKind kind, std::int16_t minimum, std::int16_t maximum);
    void IncludeBounds(const PendingUiDrawable& drawable);
    bool FlushPendingInVisitOrder();
    std::uint32_t AllocatePacketRevision();

#if defined(PS1_CPU_PROFILE)
    Ps1CpuProfileSink* CpuProfile = nullptr;
    std::uint64_t UiTextDrawUs = 0;
    std::uint64_t UiSpriteDrawUs = 0;
    std::uint64_t UiSpriteGeometryUs = 0;
    std::uint64_t UiShapeDrawUs = 0;
    std::uint64_t UiSpritePacketUs = 0;
    std::uint64_t UiShapePacketUs = 0;
#endif
    psyqo::GPU& Gpu;
    Ps1RasterRenderer& Renderer;
    Ps1VramAllocator VramAllocator;
    std::uint32_t UploadedTextures = 0;
    Ps1ScreenRect ActiveViewport{};
    std::uint16_t VisitedSprites = 0;
    std::uint16_t VisitedShapes = 0;
    std::uint16_t SkippedTexts = 0;
    std::uint16_t VisitedTexts = 0;
    std::uint16_t QueuedGlyphQuads = 0;
    std::uint32_t RequestedShapeTriangles = 0;
    std::uint16_t PendingCount = 0;
    std::uint16_t PendingShapes = 0;
    std::uint16_t PendingSprites = 0;
    PendingUiDrawable Pending[PendingUiCapacity]{};
    const PendingUiDrawable* PendingOrder[PendingUiCapacity]{};
    CachedText TextCache[TextCacheCapacity]{};
    std::uint8_t NextTextCache = 0;
    std::uint32_t NextPacketRevision = 0;
    Ps1TextGlyphCache GlyphCache;
    Ps1SpriteGeometryCache SpriteGeometryCache;
    bool QueueOpen = false;
    bool PreloadingTextTextures = false;
    bool HasUiBounds = false;
    std::int16_t UiMinY = 0;
    std::int16_t UiMaxY = 0;
    bool HasSpriteBounds = false;
    std::int16_t SpriteMinY = 0;
    std::int16_t SpriteMaxY = 0;
    bool HasShapeBounds = false;
    std::int16_t ShapeMinY = 0;
    std::int16_t ShapeMaxY = 0;
    bool HasTextBounds = false;
    std::int16_t TextMinY = 0;
    std::int16_t TextMaxY = 0;
    bool HasLoggedFirstUiGeometry = false;
    ::IRenderQueue2D* ValidatedQueue = nullptr;
    std::int32_t ValidatedQueueVersion = 0;
    bool UseValidatedQueue = false;
    const char* FailureMarker = nullptr;
};

}  // namespace helengine::ps1
