#pragma once

#include <cstddef>
#include <cstdint>

namespace helengine::ps1 {

/// <summary>One asset located inside the embedded archive image.</summary>
struct Ps1EmbeddedAsset {
    /// <summary>First byte of the asset payload inside the read-only image.</summary>
    const std::uint8_t* Data;

    /// <summary>Payload length in bytes.</summary>
    std::size_t Length;
};

/// <summary>
/// Read-only view over the archive produced by scripts/embed-authored-assets.py and linked into the
/// PS-X EXE. Open validates every header and entry range against the image length before any
/// lookup, so a truncated or corrupt image is rejected as a whole instead of failing mid-load.
/// Lookups compare paths byte for byte; the packer already canonicalized them.
/// </summary>
class Ps1EmbeddedArchive {
public:
    /// <summary>Validates the image and adopts it. Returns false and stays empty when it is malformed.</summary>
    bool Open(const std::uint8_t* image, std::size_t imageLength) {
        Image = nullptr;
        Count = 0;
        if (image == nullptr || imageLength < HeaderSize) { return false; }
        if (image[0] != 'H' || image[1] != 'E' || image[2] != 'P' || image[3] != '1') { return false; }
        if (ReadU32(image, 4) != Version) { return false; }
        const std::uint32_t count = ReadU32(image, 8);
        const std::uint32_t total = ReadU32(image, 12);
        if (total != imageLength) { return false; }
        if (count > (imageLength - HeaderSize) / EntrySize) { return false; }
        const std::size_t tableEnd = HeaderSize + static_cast<std::size_t>(count) * EntrySize;
        for (std::uint32_t index = 0; index < count; ++index) {
            const std::size_t record = HeaderSize + static_cast<std::size_t>(index) * EntrySize;
            const std::uint32_t pathOffset = ReadU32(image, record);
            const std::uint32_t pathLength = ReadU32(image, record + 4);
            const std::uint32_t dataOffset = ReadU32(image, record + 8);
            const std::uint32_t dataLength = ReadU32(image, record + 12);
            if (!RangeIsValid(pathOffset, pathLength, tableEnd, imageLength)) { return false; }
            if (dataOffset % Alignment != 0 || !RangeIsValid(dataOffset, dataLength, tableEnd, imageLength)) { return false; }
        }
        Image = image;
        Count = count;
        return true;
    }

    /// <summary>Number of assets in the opened image, or zero when nothing is open.</summary>
    std::size_t EntryCount() const { return Count; }

    /// <summary>Finds an asset by its exact canonical relative path.</summary>
    bool TryFind(const char* relativePath, Ps1EmbeddedAsset& asset) const {
        if (Image == nullptr || relativePath == nullptr) { return false; }
        std::size_t queryLength = 0;
        while (relativePath[queryLength] != '\0') { ++queryLength; }
        if (queryLength == 0) { return false; }
        for (std::size_t index = 0; index < Count; ++index) {
            const std::size_t record = HeaderSize + index * EntrySize;
            const std::uint32_t pathLength = ReadU32(Image, record + 4);
            if (pathLength != queryLength) { continue; }
            const std::uint8_t* path = Image + ReadU32(Image, record);
            bool matches = true;
            for (std::size_t offset = 0; offset < pathLength; ++offset) {
                if (path[offset] != static_cast<std::uint8_t>(relativePath[offset])) {
                    matches = false;
                    break;
                }
            }
            if (!matches) { continue; }
            asset.Data = Image + ReadU32(Image, record + 8);
            asset.Length = ReadU32(Image, record + 12);
            return true;
        }
        return false;
    }

private:
    static constexpr std::size_t HeaderSize = 16;
    static constexpr std::size_t EntrySize = 16;
    static constexpr std::uint32_t Version = 1;
    static constexpr std::size_t Alignment = 4;

    /// <summary>Reads a little-endian 32-bit value without alignment assumptions.</summary>
    static std::uint32_t ReadU32(const std::uint8_t* image, std::size_t offset) {
        return static_cast<std::uint32_t>(image[offset]) |
            (static_cast<std::uint32_t>(image[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(image[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(image[offset + 3]) << 24);
    }

    /// <summary>True when [offset, offset + length) lies inside the image and after the entry table.</summary>
    static bool RangeIsValid(std::uint32_t offset, std::uint32_t length, std::size_t tableEnd, std::size_t imageLength) {
        if (offset < tableEnd || offset > imageLength) { return false; }
        return length <= imageLength - offset;
    }

    /// <summary>Validated image, or null while closed.</summary>
    const std::uint8_t* Image = nullptr;

    /// <summary>Validated entry count.</summary>
    std::size_t Count = 0;
};

}  // namespace helengine::ps1
