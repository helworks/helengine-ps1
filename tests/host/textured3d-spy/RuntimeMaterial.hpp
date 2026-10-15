#pragma once
#include "RuntimeTexture.hpp"
#include <cassert>
class RuntimeMaterial {
public:
    virtual ~RuntimeMaterial() = default;
    virtual void Dispose() { assert(!Disposed); Disposed = true; ++DisposeCount; }
    virtual RuntimeTexture* ResolvePrimaryTexture() { return Primary; }
    virtual void SetPrimaryTexture(RuntimeTexture* texture) { Primary = texture; }
    inline static unsigned DisposeCount = 0;
private:
    bool Disposed = false;
    RuntimeTexture* Primary = nullptr;
};
