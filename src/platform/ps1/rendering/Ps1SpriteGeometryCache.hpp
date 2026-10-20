#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/ps1/rendering/Ps1AuthoredUiGeometry.hpp"

namespace helengine::ps1 {

/// <summary>Captures unresolved geometry inputs so a cache hit can avoid local rotation and corner transforms.</summary>
struct Ps1SpriteGeometryKey {
    const void* Drawable = nullptr;
    const void* Texture = nullptr;
    /// <summary>Stores M11, M12, M21, M22, M41 and M42 from the cached world matrix, including inherited shear and reflection.</summary>
    float Affine[6]{};
    /// <summary>Stores the authored local pivot before it is multiplied by sprite size.</summary>
    float Pivot[2]{};
    /// <summary>Invalidates local trigonometry when the sprite's own rotation changes.</summary>
    float RotationRadians = 0.0f;
    std::int32_t SizeX = 0;
    std::int32_t SizeY = 0;
    Ps1UiRectF Source{};
    std::uint16_t TextureWidth = 0;
    std::uint16_t TextureHeight = 0;
    Ps1ScreenRect Viewport{};
};

// Keeps static sprite geometry without retaining generated-Core objects or allocating per frame.
class Ps1SpriteGeometryCache {
public:
    static constexpr std::size_t Capacity = 8;

    bool TryGet(const Ps1SpriteGeometryKey& key, Ps1UiVertex (&vertices)[4]) const {
        for (const Entry& entry : Entries) {
            if (entry.Occupied && Equal(entry.Key, key)) {
                __builtin_memcpy(vertices, entry.Vertices, sizeof(entry.Vertices));
                return true;
            }
        }
        return false;
    }

    void Remember(const Ps1SpriteGeometryKey& key, const Ps1UiVertex (&vertices)[4]) {
        Entry* slot = nullptr;
        for (Entry& entry : Entries) {
            if (entry.Occupied && entry.Key.Drawable == key.Drawable) {
                slot = &entry;
                break;
            }
            if (!entry.Occupied && slot == nullptr) slot = &entry;
        }
        if (slot == nullptr) {
            slot = &Entries[NextSlot];
            NextSlot = (NextSlot + 1u) % Capacity;
        }
        slot->Key = key;
        __builtin_memcpy(slot->Vertices, vertices, sizeof(slot->Vertices));
        slot->Occupied = true;
    }

private:
    struct Entry {
        Ps1SpriteGeometryKey Key{};
        Ps1UiVertex Vertices[4]{};
        bool Occupied = false;
    };

    static bool Equal(const Ps1SpriteGeometryKey& a, const Ps1SpriteGeometryKey& b) {
        return a.Drawable == b.Drawable && a.Texture == b.Texture
            && __builtin_memcmp(a.Affine, b.Affine, sizeof(a.Affine)) == 0
            && __builtin_memcmp(a.Pivot, b.Pivot, sizeof(a.Pivot)) == 0
            && __builtin_memcmp(&a.RotationRadians, &b.RotationRadians, sizeof(a.RotationRadians)) == 0
            && a.SizeX == b.SizeX && a.SizeY == b.SizeY
            && __builtin_memcmp(&a.Source, &b.Source, sizeof(a.Source)) == 0
            && a.TextureWidth == b.TextureWidth && a.TextureHeight == b.TextureHeight
            && a.Viewport.x == b.Viewport.x && a.Viewport.y == b.Viewport.y
            && a.Viewport.width == b.Viewport.width && a.Viewport.height == b.Viewport.height;
    }

    Entry Entries[Capacity]{};
    std::size_t NextSlot = 0;
};

} // namespace helengine::ps1
