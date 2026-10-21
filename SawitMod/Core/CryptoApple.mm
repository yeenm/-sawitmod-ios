// ============================================================================
//  CryptoApple.mm — ECDH / ECDSA（Security.framework 实现）
// ----------------------------------------------------------------------------
//  Android 版从 libcrypto.so 动态解析 EC_KEY / ECDH_compute_key / ECDSA_do_verify。
//  iOS 没有 OpenSSL，改用系统 Security.framework 的等价能力：
//
//    * 临时密钥：SecKeyCreateRandomKey(kSecAttrKeyTypeECSECPrimeRandom, 256)
//    * 公钥导出：SecKeyCopyExternalRepresentation -> 04||X||Y（65 字节）
//      对外只取后 64 字节的 X||Y（与 Android 版 outClientPubKeyHex 一致）
//    * 共享密钥：SecKeyCopyKeyExchangeResult(kSecKeyAlgorithmECDHKeyExchangeStandard)
//      返回 32 字节（X 坐标），与 ECDH_compute_key 的输出一致
//    * 验签：     SecKeyVerifySignature(kSecKeyAlgorithmECDSASignatureDigestX962SHA256)
//      输入是"已算好的 SHA-256 摘要 + DER 签名"；服务器的签名是 P-1363 (r||s)，
//      这里自动转 DER
//
//  SecKey 对象用 CFRetain 语义持有后塞进 void*，EcdhFree() 里 CFRelease。
// ============================================================================

#include "Core/Crypto.h"

#include <cstring>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace {

// r||s（各 32 字节）转 DER：SEQUENCE { INTEGER r, INTEGER s }
NSData *DerFromRawSignature(const uint8_t *rs, size_t len) {
    if (len != 64) return nil;

    NSMutableData *body = [NSMutableData data];
    auto appendInt = [&body](const uint8_t *p, size_t n) {
        while (n > 1 && p[0] == 0x00) {
            ++p;
            --n;
        }
        const bool pad = (p[0] & 0x80) != 0;  // DER INTEGER 是带符号的
        const uint8_t hdr[2] = {0x02, (uint8_t)(n + (pad ? 1 : 0))};
        [body appendBytes:hdr length:2];
        if (pad) {
            const uint8_t zero = 0;
            [body appendBytes:&zero length:1];
        }
        [body appendBytes:p length:n];
    };
    appendInt(rs, 32);
    appendInt(rs + 32, 32);

    if (body.length > 127) return nil;
    const uint8_t hdr[2] = {0x30, (uint8_t)body.length};
    NSMutableData *der = [NSMutableData dataWithBytes:hdr length:2];
    [der appendData:body];
    return der;
}

NSData *PublicKeyX963(const std::vector<uint8_t> &pubXY) {
    if (pubXY.size() != 64) return nil;
    NSMutableData *data = [NSMutableData dataWithCapacity:65];
    const uint8_t prefix = 0x04;
    [data appendBytes:&prefix length:1];
    [data appendBytes:pubXY.data() length:64];
    return data;
}

SecKeyRef CreatePublicKey(const std::vector<uint8_t> &pubXY) {
    NSData *x963 = PublicKeyX963(pubXY);
    if (!x963) return nullptr;
    NSDictionary *attrs = @{
        (__bridge id)kSecAttrKeyType : (__bridge id)kSecAttrKeyTypeECSECPrimeRandom,
        (__bridge id)kSecAttrKeyClass : (__bridge id)kSecAttrKeyClassPublic,
        (__bridge id)kSecAttrKeySizeInBits : @256,
    };
    CFErrorRef error = nullptr;
    SecKeyRef key = SecKeyCreateWithData((__bridge CFDataRef)x963,
                                         (__bridge CFDictionaryRef)attrs, &error);
    if (error) CFRelease(error);
    return key;
}

}  // namespace

namespace SMCrypto {

void *EcdhGenerateEphemeral(std::string &outClientPubKeyHex) {
    outClientPubKeyHex.clear();

    NSDictionary *attrs = @{
        (__bridge id)kSecAttrKeyType : (__bridge id)kSecAttrKeyTypeECSECPrimeRandom,
        (__bridge id)kSecAttrKeySizeInBits : @256,
    };
    CFErrorRef error = nullptr;
    SecKeyRef priv = SecKeyCreateRandomKey((__bridge CFDictionaryRef)attrs, &error);
    if (error) CFRelease(error);
    if (!priv) return nullptr;

    SecKeyRef pub = SecKeyCopyPublicKey(priv);
    if (!pub) {
        CFRelease(priv);
        return nullptr;
    }

    CFDataRef raw = SecKeyCopyExternalRepresentation(pub, &error);
    if (error) {
        CFRelease(error);
        error = nullptr;
    }
    CFRelease(pub);
    if (!raw) {
        CFRelease(priv);
        return nullptr;
    }

    const uint8_t *bytes = CFDataGetBytePtr(raw);
    const CFIndex len = CFDataGetLength(raw);
    bool ok = false;
    if (bytes && len == 65 && bytes[0] == 0x04) {
        outClientPubKeyHex = ToHex(bytes + 1, 64);
        ok = true;
    }
    CFRelease(raw);

    if (!ok) {
        CFRelease(priv);
        return nullptr;
    }
    return (void *)priv;  // 引用已持有，由 EcdhFree 释放
}

void EcdhFree(void *key) {
    if (key) CFRelease((CFTypeRef)key);
}

bool EcdhComputeShared(void *key, const std::string &serverPubKeyHex, uint8_t out[32]) {
    if (!key || serverPubKeyHex.size() != 128) return false;
    const std::vector<uint8_t> peerXY = FromHex(serverPubKeyHex);
    if (peerXY.size() != 64) return false;

    SecKeyRef peer = CreatePublicKey(peerXY);
    if (!peer) return false;

    NSDictionary *params = @{ (__bridge id)kSecKeyKeyExchangeParameterRequestedSize : @32 };
    CFErrorRef error = nullptr;
    CFDataRef secret = SecKeyCopyKeyExchangeResult(
        (SecKeyRef)key, kSecKeyAlgorithmECDHKeyExchangeStandard, peer,
        (__bridge CFDictionaryRef)params, &error);
    if (error) CFRelease(error);
    CFRelease(peer);

    if (!secret) return false;
    const CFIndex len = CFDataGetLength(secret);
    const uint8_t *bytes = CFDataGetBytePtr(secret);
    bool ok = false;
    if (bytes && len >= 32) {
        // 取 X 坐标（末尾 32 字节），与 OpenSSL ECDH_compute_key 的默认输出一致。
        memcpy(out, bytes + (len - 32), 32);
        ok = true;
    }
    CFRelease(secret);
    return ok;
}

bool EcdsaVerifyP256(const std::vector<uint8_t> &pubXY, const uint8_t hash[32],
                     const std::vector<uint8_t> &sigRS) {
    if (pubXY.size() != 64 || sigRS.size() != 64) return false;

    SecKeyRef pub = CreatePublicKey(pubXY);
    if (!pub) return false;

    NSData *digest = [NSData dataWithBytes:hash length:32];
    NSData *der = DerFromRawSignature(sigRS.data(), sigRS.size());
    if (!der) {
        CFRelease(pub);
        return false;
    }

    CFErrorRef verifyError = nullptr;
    const bool ok = SecKeyVerifySignature(
        pub, kSecKeyAlgorithmECDSASignatureDigestX962SHA256,
        (__bridge CFDataRef)digest, (__bridge CFDataRef)der, &verifyError);
    if (verifyError) CFRelease(verifyError);
    CFRelease(pub);
    return ok;
}

}  // namespace SMCrypto
