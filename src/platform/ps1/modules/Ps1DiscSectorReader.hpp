#pragma once

#include <cstdint>

namespace helengine::ps1 {

/// <summary>Resident asynchronous sector interface shared by module loads and scene-content prefetch.</summary>
class Ps1DiscSectorReader {
public:
    virtual ~Ps1DiscSectorReader() = default;
    /// <summary>Starts one 2048-byte read if the drive is idle; never waits for completion.</summary>
    virtual bool Begin(const void* owner, std::uint32_t sector) = 0;
    /// <summary>Returns completed bytes until Consume; nullptr means the caller must yield this frame.</summary>
    virtual const std::uint8_t* Poll(const void* owner, bool& failed) = 0;
    virtual void Consume(const void* owner) = 0;
    /// <summary>Abandons the owner; callbacks touch only the permanent driver, not a deleted operation.</summary>
    virtual void Cancel(const void* owner) = 0;
};

}  // namespace helengine::ps1
