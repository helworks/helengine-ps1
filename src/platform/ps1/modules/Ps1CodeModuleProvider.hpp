#pragma once

#include "IRuntimeCodeModuleProvider.hpp"
#include "platform/ps1/modules/Ps1DiscFileLocator.hpp"
#include "platform/ps1/modules/Ps1DiscSectorReader.hpp"
#include "platform/ps1/modules/Ps1ModulePackage.hpp"
#include "psyqo/gpu.hh"

namespace helengine::ps1 {

/// <summary>Generated immutable association between a load unit and a conflict-graph arena.</summary>
struct Ps1DiscModuleBinding { const Ps1ModuleDescriptor* Descriptor; std::uint32_t Slot; };

/// <summary>Resident Core adapter; objects and callbacks never live in the arena they control.</summary>
class Ps1CodeModuleProvider final : public ::IRuntimeCodeModuleProvider {
public:
    Ps1CodeModuleProvider(Ps1DiscSectorReader& reader,Ps1DiscFileLocator& locator,psyqo::GPU& gpu,
                          const Ps1DiscModuleBinding* bindings,std::size_t count);
    ::IRuntimeCodeModuleLoadOperation* BeginLoad(::RuntimeCodeModuleManifestEntry* entry) override;
    void ReleaseSlot(std::uint32_t slot);
    /** Rejects arena retirement while DMA or GPU commands can still reference static module data. */
    void RequireGpuIdle();

private:
    Ps1DiscSectorReader& Reader;
    Ps1DiscFileLocator& Locator;
    psyqo::GPU& Gpu;
    const Ps1DiscModuleBinding* Bindings;
    std::size_t Count;
    bool Slots[8]{};
};

}  // namespace helengine::ps1
