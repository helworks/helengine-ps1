#pragma once

#include <cstdint>
#include "platform/ps1/modules/Ps1Sha256.hpp"

namespace helengine::ps1 {

/// <summary>Resident build table binds one immutable module file to one nonoverlapping linker arena.</summary>
struct Ps1ModuleDescriptor {
    const char* ModuleId;
    const char* DiscPath;
    std::uint32_t FileBytes;
    std::uint32_t LoadAddress;
    std::uint32_t ArenaBytes;
    std::uint8_t ResidentBuildId[32];
    std::uint8_t FileSha256[32];
};

/// <summary>Decoded HPM1 metadata; no on-disc fields are trusted as pointers before validation.</summary>
struct Ps1ModulePackage {
    static constexpr std::uint32_t HeaderBytes = 128;
    std::uint32_t LoadBytes = 0;
    std::uint32_t BssOffset = 0;
    std::uint32_t BssBytes = 0;
    std::uint32_t RegisterOffset = 0;
    std::uint32_t UnregisterOffset = 0;
    std::uint32_t InitArrayOffset = 0;
    std::uint32_t InitArrayCount = 0;
    std::uint32_t FiniArrayOffset = 0;
    std::uint32_t FiniArrayCount = 0;
    std::uint8_t PayloadSha256[32]{};

    static std::uint32_t ReadU32(const std::uint8_t* p) {
        return p[0] | (static_cast<std::uint32_t>(p[1]) << 8) |
            (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    /// <summary>Validates every destination and executable range before a loader writes the arena.</summary>
    bool Read(const std::uint8_t* header, const Ps1ModuleDescriptor& descriptor) {
        if (header == nullptr || descriptor.FileBytes < HeaderBytes || descriptor.ArenaBytes == 0 ||
            descriptor.LoadAddress < 0x80010000u || descriptor.LoadAddress >= 0x801f0000u ||
            descriptor.ArenaBytes > 0x801f0000u - descriptor.LoadAddress) return false;
        if (ReadU32(header) != 0x314d5048u || ReadU32(header+4) != 1 || ReadU32(header+8) != HeaderBytes ||
            ReadU32(header+12) != descriptor.FileBytes || ReadU32(header+16) != descriptor.LoadAddress ||
            ReadU32(header+56) != 0 || ReadU32(header+124) != 0 ||
            !Ps1Sha256::Equal(header+60, descriptor.ResidentBuildId)) return false;
        LoadBytes=ReadU32(header+20);BssOffset=ReadU32(header+24);BssBytes=ReadU32(header+28);
        RegisterOffset=ReadU32(header+32);UnregisterOffset=ReadU32(header+36);
        InitArrayOffset=ReadU32(header+40);InitArrayCount=ReadU32(header+44);
        FiniArrayOffset=ReadU32(header+48);FiniArrayCount=ReadU32(header+52);
        if (LoadBytes != descriptor.FileBytes-HeaderBytes || LoadBytes > descriptor.ArenaBytes ||
            BssOffset < LoadBytes || BssOffset > descriptor.ArenaBytes || BssBytes > descriptor.ArenaBytes-BssOffset ||
            !EntryValid(RegisterOffset) || !EntryValid(UnregisterOffset) ||
            !ArrayValid(InitArrayOffset,InitArrayCount) || !ArrayValid(FiniArrayOffset,FiniArrayCount)) return false;
        for (unsigned i=0;i<32;++i) PayloadSha256[i]=header[92+i];
        return true;
    }

    /// <summary>Checks a constructor/destructor pointer against the verified loaded executable range.</summary>
    bool FunctionValid(std::uint32_t pointer, const Ps1ModuleDescriptor& descriptor) const {
        return pointer >= descriptor.LoadAddress && pointer-descriptor.LoadAddress < LoadBytes && (pointer&3u)==0;
    }

private:
    bool EntryValid(std::uint32_t offset) const { return (offset&3u)==0 && offset < LoadBytes; }
    bool ArrayValid(std::uint32_t offset,std::uint32_t count) const {
        return (offset&3u)==0 && offset<=LoadBytes && count <= (LoadBytes-offset)/4;
    }
};

}  // namespace helengine::ps1
