#pragma once

#include <cstdint>

namespace helengine::ps1 {

/** Maps the bounded signed camera depth interval into the 256-entry OT. */
class Ps1DepthBucket final {
  public:
    static constexpr std::uint16_t UiBucketCount = 256;
    static constexpr std::uint16_t SceneBucketOffset = UiBucketCount;
    static constexpr std::uint16_t CombinedBucketCount = UiBucketCount * 2u;
    static constexpr std::int32_t MinimumDepth = 160;
    static constexpr std::int32_t MaximumDepth = 1183;
    static constexpr std::uint16_t BucketCount = 256;

    static constexpr std::uint16_t ToCombinedSceneBucket(std::uint16_t sceneBucket) {
        return static_cast<std::uint16_t>(SceneBucketOffset + sceneBucket);
    }

    static constexpr bool TryMap(std::int32_t z0, std::int32_t z1, std::int32_t z2, std::uint16_t& bucket) {
        if (z0 < MinimumDepth || z0 > MaximumDepth || z1 < MinimumDepth || z1 > MaximumDepth ||
            z2 < MinimumDepth || z2 > MaximumDepth) {
            return false;
        }
        const std::uint32_t mean = (static_cast<std::uint32_t>(z0) + static_cast<std::uint32_t>(z1) +
                                    static_cast<std::uint32_t>(z2)) /
                                   3u;
        const std::uint32_t mapped = (mean - static_cast<std::uint32_t>(MinimumDepth)) / 4u;
        if (mapped >= BucketCount) return false;
        bucket = static_cast<std::uint16_t>(mapped);
        return true;
    }
};

}  // namespace helengine::ps1
