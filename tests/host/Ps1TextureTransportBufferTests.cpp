#include <cassert>
#include <cstdio>
#include <cstring>
#include "platform/ps1/rendering/Ps1TextureTransportBuffer.hpp"
#include "platform/ps1/rendering/Ps1IndexedTextureUpload.hpp"

using namespace helengine::ps1;

int main() {
    alignas(8) static std::uint8_t logo[65536];
    alignas(8) static std::uint8_t temporary[65536];
    Ps1VramAllocator vram;
    std::uint16_t liveX=0,liveY=0,x=0,y=0;
    assert(vram.TryAllocate(32,64,liveX,liveY));
    const auto baseline=vram.UsedWords();
    unsigned allocations=0,frees=0,packing=0,gpuCalls=0;
    auto allocate=[&](std::size_t bytes)->std::uint8_t* { ++allocations;assert(bytes<=sizeof(temporary));return temporary; };
    auto release=[&](std::uint8_t* pointer) { ++frees;assert(pointer==temporary); };
    Ps1TextureTransportBuffer buffer;

    assert(vram.TryAllocateWithinHorizontalPage(128,256,2,x,y));
    auto result=PreparePs1TextureTransportBuffer(vram,x,y,128,256,256,2,logo,sizeof(logo),65536,allocate,buffer);
    assert(result==Ps1TextureTransportAllocation::Borrowed && buffer.Data==logo && !buffer.OwnsData);
    assert(allocations==0);
    ReleasePs1TextureTransportBuffer(buffer,release);
    assert(frees==0 && buffer.Data==nullptr);
    assert(vram.Release(x,y,128,256));

    // The shared LOW-first Indexed4 stream crosses an odd row boundary; DMA rows need repacking.
    alignas(8) std::uint8_t indexed4[]={0x21,0x43,0x65};
    assert(vram.TryAllocateWithinHorizontalPage(1,2,4,x,y));
    result=PreparePs1TextureTransportBuffer(vram,x,y,1,2,3,4,indexed4,3,4,allocate,buffer);
    assert(result==Ps1TextureTransportAllocation::Allocated && buffer.OwnsData);
    std::memset(buffer.Data,0,4);
    PackPs1Indexed4UploadRow(indexed4,0,3,buffer.Data);
    PackPs1Indexed4UploadRow(indexed4,3,3,buffer.Data+2);
    const std::uint8_t expected4[]={0x21,0x03,0x54,0x06};
    assert(std::memcmp(buffer.Data,expected4,4)==0);
    ReleasePs1TextureTransportBuffer(buffer,release);
    assert(allocations==1 && frees==1);
    assert(vram.Release(x,y,1,2));

    // Indexed8 odd rows receive zero padding; their source bytes remain unmodified.
    alignas(8) std::uint8_t indexed8[]={1,2,3,4,5,6};
    assert(vram.TryAllocateWithinHorizontalPage(2,2,2,x,y));
    result=PreparePs1TextureTransportBuffer(vram,x,y,2,2,3,2,indexed8,6,8,allocate,buffer);
    assert(result==Ps1TextureTransportAllocation::Allocated);
    std::memset(buffer.Data,0,8);
    std::memcpy(buffer.Data,indexed8,3);std::memcpy(buffer.Data+4,indexed8+3,3);
    const std::uint8_t expected8[]={1,2,3,0,4,5,6,0};
    assert(std::memcmp(buffer.Data,expected8,8)==0 && indexed8[5]==6);
    ReleasePs1TextureTransportBuffer(buffer,release);
    assert(vram.Release(x,y,2,2));

    // Failure occurs before any transport writes or transfer and preserves the unrelated region.
    assert(vram.TryAllocateWithinHorizontalPage(2,2,2,x,y));
    auto fail=[&](std::size_t bytes)->std::uint8_t* { ++allocations;assert(bytes==8);return nullptr; };
    result=PreparePs1TextureTransportBuffer(vram,x,y,2,2,3,2,indexed8,6,8,fail,buffer);
    if (result==Ps1TextureTransportAllocation::Allocated || result==Ps1TextureTransportAllocation::Borrowed) {
        ++packing;std::memset(buffer.Data,0,8);++gpuCalls;
    }
    assert(result==Ps1TextureTransportAllocation::AllocationFailed && buffer.Data==nullptr && !buffer.OwnsData);
    assert(packing==0 && gpuCalls==0 && vram.UsedWords()==baseline && vram.LiveCount()==1);
    assert(indexed8[0]==1 && indexed8[5]==6);
    assert(vram.GetRegion(0).x==liveX && vram.GetRegion(0).y==liveY);
    std::uint16_t reuseX=0,reuseY=0;
    assert(vram.TryAllocateWithinHorizontalPage(2,2,2,reuseX,reuseY) && reuseX==x && reuseY==y);
    assert(vram.Release(reuseX,reuseY,2,2));

    // Alignment and odd DMA word count independently prevent borrowing.
    assert(vram.TryAllocateWithinHorizontalPage(2,2,2,x,y));
    result=PreparePs1TextureTransportBuffer(vram,x,y,2,2,4,2,logo+1,8,8,allocate,buffer);
    assert(result==Ps1TextureTransportAllocation::Allocated);
    ReleasePs1TextureTransportBuffer(buffer,release);assert(vram.Release(x,y,2,2));
    assert(vram.TryAllocateWithinHorizontalPage(1,1,2,x,y));
    result=PreparePs1TextureTransportBuffer(vram,x,y,1,1,2,2,logo,2,2,allocate,buffer);
    assert(result==Ps1TextureTransportAllocation::Allocated);
    ReleasePs1TextureTransportBuffer(buffer,release);assert(vram.Release(x,y,1,1));

    // An invalid caller reservation is reported, not hidden by releasing a neighbor.
    std::uint8_t* rejected=reinterpret_cast<std::uint8_t*>(1);
    result=AllocatePs1TextureTransportBuffer(vram,999,999,2,2,8,fail,rejected);
    assert(result==Ps1TextureTransportAllocation::RollbackFailed && rejected==nullptr && vram.UsedWords()==baseline);
    std::puts("PASS: borrowed CI8, odd CI4/CI8 padding, allocation rollback, alignment, DMA word parity and rollback error.");
}
