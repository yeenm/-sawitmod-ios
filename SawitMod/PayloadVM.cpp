// ============================================================================
//  PayloadVM.cpp — iOS 移植
// ----------------------------------------------------------------------------
//  这个文件本来就没有平台依赖：没有 <android/log.h>、没有 JNI、没有 Java 网络。
//  唯一的外部密码学符号 Sha256HexNativePublic() 在 iOS 由 NetAuth.cpp 提供，
//  SVM2 流格式 / opcode / 绑定串 / mask 推导 / Base64 处理全部逐字节保持不变。
// ============================================================================

#include "PayloadVM.h"
#include "NetAuth.h"
#include "VirboxTargets.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr uint8_t kVersion = 2;
constexpr size_t kMaxStream = 256u * 1024u;
constexpr uint16_t kMaxInstructions = 512;
constexpr uint32_t kMaxOperand = 64u * 1024u;

enum Instruction : uint8_t {
    I_ASSEMBLY = 0x21,
    I_NAMESPACE = 0x37,
    I_CLASS = 0x4B,
    I_METHOD = 0x5D,
    I_DEFAULT_PARAMS = 0x6F,
    I_STATIC = 0x73,
    I_METADATA = 0x89,
    I_END = 0xA1,
};

uint16_t ReadU16(const std::vector<uint8_t>& b, size_t p) {
    return static_cast<uint16_t>(b[p]) |
           static_cast<uint16_t>(static_cast<uint16_t>(b[p + 1]) << 8);
}

uint32_t ReadU32(const std::string& b, size_t p) {
    return static_cast<uint32_t>(static_cast<uint8_t>(b[p])) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b[p + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b[p + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b[p + 3])) << 24);
}

void PutU16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void PutU32(std::string& out, uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

bool ConstantTimeEqual(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

// ---------------------------------------------------------------------
//  内建 SHA-256 兜底
// ---------------------------------------------------------------------
// Sha256HexNativePublic() 走的是 xdl_open("libcrypto.so") + SHA256()。游戏进程里
// 一旦取不到 libcrypto.so（没加载 / 符号被裁 / 被反调试拦住），它返回空串，
// 绑定串校验就会失败，表现是"所有功能都报 VM2_INVALID"——离线回放也会一起死。
// 这里放一份自带的实现，只在外部实现不可用时接管，结果完全一致。
// iOS：外部实现换成 NetAuth.cpp 的 Sha256HexNativePublic()（内部走 SMCrypto），
// 取不到时同样返回空串，所以这份兜底照旧接管，落点与 Android 逐字节一致。
uint32_t Rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void Sha256Block(const uint8_t* p, uint32_t h[8]) {
    static const uint32_t K[64] = {
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
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
               (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        const uint32_t S0 = Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

bool Sha256Builtin(const std::string& input, uint8_t out[32]) {
    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    const uint8_t* p = reinterpret_cast<const uint8_t*>(input.data());
    const size_t n = input.size();
    const size_t full = n / 64u;
    for (size_t i = 0; i < full; ++i) Sha256Block(p + i * 64u, h);

    uint8_t tail[128];
    const size_t rem = n - full * 64u;
    if (rem) std::memcpy(tail, p + full * 64u, rem);
    tail[rem] = 0x80;
    const size_t padLen = (rem < 56u) ? 64u : 128u;
    std::memset(tail + rem + 1u, 0, padLen - rem - 9u);
    const uint64_t bits = static_cast<uint64_t>(n) * 8u;
    for (int i = 0; i < 8; ++i) {
        tail[padLen - 1u - static_cast<size_t>(i)] =
            static_cast<uint8_t>(bits >> (8 * i));
    }
    Sha256Block(tail, h);
    if (padLen == 128u) Sha256Block(tail + 64u, h);

    for (int i = 0; i < 8; ++i) {
        out[i * 4]     = static_cast<uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
    return true;
}

bool HexToFixed32(const std::string& hex, uint8_t out[32]) {
    if (hex.size() != 64) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < 32; ++i) {
        int hi = nibble(hex[i * 2]);
        int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool Sha256(const std::string& input, uint8_t out[32]) {
    // 先用外部实现（libcrypto 的 SHA256），不可用时回落到内建实现。
    // 两者输出必须一致，否则绑定串校验会随环境变化而漂移。
    if (HexToFixed32(Sha256HexNativePublic(input), out)) return true;
    return Sha256Builtin(input, out);
}

bool Base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    static const int8_t table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
        -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    out.clear();
    if (text.empty() || (text.size() % 4) != 0) return false;
    out.reserve((text.size() / 4) * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        int v0 = table[static_cast<uint8_t>(text[i])];
        int v1 = table[static_cast<uint8_t>(text[i + 1])];
        int v2 = table[static_cast<uint8_t>(text[i + 2])];
        int v3 = table[static_cast<uint8_t>(text[i + 3])];
        if (v0 < 0 || v1 < 0 || v2 == -1 || v3 == -1) return false;
        if (v2 == -2 && v3 != -2) return false;
        uint32_t n = (static_cast<uint32_t>(v0) << 18) |
                     (static_cast<uint32_t>(v1) << 12) |
                     (static_cast<uint32_t>(v2 < 0 ? 0 : v2) << 6) |
                     static_cast<uint32_t>(v3 < 0 ? 0 : v3);
        out.push_back(static_cast<uint8_t>(n >> 16));
        if (v2 != -2) out.push_back(static_cast<uint8_t>(n >> 8));
        if (v3 != -2) out.push_back(static_cast<uint8_t>(n));
        if ((v2 == -2 || v3 == -2) && i + 4 != text.size()) return false;
    }
    return true;
}

std::string PackMetadata(const std::unordered_map<std::string, std::string>& values) {
    if (values.size() > 512) return {};
    std::string out("SVMN", 4);
    PutU16(out, static_cast<uint16_t>(values.size()));
    for (const auto& kv : values) {
        if (kv.first.empty() || kv.first.size() > 255 || kv.second.size() > kMaxOperand)
            return {};
        PutU16(out, static_cast<uint16_t>(kv.first.size()));
        PutU32(out, static_cast<uint32_t>(kv.second.size()));
        out.append(kv.first);
        out.append(kv.second);
    }
    return out;
}
}

namespace PayloadVM {
void WipeString(std::string& value) {
    if (!value.empty()) {
        volatile char* p = &value[0];
        for (size_t i = 0; i < value.size(); ++i) p[i] = 0;
    }
    value.clear();
}

std::string MetadataValue(const std::string& packed, const std::string& key) {
    if (packed.size() < 6 || packed.compare(0, 4, "SVMN") != 0 || key.empty()) return {};
    size_t p = 4;
    const uint16_t count = static_cast<uint16_t>(static_cast<uint8_t>(packed[p])) |
                           static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint8_t>(packed[p + 1])) << 8);
    p += 2;
    if (count > 512) return {};
    for (uint16_t i = 0; i < count; ++i) {
        if (p + 6 > packed.size()) return {};
        const uint16_t kl = static_cast<uint16_t>(static_cast<uint8_t>(packed[p])) |
                            static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint8_t>(packed[p + 1])) << 8);
        p += 2;
        const uint32_t vl = ReadU32(packed, p);
        p += 4;
        if (kl == 0 || kl > 255 || vl > kMaxOperand || p + kl + vl > packed.size()) return {};
        const bool match = kl == key.size() && std::memcmp(packed.data() + p, key.data(), kl) == 0;
        p += kl;
        if (match) return packed.substr(p, vl);
        p += vl;
    }
    return {};
}

bool InterpretRaw(const std::string& envelope, const Context& ctx, InvokeRequest& req) {
    if (envelope.rfind("SVM2.", 0) != 0 || envelope.size() > kMaxStream * 2u) return false;
    std::vector<uint8_t> stream;
    if (!Base64Decode(envelope.substr(5), stream) || stream.size() < 42 || stream.size() > kMaxStream)
        return false;
    if (std::memcmp(stream.data(), "SVM2", 4) != 0 || stream[4] != kVersion || stream[5] != 0)
        return false;

    const uint16_t streamOp = ReadU16(stream, 6);
    const uint16_t count = ReadU16(stream, 8);
    if (count == 0 || count > kMaxInstructions || streamOp != static_cast<uint16_t>(ctx.expectedOp))
        return false;

    const std::string bindingInput = ctx.nonce + "|" + ctx.hwid + "|" + ctx.wireId + "|" +
                                     ctx.version + "|" + ctx.channel + "|" + std::to_string(streamOp);
    uint8_t binding[32] = {}, mask[32] = {};
    if (!Sha256(bindingInput, binding) ||
        !Sha256(std::string("Sawit-PayloadVM-2|") + bindingInput, mask) ||
        !ConstantTimeEqual(binding, stream.data() + 10, 32))
        return false;

    std::unordered_map<std::string, std::string> metadata;
    bool ended = false;
    size_t p = 42;
    for (uint16_t index = 0; index < count; ++index) {
        if (p + 3 > stream.size() || ended) return false;
        const uint8_t opcode = stream[p++];
        const uint16_t len = ReadU16(stream, p);
        p += 2;
        if (len > kMaxOperand || p + len > stream.size()) return false;
        std::string operand(len, '\0');
        for (uint16_t j = 0; j < len; ++j)
            operand[j] = static_cast<char>(stream[p + j] ^ mask[(j + index * 7u + opcode) & 31u]);
        p += len;

        switch (opcode) {
            case I_ASSEMBLY: req.dllName.swap(operand); break;
            case I_NAMESPACE: req.nameSpace.swap(operand); break;
            case I_CLASS: req.className.swap(operand); break;
            case I_METHOD: req.methodDesc.swap(operand); break;
            case I_DEFAULT_PARAMS: req.paramInput.swap(operand); break;
            case I_STATIC:
                if (operand.size() != 1 || (operand[0] != 0 && operand[0] != 1)) return false;
                req.isStatic = operand[0] == 1;
                WipeString(operand);
                break;
            case I_METADATA: {
                if (operand.size() < 2) return false;
                const uint16_t kl = static_cast<uint16_t>(static_cast<uint8_t>(operand[0])) |
                                    static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint8_t>(operand[1])) << 8);
                if (kl == 0 || kl > 255 || 2u + kl > operand.size()) return false;
                std::string key = operand.substr(2, kl);
                std::string value = operand.substr(2 + kl);
                if (metadata.size() >= 512 || metadata.find(key) != metadata.end()) return false;
                metadata.emplace(std::move(key), std::move(value));
                WipeString(operand);
                break;
            }
            case I_END:
                if (!operand.empty() || index + 1 != count) return false;
                ended = true;
                break;
            default:
                return false;
        }
    }
    if (!ended || p != stream.size()) return false;
    req.op = static_cast<int>(streamOp);
    req.payloadRaw = PackMetadata(metadata);
    if (!metadata.empty() && req.payloadRaw.empty()) return false;
    return true;
}

extern "C" VIRBOX_TARGET_ATTR bool Virbox_PayloadVMInterpret(
        const std::string& envelope, const Context& ctx, InvokeRequest& req) {
    return InterpretRaw(envelope, ctx, req);
}

bool InterpretProtected(const std::string& envelope, const Context& ctx, InvokeRequest& req) {
    return Virbox_PayloadVMInterpret(envelope, ctx, req);
}
}
