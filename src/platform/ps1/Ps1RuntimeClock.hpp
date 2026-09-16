#pragma once

#include <cstdint>

namespace helengine::ps1 {

/// <summary>
/// Extends PSYQo's 32-bit microsecond GPU counter into the 64-bit monotonic clock the generated
/// runtime expects. The clock is unbound until the GPU is initialized; generated static
/// initializers (for example the engine logger's stopwatch) may read it earlier and observe the
/// epoch, which matches PSYQo's own timer reading zero before the first page flip.
/// </summary>
class Ps1RuntimeClock {
public:
    /// <summary>Reports whether a raw GPU timestamp has been adopted as the epoch.</summary>
    bool IsBound() const { return Bound; }

    /// <summary>Adopts the current raw GPU microsecond counter as the epoch. Earlier time is not counted.</summary>
    void Bind(std::uint32_t rawMicroseconds) {
        Bound = true;
        LastRawMicroseconds = rawMicroseconds;
        ElapsedMicroseconds = 0;
    }

    /// <summary>Returns the microseconds accumulated since binding, or the epoch while unbound.</summary>
    std::uint64_t Elapsed() const { return ElapsedMicroseconds; }

    /// <summary>
    /// Folds a new raw GPU sample into the extended clock and returns the updated elapsed value.
    /// Unsigned 32-bit subtraction keeps the clock monotonic across counter wrap-around.
    /// </summary>
    std::uint64_t Advance(std::uint32_t rawMicroseconds) {
        ElapsedMicroseconds += static_cast<std::uint32_t>(rawMicroseconds - LastRawMicroseconds);
        LastRawMicroseconds = rawMicroseconds;
        return ElapsedMicroseconds;
    }

private:
    /// <summary>True once the GPU clock has been adopted as the time base.</summary>
    bool Bound = false;

    /// <summary>Raw GPU microsecond counter observed at the previous sample.</summary>
    std::uint32_t LastRawMicroseconds = 0;

    /// <summary>Monotonic microseconds accumulated since binding.</summary>
    std::uint64_t ElapsedMicroseconds = 0;
};

}  // namespace helengine::ps1
