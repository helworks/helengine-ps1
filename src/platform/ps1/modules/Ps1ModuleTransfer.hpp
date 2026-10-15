#pragma once

#include "platform/ps1/modules/Ps1DiscSectorReader.hpp"
#include "platform/ps1/modules/Ps1ModulePackage.hpp"

namespace helengine::ps1 {

/// <summary>Two-pass module transfer: bounded hash verification precedes any arena mutation.</summary>
class Ps1ModuleTransfer {
public:
    enum class Phase { Verify, Copy, ClearBss, Complete, Failed };

    Ps1ModuleTransfer(Ps1DiscSectorReader& reader,const Ps1ModuleDescriptor& descriptor,
                      std::uint32_t firstSector,std::uint8_t* arena)
        : Reader(reader),Descriptor(descriptor),FirstSector(firstSector),Arena(arena) {
        if (arena == nullptr || descriptor.FileBytes < Ps1ModulePackage::HeaderBytes ||
            firstSector > UINT32_MAX - (descriptor.FileBytes-1)/2048) State=Phase::Failed;
    }

    ~Ps1ModuleTransfer() { Reader.Cancel(this); }

    /// <summary>Processes at most one sector or one 2048-byte BSS slice, then yields to the host frame.</summary>
    void Advance() {
        if (State==Phase::Complete || State==Phase::Failed) return;
        if (State==Phase::ClearBss) {
            const std::uint32_t remaining=Package.BssBytes-BssCleared;
            const std::uint32_t count=remaining<2048?remaining:2048;
            __builtin_memset(Arena+Package.BssOffset+BssCleared,0,count);
            BssCleared+=count;
            if (BssCleared==Package.BssBytes) State=Phase::Complete;
            return;
        }
        if (!Requested) {
            Requested=Reader.Begin(this,FirstSector+Position/2048);
            return;
        }
        bool failed=false;
        const std::uint8_t* sector=Reader.Poll(this,failed);
        if (sector==nullptr) return;
        if (failed) { Fail(); return; }
        const std::uint32_t remaining=Descriptor.FileBytes-Position;
        const std::uint32_t count=remaining<2048?remaining:2048;
        if (State==Phase::Verify) {
            if (Position==0 && !Package.Read(sector,Descriptor)) { Fail(); return; }
            FileHash.Update(sector,count);
            const std::uint32_t skip=Position==0?Ps1ModulePackage::HeaderBytes:0;
            PayloadHash.Update(sector+skip,count-skip);
        } else {
            const std::uint32_t skip=Position==0?Ps1ModulePackage::HeaderBytes:0;
            CopyHash.Update(sector,count);
            __builtin_memcpy(Arena+Position+skip-Ps1ModulePackage::HeaderBytes,sector+skip,count-skip);
        }
        Reader.Consume(this);Requested=false;Position+=count;
        if (Position!=Descriptor.FileBytes) return;
        if (State==Phase::Verify) {
            std::uint8_t digest[32];
            FileHash.Finish(digest);
            if (!Ps1Sha256::Equal(digest,Descriptor.FileSha256)) { Fail(); return; }
            PayloadHash.Finish(digest);
            if (!Ps1Sha256::Equal(digest,Package.PayloadSha256)) { Fail(); return; }
            State=Phase::Copy;Position=0;
        } else {
            std::uint8_t digest[32];CopyHash.Finish(digest);
            if (!Ps1Sha256::Equal(digest,Descriptor.FileSha256)) { Fail();return; }
            State=Phase::ClearBss;
        }
    }

    Phase GetPhase() const { return State; }
    const Ps1ModulePackage& GetPackage() const { return Package; }

private:
    void Fail() { Reader.Cancel(this);Requested=false;State=Phase::Failed; }
    Ps1DiscSectorReader& Reader;
    const Ps1ModuleDescriptor& Descriptor;
    std::uint32_t FirstSector;
    std::uint8_t* Arena;
    Ps1ModulePackage Package;
    Ps1Sha256 FileHash;
    Ps1Sha256 PayloadHash;
    Ps1Sha256 CopyHash;
    std::uint32_t Position=0;
    std::uint32_t BssCleared=0;
    bool Requested=false;
    Phase State=Phase::Verify;
};

}  // namespace helengine::ps1
