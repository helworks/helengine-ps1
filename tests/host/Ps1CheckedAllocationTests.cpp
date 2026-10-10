#include <cassert>
#include <cstring>
#include "platform/ps1/Ps1CheckedAllocation.hpp"

extern "C" {
volatile helengine::ps1::Ps1FrameDiagnostics helengine_ps1_frame_diagnostics{0x48454650u, 0, 68, 7, 180, 1, 1, 65536};
}
namespace {
struct AllocationFailure {};
unsigned int Requested = 0;
unsigned int ScalarCalls = 0;
unsigned int ArrayCalls = 0;
unsigned int FailCalls = 0;
unsigned char Storage[16];
void* Result = Storage;
const char* Message = nullptr;
}
extern "C" void* __real__Znwj(unsigned int bytes) { Requested = bytes; ++ScalarCalls; return Result; }
extern "C" void* __real__Znaj(unsigned int bytes) { Requested = bytes; ++ArrayCalls; return Result; }
[[noreturn]] void he_cpp_custom::Fail(const char* message) {
    // The allocation diagnostic must already exist before the fatal path reports anything.
    assert(helengine_ps1_frame_diagnostics.Stage == 90);
    assert(helengine_ps1_frame_diagnostics.Detail == Requested);
    Message = message;
    ++FailCalls;
    throw AllocationFailure{};
}
// Extracted verbatim from the platform provider by the focused verification script.
#include "allocation-wrappers.inc"
int main() {
    assert(__wrap__Znwj(0) == Storage && Requested == 0 && ScalarCalls == 1);
    assert(__wrap__Znaj(65536) == Storage && Requested == 65536 && ArrayCalls == 1);
    assert(helengine_ps1_frame_diagnostics.Stage == 68 && FailCalls == 0);
    Result = nullptr;
    try { __wrap__Znwj(17); assert(false); } catch (const AllocationFailure&) {}
    assert(FailCalls == 1 && ScalarCalls == 2 && Requested == 17);
    assert(std::strcmp(Message, "PS1 scalar allocation failed") == 0);
    try { __wrap__Znaj(65536); assert(false); } catch (const AllocationFailure&) {}
    assert(FailCalls == 2 && ArrayCalls == 2 && Requested == 65536);
    assert(std::strcmp(Message, "PS1 array allocation failed") == 0);
    Result = Storage;
    assert(__wrap__Znaj(0xffffffffu) == Storage && Requested == 0xffffffffu && ArrayCalls == 3);
    assert(FailCalls == 2);
}
