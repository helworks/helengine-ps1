#pragma once
#include <cstdint>
class RuntimeTexture {
public:
    virtual ~RuntimeTexture() = default;
    virtual void Dispose() { Disposed = true; }
    bool get_IsDisposed() const { return Disposed; }
    void set_IsDisposed(bool value) { Disposed = value; }
    std::uint16_t get_Width() const { return 16; }
    std::uint16_t get_Height() const { return 16; }
    bool Disposed = false;
};
