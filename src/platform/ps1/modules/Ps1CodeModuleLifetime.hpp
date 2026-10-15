#pragma once

#include <cstddef>
#include <cstdint>

namespace helengine::ps1 {

/// <summary>Resident pin counter; the descriptor and storage outlive every code-module object.</summary>
struct Ps1CodeModulePinState {
    const char* ModuleId;
    std::uint32_t LiveObjects=0;
    bool Executable=false;
};

/// <summary>Installs the generated immutable ID set before any overlay initialization executes.</summary>
void InstallCodeModulePinTable(Ps1CodeModulePinState* states,std::size_t count);
void SetCodeModuleExecutable(const char* moduleId,bool executable);
std::uint32_t GetCodeModuleLiveObjects(const char* moduleId);

}  // namespace helengine::ps1
