#pragma once

#include "platform/ps1/modules/Ps1DiscSectorReader.hpp"
#include "platform/ps1/modules/Ps1DiscFileLocator.hpp"
#include "psyqo/cdrom-device.hh"
#include "psyqo/iso9660-parser.hh"

namespace helengine::ps1 {

/// <summary>Serializes CD requests through one DMA-aligned sector buffer with callback-safe cancellation.</summary>
class Ps1SdkDiscSectorReader final : public Ps1DiscSectorReader, public Ps1DiscFileLocator {
public:
    explicit Ps1SdkDiscSectorReader(psyqo::CDRomDevice& device) : Device(device),Parser(&device) {}

    bool Begin(const void* owner, std::uint32_t sector) override {
        if (owner == nullptr || !DriveReady || Pending || Owner != nullptr) return false;
        Owner=owner;Pending=true;Completed=false;Success=false;Lookup=false;
        Device.readSectors(sector,1,Buffer,[this](bool success) {
            Success=success;Pending=false;Completed=true;
            if (Owner == nullptr) Completed=false;
        });
        return true;
    }

    const std::uint8_t* Poll(const void* owner, bool& failed) override {
        failed=false;
        if (Owner != owner || !Completed || Lookup) return nullptr;
        failed=!Success;
        return Buffer;
    }

    void Consume(const void* owner) override {
        if (Owner == owner && !Pending) { Owner=nullptr;Completed=false; }
    }

    void Cancel(const void* owner) override {
        if (Owner == owner) { Owner=nullptr;Completed=false; }
    }

    bool BeginFind(const void* owner,const char* path) override {
        if (owner==nullptr || path==nullptr || Pending || Owner!=nullptr) return false;
        unsigned length=0;
        while (path[length]!=0 && length<sizeof(Path)-1) { Path[length]=path[length];++length; }
        if (length==0 || path[length]!=0) return false;
        Path[length]=0;Owner=owner;Lookup=true;Success=false;Completed=false;
        if (!DriveReady) {
            Pending=true;
            Device.reset([this](bool success) { Pending=false;DriveReady=success;Success=success;Completed=!success; });
        } else if (Initialized) { Find(); }
        else { InitializeParser(); }
        return true;
    }

    bool PollFind(const void* owner,Ps1DiscFileRange& file,bool& failed) override {
        failed=false;
        if (Owner!=owner || !Lookup) return false;
        if (!Pending && !Completed) {
            if (Initialized) Find();
            else if (DriveReady) InitializeParser();
            return false;
        }
        if (!Completed) return false;
        failed=!Success || Entry.type!=psyqo::ISO9660Parser::DirEntry::FILE || Entry.size==0;
        if (!failed) { file.FirstSector=Entry.LBA;file.ByteLength=Entry.size; }
        return true;
    }

private:
    void InitializeParser() {
        Pending=true;
        Parser.initialize([this](bool success) { Pending=false;Initialized=success;Success=success;Completed=!success; });
    }
    void Find() {
        Pending=true;
        Parser.getDirentry(Path,&Entry,[this](bool success) {
            Success=success;Pending=false;Completed=true;
            if (Owner==nullptr) Completed=false;
        });
    }
    psyqo::CDRomDevice& Device;
    psyqo::ISO9660Parser Parser;
    psyqo::ISO9660Parser::DirEntry Entry;
    const void* Owner=nullptr;
    bool Pending=false;
    bool Completed=false;
    bool Success=false;
    bool Lookup=false;
    bool Initialized=false;
    bool DriveReady=false;
    char Path[64]{};
    alignas(4) std::uint8_t Buffer[2048]{};
};

}  // namespace helengine::ps1
