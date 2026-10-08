#include "platform/ps1/Ps1Video.hpp"

namespace helengine::ps1 {

void Ps1Video::Initialize(psyqo::GPU& gpu) {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::NTSC)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE)
        .set(psyqo::GPU::MiscSetting::CLEAR_VRAM);
    gpu.initialize(config);

    // PSYQo's default NTSC range ends at 255, so DuckStation reports 255 - 16 = 239 active
    // lines even though the framebuffer and drawing area are 240 lines tall. GP1(07)'s end
    // coordinate is exclusive; use 256 to expose the full 240-line progressive framebuffer.
    constexpr unsigned NtscVerticalStart = 16;
    constexpr unsigned NtscVerticalEnd = 256;
    psyqo::Hardware::GPU::Ctrl = 0x07000000u | NtscVerticalStart | (NtscVerticalEnd << 10u);
}

}  // namespace helengine::ps1
