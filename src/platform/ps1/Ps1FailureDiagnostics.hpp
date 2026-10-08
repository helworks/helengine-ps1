#pragma once
#include <cstdint>

namespace helengine::ps1 {
struct Ps1FailureDiagnostics {
    std::uint32_t Magic;
    std::uint32_t AbortCount;
    std::uint32_t AssetOpenCount;
    std::uint32_t AssetBytes;
    char AbortMessage[192];
    char LastAssetPath[192];
};

inline void CopyDiagnosticText(volatile char* destination, std::uint32_t capacity, const char* source) {
    if (capacity == 0) return;
    std::uint32_t index = 0;
    if (source != nullptr) {
        while (index + 1 < capacity && source[index] != '\0') {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}
}

extern "C" volatile helengine::ps1::Ps1FailureDiagnostics helengine_ps1_failure_diagnostics;
extern "C" void helengine_ps1_record_abort(const char* message);
