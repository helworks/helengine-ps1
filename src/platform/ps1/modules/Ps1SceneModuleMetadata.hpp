#pragma once

#include "runtime/native_runtime.hpp"
#include "runtime/array.hpp"
#include "platform/ps1/modules/Ps1CodeModuleProvider.hpp"
#include "platform/ps1/modules/Ps1CodeModuleLifetime.hpp"
#include "platform/ps1/modules/Ps1DiscContent.hpp"

class RuntimeCodeModuleManifest;
class RuntimeCodeModuleManifestEntry;

namespace helengine::ps1 {

/** Declares the generated immutable disc tables independently of their physical addresses and offsets. */
const Ps1DiscModuleBinding* Ps1GeneratedModules(std::size_t* count);
Ps1CodeModulePinState* Ps1GeneratedPins(std::size_t* count);
const Ps1DiscAssetDescriptor* Ps1GeneratedAssets(std::uint32_t* count);
const Ps1DiscSceneDependencies* Ps1GeneratedScenes(std::uint32_t* count);
std::uint32_t Ps1GeneratedArchiveBytes();
std::uint32_t Ps1GeneratedCacheBudgetBytes();
Array<HeCppString>* Ps1GeneratedRequiredModules(const char* sceneId);
::RuntimeCodeModuleManifest* Ps1GeneratedModuleManifest();

/** Allocates the same owned runtime entry used by generated metadata, borrowing its inputs as the managed constructor does. */
::RuntimeCodeModuleManifestEntry* Ps1CreateDiscModuleEntry(const HeCppString& moduleId, Array<HeCppString>* dependencies);

/** Allocates the owned runtime manifest while borrowing the entries array, preserving the existing managed constructor contract. */
::RuntimeCodeModuleManifest* Ps1CreateDiscModuleManifest(Array<::RuntimeCodeModuleManifestEntry*>* entries);

}  // namespace helengine::ps1
