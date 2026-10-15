#pragma once

#include <cstdint>

namespace helengine::ps1 {

/// <summary>A validated ISO file range; lookups never allocate a buffer for the whole file.</summary>
struct Ps1DiscFileRange { std::uint32_t FirstSector=0; std::uint32_t ByteLength=0; };

/// <summary>Asynchronous path lookup used before sector reads, sharing the same physical-drive lease.</summary>
class Ps1DiscFileLocator {
public:
    virtual ~Ps1DiscFileLocator() = default;
    virtual bool BeginFind(const void* owner,const char* path) = 0;
    virtual bool PollFind(const void* owner,Ps1DiscFileRange& file,bool& failed) = 0;
};

}  // namespace helengine::ps1
