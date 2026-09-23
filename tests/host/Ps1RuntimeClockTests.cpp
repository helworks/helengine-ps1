#include "platform/ps1/Ps1RuntimeClock.hpp"

#include <cstdint>

int main() {
    helengine::ps1::Ps1RuntimeClock clock;
    // Static initializers run before the GPU clock exists: they read the epoch and stay monotonic.
    if (clock.IsBound() || clock.Elapsed() != 0) return 1;
    if (clock.Elapsed() != 0) return 2;

    // Binding adopts the current HSync counter as the epoch without counting earlier time.
    clock.Bind(5000u);
    if (!clock.IsBound() || clock.Elapsed() != 0) return 3;
    if (clock.Advance(5000u) != 0) return 4;
    // The hardware counter advances during Core::Draw, unlike GPU::now(), which is cached.
    if (clock.Advance(5001u) != 64) return 5;
    if (clock.Elapsed() != 64) return 6;

    // Elapsed time accumulates across samples, quantized to one 64 us scanline.
    if (clock.Advance(5003u) != 192) return 7;

    // The 16-bit HSync counter wraps after roughly 4.2 seconds; the extended clock must not.
    helengine::ps1::Ps1RuntimeClock wrapping;
    wrapping.Bind(0xFFF0u);
    if (wrapping.Advance(0x0010u) != 32u * 64u) return 8;
    if (wrapping.Advance(0x0010u) != 32u * 64u) return 9;
    return 0;
}
