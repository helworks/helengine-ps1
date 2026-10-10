#pragma once
#include <cstdint>
#include "platform/ps1/Ps1FrameDiagnostics.hpp"

namespace he_cpp_custom {
[[noreturn]] void Fail(const char* message);
}

namespace helengine::ps1 {
/** Preserves successful SDK allocations and records a failed request before any reporting. */
inline void* RequireAllocatedStorage(void* storage, std::uint32_t bytes, const char* message) {
    if (storage == nullptr) {
        MarkFrameStage(Ps1FrameStage::FatalBeforeLog, bytes);
        he_cpp_custom::Fail(message);
    }
    return storage;
}
}
