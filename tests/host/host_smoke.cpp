#include <cstdint>

namespace {
constexpr std::uint32_t kRamStart = 0x80010000;
constexpr std::uint32_t kRamTop = 0x80200000;
constexpr std::uint32_t kStackReserve = 64u * 1024u;
constexpr std::uint32_t kFramebufferWidth = 320;
constexpr std::uint32_t kFramebufferHeight = 240;
}

int main() {
    if (kRamTop - kRamStart != 0x1F0000u) return 1;
    if (kRamTop - kStackReserve != 0x801F0000) return 2;
    if (kFramebufferWidth != 320 || kFramebufferHeight != 240) return 3;
    return 0;
}
