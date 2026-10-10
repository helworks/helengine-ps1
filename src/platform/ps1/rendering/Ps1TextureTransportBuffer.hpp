#pragma once

#include <cstddef>
#include <cstdint>
#include "platform/ps1/rendering/Ps1VramAllocator.hpp"

namespace helengine::ps1 {
/** Result of reserving CPU transport storage for an already reserved VRAM rectangle. */
enum class Ps1TextureTransportAllocation : std::uint32_t {
    Pending = 0,
    Allocated = 1,
    AllocationFailed = 2,
    RollbackFailed = 3,
    Borrowed = 4
};

/** CPU bytes remain owned by the asset unless packing allocated a temporary. */
struct Ps1TextureTransportBuffer {
    std::uint8_t* Data = nullptr;
    bool OwnsData = false;
};

/**
 * Allocates through an explicitly nullable allocator. A failed allocation releases only the
 * caller's untouched VRAM rectangle; no packing or GPU transfer is performed by this helper.
 */
template <typename Allocator>
Ps1TextureTransportAllocation AllocatePs1TextureTransportBuffer(
    Ps1VramAllocator& vram, std::uint16_t x, std::uint16_t y,
    std::uint16_t widthWords, std::uint16_t height, std::size_t bytes,
    Allocator allocate, std::uint8_t*& buffer) {
    buffer = allocate(bytes);
    if (buffer != nullptr) return Ps1TextureTransportAllocation::Allocated;
    if (!vram.Release(x, y, widthWords, height)) return Ps1TextureTransportAllocation::RollbackFailed;
    return Ps1TextureTransportAllocation::AllocationFailed;
}

/** Borrows packed rows only when byte length, word count and alignment satisfy GPU DMA. */
template <typename Allocator>
Ps1TextureTransportAllocation PreparePs1TextureTransportBuffer(
    Ps1VramAllocator& vram, std::uint16_t x, std::uint16_t y,
    std::uint16_t widthWords, std::uint16_t height, std::uint16_t width,
    std::uint16_t pixelsPerWord, std::uint8_t* source, std::size_t sourceBytes,
    std::size_t transportBytes, Allocator allocate, Ps1TextureTransportBuffer& buffer) {
    buffer = {};
    if (source != nullptr && pixelsPerWord != 0 && width % pixelsPerWord == 0 &&
        sourceBytes == transportBytes && transportBytes % 4 == 0 &&
        reinterpret_cast<std::uintptr_t>(source) % 4 == 0) {
        buffer.Data = source;
        return Ps1TextureTransportAllocation::Borrowed;
    }
    const auto result = AllocatePs1TextureTransportBuffer(
        vram, x, y, widthWords, height, transportBytes, allocate, buffer.Data);
    buffer.OwnsData = result == Ps1TextureTransportAllocation::Allocated;
    return result;
}

/** Frees only an owned temporary after synchronous DMA stops reading its bytes. */
template <typename Deallocator>
void ReleasePs1TextureTransportBuffer(Ps1TextureTransportBuffer& buffer, Deallocator release) {
    if (buffer.OwnsData && buffer.Data != nullptr) release(buffer.Data);
    buffer = {};
}

/** Fixed RAM telemetry records the actual nullable allocation result before any pixel writes. */
struct Ps1TextureTransportDiagnostics {
    std::uint32_t Magic;
    std::uint32_t Sequence;
    std::uint32_t ExpectedBytes;
    std::uint32_t AssetAddress;
    std::uint32_t BufferAddress;
    std::uint32_t AllocationResult;
    std::uint32_t VramWordsBefore;
    std::uint32_t VramWordsAfter;
};
}

extern "C" volatile helengine::ps1::Ps1TextureTransportDiagnostics helengine_ps1_texture_transport_diagnostics;
