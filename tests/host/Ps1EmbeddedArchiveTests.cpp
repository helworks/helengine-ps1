#include "platform/ps1/Ps1EmbeddedArchive.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void PutU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

// Builds the same layout scripts/embed-authored-assets.py emits: two entries,
// "a/mat.bin" = "MAT" and "s/scene.helen" = 0..36 (37 bytes).
std::vector<std::uint8_t> BuildArchive() {
    const char* pathA = "a/mat.bin";
    const char* pathB = "s/scene.helen";
    const std::size_t headerSize = 16;
    const std::size_t entrySize = 16;
    const std::size_t tableOffset = headerSize + entrySize * 2;
    const std::size_t pathLengthA = std::strlen(pathA);
    const std::size_t pathLengthB = std::strlen(pathB);
    const std::size_t tableEnd = tableOffset + pathLengthA + pathLengthB;
    const std::size_t dataStart = (tableEnd + 3) / 4 * 4;
    const std::size_t dataOffsetA = dataStart;
    const std::size_t dataOffsetB = dataOffsetA + 4;  // "MAT" padded to 4
    const std::size_t total = (dataOffsetB + 37 + 3) / 4 * 4;

    std::vector<std::uint8_t> bytes(total, 0);
    bytes[0] = 'H'; bytes[1] = 'E'; bytes[2] = 'P'; bytes[3] = '1';
    PutU32(bytes, 4, 1);
    PutU32(bytes, 8, 2);
    PutU32(bytes, 12, static_cast<std::uint32_t>(total));
    PutU32(bytes, 16, static_cast<std::uint32_t>(tableOffset));
    PutU32(bytes, 20, static_cast<std::uint32_t>(pathLengthA));
    PutU32(bytes, 24, static_cast<std::uint32_t>(dataOffsetA));
    PutU32(bytes, 28, 3);
    PutU32(bytes, 32, static_cast<std::uint32_t>(tableOffset + pathLengthA));
    PutU32(bytes, 36, static_cast<std::uint32_t>(pathLengthB));
    PutU32(bytes, 40, static_cast<std::uint32_t>(dataOffsetB));
    PutU32(bytes, 44, 37);
    std::memcpy(&bytes[tableOffset], pathA, pathLengthA);
    std::memcpy(&bytes[tableOffset + pathLengthA], pathB, pathLengthB);
    std::memcpy(&bytes[dataOffsetA], "MAT", 3);
    for (std::uint8_t index = 0; index < 37; ++index) {
        bytes[dataOffsetB + index] = index;
    }
    return bytes;
}

}  // namespace

int main() {
    using helengine::ps1::Ps1EmbeddedArchive;
    using helengine::ps1::Ps1EmbeddedAsset;

    std::vector<std::uint8_t> bytes = BuildArchive();
    Ps1EmbeddedArchive archive;
    if (!archive.Open(bytes.data(), bytes.size())) return 1;
    if (archive.EntryCount() != 2) return 2;

    Ps1EmbeddedAsset asset{};
    if (!archive.TryFind("s/scene.helen", asset) || asset.Length != 37 || asset.Data[0] != 0 || asset.Data[36] != 36) return 3;
    if (!archive.TryFind("a/mat.bin", asset) || asset.Length != 3 || std::memcmp(asset.Data, "MAT", 3) != 0) return 4;
    // Lookup is exact: separators are not rewritten and prefixes do not match.
    if (archive.TryFind("a\\mat.bin", asset) || archive.TryFind("a/mat", asset) || archive.TryFind("", asset)) return 5;
    if (archive.TryFind("missing.bin", asset)) return 6;

    // Truncated image: declared total exceeds the bytes actually present.
    Ps1EmbeddedArchive truncated;
    if (truncated.Open(bytes.data(), bytes.size() - 1)) return 7;

    // Corrupt total that is smaller than the entry table.
    std::vector<std::uint8_t> shortTotal = bytes;
    PutU32(shortTotal, 12, 20);
    Ps1EmbeddedArchive corruptTotal;
    if (corruptTotal.Open(shortTotal.data(), 20)) return 8;

    // Entry data range past the end of the image.
    std::vector<std::uint8_t> badRange = bytes;
    PutU32(badRange, 28, 0x7FFFFFFF);
    Ps1EmbeddedArchive corruptRange;
    if (corruptRange.Open(badRange.data(), badRange.size())) return 9;

    // Unaligned payload offset.
    std::vector<std::uint8_t> unaligned = bytes;
    PutU32(unaligned, 24, 49);
    Ps1EmbeddedArchive corruptAlignment;
    if (corruptAlignment.Open(unaligned.data(), unaligned.size())) return 10;

    // Wrong magic and unsupported version.
    std::vector<std::uint8_t> badMagic = bytes;
    badMagic[3] = '2';
    Ps1EmbeddedArchive wrongMagic;
    if (wrongMagic.Open(badMagic.data(), badMagic.size())) return 11;
    std::vector<std::uint8_t> badVersion = bytes;
    PutU32(badVersion, 4, 2);
    Ps1EmbeddedArchive wrongVersion;
    if (wrongVersion.Open(badVersion.data(), badVersion.size())) return 12;

    // Path range overlapping the entry table is rejected.
    std::vector<std::uint8_t> badPath = bytes;
    PutU32(badPath, 16, 16);
    Ps1EmbeddedArchive corruptPath;
    if (corruptPath.Open(badPath.data(), badPath.size())) return 13;

    // An unopened archive finds nothing and reports zero entries.
    Ps1EmbeddedArchive unopened;
    if (unopened.EntryCount() != 0 || unopened.TryFind("a/mat.bin", asset)) return 14;
    return 0;
}
