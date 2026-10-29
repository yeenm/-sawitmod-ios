#include "ClientControlAgent.h"

#include "Global.h"
#include "NetAuth.h"
#include "ModUtils.h"
// iOS：ModFeatures 头在 ios 树里可能落在 Core/ 下（FramePump.cpp 就是这么引的），
// 这里两种布局都兼容。
#if __has_include("ModFeatures.h")
#include "ModFeatures.h"
#elif __has_include("ModFeatures.h")
#include "ModFeatures.h"
#endif
#include "AndroidBrandConfig.h"
#include "App/HttpClient.h"   // iOS：替换 Java HttpURLConnection 的 HttpPostNative
#include "App/Platform.h"     // iOS：设备/安装信息（替换原来的 JNI 块）
#include "Includes/Dobby/dobby.h"
#include "Includes/Logger.h"
// iOS：Android 版的 StartupTrace.h 只包了 <android/log.h>。没有这个头时按 Android
// 默认构建（SAWIT_STARTUP_TRACE=0）的语义退化成空语句，跟踪调用点保持原样。
#if __has_include("StartupTrace.h")
#include "StartupTrace.h"
#endif
#ifndef STARTUP_TRACE
#define STARTUP_TRACE(stage) ((void)0)
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unistd.h>

extern "C" AuthResult Virbox_NetAuthParse(const std::string& response,
                                           const std::string& nonce,
                                           const std::string& hwid);
extern "C" void Virbox_WipeAllFeatures();

namespace ClientControlAgent {
namespace {
struct Snapshot {
    bool gameLoggedIn = false;
    std::string userId, userName, playerId, shortId, encodedShortId;
    std::string serialNumber, device, osType = "iOS", sceneProtocolVersion;
    std::string unionId, publishId, tapShortId, clientId, loginType, loginState;
    bool isWhitelisted = false, isTapShortIdSet = false, isPushIdSet = false;
    std::string loginTimestamp, banEndAt, banReason;
    std::string continentCode, isoCode, timeZone, city, ipErrorCode;
};

std::atomic<bool> running{false};
std::string installationId, secret, modLicense;
std::mutex stateMutex, ackMutex;
Snapshot captured;
std::atomic<void*> loginPanel{nullptr};
std::atomic<void*> clientSession{nullptr};
std::atomic<int> pendingAction{0}; // 1 disable, 2 logout, 3 prepare-exit, 4 prepare-uninject, 5 exit, 6 uninject
std::atomic<int> deferredTerminalAction{0};
std::atomic<long long> pendingCommandId{0}, ackId{0};
std::string ackStatus, ackMessage;
std::atomic<bool> hooksInstalled{false};
std::atomic<long long> lastRefreshMs{0};

using LoginResultFn = void(*)(void*, int, int, void*);
using ObjectFn = void(*)(void*);
using ZoneFn = void(*)(void*, int);
using I64SetterFn = void(*)(void*, long long);
using BoolSetterFn = void(*)(void*, uint8_t);
using StringSetterFn = void(*)(void*, void*);
LoginResultFn originalLoginResult = nullptr;
ObjectFn originalPanelStart = nullptr, originalSessionCreate = nullptr, originalSessionDestroy = nullptr;
ZoneFn originalZoneSuccess = nullptr;
I64SetterFn originalTimestampSetter = nullptr, originalBanEndSetter = nullptr;
BoolSetterFn originalWhitelistSetter = nullptr;
StringSetterFn originalBanReasonSetter = nullptr;

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string Escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    static const char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[c >> 4]);
                    out.push_back(hex[c & 15]);
                } else out.push_back((char)c);
        }
    }
    return out;
}

std::string JsonString(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    size_t p = json.find(marker);
    if (p == std::string::npos || (p = json.find(':', p + marker.size())) == std::string::npos ||
        (p = json.find('"', p + 1)) == std::string::npos) return {};
    std::string out;
    bool escaped = false;
    for (++p; p < json.size(); ++p) {
        char c = json[p];
        if (escaped) {
            if (c == 'n') out.push_back('\n');
            else if (c == 'r') out.push_back('\r');
            else if (c == 't') out.push_back('\t');
            else out.push_back(c);
            escaped = false;
        } else if (c == '\\') escaped = true;
        else if (c == '"') return out;
        else out.push_back(c);
    }
    return {};
}

long long JsonInteger(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    size_t p = json.find(marker);
    if (p == std::string::npos || (p = json.find(':', p + marker.size())) == std::string::npos) return 0;
    p = json.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos) return 0;
    size_t e = p;
    if (json[e] == '-') ++e;
    while (e < json.size() && json[e] >= '0' && json[e] <= '9') ++e;
    try { return std::stoll(json.substr(p, e - p)); } catch (...) { return 0; }
}

std::string ReadString(void* object, const char* getter, const char* fieldName = nullptr) {
    if (!object || !_monoObjectGetClass || !_monoRuntimeInvoke) return {};
    void* klass = _monoObjectGetClass(object);
    void* exception = nullptr;
    void* method = klass ? FindMethodInHierarchy(klass, getter, 0) : nullptr;
    void* value = method ? _monoRuntimeInvoke(method, object, nullptr, &exception) : nullptr;
    if ((!value || exception) && fieldName && klass && _monoFieldGetValue) {
        exception = nullptr;
        void* field = FindFieldInHierarchy(klass, fieldName);
        if (field) _monoFieldGetValue(object, field, &value);
    }
    if (!value || exception) return {};
    std::string result = ReadMonoStringNative(value);
    return result == "NULL" || result == "Invalid_String" ? std::string() : result;
}

long long ReadInt64(void* object, const char* getter) {
    if (!object || !_monoObjectGetClass || !_monoRuntimeInvoke || !_monoObjectUnbox) return 0;
    void* method = FindMethodInHierarchy(_monoObjectGetClass(object), getter, 0);
    void* exception = nullptr;
    void* boxed = method ? _monoRuntimeInvoke(method, object, nullptr, &exception) : nullptr;
    return boxed && !exception ? *(long long*)_monoObjectUnbox(boxed) : 0;
}

int ReadInt32(void* object, const char* getter) {
    if (!object || !_monoObjectGetClass || !_monoRuntimeInvoke || !_monoObjectUnbox) return 0;
    void* method = FindMethodInHierarchy(_monoObjectGetClass(object), getter, 0);
    void* exception = nullptr;
    void* boxed = method ? _monoRuntimeInvoke(method, object, nullptr, &exception) : nullptr;
    return boxed && !exception ? *(int*)_monoObjectUnbox(boxed) : 0;
}

bool ReadBool(void* object, const char* getter) {
    if (!object || !_monoObjectGetClass || !_monoRuntimeInvoke || !_monoObjectUnbox) return false;
    void* method = FindMethodInHierarchy(_monoObjectGetClass(object), getter, 0);
    void* exception = nullptr;
    void* boxed = method ? _monoRuntimeInvoke(method, object, nullptr, &exception) : nullptr;
    return boxed && !exception && *(uint8_t*)_monoObjectUnbox(boxed) != 0;
}

std::string ReadGuid(void* object, const char* getter) {
    if (!object || !_monoObjectGetClass || !_monoRuntimeInvoke) return {};
    void* exception = nullptr;
    void* method = FindMethodInHierarchy(_monoObjectGetClass(object), getter, 0);
    void* boxed = method ? _monoRuntimeInvoke(method, object, nullptr, &exception) : nullptr;
    if (!boxed || exception) return {};
    void* toString = FindMethodInHierarchy(_monoObjectGetClass(boxed), "ToString", 0);
    void* text = toString ? _monoRuntimeInvoke(toString, boxed, nullptr, &exception) : nullptr;
    if (!text || exception) return {};
    std::string result = ReadMonoStringNative(text);
    return result == "NULL" || result == "Invalid_String" ? std::string() : result;
}

const char* LoginStateName(int state) {
    static const char* names[] = {"Ok","Used","Invalid","NeedSerialNumber","Ban","Maintain","Fail",
        "VersionNotSupport","DirtyData","Canceled","NetworkException","NotAllowedSuperLogin",
        "Timeout","AccountNotExist","TdsNeedLogout","RiskControlBan"};
    return state >= 0 && state < (int)(sizeof(names) / sizeof(names[0])) ? names[state] : "Unknown";
}

void CaptureTdsUser(void* session, int state) {
    if (!session || !_monoObjectGetClass || !_monoFieldGetValue) return;
    void* klass = _monoObjectGetClass(session);
    void* field = klass ? FindFieldInHierarchy(klass, "_tdsLoginUser") : nullptr;
    void* user = nullptr;
    if (field) _monoFieldGetValue(session, field, &user);
    std::lock_guard<std::mutex> lock(stateMutex);
    captured.loginState = LoginStateName(state);
    if (!user) return;
    captured.userId = ReadString(user, "get_UserId", "<UserId>k__BackingField");
    captured.serialNumber = ReadString(user, "get_SerialNumber", "<SerialNumber>k__BackingField");
    captured.device = ReadString(user, "get_Device", "<Device>k__BackingField");
    captured.sceneProtocolVersion = ReadString(user, "get_SceneProtocolVersion", "<SceneProtocolVersion>k__BackingField");
    captured.publishId = ReadString(user, "get_PublishId", "<PublishId>k__BackingField");
    captured.tapShortId = ReadString(user, "get_TapShortId", "<TapShortId>k__BackingField");
    captured.clientId = ReadString(user, "get_ClientId", "<ClientId>k__BackingField");
    captured.loginType = ReadString(user, "get_LoginType", "<LoginType>k__BackingField");
    static const char* osNames[] = {"Invalid", "iOS", "Android", "Web", "MacOS", "Windows", "Unknown"};
    int os = ReadInt32(user, "get_OsType");
    captured.osType = os >= 0 && os < 7 ? osNames[os] : "iOS";
}

void CaptureIpInfo(void* panel) {
    if (!panel || !_monoObjectGetClass || !_monoFieldGetValue) return;
    void* field = FindFieldInHierarchy(_monoObjectGetClass(panel), "_ipInfoResult");
    void* info = nullptr;
    if (field) _monoFieldGetValue(panel, field, &info);
    if (!info) return;
    std::lock_guard<std::mutex> lock(stateMutex);
    captured.continentCode = ReadString(info, "get_ContinentCode", "<ContinentCode>k__BackingField");
    captured.isoCode = ReadString(info, "get_ISOCode", "<ISOCode>k__BackingField");
    captured.timeZone = ReadString(info, "get_TimeZone", "<TimeZone>k__BackingField");
    captured.city = ReadString(info, "get_City", "<City>k__BackingField");
    captured.ipErrorCode = std::to_string(ReadInt32(info, "get_ErrorCode"));
}

void HookSessionCreate(void* self) {
    clientSession.store(self, std::memory_order_release);
    if (originalSessionCreate) originalSessionCreate(self);
}
void HookSessionDestroy(void* self) {
    void* expected = self;
    clientSession.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        captured.gameLoggedIn = false;
        captured.loginState = "LoggedOut";
    }
    if (originalSessionDestroy) originalSessionDestroy(self);
}
void HookLoginResult(void* self, int resultCode, int state, void* message) {
    clientSession.store(self, std::memory_order_release);
    CaptureTdsUser(self, state);
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        captured.gameLoggedIn = (state == 0);
    }
    if (originalLoginResult) originalLoginResult(self, resultCode, state, message);
}
void HookPanelStart(void* self) {
    loginPanel.store(self, std::memory_order_release);
    if (originalPanelStart) originalPanelStart(self);
    // OnStart merupakan callback managed pada Unity thread dan tersedia sebelum
    // GameApp_Update pada layar login, sehingga aman untuk bootstrap TestIndex.
    DisableTestIndexNowNative();
    CaptureIpInfo(self);
}
void HookZoneSuccess(void* self, int zone) {
    loginPanel.store(self, std::memory_order_release);
    if (originalZoneSuccess) originalZoneSuccess(self, zone);
    // Retry karena TEST_INDEX mungkin belum terbentuk saat LoginPanel.OnStart.
    DisableTestIndexNowNative();
    CaptureIpInfo(self);
}
void HookTimestamp(void* self, long long value) {
    if (originalTimestampSetter) originalTimestampSetter(self, value);
    std::lock_guard<std::mutex> lock(stateMutex);
    captured.loginTimestamp = value > 0 ? std::to_string(value) : std::string();
}
void HookBanEnd(void* self, long long value) {
    if (originalBanEndSetter) originalBanEndSetter(self, value);
    std::lock_guard<std::mutex> lock(stateMutex);
    captured.banEndAt = value > 0 ? std::to_string(value) : std::string();
}
void HookWhitelist(void* self, uint8_t value) {
    if (originalWhitelistSetter) originalWhitelistSetter(self, value);
    std::lock_guard<std::mutex> lock(stateMutex);
    captured.isWhitelisted = value != 0;
}
void HookBanReason(void* self, void* value) {
    if (originalBanReasonSetter) originalBanReasonSetter(self, value);
    std::lock_guard<std::mutex> lock(stateMutex);
    captured.banReason = value ? ReadMonoStringNative(value) : std::string();
}

bool HookMethod(void* klass, const char* name, int params, void* replacement, void** original) {
    // Retry EnsureHooks boleh terjadi setiap 5 detik. Hook yang sebelumnya sudah
    // berhasil tidak boleh dipasang ulang saat hook lain sempat belum tersedia.
    if (original && *original) return true;
    if (!klass || !_monoCompileMethod) return false;
    void* method = FindMethodInHierarchy(klass, name, params);
    void* address = method ? _monoCompileMethod(method) : nullptr;
    if (!address || DobbyHook(address, replacement, original) != RS_SUCCESS) return false;
    VirtualProtect(address, 0x4000);
    return true;
}

bool EnsureHooks() {
    if (hooksInstalled.load(std::memory_order_acquire)) return true;
    if (!_monoGetRootDomain || !_monoImageLoaded || !_monoClassFromName || !_monoCompileMethod) return false;
    void* game = _monoImageLoaded("XDTGameSystem");
    void* ui = _monoImageLoaded("XDTGameUI");
    void* data = _monoImageLoaded("XDTDataAndProtocol");
    if (!game || !ui || !data) return false;
    void* session = _monoClassFromName(game, "XDTGameSystem", "ClientSession");
    void* panel = _monoClassFromName(ui, "XDTGame.UI.Panel", "LoginPanel");
    void* result = _monoClassFromName(data, "Sazabi.Login.Shared", "LoginResult");
    bool a = HookMethod(session, "LoginResult", 3, (void*)HookLoginResult, (void**)&originalLoginResult);
    HookMethod(session, "OnCreate", 0, (void*)HookSessionCreate, (void**)&originalSessionCreate);
    HookMethod(session, "OnDestroy", 0, (void*)HookSessionDestroy, (void**)&originalSessionDestroy);
    bool b = HookMethod(panel, "OnStart", 0, (void*)HookPanelStart, (void**)&originalPanelStart);
    bool c = HookMethod(panel, "GetZoneServerInfoSuccess", 1, (void*)HookZoneSuccess, (void**)&originalZoneSuccess);
    HookMethod(result, "set_timestampUTC", 1, (void*)HookTimestamp, (void**)&originalTimestampSetter);
    HookMethod(result, "set_BanEndTimeStamp", 1, (void*)HookBanEnd, (void**)&originalBanEndSetter);
    HookMethod(result, "set_IsInWhiteList", 1, (void*)HookWhitelist, (void**)&originalWhitelistSetter);
    HookMethod(result, "set_BanReason", 1, (void*)HookBanReason, (void**)&originalBanReasonSetter);
    hooksInstalled.store(a && b && c, std::memory_order_release);
    return a && b && c;
}

Snapshot CollectSnapshot() {
    Snapshot out;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        out = captured;
    }
    if (!_monoGetRootDomain || !_monoImageLoaded || !_monoClassFromName ||
        !_monoClassGetVTable || !_monoFieldStaticGetValue) return out;
    void* domain = _monoGetRootDomain();
    if (!domain) return out;
    if (_monoThreadAttach) _monoThreadAttach(domain);
    void* image = _monoImageLoaded("XDTDataAndProtocol");
    void* klass = image ? _monoClassFromName(image, "XDTDataAndProtocol", "PlayerDataCenter") : nullptr;
    void* vtable = klass ? _monoClassGetVTable(domain, klass) : nullptr;
    void* field = klass ? FindFieldInHierarchy(klass, "_loginInfo") : nullptr;
    void* info = nullptr;
    if (vtable && field) _monoFieldStaticGetValue(vtable, field, &info);
    out.gameLoggedIn = info != nullptr;
    if (info) {
        out.userName = ReadString(info, "get_UserName", "<UserName>k__BackingField");
        out.playerId = ReadGuid(info, "get_PlayerId");
        if (out.userId.empty()) out.userId = out.playerId;
        long long shortId = ReadInt64(info, "get_ShortId");
        out.shortId = shortId ? std::to_string(shortId) : std::string();
        out.encodedShortId = ReadString(info, "get_GetEncodeShortId");
        out.isTapShortIdSet = ReadBool(info, "get_IsTapShortIdSet");
        out.isPushIdSet = ReadBool(info, "get_IsPushIdSet");
        if (out.loginState.empty()) out.loginState = "Ok";
    } else {
        out.userName.clear(); out.playerId.clear(); out.shortId.clear(); out.encodedShortId.clear();
        out.loginState = "LoggedOut";
    }
    return out;
}

std::string IdentityPath() {
    // iOS 版没有 java.io.File / getFilesDir，也没有 JNIEnv：安装身份直接落
    // Library/Application Support/SawitMod（SMPlatform::DataDir 会顺手建目录）。
    // Android 版语义（同一目录、同一文件名、同一读写格式）保持不变。
    const std::string dir = SMPlatform::DataDir();
    const std::string path = dir + "/SawitModControl.dat";
    return path;
}

bool LoadOrCreateIdentity() {
    std::ifstream input(IdentityPath());
    if (input) { std::getline(input, installationId); std::getline(input, secret); }
    if (installationId.size() == 32 && secret.size() == 64) return true;
    installationId = GenerateNonceNative();
    secret = GenerateNonceNative() + GenerateNonceNative();
    std::ofstream output(IdentityPath(), std::ios::trunc);
    if (!output) return false;
    output << installationId << '\n' << secret << '\n';
    return output.good();
}

std::string TelemetryJson(const Snapshot& s) {
    std::string license;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        license = modLicense;
    }
    const bool modLoggedIn = SesiAktif() && !license.empty();
    auto q = [](const std::string& v) { return "\"" + Escape(v) + "\""; };
    return std::string("{") +
        "\"modLoggedIn\":" + (modLoggedIn ? "true" : "false") +
        ",\"gameLoggedIn\":" + (s.gameLoggedIn ? "true" : "false") +
        ",\"licenseKey\":" + q(modLoggedIn ? license : "") +
        ",\"modVersion\":" + q(SAWIT_VERSION) + ",\"channel\":" + q(SAWIT_CHANNEL) +
        ",\"userId\":" + q(s.userId) + ",\"userName\":" + q(s.userName) +
        ",\"playerId\":" + q(s.playerId) + ",\"shortId\":" + q(s.shortId) +
        ",\"encodedShortId\":" + q(s.encodedShortId) + ",\"serialNumber\":" + q(s.serialNumber) +
        ",\"device\":" + q(s.device) + ",\"osType\":" + q(s.osType) +
        ",\"sceneProtocolVersion\":" + q(s.sceneProtocolVersion) + ",\"unionId\":" + q(s.unionId) +
        ",\"publishId\":" + q(s.publishId) + ",\"tapShortId\":" + q(s.tapShortId) +
        ",\"clientId\":" + q(s.clientId) + ",\"loginType\":" + q(s.loginType) +
        ",\"loginState\":" + q(s.loginState) + ",\"loginTimestamp\":" + q(s.loginTimestamp) +
        ",\"isTapShortIdSet\":" + (s.isTapShortIdSet ? "true" : "false") +
        ",\"isPushIdSet\":" + (s.isPushIdSet ? "true" : "false") +
        ",\"isWhitelisted\":" + (s.isWhitelisted ? "true" : "false") +
        ",\"banEndAt\":" + q(s.banEndAt) + ",\"banReason\":" + q(s.banReason) +
        ",\"continentCode\":" + q(s.continentCode) + ",\"isoCode\":" + q(s.isoCode) +
        ",\"timeZone\":" + q(s.timeZone) + ",\"city\":" + q(s.city) +
        ",\"ipErrorCode\":" + q(s.ipErrorCode) + "}";
}

bool Enroll() {
    std::string hwid = GetHWIDNative();
    if (hwid.empty() || hwid == "UNKNOWN_HWID") return false;
    std::string nonce = GenerateNonceNative();
    long long now = (long long)std::time(nullptr);
    std::ostringstream body;
    body << "{\"installationId\":\"" << installationId
         << "\",\"secretHash\":\"" << Sha256HexNativePublic(secret)
         << "\",\"hwid\":\"" << Escape(hwid)
         << "\",\"version\":\"" << SAWIT_VERSION
         << "\",\"channel\":\"" << SAWIT_CHANNEL
         << "\",\"timestamp\":" << now << ",\"nonce\":\"" << nonce << "\"}";
    // iOS：Android 版的 HttpPostNative 走 java.net.HttpURLConnection（jni 桥），
    // 这里换成 SMHttp::PostJson（NSURLSession）；URL 与 JSON 字段一字未改。
    AuthResult result = Virbox_NetAuthParse(
        SMHttp::PostJson("http://yslf.work/api/client-control/enroll", body.str()),
        nonce, hwid);
    return result.ok;
}

void SetAck(long long id, const char* status, const char* message) {
    std::lock_guard<std::mutex> lock(ackMutex);
    ackId.store(id);
    ackStatus = status;
    ackMessage = message;
}

void QueueCommand(const std::string& type, long long id) {
    int action = 0;
    if (type == "DISABLE_FEATURES") action = 1;
    else if (type == "GAME_LOGOUT") action = 2;
    else if (type == "GRACEFUL_EXIT") action = 3;
    else if (type == "UNINJECT_MOD") action = 4;
    else if (type == "REQUEST_STATUS") {
        lastRefreshMs.store(0, std::memory_order_release);
        SetAck(id, "executed", "Status refresh queued");
        return;
    } else {
        SetAck(id, "failed", "Unknown command");
        return;
    }
    // Hanya worker heartbeat yang memproduksi command. Publikasikan ID lebih dulu,
    // lalu action dengan release agar Unity thread tidak pernah mengambil action
    // baru dengan command ID yang masih nol.
    if (pendingAction.load(std::memory_order_acquire) != 0) {
        SetAck(id, "failed", "Another command is pending");
        return;
    }
    pendingCommandId.store(id, std::memory_order_relaxed);
    pendingAction.store(action, std::memory_order_release);
}

bool Heartbeat() {
    Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        snapshot = captured;
    }
    const std::string telemetry = TelemetryJson(snapshot);
    const std::string hwid = GetHWIDNative();
    if (hwid.empty() || hwid == "UNKNOWN_HWID") return false;
    const std::string nonce = GenerateNonceNative();
    const long long now = (long long)std::time(nullptr);
    long long ack = 0;
    std::string status, message;
    {
        std::lock_guard<std::mutex> lock(ackMutex);
        ack = ackId.load(); status = ackStatus; message = ackMessage;
    }
    const std::string canonical = installationId + "|" + std::to_string(now) + "|" + nonce + "|" +
        Sha256HexNativePublic(telemetry) + "|" + std::to_string(ack) + "|" + status;
    const std::string proof = HmacSha256HexNative(Sha256HexNativePublic(secret), canonical);
    std::ostringstream body;
    body << "{\"installationId\":\"" << installationId << "\",\"hwid\":\"" << Escape(hwid)
         << "\",\"timestamp\":" << now << ",\"nonce\":\"" << nonce
         << "\",\"telemetry\":" << telemetry << ",\"ackId\":" << ack
         << ",\"ackStatus\":\"" << Escape(status) << "\",\"ackMessage\":\"" << Escape(message)
         << "\",\"proof\":\"" << proof << "\"}";
    // iOS：同上，Java 网络桥换成 SMHttp::PostJson，endpoint 不变。
    AuthResult result = Virbox_NetAuthParse(
        SMHttp::PostJson("http://yslf.work/api/client-control/heartbeat", body.str()),
        nonce, hwid);
    if (!result.ok || result.data.empty()) return false;
    if (ack > 0) {
        std::lock_guard<std::mutex> lock(ackMutex);
        if (ackId.load() == ack) {
            ackId.store(0); ackStatus.clear(); ackMessage.clear();
            int deferred = deferredTerminalAction.exchange(0);
            if (deferred == 3) pendingAction.store(5);
            else if (deferred == 4) pendingAction.store(6);
        }
    }
    std::string plain = DecryptPayload(result.data);
    std::string type = JsonString(plain, "command_type");
    long long id = JsonInteger(plain, "id");
    if (!type.empty() && id > 0) QueueCommand(type, id);
    return true;
}

void WorkerLoop() {
    STARTUP_TRACE("A01 control worker entered");
    if (!LoadOrCreateIdentity()) {
        STARTUP_TRACE("A02 identity unavailable; worker stopped");
        running.store(false);
        return;
    }
    STARTUP_TRACE("A02 identity ready");
    bool enrolled = false;
    bool firstAttempt = true;
    while (running.load(std::memory_order_acquire)) {
        if (!enrolled) {
            if (firstAttempt) STARTUP_TRACE("A03 first enroll begin");
            enrolled = Enroll();
            if (firstAttempt) STARTUP_TRACE(enrolled ? "A04 first enroll success" : "A04 first enroll failed");
            firstAttempt = false;
        } else if (!Heartbeat()) enrolled = false;
        const int seconds = enrolled ? 30 : 10;
        for (int i = 0; i < seconds * 10 && running.load(); ++i) usleep(100000);
    }
}
} // namespace

bool InitializeAfterMono() {
#if !SAWIT_CLIENT_CONTROL_ENABLED
    return false;
#else
    // Jangan memasang detour pada method JIT Mono dari startup worker. Percobaan
    // sebelumnya menyebabkan SIGSEGV di UnityMain ketika callback managed masuk.
    // Enrollment/device telemetry tetap dapat hidup tanpa hook akun.
    Start();
    return running.load(std::memory_order_acquire);
#endif
}

void PumpUnityCommands() {
#if !SAWIT_CLIENT_CONTROL_ENABLED
    return;
#endif
    if (!running.load(std::memory_order_acquire)) return;

    long long now = NowMs();
    if (now - lastRefreshMs.load(std::memory_order_acquire) >= 5000) {
        // Hook callback akun Mono dinonaktifkan: signature/trampoline versi game
        // ini belum aman. Snapshot statis tetap dibaca pada Unity thread.
        Snapshot fresh = CollectSnapshot();
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            captured = std::move(fresh);
        }
        lastRefreshMs.store(now, std::memory_order_release);
    }

    int action = pendingAction.exchange(0, std::memory_order_acq_rel);
    if (!action) return;
    long long id = pendingCommandId.exchange(0, std::memory_order_acq_rel);
    if (action == 1) {
        Virbox_WipeAllFeatures();
        SetAck(id, "executed", "All mod features disabled");
    } else if (action == 2) {
        void* session = clientSession.load(std::memory_order_acquire);
        void* image = _monoImageLoaded ? _monoImageLoaded("XDTGameSystem") : nullptr;
        void* klass = image && _monoClassFromName ? _monoClassFromName(image, "XDTGameSystem", "ClientSession") : nullptr;
        void* method = session && klass ? FindMethodInHierarchy(klass, "Logout", 0) : nullptr;
        void* exception = nullptr;
        if (method && _monoRuntimeInvoke) _monoRuntimeInvoke(method, session, nullptr, &exception);
        SetAck(id, method && !exception ? "executed" : "failed",
               method && !exception ? "Game logout requested" : "ClientSession unavailable");
    } else if (action == 3) {
        deferredTerminalAction.store(3);
        SetAck(id, "executed", "Graceful exit accepted");
    } else if (action == 4) {
        deferredTerminalAction.store(4);
        SetAck(id, "executed", "Uninject accepted");
    } else if (action == 5) {
        TriggerQuit("REMOTE_EXIT", "Remote control request");
    } else if (action == 6) {
        // iOS tidak mendukung dlclose aman untuk library yang sedang aktif.
        // Efek uninject diterapkan secara aman setelah ACK: fitur dan sesi dimatikan.
        Virbox_WipeAllFeatures();
        StopLicenseHeartbeat();
        AkhiriSesiLokal();
        ClearModSession();
    }
}

void Start() {
#if !SAWIT_CLIENT_CONTROL_ENABLED
    return;
#endif
    STARTUP_TRACE("A00 control Start entered");
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        STARTUP_TRACE("A00 control already running");
        return;
    }
    std::thread(WorkerLoop).detach();
    STARTUP_TRACE("A00 control worker launched");
}

void Stop() {
    running.store(false, std::memory_order_release);
}

void SetModSession(const std::string& license) {
#if !SAWIT_CLIENT_CONTROL_ENABLED
    (void)license;
    return;
#endif
    std::lock_guard<std::mutex> lock(stateMutex);
    modLicense = license;
}

void ClearModSession() {
#if !SAWIT_CLIENT_CONTROL_ENABLED
    return;
#endif
    std::lock_guard<std::mutex> lock(stateMutex);
    modLicense.clear();
}
} // namespace ClientControlAgent
