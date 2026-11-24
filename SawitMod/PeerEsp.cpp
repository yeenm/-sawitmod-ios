// ============================================================================
//  PeerEsp.cpp — 同行绘制（iOS 版）
// ----------------------------------------------------------------------------
//  与安卓 v1.8.7/v1.8.8 的 app/src/main/jni/PeerEsp.cpp 同源，但平台差异全部
//  收敛在本文件里，原有 iOS 功能（雷达 ESP / 各解锁 / 采集）一行都不改：
//
//    安卓                                   iOS
//    ---------------------------------------------------------------------
//    HttpPostNative + jni 桥                HttpPostNative -> SMHttp(NSURLSession)
//    /sdcard/... peer_tags.json             Documents / DataDir 下的 peer_tags.json
//    mono_*（热更 Mono）                    _monoXxx -> IL2CPP/MonoHot 真实 mono
//    ImGui 画在游戏 EGL 上                  ImGui 画在自己的悬浮层（CADisplayLink）
//    帧泵 = hook GameApp_Update             帧泵 = CADisplayLink 每帧调 SMRunUnityFramePump
//
//  数据链路（与安卓完全一致）：
//    名单层   POST /api/peer/register  +  POST /api/peer/list   （后台线程，6~30s）
//            本地兜底 peer_tags.json                            （5s 一次，便宜）
//    扫描层   _netEntitiesMap 枚举 netId ->（Unity 线程，300ms 一轮，分批探测）
//            PlayerProtocolManager.TryGetPlayerShortId / TryGetPlayerEncodeShortId
//            -> { raw shortId, 名片 Id } -> 名单双路匹配
//    投影层   Camera.WorldToScreenPoint + 背后 1-x/1-y 翻转 + 相机直线距离（每帧）
//    绘制层   金色方框 RGB(255,196,0) + 称谓/距离 + 屏外贴边箭头（每帧，纯 ImGui）
//
//  性能约束（照抄安卓的数值）：
//    扫描 300ms 一轮；每轮新实体探测预算 64；探测缓存 20000 条超了清空；
//    名单同步 6s（首次）/10s（名单空）/30s（有名单）；自身身份 15s 一次；
//    每帧只对「已确认命中名单」的少数 netId 取坐标（通常 1~10 次）。
// ============================================================================

#include "PeerEsp.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "App/EndpointObf.h"
#include "App/Platform.h"
#include "Core/State.h"
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "ModFeatures.h"
#include "ModUtils.h"
#include "MonoGameApi.h"
#include "NetAuth.h"
#include "Version.h"
#include "Weiyan.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/imgui.h"

using namespace MonoApi;

namespace {

// ---------------------------------------------------------------------------
//  常量（与安卓版一一对应）
// ---------------------------------------------------------------------------
constexpr long long kScanIntervalMs = 300;        // 实体扫描周期
constexpr int kProbeBudgetPerScan = 64;           // 每轮新实体探测预算
constexpr int kComponentProbeBudget = 12;         // 每轮最多做几次组件兜底判型
constexpr size_t kProbeCacheLimit = 20000;        // 探测缓存上限（超了清空）
constexpr long long kSelfIdentityIntervalMs = 15000;
constexpr long long kSelfNetIdIntervalMs = 2000;
constexpr long long kLocalFileIntervalMs = 5000;
constexpr long long kSyncIntervalFirstMs = 6000;
constexpr long long kSyncIntervalEmptyMs = 10000;
constexpr long long kSyncIntervalWithPeersMs = 30000;

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

// ---------------------------------------------------------------------------
//  迷你 JSON 工具（名单对象都很小，够用；与安卓版同一套实现）
// ---------------------------------------------------------------------------
std::string JsonQuote(const std::string &s) {
    std::string out = "\"";
    for (char c: s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    out += "\"";
    return out;
}

bool JsonStringField(const std::string &obj, const char *key, std::string &out) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos = obj.find('"', pos + 1);
    if (pos == std::string::npos) return false;
    std::string value;
    for (size_t i = pos + 1; i < obj.size(); ++i) {
        const char c = obj[i];
        if (c == '\\' && i + 1 < obj.size()) {
            const char n = obj[++i];
            if (n == 'n') value += '\n';
            else if (n == 'r') value += '\r';
            else if (n == 't') value += '\t';
            else value += n;
            continue;
        }
        if (c == '"') {
            out.swap(value);
            return true;
        }
        value += c;
    }
    return false;
}

bool ParseLongLong(const std::string &text, long long &out) {
    if (text.empty() || text.size() > 20) return false;
    long long value = 0;
    for (char c: text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return value > 0;
}

bool ValidPeerUid(const std::string &s) {
    if (s.size() < 2 || s.size() > 40) return false;
    for (char c: s) {
        const unsigned char u = (unsigned char) c;
        if (!(std::isalnum(u) || c == '_' || c == '-')) return false;
    }
    return true;
}

// 从任意 JSON 文本里扫出 "键":"字符串值" 对，按「键是不是纯数字」分成
// shortId 名单和名片 Id 名单。peer_tags.json 只有两层，够用。
void ScanLabelPairs(const std::string &text,
                    std::unordered_map<long long, std::string> &byShort,
                    std::unordered_map<std::string, std::string> &byUid) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '"') continue;
        std::string key;
        size_t j = i + 1;
        bool closed = false;
        for (; j < text.size(); ++j) {
            const char c = text[j];
            if (c == '\\' && j + 1 < text.size()) {
                key += text[++j];
                continue;
            }
            if (c == '"') {
                closed = true;
                ++j;
                break;
            }
            key += c;
        }
        if (!closed) break;
        i = j - 1;
        size_t k = j;
        while (k < text.size() && std::isspace((unsigned char) text[k])) ++k;
        if (k >= text.size() || text[k] != ':') continue;
        ++k;
        while (k < text.size() && std::isspace((unsigned char) text[k])) ++k;
        if (k >= text.size() || text[k] != '"') continue;
        std::string value;
        size_t m = k + 1;
        bool vclosed = false;
        for (; m < text.size(); ++m) {
            const char c = text[m];
            if (c == '\\' && m + 1 < text.size()) {
                value += text[++m];
                continue;
            }
            if (c == '"') {
                vclosed = true;
                ++m;
                break;
            }
            value += c;
        }
        if (!vclosed || value.empty() || value.size() > 80) continue;
        i = m - 1;
        if (key.empty() || key.size() > 40) continue;
        bool numeric = true;
        for (char c: key) {
            if (c < '0' || c > '9') {
                numeric = false;
                break;
            }
        }
        if (numeric) {
            long long shortId = 0;
            if (ParseLongLong(key, shortId)) byShort[shortId] = value;
        } else if (ValidPeerUid(key)) {
            byUid[key] = value;
        }
    }
}

// ---------------------------------------------------------------------------
//  名单状态（后台线程写、Unity 线程 / 渲染线程读）
// ---------------------------------------------------------------------------
std::mutex g_peerMutex;
std::unordered_map<long long, std::string> g_labelByShort;   // raw shortId -> label
std::unordered_map<std::string, std::string> g_labelByUid;   // 名片 Id -> label
std::unordered_map<long long, std::string> g_uidByShort;     // 扫描期学到的 shortId -> 名片 Id
std::unordered_map<long long, std::string> g_localByShort;   // peer_tags.json: tags
std::unordered_map<std::string, std::string> g_localByUid;   // peer_tags.json: uids

std::atomic<bool> g_drawEnabled{true};
std::atomic<int> g_peerCount{0};
std::atomic<int> g_syncCount{0};
std::atomic<int> g_syncFails{0};
std::atomic<long long> g_lastSyncTryMs{0};
std::atomic<long long> g_lastSyncOkMs{0};
std::atomic<bool> g_syncBusy{false};
std::atomic<long long> g_lastLocalReadMs{0};

// 菜单状态行（后台线程写，渲染线程读）
std::mutex g_noteMutex;
std::string g_syncNote = u8"未同步";
std::string g_deviceNote;   // 本次同步用的设备码来源（weiyan/hwid + 长度），诊断用

// 自身身份（名片 Id + raw shortId + 自己的卡等级标签）
std::mutex g_selfMutex;
std::string g_selfUid;
std::string g_selfShortStr;
std::string g_selfLabel;
std::atomic<long long> g_selfShortVal{0};
std::atomic<long long> g_selfRefreshMs{0};
std::atomic<unsigned int> g_selfNetId{0};
std::atomic<long long> g_selfNetIdTryMs{0};

// ---------------------------------------------------------------------------
//  扫描缓存 / 绘制列表
// ---------------------------------------------------------------------------
struct PeerProbe {
    bool checked = false;
    bool isPlayer = false;
    long long shortId = 0;
    std::string uid;
};

struct PeerDrawEntry {
    unsigned int netId = 0;
    long long shortId = 0;
    std::string uid;
    std::string label;
    float sx = 0.0f;         // 已换算到 ImGui 显示坐标（像素）
    float sy = 0.0f;
    float distance = 0.0f;
    bool onScreen = false;
    bool behind = false;
};

std::mutex g_probeMutex;
std::unordered_map<unsigned int, PeerProbe> g_probeCache;

std::mutex g_drawMutex;
std::vector<PeerDrawEntry> g_drawList;
std::atomic<bool> g_drawListCleared{true};

std::atomic<int> g_statPlayers{0};
std::atomic<int> g_statPeers{0};
std::atomic<long long> g_lastScanMs{0};

void SetSyncNote(const std::string &note) {
    std::lock_guard<std::mutex> lock(g_noteMutex);
    g_syncNote = note;
}

std::string GetSyncNote() {
    std::lock_guard<std::mutex> lock(g_noteMutex);
    return g_syncNote;
}

std::string GetDeviceNote() {
    std::lock_guard<std::mutex> lock(g_noteMutex);
    return g_deviceNote;
}

// ---------------------------------------------------------------------------
//  mono API 解析（只解析一次；mono 晚就绪时最多 3s 重试一次）
// ---------------------------------------------------------------------------
struct MonoApis {
    bool tried = false;
    bool ready = false;
    long long lastTryMs = 0;

    void *imgData = nullptr;
    void *imgLevel = nullptr;

    void *dataCenterKlass = nullptr;
    void *m_getCurrentData = nullptr;

    void *entitiesKlass = nullptr;
    void *m_GetAnyEntity = nullptr;

    void *entityKlass = nullptr;
    void *m_getPos = nullptr;

    // Entity.GetComponent<T>(bool) 的泛型膨胀结果（判"这条实体是不是玩家"的兜底）
    void *m_GetComponent = nullptr;
    void *m_compPlayer[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

    // PlayerProtocolManager 的两个静态方法
    void *playerProtoKlass = nullptr;
    void *m_TryPlayerShortId = nullptr;      // (uint netId, out long) -> bool
    void *m_TryPlayerEncodeShortId = nullptr;// (uint netId, out string) -> bool

    // 名片 Id -> raw shortId（Sazabi.Login.Shared.ShortIdUtil.DecodeShortId）
    bool decodeTried = false;
    void *m_DecodeShortId = nullptr;

    // UnityEngine.Screen.get_width / get_height
    void *m_screenWidth = nullptr;
    void *m_screenHeight = nullptr;
};

MonoApis g_api;
std::mutex g_apiMutex;

bool MonoUsable() {
    if (!_monoGetRootDomain || !_monoRuntimeInvoke || !_monoObjectUnbox) return false;
    void *domain = _monoGetRootDomain();
    if (!domain) return false;
    if (_monoThreadAttach) _monoThreadAttach(domain);
    return true;
}

void *FindClassInCandidates(const char *const *images, int imageCount, const char *nameSpace,
                            const char *className, std::string *outImage) {
    return FindClass(images, imageCount, nameSpace, className, outImage, true);
}

// 解析所有依赖符号。带节流：mono 没就绪时 3s 后再试；已就绪但两个 ID 解析器还没命中时
// 每 10s 再试一次（热更程序集是懒加载的，晚一点才出现是常态）。
void EnsureApis() {
    std::lock_guard<std::mutex> lock(g_apiMutex);
    const long long now = NowMs();
    if (g_api.ready && (g_api.m_TryPlayerShortId || g_api.m_TryPlayerEncodeShortId)) return;
    const long long retryMs = g_api.ready ? 10000 : 3000;
    if (g_api.tried && now - g_api.lastTryMs < retryMs) return;
    g_api.tried = true;
    g_api.lastTryMs = now;

    if (!MonoUsable()) return;

    static const char *const kDataImages[] = {"XDTDataAndProtocol", "EcsClient", "XDTGameSystem"};
    static const char *const kLevelImages[] = {"XDTLevelAndEntity", "EcsClient", "XDTGameSystem"};

    std::string image;
    g_api.imgData = LoadedImage(OBFUSCATE("XDTDataAndProtocol"));
    g_api.imgLevel = LoadedImage(OBFUSCATE("XDTLevelAndEntity"));

    // --- 实体表：XDTDataAndProtocol.ComponentsData.DataCenter.get_CurrentData() ---
    g_api.dataCenterKlass = FindClassInCandidates(kDataImages, 3,
                                                 OBFUSCATE("XDTDataAndProtocol.ComponentsData"),
                                                 OBFUSCATE("DataCenter"), &image);
    if (!g_api.dataCenterKlass) {
        g_api.dataCenterKlass = FindClassInCandidates(kDataImages, 3, OBFUSCATE(""),
                                                     OBFUSCATE("DataCenter"), &image);
    }
    if (g_api.dataCenterKlass) {
        g_api.m_getCurrentData = FindMethodInHierarchy(
                g_api.dataCenterKlass, OBFUSCATE("get_CurrentData"), 0);
    }

    // --- 实体对象：XDTLevelAndEntity.BaseSystem.EntitiesManager.Entities.GetAnyEntity(int) ---
    g_api.entitiesKlass = FindClassInCandidates(kLevelImages, 3,
                                                OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
                                                OBFUSCATE("Entities"), &image);
    if (!g_api.entitiesKlass) {
        g_api.entitiesKlass = FindClassInCandidates(kLevelImages, 3, OBFUSCATE(""),
                                                   OBFUSCATE("Entities"), &image);
    }
    if (g_api.entitiesKlass) {
        g_api.m_GetAnyEntity = FindMethodInHierarchy(g_api.entitiesKlass,
                                                     OBFUSCATE("GetAnyEntity"), 1);
    }

    // --- 世界坐标：XDTLevelAndEntity.Core.World.Entity.get_position() ---
    g_api.entityKlass = FindClassInCandidates(kDataImages, 3,
                                             OBFUSCATE("XDTLevelAndEntity.Core.World"),
                                             OBFUSCATE("Entity"), &image);
    if (!g_api.entityKlass) {
        g_api.entityKlass = FindClassInCandidates(kLevelImages, 3,
                                                  OBFUSCATE("XDTLevelAndEntity.Core.World"),
                                                  OBFUSCATE("Entity"), &image);
    }
    if (g_api.entityKlass) {
        g_api.m_getPos = FindMethodInHierarchy(g_api.entityKlass, OBFUSCATE("get_position"), 0);
    }

    // --- 玩家组件判型（与原有雷达同一套 iOS 已验证的组件名）---
    if (g_api.entityKlass) {
        g_api.m_GetComponent = FindMethodInHierarchy(g_api.entityKlass,
                                                     OBFUSCATE("GetComponent"), 1);
    }
    if (g_api.m_GetComponent && g_api.imgLevel) {
        // 组件名沿用 iOS 雷达（已真机验证）那套；逐条写是为了让字符串都过 OBFUSCATE。
        g_api.m_compPlayer[0] = InflateGetComponent(
                g_api.imgLevel, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"),
                OBFUSCATE("BasePlayerComponent"), g_api.m_GetComponent);
        g_api.m_compPlayer[1] = InflateGetComponent(
                g_api.imgLevel, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"),
                OBFUSCATE("RemotePlayerComponent"), g_api.m_GetComponent);
        g_api.m_compPlayer[2] = InflateGetComponent(
                g_api.imgLevel, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"),
                OBFUSCATE("PlayerComponent"), g_api.m_GetComponent);
        g_api.m_compPlayer[3] = InflateGetComponent(
                g_api.imgLevel, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.HideAndSeek"),
                OBFUSCATE("PlayerMorphComponent"), g_api.m_GetComponent);
        g_api.m_compPlayer[4] = InflateGetComponent(
                g_api.imgLevel, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.HideAndSeek"),
                OBFUSCATE("ClientHideAndSeekHiderPlacedComponent"), g_api.m_GetComponent);
    }

    // --- PlayerProtocolManager：netId -> raw shortId / 名片 Id ---
    static const char *const kPlayerProtoNamespaces[] = {
            "XDTDataAndProtocol.ProtocolService.Player",
            "XDTDataAndProtocol.ProtocolService.GamePlay.Player",
            "XDTDataAndProtocol.ProtocolService",
            "XDTDataAndProtocol",
            "",
    };
    for (const char *ns: kPlayerProtoNamespaces) {
        g_api.playerProtoKlass = FindClassInCandidates(
                nullptr, 0, ns, OBFUSCATE("PlayerProtocolManager"), &image);
        if (g_api.playerProtoKlass) break;
    }
    if (g_api.playerProtoKlass) {
        // (uint, out long) -> 参数码 "i?"（out 参数是 BYREF，'?' 通配更稳）
        g_api.m_TryPlayerShortId = FindMethodBySignature(
                g_api.playerProtoKlass, OBFUSCATE("TryGetPlayerShortId"), "i?");
        if (!g_api.m_TryPlayerShortId) {
            g_api.m_TryPlayerShortId = FindMethodInHierarchy(
                    g_api.playerProtoKlass, OBFUSCATE("TryGetPlayerShortId"), 2);
        }
        g_api.m_TryPlayerEncodeShortId = FindMethodBySignature(
                g_api.playerProtoKlass, OBFUSCATE("TryGetPlayerEncodeShortId"), "i?");
        if (!g_api.m_TryPlayerEncodeShortId) {
            g_api.m_TryPlayerEncodeShortId = FindMethodInHierarchy(
                    g_api.playerProtoKlass, OBFUSCATE("TryGetPlayerEncodeShortId"), 2);
        }
    }

    // --- UnityEngine.Screen.get_width / get_height ---
    void *imgEngine = LoadedImage(OBFUSCATE("EngineWrapper"));
    if (!imgEngine) imgEngine = LoadedImage(OBFUSCATE("UnityEngine"));
    void *screenKlass = imgEngine
                                ? _monoClassFromName(imgEngine, OBFUSCATE("UnityEngine"),
                                                     OBFUSCATE("Screen"))
                                : nullptr;
    if (screenKlass) {
        g_api.m_screenWidth = FindMethodInHierarchy(screenKlass, OBFUSCATE("get_width"), 0);
        g_api.m_screenHeight = FindMethodInHierarchy(screenKlass, OBFUSCATE("get_height"), 0);
    }

    // 「实体表 + 实体对象」齐了就算可用；坐标 get_position 缺了走 NativeGetPosition 兜底，
    // ID 解析器缺了就走组件判型（只按能拿到的那个 id 匹配）。
    g_api.ready = (g_api.m_getCurrentData != nullptr && g_api.m_GetAnyEntity != nullptr);
    g_api.imgData = g_api.imgData ? g_api.imgData : LoadedImage(OBFUSCATE("XDTDataAndProtocol"));
    g_api.imgLevel = g_api.imgLevel ? g_api.imgLevel : LoadedImage(OBFUSCATE("XDTLevelAndEntity"));

    LOGI(OBFUSCATE("[PeerEsp] api ready=%d proto=%s shortId=%s encode=%s comp=%d"),
         g_api.ready ? 1 : 0,
         g_api.playerProtoKlass ? ClassFullName(g_api.playerProtoKlass).c_str() : "NULL",
         g_api.m_TryPlayerShortId ? "OK" : "MISS",
         g_api.m_TryPlayerEncodeShortId ? "OK" : "MISS",
         g_api.m_GetComponent ? 1 : 0);
}

// ---------------------------------------------------------------------------
//  mono 读取原语
// ---------------------------------------------------------------------------

// 静态方法 TryGetPlayerShortId(uint, out long) -> bool
bool InvokePlayerShortId(unsigned int netId, long long &outShortId) {
    outShortId = 0;
    if (!g_api.m_TryPlayerShortId) return false;
    unsigned int argNetId = netId;
    long long resolved = 0;
    void *args[2] = {&argNetId, &resolved};
    void *exc = nullptr;
    void *boxed = _monoRuntimeInvoke(g_api.m_TryPlayerShortId, nullptr, args, &exc);
    if (!boxed || exc) return false;
    void *raw = _monoObjectUnbox(boxed);
    if (!raw) return false;
    if (*(unsigned char *) raw == 0 || resolved <= 0) return false;
    outShortId = resolved;
    return true;
}

// 静态方法 TryGetPlayerEncodeShortId(uint, out string) -> bool
bool InvokePlayerEncodeShortId(unsigned int netId, std::string &outUid) {
    outUid.clear();
    if (!g_api.m_TryPlayerEncodeShortId) return false;
    unsigned int argNetId = netId;
    void *outString = nullptr;
    void *args[2] = {&argNetId, &outString};
    void *exc = nullptr;
    void *boxed = _monoRuntimeInvoke(g_api.m_TryPlayerEncodeShortId, nullptr, args, &exc);
    if (!boxed || exc) return false;
    void *raw = _monoObjectUnbox(boxed);
    if (!raw || *(unsigned char *) raw == 0 || !outString) return false;
    const std::string uid = ReadMonoStringNative(outString);
    if (uid.empty() || uid == (const char *) OBFUSCATE("NULL") ||
        uid == (const char *) OBFUSCATE("Invalid_String")) {
        return false;
    }
    if (!ValidPeerUid(uid)) return false;
    outUid = uid;
    return true;
}

// 名片 Id -> raw shortId：Sazabi.Login.Shared.ShortIdUtil.DecodeShortId(string) -> long
long long DecodeShortIdFromUid(const std::string &uid) {
    if (uid.empty() || !_monoStringNew) return 0;
    if (!g_api.decodeTried) {
        g_api.decodeTried = true;
        static const char *const kNamespaces[] = {"Sazabi.Login.Shared", "Sazabi.Login", ""};
        void *klass = nullptr;
        for (const char *ns: kNamespaces) {
            klass = FindClassInCandidates(nullptr, 0, ns, OBFUSCATE("ShortIdUtil"), nullptr);
            if (klass) break;
        }
        if (klass) {
            g_api.m_DecodeShortId = FindMethodBySignature(klass, OBFUSCATE("DecodeShortId"), "s");
            if (!g_api.m_DecodeShortId) {
                g_api.m_DecodeShortId = FindMethodInHierarchy(klass, OBFUSCATE("DecodeShortId"), 1);
            }
        }
        LOGI(OBFUSCATE("[PeerEsp] ShortIdUtil.DecodeShortId=%s"),
             g_api.m_DecodeShortId ? "OK" : "MISS");
    }
    if (!g_api.m_DecodeShortId) return 0;
    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!domain) return 0;
    void *managedString = _monoStringNew(domain, uid.c_str());
    if (!managedString) return 0;
    void *args[1] = {managedString};
    void *exc = nullptr;
    void *boxed = _monoRuntimeInvoke(g_api.m_DecodeShortId, nullptr, args, &exc);
    if (!boxed || exc) return 0;
    void *raw = _monoObjectUnbox(boxed);
    if (!raw) return 0;
    const long long value = *(long long *) raw;
    return value > 0 ? value : 0;
}

// Entity.GetComponent<Character.XxxComponent>(bool) 判"是不是玩家"
bool HasPlayerComponent(unsigned int netId, bool &outIsPlayer) {
    outIsPlayer = false;
    if (!g_api.m_GetAnyEntity) return false;
    bool anyInflated = false;
    for (int i = 0; i < 5; ++i) {
        if (g_api.m_compPlayer[i]) {
            anyInflated = true;
            break;
        }
    }
    if (!anyInflated) return false;

    unsigned int argNetId = netId;
    void *args[1] = {&argNetId};
    void *exc = nullptr;
    void *entity = _monoRuntimeInvoke(g_api.m_GetAnyEntity, nullptr, args, &exc);
    if (!entity || exc) return false;

    bool paramTrue = true;
    void *compArgs[1] = {&paramTrue};
    for (int i = 0; i < 5; ++i) {
        if (!g_api.m_compPlayer[i]) continue;
        exc = nullptr;
        void *component = _monoRuntimeInvoke(g_api.m_compPlayer[i], entity, compArgs, &exc);
        if (component && !exc) {
            outIsPlayer = true;
            return true;
        }
    }
    return false;
}

bool ReadEntityPosition(unsigned int netId, Vector3 &out) {
    out = Vector3(0.0f, 0.0f, 0.0f);
    if (!g_api.m_GetAnyEntity) return false;
    unsigned int argNetId = netId;
    void *args[1] = {&argNetId};
    void *exc = nullptr;
    void *entity = _monoRuntimeInvoke(g_api.m_GetAnyEntity, nullptr, args, &exc);
    if (!entity || exc) return false;
    if (g_api.m_getPos) {
        exc = nullptr;
        void *posObj = _monoRuntimeInvoke(g_api.m_getPos, entity, nullptr, &exc);
        if (posObj && !exc) {
            void *raw = _monoObjectUnbox(posObj);
            if (raw) {
                out = *(Vector3 *) raw;
                return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
            }
        }
    }
    // 兜底：ModUtils 的通用取坐标（get_entity / get_transform / get_position 全试一遍）
    const Vector3 fallback = NativeGetPosition(entity);
    if (std::isfinite(fallback.x) && std::isfinite(fallback.y) && std::isfinite(fallback.z) &&
        !(fallback.x == 0.0f && fallback.y == 0.0f && fallback.z == 0.0f)) {
        out = fallback;
        return true;
    }
    return false;
}

// 枚举世界里的实体 netId（与原有雷达同一套 Dictionary 布局读取）
bool EnumerateNetIds(std::vector<unsigned int> &out) {
    out.clear();
    if (!g_api.m_getCurrentData) return false;
    void *exc = nullptr;
    void *currentData = _monoRuntimeInvoke(g_api.m_getCurrentData, nullptr, nullptr, &exc);
    if (!currentData || exc) return false;

    void *klass = _monoObjectGetClass ? _monoObjectGetClass(currentData) : nullptr;
    if (!klass) return false;
    void *f_netMap = FindFieldInHierarchy(klass, OBFUSCATE("_netEntitiesMap"));
    if (!f_netMap) return false;
    void *dictObj = nullptr;
    _monoFieldGetValue(currentData, f_netMap, &dictObj);
    if (!dictObj) return false;

    void *dictKlass = _monoObjectGetClass(dictObj);
    if (!dictKlass) return false;
    void *f_entries = FindFieldInHierarchy(dictKlass, OBFUSCATE("entries"));
    if (!f_entries) f_entries = FindFieldInHierarchy(dictKlass, OBFUSCATE("_entries"));
    if (!f_entries) return false;
    void *entriesArr = nullptr;
    _monoFieldGetValue(dictObj, f_entries, &entriesArr);
    if (!entriesArr) return false;

    const int length = *(int *) ((uint8_t *) entriesArr + OFF_ARR_COUNT);
    if (length <= 0 || length > 50000) return false;
    uint8_t *dataStart = (uint8_t *) entriesArr + OFF_ARR_DATA;
    out.reserve((size_t) length);
    for (int i = 0; i < length; ++i) {
        uint8_t *entry = dataStart + (size_t) i * (size_t) SZ_DICT_ENTRY;
        if (*(int *) entry < 0) continue;                      // hashCode < 0 = 空槽
        const unsigned int netId = *(unsigned int *) (entry + OFF_ENTRY_KEY);
        if (netId == 0) continue;
        out.push_back(netId);
    }
    return !out.empty();
}

// 自身 netId：GetSelfNetPlayerId() -> EntityUtil.GetSelfPlayer().entity.GetNetId()
// （0.6.1 里 PlayerDataCenter.GetSelfNetPlayerId 有时不存在，所以留两级兜底）
unsigned int ResolveSelfNetId() {
    unsigned int netId = g_gardenPlayerNetId;
    if (netId != 0) return netId;

    void *imgData = g_api.imgData ? g_api.imgData : LoadedImage(OBFUSCATE("XDTDataAndProtocol"));
    if (imgData) {
        void *pdcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol"),
                                           OBFUSCATE("PlayerDataCenter"));
        if (!pdcKlass) {
            pdcKlass = _monoClassFromName(imgData, OBFUSCATE(""), OBFUSCATE("PlayerDataCenter"));
        }
        void *m_getNetId = pdcKlass
                                   ? FindMethodInHierarchy(pdcKlass, OBFUSCATE("GetSelfNetPlayerId"), 0)
                                   : nullptr;
        if (m_getNetId) {
            void *exc = nullptr;
            void *res = _monoRuntimeInvoke(m_getNetId, nullptr, nullptr, &exc);
            if (res && !exc) {
                void *raw = _monoObjectUnbox(res);
                if (raw) netId = *(unsigned int *) raw;
            }
        }
    }
    if (netId != 0) {
        g_gardenPlayerNetId = netId;
        return netId;
    }

    void *imgLevel = g_api.imgLevel ? g_api.imgLevel : LoadedImage(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) return 0;
    void *utilKlass = FindClassInCandidates(nullptr, 0,
                                            OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
                                            OBFUSCATE("EntityUtil"), nullptr);
    if (!utilKlass) return 0;
    void *m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
    if (!m_getSelf) return 0;
    void *exc = nullptr;
    void *selfPlayer = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, &exc);
    if (!selfPlayer || exc) return 0;
    void *spKlass = _monoObjectGetClass(selfPlayer);
    void *m_getEntity = spKlass ? FindMethodInHierarchy(spKlass, OBFUSCATE("get_entity"), 0) : nullptr;
    if (!m_getEntity) return 0;
    exc = nullptr;
    void *entity = _monoRuntimeInvoke(m_getEntity, selfPlayer, nullptr, &exc);
    if (!entity || exc) return 0;
    void *entKlass = _monoObjectGetClass(entity);
    if (!entKlass) return 0;
    void *m_getNid = FindMethodInHierarchy(entKlass, OBFUSCATE("GetNetId"), 0);
    if (!m_getNid) m_getNid = FindMethodInHierarchy(entKlass, OBFUSCATE("get_NetId"), 0);
    if (!m_getNid) return 0;
    exc = nullptr;
    void *boxed = _monoRuntimeInvoke(m_getNid, entity, nullptr, &exc);
    if (!boxed || exc) return 0;
    void *raw = _monoObjectUnbox(boxed);
    if (!raw) return 0;
    netId = *(unsigned int *) raw;
    if (netId != 0) g_gardenPlayerNetId = netId;
    return netId;
}

// 自身身份（名片 Id + raw shortId）：PlayerDataCenter._loginInfo 上的静态字段对象
void ReadSelfIdentity(std::string &outUid, long long &outShortId) {
    outUid.clear();
    outShortId = 0;
    void *imgData = g_api.imgData ? g_api.imgData : LoadedImage(OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData || !_monoFieldStaticGetValue || !_monoClassGetVTable) return;
    void *klass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol"),
                                    OBFUSCATE("PlayerDataCenter"));
    if (!klass) return;
    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!domain) return;
    void *vtable = _monoClassGetVTable(domain, klass);
    void *field = FindFieldInHierarchy(klass, OBFUSCATE("_loginInfo"));
    if (!vtable || !field) return;
    void *info = nullptr;
    _monoFieldStaticGetValue(vtable, field, &info);
    if (!info) return;
    void *infoKlass = _monoObjectGetClass(info);
    if (!infoKlass) return;

    // raw shortId：get_ShortId() -> long
    void *m_short = FindMethodInHierarchy(infoKlass, OBFUSCATE("get_ShortId"), 0);
    if (!m_short) m_short = FindMethodInHierarchy(infoKlass, OBFUSCATE("get_shortId"), 0);
    if (m_short) {
        void *exc = nullptr;
        void *boxed = _monoRuntimeInvoke(m_short, info, nullptr, &exc);
        if (boxed && !exc) {
            void *raw = _monoObjectUnbox(boxed);
            if (raw) {
                const long long value = *(long long *) raw;
                if (value > 0) outShortId = value;
            }
        }
    }

    // 名片 Id：get_GetEncodeShortId() -> string
    static const char *const kUidGetters[] = {"get_GetEncodeShortId", "get_EncodeShortId",
                                              "get_EncodedShortId", "get_EncodeId"};
    for (const char *name: kUidGetters) {
        void *getter = FindMethodInHierarchy(infoKlass, name, 0);
        if (!getter) continue;
        void *exc = nullptr;
        void *boxed = _monoRuntimeInvoke(getter, info, nullptr, &exc);
        if (!boxed || exc) continue;
        const std::string value = ReadMonoStringNative(boxed);
        if (value.empty() || value == (const char *) OBFUSCATE("NULL") ||
            value == (const char *) OBFUSCATE("Invalid_String")) {
            continue;
        }
        if (!ValidPeerUid(value)) continue;
        outUid = value;
        break;
    }
}

// 自身身份节流刷新（15s 一次；Unity 线程）
void RefreshSelfIdentity(long long now, bool force) {
    const long long last = g_selfRefreshMs.load(std::memory_order_relaxed);
    if (!force && now - last < kSelfIdentityIntervalMs) return;
    g_selfRefreshMs.store(now, std::memory_order_relaxed);

    const unsigned int netId = ResolveSelfNetId();
    g_selfNetId.store(netId, std::memory_order_relaxed);

    std::string uid;
    long long shortId = 0;
    ReadSelfIdentity(uid, shortId);
    if (shortId == 0 && !uid.empty()) shortId = DecodeShortIdFromUid(uid);

    g_selfShortVal.store(shortId, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_selfMutex);
        g_selfUid = uid;
        g_selfShortStr = shortId > 0 ? std::to_string(shortId) : std::string();
    }
    LOGI(OBFUSCATE("[PeerEsp] self netId=%u uid=%s shortId=%lld"), netId, uid.c_str(), shortId);
}

std::string SelfUid() {
    std::lock_guard<std::mutex> lock(g_selfMutex);
    return g_selfUid;
}

std::string SelfShortStr() {
    std::lock_guard<std::mutex> lock(g_selfMutex);
    return g_selfShortStr;
}

std::string SelfLabel() {
    std::lock_guard<std::mutex> lock(g_selfMutex);
    return g_selfLabel;
}

// ---------------------------------------------------------------------------
//  名单同步（后台线程）
// ---------------------------------------------------------------------------
long long SyncIntervalMs() {
    if (g_lastSyncOkMs.load(std::memory_order_relaxed) == 0) return kSyncIntervalFirstMs;
    if (g_peerCount.load(std::memory_order_relaxed) == 0) return kSyncIntervalEmptyMs;
    return kSyncIntervalWithPeersMs;
}

// 解析 /api/peer/list 的 {"ok":true,"peers":[{uid,label,shortId,...}]}
// 返回 true 表示这次响应确实是名单（包含"空名单"也可以覆盖旧缓存）。
bool ApplyPeerList(const std::string &body) {
    const size_t peersPos = body.find("\"peers\"");
    if (peersPos == std::string::npos) return false;
    const size_t arrayPos = body.find('[', peersPos);
    if (arrayPos == std::string::npos) return false;

    std::vector<std::pair<long long, std::string>> byShort;
    std::unordered_map<std::string, std::string> byUid;

    int depth = 0;
    size_t objStart = std::string::npos;
    for (size_t i = arrayPos + 1; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '{') {
            if (depth == 0) objStart = i;
            ++depth;
        } else if (c == '}') {
            if (depth > 0) --depth;
            if (depth == 0 && objStart != std::string::npos) {
                const std::string obj = body.substr(objStart, i - objStart + 1);
                std::string uid, label, shortIdText;
                JsonStringField(obj, "uid", uid);
                JsonStringField(obj, "label", label);
                JsonStringField(obj, "shortId", shortIdText);
                long long shortId = 0;
                if (!label.empty()) {
                    if (ParseLongLong(shortIdText, shortId)) byShort.emplace_back(shortId, label);
                    if (ValidPeerUid(uid)) byUid[uid] = label;
                }
                objStart = std::string::npos;
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
    }

    std::lock_guard<std::mutex> lock(g_peerMutex);
    g_labelByShort.clear();
    for (const auto &item: byShort) g_labelByShort[item.first] = item.second;
    g_labelByUid.swap(byUid);
    // 服务器只知道 uid、不知道 raw shortId 的账号：用扫描期学到的对应关系回填。
    for (const auto &item: g_uidByShort) {
        if (g_labelByShort.count(item.first)) continue;
        auto it = g_labelByUid.find(item.second);
        if (it != g_labelByUid.end()) g_labelByShort[item.first] = it->second;
    }
    g_peerCount.store((int) g_labelByShort.size(), std::memory_order_relaxed);
    return true;
}

void SyncWorker(std::string card, std::string hwid, std::string uid, std::string shortId) {
    const std::string body = "{\"card\":" + JsonQuote(card) +
                             ",\"hwid\":" + JsonQuote(hwid) +
                             ",\"uid\":" + JsonQuote(uid) +
                             ",\"shortId\":" + JsonQuote(shortId) +
                             ",\"platform\":\"ios\",\"version\":\"" SAWIT_VERSION "\"}";

    std::string registerResp;
    if (ValidPeerUid(uid)) {
        registerResp = HttpPostNative(endpoint::Base() +
                                              std::string((const char *) OBFUSCATE("/api/peer/register")),
                                      body);
        const bool ok = registerResp.find("\"ok\":true") != std::string::npos ||
                        registerResp.find("\"ok\": true") != std::string::npos;
        if (!ok) {
            g_syncFails.fetch_add(1, std::memory_order_relaxed);
            if (registerResp.size() >= 8) {
                SetSyncNote(std::string(u8"报到失败：") + registerResp.substr(0, 96));
            } else {
                SetSyncNote(u8"报到失败：网络不可达");
            }
        } else {
            std::string label;
            JsonStringField(registerResp, "label", label);
            if (!label.empty()) {
                std::lock_guard<std::mutex> lock(g_selfMutex);
                g_selfLabel = label;
            }
        }
    }
    LOGI(OBFUSCATE("[PeerEsp] register uid=%s short=%s resp=%.120s"),
         uid.c_str(), shortId.c_str(), registerResp.c_str());

    const std::string listResp = HttpPostNative(
            endpoint::Base() + std::string((const char *) OBFUSCATE("/api/peer/list")), body);
    const bool listed = ApplyPeerList(listResp);
    if (listed) {
        g_lastSyncOkMs.store(NowMs(), std::memory_order_relaxed);
        char buf[160];
        snprintf(buf, sizeof(buf), u8"已同步：名单 %d 条", g_peerCount.load());
        SetSyncNote(buf);
    } else if (listResp.empty()) {
        SetSyncNote(u8"拉取名单失败：网络不可达");
    } else if (listResp.size() >= 8) {
        SetSyncNote(std::string(u8"拉取名单失败：") + listResp.substr(0, 96));
    }

    size_t cachedShort = 0, cachedUid = 0;
    {
        std::lock_guard<std::mutex> lock(g_peerMutex);
        cachedShort = g_labelByShort.size();
        cachedUid = g_labelByUid.size();
    }
    LOGI(OBFUSCATE("[PeerEsp] peers cached short=%zu uid=%zu listed=%d body=%.160s"),
         cachedShort, cachedUid, listed ? 1 : 0, listResp.c_str());
    g_syncCount.fetch_add(1, std::memory_order_relaxed);
    g_syncBusy.store(false, std::memory_order_release);
}

bool StartSyncNow(bool force) {
    const long long now = NowMs();
    if (!force && now - g_lastSyncTryMs.load(std::memory_order_relaxed) < SyncIntervalMs()) {
        return false;
    }
    if (g_syncBusy.exchange(true, std::memory_order_acq_rel)) return false;
    g_lastSyncTryMs.store(now, std::memory_order_relaxed);

    // 卡密优先取 Weiyan 里「验证通过的那一张」（和聊天室 / 授权心跳同源）；
    // 只有它也空的时候才退回菜单输入框，避免用户改了输入框导致 session 对不上。
    std::string license = Weiyan::CurrentCard();
    if (license.empty()) license = std::string(license_input);

    // ⚠️ 设备码必须用「微验登录时上行的那一个」：面板的会话 (/api/?id=kmlogon) 是按
    //    (卡密, markcode) 建的，markcode = Weiyan::DeviceCode()（HWID 前 16 位短码 /
    //    weiyan_imei_override.txt）。用 GetHWIDNative() 的完整 UUID 去查会话永远查不到，
    //    /api/peer/* 就会回 {"code":"NO_SESSION"}（这正是 iOS 版实测踩到的坑）。
    std::string hwid = Weiyan::DeviceCode();
    const char *hwidSrc = "weiyan";
    if (hwid.empty()) {
        hwid = GetHWIDNative();
        hwidSrc = "hwid";
    }
    {
        std::lock_guard<std::mutex> lock(g_noteMutex);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s:%s(len=%zu)", hwidSrc,
                 hwid.empty() ? "-" : hwid.substr(0, 8).c_str(), hwid.size());
        g_deviceNote = buf;
    }

    std::string uid = SelfUid();
    std::string shortStr = SelfShortStr();
    if (shortStr.empty()) {
        const long long value = g_selfShortVal.load(std::memory_order_relaxed);
        if (value > 0) shortStr = std::to_string(value);
    }
    if (license.empty() || hwid.empty() || (uid.empty() && shortStr.empty())) {
        g_syncBusy.store(false, std::memory_order_release);
        if (license.empty()) SetSyncNote(u8"未登录卡密（名单同步需要卡密）");
        return false;
    }
    if (!ValidPeerUid(uid) && !shortStr.empty()) uid = shortStr;   // 名片 Id 没读到时用 raw shortId 当 uid
    std::thread(SyncWorker, license, hwid, uid, shortStr).detach();
    return true;
}

// ---------------------------------------------------------------------------
//  本地兜底名单 peer_tags.json（服务端不可用时用；PC 版写的就是这个格式）
// ---------------------------------------------------------------------------
std::vector<std::string> LocalPeerFilePaths() {
    std::vector<std::string> paths;
    const std::string docs = SMPlatform::DocumentDir();
    const std::string data = SMPlatform::DataDir();
    if (!docs.empty()) paths.push_back(docs + "/peer_tags.json");
    if (!data.empty()) paths.push_back(data + "/peer_tags.json");
    if (!docs.empty()) paths.push_back(docs + "/peer_tags.txt");
    return paths;
}

void LoadLocalPeerFile(long long now) {
    if (now - g_lastLocalReadMs.load(std::memory_order_relaxed) < kLocalFileIntervalMs) return;
    g_lastLocalReadMs.store(now, std::memory_order_relaxed);

    std::unordered_map<long long, std::string> byShort;
    std::unordered_map<std::string, std::string> byUid;
    std::string used;
    for (const std::string &path: LocalPeerFilePaths()) {
        std::ifstream file(path);
        if (!file.is_open()) continue;
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string text = buffer.str();
        if (text.size() < 8 || text.size() > 512 * 1024) continue;
        ScanLabelPairs(text, byShort, byUid);
        used = path;
        break;
    }
    if (used.empty()) {
        // 文件不存在：不清空（用户可能刚删），等下一轮
        return;
    }
    std::lock_guard<std::mutex> lock(g_peerMutex);
    const size_t before = g_localByShort.size() + g_localByUid.size();
    g_localByShort.swap(byShort);
    g_localByUid.swap(byUid);
    // 已知 shortId <-> 名片 Id 对应关系的，也补进 shortId 名单
    for (const auto &item: g_uidByShort) {
        if (g_localByShort.count(item.first)) continue;
        auto it = g_localByUid.find(item.second);
        if (it != g_localByUid.end()) g_localByShort[item.first] = it->second;
    }
    const size_t after = g_localByShort.size() + g_localByUid.size();
    if (after != before) {
        LOGI(OBFUSCATE("[PeerEsp] local peer file %s -> short=%zu uid=%zu"),
             used.c_str(), g_localByShort.size(), g_localByUid.size());
    }
}

// ---------------------------------------------------------------------------
//  投影（WorldToScreenPoint -> ImGui 显示像素）
// ---------------------------------------------------------------------------
struct ScreenMetrics {
    float unityW = 0.0f;
    float unityH = 0.0f;
    float fbW = 0.0f;
    float fbH = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
};

int InvokeScreenInt(void *method) {
    if (!method) return 0;
    void *exc = nullptr;
    void *boxed = _monoRuntimeInvoke(method, nullptr, nullptr, &exc);
    if (!boxed || exc) return 0;
    void *raw = _monoObjectUnbox(boxed);
    if (!raw) return 0;
    return *(int *) raw;
}

ScreenMetrics ReadScreenMetrics() {
    ScreenMetrics m;
    m.fbW = g_RealScreenWidth.load(std::memory_order_relaxed);
    m.fbH = g_RealScreenHeight.load(std::memory_order_relaxed);
    if (m.fbW < 1.0f) m.fbW = (float) (glWidth > 0 ? glWidth : 1920);
    if (m.fbH < 1.0f) m.fbH = (float) (glHeight > 0 ? glHeight : 1080);

    // Unity 的 Screen 尺寸是静态属性，不会每帧变；500ms 取一次就够
    //（省下每帧两次 mono 反射，方框仍然跟着 framebuffer 尺寸实时缩放）。
    static float s_unityW = 0.0f;
    static float s_unityH = 0.0f;
    static long long s_metricsMs = 0;
    const long long now = NowMs();
    if (s_unityW < 1.0f || now - s_metricsMs >= 500) {
        s_metricsMs = now;
        s_unityW = (float) InvokeScreenInt(g_api.m_screenWidth);
        s_unityH = (float) InvokeScreenInt(g_api.m_screenHeight);
    }
    m.unityW = s_unityW;
    m.unityH = s_unityH;
    if (m.unityW < 1.0f) m.unityW = m.fbW;
    if (m.unityH < 1.0f) m.unityH = m.fbH;
    m.scaleX = m.fbW / m.unityW;
    m.scaleY = m.fbH / m.unityH;
    return m;
}

// 相机世界坐标（算直线距离用；WorldToScreenPoint 的 z 在屏外/背后会 <= 0）
Vector3 ReadCameraPosition(void *camera) {
    if (!camera) return Vector3(0.0f, 0.0f, 0.0f);
    const Vector3 pos = NativeGetPosition(camera);
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) {
        return Vector3(0.0f, 0.0f, 0.0f);
    }
    if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) return Vector3(0.0f, 0.0f, 0.0f);
    return pos;
}

// ---------------------------------------------------------------------------
//  名单匹配（双路：raw shortId 或 名片 Id）
// ---------------------------------------------------------------------------
std::string MatchPeerLabel(const PeerProbe &probe) {
    std::string label;
    if (probe.shortId != 0) {
        std::lock_guard<std::mutex> lock(g_peerMutex);
        auto it = g_labelByShort.find(probe.shortId);
        if (it != g_labelByShort.end()) label = it->second;
        else {
            auto local = g_localByShort.find(probe.shortId);
            if (local != g_localByShort.end()) label = local->second;
        }
    }
    if (label.empty() && !probe.uid.empty()) {
        std::lock_guard<std::mutex> lock(g_peerMutex);
        auto it = g_labelByUid.find(probe.uid);
        if (it != g_labelByUid.end()) label = it->second;
        else {
            auto local = g_localByUid.find(probe.uid);
            if (local != g_localByUid.end()) label = local->second;
        }
    }
    return label;
}

bool HasAnyPeerName() {
    std::lock_guard<std::mutex> lock(g_peerMutex);
    return !g_labelByShort.empty() || !g_labelByUid.empty() || !g_localByShort.empty() ||
           !g_localByUid.empty();
}

// 自排除：netId 命中，或者 shortId / 名片 Id 和自己一致（后者不依赖 netId 能否取到）
bool IsSelfEntry(unsigned int netId, const PeerProbe &probe) {
    const unsigned int selfNetId = g_selfNetId.load(std::memory_order_relaxed);
    if (selfNetId != 0 && netId == selfNetId) return true;
    const long long selfShort = g_selfShortVal.load(std::memory_order_relaxed);
    if (selfShort != 0 && probe.shortId != 0 && probe.shortId == selfShort) return true;
    if (!probe.uid.empty()) {
        const std::string mine = SelfUid();
        if (!mine.empty() && probe.uid == mine) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  扫描（Unity 线程，300ms 一轮）：只负责「谁命中名单 + 世界坐标」
// ---------------------------------------------------------------------------
//  投影放在 ProjectCandidates() 里每帧做（和安卓一样）：300ms 刷新一次世界坐标、
//  60fps 刷新屏幕坐标，方框才会跟手而不是三帧一跳。
struct PeerCandidate {
    unsigned int netId = 0;
    long long shortId = 0;
    std::string uid;
    std::string label;
    Vector3 worldPos;
};

std::mutex g_candidateMutex;
std::vector<PeerCandidate> g_candidates;

void ClearCandidatesAndDraw() {
    {
        std::lock_guard<std::mutex> lock(g_candidateMutex);
        g_candidates.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_drawMutex);
        g_drawList.clear();
        g_drawListCleared = true;
    }
    g_statPlayers.store(0, std::memory_order_relaxed);
    g_statPeers.store(0, std::memory_order_relaxed);
}

void ScanEntities(long long now) {
    g_lastScanMs.store(now, std::memory_order_relaxed);

    std::vector<unsigned int> netIds;
    if (!EnumerateNetIds(netIds)) {
        ClearCandidatesAndDraw();
        return;
    }

    // --- 1) 分批探测："这个 netId 是不是玩家" + 拿到两种 Id ---
    std::vector<unsigned int> toProbe;
    {
        std::lock_guard<std::mutex> lock(g_probeMutex);
        if (g_probeCache.size() > kProbeCacheLimit) g_probeCache.clear();
        for (unsigned int netId: netIds) {
            auto it = g_probeCache.find(netId);
            if (it != g_probeCache.end() && it->second.checked) continue;
            toProbe.push_back(netId);
            if ((int) toProbe.size() >= kProbeBudgetPerScan) break;
        }
    }

    int componentProbes = 0;
    for (unsigned int netId: toProbe) {
        PeerProbe probe;
        probe.checked = true;
        long long shortId = 0;
        std::string uid;
        const bool haveShort = InvokePlayerShortId(netId, shortId);
        const bool haveUid = InvokePlayerEncodeShortId(netId, uid);
        bool isPlayer = haveShort || haveUid;
        if (!isPlayer && componentProbes < kComponentProbeBudget) {
            ++componentProbes;
            bool byComponent = false;
            HasPlayerComponent(netId, byComponent);
            isPlayer = byComponent;
        }
        if (isPlayer && shortId == 0 && !uid.empty()) shortId = DecodeShortIdFromUid(uid);
        if (isPlayer && shortId != 0 && !uid.empty()) PeerEspRememberProfile(shortId, uid);
        probe.isPlayer = isPlayer;
        probe.shortId = shortId;
        probe.uid = uid;
        std::lock_guard<std::mutex> lock(g_probeMutex);
        g_probeCache[netId] = probe;
    }

    // --- 2) 清理已 despawn 的 netId（换场景/换镇后 netId 会变）---
    {
        std::lock_guard<std::mutex> lock(g_probeMutex);
        if (g_probeCache.size() > 64) {
            std::unordered_set<unsigned int> active;
            active.reserve(netIds.size() * 2);
            for (unsigned int id: netIds) active.insert(id);
            for (auto it = g_probeCache.begin(); it != g_probeCache.end();) {
                if (active.find(it->first) == active.end()) it = g_probeCache.erase(it);
                else ++it;
            }
        }
    }

    // --- 3) 命中名单的玩家：现取世界坐标（玩家会动）---
    std::vector<PeerCandidate> candidates;
    int players = 0;
    for (unsigned int netId: netIds) {
        PeerProbe probe;
        {
            std::lock_guard<std::mutex> lock(g_probeMutex);
            auto it = g_probeCache.find(netId);
            if (it == g_probeCache.end() || !it->second.checked) continue;
            probe = it->second;
        }
        if (!probe.isPlayer) continue;
        ++players;
        if (IsSelfEntry(netId, probe)) continue;
        const std::string label = MatchPeerLabel(probe);
        if (label.empty()) continue;
        Vector3 pos;
        if (!ReadEntityPosition(netId, pos)) continue;
        PeerCandidate candidate;
        candidate.netId = netId;
        candidate.shortId = probe.shortId;
        candidate.uid = probe.uid;
        candidate.label = label;
        candidate.worldPos = pos;
        candidates.push_back(candidate);
    }
    g_statPlayers.store(players, std::memory_order_relaxed);
    g_statPeers.store((int) candidates.size(), std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_candidateMutex);
        g_candidates.swap(candidates);
    }
}

// ---------------------------------------------------------------------------
//  投影（Unity 线程，每帧）：世界坐标 -> ImGui 显示像素 + 距离/屏内外
// ---------------------------------------------------------------------------
void ProjectCandidates() {
    std::vector<PeerCandidate> candidates;
    {
        std::lock_guard<std::mutex> lock(g_candidateMutex);
        if (g_candidates.empty()) {
            std::lock_guard<std::mutex> drawLock(g_drawMutex);
            g_drawList.clear();
            g_drawListCleared = true;
            return;
        }
        candidates = g_candidates;
    }

    void *camera = GetMainCameraNative();
    if (!camera) return;
    const ScreenMetrics metrics = ReadScreenMetrics();

    // 相机世界坐标只用来算距离，100ms 刷新一次足够（省下每帧的反射开销）。
    static Vector3 s_cameraPos(0.0f, 0.0f, 0.0f);
    static long long s_cameraPosMs = 0;
    const long long now = NowMs();
    if (now - s_cameraPosMs >= 100) {
        s_cameraPosMs = now;
        s_cameraPos = ReadCameraPosition(camera);
    }
    const bool cameraPosOk =
            !(s_cameraPos.x == 0.0f && s_cameraPos.y == 0.0f && s_cameraPos.z == 0.0f);

    std::vector<PeerDrawEntry> projected;
    projected.reserve(candidates.size());
    for (const PeerCandidate &candidate: candidates) {
        const Vector3 sp = WorldToScreenNative(camera, candidate.worldPos);
        if (!std::isfinite(sp.x) || !std::isfinite(sp.y) || !std::isfinite(sp.z)) continue;

        // 视口归一化坐标；z <= 0（背后）按 PC/安卓同款做法 1-x / 1-y 翻转
        const float vxRaw = metrics.unityW > 0.0f ? sp.x / metrics.unityW : 0.0f;
        const float vyRaw = metrics.unityH > 0.0f ? sp.y / metrics.unityH : 0.0f;
        const bool behind = sp.z <= 0.01f;
        const float vx = behind ? (1.0f - vxRaw) : vxRaw;
        const float vy = behind ? (1.0f - vyRaw) : vyRaw;
        const float x = vx * metrics.fbW;
        const float y = (1.0f - vy) * metrics.fbH;

        float distance = sp.z;
        if (cameraPosOk) {
            const float realDist = GetVector3Distance(s_cameraPos, candidate.worldPos);
            if (std::isfinite(realDist) && realDist > 0.0f) distance = realDist;
        }
        if (!std::isfinite(distance) || distance < 0.0f) distance = 0.0f;

        PeerDrawEntry entry;
        entry.netId = candidate.netId;
        entry.shortId = candidate.shortId;
        entry.uid = candidate.uid;
        entry.label = candidate.label;
        entry.sx = x;
        entry.sy = y;
        entry.distance = distance;
        entry.behind = behind;
        entry.onScreen = !behind && x >= 0.0f && x <= metrics.fbW && y >= 0.0f &&
                         y <= metrics.fbH;
        projected.push_back(entry);
    }

    std::lock_guard<std::mutex> lock(g_drawMutex);
    g_drawList.swap(projected);
    g_drawListCleared = false;
}

}  // namespace

// ===========================================================================
//  对外接口
// ===========================================================================

bool PeerEspDrawEnabled() { return g_drawEnabled.load(std::memory_order_relaxed); }

void PeerEspSetDrawEnabled(bool enabled) {
    // 幂等：菜单每帧都会调一次 true（同行绘制固定开启、不提供关闭入口），
    // 只有状态真的变化才动作，否则会每帧强制同步一次。
    if (g_drawEnabled.exchange(enabled, std::memory_order_acq_rel) == enabled) return;
    if (!enabled) {
        ClearCandidatesAndDraw();
    } else {
        // 打开时立刻同步一次，省得等 6~30s
        StartSyncNow(true);
    }
}

bool PeerEspHasPeer(long long shortId) {
    if (shortId == 0) return false;
    std::lock_guard<std::mutex> lock(g_peerMutex);
    return g_labelByShort.find(shortId) != g_labelByShort.end() ||
           g_localByShort.find(shortId) != g_localByShort.end();
}

std::string PeerEspLabel(long long shortId) {
    if (shortId == 0) return std::string();
    std::lock_guard<std::mutex> lock(g_peerMutex);
    auto it = g_labelByShort.find(shortId);
    if (it != g_labelByShort.end()) return it->second;
    auto local = g_localByShort.find(shortId);
    return local == g_localByShort.end() ? std::string() : local->second;
}

bool PeerEspHasPeerUid(const std::string &uid) {
    if (uid.empty()) return false;
    std::lock_guard<std::mutex> lock(g_peerMutex);
    return g_labelByUid.find(uid) != g_labelByUid.end() ||
           g_localByUid.find(uid) != g_localByUid.end();
}

std::string PeerEspLabelForUid(const std::string &uid) {
    if (uid.empty()) return std::string();
    std::lock_guard<std::mutex> lock(g_peerMutex);
    auto it = g_labelByUid.find(uid);
    if (it != g_labelByUid.end()) return it->second;
    auto local = g_localByUid.find(uid);
    return local == g_localByUid.end() ? std::string() : local->second;
}

void PeerEspRememberProfile(long long shortId, const std::string &encodedUid) {
    if (shortId == 0 || encodedUid.empty()) return;
    std::lock_guard<std::mutex> lock(g_peerMutex);
    g_uidByShort[shortId] = encodedUid;
    if (g_labelByShort.count(shortId)) return;
    auto it = g_labelByUid.find(encodedUid);
    if (it != g_labelByUid.end()) {
        g_labelByShort[shortId] = it->second;
        g_peerCount.store((int) g_labelByShort.size(), std::memory_order_relaxed);
    }
}

int PeerEspPeerCount() { return g_peerCount.load(std::memory_order_relaxed); }

void PeerEspSyncNow() {
    // 菜单按钮：强制同步（自身身份也刷新一次，避免刚进游戏还没读到名片 Id）
    RunOnUnityThreadAsync([]() {
        if (MonoUsable()) {
            EnsureApis();
            RefreshSelfIdentity(NowMs(), true);
        }
        StartSyncNow(true);
    });
}

void PeerEspPrime() {
    // 「符号自检」按钮用：Unity 线程上把依赖符号和自身身份解析一遍，
    // 然后 PeerEspDiagnostics() 才能报出真实的命中情况。
    if (!MonoUsable()) return;
    EnsureApis();
    RefreshSelfIdentity(NowMs(), true);
}

void PeerEspFrameTick() {
    if (!g_drawEnabled.load(std::memory_order_relaxed)) return;

    const long long now = NowMs();

    // 本地兜底名单（便宜，5s 一次）
    LoadLocalPeerFile(now);

    // 没登录卡密：名单拿不到，也不再碰 mono（清掉绘制内容）
    if (!SesiAktif()) {
        ClearCandidatesAndDraw();
        return;
    }

    // 自身身份（名片 Id / raw shortId）+ 自身 netId：mono 就绪后才做
    if (MonoUsable()) {
        EnsureApis();
        if (g_api.ready) {
            if (now - g_selfRefreshMs.load(std::memory_order_relaxed) >= kSelfIdentityIntervalMs) {
                RefreshSelfIdentity(now, false);
            } else if (g_selfNetId.load(std::memory_order_relaxed) == 0 &&
                       now - g_selfNetIdTryMs.load(std::memory_order_relaxed) >=
                               kSelfNetIdIntervalMs) {
                g_selfNetIdTryMs.store(now, std::memory_order_relaxed);
                g_selfNetId.store(ResolveSelfNetId(), std::memory_order_relaxed);
            }
        }
    }

    // 服务端名单同步（后台线程，绝不能在这个线程发 HTTP）
    StartSyncNow(false);

    if (!g_api.ready) return;

    // 名单一条都没有就别扫了（省 CPU）；本地文件里有名单时照样扫
    if (!HasAnyPeerName()) {
        ClearCandidatesAndDraw();
        return;
    }

    // 300ms 重建一次「命中名单 + 世界坐标」，每帧只做投影（方框才跟手）。
    if (now - g_lastScanMs.load(std::memory_order_relaxed) >= kScanIntervalMs) {
        ScanEntities(now);
    }
    ProjectCandidates();
}

void PeerEspRenderOverlay() {
    if (!g_drawEnabled.load(std::memory_order_relaxed)) return;

    std::vector<PeerDrawEntry> list;
    {
        std::lock_guard<std::mutex> lock(g_drawMutex);
        if (g_drawList.empty()) return;
        list = g_drawList;
    }
    ImDrawList *drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x < 1.0f || display.y < 1.0f) return;
    const ImVec2 screenCenter(display.x * 0.5f, display.y * 0.5f);

    // 投影用的是 framebuffer 像素（= OverlayWindow 的 g_RealScreenWidth）；
    // ImGui 的 DisplaySize 与它同源，但旋转/尺寸切换的那一两帧可能不同步，
    // 这里按实测 DisplaySize 再缩放一次，避免方框整体偏移。
    const float fbW = g_RealScreenWidth.load(std::memory_order_relaxed);
    const float fbH = g_RealScreenHeight.load(std::memory_order_relaxed);
    const float fitX = (fbW >= 1.0f) ? (display.x / fbW) : 1.0f;
    const float fitY = (fbH >= 1.0f) ? (display.y / fbH) : 1.0f;

    // 与安卓版同一套配色/尺寸
    const ImU32 gold = IM_COL32(255, 196, 0, 255);

    for (const PeerDrawEntry &ent: list) {
        const ImVec2 sp(ent.sx * fitX, ent.sy * fitY);
        std::string label = ent.label.empty() ? std::string(u8"同行") : ent.label;
        char buf[192];

        if (!ent.onScreen) {
            // ---- 屏外/背后：沿「屏幕中心 -> 目标」方向贴边画箭头 + 距离 ----
            float dx = sp.x - screenCenter.x;
            float dy = sp.y - screenCenter.y;
            if (!std::isfinite(dx) || !std::isfinite(dy)) continue;
            if (std::fabs(dx) < 1.0f && std::fabs(dy) < 1.0f) {
                dx = 0.0f;
                dy = 1.0f;
            }
            const float margin = 42.0f;
            const float halfW = screenCenter.x - margin;
            const float halfH = screenCenter.y - margin;
            const float absDx = std::fabs(dx) < 1e-3f ? 1e-3f : std::fabs(dx);
            const float absDy = std::fabs(dy) < 1e-3f ? 1e-3f : std::fabs(dy);
            const float scaleX = halfW / absDx;
            const float scaleY = halfH / absDy;
            const float s = scaleX < scaleY ? scaleX : scaleY;
            const ImVec2 edge(screenCenter.x + dx * s, screenCenter.y + dy * s);
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-3f) continue;
            const float ux = dx / len;
            const float uy = dy / len;
            const ImVec2 tip(edge.x + ux * 14.0f, edge.y + uy * 14.0f);
            const ImVec2 left(edge.x - uy * 9.0f - ux * 4.0f, edge.y + ux * 9.0f - uy * 4.0f);
            const ImVec2 right(edge.x + uy * 9.0f - ux * 4.0f, edge.y - ux * 9.0f - uy * 4.0f);
            drawList->AddTriangleFilled(tip, left, right, gold);
            drawList->AddTriangle(tip, left, right, IM_COL32(0, 0, 0, 200), 1.5f);

            snprintf(buf, sizeof(buf), "%s %.0fm", label.c_str(), ent.distance);
            const ImVec2 ts = ImGui::CalcTextSize(buf);
            const ImVec2 tp(edge.x - ts.x * 0.5f, edge.y - ts.y - 6.0f);
            drawList->AddRectFilled(ImVec2(tp.x - 4.0f, tp.y - 1.0f),
                                    ImVec2(tp.x + ts.x + 4.0f, tp.y + ts.y + 1.0f),
                                    IM_COL32(0, 0, 0, 120), 3.0f);
            drawList->AddText(ImVec2(tp.x + 1.0f, tp.y + 1.0f), IM_COL32(0, 0, 0, 220), buf);
            drawList->AddText(tp, gold, buf);
            continue;
        }

        // ---- 屏内：金色方框 + 四角加粗 + 脚下短线 + 上方称谓/距离 ----
        float boxH = 2400.0f / (ent.distance + 12.0f);
        if (boxH < 46.0f) boxH = 46.0f;
        if (boxH > 260.0f) boxH = 260.0f;
        const float boxW = boxH * 0.46f;
        const ImVec2 tl(sp.x - boxW * 0.5f, sp.y - boxH);
        const ImVec2 br(sp.x + boxW * 0.5f, sp.y + 6.0f);
        drawList->AddRect(tl, br, gold, 2.0f, 0, 1.6f);

        const float corner = boxW * 0.28f;
        drawList->AddLine(tl, ImVec2(tl.x + corner, tl.y), gold, 2.6f);
        drawList->AddLine(tl, ImVec2(tl.x, tl.y + corner), gold, 2.6f);
        drawList->AddLine(ImVec2(br.x, tl.y), ImVec2(br.x - corner, tl.y), gold, 2.6f);
        drawList->AddLine(ImVec2(br.x, tl.y), ImVec2(br.x, tl.y + corner), gold, 2.6f);
        drawList->AddLine(ImVec2(sp.x, br.y), ImVec2(sp.x, br.y + 8.0f), gold, 1.6f);

        snprintf(buf, sizeof(buf), "%s [%.0fm]", label.c_str(), ent.distance);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        const ImVec2 tp(sp.x - ts.x * 0.5f, tl.y - ts.y - 4.0f);
        drawList->AddRectFilled(ImVec2(tp.x - 4.0f, tp.y - 1.0f),
                                ImVec2(tp.x + ts.x + 4.0f, tp.y + ts.y + 1.0f),
                                IM_COL32(0, 0, 0, 120), 3.0f);
        drawList->AddText(ImVec2(tp.x + 1.0f, tp.y + 1.0f), IM_COL32(0, 0, 0, 220), buf);
        drawList->AddText(tp, gold, buf);
    }
}

std::string PeerEspStatusLine() {
    if (!g_drawEnabled.load(std::memory_order_relaxed)) return std::string(u8"已关闭");
    if (!SesiAktif()) return std::string(u8"未登录卡密");

    char head[192];
    const long long lastOk = g_lastSyncOkMs.load(std::memory_order_relaxed);
    if (lastOk == 0) {
        snprintf(head, sizeof(head), u8"名单 %d 条 · %s",
                 g_peerCount.load(std::memory_order_relaxed), GetSyncNote().c_str());
    } else {
        const long long ago = (NowMs() - lastOk) / 1000;
        snprintf(head, sizeof(head), u8"名单 %d 条 · %llds 前同步",
                 g_peerCount.load(std::memory_order_relaxed), ago);
    }

    char tail[192];
    snprintf(tail, sizeof(tail), u8" · 场景玩家 %d / 命中 %d",
             g_statPlayers.load(std::memory_order_relaxed),
             g_statPeers.load(std::memory_order_relaxed));

    char self[192] = "";
    const std::string uid = SelfUid();
    const std::string shortStr = SelfShortStr();
    const std::string selfLabel = SelfLabel();
    if (!selfLabel.empty()) {
        snprintf(self, sizeof(self), u8" · 我: %s/%s(%s)", uid.empty() ? "-" : uid.c_str(),
                 shortStr.empty() ? "-" : shortStr.c_str(), selfLabel.c_str());
    } else {
        snprintf(self, sizeof(self), u8" · 我: %s/%s", uid.empty() ? "-" : uid.c_str(),
                 shortStr.empty() ? "-" : shortStr.c_str());
    }

    std::string out = std::string(head) + tail + self;
    // 符号没解析到时给一句人话，省得用户以为是名单问题
    if (!g_api.ready) {
        out += u8" · 等待游戏符号（进世界后自动重试）";
    } else if (!g_api.m_TryPlayerShortId && !g_api.m_TryPlayerEncodeShortId) {
        out += u8" · [注意] 玩家 ID 符号未命中（看「符号自检」）";
    }
    return out;
}

std::string PeerEspDiagnostics() {
    std::string out;
    char buf[512];

    snprintf(buf, sizeof(buf),
             "PeerEsp\n"
             "  api.ready=%d  proto=%s\n"
             "  TryGetPlayerShortId=%s  TryGetPlayerEncodeShortId=%s\n"
             "  DecodeShortId=%s  GetComponent(base player)=%d\n"
             "  selfNetId=%u  selfUid=%s  selfShortId=%lld\n"
             "  peers=%d  syncOk=%d  syncFails=%d  players=%d  hits=%d\n"
             "  hwid=%s  note=%s",
             g_api.ready ? 1 : 0,
             g_api.playerProtoKlass ? ClassFullName(g_api.playerProtoKlass).c_str() : "NULL",
             g_api.m_TryPlayerShortId ? "OK" : "MISS",
             g_api.m_TryPlayerEncodeShortId ? "OK" : "MISS",
             g_api.m_DecodeShortId ? "OK" : "MISS",
             g_api.m_compPlayer[0] ? 1 : 0,
             g_selfNetId.load(std::memory_order_relaxed),
             SelfUid().empty() ? "-" : SelfUid().c_str(),
             g_selfShortVal.load(std::memory_order_relaxed),
             g_peerCount.load(std::memory_order_relaxed),
             g_syncCount.load(std::memory_order_relaxed),
             g_syncFails.load(std::memory_order_relaxed),
             g_statPlayers.load(std::memory_order_relaxed),
             g_statPeers.load(std::memory_order_relaxed),
             GetDeviceNote().empty() ? "-" : GetDeviceNote().c_str(),
             GetSyncNote().c_str());
    out += buf;
    return out;
}
