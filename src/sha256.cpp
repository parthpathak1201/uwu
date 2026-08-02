#include "sha256.hpp"

#include <cstdint>
#include <cstring>

namespace uwu {

namespace {

// ---------------------------------------------------------------------------
// SHA-256 constants (FIPS 180-4, Section 5.3.3 and 4.2.2)
//
// H is the initial hash value: the first 32 bits of the fractional parts of
// the square roots of the first 8 primes (2, 3, 5, 7, 11, 13, 17, 19).
//
// K is the round constant array: the first 32 bits of the fractional parts of
// the cube roots of the first 64 primes. Both are constants of the algorithm
// and must never change.
// ---------------------------------------------------------------------------

constexpr uint32_t H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

// ---------------------------------------------------------------------------
// Primitive helpers
// ---------------------------------------------------------------------------

// Rotate-right by n bits. The FIPS spec writes ROTR(x, n); the compile-time
// n means the shl/shift pair never has undefined shift-count problems.
constexpr uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

// Four "choice" / "majority" / "sigma" functions from FIPS 180-4 4.1.2.
//   Ch(e,f,g)  = (e AND f) XOR (NOT e AND g)
//   Maj(a,b,c) = (a AND b) XOR (a AND c) XOR (b AND c)
//   Sum0(a)    = ROTR(a,2)  XOR ROTR(a,13) XOR ROTR(a,22)
//   Sum1(e)    = ROTR(e,6)  XOR ROTR(e,11) XOR ROTR(e,25)
constexpr uint32_t ch(uint32_t e, uint32_t f, uint32_t g) {
    return (e & f) ^ (~e & g);
}
constexpr uint32_t maj(uint32_t a, uint32_t b, uint32_t c) {
    return (a & b) ^ (a & c) ^ (b & c);
}
constexpr uint32_t sum0(uint32_t a) {
    return rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
}
constexpr uint32_t sum1(uint32_t e) {
    return rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
}

// Message-schedule small sigmas (FIPS 180-4 4.1.2).
//   sigma0(x) = ROTR(x,7)  XOR ROTR(x,18) XOR (x >> 3)
//   sigma1(x) = ROTR(x,17) XOR ROTR(x,19) XOR (x >> 10)
constexpr uint32_t sigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}
constexpr uint32_t sigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

// Reads 4 bytes as a big-endian 32-bit word (FIPS 180-4 3.1.1).
uint32_t load_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// ---------------------------------------------------------------------------
// Streaming hasher
// ---------------------------------------------------------------------------

class Hasher {
public:
    Hasher() {
        std::memcpy(state_, H0, sizeof(H0));
    }

    // Feeds more bytes into the hash. Full 64-byte blocks are compressed
    // immediately; a partial tail is kept in buf_ until finalize().
    void update(const void* data, size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        size_t i = 0;

        if (buf_len_ > 0) {
            // Finish filling the pending block first so compression always
            // operates on a complete 64-byte block.
            const size_t need = 64 - buf_len_;
            const size_t take = need < size ? need : size;
            std::memcpy(buf_ + buf_len_, p, take);
            buf_len_ += take;
            i += take;
            if (buf_len_ == 64) {
                compress(buf_);
                total_bits_ += 512;
                buf_len_ = 0;
            }
        }

        // Compress whole blocks straight from the caller's buffer.
        for (; i + 64 <= size; i += 64) {
            compress(p + i);
            total_bits_ += 512;
        }

        // Stash any remaining bytes (less than a block).
        if (i < size) {
            std::memcpy(buf_, p + i, size - i);
            buf_len_ = size - i;
        }
    }

    // Applies the FIPS 180-4 5.1.1 padding rule and emits the digest:
    //   * append a single 0x80 bit/byte
    //   * pad with zeros until the length is 56 mod 64
    //   * append the original message length in bits as a 64-bit
    //     big-endian integer
    //   * compress the final block(s)
    str finalize() {
        const uint64_t total_bits = total_bits_ + uint64_t(buf_len_) * 8;

        buf_[buf_len_++] = 0x80;                         // the mandatory 1-bit
        if (buf_len_ > 56) {                             // no room for length
            std::memset(buf_ + buf_len_, 0, 64 - buf_len_);
            compress(buf_);
            buf_len_ = 0;
        }
        std::memset(buf_ + buf_len_, 0, 56 - buf_len_);  // zero pad

        // Append length: big-endian 64 bits.
        for (int i = 0; i < 8; i++)
            buf_[56 + i] = uint8_t(total_bits >> (56 - 8 * i));
        compress(buf_);

        // Serialize the 8 state words big-endian into lowercase hex.
        str out(64, '0');
        static constexpr char hex[] = "0123456789abcdef";
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                const int shift = 28 - 4 * j;
                out[8 * i + j] = hex[(state_[i] >> shift) & 0xf];
            }
        }
        return out;
    }

private:
    // The core 64-round compression function (FIPS 180-4 6.2.2).
    // Operates on one 512-bit block.
    void compress(const uint8_t* block) {
        // 6.2.2 step 1: expand the 16 big-endian words into a 64-word
        // message schedule. W[t] for t >= 16 mixes four earlier words.
        uint32_t w[64];
        for (int t = 0; t < 16; t++) w[t] = load_be32(block + 4 * t);
        for (int t = 16; t < 64; t++) {
            w[t] = w[t - 16] + sigma0(w[t - 15]) + w[t - 7] + sigma1(w[t - 2]);
        }

        // 6.2.2 step 2: initialize the 8 working variables from state.
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

        // 6.2.2 step 3: 64 rounds.
        for (int t = 0; t < 64; t++) {
            const uint32_t t1 = h + sum1(e) + ch(e, f, g) + K[t] + w[t];
            const uint32_t t2 = sum0(a) + maj(a, b, c);
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        // 6.2.2 step 4: fold the working variables back into state.
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    uint32_t state_[8];     // current 256-bit hash state (big-endian words)
    uint64_t total_bits_ = 0; // number of message bits fully compressed
    uint8_t buf_[64];       // pending partial block
    size_t buf_len_ = 0;    // bytes currently buffered
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

str sha256_hex(const void* data, size_t size) {
    Hasher h;
    h.update(data, size);
    return h.finalize();
}

str sha256_hex(const str& data) {
    return sha256_hex(data.data(), data.size());
}

} // namespace uwu
