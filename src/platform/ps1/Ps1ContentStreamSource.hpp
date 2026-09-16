#pragma once

#include <cstddef>
#include <cstdint>

#include "IContentStreamSource.hpp"
#include "platform/ps1/Ps1EmbeddedArchive.hpp"

namespace helengine::ps1 {

/// <summary>
/// Serves Core's content requests from the archive linked into the PS-X EXE. Paths are the cooked
/// relative paths the build manifest declared; anything else fails through the runtime failure
/// policy so a missing asset stops the boot instead of degrading silently.
/// </summary>
class Ps1ContentStreamSource final : public ::IContentStreamSource {
public:
    /// <summary>Validates and adopts the linked archive image; fails fatally when it is malformed.</summary>
    Ps1ContentStreamSource(const std::uint8_t* image, std::size_t imageLength);

    /// <summary>Opens a read-only stream over the named asset or fails fatally when it is absent.</summary>
    ::Stream* OpenRead(HeCppString assetPath) override;

    /// <summary>Number of assets available in the archive.</summary>
    std::size_t AssetCount() const { return Archive.EntryCount(); }

private:
    /// <summary>Validated archive view.</summary>
    Ps1EmbeddedArchive Archive;
};

}  // namespace helengine::ps1
