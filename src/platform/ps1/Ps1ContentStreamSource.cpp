#include "platform/ps1/Ps1ContentStreamSource.hpp"

#include "common/syscalls/syscalls.h"
#include "platform/ps1/Ps1EmbeddedAssetStream.hpp"
#include "platform/ps1/Ps1RuntimeProvider.hpp"
#include "platform/ps1/Ps1FailureDiagnostics.hpp"

namespace helengine::ps1 {

Ps1ContentStreamSource::Ps1ContentStreamSource(const std::uint8_t* image, std::size_t imageLength) {
    if (!Archive.Open(image, imageLength)) {
        ramsyscall_printf("PS1_AUTHORED_ARCHIVE_INVALID bytes=%u\n", static_cast<unsigned>(imageLength));
        he_cpp_custom::Fail("Embedded content archive is malformed");
    }
    ramsyscall_printf("PS1_AUTHORED_ARCHIVE_READY bytes=%u assets=%u\n", static_cast<unsigned>(imageLength),
                      static_cast<unsigned>(Archive.EntryCount()));
}

::Stream* Ps1ContentStreamSource::OpenRead(HeCppString assetPath) {
    CopyDiagnosticText(helengine_ps1_failure_diagnostics.LastAssetPath, 192, assetPath.c_str());
    helengine_ps1_failure_diagnostics.AssetOpenCount = helengine_ps1_failure_diagnostics.AssetOpenCount + 1;
    helengine_ps1_failure_diagnostics.AssetBytes = 0;
    Ps1EmbeddedAsset asset{};
    if (!Archive.TryFind(assetPath.c_str(), asset)) {
        ramsyscall_printf("PS1_AUTHORED_ASSET_MISSING %s\n", assetPath.c_str());
        he_cpp_custom::Fail("Embedded content archive does not contain the requested asset");
    }
    helengine_ps1_failure_diagnostics.AssetBytes = static_cast<std::uint32_t>(asset.Length);
    ramsyscall_printf("PS1_AUTHORED_ASSET_OPEN %s bytes=%u\n", assetPath.c_str(), static_cast<unsigned>(asset.Length));
    return new Ps1EmbeddedAssetStream(asset.Data, asset.Length);
}

}  // namespace helengine::ps1
