#pragma once
// ============================================================================
//  Crypto.h — iOS 版密码学原语（替代 Android 上从 libcrypto.so 动态解析）
// ----------------------------------------------------------------------------
//  Android 工程通过 xdl_open("libcrypto.so") + xdl_sym 调用 BoringSSL 的
//  SHA256 / HMAC / EC_KEY / ECDH_compute_key / ECDSA_do_verify / EVP_aes_256_gcm。
//  iOS 没有 libcrypto.so，这里分成两块：
//
//    Crypto.cpp       纯 C++：SHA-256 / HMAC-SHA256 / MD5 / RC4 / AES-256-GCM /
//                     hex / base64 / HKDF-SHA256
//    CryptoApple.mm   Security.framework：NIST P-256 临时密钥 + ECDH 共享密钥
//                     和 ECDSA P-256 验签（r||s -> DER 由调用方无需关心）
//
//  协议参数与 Android 版逐字节一致：
//    * HKDF: PRK = HMAC(salt = nonce + "|" + hwid, ikm = sharedSecret)
//             key = HMAC(PRK, "SawitMod-Session-v4" + 0x01)
//    * AES-256-GCM: 12 字节 IV、16 字节 tag、无 AAD
//    * ECDSA: SHA-256 摘要 + P-1363 (r||s) 64 字节签名
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SMCrypto {

// ---- 摘要 ----
std::string Sha256Hex(const std::string &data);
bool Sha256Raw(const std::string &data, uint8_t out[32]);
std::string HmacSha256Hex(const std::string &key, const std::string &data);
bool HmacSha256Raw(const std::string &key, const uint8_t *data, size_t len, uint8_t out[32]);
std::string Md5Hex(const std::string &data);

// ---- 对称流 ----
std::string Rc4(const std::string &data, const std::string &key);

// ---- hex / base64 ----
std::string ToHex(const uint8_t *data, size_t len);
std::string ToHex(const std::vector<uint8_t> &data);
std::vector<uint8_t> FromHex(const std::string &hex);
std::string Base64Decode(const std::string &text);   // 失败返回空串

// ---- AES-256-GCM ----
// 解密；tag 校验失败返回 false。
bool Aes256GcmDecrypt(const uint8_t key[32], const uint8_t *iv, size_t ivLen,
                      const uint8_t *cipher, size_t cipherLen,
                      const uint8_t *tag, size_t tagLen, std::vector<uint8_t> &out);
bool Aes256GcmEncrypt(const uint8_t key[32], const uint8_t *iv, size_t ivLen,
                      const uint8_t *plain, size_t plainLen,
                      std::vector<uint8_t> &cipher, std::vector<uint8_t> &tag);

// ---- ECDH P-256（Security.framework 实现，见 CryptoApple.mm）----
// 生成临时密钥；成功时 outClientPubKeyHex 为 128 hex（X||Y），返回值是非空的
// 不透明句柄，用完必须调 EcdhFree()。
void *EcdhGenerateEphemeral(std::string &outClientPubKeyHex);
void EcdhFree(void *key);
// 用临时私钥 + 服务器公钥算 32 字节共享密钥（P-256 ECDH 的 X 坐标）。
bool EcdhComputeShared(void *key, const std::string &serverPubKeyHex, uint8_t out[32]);

// ---- ECDSA P-256 验签 ----
// pubXY: 64 字节 X||Y；hash: 32 字节 SHA-256 摘要；sigRS: 64 字节 r||s。
bool EcdsaVerifyP256(const std::vector<uint8_t> &pubXY, const uint8_t hash[32],
                     const std::vector<uint8_t> &sigRS);

}  // namespace SMCrypto
