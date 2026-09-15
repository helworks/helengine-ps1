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
}

}  // namespace helengine::ps1
