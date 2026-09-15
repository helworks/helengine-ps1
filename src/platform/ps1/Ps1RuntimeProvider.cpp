#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "platform/ps1/Ps1RuntimeClock.hpp"
#include "psyqo/gpu.hh"
#include "psyqo/kernel.hh"
#include "common/syscalls/syscalls.h"

namespace {
psyqo::GPU* RuntimeGpu = nullptr;
helengine::ps1::Ps1RuntimeClock RuntimeClock;
}

void helengine::ps1::InitializeRuntimeClock(psyqo::GPU& gpu) {
    RuntimeGpu = &gpu;
    RuntimeClock.Bind(gpu.now());
}

std::uint64_t he_cpp_custom::MonotonicMicroseconds() {
    // Generated static initializers may sample the clock before the GPU exists. They observe the
    // epoch, exactly as PSYQo's own microsecond counter reads zero before the first page flip.
    if (RuntimeGpu == nullptr) { return RuntimeClock.Elapsed(); }
    return RuntimeClock.Advance(RuntimeGpu->now());
}

[[noreturn]] void he_cpp_custom::Fail(const char* message) {
    ramsyscall_printf("PS1_CORE_FATAL %s\n", message);
    psyqo::Kernel::abort(message);
}

// libsupc++ compares type_info names with strcmp when it cannot merge them. The
// freestanding SDK ships no libc, so the RTTI-enabled Core link supplies it here.
extern "C" int strcmp(const char* left, const char* right) {
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return static_cast<int>(static_cast<unsigned char>(*left)) - static_cast<int>(static_cast<unsigned char>(*right));
}
