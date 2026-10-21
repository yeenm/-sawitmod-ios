// ============================================================================
//  crypto_test.cpp — Crypto.cpp 的本地自检（Windows/MSVC 或 clang 都能跑）
// ----------------------------------------------------------------------------
//  移植时不能连加密都"看起来对"就交付：这里对 SHA-256 / HMAC-SHA256 / MD5 /
//  RC4 / AES-256-GCM 用公开测试向量做回归，向量来源：
//    SHA-256  FIPS 180-2 / NIST
//    HMAC     RFC 4231
//    MD5      RFC 1321
//    RC4      RFC 6229 派生用例（"Key"/"Plaintext"）
//    AES-GCM  NIST CAVP gcmEncryptExtIV256
//  编译： cl /std:c++17 /EHsc /I.. crypto_test.cpp ..\SawitMod\Core\Crypto.cpp
// ============================================================================

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Core/Crypto.h"

static int g_failed = 0;

static void Check(const char *name, const std::string &got, const std::string &want) {
    const bool ok = (got == want);
    if (!ok) ++g_failed;
    printf("[%s] %-28s\n", ok ? " OK " : "FAIL", name);
    if (!ok) {
        printf("        got : %s\n", got.c_str());
        printf("        want: %s\n", want.c_str());
    }
}

static void CheckBytes(const char *name, const uint8_t *got, const uint8_t *want, size_t n) {
    const bool ok = memcmp(got, want, n) == 0;
    if (!ok) ++g_failed;
    printf("[%s] %-28s\n", ok ? " OK " : "FAIL", name);
    if (!ok) {
        printf("        got : ");
        for (size_t i = 0; i < n; ++i) printf("%02x", got[i]);
        printf("\n        want: ");
        for (size_t i = 0; i < n; ++i) printf("%02x", want[i]);
        printf("\n");
    }
}

int main() {
    // ---- SHA-256 ----
    Check("sha256('abc')", SMCrypto::Sha256Hex("abc"),
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Check("sha256('')", SMCrypto::Sha256Hex(""),
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Check("sha256(448-bit msg)",
          SMCrypto::Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // ---- HMAC-SHA-256 (RFC 4231) ----
    Check("hmac case 1", SMCrypto::HmacSha256Hex(std::string(20, '\x0b'), "Hi There"),
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    Check("hmac case 2", SMCrypto::HmacSha256Hex("Jefe", "what do ya want for nothing?"),
          "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    // ---- MD5 ----
    Check("md5('abc')", SMCrypto::Md5Hex("abc"), "900150983cd24fb0d6963f7d28e17f72");
    Check("md5('')", SMCrypto::Md5Hex(""), "d41d8cd98f00b204e9800998ecf8427e");
    Check("md5('message digest')", SMCrypto::Md5Hex("message digest"),
          "f96b697d7cb7938d525a2f31aaf161d0");

    // ---- RC4 ----
    {
        const std::string stream = SMCrypto::Rc4("Plaintext", "Key");
        std::vector<uint8_t> bytes(stream.begin(), stream.end());
        Check("rc4('Plaintext','Key')", SMCrypto::ToHex(bytes), "bbf316e8d940af0ad3");
    }

    // ---- AES-256-GCM (NIST CAVP) ----
    const std::string keyHex(64, '0');             // 32 字节全 0
    const std::string ivHex(24, '0');              // 12 字节全 0
    std::vector<uint8_t> key = SMCrypto::FromHex(keyHex);
    std::vector<uint8_t> iv = SMCrypto::FromHex(ivHex);

    {
        std::vector<uint8_t> cipher, tag;
        SMCrypto::Aes256GcmEncrypt(key.data(), iv.data(), iv.size(), nullptr, 0, cipher, tag);
        Check("aes-gcm empty tag", SMCrypto::ToHex(tag), "530f8afbc74536b9a963b4f1c4cb738b");
    }
    {
        const uint8_t pt[16] = {0};
        std::vector<uint8_t> cipher, tag;
        SMCrypto::Aes256GcmEncrypt(key.data(), iv.data(), iv.size(), pt, 16, cipher, tag);
        Check("aes-gcm cipher", SMCrypto::ToHex(cipher), "cea7403d4d606b6e074ec5d3baf39d18");
        Check("aes-gcm tag", SMCrypto::ToHex(tag), "d0d1c8a799996bf0265b98b5d48ab919");

        std::vector<uint8_t> out;
        const bool ok = SMCrypto::Aes256GcmDecrypt(key.data(), iv.data(), iv.size(),
                                                   cipher.data(), cipher.size(),
                                                   tag.data(), tag.size(), out);
        Check("aes-gcm roundtrip", ok && out.size() == 16 ? "ok" : "bad", "ok");
        // 篡改 tag 必须失败
        tag[0] ^= 0x01;
        const bool rejected = !SMCrypto::Aes256GcmDecrypt(key.data(), iv.data(), iv.size(),
                                                          cipher.data(), cipher.size(),
                                                          tag.data(), tag.size(), out);
        Check("aes-gcm rejects bad tag", rejected ? "ok" : "bad", "ok");
    }

    printf("\n%s (%d failure%s)\n", g_failed ? "FAILED" : "ALL PASSED", g_failed,
           g_failed == 1 ? "" : "s");
    return g_failed ? 1 : 0;
}
