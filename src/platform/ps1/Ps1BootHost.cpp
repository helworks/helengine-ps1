#include "platform/ps1/Ps1BootHost.hpp"

#include "platform/ps1/Ps1CubeScene.hpp"
#include "platform/ps1/Ps1Video.hpp"
#include "common/syscalls/syscalls.h"
#include "psyqo/alloc.h"
#include "psyqo/hardware/cpu.hh"
#include "psyqo/kernel.hh"

#include <cstdint>

extern "C" char __stack_start;

namespace {
helengine::ps1::Ps1CubeScene g_rootScene;
constexpr std::uint32_t kStackGuard = 0x50533147;
volatile std::uint32_t* const g_stackGuard = reinterpret_cast<volatile std::uint32_t*>(&__stack_start);
}

namespace helengine::ps1 {

void Ps1BootHost::prepare() { Ps1Video::Initialize(gpu()); }

void Ps1BootHost::start() {
    *g_stackGuard = kStackGuard;
    ramsyscall_printf("PS1_STACK_GUARD address=%p value=%08x\n", g_stackGuard, kStackGuard);
    ramsyscall_printf("PS1_HEAP_INIT start=%p end=%p\n", psyqo_heap_start(), psyqo_heap_end());
    ramsyscall_printf("PS1_IRQ_MASK_AFTER_GPU=%08x GPU_IRQ_MASKED=%u\n", static_cast<uint32_t>(psyqo::Hardware::CPU::IMask),
                      psyqo::Hardware::CPU::IMask.isSet(psyqo::Hardware::CPU::IRQ::GPU) ? 0u : 1u);
}

void Ps1BootHost::frame() {
    psyqo::Kernel::assert(*g_stackGuard == kStackGuard, "PS1_STACK_GUARD_CORRUPTED");
    psyqo::Application::frame();
}

void Ps1BootHost::createScene() { pushScene(&g_rootScene); }

}  // namespace helengine::ps1
