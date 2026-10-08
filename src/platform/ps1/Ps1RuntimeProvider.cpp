#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "platform/ps1/Ps1RuntimeClock.hpp"
#include "psyqo/gpu.hh"
#include "psyqo/kernel.hh"
#include "common/syscalls/syscalls.h"
#include "common/hardware/counters.h"
#include "platform/ps1/Ps1FailureDiagnostics.hpp"

extern "C" {
volatile helengine::ps1::Ps1FailureDiagnostics helengine_ps1_failure_diagnostics{0x48454644u, 0, 0, 0, {}, {}};
void helengine_ps1_record_abort(const char* message) {
    helengine::ps1::CopyDiagnosticText(helengine_ps1_failure_diagnostics.AbortMessage, 192, message);
    helengine_ps1_failure_diagnostics.AbortCount = helengine_ps1_failure_diagnostics.AbortCount + 1;
}
[[noreturn]] void __real__ZN5psyqo6Kernel8Internal5abortEPKcSt15source_location(const char*, std::source_location);
[[noreturn]] void __real__ZN5psyqo6Kernel8Internal5abortEv();
[[noreturn]] void __wrap__ZN5psyqo6Kernel8Internal5abortEPKcSt15source_location(const char* message, std::source_location location) {
    helengine_ps1_record_abort(message);
    __real__ZN5psyqo6Kernel8Internal5abortEPKcSt15source_location(message, location);
}
[[noreturn]] void __wrap__ZN5psyqo6Kernel8Internal5abortEv() {
    helengine_ps1_record_abort("SDK abort without message");
    __real__ZN5psyqo6Kernel8Internal5abortEv();
}
}

namespace {
psyqo::GPU* RuntimeGpu = nullptr;
helengine::ps1::Ps1RuntimeClock RuntimeClock;
}

void helengine::ps1::InitializeRuntimeClock(psyqo::GPU& gpu) {
    RuntimeGpu = &gpu;
    RuntimeClock.Bind(COUNTERS[1].value);
}

std::uint64_t he_cpp_custom::MonotonicMicroseconds() {
    // Generated static initializers may sample the clock before the GPU exists. They observe the
    // epoch until the hardware HSync counter is initialized.
    if (RuntimeGpu == nullptr) { return RuntimeClock.Elapsed(); }
    return RuntimeClock.Advance(COUNTERS[1].value);
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
