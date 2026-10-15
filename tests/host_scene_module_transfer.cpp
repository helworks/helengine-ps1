#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "platform/ps1/modules/Ps1ModuleTransfer.hpp"

using namespace helengine::ps1;

class DelayedDisc final : public Ps1DiscSectorReader {
public:
    std::vector<std::uint8_t> Image;
    const void* Owner=nullptr;
    std::uint32_t Sector=0;
    bool Complete=false;
    unsigned Reads=0;
    bool Begin(const void* owner,std::uint32_t sector) override {
        if (Owner!=nullptr) return false;
        Owner=owner;Sector=sector;Complete=false;++Reads;return true;
    }
    const std::uint8_t* Poll(const void* owner,bool& failed) override {
        failed=false;
        return owner==Owner && Complete ? Image.data()+Sector*2048 : nullptr;
    }
    void Consume(const void* owner) override { if (Owner==owner) Owner=nullptr; }
    void Cancel(const void* owner) override { Consume(owner); }
};

void WriteU32(std::uint8_t* bytes,std::uint32_t value) {
    for (unsigned i=0;i<4;++i) bytes[i]=static_cast<std::uint8_t>(value>>(8*i));
}

void BuildImage(DelayedDisc& disc,Ps1ModuleDescriptor& descriptor) {
    const std::uint32_t payload=5000;
    disc.Image.assign(6144,0);
    descriptor={"fixture","MODULE.BIN",128+payload,0x80140000,8192,{}, {}};
    const std::uint32_t header[15]={0x314d5048,1,128,128+payload,descriptor.LoadAddress,payload,payload,3072,0,4,8,0,8,0,0};
    for (unsigned i=0;i<15;++i) WriteU32(disc.Image.data()+i*4,header[i]);
    for (std::uint32_t i=0;i<payload;++i) disc.Image[128+i]=static_cast<std::uint8_t>(i*7);
    Ps1Sha256 payloadHash;payloadHash.Update(disc.Image.data()+128,payload);payloadHash.Finish(disc.Image.data()+92);
    Ps1Sha256 fileHash;fileHash.Update(disc.Image.data(),descriptor.FileBytes);fileHash.Finish(descriptor.FileSha256);
}

void Run(Ps1ModuleTransfer& transfer,DelayedDisc& disc) {
    for (unsigned step=0;step<100;++step) {
        transfer.Advance();disc.Complete=true;
        if (transfer.GetPhase()==Ps1ModuleTransfer::Phase::Complete || transfer.GetPhase()==Ps1ModuleTransfer::Phase::Failed) return;
    }
    assert(false);
}

int main() {
    const std::uint8_t expected[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    Ps1Sha256 hash;hash.Update(reinterpret_cast<const std::uint8_t*>("a"),1);hash.Update(reinterpret_cast<const std::uint8_t*>("bc"),2);
    std::uint8_t digest[32];hash.Finish(digest);assert(Ps1Sha256::Equal(digest,expected));
    DelayedDisc disc;Ps1ModuleDescriptor descriptor{};BuildImage(disc,descriptor);
    std::array<std::uint8_t,8192> arena;arena.fill(0xcd);
    {
        Ps1ModuleTransfer transfer(disc,descriptor,0,arena.data());
        transfer.Advance();transfer.Advance(); // pending read cannot advance or mutate the arena
        assert(disc.Reads==1 && arena[0]==0xcd);
        while (transfer.GetPhase()==Ps1ModuleTransfer::Phase::Verify) { disc.Complete=true;transfer.Advance(); }
        for (auto byte:arena) assert(byte==0xcd);
        Run(transfer,disc);assert(transfer.GetPhase()==Ps1ModuleTransfer::Phase::Complete);
        assert(std::memcmp(arena.data(),disc.Image.data()+128,5000)==0);
        for (unsigned i=5000;i<8072;++i) assert(arena[i]==0);
        for (unsigned i=8072;i<8192;++i) assert(arena[i]==0xcd);
    }
    for (unsigned cycle=0;cycle<2;++cycle) {
        arena.fill(0xee);Ps1ModuleTransfer transfer(disc,descriptor,0,arena.data());Run(transfer,disc);
        assert(transfer.GetPhase()==Ps1ModuleTransfer::Phase::Complete);
    }
    disc.Image[4096]^=1;arena.fill(0x5a);
    { Ps1ModuleTransfer transfer(disc,descriptor,0,arena.data());Run(transfer,disc);assert(transfer.GetPhase()==Ps1ModuleTransfer::Phase::Failed); }
    for (auto byte:arena) assert(byte==0x5a);
    BuildImage(disc,descriptor);WriteU32(disc.Image.data()+44,UINT32_MAX);
    { Ps1ModuleTransfer transfer(disc,descriptor,0,arena.data());Run(transfer,disc);assert(transfer.GetPhase()==Ps1ModuleTransfer::Phase::Failed); }
    for (auto byte:arena) assert(byte==0x5a);
    BuildImage(disc,descriptor);
    { Ps1ModuleTransfer transfer(disc,descriptor,0,arena.data());transfer.Advance();assert(disc.Owner!=nullptr); }
    assert(disc.Owner==nullptr);
    std::puts("PASS SHA256, asynchronous yield, pre-write hash/bounds, bounded BSS, two reloads, cancellation");
}
