#pragma once
#include "Ps1VramAllocator.hpp"
namespace helengine::ps1 {
struct Ps1VramDiagnostics {
    std::uint32_t Magic=0x4856524d, Serial=0, UsedWords=0, LiveRegions=0;
    std::uint32_t Releases=0, BeforeReleaseWords=0, AfterReleaseWords=0;
    std::uint32_t FailedWidth=0, FailedHeight=0;
    Ps1VramAllocator::Region Regions[Ps1VramAllocator::Capacity]{};
};
extern "C" volatile Ps1VramDiagnostics helengine_ps1_vram_diagnostics;
}
