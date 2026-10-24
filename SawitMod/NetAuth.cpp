#include "NetAuth.h"
#include "Global.h"
#include "AndroidBrandConfig.h"
#include "Includes/obfuscate.h"
#include "VirboxTargets.h"
#include "ClientControlAgent.h"
#include "PayloadVM.h"
#include "ModUtils.h"
#include "LocalCatalog.h"
#include "Weiyan.h"

// ---- iOS 平台能力（替代 Android 侧的 JNI/Java 调用）----
//   App/Platform.h   -> HWID / 进程退出
//   App/HttpClient.h -> 同步 HTTP POST（替代 HttpURLConnection）
//   Core/Crypto.h    -> SHA-256 / HMAC / ECDSA P-256（替代运行时解析系统加密库）
#include "App/Platform.h"
#include "App/HttpClient.h"
#include "Core/Crypto.h"

#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstdint>
#include <fcntl.h>

// =========================================================
//  KONFIGURASI
// =========================================================
// MIGRASI KE TANDA TANGAN ASIMETRIS (ECDSA P-256), sama seperti versi PC.
// Client HANYA memegang PUBLIC KEY (aman di-embed walau binary dibongkar);
// PRIVATE KEY hanya ada di server, jadi respons tidak bisa dipalsukan.
// PUBKEY_XY_HEX = koordinat X||Y mentah (64 byte = 128 hex). WAJIB identik
// dengan public key di versi PC (NetAuth.h) karena server-nya sama.
#define PUBKEY_XY_HEX ((const char*)OBFUSCATE("05f6fe9d05a7396e71a2bcb61d1df78dc168c3c888756b0bb1666a441697108ae5b1f9cb52e85cb18ab731dca7c57ea92731952cf06814876c7011da7aeaaac1"))

// Versi format baris tanda tangan. HARUS sama dengan SIG_VERSION di server.
#define SIG_VERSION   ((const char*)OBFUSCATE("v4"))

// TLS aktif (port 443). Host utama sama dengan versi PC.
#define API_BASE      ((const char*)OBFUSCATE("https://api.arsipharian.com"))
//#define API_BASE      ((const char*)OBFUSCATE("https://test.arsipharian.com"))
#define ALOG(...) ((void)0)

// =========================================================
//  UTILITAS JSON (khusus field otentikasi)
// =========================================================
// Sengaja tidak memakai extractJsonString untuk field kritis. Field yang
// menentukan hidup-mati sesi tidak boleh bergantung pada parser yang
// mudah tertipu substring.
static std::string JsonGetString(const std::string& j, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t k = j.find(pat);
    if (k == std::string::npos) return "";
    size_t c = j.find(':', k + pat.size());
    if (c == std::string::npos) return "";
    size_t q1 = j.find('"', c);
    if (q1 == std::string::npos) return "";
    std::string out;
    for (size_t i = q1 + 1; i < j.size(); ++i) {
        if (j[i] == '\\' && i + 1 < j.size()) { out.push_back(j[i + 1]); ++i; continue; }
        if (j[i] == '"') break;
        out.push_back(j[i]);
    }
    return out;
}

static bool JsonGetBool(const std::string& j, const std::string& key, bool def) {
    std::string pat = "\"" + key + "\"";
    size_t k = j.find(pat);
    if (k == std::string::npos) return def;
    size_t c = j.find(':', k + pat.size());
    if (c == std::string::npos) return def;
    size_t p = j.find_first_not_of(" \t\r\n", c + 1);
    if (p == std::string::npos) return def;
    if (j.compare(p, 4, "true") == 0)  return true;
    if (j.compare(p, 5, "false") == 0) return false;
    return def;
}

static std::string JsonGetNumberRaw(const std::string& j, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t k = j.find(pat);
    if (k == std::string::npos) return "";
    size_t c = j.find(':', k + pat.size());
    if (c == std::string::npos) return "";
    size_t p = j.find_first_not_of(" \t\r\n", c + 1);
    if (p == std::string::npos) return "";
    size_t e = p;
    while (e < j.size() && (isdigit((unsigned char)j[e]) || j[e] == '-' || j[e] == '.')) ++e;
    return j.substr(p, e - p);
}

static int64_t JsonGetInt64(const std::string& j, const std::string& key, int64_t def = 0) {
    std::string raw = JsonGetNumberRaw(j, key);
    if (raw.empty()) return def;
    try { return std::stoll(raw); } catch (...) { return def; }
}

static int JsonHex4(const std::string& text, size_t pos) {
    if (pos + 4 > text.size()) return -1;
    int value = 0;
    for (size_t i = 0; i < 4; ++i) {
        char c = text[pos + i];
        int digit = c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return -1;
        value = (value << 4) | digit;
    }
    return value;
}

static void AppendUtf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) out.push_back((char)cp);
    else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

static bool JsonGetEscapedString(const std::string& json, const std::string& key, std::string& out) {
    const std::string marker = "\"" + key + "\":";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos + marker.size());
    if (pos == std::string::npos) return false;
    ++pos;
    out.clear();
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') return true;
        if (c != '\\') { out.push_back(c); continue; }
        if (pos >= json.size()) return false;
        char esc = json[pos++];
        if (esc == 'n') out.push_back('\n');
        else if (esc == 'r') out.push_back('\r');
        else if (esc == 't') out.push_back('\t');
        else if (esc == 'b') out.push_back('\b');
        else if (esc == 'f') out.push_back('\f');
        else if (esc == '"' || esc == '\\' || esc == '/') out.push_back(esc);
        else if (esc == 'u') {
            int first = JsonHex4(json, pos);
            if (first < 0) return false;
            pos += 4;
            unsigned int cp = (unsigned int)first;
            if (first >= 0xD800 && first <= 0xDBFF && pos + 6 <= json.size() &&
                json[pos] == '\\' && json[pos + 1] == 'u') {
                int second = JsonHex4(json, pos + 2);
                if (second >= 0xDC00 && second <= 0xDFFF) {
                    cp = 0x10000u + (((unsigned int)first - 0xD800u) << 10) +
                         ((unsigned int)second - 0xDC00u);
                    pos += 6;
                }
            }
            AppendUtf8(out, cp);
        } else return false;
    }
    return false;
}

static void ParseBroadcastMessages(const std::string& json) {
    std::vector<BroadcastMessage> parsed;
    const std::string marker = "\"messages\":[";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) {
        std::lock_guard<std::mutex> lock(g_broadcastMutex);
        g_broadcastMessages.clear();
        g_lastSeenBroadcastId = 0;
        return;
    }
    pos += marker.size();
    bool inString = false, escaped = false;
    int depth = 0;
    size_t start = std::string::npos;
    for (; pos < json.size(); ++pos) {
        char c = json[pos];
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == ']' && depth == 0) break;
        if (c == '{') { if (depth++ == 0) start = pos; }
        else if (c == '}' && depth > 0 && --depth == 0 && start != std::string::npos) {
            std::string item = json.substr(start, pos - start + 1);
            BroadcastMessage message;
            message.id = JsonGetInt64(item, "id", 0);
            JsonGetEscapedString(item, "title", message.title);
            JsonGetEscapedString(item, "contentEn", message.contentEn);
            JsonGetEscapedString(item, "contentZh", message.contentZh);
            JsonGetEscapedString(item, "createdAt", message.createdAt);
            JsonGetEscapedString(item, "updatedAt", message.updatedAt);
            if (message.id > 0 && !message.title.empty()) parsed.push_back(std::move(message));
            start = std::string::npos;
        }
    }
    std::sort(parsed.begin(), parsed.end(), [](const BroadcastMessage& a, const BroadcastMessage& b) {
        return a.id > b.id;
    });
    std::lock_guard<std::mutex> lock(g_broadcastMutex);
    g_broadcastMessages.swap(parsed);
    g_lastSeenBroadcastId = g_broadcastMessages.empty() ? 0 : g_broadcastMessages.front().id;
}

static std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) continue;
                out.push_back(c);
        }
    }
    return out;
}

struct KonteksSesiAndroid {
    StatusSesi status = StatusSesi::Keluar;
    std::string idSesi;
    std::string tokenSesi;
    uint64_t sessionKeyDigest = 0;
    int64_t berlakuSampai = 0;
    uint64_t generasi = 0;
    bool modeAvatar = false;
    bool lisensiPermanen = false;
    std::string tanggalKedaluwarsa;
};

static KonteksSesiAndroid g_sesi;
static std::mutex g_sesiMtx;
static std::mutex g_permintaanSesiMtx;

static int64_t WaktuSekarangDetik() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

static void HapusAman(std::string& nilai) {
    if (!nilai.empty()) {
        volatile char* p = &nilai[0];
        for (size_t i = 0; i < nilai.size(); ++i) p[i] = 0;
    }
    nilai.clear();
}

// 离线模式的会话指纹，与 PasangSesi 安装的 g_sesi.sessionKeyDigest 配对。只有
// HashTokenDigest 在离线模式下返回它，SesiAktif / ValidasiOtentikasiKetat /
// GenerasiSesi / LisensiPermanen 才会一致地放行 —— 这个构建压根不请求服务器，
// 见 Version.h 的 SAWIT_OFFLINE_MODE。
#define SAWIT_OFFLINE_SESSION_DIGEST 0x5A17C0DE5A17C0DEULL

static OLLVM_SEC_CRITICAL uint64_t HashTokenDigest(const std::string& token, const std::string& id, int64_t expiry) {
#if SAWIT_OFFLINE_MODE
    // 本地会话没有服务器签发的 token，指纹由这个常量顶替。闸门函数（SesiAktif
    // 等）与 PasangSesi 都走这一个函数，因此它们看到的是同一个值。
    (void)token; (void)id; (void)expiry;
    return SAWIT_OFFLINE_SESSION_DIGEST;
#else
    if (token.size() != 64 || id.empty()) return 0;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : token) {
        h ^= (uint64_t)(unsigned char)c;
        h *= 0x100000001b3ULL;
    }
    for (char c : id) {
        h ^= (uint64_t)(unsigned char)c;
        h *= 0x100000001b3ULL;
    }
    h ^= (uint64_t)expiry;
    h *= 0x100000001b3ULL;
    return h ^ 0xA55A5AA5C33CA55AULL;
#endif
}

static std::atomic<uint64_t> g_runtimeSessionProof{0};

extern "C" VIRBOX_TARGET_ATTR bool Virbox_SessionProofCheck(
        size_t idLength, size_t tokenLength, int64_t berlakuSampai, int64_t sekarang) {
    return idLength != 0 && tokenLength == 64 && berlakuSampai > sekarang;
}

static inline bool IsFunctionTampered(const void* fn) {
    if (!fn) return true;
    const volatile uint32_t* p = reinterpret_cast<const volatile uint32_t*>(fn);
    uint32_t w0 = p[0];
    uint32_t w1 = p[1];
    if (w0 == 0xd65f03c0) return true; // immediate RET
    if (w0 == 0x52800020 || w0 == 0xd2800020 || w0 == 0x52800000 || w0 == 0xd2800000) return true; // MOV W0/X0, #1 or #0
    if ((w0 & 0xffe00000) == 0x52800000 && w1 == 0xd65f03c0) return true; // MOV Wn, #imm; RET
    if ((w0 & 0xffe00000) == 0xd2800000 && w1 == 0xd65f03c0) return true; // MOV Xn, #imm; RET
    if (w0 == 0x58000050 && w1 == 0xd61f0200) return true; // LDR X16, #8; BR X16 (inline hook jump)
    if (w0 == 0x58000051 && w1 == 0xd61f0220) return true; // LDR X17, #8; BR X17
    return false;
}

static OLLVM_SEC_CRITICAL bool AntiHookCheck();
static OLLVM_SEC_CRITICAL bool AntiAnalysisSecurityCheck();
extern "C" VIRBOX_TARGET_ATTR AuthResult Virbox_DoVerify(const std::string& license);
OLLVM_SEC_CRITICAL void VerifyLicenseJNI(const char* licenseKey);

static inline bool ValidateCodePrologues() {
    if (IsFunctionTampered((const void*)&Virbox_SessionProofCheck) ||
        IsFunctionTampered((const void*)&AntiHookCheck) ||
        IsFunctionTampered((const void*)&AntiAnalysisSecurityCheck) ||
        IsFunctionTampered((const void*)&SesiAktif) ||
        IsFunctionTampered((const void*)&GenerasiSesi) ||
        IsFunctionTampered((const void*)&LisensiPermanen) ||
        IsFunctionTampered((const void*)&ValidasiOtentikasiKetat) ||
        IsFunctionTampered((const void*)&VerifyLicenseJNI) ||
        IsFunctionTampered((const void*)&Virbox_DoVerify)) {
        return false;
    }
    return true;
}

static OLLVM_SEC_CRITICAL bool AntiAnalysisSecurityCheck() {
    if (!ValidateCodePrologues()) return false;
    static std::atomic<int64_t> lastCheckSec{0};
    static std::atomic<bool> lastResult{true};
    int64_t now = WaktuSekarangDetik();
    if (now - lastCheckSec.load(std::memory_order_relaxed) < 3) {
        return lastResult.load(std::memory_order_relaxed);
    }
    lastCheckSec.store(now, std::memory_order_relaxed);

    int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        char buf[512];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            const char* tracer = strstr(buf, "TracerPid:\t");
            if (tracer) {
                int pid = atoi(tracer + 11);
                if (pid > 0) {
                    lastResult.store(false, std::memory_order_relaxed);
                    return false;
                }
            }
        }
    }

    FILE* fp = fopen("/proc/self/maps", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "frida-agent") || strstr(line, "frida-gadget") ||
                strstr(line, "libgadget.so") || strstr(line, "gdbserver")) {
                fclose(fp);
                lastResult.store(false, std::memory_order_relaxed);
                return false;
            }
        }
        fclose(fp);
    }

    lastResult.store(true, std::memory_order_relaxed);
    return true;
}

static OLLVM_SEC_CRITICAL bool AntiHookCheck() {
    if (!ValidateCodePrologues()) {
        return false;
    }
    return true;
}

OLLVM_SEC_CRITICAL bool SesiAktif() {
    if (!ValidateCodePrologues() || !AntiHookCheck() || !AntiAnalysisSecurityCheck()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_sesiMtx);
    if (g_sesi.status != StatusSesi::Aktif || g_sesi.idSesi.empty() ||
        g_sesi.tokenSesi.size() != 64 || g_sesi.berlakuSampai <= WaktuSekarangDetik()) {
        return false;
    }
    uint64_t expectedDigest = HashTokenDigest(g_sesi.tokenSesi, g_sesi.idSesi, g_sesi.berlakuSampai);
    if (g_sesi.sessionKeyDigest == 0 || g_sesi.sessionKeyDigest != expectedDigest) {
        return false;
    }
    if (g_runtimeSessionProof.load(std::memory_order_acquire) != (expectedDigest ^ 0x6A09E667F3BCC908ULL)) {
        return false;
    }
    return true;
}

uint64_t GenerasiSesi() {
    if (!ValidateCodePrologues()) return 0;
    std::lock_guard<std::mutex> lock(g_sesiMtx);
    if (g_sesi.status != StatusSesi::Aktif || g_sesi.sessionKeyDigest == 0) return 0;
    uint64_t expectedDigest = HashTokenDigest(g_sesi.tokenSesi, g_sesi.idSesi, g_sesi.berlakuSampai);
    if (g_sesi.sessionKeyDigest != expectedDigest) return 0;
    if (g_runtimeSessionProof.load(std::memory_order_acquire) != (expectedDigest ^ 0x6A09E667F3BCC908ULL)) return 0;
    return g_sesi.generasi;
}

bool LisensiPermanen() {
    if (!ValidateCodePrologues()) return false;
    std::lock_guard<std::mutex> lock(g_sesiMtx);
    if (g_sesi.status != StatusSesi::Aktif || g_sesi.sessionKeyDigest == 0) return false;
    uint64_t expectedDigest = HashTokenDigest(g_sesi.tokenSesi, g_sesi.idSesi, g_sesi.berlakuSampai);
    if (g_sesi.sessionKeyDigest != expectedDigest) return false;
    if (g_runtimeSessionProof.load(std::memory_order_acquire) != (expectedDigest ^ 0x6A09E667F3BCC908ULL)) return false;
    return g_sesi.lisensiPermanen;
}

bool ValidasiOtentikasiKetat() {
    if (!ValidateCodePrologues()) return false;
    std::lock_guard<std::mutex> lock(g_sesiMtx);
    if (g_sesi.status != StatusSesi::Aktif || g_sesi.idSesi.empty() || g_sesi.tokenSesi.size() != 64) {
        return false;
    }
    uint64_t expectedDigest = HashTokenDigest(g_sesi.tokenSesi, g_sesi.idSesi, g_sesi.berlakuSampai);
    if (g_sesi.sessionKeyDigest == 0 || g_sesi.sessionKeyDigest != expectedDigest) {
        return false;
    }
    return g_runtimeSessionProof.load(std::memory_order_acquire) == (expectedDigest ^ 0x6A09E667F3BCC908ULL);
}

std::string TanggalKedaluwarsaLisensi() {
    std::lock_guard<std::mutex> lock(g_sesiMtx);
    return g_sesi.tanggalKedaluwarsa;
}

static bool SalinBuktiSesi(std::string& idSesi, std::string& tokenSesi, uint64_t* generasi = nullptr) {
    std::lock_guard<std::mutex> lock(g_sesiMtx);
    if (g_sesi.status != StatusSesi::Aktif || g_sesi.berlakuSampai <= WaktuSekarangDetik())
        return false;
    idSesi = g_sesi.idSesi;
    tokenSesi = g_sesi.tokenSesi;
    if (generasi) *generasi = g_sesi.generasi;
    return !idSesi.empty() && !tokenSesi.empty();
}

OLLVM_SEC_CRITICAL bool PasangSesi(const std::string& jsonSesi, bool modeAvatar) {
#if SAWIT_OFFLINE_MODE
    // 离线闸：不上服务器、不认卡密，直接装一个本地会话。token 是 64 个字符的
    // 占位串，既能过长度的校验，又不会冒充任何真实签发过的会话；指纹由
    // HashTokenDigest 的离线分支给出，因此 SesiAktif 等闸门一致放行。
    // 有效期取 64 位时间戳上限，避免任何"过期"判断误伤。
    (void)jsonSesi;
    {
        std::lock_guard<std::mutex> lock(g_sesiMtx);
        g_sesi.idSesi          = "OFFLINE-LOCAL-SESSION";
        g_sesi.tokenSesi       = std::string(64, '0');
        g_sesi.sessionKeyDigest = HashTokenDigest(g_sesi.tokenSesi, g_sesi.idSesi, 0);
        g_sesi.berlakuSampai   = 0x7FFFFFFFFFFFFFFFLL;
        g_sesi.status          = StatusSesi::Aktif;
        g_sesi.modeAvatar      = modeAvatar;
        g_sesi.lisensiPermanen = true;
        g_sesi.tanggalKedaluwarsa = "offline build";
        ++g_sesi.generasi;
        g_runtimeSessionProof.store(g_sesi.sessionKeyDigest ^ 0x6A09E667F3BCC908ULL,
                                    std::memory_order_release);
    }
    return true;
#else
    std::string id = JsonGetString(jsonSesi, (const char*)OBFUSCATE("idSesi"));
    std::string token = JsonGetString(jsonSesi, (const char*)OBFUSCATE("tokenSesi"));
    std::string batas = JsonGetNumberRaw(jsonSesi, (const char*)OBFUSCATE("berlakuSampai"));
    const bool permanen = JsonGetBool(jsonSesi, (const char*)OBFUSCATE("isPermanent"), false);
    const std::string expiresAt = JsonGetString(jsonSesi, (const char*)OBFUSCATE("expiresAt"));
    int64_t sampai = 0;
    try { sampai = std::stoll(batas); } catch (...) { sampai = 0; }
    if (!Virbox_SessionProofCheck(id.size(), token.size(), sampai, WaktuSekarangDetik()))
        return false;

    std::lock_guard<std::mutex> lock(g_sesiMtx);
    const bool sesiBaru = g_sesi.idSesi != id || g_sesi.status != StatusSesi::Aktif;
    HapusAman(g_sesi.tokenSesi);
    g_sesi.idSesi = id;
    g_sesi.tokenSesi = token;
    g_sesi.sessionKeyDigest = HashTokenDigest(token, id, sampai);
    g_runtimeSessionProof.store(g_sesi.sessionKeyDigest ^ 0x6A09E667F3BCC908ULL, std::memory_order_release);
    g_sesi.berlakuSampai = sampai;
    g_sesi.status = StatusSesi::Aktif;
    g_sesi.modeAvatar = modeAvatar;
    g_sesi.lisensiPermanen = permanen;
    g_sesi.tanggalKedaluwarsa = expiresAt;
    if (sesiBaru) ++g_sesi.generasi;
    return true;
#endif
}

// 离线闸入口：不需要卡密、不需要联网，直接把本地会话装好。ModUtils.cpp 的
// LoadLicenseLocally() 在离线构建里启动即调用它，所以玩家一进游戏菜单就是
// 已登录状态。SAWIT_OFFLINE_MODE=0 时它走正常的 PasangSesi（要求真实目录），
// 因此不会成为绕过服务器校验的后门——离线构建本来就不请求服务器。
bool LoginOfflineSession(bool modeAvatar) {
    if (!PasangSesi(std::string(), modeAvatar)) return false;
#if SAWIT_LOCAL_FEATURES
    // 会话装好之后紧接着装功能目录：没有这一步 WireForOp(OP_*) 全是空串，
    // 菜单里的功能按钮点了不会有任何请求发出去。
    if (!LocalCatalog::InstallEmbedded()) {
        ALOG("LoginOfflineSession: katalog offline gagal dipasang");
    } else {
        LocalCatalog::SelfTest(LocalCatalog::CatalogSource());
    }
#endif
    return true;
}

extern "C" VIRBOX_TARGET_ATTR void Virbox_WipeAllFeatures() {
    g_radarEnabled = false;
    g_autoCollectBubbleActive = false;
    g_vehicleSwapSuccess = false;
    g_speedToggle = false;
    g_speedActive = false;
    g_globalSpeedToggle = false;
    g_jumpHackActive = false;
    g_isGardenBotRunning = false;
    g_isPuzzleBotRunning = false;
    g_autoCookingActive = false;
    g_isChefActive = false;
    g_isHelperCookActive = false;
    g_isFailedCookActive = false;
    g_autoFarmActive = false;
    g_instantFarmingActive = false;
    g_legacyInstantFarmingActive = false;
    g_instantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
    g_legacyInstantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
    g_autoCleanActive = false;
    g_autoClearTaskActive = false;
    g_autoFishingActive = false;
    g_autoFishingGeneration.fetch_add(1, std::memory_order_acq_rel);
    g_autoLureBallActive = false;
    g_lureBallLoopGeneration.fetch_add(1, std::memory_order_acq_rel);
    g_lureBallBusy.store(false, std::memory_order_release);
    g_lureBallCooldownUntilMs.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_lureBallMutex);
        g_lureBallStatus = u8"就绪";
        g_lureBallCount = 0;
    }
    g_autoFishBaitActive = false;
    g_fishBaitLoopGeneration.fetch_add(1, std::memory_order_acq_rel);
    g_fishBaitBusy.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_fishBaitMutex);
        g_fishBaitStatus = u8"就绪";
        g_fishBaitStatusZh = u8"\u5c31\u7eea";
        g_fishBaitEmpty = false;
        g_fishBaitCount = 0;
        g_fishBaitCurrentStackCount = 0;
        g_fishBaitStackCount = 0;
    }
    g_autoEatActive = false;
    g_enableBirdFarm = false;
    g_birdRequestPending.store(false, std::memory_order_release);
    g_birdCurrentTarget = {};
    g_birdRetryCount = 0;
    g_birdFarmStatus = u8"待机";
    g_processedBirds.clear();
    g_swimHackActive = false;
    g_skateHackActive = false;
    g_cookPn.clear();
    g_puzzlePn.clear();
    g_drawPn.clear();
    g_musicPn.clear();
    g_picturePn.clear();
    g_avatarPn.clear();
    featureList.clear();
    shopNames.clear();
    shopIds.clear();
    {
        std::lock_guard<std::mutex> lock(invokeMutex);
        invokeQueue.clear();
    }
}

void AkhiriSesiLokal(StatusSesi status) {
    g_runtimeSessionProof.store(0, std::memory_order_release);
    ClientControlAgent::ClearModSession();
    {
        std::lock_guard<std::mutex> lock(g_sesiMtx);
        HapusAman(g_sesi.tokenSesi);
        HapusAman(g_sesi.idSesi);
        g_sesi.sessionKeyDigest = 0;
        g_sesi.berlakuSampai = 0;
        g_sesi.modeAvatar = false;
        g_sesi.lisensiPermanen = false;
        HapusAman(g_sesi.tanggalKedaluwarsa);
        g_sesi.status = status;
        ++g_sesi.generasi;
    }
    Virbox_WipeAllFeatures();
    {
        std::lock_guard<std::mutex> messageLock(g_broadcastMutex);
        g_broadcastMessages.clear();
        g_lastSeenBroadcastId = 0;
    }
}

// (ConstantTimeEquals dihapus: verifikasi HMAC diganti ECDSA P-256.)

// =========================================================
//  NONCE
// =========================================================
// Kode lama hanya 8 digit hex. Kolom nonce di server UNIQUE, jadi dengan
// 8 digit tabrakan akan terjadi dan user sah tertolak REPLAY_DETECTED.
// 32 digit hex, sama seperti versi Windows.
std::string GenerateNonceNative() {
    static std::mt19937_64 gen(
        (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count() ^
        (uint64_t)std::random_device{}());
    std::stringstream ss;
    for (int i = 0; i < 2; ++i)
        ss << std::hex << std::setw(16) << std::setfill('0') << gen();
    return ss.str();
}

// =========================================================
//  HWID
// =========================================================
// iOS：设备码来自 Keychain 里持久化的 UUID（SMPlatform::HWID），不再走 JNI。
// 服务端把 HWID 当设备绑定用，所以必须稳定；空串时回落到占位符，与原来一致。
std::string GetHWIDNative() {
    std::string hwidStr = SMPlatform::HWID();
    if (hwidStr.empty()) hwidStr = "UNKNOWN_HWID";
    return hwidStr;
}

// =========================================================
//  HMAC-SHA256（iOS：Core/Crypto.h 的纯 C++ 实现）
// =========================================================
std::string HmacSha256HexNative(const std::string& key, const std::string& data) {
    return SMCrypto::HmacSha256Hex(key, data);
}

// =========================================================
//  HTTP POST
// =========================================================
std::string HttpPostNative(const std::string& urlStr, const std::string& jsonPayload) {
    // iOS：同步 POST，连接/读取各 8 秒超时；非 2xx 也把 body 读回来（服务器在
    // 401/403 里同样下发带签名的 JSON，那才是 LICENSE_EXPIRED 的载体）。
    // 网络错误返回空串，调用方据此走 netError 分支。签名保持不变：
    // ModUtils.cpp / 其它模块直接调用 HttpPostNative。
    return SMHttp::PostJson(urlStr, jsonPayload, 8);
}

// =========================================================
//  PESAN UNTUK USER
// =========================================================
std::string AuthDisplayMessage(const AuthResult& r) {
    const std::string& c = r.code;
    const bool zh = (g_language == LANG_CN);
    if (c == (const char*)OBFUSCATE("VERSION_UNSUPPORTED")) return zh ? u8"有新版本可用，请先更新" : (const char*)OBFUSCATE("Update Available, please update first");
    if (c == (const char*)OBFUSCATE("VERSION_INVALID"))     return zh ? u8"未知的客户端版本" : (const char*)OBFUSCATE("Unknown client version");
    if (c == (const char*)OBFUSCATE("LICENSE_EXPIRED"))     return zh ? u8"许可证已过期" : (const char*)OBFUSCATE("License expired");
    if (c == (const char*)OBFUSCATE("LICENSE_NOT_FOUND"))   return zh ? u8"许可证未注册" : (const char*)OBFUSCATE("License Not Registered");
    if (c == (const char*)OBFUSCATE("LICENSE_DISABLED"))    return zh ? u8"许可证已禁用" : (const char*)OBFUSCATE("License disabled");
    if (c == (const char*)OBFUSCATE("DEVICE_MISMATCH"))     return zh ? u8"设备未授权" : (const char*)OBFUSCATE("Device not authorized");
    if (c == (const char*)OBFUSCATE("NOT_ACTIVATED"))       return zh ? u8"请先验证许可证" : (const char*)OBFUSCATE("Please verify your license first");
    if (c == (const char*)OBFUSCATE("UNKNOWN_FEATURE"))     return zh ? u8"功能不可用" : (const char*)OBFUSCATE("Feature not available");
    if (c == (const char*)OBFUSCATE("REPLAY_DETECTED"))     return zh ? u8"请求被拒绝，请重试" : (const char*)OBFUSCATE("Request rejected, please retry");
    if (c == (const char*)OBFUSCATE("REQUEST_EXPIRED"))     return zh ? u8"请检查系统时间" : (const char*)OBFUSCATE("Check your system clock");
    if (c == (const char*)OBFUSCATE("SIGNATURE_INVALID"))   return zh ? u8"连接不安全" : (const char*)OBFUSCATE("Connection is not secure");
    if (c == (const char*)OBFUSCATE("NET_ERROR"))           return zh ? u8"无法连接服务器" : (const char*)OBFUSCATE("Cannot reach server");
    if (c == (const char*)OBFUSCATE("OFFLINE_TIMEOUT"))     return zh ? u8"连接已断开" : (const char*)OBFUSCATE("Connection lost");
    if (c == (const char*)OBFUSCATE("SESI_TIDAK_SAH") ||
        c == (const char*)OBFUSCATE("SESI_DICABUT") ||
        c == (const char*)OBFUSCATE("SESI_KEDALUWARSA"))    return zh ? u8"会话已结束，请重新登录" : (const char*)OBFUSCATE("Session ended, please log in again");
    if (c == (const char*)OBFUSCATE("RATE_LIMITED"))        return zh ? u8"请求过于频繁，请稍候" : (const char*)OBFUSCATE("Too many requests, please wait");
    if (c == (const char*)OBFUSCATE("DEVICE_BLACKLISTED"))    return zh ? u8"设备或账户已被永久封禁" : (const char*)OBFUSCATE("Device or account permanently banned");
    if (c == (const char*)OBFUSCATE("INTEGRITY_CHECK_FAILED")) return zh ? u8"内存完整性校验失败" : (const char*)OBFUSCATE("Security integrity check failed");
    if (c == (const char*)OBFUSCATE("SERVER_ERROR"))        return zh ? u8"服务器错误，请重试" : (const char*)OBFUSCATE("Server error, please retry");
    if (!r.message.empty())         return r.message;
    return zh ? u8"身份验证失败" : (const char*)OBFUSCATE("Authentication failed");
}

// Fail closed tanpa memberi respons jaringan yang rusak kemampuan melakukan
// denial-of-service. Payload/signature yang tidak sah tetap ditolak, tetapi
// tidak boleh menghapus sesi atau menutup game. Hanya `kill` yang sudah
// diperoleh setelah verifikasi authSig berhasil yang boleh menutup aplikasi.
bool ShouldQuitApp(const AuthResult& r) {
    return r.kill && !r.tampered;
}

// =========================================================
//  KRIPTO TANDA TANGAN (ECDSA P-256 + SHA-256)
// =========================================================
// Setara versi PC (BCrypt)；Android 版从系统加密库动态解析符号，iOS 版改由
// Core/Crypto.h 直接提供，语义与 DecryptPayload di ModUtils.cpp 一致。
static std::vector<uint8_t> HexToBytesAuth(const std::string& hex) {
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
        int hi = val(hex[i]), lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::vector<uint8_t>();
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return out;
}

static bool Sha256RawNative(const std::string& data, unsigned char out[32]) {
    // iOS：Core/Crypto.h 的 SHA-256，等价原来动态取到的 SHA256()。
    return SMCrypto::Sha256Raw(data, out);
}

static std::string Sha256HexNative(const std::string& data) {
    return SMCrypto::Sha256Hex(data);
}

std::string Sha256HexNativePublic(const std::string& data) {
    return Sha256HexNative(data);
}

// Verifikasi ECDSA P-256. pubXY = 64 byte (X||Y), sigRS = 64 byte (r||s),
// hash = SHA-256 (32 byte). r||s = format IEEE P-1363 yang dikirim server.
static OLLVM_SEC_CRITICAL bool VerifyEcdsaP256Native(const std::vector<uint8_t>& pubXY,
                                                  const unsigned char hash[32],
                                                  const std::vector<uint8_t>& sigRS) {
    if (pubXY.size() != 64 || sigRS.size() != 64) return false;

    // iOS：Security.framework 的 P-256 验签；r||s -> DER 的转换在实现内部完成。
    return SMCrypto::EcdsaVerifyP256(pubXY, hash, sigRS);
}

// =========================================================
//  PARSING + VERIFIKASI TANDA TANGAN
// =========================================================
extern "C" VIRBOX_TARGET_ATTR AuthResult Virbox_NetAuthParse(const std::string& resp,
                                    const std::string& nonce,
                                    const std::string& hwid) {
    AuthResult r;
    r.raw = resp;

    if (resp.empty()) {
        r.netError = true;
        r.code = (const char*)OBFUSCATE("NET_ERROR");
        r.message = AuthDisplayMessage(r);
        return r;
    }

    bool success   = JsonGetBool(resp, "success", false);
    bool kill      = JsonGetBool(resp, "kill", false);
    std::string code      = JsonGetString(resp, "code");
    std::string timestamp = JsonGetNumberRaw(resp, "timestamp");
    std::string sig       = JsonGetString(resp, "authSig");

    // Tanpa authSig, respons ini bukan dari server kita (atau server
    // masih memakai kode lama). Perlakukan sebagai palsu.
    if (sig.empty() || code.empty() || timestamp.empty()) {
        r.tampered = true;
        r.code = (const char*)OBFUSCATE("SIGNATURE_INVALID");
        r.message = AuthDisplayMessage(r);
        return r;
    }
    // `data` = ciphertext payload (untuk /payload) atau "" (untuk /verify).
    std::string data = JsonGetString(resp, (const char*)OBFUSCATE("data"));
    std::string serverPubKey = JsonGetString(resp, (const char*)OBFUSCATE("serverPubKey"));

    std::string line;
    line += SIG_VERSION;          line += "|";
    line += success ? "1" : "0";  line += "|";
    line += code;                 line += "|";
    line += kill ? "1" : "0";     line += "|";
    line += timestamp;            line += "|";
    line += nonce;                line += "|";
    line += hwid;                 line += "|";
    line += Sha256HexNative(data);line += "|";
    line += serverPubKey;

    // Verifikasi ECDSA P-256 dengan PUBLIC KEY yang di-embed. Respons hanya
    // sah bila ditandatangani PRIVATE KEY server -> anti server palsu / MITM.
    unsigned char digest[32];
    std::vector<uint8_t> pub      = HexToBytesAuth(PUBKEY_XY_HEX);
    std::vector<uint8_t> sigBytes = HexToBytesAuth(sig);

    if (sig.empty() || sigBytes.size() != 64 || pub.size() != 64 ||
        !Sha256RawNative(line, digest) ||
        !VerifyEcdsaP256Native(pub, digest, sigBytes)) {
        r.tampered = true;
        r.code = (const char*)OBFUSCATE("SIGNATURE_INVALID");
        r.message = AuthDisplayMessage(r);
        return r;
    }

    r.ok      = success;
    r.kill    = kill;
    r.code    = code;
    r.data    = data;
    r.serverPubKey = std::move(serverPubKey);
    r.requestNonce = nonce;
    r.requestHwid = hwid;
    r.message = AuthDisplayMessage(r);
    return r;
}

// =========================================================
//  MENUTUP APLIKASI
// =========================================================
static std::atomic<bool> g_quitTriggered{false};

void QuitApp(const std::string& message) {
    AkhiriSesiLokal(StatusSesi::Dicabut);
    is_loading       = false;
    auth_status_msg  = message;
    auth_status_color = ImVec4(172/255.f, 46/255.f, 38/255.f, 1.0f);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // iOS：没有 Activity/Process 可操作，直接结束进程。
    SMPlatform::Quit();
}

void TriggerQuit(const std::string& code, const std::string& message) {
    bool expected = false;
    if (!g_quitTriggered.compare_exchange_strong(expected, true)) return;

    ALOG("Sesi diakhiri: %s (%s)", code.c_str(), message.c_str());
    std::thread([message]() { QuitApp(message); }).detach();
}

// =========================================================
//  REQUEST VERIFY
// =========================================================
// =========================================================
//  KATALOG & PETA OP<->WIRE (disamakan dengan versi PC/Server)
// =========================================================
// Server BARU mengirim katalog TERENKRIPSI di field "featuresEnc" (AES-GCM),
// tanpa canonical "id". Client cukup tahu WIRE id + OP.
static std::string ExtractCatalogJson(const AuthResult& hasil) {
    if (!hasil.decrypted.empty()) return hasil.decrypted;
    return {};
}

// Bangun ulang g_opToWire / g_wireToOp dari catalog JSON terdekripsi.
// buildList=true -> isi ulang featureList juga (dipakai saat login sukses).
extern "C" VIRBOX_TARGET_ATTR void Virbox_RebuildWireMap(
        const std::string& catalogJson, bool buildList) {
    // 格式闸：只接受云端原始 catalog（feature 字段名 "w"）。设备上那份 dump 快照
    // 的 catalog.json 用的是 "wireId"，这里若放行会解析出 0 条映射并把映射表清空，
    // 直接导致 WireForOp/HasWire 全线失败（商店、烹饪、传送、载具替换点了没反应）。
    // 所以拒绝时保持现有映射不动，让内嵌目录继续生效。
    if (!LocalCatalog::IsValidCatalog(catalogJson)) {
        ALOG("RebuildWireMap ditolak: format catalog tidak valid (bukan \"w\")");
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_opToWireMtx);
        g_opToWire.clear();
        g_wireToOp.clear();
    }
    (void)buildList;

    size_t p = 0;
    while ((p = catalogJson.find((const char*)OBFUSCATE("{\"w\":"), p)) != std::string::npos) {
        // cari '}' penutup objek dgn menghitung kedalaman (aman utk objek bersarang)
        size_t endPos = std::string::npos;
        int depth = 0; bool inStr = false;
        for (size_t k = p; k < catalogJson.size(); ++k) {
            char c = catalogJson[k];
            if (inStr) { if (c == '\\') { ++k; continue; } if (c == '"') inStr = false; continue; }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') ++depth;
            else if (c == '}') { --depth; if (depth == 0) { endPos = k; break; } }
        }
        if (endPos == std::string::npos) break;
        std::string item = catalogJson.substr(p, endPos - p + 1);

        // wire id (opaque, rotasi tiap restart server)
        std::string wv;
        {
            size_t wp = item.find((const char*)OBFUSCATE("\"w\":\""));
            if (wp != std::string::npos) {
                size_t ws = wp + 5;
                size_t we = item.find('"', ws);
                if (we != std::string::npos) wv = item.substr(ws, we - ws);
            }
        }
        // op-code (penanda identitas fitur; 0 = generic)
        int op = 0;
        std::string opStr = JsonGetNumberRaw(item, (const char*)OBFUSCATE("op"));
        if (!opStr.empty()) { try { op = std::stoi(opStr); } catch (...) { op = 0; } }

        if (!wv.empty()) {
            std::lock_guard<std::mutex> lk(g_opToWireMtx);
            g_wireToOp[wv] = op;
            if (op != 0) g_opToWire[op] = wv;
        }

        p = endPos + 1;
    }
}

extern "C" VIRBOX_TARGET_ATTR void Virbox_MenuCatalogParse(
        const std::string& catalogJson) {
    featureList.clear();
    size_t p = 0;
    while ((p = catalogJson.find((const char*)OBFUSCATE("{\"w\":"), p)) != std::string::npos) {
        size_t endPos = std::string::npos;
        int depth = 0;
        bool inStr = false;
        for (size_t k = p; k < catalogJson.size(); ++k) {
            const char c = catalogJson[k];
            if (inStr) {
                if (c == '\\') { ++k; continue; }
                if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) { endPos = k; break; }
        }
        if (endPos == std::string::npos) break;

        const std::string item = catalogJson.substr(p, endPos - p + 1);
        CheatFeature f;
        f.id = extractJsonString(item, (const char*)OBFUSCATE("w"));
        f.name = extractJsonString(item, (const char*)OBFUSCATE("name"));
        f.nameZh = extractJsonString(item, (const char*)OBFUSCATE("nameZh"));
        // 菜单固定中文：中文名尾部的英文括注 + 残留英文词（NPC/ANIMAL/EVENT/ID…）
        // 在解析阶段就清掉，界面上不会出现英文。
        LocalCatalog::LocalizeCatalogName(f.nameZh);
        f.isPatch = item.find((const char*)OBFUSCATE("\"isPatch\":true")) != std::string::npos;
        f.catalog = 1;
        const std::string opStr = JsonGetNumberRaw(item, (const char*)OBFUSCATE("op"));
        const std::string catStr = JsonGetNumberRaw(item, (const char*)OBFUSCATE("catalog"));
        if (!opStr.empty()) { try { f.op = std::stoi(opStr); } catch (...) { f.op = 0; } }
        if (!catStr.empty()) { try { f.catalog = std::stoi(catStr); } catch (...) { f.catalog = 1; } }
        if (!f.id.empty()) featureList.push_back(std::move(f));
        p = endPos + 1;
    }
}

// =========================================================
//  PENERAPAN KATALOG (dipakai jalur cloud maupun dump offline)
// =========================================================
// shopNames / shopIds ikut di dalam blob catalog. Main.cpp mengiterasi
// shopNames.size() lalu mengakses shopIds[i] -> panjang wajib seragam.
static void SawitParseStringArray(const std::string& json, const std::string& key,
                                  std::vector<std::string>& out) {
    out.clear();
    const std::string head = (const char*)OBFUSCATE("\"") + key + (const char*)OBFUSCATE("\":[");
    size_t aPos = json.find(head);
    if (aPos == std::string::npos) return;
    size_t arrEnd = json.find((const char*)OBFUSCATE("]"), aPos);
    size_t sIdx = json.find((const char*)OBFUSCATE("\""), aPos + head.length());
    while (sIdx != std::string::npos && arrEnd != std::string::npos && sIdx < arrEnd) {
        size_t e = json.find((const char*)OBFUSCATE("\""), sIdx + 1);
        if (e == std::string::npos) break;
        out.push_back(json.substr(sIdx + 1, e - sIdx - 1));
        sIdx = json.find((const char*)OBFUSCATE("\""), e + 1);
    }
}

static void SawitParseShopTables(const std::string& catalogJson) {
    SawitParseStringArray(catalogJson, (const char*)OBFUSCATE("shopNames"), shopNames);
    SawitParseStringArray(catalogJson, (const char*)OBFUSCATE("shopNamesZh"), shopNamesZh);
    SawitParseStringArray(catalogJson, (const char*)OBFUSCATE("shopIds"), shopIds);
    // PENGAMAN CRASH: Main.cpp melakukan for (i < shopNames.size()) lalu
    // mengakses shopIds[i]. Kalau shopIds lebih pendek -> out of bounds.
    if (shopIds.size() < shopNames.size()) {
        shopNames.resize(shopIds.size());
    }
    if (shopNamesZh.size() < shopNames.size()) {
        shopNamesZh.resize(shopNames.size());
    }
}

// Satu pintu masuk: peta op<->wire, menu featureList, tabel shop, pesan broadcast.
// Katalog cloud maupun katalog dump offline lewat sini supaya perilakunya identik.
void SawitApplyCatalog(const std::string& catalogJson) {
    if (!LocalCatalog::IsValidCatalog(catalogJson)) {
        ALOG("SawitApplyCatalog ditolak: format catalog tidak valid (bukan \"w\")");
        return;
    }
    Virbox_RebuildWireMap(catalogJson, true);
    Virbox_MenuCatalogParse(catalogJson);
    ParseBroadcastMessages(catalogJson);
    SawitParseShopTables(catalogJson);
}

// op special (!=0) -> wire id terkini. "" kalau tak ada.
std::string WireForOp(int op) {
    std::lock_guard<std::mutex> lk(g_opToWireMtx);
    auto it = g_opToWire.find(op);
    return it != g_opToWire.end() ? it->second : std::string();
}

// wire id -> op. 0 kalau generic / tak dikenal.
int OpForWire(const std::string& wire) {
    std::lock_guard<std::mutex> lk(g_opToWireMtx);
    auto it = g_wireToOp.find(wire);
    return it != g_wireToOp.end() ? it->second : 0;
}

bool HasWire(const std::string& wire) {
    std::lock_guard<std::mutex> lk(g_opToWireMtx);
    return !wire.empty() && g_wireToOp.find(wire) != g_wireToOp.end();
}

static OLLVM_SEC_CRITICAL std::string GenerateIntegrityProof(const std::string& nonce, const std::string& hwid) {
    if (!AntiHookCheck() || !AntiAnalysisSecurityCheck()) {
        return "TAMPERED_DEBUGGER_DETECTED";
    }
    // Verifikasi alamat fungsi security
    volatile uint32_t* p = (volatile uint32_t*)&Virbox_SessionProofCheck;
    if (!p || *p == 0x52800020 || *p == 0xd65f03c0) {
        return "TAMPERED_HOOK_DETECTED";
    }
    std::string seed = nonce + "|" + hwid;
    return HmacSha256HexNative(seed, "SawitMod-Arm64-Integrity-Proof-v1");
}

extern "C" VIRBOX_TARGET_ATTR AuthResult Virbox_DoVerify(const std::string& license) {
    std::string hwid  = GetHWIDNative();
    std::string nonce = GenerateNonceNative();
    long timestamp = (long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::string clientPubKey;
    void* privKey = GenerateEphemeralEcdhKey(clientPubKey);

    std::ostringstream body;
    body << "{\"license\":\""   << JsonEscape(license)
         << "\",\"hwid\":\""    << JsonEscape(hwid)
         << "\",\"clientBrand\":\"" << ANDROID_CLIENT_BRAND
         << "\",\"version\":\"" << SAWIT_VERSION
         << "\",\"channel\":\"" << SAWIT_CHANNEL
         << "\",\"platform\":\"android\""
         << ",\"payloadProtocol\":2"
         << ",\"timestamp\":" << timestamp
         << ",\"nonce\":\""     << nonce
         << "\",\"clientPubKey\":\"" << clientPubKey
         << "\"}";

    std::string url = API_BASE;
    // Tab AVATAR pakai endpoint login terpisah supaya key VIP & AVATAR tak saling login.
    url += g_avatarMode ? (const char*)OBFUSCATE("/api/license/avatar/verify")
                        : (const char*)OBFUSCATE("/api/license/verify");
    std::string resp = HttpPostNative(url, body.str());

    AuthResult r = Virbox_NetAuthParse(resp, nonce, hwid);
    if (r.ok && !r.data.empty() && privKey) {
        r.decrypted = DecryptDynamicPayload(r.data, privKey, r.serverPubKey, nonce, hwid);
        privKey = nullptr;
    }
    if (privKey) {
        FreeEphemeralEcdhKey(privKey);
    }
    return r;
}

AuthResult DoVerifyRequest(const std::string& license) {
    return Virbox_DoVerify(license);
}

static std::atomic<bool> g_hbRunning{false};
static std::mutex        g_licMtx;
static std::string       g_license;

static AuthResult KirimPermintaanSesi(const std::string& license, const char* jalur) {
    std::lock_guard<std::mutex> kunci(g_permintaanSesiMtx);
    AuthResult gagal;
    std::string idSesi, tokenSesi;
    if (!SalinBuktiSesi(idSesi, tokenSesi)) {
        gagal.code = (const char*)OBFUSCATE("SESI_TIDAK_SAH");
        return gagal;
    }
    std::string hwid = GetHWIDNative();
    std::string nonce = GenerateNonceNative();
    long timestamp = (long)WaktuSekarangDetik();

    std::string clientPubKey;
    void* privKey = GenerateEphemeralEcdhKey(clientPubKey);

    std::ostringstream body;
    body << "{\"license\":\"" << JsonEscape(license)
         << "\",\"hwid\":\"" << JsonEscape(hwid)
         << "\",\"idSesi\":\"" << JsonEscape(idSesi)
         << "\",\"tokenSesi\":\"" << JsonEscape(tokenSesi)
         << "\",\"clientBrand\":\"" << ANDROID_CLIENT_BRAND
         << "\",\"jenisLogin\":\"" << (g_avatarMode ? "AVATAR" : "VIP")
         << "\",\"version\":\"" << SAWIT_VERSION
         << "\",\"channel\":\"" << SAWIT_CHANNEL
         << "\",\"timestamp\":" << timestamp
         << ",\"nonce\":\"" << nonce
         << "\",\"clientPubKey\":\"" << clientPubKey
         << "\"}";
    std::string resp = HttpPostNative(std::string(API_BASE) + jalur, body.str());
    AuthResult r = Virbox_NetAuthParse(resp, nonce, hwid);
    if (r.ok && !r.data.empty() && privKey) {
        r.decrypted = DecryptDynamicPayload(r.data, privKey, r.serverPubKey, nonce, hwid);
        privKey = nullptr;
    }
    if (privKey) {
        FreeEphemeralEcdhKey(privKey);
    }
    return r;
}

bool PerbaruiSesi(const std::string& license) {
    AuthResult r = KirimPermintaanSesi(license, (const char*)OBFUSCATE("/api/license/sesi/perbarui"));
    if (!r.ok || r.decrypted.empty()) return false;
    return PasangSesi(r.decrypted, g_avatarMode);
}

void KeluarSesiServer(const std::string& license) {
    KirimPermintaanSesi(license, (const char*)OBFUSCATE("/api/license/sesi/keluar"));
    AkhiriSesiLokal();
}

void StopLicenseHeartbeat() {
    g_hbRunning.store(false);
}

void StartLicenseHeartbeat(const std::string& license) {
    {
        std::lock_guard<std::mutex> lock(g_licMtx);
        g_license = license;
    }
    bool expected = false;
    if (!g_hbRunning.compare_exchange_strong(expected, true)) return;

    std::thread([]() {
        const int PERIKSA_DETIK = 5 * 60;
        const int COBA_LAGI_DETIK = 60;
        const int MAKS_GAGAL = 5;
        int gagal = 0;
        int tunggu = PERIKSA_DETIK;

        while (g_hbRunning.load()) {
            for (int i = 0; i < tunggu; ++i) {
                if (!g_hbRunning.load()) return;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            std::string lisensi;
            {
                std::lock_guard<std::mutex> lock(g_licMtx);
                lisensi = g_license;
            }
            if (lisensi.empty() || !SesiAktif()) break;
            if (!PerbaruiSesi(lisensi)) {
                if (++gagal >= MAKS_GAGAL || !SesiAktif()) {
                    AkhiriSesiLokal(StatusSesi::Dicabut);
                    featureList.clear();
                    std::lock_guard<std::mutex> lock(invokeMutex);
                    invokeQueue.clear();
                    break;
                }
                tunggu = COBA_LAGI_DETIK;
                continue;
            }
            gagal = 0;
            tunggu = PERIKSA_DETIK;
        }
        g_hbRunning.store(false);
    }).detach();
}

// =========================================================
//  LOGIN
// =========================================================
// 卡密登录失败回调（对齐安卓端）：
//   安卓端 VerifyLicenseJNI 的每条失败分支都会把原因顶到 auth_status_msg，
//   登录按钮那边再 Toast 出来。iOS 这里收敛成一个函数：文案 + 状态色 +
//   解除 loading + 悬浮轻提示（Platform.mm 的 NotifyAuthFailure）。
[[maybe_unused]] static void AuthFailCallback(const std::string& reason) {
    const std::string text = reason.empty()
        ? std::string(u8"卡密验证失败：未知原因（服务器未返回信息）")
        : std::string(u8"卡密验证失败：") + reason;
    auth_status_msg   = text;
    auth_status_color = ImVec4(172/255.f, 46/255.f, 38/255.f, 1.0f);
    is_loading        = false;
    SMPlatform::NotifyAuthFailure(reason);
    ALOG("%s", text.c_str());
}

OLLVM_SEC_CRITICAL void VerifyLicenseJNI(const char* licenseKey) {
    std::string license = licenseKey ? licenseKey : "";

#if SAWIT_OFFLINE_MODE
    // 卡密验证已关闭（离线闸）：不校验、不联网，只把本地会话装好，
    // 让菜单直接可用。下面那套服务端校验与它取来的功能目录仍然保留，
    // 把 Version.h 的 SAWIT_OFFLINE_MODE 改成 0 即完全恢复原行为。
    (void)license;
    // LoginOfflineSession 同时负责装内嵌 catalog（含 96 个 feature + 194 条
    // payload 信封），否则离线构建拿不到任何 op<->wire 映射。
    LoginOfflineSession(g_avatarMode);
    auth_status_msg   = u8"离线版 - 已关闭卡密验证";
    auth_status_color = ImVec4(30/255.f, 118/255.f, 60/255.f, 1.0f);
    is_loading        = false;
    return;
#else
    if (license.empty()) {
        AuthFailCallback(u8"请输入卡密");
        return;
    }

    AuthResult r = Virbox_DoVerify(license);
    if (!r.ok) {
        // 失败原因（网络 / 状态码 / 面板原文）统一走失败回调：状态行 + 悬浮提示。
        AuthFailCallback(AuthDisplayMessage(r));
        return;
    }

    // Bukti Sesi dan katalog berada dalam blob `data` yang terenkripsi serta
    // hash ciphertext-nya terikat pada signature ECDSA.
    std::string catalogJson = ExtractCatalogJson(r);
    if (catalogJson.empty() || !PasangSesi(catalogJson, g_avatarMode)) {
        AkhiriSesiLokal(StatusSesi::Dicabut);
        AuthFailCallback(u8"会话无效（服务端返回的会话凭据无法安装）");
        return;
    }
    SaveLicenseLocally(license_input);
    // 服务端目录优先；格式不合规（例如被外部 dump 快照覆盖过）就回落到内嵌目录，
    // 两条路都过同一个格式闸，不会出现"目录解析出 0 条映射"的静默失败。
    if (LocalCatalog::IsValidCatalog(catalogJson)) {
        SawitApplyCatalog(catalogJson);
        LocalCatalog::InstallCatalog(catalogJson, "cloud");
    } else if (!LocalCatalog::InstallEmbedded()) {
        ALOG("VerifyLicenseJNI: katalog cloud invalid dan embedded gagal dipasang");
    }
    LocalCatalog::SelfTest(LocalCatalog::CatalogSource());

    auth_status_msg   = u8"登录成功";
    auth_status_color = ImVec4(30/255.f, 118/255.f, 60/255.f, 1.0f);
    is_loading        = false;

    ClientControlAgent::SetModSession(license);
    StartLicenseHeartbeat(license);
#endif
}

// =========================================================
//  PAYLOAD
// =========================================================
// Dipakai bersama oleh FetchPayloadSync & FetchAndExecute supaya format
// request-nya tidak pernah beda lagi antar fungsi.
struct PayloadResult {
    bool ok = false;
    bool quitRequested = false;
    bool localReplay = false;   // true = 信封来自内嵌 dump，没联网
    std::string decrypted;
    std::string requestNonce;
    std::string requestHwid;
    std::string requestWireId;
    // 绑定串 (nonce|hwid|wire|version|channel|op) 里用的 version/channel。
    // 离线回放必须原样带回抓包时那一组，否则 PayloadVM 的绑定校验不通过。
    std::string bindVersion;
    std::string bindChannel;
};

// Ambil ulang katalog & rebuild peta op<->wire saat server sudah rotasi (restart).
static bool RefreshWireMap() {
    // Login ulang otomatis akan menerbitkan Sesi baru dan mencabut Sesi aktif.
    return false;
}

extern "C" VIRBOX_TARGET_ATTR PayloadResult Virbox_RequestPayload(const std::string& featId, bool allowRetry) {
    std::lock_guard<std::mutex> kunciPermintaan(g_permintaanSesiMtx);
    PayloadResult out;

    // featId yang masuk SUDAH berupa wire id (dari WireForOp(OP_*) atau feat.id).
    // Simpan op-nya supaya bisa dihitung ulang kalau server rotasi (UNKNOWN_FEATURE).
    std::string wireId = featId;
    int op = OpForWire(featId);
    std::string idSesi, tokenSesi;
    uint64_t generasiSesi = 0;
    if (!SalinBuktiSesi(idSesi, tokenSesi, &generasiSesi)) return out;

#if SAWIT_LOCAL_FEATURES
    // ---- 离线回放：内嵌 dump 优先 ----
    // 信封连同抓包时的 nonce/hwid/version/channel 一起取回，解密/绑定校验完全
    // 走原有的 PayloadVM 路径，只是不再有 HTTP 往返。
    {
        LocalCatalog::Entry local;
        if (LocalCatalog::TakePayload(wireId, local)) {
            out.decrypted     = std::move(local.envelope);
            out.requestNonce  = std::move(local.nonce);
            out.requestHwid   = std::move(local.hwid);
            out.requestWireId = std::move(local.wire);
            out.bindVersion   = std::move(local.bindVersion);
            out.bindChannel   = std::move(local.bindChannel);
            out.localReplay   = true;
            out.ok            = true;
            return out;
        }
    }
#if SAWIT_LOCAL_PAYLOAD_ONLY
    // 离线构建：没抓到信封的功能（op=3,4,5,6,7,13,15,16,18,19,20,24,25,26,27,32,33）
    // 直接给个明确状态，不要再去撞网络。
    auth_status_msg = std::string((const char*)OBFUSCATE("Offline payload missing: ")) +
                      (op != 0 ? std::to_string(op) : wireId);
    auth_status_color = ImVec4(168/255.f, 112/255.f, 12/255.f, 1.0f);
    return out;
#endif
#endif

    std::string hwid  = GetHWIDNative();
    std::string nonce = GenerateNonceNative();
    long timestamp = (long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::string clientPubKey;
    void* privKey = GenerateEphemeralEcdhKey(clientPubKey);

    std::ostringstream body;
    body << "{\"license\":\""     << JsonEscape(license_input)
         << "\",\"hwid\":\""      << JsonEscape(hwid)
         << "\",\"idSesi\":\""    << JsonEscape(idSesi)
         << "\",\"tokenSesi\":\"" << JsonEscape(tokenSesi)
         << "\",\"clientBrand\":\"" << ANDROID_CLIENT_BRAND
         << "\",\"featureId\":\"" << JsonEscape(wireId)
         << "\",\"platform\":\"android\""
         << ",\"payloadProtocol\":2"
         << ",\"version\":\""      << SAWIT_VERSION
         << "\",\"channel\":\""   << SAWIT_CHANNEL
         << "\",\"timestamp\":"   << timestamp
         << ",\"nonce\":\""       << nonce
         << "\",\"clientPubKey\":\"" << clientPubKey
         << "\"}";

    std::string url  = API_BASE;
    url += g_avatarMode ? (const char*)OBFUSCATE("/api/license/avatar/payload")
                        : (const char*)OBFUSCATE("/api/license/payload");
    std::string resp = HttpPostNative(url, body.str());

    AuthResult r = Virbox_NetAuthParse(resp, nonce, hwid);
    if (ShouldQuitApp(r)) {
        if (privKey) FreeEphemeralEcdhKey(privKey);
        TriggerQuit(r.code, AuthDisplayMessage(r));
        out.quitRequested = true;
        return out;
    }

    if (!r.ok) {
        if (privKey) FreeEphemeralEcdhKey(privKey);
        // Tampilkan hanya kode tahap, tanpa payload/license/token/HWID.
        auth_status_msg = std::string((const char*)OBFUSCATE("Payload rejected: ")) +
                          (r.code.empty() ? (const char*)OBFUSCATE("UNKNOWN") : r.code);
        auth_status_color = ImVec4(172/255.f, 46/255.f, 38/255.f, 1.0f);
        if (r.code == (const char*)OBFUSCATE("SESI_TIDAK_SAH") ||
            r.code == (const char*)OBFUSCATE("SESI_DICABUT") ||
            r.code == (const char*)OBFUSCATE("SESI_KEDALUWARSA"))
            AkhiriSesiLokal(StatusSesi::Dicabut);
        (void)allowRetry;
        (void)op;
        return out;
    }
    if (!SesiAktif() || generasiSesi != GenerasiSesi()) {
        if (privKey) FreeEphemeralEcdhKey(privKey);
        return out;
    }

    // `r.data` adalah field yang sudah ikut diverifikasi dalam authSig.
    // Jangan parse ulang response mentah karena hasil parser kedua dapat berbeda.
    const std::string& encryptedRaw = r.data;
    if (encryptedRaw.empty()) {
        if (privKey) FreeEphemeralEcdhKey(privKey);
        auth_status_msg = (const char*)OBFUSCATE("Payload rejected: EMPTY_DATA");
        auth_status_color = ImVec4(172/255.f, 46/255.f, 38/255.f, 1.0f);
        return out;
    }

    out.decrypted = DecryptDynamicPayload(encryptedRaw, privKey, r.serverPubKey, nonce, hwid);
    privKey = nullptr; // DecryptDynamicPayload frees privKey

    if (out.decrypted.empty()) {
        auth_status_msg = (const char*)OBFUSCATE("Payload rejected: DECRYPT_FAILED");
        auth_status_color = ImVec4(172/255.f, 46/255.f, 38/255.f, 1.0f);
        return out;
    }
    out.requestNonce = std::move(nonce);
    out.requestHwid = std::move(hwid);
    out.requestWireId = std::move(wireId);
    out.ok = true;
    return out;
}

extern "C" VIRBOX_TARGET_ATTR InvokeRequest Virbox_FetchPayload(std::string featId) {
    InvokeRequest req = {featId, "", "", "", "", "", false, ""};
    // 卡密未通过：除了免费功能（采集类），其余一概不下发
    if (!Weiyan::IsVerified() && !SawitFree::IsFree(featId)) return req;
    if (!SesiAktif() || !HasWire(featId)) return req;
    req.generasiSesi = GenerasiSesi();

    PayloadResult pr = Virbox_RequestPayload(featId, true);
    if (!pr.ok || !SesiAktif() || req.generasiSesi != GenerasiSesi()) {
        return req;
    }

    req.op = OpForWire(featId);
    PayloadVM::Context vmContext;
    vmContext.nonce = pr.requestNonce;
    vmContext.hwid = pr.requestHwid;
    vmContext.wireId = pr.requestWireId;
    // 绑定串里的 version/channel 必须与签发该信封时一致：云端信封用当时的
    // SAWIT_VERSION/SAWIT_CHANNEL，内嵌 dump 信封用抓包时记下的那一组。
    vmContext.version = pr.bindVersion.empty() ? std::string(SAWIT_VERSION) : pr.bindVersion;
    vmContext.channel = pr.bindChannel.empty() ? std::string(SAWIT_CHANNEL) : pr.bindChannel;
    vmContext.expectedOp = req.op;
    if (!PayloadVM::InterpretRaw(pr.decrypted, vmContext, req)) {
        PayloadVM::WipeString(pr.decrypted);
        auth_status_msg = (const char*)OBFUSCATE("Payload rejected: VM2_INVALID");
        auth_status_color = ImVec4(172/255.f, 46/255.f, 38/255.f, 1.0f);
        req = InvokeRequest{};
        req.featureId = featId;
        return req;
    }
    PayloadVM::WipeString(pr.decrypted);
    req.featureId = featId;
    req.generasiSesi = GenerasiSesi();
    return req;
}

extern "C" VIRBOX_TARGET_ATTR void Virbox_FetchAndExecute(
        std::string featId, std::string customParam, std::string customInstance) {
    // 卡密未通过：只有免费功能（采集类）放行，其余功能请求直接丢弃
    if (!Weiyan::IsVerified() && !SawitFree::IsFree(featId)) return;
    // OP_GENERIC bernilai 0 tetapi wire-nya tetap valid. Jangan gunakan syarat
    // `op != 0`; cek keberadaan wire di katalog agar semua fitur patch berjalan.
    if (!SesiAktif() || !HasWire(featId)) return;
    const uint64_t generasiSesi = GenerasiSesi();
    PayloadResult pr = Virbox_RequestPayload(featId, true);
    if (!pr.ok || !SesiAktif() || generasiSesi != GenerasiSesi()) {
        return;
    }

    InvokeRequest req;
    req.featureId = featId;
    req.op = OpForWire(featId);
    req.vmStream.swap(pr.decrypted);
    req.vmNonce = std::move(pr.requestNonce);
    req.vmHwid = std::move(pr.requestHwid);
    req.vmWireId = std::move(pr.requestWireId);
    req.vmParamOverride = std::move(customParam);
    req.vmInstanceOverride = std::move(customInstance);
    req.vmBindVersion = std::move(pr.bindVersion);
    req.vmBindChannel = std::move(pr.bindChannel);
    req.generasiSesi = generasiSesi;
    {
        if (!SesiAktif() || generasiSesi != GenerasiSesi()) return;
        std::lock_guard<std::mutex> lock(invokeMutex);
        invokeQueue.push_back(std::move(req));
    }
}

InvokeRequest FetchPayloadSync(std::string featId) {
    return Virbox_FetchPayload(std::move(featId));
}

void FetchAndExecute(std::string featId, std::string customParam,
                     std::string customInstance) {
    Virbox_FetchAndExecute(std::move(featId), std::move(customParam),
                           std::move(customInstance));
}

// 免费功能清单在 SawitFree.h 里（纯 inline，界面/门禁/设备测试共用一份）。
