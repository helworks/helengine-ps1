#pragma once

#include <cstdint>

namespace helengine::ps1 {

/// <summary>
/// Extends the 16-bit HSync root counter into the 64-bit microsecond clock the generated
/// runtime expects. Unlike GPU::now(), the hardware counter advances during Core::Draw.
/// Static initializers may read the epoch before the GPU initializes.
/// </summary>
class Ps1RuntimeClock {
public:
    /// <summary>Reports whether a raw HSync counter has been adopted as the epoch.</summary>
    bool IsBound() const { return Bound; }

    /// <summary>Adopts the current raw HSync counter as the epoch. Earlier time is not counted.</summary>
    void Bind(std::uint16_t rawHsync) {
        Bound = true;
        LastRawHsync = rawHsync;
        ElapsedMicroseconds = 0;
    }

    /// <summary>Returns the microseconds accumulated since binding, or the epoch while unbound.</summary>
    std::uint64_t Elapsed() const { return ElapsedMicroseconds; }

    /// <summary>
    /// Folds a new HSync sample into the extended clock and returns the updated elapsed value.
    /// A 16-bit subtraction handles the hardware wrap, provided samples are less than 4.2 s apart.
    /// </summary>
    std::uint64_t Advance(std::uint16_t rawHsync) {
        ElapsedMicroseconds += static_cast<std::uint16_t>(rawHsync - LastRawHsync) * 64u;
        LastRawHsync = rawHsync;
        return ElapsedMicroseconds;
    }

private:
    /// <summary>True once the GPU clock has been adopted as the time base.</summary>
    bool Bound = false;

    /// <summary>Raw HSync counter observed at the previous sample.</summary>
    std::uint16_t LastRawHsync = 0;

    /// <summary>Monotonic microseconds accumulated since binding.</summary>
    std::uint64_t ElapsedMicroseconds = 0;
};

}  // namespace helengine::ps1
