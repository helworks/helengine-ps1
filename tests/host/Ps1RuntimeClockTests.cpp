#include "platform/ps1/Ps1RuntimeClock.hpp"

#include <cstdint>

int main() {
    helengine::ps1::Ps1RuntimeClock clock;
    // Static initializers run before the GPU clock exists: they read the epoch and stay monotonic.
    if (clock.IsBound() || clock.Elapsed() != 0) return 1;
    if (clock.Elapsed() != 0) return 2;

    // Binding adopts the current raw GPU timestamp as the epoch without counting earlier time.
    clock.Bind(5000u);
    if (!clock.IsBound() || clock.Elapsed() != 0) return 3;
    if (clock.Advance(5000u) != 0) return 4;
    if (clock.Advance(5500u) != 500) return 5;
    if (clock.Elapsed() != 500) return 6;

    // Elapsed time accumulates across samples.
    if (clock.Advance(6000u) != 1000) return 7;

    // The 32-bit GPU counter wraps after roughly an hour; the extended clock must not.
    helengine::ps1::Ps1RuntimeClock wrapping;
    wrapping.Bind(0xFFFFFF00u);
    if (wrapping.Advance(0x00000010u) != 0x110) return 8;
    if (wrapping.Advance(0x00000010u) != 0x110) return 9;
    return 0;
}
