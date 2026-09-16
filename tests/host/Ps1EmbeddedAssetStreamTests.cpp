#include "platform/ps1/Ps1EmbeddedAssetStream.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#ifdef HE_CPP_TEST_HOST
namespace he_cpp_custom {
[[noreturn]] void Fail(const char* message) {
    std::fputs(message, stderr);
    std::exit(73);
}
}  // namespace he_cpp_custom
#endif

int main(int argc, char**) {
    const std::uint8_t image[] = {0xAA, 0xBB, 10, 11, 12, 13, 14, 15, 16, 17, 0xCC};
    // The stream covers only the middle eight bytes; neighbours must stay invisible.
    helengine::ps1::Ps1EmbeddedAssetStream stream(image + 2, 8);
    if (argc > 1) {
        // Failure path: seeking past the asset must fail through the runtime policy.
        stream.Seek(9, SeekOrigin::Begin);
        return 20;
    }
    if (!stream.CanRead() || stream.CanWrite() || !stream.CanSeek() || stream.Length() != 8 || stream.Position() != 0) return 1;

    std::uint8_t buffer[16] = {0};
    if (stream.Read(buffer, 0, 3) != 3 || buffer[0] != 10 || buffer[2] != 12 || stream.Position() != 3) return 2;
    // Reads are bounded by the remaining asset bytes and never spill into neighbouring assets.
    if (stream.Read(buffer, 4, 16) != 5 || buffer[4] != 13 || buffer[8] != 17 || buffer[9] != 0) return 3;
    if (stream.Read(buffer, 0, 4) != 0 || stream.ReadByte() != -1) return 4;
    // Zero-length reads are no-ops.
    if (stream.Read(buffer, 0, 0) != 0) return 5;
    if (stream.Seek(-2, SeekOrigin::End) != 6 || stream.ReadByte() != 16 || stream.ReadByte() != 17 || stream.ReadByte() != -1) return 6;
    if (stream.Seek(1, SeekOrigin::Begin) != 1 || stream.Seek(2, SeekOrigin::Current) != 3 || stream.ReadByte() != 13) return 7;
    stream.SetPosition(8);
    if (stream.Position() != 8 || stream.ReadByte() != -1) return 8;

    // Cursors are independent between streams over the same bytes.
    helengine::ps1::Ps1EmbeddedAssetStream second(image + 2, 8);
    if (second.Position() != 0 || second.ReadByte() != 10) return 9;
    // A zero-length asset is valid and immediately at end.
    helengine::ps1::Ps1EmbeddedAssetStream empty(image, 0);
    if (empty.Length() != 0 || empty.ReadByte() != -1 || empty.Read(buffer, 0, 4) != 0) return 10;
    return 0;
}
