#pragma once
#include <cstdint>

namespace helengine::ps1 {
/// Validates selected numeric storage/evaluation on the guest before a CPU-profile capture starts.
class Ps1NumericPolicySelfTest {
public:
    /// Runs bounded IEEE fixtures once at startup; a mismatch fails the instrumented build explicitly.
    static void Run();
private:
    /// Compares half encodings with NaN classification and a separately declared math-library tolerance.
    static bool Matches(std::uint16_t expected, std::uint16_t observed, unsigned tolerance);
};
}
