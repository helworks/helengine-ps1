#include "platform/ps1/modules/Ps1SdkDiscSectorReader.hpp"
static_assert(sizeof(helengine::ps1::Ps1SdkDiscSectorReader)<8192);
bool BeginRead(helengine::ps1::Ps1SdkDiscSectorReader& reader) {
    return reader.Begin(&reader,42);
}
#include "platform/ps1/modules/Ps1ModuleGpuFence.hpp"
