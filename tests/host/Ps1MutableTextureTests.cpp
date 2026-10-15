#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "platform/ps1/rendering/Ps1MutableTexture.hpp"
#include "platform/ps1/rendering/Ps1RuntimeTexture.hpp"

using namespace helengine::ps1;

extern "C" { volatile Ps1FrameDiagnostics helengine_ps1_frame_diagnostics{}; }

struct MutableGpuSpy {
    struct Call { psyqo::Rect Region; std::vector<std::uint16_t> Pixels; };
    std::vector<Call> Calls;
    int ChainWaits = 0, ReadyWaits = 0;
    std::uint16_t* Shadow = nullptr;
    std::uint16_t BeforeFence = 0;
    void waitChainIdle() { assert(helengine_ps1_frame_diagnostics.Stage == 50); assert(Shadow == nullptr || *Shadow == BeforeFence); ++ChainWaits; }
    void waitReady() { assert(helengine_ps1_frame_diagnostics.Stage == 52); assert(ChainWaits > ReadyWaits); ++ReadyWaits; }
    void uploadToVRAM(const std::uint16_t* source, psyqo::Rect region) {
        assert(ChainWaits == ReadyWaits && ChainWaits > 0);
        assert(helengine_ps1_frame_diagnostics.Stage == 30);
        assert(reinterpret_cast<std::uintptr_t>(source) % 4u == 0);
        const unsigned count = static_cast<unsigned>(region.size.w) * region.size.h;
        assert(count % 2u == 0);
        Calls.push_back({region, std::vector<std::uint16_t>(source, source + count)});
    }
};

static int Frees = 0;
static void FreeMutableShadow(void* data) { ++Frees; std::free(data); }
static void* AllocateMutableShadow(std::size_t bytes) { return std::malloc(bytes); }
static void* RejectMutableShadow(std::size_t) { return nullptr; }

int main() {
    Ps1VramAllocator allocator;
    const unsigned baseline = allocator.UsedWords();
    std::uint16_t* shadow = nullptr;
    std::uint16_t stride = 0, x = 0, y = 0;
    assert(!AllocatePs1MutableTexture(allocator, 5, 3, RejectMutableShadow, shadow, stride, x, y));
    assert(shadow == nullptr && allocator.UsedWords() == baseline);
    assert(!AllocatePs1MutableTexture(allocator, 257, 3, AllocateMutableShadow, shadow, stride, x, y));
    assert(AllocatePs1MutableTexture(allocator, 5, 3, AllocateMutableShadow, shadow, stride, x, y));
    assert(stride == 6 && allocator.UsedWords() == baseline + 18);
    const unsigned used = allocator.UsedWords();
    std::uint8_t initial[60]{};
    for (int pixel = 0; pixel < 15; ++pixel) initial[pixel * 4 + 3] = 255;
    MutableGpuSpy gpu;
    gpu.Shadow = shadow;
    gpu.BeforeFence = 0;
    UpdatePs1MutableTexture(gpu, shadow, stride, x, y, 0, 0, 5, 3, initial, 20, false);
    assert(gpu.Calls.size() == 1 && gpu.Calls[0].Region.size.w == 6 && gpu.Calls[0].Region.size.h == 3);
    assert(shadow[0] == 0x8000 && shadow[5] == 0 && shadow[11] == 0);
    std::uint8_t tile[20] = {255,0,0,255, 0,0,255,255, 99,99,99,99, 0,255,0,255, 255,255,255,127};
    assert(IsPs1MutableRegionValid(5, 3, stride, 1, 1, 2, 2, tile, sizeof(tile), 12));
    gpu.BeforeFence = 0x8000;
    UpdatePs1MutableTexture(gpu, shadow, stride, x, y, 1, 1, 2, 2, tile, 12, false);
    assert(gpu.Calls.size() == 3);
    assert(gpu.Calls[1].Region.pos.x == x && gpu.Calls[1].Region.size.w == 4);
    assert(shadow[6] == 0x8000 && shadow[7] == 0x801F && shadow[8] == 0xFC00 && shadow[9] == 0x8000);
    assert(shadow[13] == 0x83E0 && shadow[14] == 0 && shadow[15] == 0x8000);
    assert(allocator.UsedWords() == used && gpu.ChainWaits == 2 && gpu.ReadyWaits == 2);
    const auto before = std::vector<std::uint16_t>(shadow, shadow + 18);
    assert(!IsPs1MutableRegionValid(5, 3, stride, -1, 0, 1, 1, tile, sizeof(tile), 4));
    assert(!IsPs1MutableRegionValid(5, 3, stride, 4, 2, 2, 1, tile, sizeof(tile), 8));
    assert(!IsPs1MutableRegionValid(5, 3, stride, 0, 0, 2, 2, tile, 19, 12));
    assert(!IsPs1MutableRegionValid(5, 3, stride, 0, 0, 2, 1, tile, sizeof(tile), 7));
    assert(!IsPs1MutableRegionValid(5, 3, 5, 0, 0, 2, 1, tile, sizeof(tile), 8));
    assert(!IsPs1MutableRegionValid(5, 3, stride, 0, 0, 1, 3, tile, sizeof(tile), 0x7FFFFFFF));
    assert(before == std::vector<std::uint16_t>(shadow, shadow + 18) && gpu.Calls.size() == 3);
    std::uint8_t golden[4] = {255, 128, 64, 127};
    assert(PackPs1MutableTexel(golden, false) == 0);
    golden[3] = 128;
    assert(PackPs1MutableTexel(golden, false) == 0xA21F);
    golden[3] = 0;
    assert(PackPs1MutableTexel(golden, true) == 0xA21F);
    std::uint8_t black[4]{};
    assert(PackPs1MutableTexel(black, true) == 0x8000);
    assert(PackPs1MutableTexel(black, false) == 0);
    auto* texture = new Ps1RuntimeTexture();
    assert(!texture->CanUpdatePixels(&allocator));
    texture->MutablePixels = shadow;
    texture->ReleaseMutablePixels = FreeMutableShadow;
    texture->MutableOwner = &allocator;
    texture->PixelDepth = Ps1RuntimeTexture::Depth::SixteenBit;
    assert(texture->CanUpdatePixels(&allocator) && !texture->CanUpdatePixels(&gpu));
    texture->Dispose();
    assert(!texture->CanUpdatePixels(&allocator));
    texture->Dispose();
    delete texture;
    assert(Frees == 1);
    assert(allocator.Release(x, y, stride, 3) && allocator.UsedWords() == baseline);
    assert(AllocatePs1MutableTexture(allocator, 128, 96, AllocateMutableShadow, shadow, stride, x, y));
    assert(stride == 128 && allocator.UsedWords() == baseline + 128 * 96);
    std::vector<std::uint8_t> rgba(128 * 96 * 4, 255);
    MutableGpuSpy tracerGpu;
    UpdatePs1MutableTexture(tracerGpu, shadow, stride, x, y, 0, 0, 128, 96, rgba.data(), 512, false);
    assert(tracerGpu.Calls.size() == 1 && tracerGpu.Calls[0].Pixels.size() == 128 * 96);
    assert(tracerGpu.Calls[0].Pixels[128 * 96 - 1] == 0xFFFF);
    std::free(shadow);
    assert(allocator.Release(x, y, stride, 96) && allocator.UsedWords() == baseline);
    std::puts("PS1_MUTABLE_TEXTURE_PASS packing=4 odd_stride=1 partial_neighbors=1 dma_fence=1 bounds=6 rollback=1 ownership=1 immutable=1 tracer128x96=1");
}
