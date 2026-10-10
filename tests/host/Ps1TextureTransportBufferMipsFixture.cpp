#include "platform/ps1/rendering/Ps1TextureTransportBuffer.hpp"
#include "psyqo/alloc.h"

/** Compile-only target verifies the nullable SDK allocation branch survives native optimization. */
extern "C" std::uint32_t helengine_ps1_checked_transport_fixture(
    helengine::ps1::Ps1VramAllocator* vram, std::uint16_t x, std::uint16_t y,
    std::uint16_t width, std::uint16_t height, std::uint32_t bytes) {
    std::uint8_t* buffer=nullptr;
    const auto result=helengine::ps1::AllocatePs1TextureTransportBuffer(
        *vram,x,y,width,height,bytes,
        [](std::size_t count) { return static_cast<std::uint8_t*>(psyqo_malloc(count)); },buffer);
    if (result!=helengine::ps1::Ps1TextureTransportAllocation::Allocated) return static_cast<std::uint32_t>(result);
    __builtin_memset(buffer,0,bytes);
    psyqo_free(buffer);
    return 1;
}
