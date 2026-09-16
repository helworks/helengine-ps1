#pragma once

#include <cstddef>
#include <cstdint>

#include "system/io/stream.hpp"

namespace helengine::ps1 {

/// <summary>
/// Read-only, seekable Core stream over one asset range inside the embedded archive image.
/// Each stream owns an independent cursor and never exposes bytes outside its range; the
/// image itself is never written. Write, resize and timeout operations report NotSupported
/// through the runtime failure policy.
/// </summary>
class Ps1EmbeddedAssetStream final : public ::Stream {
public:
    /// <summary>Creates a stream over [data, data + length) with the cursor at zero.</summary>
    Ps1EmbeddedAssetStream(const std::uint8_t* data, std::size_t length)
        : Data(data), LengthValue(data == nullptr ? 0 : length) {}

    /// <summary>Copies up to count bytes into buffer at offset and returns the bytes copied, zero at end.</summary>
    size_t Read(uint8_t* buffer, size_t offset, size_t count) override {
        if (buffer == nullptr || count == 0 || PositionValue >= LengthValue) { return 0; }
        const size_t available = LengthValue - PositionValue;
        const size_t copied = count < available ? count : available;
        for (size_t index = 0; index < copied; ++index) {
            buffer[offset + index] = Data[PositionValue + index];
        }
        PositionValue += copied;
        return copied;
    }

    /// <summary>Writing into the embedded image is not supported.</summary>
    void Write(const uint8_t*, size_t, size_t) override { RaiseReadOnly(); }

    /// <summary>Moves the cursor and returns the new position; positions outside the range are rejected.</summary>
    size_t Seek(int64_t offset, SeekOrigin origin) override {
        int64_t base = 0;
        if (origin == SeekOrigin::Current) { base = static_cast<int64_t>(PositionValue); }
        else if (origin == SeekOrigin::End) { base = static_cast<int64_t>(LengthValue); }
        const int64_t target = base + offset;
        if (target < 0 || target > static_cast<int64_t>(LengthValue)) {
#if HE_CPP_COMPACT_NATIVE_EXCEPTION_MESSAGES
            return he_cpp_raise_value<size_t>(ArgumentOutOfRangeException());
#else
            return he_cpp_raise_value<size_t>(ArgumentOutOfRangeException("Embedded asset seek outside the asset range"));
#endif
        }
        PositionValue = static_cast<size_t>(target);
        return PositionValue;
    }

    /// <summary>Resizing the embedded image is not supported.</summary>
    void SetLength(size_t) override { RaiseReadOnly(); }

    bool CanRead() const override { return true; }
    bool CanWrite() const override { return false; }
    bool CanSeek() const override { return true; }
    size_t Length() const override { return LengthValue; }
    size_t Position() const override { return PositionValue; }

    /// <summary>Sets the cursor; positions past the end are rejected.</summary>
    void SetPosition(size_t value) override { Seek(static_cast<int64_t>(value), SeekOrigin::Begin); }

    void InternalReserve(size_t) override { RaiseReadOnly(); }
    void InternalWriteByte(uint8_t) override { RaiseReadOnly(); }

    /// <summary>Returns the next byte or -1 at the end of the asset.</summary>
    int InternalReadByte() override {
        if (PositionValue >= LengthValue) { return -1; }
        return static_cast<int>(Data[PositionValue++]);
    }

private:
    /// <summary>Reports a write attempt against the read-only image.</summary>
    [[noreturn]] static void RaiseReadOnly() {
#if HE_CPP_COMPACT_NATIVE_EXCEPTION_MESSAGES
        he_cpp_raise(NotSupportedException());
#else
        he_cpp_raise(NotSupportedException("Embedded assets are read-only"));
#endif
    }

    /// <summary>First byte of the asset inside the immutable archive image.</summary>
    const std::uint8_t* Data;

    /// <summary>Asset length in bytes.</summary>
    std::size_t LengthValue;

    /// <summary>Independent read cursor.</summary>
    std::size_t PositionValue = 0;
};

}  // namespace helengine::ps1
