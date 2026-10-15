#pragma once

#include <cstddef>
#include <cstdint>

namespace helengine::ps1 {

/// <summary>Incremental SHA-256 for bounded CD reads; keeps only one 64-byte block in RAM.</summary>
class Ps1Sha256 {
public:
    void Update(const std::uint8_t* bytes, std::size_t count) {
        Length += count;
        while (count != 0) {
            const std::size_t available = 64 - Used;
            const std::size_t copied = count < available ? count : available;
            for (std::size_t i = 0; i < copied; ++i) Block[Used + i] = bytes[i];
            Used += copied;
            bytes += copied;
            count -= copied;
            if (Used == 64) { Compress(); Used = 0; }
        }
    }

    /// <summary>Writes the digest, consuming this hash state; callers must start a fresh instance to reuse it.</summary>
    void Finish(std::uint8_t output[32]) {
        const std::uint64_t bits = Length * 8;
        Block[Used++] = 0x80;
        if (Used > 56) {
            while (Used < 64) Block[Used++] = 0;
            Compress();
            Used = 0;
        }
        while (Used < 56) Block[Used++] = 0;
        for (unsigned i = 0; i < 8; ++i) Block[56 + i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        Compress();
        for (unsigned i = 0; i < 8; ++i) {
            for (unsigned b = 0; b < 4; ++b) output[i * 4 + b] = static_cast<std::uint8_t>(State[i] >> (24 - b * 8));
        }
    }

    static bool Equal(const std::uint8_t left[32], const std::uint8_t right[32]) {
        std::uint8_t difference = 0;
        for (unsigned i = 0; i < 32; ++i) difference |= left[i] ^ right[i];
        return difference == 0;
    }

private:
    static std::uint32_t Rotate(std::uint32_t value, unsigned count) {
        return (value >> count) | (value << (32 - count));
    }

    void Compress() {
        static constexpr std::uint32_t constants[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t words[64];
        for (unsigned i = 0; i < 16; ++i) {
            words[i] = (static_cast<std::uint32_t>(Block[i*4]) << 24) |
                (static_cast<std::uint32_t>(Block[i*4+1]) << 16) |
                (static_cast<std::uint32_t>(Block[i*4+2]) << 8) | Block[i*4+3];
        }
        for (unsigned i = 16; i < 64; ++i) {
            const std::uint32_t s0 = Rotate(words[i-15],7) ^ Rotate(words[i-15],18) ^ (words[i-15] >> 3);
            const std::uint32_t s1 = Rotate(words[i-2],17) ^ Rotate(words[i-2],19) ^ (words[i-2] >> 10);
            words[i] = words[i-16] + s0 + words[i-7] + s1;
        }
        std::uint32_t a=State[0],b=State[1],c=State[2],d=State[3],e=State[4],f=State[5],g=State[6],h=State[7];
        for (unsigned i = 0; i < 64; ++i) {
            const std::uint32_t t1 = h + (Rotate(e,6)^Rotate(e,11)^Rotate(e,25)) + ((e&f)^(~e&g)) + constants[i] + words[i];
            const std::uint32_t t2 = (Rotate(a,2)^Rotate(a,13)^Rotate(a,22)) + ((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        State[0]+=a;State[1]+=b;State[2]+=c;State[3]+=d;State[4]+=e;State[5]+=f;State[6]+=g;State[7]+=h;
    }

    std::uint32_t State[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::uint8_t Block[64]{};
    std::size_t Used = 0;
    std::uint64_t Length = 0;
};

}  // namespace helengine::ps1
