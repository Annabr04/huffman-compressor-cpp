// sha256.hpp - small self-contained SHA-256 (FIPS 180-4), C++17, no deps.
// Written for this repo from the FIPS 180-4 specification; public-domain style,
// use freely. Used only for the round-trip integrity check (it is not a MAC).
// Header-only and ODR-safe: everything is in namespace sha256; free functions
// are `inline`, member functions are defined in-class (implicitly inline,
// including the stateless static member rotr), and the round-constant table is
// a static constexpr class member, implicitly inline in C++17. No globals, no
// mutable or non-inline statics.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sha256 {

using Digest = std::array<std::uint8_t, 32>;

class Hasher {
public:
    Hasher() { reset(); }
    void reset() {
        h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        len_ = 0;
        blen_ = 0;
    }
    void update(const void* data, std::size_t n) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        len_ += static_cast<std::uint64_t>(n);
        while (n > 0) {
            const std::size_t take = (64 - blen_ < n) ? 64 - blen_ : n;
            for (std::size_t i = 0; i < take; ++i) buf_[blen_ + i] = p[i];
            blen_ += take;
            p += take;
            n -= take;
            if (blen_ == 64) {
                block(buf_.data());
                blen_ = 0;
            }
        }
    }
    Digest finish() {
        const std::uint64_t bits = len_ * 8;
        const std::uint8_t pad80 = 0x80, zero = 0x00;
        update(&pad80, 1);
        while (blen_ != 56) update(&zero, 1);
        std::uint8_t lenbe[8];
        for (int i = 0; i < 8; ++i) lenbe[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        update(lenbe, 8);
        Digest d{};
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 4; ++j) d[4 * i + j] = static_cast<std::uint8_t>(h_[i] >> (24 - 8 * j));
        reset();
        return d;
    }

private:
    static constexpr std::uint32_t K[64] = {  // FIPS 180-4 4.2.2; implicitly inline (C++17)
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    static std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
    void block(const std::uint8_t* b) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t{b[4 * i]} << 24) | (std::uint32_t{b[4 * i + 1]} << 16) |
                   (std::uint32_t{b[4 * i + 2]} << 8) | std::uint32_t{b[4 * i + 3]};
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], bb = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + S1 + ch + K[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t mj = (a & bb) ^ (a & c) ^ (bb & c);
            const std::uint32_t t2 = S0 + mj;
            h = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += bb; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }
    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> buf_{};
    std::uint64_t len_ = 0;
    std::size_t blen_ = 0;
};

inline Digest hash(const std::string& s) {
    Hasher x;
    x.update(s.data(), s.size());
    return x.finish();
}

inline std::string hex(const Digest& d) {
    constexpr char k[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint8_t v : d) {
        out += k[v >> 4];
        out += k[v & 15];
    }
    return out;
}

}  // namespace sha256
