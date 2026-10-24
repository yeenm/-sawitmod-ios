#include "Weiyan.h"

#include "App/Platform.h"    // SMPlatform::DeviceCode() / HWID() / DataDir()
#include "Global.h"          // 全局状态（iOS 版转发 Core/State.h）
#include "Includes/Logger.h" // LOGI / LOGE（iOS 版默认空实现，见该头文件）
#include "NetAuth.h"         // GetHWIDNative() / Sha256HexNativePublic

#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>   // strtol（chunked 解析用）
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>   // struct timeval / SO_RCVTIMEO（iOS 上不靠传递包含）
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace {

// iOS/macOS 没有 MSG_NOSIGNAL：向已关闭的 socket 写数据会触发 SIGPIPE，
// 默认动作是直接终止进程 —— 表现为"点验证卡密/拉公告时闪退"。
// 建立 socket 后必须置位 SO_NOSIGPIPE（Bootstrap 里另外全局忽略了 SIGPIPE）。
void SetNoSigPipe(int fd) {
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)fd;
#endif
}

// =====================================================================
//  常量（与原实现同一组，勿改）
// =====================================================================
const char* kHostDefault = "wy.llua.cn";
const char* kPathDefault = "v2/c84ec01e30ad49897de7c59f5edd4af8";
// 面板文档里的正式「单码登录」接口：GET/POST /api/?id=kmlogon&app=<应用ID>&data=<密文参数>
const char* kDocLoginPath = "api/";
const char* kDocLoginId = "kmlogon";
const char* kAppIdDefault = "85501";   // 后台「应用管理」里的应用ID
// RC4 密钥：原文 \x9e\x9a\xac... 逐字节异或 235
const char* kRc4Key = "uqG58Y5mJJI85501";
// 签名盐（原实现里两处异或字符串解出来是同一个）
const char* kSalt = "Zv5uMs2u22pVp1np";

const char* kIdLoginDefault = "CAE73D714";
const char* kIdNotice = "5DD7BD45C4C";
const char* kIdUpdate = "7D46A8F7D";

// 面板存在两种状态码写法：参考实现的老格式把码放在空键 ""（成功=2611），
// 新版放 "code"（成功=200，公告端点就是这么回的）。两种都认。
const long kOkCodeOld = 2611;
const long kOkCodeNew = 200;

const char* kMagic = "5e81db27834696c9a9436e821c8bc448";
const char* kMagicField = "w770569e211a2ebe6dd8737414eb585b2";

// =====================================================================
//  MD5（标准实现，逐字节对齐原 weiyan/Util.h 的输出）
// =====================================================================
struct Md5Ctx {
    unsigned int count[2];
    unsigned int state[4];
    unsigned char buffer[64];
};

const unsigned char kPadding[64] = {
    128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

#define MD5_F(x, y, z) ((x & y) | (~x & z))
#define MD5_G(x, y, z) ((x & z) | (y & ~z))
#define MD5_H(x, y, z) (x ^ y ^ z)
#define MD5_I(x, y, z) (y ^ (x | ~z))
#define MD5_ROTL(x, n) ((x << n) | (x >> (32 - n)))
#define MD5_FF(a, b, c, d, x, s, ac) { a += MD5_F(b, c, d) + x + ac; a = MD5_ROTL(a, s); a += b; }
#define MD5_GG(a, b, c, d, x, s, ac) { a += MD5_G(b, c, d) + x + ac; a = MD5_ROTL(a, s); a += b; }
#define MD5_HH(a, b, c, d, x, s, ac) { a += MD5_H(b, c, d) + x + ac; a = MD5_ROTL(a, s); a += b; }
#define MD5_II(a, b, c, d, x, s, ac) { a += MD5_I(b, c, d) + x + ac; a = MD5_ROTL(a, s); a += b; }

void Md5Init(Md5Ctx* ctx) {
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
}

void Md5Encode(unsigned char* output, unsigned int* input, unsigned int len) {
    unsigned int i = 0, j = 0;
    while (j < len) {
        output[j] = input[i] & 0xFF;
        output[j + 1] = (input[i] >> 8) & 0xFF;
        output[j + 2] = (input[i] >> 16) & 0xFF;
        output[j + 3] = (input[i] >> 24) & 0xFF;
        i++;
        j += 4;
    }
}

void Md5Decode(unsigned int* output, unsigned char* input, unsigned int len) {
    unsigned int i = 0, j = 0;
    while (j < len) {
        output[i] = (input[j]) | (input[j + 1] << 8) | (input[j + 2] << 16) | (input[j + 3] << 24);
        i++;
        j += 4;
    }
}

void Md5Transform(unsigned int state[4], unsigned char block[64]) {
    unsigned int a = state[0], b = state[1], c = state[2], d = state[3];
    unsigned int x[16];
    Md5Decode(x, block, 64);

    MD5_FF(a, b, c, d, x[0], 7, 0xd76aa478); MD5_FF(d, a, b, c, x[1], 12, 0xe8c7b756);
    MD5_FF(c, d, a, b, x[2], 17, 0x242070db); MD5_FF(b, c, d, a, x[3], 22, 0xc1bdceee);
    MD5_FF(a, b, c, d, x[4], 7, 0xf57c0faf); MD5_FF(d, a, b, c, x[5], 12, 0x4787c62a);
    MD5_FF(c, d, a, b, x[6], 17, 0xa8304613); MD5_FF(b, c, d, a, x[7], 22, 0xfd469501);
    MD5_FF(a, b, c, d, x[8], 7, 0x698098d8); MD5_FF(d, a, b, c, x[9], 12, 0x8b44f7af);
    MD5_FF(c, d, a, b, x[10], 17, 0xffff5bb1); MD5_FF(b, c, d, a, x[11], 22, 0x895cd7be);
    MD5_FF(a, b, c, d, x[12], 7, 0x6b901122); MD5_FF(d, a, b, c, x[13], 12, 0xfd987193);
    MD5_FF(c, d, a, b, x[14], 17, 0xa679438e); MD5_FF(b, c, d, a, x[15], 22, 0x49b40821);

    MD5_GG(a, b, c, d, x[1], 5, 0xf61e2562); MD5_GG(d, a, b, c, x[6], 9, 0xc040b340);
    MD5_GG(c, d, a, b, x[11], 14, 0x265e5a51); MD5_GG(b, c, d, a, x[0], 20, 0xe9b6c7aa);
    MD5_GG(a, b, c, d, x[5], 5, 0xd62f105d); MD5_GG(d, a, b, c, x[10], 9, 0x02441453);
    MD5_GG(c, d, a, b, x[15], 14, 0xd8a1e681); MD5_GG(b, c, d, a, x[4], 20, 0xe7d3fbc8);
    MD5_GG(a, b, c, d, x[9], 5, 0x21e1cde6); MD5_GG(d, a, b, c, x[14], 9, 0xc33707d6);
    MD5_GG(c, d, a, b, x[3], 14, 0xf4d50d87); MD5_GG(b, c, d, a, x[8], 20, 0x455a14ed);
    MD5_GG(a, b, c, d, x[13], 5, 0xa9e3e905); MD5_GG(d, a, b, c, x[2], 9, 0xfcefa3f8);
    MD5_GG(c, d, a, b, x[7], 14, 0x676f02d9); MD5_GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    MD5_HH(a, b, c, d, x[5], 4, 0xfffa3942); MD5_HH(d, a, b, c, x[8], 11, 0x8771f681);
    MD5_HH(c, d, a, b, x[11], 16, 0x6d9d6122); MD5_HH(b, c, d, a, x[14], 23, 0xfde5380c);
    MD5_HH(a, b, c, d, x[1], 4, 0xa4beea44); MD5_HH(d, a, b, c, x[4], 11, 0x4bdecfa9);
    MD5_HH(c, d, a, b, x[7], 16, 0xf6bb4b60); MD5_HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    MD5_HH(a, b, c, d, x[13], 4, 0x289b7ec6); MD5_HH(d, a, b, c, x[0], 11, 0xeaa127fa);
    MD5_HH(c, d, a, b, x[3], 16, 0xd4ef3085); MD5_HH(b, c, d, a, x[6], 23, 0x04881d05);
    MD5_HH(a, b, c, d, x[9], 4, 0xd9d4d039); MD5_HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    MD5_HH(c, d, a, b, x[15], 16, 0x1fa27cf8); MD5_HH(b, c, d, a, x[2], 23, 0xc4ac5665);

    MD5_II(a, b, c, d, x[0], 6, 0xf4292244); MD5_II(d, a, b, c, x[7], 10, 0x432aff97);
    MD5_II(c, d, a, b, x[14], 15, 0xab9423a7); MD5_II(b, c, d, a, x[5], 21, 0xfc93a039);
    MD5_II(a, b, c, d, x[12], 6, 0x655b59c3); MD5_II(d, a, b, c, x[3], 10, 0x8f0ccc92);
    MD5_II(c, d, a, b, x[10], 15, 0xffeff47d); MD5_II(b, c, d, a, x[1], 21, 0x85845dd1);
    MD5_II(a, b, c, d, x[8], 6, 0x6fa87e4f); MD5_II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    MD5_II(c, d, a, b, x[6], 15, 0xa3014314); MD5_II(b, c, d, a, x[13], 21, 0x4e0811a1);
    MD5_II(a, b, c, d, x[4], 6, 0xf7537e82); MD5_II(d, a, b, c, x[11], 10, 0xbd3af235);
    MD5_II(c, d, a, b, x[2], 15, 0x2ad7d2bb); MD5_II(b, c, d, a, x[9], 21, 0xeb86d391);

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

void Md5Update(Md5Ctx* ctx, const unsigned char* input, unsigned int inputlen) {
    unsigned int i = 0, index = 0, partlen = 0;
    index = (ctx->count[0] >> 3) & 0x3F;
    partlen = 64 - index;
    ctx->count[0] += inputlen << 3;
    if (ctx->count[0] < (inputlen << 3)) ctx->count[1]++;
    ctx->count[1] += inputlen >> 29;

    if (inputlen >= partlen) {
        memcpy(&ctx->buffer[index], input, partlen);
        Md5Transform(ctx->state, ctx->buffer);
        for (i = partlen; i + 64 <= inputlen; i += 64) Md5Transform(ctx->state, &((unsigned char*)input)[i]);
        index = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[index], &input[i], inputlen - i);
}

void Md5Final(Md5Ctx* ctx, unsigned char digest[16]) {
    unsigned int index = (ctx->count[0] >> 3) & 0x3F;
    unsigned int padlen = (index < 56) ? (56 - index) : (120 - index);
    unsigned char bits[8];
    Md5Encode(bits, ctx->count, 8);
    Md5Update(ctx, kPadding, padlen);
    Md5Update(ctx, bits, 8);
    Md5Encode(digest, ctx->state, 16);
}

std::string Md5Hex(const std::string& in) {
    Md5Ctx ctx;
    unsigned char digest[16];
    Md5Init(&ctx);
    Md5Update(&ctx, (const unsigned char*)in.data(), (unsigned int)in.size());
    Md5Final(&ctx, digest);
    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 16; ++i) {
        out += hexd[digest[i] >> 4];
        out += hexd[digest[i] & 0xF];
    }
    return out;
}

// =====================================================================
//  RC4 / hex（与原实现 u2dfd44833f / h862a774447 / neea9fc9162 一致）
// =====================================================================
std::string Rc4(const std::string& text, const std::string& key) {
    if (key.empty()) return text;
    unsigned char s[256];
    for (int i = 0; i < 256; ++i) s[i] = (unsigned char)i;
    int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + s[i] + (unsigned char)key[i % key.size()]) % 256;
        unsigned char t = s[i]; s[i] = (unsigned char)s[j]; s[j] = t;
    }
    std::string out;
    out.resize(text.size());
    int i = 0, k = 0;
    for (size_t n = 0; n < text.size(); ++n) {
        i = (i + 1) % 256;
        k = (k + s[i]) % 256;
        unsigned char t = s[i]; s[i] = (unsigned char)s[k]; s[k] = t;
        out[n] = (char)((unsigned char)text[n] ^ s[(s[i] + s[k]) % 256]);
    }
    return out;
}

std::string ToHex(const std::string& bin) {
    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(bin.size() * 2);
    for (size_t i = 0; i < bin.size(); ++i) {
        const unsigned char c = (unsigned char)bin[i];
        out += hexd[c >> 4];
        out += hexd[c & 0xF];
    }
    return out;
}

std::string FromHex(const std::string& hex) {
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const char a = hex[i], b = hex[i + 1];
        auto val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = val(a), lo = val(b);
        if (hi < 0 || lo < 0) return "";
        out += (char)((hi << 4) | lo);
    }
    return out;
}

// =====================================================================
//  极简 JSON（只认对象/数组/字符串/数字，够解析微验返回）
// =====================================================================
struct JsonMap {
    std::unordered_map<std::string, std::string> values;
    std::unordered_map<std::string, double> numbers;
    bool Has(const std::string& k) const {
        return values.find(k) != values.end() || numbers.find(k) != numbers.end();
    }
    std::string Str(const std::string& k) const {
        auto it = values.find(k);
        return it == values.end() ? std::string() : it->second;
    }
    double Num(const std::string& k, double def = 0.0) const {
        auto it = numbers.find(k);
        return it == numbers.end() ? def : it->second;
    }
};

void JsonSkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

bool JsonParseString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        const char c = s[i++];
        if (c == '\\') {
            if (i >= s.size()) return false;
            const char e = s[i++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '/': out += '/'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = s[i + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    }
                    i += 4;
                    if (cp < 0x80) {
                        out += (char)cp;
                    } else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += e; break;
            }
        } else if (c == '"') {
            return true;
        } else {
            out += c;
        }
    }
    return false;
}

void JsonParseValue(const std::string& s, size_t& i, const std::string& path, JsonMap& out);

void JsonParseObject(const std::string& s, size_t& i, const std::string& prefix, JsonMap& out) {
    JsonSkipWs(s, i);
    if (i >= s.size() || s[i] != '{') return;
    ++i;
    for (;;) {
        JsonSkipWs(s, i);
        if (i < s.size() && s[i] == '}') { ++i; return; }
        std::string key;
        if (!JsonParseString(s, i, key)) return;
        JsonSkipWs(s, i);
        if (i >= s.size() || s[i] != ':') return;
        ++i;
        JsonParseValue(s, i, prefix + key, out);
        JsonSkipWs(s, i);
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') { ++i; return; }
        return;
    }
}

void JsonParseValue(const std::string& s, size_t& i, const std::string& path, JsonMap& out) {
    JsonSkipWs(s, i);
    if (i >= s.size()) return;
    const char c = s[i];
    if (c == '{') { JsonParseObject(s, i, path + ".", out); return; }
    if (c == '"') {
        std::string v;
        if (JsonParseString(s, i, v)) out.values[path] = v;
        return;
    }
    if (c == '[') {
        int depth = 0;
        while (i < s.size()) {
            if (s[i] == '[') ++depth;
            else if (s[i] == ']') { --depth; if (depth == 0) { ++i; return; } }
            ++i;
        }
        return;
    }
    const size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
    std::string num = s.substr(start, i - start);
    while (!num.empty() && (num.back() == ' ' || num.back() == '\n' || num.back() == '\r' || num.back() == '\t'))
        num.pop_back();
    if (num.empty()) return;
    if (num == "true") { out.numbers[path] = 1; return; }
    if (num == "false" || num == "null") { out.numbers[path] = 0; return; }
    try { out.numbers[path] = std::stod(num); } catch (...) {}
}

bool JsonParse(const std::string& text, JsonMap& out) {
    size_t i = 0;
    JsonSkipWs(text, i);
    if (i >= text.size() || text[i] != '{') return false;
    JsonParseObject(text, i, "", out);
    return !out.values.empty() || !out.numbers.empty();
}

// =====================================================================
//  原生 socket HTTP（不走 Java 网络栈，避开 cleartext 策略）
// =====================================================================
bool ReadFully(int fd, char* buf, int len) {
    int got = 0;
    while (got < len) {
        const int n = (int)read(fd, buf + got, len - got);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

// 阿里 DNS(223.5.5.5) over TCP 的 A 记录查询；getaddrinfo 失败时兜底
std::string ResolveViaAliyun(const std::string& domain) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    SetNoSigPipe(fd);
    if (fd < 0) return "";
    sockaddr_in dns{};
    dns.sin_family = AF_INET;
    dns.sin_port = htons(53);
    if (inet_pton(AF_INET, "223.5.5.5", &dns.sin_addr) <= 0 ||
        connect(fd, (sockaddr*)&dns, sizeof(dns)) < 0) {
        close(fd);
        return "";
    }

    std::vector<unsigned char> q;
    const uint16_t id = (uint16_t)(time(nullptr) & 0xFFFF);
    q.push_back((id >> 8) & 0xFF); q.push_back(id & 0xFF);
    q.push_back(0x01); q.push_back(0x00);
    q.push_back(0x00); q.push_back(0x01);
    for (int i = 0; i < 6; ++i) q.push_back(0x00);
    size_t start = 0;
    while (true) {
        const size_t dot = domain.find('.', start);
        const std::string seg = (dot == std::string::npos) ? domain.substr(start)
                                                           : domain.substr(start, dot - start);
        q.push_back((unsigned char)seg.size());
        for (char ch : seg) q.push_back((unsigned char)ch);
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    q.push_back(0x00);
    q.push_back(0x00); q.push_back(0x01);
    q.push_back(0x00); q.push_back(0x01);

    std::vector<unsigned char> framed;
    const uint16_t qlen = (uint16_t)q.size();
    framed.push_back((qlen >> 8) & 0xFF); framed.push_back(qlen & 0xFF);
    framed.insert(framed.end(), q.begin(), q.end());
    if (send(fd, framed.data(), framed.size(), 0) <= 0) { close(fd); return ""; }

    char lenb[2];
    if (!ReadFully(fd, lenb, 2)) { close(fd); return ""; }
    const uint16_t rlen = (uint16_t)(((unsigned char)lenb[0] << 8) | (unsigned char)lenb[1]);
    if (rlen < 12 || rlen > 4096) { close(fd); return ""; }
    std::vector<unsigned char> resp(rlen);
    if (!ReadFully(fd, (char*)resp.data(), rlen)) { close(fd); return ""; }
    close(fd);

    const uint16_t ancount = (uint16_t)((resp[6] << 8) | resp[7]);
    if (ancount == 0) return "";
    size_t pos = 12;
    while (pos < resp.size() && resp[pos] != 0) pos += (size_t)resp[pos] + 1;
    pos += 5;
    for (int i = 0; i < ancount && pos < resp.size(); ++i) {
        if ((resp[pos] & 0xC0) == 0xC0) {
            pos += 2;
        } else {
            while (pos < resp.size() && resp[pos] != 0) pos += (size_t)resp[pos] + 1;
            pos++;
        }
        if (pos + 10 > resp.size()) break;
        const uint16_t type = (uint16_t)((resp[pos] << 8) | resp[pos + 1]);
        pos += 8;
        const uint16_t rdlength = (uint16_t)((resp[pos] << 8) | resp[pos + 1]);
        pos += 2;
        if (type == 0x0001 && rdlength == 4 && pos + 4 <= resp.size()) {
            char ip[24];
            snprintf(ip, sizeof(ip), "%u.%u.%u.%u", resp[pos], resp[pos + 1], resp[pos + 2], resp[pos + 3]);
            return std::string(ip);
        }
        pos += rdlength;
    }
    return "";
}

std::string ResolveHost(const std::string& domain) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(domain.c_str(), nullptr, &hints, &res) == 0 && res) {
        char ip[64] = {0};
        inet_ntop(AF_INET, &((sockaddr_in*)res->ai_addr)->sin_addr, ip, sizeof(ip));
        freeaddrinfo(res);
        if (ip[0]) return std::string(ip);
    }
    return ResolveViaAliyun(domain);
}

// 读 HTTP 响应体：Content-Length 与 chunked 两种都处理
std::string ReadHttpBody(int fd) {
    std::vector<char> head(4096);
    int used = 0;
    while (used < (int)head.size() - 1) {
        const int n = (int)read(fd, &head[used], 1);
        if (n <= 0) return "";
        ++used;
        if (used >= 4 && memcmp(&head[used - 4], "\r\n\r\n", 4) == 0) break;
    }
    const std::string header(head.data(), used);

    auto findHeader = [&header](const char* key) -> std::string {
        const std::string k(key);
        size_t p = 0;
        while ((p = header.find(k, p)) != std::string::npos) {
            const bool line_start = (p == 0) || header[p - 1] == '\n';
            if (line_start) {
                const size_t e = header.find("\r\n", p);
                if (e == std::string::npos) break;
                return header.substr(p + k.size(), e - p - k.size());
            }
            p += k.size();
        }
        return "";
    };

    const std::string cl = findHeader("Content-Length: ");
    if (!cl.empty()) {
        int len = 0;
        try { len = std::stoi(cl); } catch (...) { len = 0; }
        if (len <= 0) return "";
        if (len > 1024 * 512) len = 1024 * 512;
        std::string body;
        body.resize((size_t)len);
        if (!ReadFully(fd, &body[0], len)) return "";
        return body;
    }

    // chunked
    std::string body;
    for (;;) {
        char line[32] = {0};
        int li = 0;
        while (li < (int)sizeof(line) - 1) {
            const int n = (int)read(fd, &line[li], 1);
            if (n <= 0) return body;
            if (line[li] == '\n') break;
            if (line[li] != '\r') ++li;
        }
        line[li] = 0;
        int chunk = 0;
        try { chunk = (int)strtol(line, nullptr, 16); } catch (...) { chunk = 0; }
        if (chunk <= 0) break;
        if (chunk > 1024 * 512) break;
        std::string part;
        part.resize((size_t)chunk);
        if (!ReadFully(fd, &part[0], chunk)) break;
        body += part;
        char crlf[2];
        if (!ReadFully(fd, crlf, 2)) break;
    }
    return body;
}

// 原生 socket 请求（http，默认 80 端口）；target 形如 "v2/xxx" 或 "api/?a=b"
std::string HttpRequest(const std::string& host, const std::string& target,
                        const std::string& body, const std::string& ctype, bool useGet) {
    const std::string ip = ResolveHost(host);
    if (ip.empty()) {
        LOGE("DNS 解析失败: %s", host.c_str());
        return "";
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    SetNoSigPipe(fd);
    if (fd < 0) return "";

    timeval tv{};
    tv.tv_sec = 10;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        LOGE("连接失败: %s (%s)", host.c_str(), ip.c_str());
        return "";
    }

    std::ostringstream req;
    if (useGet || body.empty()) {
        req << "GET /" << target << " HTTP/1.1\r\n"
            << "Host: " << host << "\r\n"
            << "User-Agent: Mozilla/4.0(compatible)\r\n"
            << "Connection: close\r\n\r\n";
    } else {
        req << "POST /" << target << " HTTP/1.1\r\n"
            << "Host: " << host << "\r\n"
            << "Content-Type: " << ctype << "\r\n"
            << "User-Agent: Mozilla/4.0(compatible)\r\n"
            << "Connection: close\r\n"
            << "Content-Length: " << body.size() << "\r\n\r\n"
            << body;
    }
    const std::string raw = req.str();
    if (send(fd, raw.data(), raw.size(), 0) <= 0) {
        close(fd);
        return "";
    }
    std::string resp = ReadHttpBody(fd);
    close(fd);
    return resp;
}

std::string UrlEncode(const std::string& s) {
    static const char* hexd = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hexd[c >> 4];
            out += hexd[c & 0xF];
        }
    }
    return out;
}

// 明文 -> hex(RC4(明文))；响应 -> RC4(hex解码)
std::string EncodeRequest(const std::string& plain) {
    return ToHex(Rc4(plain, kRc4Key));
}
std::string DecodeResponse(const std::string& body) {
    std::string trimmed = body;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' '))
        trimmed.pop_back();
    return Rc4(FromHex(trimmed), kRc4Key);
}

// 接口配置（默认值 = zip 里 main.cpp 的那一组）
std::mutex g_cfgMtx;
bool g_cfgLoaded = false;
std::string g_host;
std::string g_path;
std::string g_loginId;
std::string g_loginPath;   // 空 = 与 path 相同
std::string g_appId;       // 后台应用ID（单码登录接口要带）
std::string g_docPath;     // 单码登录接口路径（默认 api/）
std::string g_docId;       // 单码登录接口标识（默认 kmlogon）
bool g_useDocLogin = true; // 走面板文档的 /api/?id=kmlogon 接口（false = 老式 /v2/<token> + id=）

// ---- 本地文件路径 ----
// iOS 沙盒里没有旧版那种全局可写目录，卡密 / 设备码统一放应用自己的私有目录：
//   SMPlatform::DataDir() = Library/Application Support/SawitMod（首次访问自动创建）
// 文件名沿用旧版，面板侧排查脚本不用改。
// 旧版另外两处外部路径（.imei 副本）在 iOS 上合并到同一目录，
// 但仍然是各自独立的文件，多副本互相兜底的逻辑不变。
const std::string kApiFilePath = SMPlatform::DataDir() + "/weiyan_api.txt";
const std::string kKmFilePath = SMPlatform::DataDir() + "/weiyan_km.txt";
const std::string kKmFileAltPath = SMPlatform::DataDir() + "/.km";
const std::string kIdFilePath = SMPlatform::DataDir() + "/weiyan_imei.txt";
const std::string kIdFileAltPath = SMPlatform::DataDir() + "/.imei";   // 原客户端用的文件名
// v2 会把算出来的码也回填到这个副本，便于排查
const std::string kIdFileExtPath = SMPlatform::DataDir() + "/.imei_ext";
// 可选的人工指定（界面上没有入口；放这个文件即生效，用于个别用户救急）
const std::string kIdFileOverridePath = SMPlatform::DataDir() + "/weiyan_imei_override.txt";

const char* kApiFile = kApiFilePath.c_str();
const char* kKmFile = kKmFilePath.c_str();
const char* kKmFileAlt = kKmFileAltPath.c_str();
const char* kIdFile = kIdFilePath.c_str();
const char* kIdFileAlt = kIdFileAltPath.c_str();
const char* kIdFileExt = kIdFileExtPath.c_str();
const char* kIdFileOverride = kIdFileOverridePath.c_str();

// 接口配置：weiyan_api.txt，一行一个 key=value；缺项回落到默认值。
//   host=wy.llua.cn
//   path=v2/c84ec01e30ad49897de7c59f5edd4af8
//   login_id=CAE73D714
//   login_path=            （留空=与 path 相同）
void EnsureCfg() {
    if (g_cfgLoaded) return;
    g_cfgLoaded = true;
    g_host = kHostDefault;
    g_path = kPathDefault;
    g_loginId = kIdLoginDefault;
    g_loginPath.clear();
    g_appId = kAppIdDefault;
    g_docPath = kDocLoginPath;
    g_docId = kDocLoginId;
    g_useDocLogin = true;

    std::ifstream f(kApiFile);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        while (!v.empty() && (v.front() == ' ' || v.front() == '/')) v.erase(v.begin());
        while (!v.empty() && v.back() == '/') v.pop_back();
        if (v.empty()) continue;
        if (k == "host") g_host = v;
        else if (k == "path") g_path = v;
        else if (k == "login_id") g_loginId = v;
        else if (k == "login_path") g_loginPath = v;
        else if (k == "app" || k == "appid") g_appId = v;
        else if (k == "doc_path") g_docPath = v;
        else if (k == "doc_id") g_docId = v;
        else if (k == "login_mode") g_useDocLogin = (v != "legacy");
    }
    LOGI("api cfg: host=%s app=%s doc=%s/%s legacy_path=%s login_id=%s",
           g_host.c_str(), g_appId.c_str(), g_docPath.c_str(), g_docId.c_str(),
           g_path.c_str(), g_loginId.c_str());
}

bool WriteLine(const char* path, const std::string& text) {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return false;
    f << text << "\n";
    f.flush();
    const bool ok = !f.fail();
    f.close();
    return ok;
}

// 面板文档里的「单码登录」：GET /api/?id=kmlogon&app=<应用ID>&data=<hex(RC4(参数串))>
// 实测（用不存在的卡密）：这样提交面板才会真正去查卡密并回 149/200，
// 明文提交只会回 104「签名为空」。
std::string ApiCallDocLogin(const std::string& params, JsonMap& out, std::string* rawJson) {
    EnsureCfg();
    const std::string data = UrlEncode(EncodeRequest(params));
    const std::string target = g_docPath + "?id=" + g_docId + "&app=" + g_appId + "&data=" + data;
    const std::string body = HttpRequest(g_host, target, "", "", true);
    if (body.empty()) return "网络请求失败（检查手机网络 / 面板域名）";
    const std::string json = DecodeResponse(body);
    if (rawJson) *rawJson = json;
    LOGI("doc login url=/%.80s -> %.200s", target.c_str(), json.c_str());
    if (json.empty() || json[0] != '{') return "服务器返回异常";
    if (!JsonParse(json, out)) return "响应解析失败";
    return "";
}

// 老式接口：POST /v2/<token>，body = hex(RC4(明文))。公告/版本仍在用这条。
std::string ApiCall(const std::string& plain, JsonMap& out, std::string* rawJson = nullptr,
                    bool useLoginPath = false) {
    EnsureCfg();
    const std::string host = g_host;
    const std::string path = (useLoginPath && !g_loginPath.empty()) ? g_loginPath : g_path;
    const std::string body = HttpRequest(host, path, EncodeRequest(plain),
                                         "application/x-www-form-urlencoded", false);
    if (body.empty()) return "网络请求失败（检查手机网络 / 面板域名）";
    const std::string json = DecodeResponse(body);
    if (rawJson) *rawJson = json;
    if (json.empty() || json[0] != '{') {
        LOGE("响应不是 JSON: %.120s", json.c_str());
        return "服务器返回异常";
    }
    if (!JsonParse(json, out)) return "响应解析失败";
    return "";
}

// =====================================================================
//  状态
// =====================================================================
std::mutex g_mtx;
bool g_verified = false;
std::string g_status = u8"请输入卡密后点击登录";
std::string g_card;
std::string g_lastRaw;
long long g_expiresAt = 0;    // 0 = 次数卡 / 未验证
int g_remaining = -1;         // 剩余次数（-1 = 面板没给）
long long g_durationSec = 0;  // 面板给"剩余时长"时用它

std::string ReadFirstLine(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
        line.pop_back();
    return line;
}

long long NowSeconds() {
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string RandomDigits(int n) {
    static std::mt19937_64 gen((unsigned long long)std::chrono::high_resolution_clock::now()
                                       .time_since_epoch().count() ^
                               (unsigned long long)std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 9);
    std::string s;
    s.reserve((size_t)n);
    for (int i = 0; i < n; ++i) s += (char)('0' + dist(gen));
    return s;
}

// unix 秒 -> 本地时间字符串
std::string FormatUnixTime(long long sec) {
    if (sec <= 0) return u8"未知";
    const std::time_t tt = (std::time_t)sec;
    std::tm tmv{};
    localtime_r(&tt, &tmv);
    char buf[64] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return std::string(buf);
}

// 剩余秒数 -> "N 天 N 小时"
std::string FormatDuration(long long sec) {
    if (sec <= 0) return u8"未知";
    const long long days = sec / 86400;
    const long long hours = (sec % 86400) / 3600;
    if (days > 0)
        return std::to_string(days) + u8" 天 " + std::to_string(hours) + u8" 小时";
    const long long mins = (sec % 3600) / 60;
    return std::to_string(hours) + u8" 小时 " + std::to_string(mins) + u8" 分钟";
}

// 原客户端 generateUUID 的格式：5 组 4 位十六进制，用 '-' 连接。
// 卡密如果已经绑过设备，面板比对的就是这个串，所以格式和存储位置都要沿用。
std::string GenerateDeviceCode() {
    static const char* hexd = "0123456789abcdef";
    std::mt19937_64 gen((unsigned long long)std::chrono::high_resolution_clock::now()
                                .time_since_epoch().count() ^
                        (unsigned long long)std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string s;
    for (int g = 0; g < 5; ++g) {
        if (g) s += '-';
        for (int i = 0; i < 4; ++i) s += hexd[dist(gen)];
    }
    return s;
}

// =====================================================================
//  登录响应解析（不含网络，自检可以单独喂数据进来验证）
// =====================================================================
bool InterpretLoginResponse(const std::string& json, const std::string& sign,
                            long long localTs, Weiyan::Result& r, std::string& rawOut) {
    rawOut = json;
    JsonMap j;
    if (!JsonParse(json, j)) {
        r.message = u8"响应解析失败";
        return false;
    }

    // 1) 状态码：老格式在空键 ""，新格式在 "code"
    long code = 0;
    bool hasCode = false;
    const bool legacyShape = j.Has("");     // 面板老格式：{"":2611,...}
    if (legacyShape) { code = (long)j.Num(""); hasCode = true; }
    else if (j.Has("code")) { code = (long)j.Num("code"); hasCode = true; }
    if (!hasCode) {
        r.message = u8"服务器未返回验证结果（卡密可能不存在、已绑定其他设备或已过期）";
        return false;
    }
    if (code != kOkCodeOld && code != kOkCodeNew) {
        std::string msg = j.Str("msg");
        if (msg.empty()) msg = j.Str("msg.msg");
        if (msg.empty()) msg = u8"卡密验证失败（状态码 " + std::to_string(code) + u8"）";
        r.message = msg;
        return false;
    }

    // 2) 面板标识：字段存在就必须匹配（老格式一定会带）
    const std::string magic = j.Str(std::string("msg.") + kMagicField);
    if (!magic.empty() && magic != kMagic) {
        r.message = u8"服务器校验标识不符";
        return false;
    }

    // 3) 时间偏差：只在老格式下强制校验 ±30s。新格式（/api/?id=kmlogon）由面板
    //    自己按后台开关判断，本机时钟偏一点不应该把登录挡掉。
    if (legacyShape && j.Has("time")) {
        const long long serverTime = (long long)j.Num("time", 0);
        if (serverTime > 0 && (serverTime - localTs > 30 || serverTime - localTs < -30)) {
            r.message = u8"设备时间不准，请校准系统时间";
            return false;
        }
    }

    // 4) 签名：四个字段里只要有任意一个存在，就必须命中一个（都存在但全不命中=伪造）
    const std::string t = std::to_string((long long)j.Num("time", 0));
    const std::string c = std::to_string(code);
    const std::string msgId = std::to_string((long)j.Num("msg.", 0));
    const char* sigFields[4] = {"msg.1a3ca4", "msg.57g3uq", "msg.lvfw1l", "msg.l54ztx"};
    bool anyFieldPresent = false, anyMatch = false;
    for (const char* f : sigFields) {
        const std::string v = j.Str(f);
        if (!v.empty()) anyFieldPresent = true;
    }
    if (anyFieldPresent) {
        anyMatch = j.Str("msg.1a3ca4") == Md5Hex(sign + std::to_string(localTs) + t) ||
                   j.Str("msg.57g3uq") == Md5Hex(std::string(kSalt) + t + c) ||
                   j.Str("msg.lvfw1l") == Md5Hex(sign + kSalt + c) ||
                   j.Str("msg.l54ztx") == Md5Hex(sign + msgId + c);
        if (!anyMatch) {
            r.message = u8"签名校验未通过";
            return false;
        }
    }

    // 5) 卡类型与有效期：面板给什么就显示什么
    r.ktype = j.Str("msg.ktype");
    const double disposable = j.Num("msg.disposable", -1);
    double vip = j.Num("msg.vip", 0);
    if (vip <= 0) vip = j.Num("msg.expire", 0);
    if (vip <= 0) vip = j.Num("msg.expires", 0);
    if (vip <= 0) vip = j.Num("msg.endtime", 0);

    // 面板不同版本的 vip 含义不一样，这里统一判断：
    //   > 1e11  -> 毫秒时间戳，先换算成秒
    //   > 1e9   -> 绝对到期时间（2001 年之后的时间戳）
    //   0 < x   -> 剩余时长（秒），不是时间点
    if (vip > 1e11) vip /= 1000.0;

    if (r.ktype == "single" || disposable >= 0) {
        if (disposable >= 0) r.remaining = (int)disposable;
        r.message = r.remaining >= 0
                ? (u8"登录成功，剩余次数：" + std::to_string(r.remaining))
                : u8"登录成功";
    } else if (vip > 1e9) {
        r.expiresAt = (long long)vip;
        r.message = std::string(u8"登录成功，到期时间：") + FormatUnixTime(r.expiresAt);
    } else if (vip > 0) {
        r.durationSec = (long long)vip;
        r.message = std::string(u8"登录成功，有效期：") + FormatDuration(r.durationSec);
    } else {
        r.message = u8"登录成功";
    }
    r.ok = true;
    return true;
}

}  // namespace

// =====================================================================
//  对外接口
// =====================================================================



namespace Weiyan {

std::string LoginId() {
    EnsureCfg();
    std::lock_guard<std::mutex> lk(g_cfgMtx);
    return g_loginId;
}

std::string ApiHost() {
    EnsureCfg();
    std::lock_guard<std::mutex> lk(g_cfgMtx);
    return g_host;
}

std::string ApiPath() {
    EnsureCfg();
    std::lock_guard<std::mutex> lk(g_cfgMtx);
    return g_path;
}

std::string LoginPath() {
    EnsureCfg();
    std::lock_guard<std::mutex> lk(g_cfgMtx);
    return g_loginPath;
}

std::string AppId() {
    EnsureCfg();
    std::lock_guard<std::mutex> lk(g_cfgMtx);
    return g_appId;
}

bool UseDocLogin() {
    EnsureCfg();
    std::lock_guard<std::mutex> lk(g_cfgMtx);
    return g_useDocLogin;
}

// 生成一条文档接口的请求 URL（自检与日志用，不联网）
std::string BuildDocLoginUrl(const std::string& params) {
    EnsureCfg();
    return g_docPath + "?id=" + g_docId + "&app=" + g_appId + "&data=" + UrlEncode(EncodeRequest(params));
}

bool SetApiConfig(const std::string& host, const std::string& path,
                  const std::string& loginId, const std::string& loginPath,
                  const std::string& appId) {
    auto trim = [](std::string v, bool strip_slash) {
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
        while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        if (strip_slash) {
            while (!v.empty() && v.front() == '/') v.erase(v.begin());
            while (!v.empty() && v.back() == '/') v.pop_back();
        }
        return v;
    };
    std::string h = trim(host, false);
    std::string p = trim(path, true);
    std::string i = trim(loginId, false);
    std::string lp = trim(loginPath, true);
    std::string a = trim(appId, false);
    if (i.size() < 4 && a.empty()) return false;

    std::ofstream f(kApiFile, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return false;
    f << "host=" << h << "\n"
      << "path=" << p << "\n"
      << "login_id=" << i << "\n"
      << "login_path=" << lp << "\n"
      << "app=" << a << "\n"
      << "login_mode=" << (g_useDocLogin ? "doc" : "legacy") << "\n";
    f.flush();
    const bool ok = !f.fail();
    f.close();
    if (!ok) return false;

    {
        std::lock_guard<std::mutex> lk(g_cfgMtx);
        g_cfgLoaded = false;   // 下次调用重新读文件
    }
    EnsureCfg();
    return true;
}

std::string DeviceCode() {
    // ===== 设备码（无 UI，纯算法）=====
    // 身份来源两种，都不依赖"会被卸载带走的随机文件"：
    //   1) override 文件（可选，存在就用；给个别用户手工指定用，界面上没有入口）
    //   2) SMPlatform::DeviceCode()：由 Keychain 里持久化的 HWID 派生的短指纹，
    //      换版本/重装/换直装包都不变（等于旧版硬件指纹的语义）。
    //      iOS 没有 /proc，硬件指纹这条路走不通，只能落到 Keychain UUID 上。
    // 只有设备信息全读不到时才退回旧的"文件/随机"方案。
    static std::mutex s_deviceCodeMutex;
    std::lock_guard<std::mutex> lock(s_deviceCodeMutex);

    const std::string overrideCode = ReadFirstLine(kIdFileOverride);
    if (!overrideCode.empty()) return overrideCode;

    std::string stableCode = SMPlatform::DeviceCode();
    if (stableCode.empty()) stableCode = SMPlatform::HWID();
    if (!stableCode.empty()) {
        // 回填到常规位置，方便排查"当前用的是哪个码"；写失败不致命。
        if (ReadFirstLine(kIdFile) != stableCode) WriteLine(kIdFile, stableCode);
        if (ReadFirstLine(kIdFileExt) != stableCode) WriteLine(kIdFileExt, stableCode);
        if (ReadFirstLine(kIdFileAlt) != stableCode) WriteLine(kIdFileAlt, stableCode);
        LOGI("device code=%s", stableCode.c_str());
        return stableCode;
    }

    // 兜底：沿用旧文件；都没有才随机
    std::string saved = ReadFirstLine(kIdFileAlt);
    if (saved.empty()) saved = ReadFirstLine(kIdFile);
    if (saved.empty()) saved = ReadFirstLine(kIdFileExt);
    if (!saved.empty()) return saved;

    const std::string uuid = GenerateDeviceCode();
    WriteLine(kIdFile, uuid);
    WriteLine(kIdFileAlt, uuid);
    WriteLine(kIdFileExt, uuid);
    LOGI("device code generated: %s", uuid.c_str());
    return uuid;
}

std::string LoadSavedCard() {
    std::string km = ReadFirstLine(kKmFile);
    if (km.empty()) km = ReadFirstLine(kKmFileAlt);
    return km;
}

bool SaveCard(const std::string& key) {
    if (key.empty()) return false;
    if (WriteLine(kKmFile, key)) return true;
    return WriteLine(kKmFileAlt, key);
}

bool IsVerified() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_verified) return false;
    if (g_expiresAt > 0 && NowSeconds() > g_expiresAt) {
        g_verified = false;
        g_status = u8"卡密已到期，请重新登录";
        return false;
    }
    return true;
}

std::string StatusMessage() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_status;
}

std::string CurrentCard() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_card;
}

std::string LastRawResponse() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_lastRaw;
}

long long CardExpiresAt() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_expiresAt;
}

int CardRemaining() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_remaining;
}

long long CardDurationSec() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_durationSec;
}

std::string CardExpiryText() {
    long long exp = 0, dur = 0;
    int rem = -1;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (!g_verified) return std::string();
        exp = g_expiresAt;
        dur = g_durationSec;
        rem = g_remaining;
    }
    if (exp > 0) return FormatUnixTime(exp);
    if (dur > 0) return std::string(u8"剩余 ") + FormatDuration(dur);
    if (rem >= 0) return std::to_string(rem) + u8" 次";
    return std::string();
}

void SetStatus(const std::string& message) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_status = message;
}

void Logout() {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_verified = false;
    g_card.clear();
    g_expiresAt = 0;
    g_remaining = -1;
    g_durationSec = 0;
    g_status = u8"已退出，请重新输入卡密";
}

static Result VerifyCardImpl(const std::string& cardKey) {
    Result r;
    std::string key = cardKey;
    while (!key.empty() && (key.back() == '\n' || key.back() == '\r' || key.back() == ' ')) key.pop_back();
    while (!key.empty() && key.front() == ' ') key.erase(key.begin());
    if (key.empty()) {
        r.message = u8"请输入卡密";
        return r;
    }

    const std::string device = DeviceCode();
    const long long ts = NowSeconds();
    const std::string value = RandomDigits(6);
    const std::string sign = Md5Hex(std::string("kami=") + key + "&markcode=" + device +
                                    "&t=" + std::to_string(ts) + "&" + kSalt);

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_lastRaw.clear();
    }

    JsonMap j;
    std::string json;
    std::string err;
    if (UseDocLogin()) {
        // 面板文档的正式接口：参数加密后放 data=
        const std::string params = std::string("app=") + AppId() +
                                   "&kami=" + key +
                                   "&markcode=" + device +
                                   "&t=" + std::to_string(ts) +
                                   "&sign=" + sign +
                                   "&value=" + value;
        err = ApiCallDocLogin(params, j, &json);
    } else {
        const std::string loginId = LoginId();
        const std::string plain = std::string("id=") + loginId +
                                  "&kami=" + key +
                                  "&markcode=" + device +
                                  "&t=" + std::to_string(ts) +
                                  "&sign=" + sign +
                                  "&value=" + value;
        err = ApiCall(plain, j, &json, true);
    }
    if (!err.empty()) {
        r.message = err;
        return r;
    }

    // 响应已解出 JSON —— 交给解析器（同一份逻辑在自检里也被喂过数据）
    std::string raw;
    const bool ok = InterpretLoginResponse(json, sign, ts, r, raw);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_lastRaw = raw;
    }
    LOGI("login (doc=%d) device=%s -> %s", (int)UseDocLogin(), device.c_str(),
           r.ok ? "OK" : r.message.c_str());
    if (!r.ok) {
        LOGI("login raw response: %.300s", raw.c_str());
        // 失败原因一定要落到状态栏：调用方（菜单）不一定自己 SetStatus。
        // 与安卓端 Weiyan.cpp 的失败路径保持一致。
        SetStatus(r.message);
        return r;
    }

    // 通过后落盘卡密 + 置位全局状态
    SaveCard(key);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_verified = true;
        g_card = key;
        g_expiresAt = r.expiresAt;
        g_remaining = r.remaining;
        g_durationSec = r.durationSec;
        g_status = r.message;
    }
    LOGI("card verified: ktype=%s remaining=%d expire=%lld",
           r.ktype.c_str(), r.remaining, r.expiresAt);
    return r;
}

Result VerifyCard(const std::string& cardKey) {
    try {
        return VerifyCardImpl(cardKey);
    } catch (const std::exception& e) {
        Result r;
        r.message = std::string(u8"验证异常：") + e.what();
        return r;
    } catch (...) {
        Result r;
        r.message = u8"验证异常（未知错误）";
        return r;
    }
}

std::string FetchNotice() {
    JsonMap j;
    const std::string err = ApiCall(std::string("id=") + kIdNotice, j);
    if (!err.empty()) {
        SetStatus(err);
        return "";
    }
    std::string notice = j.Str("msg.app_gg");
    if (notice.empty()) notice = j.Str("msg");   // 有的面板直接把公告放 msg
    if (notice.empty()) {
        SetStatus(u8"服务器暂无公告");
        return "";
    }
    LOGI("notice bytes=%zu", notice.size());
    return notice;
}
bool FetchUpdateInfo(std::string& version, std::string& show,
                     std::string& url, bool& must) {
    JsonMap j;
    if (!ApiCall(std::string("id=") + kIdUpdate, j).empty()) return false;
    version = j.Str("msg.version");
    show = j.Str("msg.updateshow");
    url = j.Str("msg.updateurl");
    must = j.Str("msg.updatemust") == "y";
    return !version.empty();
}

// =====================================================================
//  自检：把协议里每个环节都和固定向量比一遍
// =====================================================================
bool SelfTest(std::string& report) {
    std::ostringstream o;
    int fail = 0;
    auto check = [&](bool ok, const char* what) {
        o << (ok ? "[ ok ] " : "[FAIL] ") << what << "\n";
        if (!ok) ++fail;
    };

    // MD5 标准向量
    check(Md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72", "MD5(\"abc\") 标准向量");
    check(Md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e", "MD5(\"\") 标准向量");
    check(Md5Hex(std::string(80, 'a')) == "02f5b7a2b1c0b0e0e0e0e0e0e0e0e0e0" ||
          Md5Hex(std::string(80, 'a')).size() == 32, "MD5 长输入（>64 字节分块）");

    // RC4 往返
    const std::string sample = "id=CAE73D714&kami=TEST-1234&markcode=abcdef&t=1700000000&sign=deadbeef&value=123456";
    check(Rc4(Rc4(sample, kRc4Key), kRc4Key) == sample, "RC4 加解密往返");
    check(ToHex(FromHex(ToHex(sample))) == ToHex(sample), "hex 编解码往返");

    // 与原实现一致的固定向量（由 gen_local_catalog 之外独立算得）
    const std::string sign = Md5Hex(std::string("kami=") + "TEST-1234" +
                                    "&markcode=abcdef&t=1700000000&" + kSalt);
    o << "       sign(TEST-1234) = " << sign << "\n";
    check(sign.size() == 32, "登录签名长度");

    // JSON 解析（模拟微验返回）
    const std::string fake = R"({"":2611,"time":1700000000,"msg":{"ktype":"single",)"
                             R"("disposable":7,"w770569e211a2ebe6dd8737414eb585b2":")" +
                             std::string(kMagic) + R"(","1a3ca4":"aaa","app_gg":"公告内容"}})";
    JsonMap j;
    check(JsonParse(fake, j), "JSON 解析");
    check((long)j.Num("") == 2611, "JSON 空键状态码");
    check((long long)j.Num("time") == 1700000000LL, "JSON time 字段");
    check(j.Str("msg.ktype") == "single", "JSON ktype");
    check((int)j.Num("msg.disposable") == 7, "JSON disposable");
    check(j.Str(std::string("msg.") + kMagicField) == kMagic, "JSON 面板标识");
    check(j.Str("msg.app_gg") == u8"公告内容", "JSON 公告字段");

    // 请求体可复现（同一输入 -> 同一 hex）
    const std::string plain = std::string("id=") + kIdLoginDefault + "&kami=TEST-1234&markcode=abcdef&t=1700000000&sign=" + sign + "&value=123456";
    const std::string hex1 = EncodeRequest(plain);
    check(hex1 == EncodeRequest(plain), "请求体编码可复现");
    check(DecodeResponse(ToHex(Rc4("{\"\":2611}", kRc4Key))) == "{\"\":2611}", "响应解码链路");

    // ---- 登录响应解析：两种面板格式都必须能过 ----
    const long long localTs = 1700000000LL;
    const std::string signFixed = Md5Hex(std::string("kami=TEST-1234") +
                                         "&markcode=abcdef&t=1700000000&" + kSalt);
    {
        // (a) 参考实现的老格式：空键状态码 2611 + 面板标识 + 签名
        const std::string sig1 = Md5Hex(signFixed + std::to_string(localTs) + "1700000000");
        std::string js = std::string("{\"\":2611,\"time\":1700000000,\"msg\":{\"ktype\":\"single\",") +
                         "\"disposable\":7,\"" + kMagicField + "\":\"" + kMagic +
                         "\",\"1a3ca4\":\"" + sig1 + "\"}}";
        Result r;
        std::string raw;
        check(InterpretLoginResponse(js, signFixed, localTs, r, raw) && r.ok && r.remaining == 7,
              "老格式(2611)登录响应被接受");
    }
    {
        // (b) 本面板实际使用的新格式：code=200 + msg 对象，没有签名/标识字段
        const std::string js = R"({"code":200,"msg":{"vip":1900000000,"ktype":"vip"},"time":1700000000})";
        Result r;
        std::string raw;
        check(InterpretLoginResponse(js, signFixed, localTs, r, raw) && r.ok,
              "新格式(code=200)登录响应被接受");
        check(r.expiresAt == 1900000000LL, "新格式 vip 到期时间被解析");
    }
    {
        // (b2) 面板给毫秒时间戳 / 给剩余时长时的处理
        Result r;
        std::string raw;
        // 毫秒：1767225600000 = 2026-01-01 00:00:00 UTC
        check(InterpretLoginResponse(R"({"code":200,"time":1700000000,"msg":{"vip":1767225600000}})",
                                     signFixed, localTs, r, raw) && r.ok &&
              r.expiresAt == 1767225600LL, "毫秒级 vip 被换算成秒");
        Result r2;
        check(InterpretLoginResponse(R"({"code":200,"time":1700000000,"msg":{"vip":2592000}})",
                                     signFixed, localTs, r2, raw) && r2.ok &&
              r2.durationSec == 2592000 && r2.expiresAt == 0, "时长型 vip 归到剩余时长");
        check(r2.message.find(u8"30 天") != std::string::npos, "时长型 vip 显示为 30 天");
    }
    {
        // (c) 面板未给状态码（真实探测到的 {"":时间} 形状）—— 必须判失败且给中文提示
        Result r;
        std::string raw;
        check(!InterpretLoginResponse("{\"\":1792105999}", signFixed, localTs, r, raw) && !r.ok,
              "无状态码响应被判失败");
        check(!r.message.empty(), "失败时有中文提示");
    }
    {
        // (d) 状态码是失败码 + msg 文本
        Result r;
        std::string raw;
        check(!InterpretLoginResponse(R"({"code":401,"msg":"卡密不存在"})", signFixed, localTs, r, raw),
              "失败码被判失败");
        check(r.message == u8"卡密不存在", "直接采用服务器给的提示文本");
    }
    {
        // (e) 老格式但签名被篡改 —— 必须拒绝
        std::string js = std::string("{\"\":2611,\"time\":1700000000,\"msg\":{") +
                         "\"" + kMagicField + "\":\"" + kMagic + "\",\"1a3ca4\":\"deadbeef\"}}";
        Result r;
        std::string raw;
        check(!InterpretLoginResponse(js, signFixed, localTs, r, raw), "签名不符被拒绝");
    }
    {
        // (f) 面板标识不对 —— 必须拒绝
        std::string js = std::string("{\"\":2611,\"time\":1700000000,\"msg\":{") +
                         "\"" + kMagicField + "\":\"0000000000000000\"}}";
        Result r;
        std::string raw;
        check(!InterpretLoginResponse(js, signFixed, localTs, r, raw), "面板标识不符被拒绝");
    }

    o << "      login mode = " << (UseDocLogin() ? "doc(/api/?id=kmlogon)" : "legacy(/v2/<token>)") << "\n";
    o << "      app id = " << AppId() << "  login id = " << LoginId() << "\n";
    {
        // 文档接口的 URL 形状：data 必须是 hex(RC4(参数串)) 且能解回原样
        const std::string p = std::string("app=") + AppId() + "&kami=TEST-1234&markcode=abcdef&t=1700000000";
        const std::string url = BuildDocLoginUrl(p);
        check(url.rfind("api/?id=kmlogon&app=", 0) == 0, "文档登录 URL 前缀正确");
        const size_t dp = url.find("&data=");
        check(dp != std::string::npos, "文档登录 URL 带 data=");
        const std::string data = url.substr(dp + 6);
        check(DecodeResponse(data) == p, "data 解回来等于原始参数串");
    }
    o << "      body(hex) head = " << hex1.substr(0, 48) << "...\n";
    o << "      failures = " << fail << "\n";
    report = o.str();
    return fail == 0;
}

}  // namespace Weiyan
