// ============================================================================
//  Crypto.cpp — 纯 C++ 的密码学原语（不依赖任何系统库）
// ----------------------------------------------------------------------------
//  移植自 Android 版对 libcrypto.so 的调用点：
//    SHA256() / HMAC(EVP_sha256()) / EVP_aes_256_gcm 解密 / MD5 / RC4
//  算法输出与 OpenSSL 完全一致，已按 FIPS-197 / SP800-38D / RFC 2104 实现。
// ============================================================================

#include "Core/Crypto.h"

#include <cstring>

namespace SMCrypto {
namespace {

// ---------------------------------------------------------------- SHA-256
inline uint32_t Rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

const uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

void Sha256Block(const uint8_t *p, uint32_t h[8]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
               (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = hh + S1 + ch + kSha256K[i] + w[i];
        const uint32_t S0 = Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

// ---------------------------------------------------------------- MD5
const uint32_t kMd5K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au,
    0xa8304613u, 0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u,
    0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u,
    0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u, 0x289b7ec6u, 0xeaa127fau,
    0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u,
    0xffeff47du, 0x85845dd1u, 0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};

inline uint32_t Rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void Md5Block(const uint8_t *p, uint32_t h[4]) {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = uint32_t(p[i * 4]) | (uint32_t(p[i * 4 + 1]) << 8) |
               (uint32_t(p[i * 4 + 2]) << 16) | (uint32_t(p[i * 4 + 3]) << 24);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    static const int r[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        const uint32_t tmp = d;
        d = c;
        c = b;
        b = b + Rotl32(a + f + kMd5K[i] + m[g], r[i]);
        a = tmp;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}

// ---------------------------------------------------------------- AES
const uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

uint8_t XTime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) ? 0x1b : 0x00)); }

uint8_t GFMul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) p ^= a;
        const uint8_t hi = (uint8_t)(a & 0x80);
        a = (uint8_t)(a << 1);
        if (hi) a ^= 0x1b;
        b = (uint8_t)(b >> 1);
    }
    return p;
}

// AES-256（Nk=8, Nr=14），rk 长度 240。
void Aes256Expand(const uint8_t key[32], uint8_t rk[240]) {
    static const uint8_t rcon[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    memcpy(rk, key, 32);
    for (int i = 8; i < 60; ++i) {
        uint8_t t[4];
        memcpy(t, rk + (i - 1) * 4, 4);
        if (i % 8 == 0) {
            const uint8_t tmp = t[0];
            t[0] = (uint8_t)(kSbox[t[1]] ^ rcon[i / 8 - 1]);
            t[1] = kSbox[t[2]];
            t[2] = kSbox[t[3]];
            t[3] = kSbox[tmp];
        } else if (i % 8 == 4) {
            for (int k = 0; k < 4; ++k) t[k] = kSbox[t[k]];
        }
        for (int k = 0; k < 4; ++k) rk[i * 4 + k] = (uint8_t)(rk[(i - 8) * 4 + k] ^ t[k]);
    }
}

void AesEncryptBlock(const uint8_t rk[240], const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    memcpy(s, in, 16);
    for (int i = 0; i < 16; ++i) s[i] ^= rk[i];

    for (int round = 1; round <= 14; ++round) {
        // SubBytes + ShiftRows
        uint8_t t[16];
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                t[c * 4 + r] = kSbox[s[((c + r) % 4) * 4 + r]];
            }
        }
        memcpy(s, t, 16);
        if (round != 14) {
            for (int c = 0; c < 4; ++c) {
                uint8_t *col = s + c * 4;
                const uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = (uint8_t)(GFMul(a0, 2) ^ GFMul(a1, 3) ^ a2 ^ a3);
                col[1] = (uint8_t)(a0 ^ GFMul(a1, 2) ^ GFMul(a2, 3) ^ a3);
                col[2] = (uint8_t)(a0 ^ a1 ^ GFMul(a2, 2) ^ GFMul(a3, 3));
                col[3] = (uint8_t)(GFMul(a0, 3) ^ a1 ^ a2 ^ GFMul(a3, 2));
            }
        }
        for (int i = 0; i < 16; ++i) s[i] ^= rk[round * 16 + i];
    }
    memcpy(out, s, 16);
}

// GF(2^128) 乘法（GCM 的 GHASH 用，位序为 GCM 规范的大端表示）
void Gf128Mul(const uint8_t x[16], const uint8_t y[16], uint8_t z[16]) {
    uint8_t v[16];
    uint8_t acc[16];
    memcpy(v, y, 16);
    memset(acc, 0, 16);
    for (int i = 0; i < 128; ++i) {
        if (x[i >> 3] & (0x80 >> (i & 7))) {
            for (int j = 0; j < 16; ++j) acc[j] ^= v[j];
        }
        const bool lsb = (v[15] & 1) != 0;
        for (int j = 15; j > 0; --j) v[j] = (uint8_t)((v[j] >> 1) | ((v[j - 1] & 1) << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1;
    }
    memcpy(z, acc, 16);
}

void GHashUpdate(uint8_t Y[16], const uint8_t H[16], const uint8_t *data, size_t len) {
    uint8_t block[16];
    for (size_t off = 0; off < len; off += 16) {
        const size_t n = (len - off >= 16) ? 16 : (len - off);
        memcpy(block, data + off, n);
        if (n < 16) memset(block + n, 0, 16 - n);
        for (int i = 0; i < 16; ++i) Y[i] ^= block[i];
        Gf128Mul(Y, H, Y);
    }
}

void PutBe64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (56 - i * 8));
}

void Gctr(const uint8_t rk[240], const uint8_t icb[16], const uint8_t *in, size_t len,
          uint8_t *out) {
    uint8_t cb[16];
    memcpy(cb, icb, 16);
    uint8_t ks[16];
    for (size_t off = 0; off < len; off += 16) {
        AesEncryptBlock(rk, cb, ks);
        const size_t n = (len - off >= 16) ? 16 : (len - off);
        for (size_t i = 0; i < n; ++i) out[off + i] = (uint8_t)(in[off + i] ^ ks[i]);
        // inc32
        for (int i = 15; i >= 12; --i) {
            if (++cb[i] != 0) break;
        }
    }
}

void GcmTag(const uint8_t rk[240], const uint8_t H[16], const uint8_t J0[16],
            const uint8_t *cipher, size_t clen, const uint8_t *aad, size_t alen,
            uint8_t tag[16]) {
    uint8_t Y[16] = {0};
    if (alen) GHashUpdate(Y, H, aad, alen);
    if (clen) GHashUpdate(Y, H, cipher, clen);
    uint8_t lenBlock[16];
    PutBe64(lenBlock, (uint64_t)alen * 8);
    PutBe64(lenBlock + 8, (uint64_t)clen * 8);
    for (int i = 0; i < 16; ++i) Y[i] ^= lenBlock[i];
    Gf128Mul(Y, H, Y);

    uint8_t ek[16];
    AesEncryptBlock(rk, J0, ek);
    for (int i = 0; i < 16; ++i) tag[i] = (uint8_t)(ek[i] ^ Y[i]);
}

}  // namespace

// ================================================================ 摘要
bool Sha256Raw(const std::string &data, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    const uint8_t *p = (const uint8_t *)data.data();
    const size_t n = data.size();
    const size_t full = n / 64;
    for (size_t i = 0; i < full; ++i) Sha256Block(p + i * 64, h);

    uint8_t tail[128];
    const size_t rem = n - full * 64;
    if (rem) memcpy(tail, p + full * 64, rem);
    tail[rem] = 0x80;
    const size_t padLen = (rem < 56) ? 64 : 128;
    memset(tail + rem + 1, 0, padLen - rem - 9);
    const uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; ++i) tail[padLen - 1 - i] = (uint8_t)(bits >> (8 * i));
    Sha256Block(tail, h);
    if (padLen == 128) Sha256Block(tail + 64, h);

    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)h[i];
    }
    return true;
}

std::string ToHex(const uint8_t *data, size_t len) {
    static const char *hexd = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hexd[data[i] >> 4]);
        out.push_back(hexd[data[i] & 0x0F]);
    }
    return out;
}

std::string ToHex(const std::vector<uint8_t> &data) { return ToHex(data.data(), data.size()); }

std::string Sha256Hex(const std::string &data) {
    uint8_t h[32];
    Sha256Raw(data, h);
    return ToHex(h, 32);
}

bool HmacSha256Raw(const std::string &key, const uint8_t *msg, size_t msgLen, uint8_t out[32]) {
    uint8_t k[64];
    memset(k, 0, sizeof(k));
    if (key.size() > 64) {
        uint8_t hashed[32];
        Sha256Raw(key, hashed);
        memcpy(k, hashed, 32);
    } else {
        memcpy(k, key.data(), key.size());
    }

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = (uint8_t)(k[i] ^ 0x36);
        opad[i] = (uint8_t)(k[i] ^ 0x5c);
    }

    std::string inner;
    inner.reserve(64 + msgLen + 9);
    inner.append((const char *)ipad, 64);
    if (msg && msgLen) inner.append((const char *)msg, msgLen);
    uint8_t innerHash[32];
    Sha256Raw(inner, innerHash);

    std::string outer;
    outer.reserve(64 + 32 + 9);
    outer.append((const char *)opad, 64);
    outer.append((const char *)innerHash, 32);
    Sha256Raw(outer, out);
    return true;
}

std::string HmacSha256Hex(const std::string &key, const std::string &data) {
    uint8_t mac[32];
    HmacSha256Raw(key, (const uint8_t *)data.data(), data.size(), mac);
    return ToHex(mac, 32);
}

std::string Md5Hex(const std::string &data) {
    uint32_t h[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    const uint8_t *p = (const uint8_t *)data.data();
    const size_t n = data.size();
    const size_t full = n / 64;
    for (size_t i = 0; i < full; ++i) Md5Block(p + i * 64, h);

    uint8_t tail[128];
    const size_t rem = n - full * 64;
    if (rem) memcpy(tail, p + full * 64, rem);
    tail[rem] = 0x80;
    const size_t padLen = (rem < 56) ? 64 : 128;
    memset(tail + rem + 1, 0, padLen - rem - 9);
    const uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; ++i) tail[padLen - 8 + i] = (uint8_t)(bits >> (8 * i));
    Md5Block(tail, h);
    if (padLen == 128) Md5Block(tail + 64, h);

    uint8_t digest[16];
    for (int i = 0; i < 4; ++i) {
        digest[i * 4] = (uint8_t)h[i];
        digest[i * 4 + 1] = (uint8_t)(h[i] >> 8);
        digest[i * 4 + 2] = (uint8_t)(h[i] >> 16);
        digest[i * 4 + 3] = (uint8_t)(h[i] >> 24);
    }
    return ToHex(digest, 16);
}

// ================================================================ RC4
std::string Rc4(const std::string &data, const std::string &key) {
    if (key.empty()) return std::string();
    uint8_t s[256];
    for (int i = 0; i < 256; ++i) s[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (uint8_t)(j + s[i] + (uint8_t)key[(size_t)i % key.size()]);
        const uint8_t t = s[i];
        s[i] = s[j];
        s[j] = t;
    }
    std::string out(data.size(), '\0');
    uint8_t i = 0;
    j = 0;
    for (size_t k = 0; k < data.size(); ++k) {
        i = (uint8_t)(i + 1);
        j = (uint8_t)(j + s[i]);
        const uint8_t t = s[i];
        s[i] = s[j];
        s[j] = t;
        const uint8_t ks = s[(uint8_t)(s[i] + s[j])];
        out[k] = (char)((uint8_t)data[k] ^ ks);
    }
    return out;
}

// ================================================================ hex/base64
std::vector<uint8_t> FromHex(const std::string &hex) {
    std::vector<uint8_t> out;
    if (hex.size() % 2 != 0) return out;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = val(hex[i]);
        const int lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::vector<uint8_t>();
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return out;
}

std::string Base64Decode(const std::string &text) {
    static const int8_t table[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    if (text.empty() || (text.size() % 4) != 0) return std::string();
    std::string out;
    out.reserve((text.size() / 4) * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        const int v0 = table[(uint8_t)text[i]];
        const int v1 = table[(uint8_t)text[i + 1]];
        const int v2 = table[(uint8_t)text[i + 2]];
        const int v3 = table[(uint8_t)text[i + 3]];
        if (v0 < 0 || v1 < 0 || v2 == -1 || v3 == -1) return std::string();
        if (v2 == -2 && v3 != -2) return std::string();
        const uint32_t n = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) |
                           ((uint32_t)(v2 < 0 ? 0 : v2) << 6) | (uint32_t)(v3 < 0 ? 0 : v3);
        out.push_back((char)(uint8_t)(n >> 16));
        if (v2 != -2) out.push_back((char)(uint8_t)(n >> 8));
        if (v3 != -2) out.push_back((char)(uint8_t)n);
        if ((v2 == -2 || v3 == -2) && i + 4 != text.size()) return std::string();
    }
    return out;
}

// ================================================================ AES-256-GCM
bool Aes256GcmDecrypt(const uint8_t key[32], const uint8_t *iv, size_t ivLen,
                      const uint8_t *cipher, size_t cipherLen,
                      const uint8_t *tag, size_t tagLen, std::vector<uint8_t> &out) {
    out.clear();
    if (!iv || ivLen == 0 || !cipher || !tag || tagLen != 16) return false;

    uint8_t rk[240];
    Aes256Expand(key, rk);
    uint8_t H[16] = {0};
    AesEncryptBlock(rk, H, H);

    // J0：96 位 IV 时 = IV || 0^31 || 1；否则 GHASH(IV)。
    uint8_t J0[16];
    if (ivLen == 12) {
        memcpy(J0, iv, 12);
        J0[12] = 0;
        J0[13] = 0;
        J0[14] = 0;
        J0[15] = 1;
    } else {
        memset(J0, 0, 16);
        GHashUpdate(J0, H, iv, ivLen);
        uint8_t lenBlock[16] = {0};
        PutBe64(lenBlock + 8, (uint64_t)ivLen * 8);
        for (int i = 0; i < 16; ++i) J0[i] ^= lenBlock[i];
        Gf128Mul(J0, H, J0);
    }

    uint8_t icb[16];
    memcpy(icb, J0, 16);
    for (int i = 15; i >= 12; --i) {
        if (++icb[i] != 0) break;
    }

    std::vector<uint8_t> plain(cipherLen);
    if (cipherLen) Gctr(rk, icb, cipher, cipherLen, plain.data());

    uint8_t expect[16];
    GcmTag(rk, H, J0, cipher, cipherLen, nullptr, 0, expect);
    uint8_t diff = 0;
    for (int i = 0; i < 16; ++i) diff |= (uint8_t)(expect[i] ^ tag[i]);
    if (diff != 0) return false;

    out.swap(plain);
    return true;
}

bool Aes256GcmEncrypt(const uint8_t key[32], const uint8_t *iv, size_t ivLen,
                      const uint8_t *plain, size_t plainLen,
                      std::vector<uint8_t> &cipher, std::vector<uint8_t> &tag) {
    cipher.clear();
    tag.clear();
    if (!iv || ivLen == 0) return false;

    uint8_t rk[240];
    Aes256Expand(key, rk);
    uint8_t H[16] = {0};
    AesEncryptBlock(rk, H, H);

    uint8_t J0[16];
    if (ivLen == 12) {
        memcpy(J0, iv, 12);
        J0[12] = 0;
        J0[13] = 0;
        J0[14] = 0;
        J0[15] = 1;
    } else {
        memset(J0, 0, 16);
        GHashUpdate(J0, H, iv, ivLen);
        uint8_t lenBlock[16] = {0};
        PutBe64(lenBlock + 8, (uint64_t)ivLen * 8);
        for (int i = 0; i < 16; ++i) J0[i] ^= lenBlock[i];
        Gf128Mul(J0, H, J0);
    }

    uint8_t icb[16];
    memcpy(icb, J0, 16);
    for (int i = 15; i >= 12; --i) {
        if (++icb[i] != 0) break;
    }

    cipher.resize(plainLen);
    if (plainLen) Gctr(rk, icb, plain, plainLen, cipher.data());

    uint8_t t[16];
    GcmTag(rk, H, J0, cipher.data(), cipher.size(), nullptr, 0, t);
    tag.assign(t, t + 16);
    return true;
}

}  // namespace SMCrypto
