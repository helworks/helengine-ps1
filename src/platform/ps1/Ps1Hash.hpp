#pragma once

#include <cstddef>
#include <cstdint>
#include <EASTL/functional.h>

namespace helengine::ps1 {
/// <summary>Hashes floating values without hosted libstdc++ or out-of-range numeric casts.</summary>
template <typename T>
inline std::size_t HashFloatingValue(T value) {
    if (value == 0) { return 0; }
    if (value != value) { return 0x7fc00000u; }
    unsigned char bytes[sizeof(T)];
    __builtin_memcpy(bytes, &value, sizeof(T));
    std::uint32_t hash = 2166136261u;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        hash = (hash ^ bytes[index]) * 16777619u;
    }
    return hash;
}

/// <summary>Uses SDK hashing for ordinary keys and safe bit hashing for floating keys.</summary>
template <typename T>
struct Ps1Hash : eastl::hash<T> {};

template <>
struct Ps1Hash<float> {
    /// <summary>Hashes single-precision values, normalizing zero and NaN equality cases.</summary>
    std::size_t operator()(float value) const { return HashFloatingValue(value); }
};

template <>
struct Ps1Hash<double> {
    /// <summary>Hashes double-precision values, normalizing zero and NaN equality cases.</summary>
    std::size_t operator()(double value) const { return HashFloatingValue(value); }
};
}
