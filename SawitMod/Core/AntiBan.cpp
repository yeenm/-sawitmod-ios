// ============================================================================
//  AntiBan.cpp — iOS 端「过检测 / 防封」
// ----------------------------------------------------------------------------
//  原理：Android 版用 libil2cpp.so 的原生 RVA 去打 Themis/TapSDK 补丁；iOS 的
//  UnityFramework 是另一份编译产物，RVA 不能复用。这里改成**按托管类名/方法名
//  在运行时解析**，然后把方法体头两条指令改写成「立即返回」：
//
//      void  : RET                  (D65F03C0)
//      false : MOV W0,#0 ; RET      (52800000, D65F03C0)
//      null  : MOV X0,#0 ; RET      (D2800000, D65F03C0)
//
//  ⚠️ 关键点：il2cpp 的程序集是**按需加载**的 —— 刚进游戏时 ThemisSDK/Logan/
//  XDCore 这些 image 可能还没进 domain，`il2cpp_class_from_name` 会返回 null。
//  所以补丁不能只在启动时试一次：这里维护一张 pending 表，由渲染线程每秒
//  重试（SMAntiBanTick），直到全部命中或重试次数耗尽。菜单设置页显示
//  patched/pending 计数，用户能直接看到拦截是否生效。
//
//  拦截清单（全部用 dump.cs 的类/方法/参数个数逐一核对过）：
//    1. Themis / TapSDK 检测与上报（含 TapThemisAgentIOS 的 native 上报桥）
//    2. Logan 日志上传
//    3. XD.SDK.Report 举报/证据上传
//    4. BugReporter 崩溃上报
//    5. XDPerf 位置/UID/设备指纹埋点、模拟器检测
//    6. LogSystem 日志提交/截图开关、CodeCheck Mono 程序集 MD5 校验
//  另外用类名解析装 GameApp.Update 帧泵（替代 OFF_GAMEAPP_UPDATE）。
// ============================================================================

#include "Core/AntiBan.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "Core/FramePump.h"
#include "Core/State.h"
#include "IL2CPP/Il2CppApi.h"
#include "IL2CPP/MonoCompat.h"
#include "Includes/Dobby/dobby.h"
#include "Includes/Logger.h"
#include "Includes/OSCache.h"
#include "Includes/obfuscate.h"
#include "ModUtils.h"

namespace {

using namespace SMIL2CPP;

enum PatchMode {
    kPatchVoid = 0,   // RET
    kPatchFalse,      // return false
    kPatchTrue,       // return true
    kPatchIntZero,    // return 0 (int)
    kPatchNull,       // return null (object/string/task)
};

// 补丁类型：单参数个数 / 多参数个数候选（重载）/ 字段写 / 静态方法调用
enum Kind {
    kKindPatch = 0,
    kKindMulti,
    kKindStaticBool,
    kKindInstanceBool,
    kKindInvokeBool,
};

struct Pending {
    Kind kind;
    const char *ns;
    const char *cls;
    const char *name;    // 方法名；静态 bool 时为字段名
    const char *extra;   // 实例 bool：单例 getter 名
    int argc;
    const int *argcs;
    int argcCount;
    PatchMode mode;
    bool bvalue;
    // 运行时状态
    bool done;
    int tries;
};

// ---- 运行期统计 ----
std::mutex g_mtx;
std::vector<Pending> g_pending;
bool g_listsReady = false;
std::once_flag g_initOnce;
int g_applied = 0;
int g_pendingLeft = 0;
bool g_frameHookOk = false;
int g_frameTries = 0;
std::string g_status = "init";

constexpr int kMaxTries = 80;   // 每秒左右一次 → 约 1 分 20 秒窗口
constexpr int kFrameMaxTries = 30;

void AttachThread() {
    if (_monoGetRootDomain && _monoThreadAttach) {
        _monoThreadAttach(_monoGetRootDomain());
    }
}

// 跨镜像找类：ns 传 "" 也能找到全局命名空间的类（shim 会扫全部 image）。
void *FindClassByName(const char *ns, const char *name) {
    if (!name) return nullptr;
    return _monoClassFromName(nullptr, ns ? ns : "", name);
}

void *FindMethodEx(void *klass, const char *name, int argc) {
    if (!klass || !name) return nullptr;
    void *m = _monoClassGetMethodFromName(klass, name, argc);
    if (!m) m = FindMethodInHierarchy(klass, name, argc);
    return m;
}

bool WritePatch(void *code, PatchMode mode) {
    uint32_t payload[2] = {0, 0xD65F03C0};
    int words = 1;
    switch (mode) {
        case kPatchVoid:
            payload[0] = 0xD65F03C0;  // RET
            words = 1;
            break;
        case kPatchFalse:
        case kPatchIntZero:
            payload[0] = 0x52800000;  // MOV W0, #0
            payload[1] = 0xD65F03C0;
            words = 2;
            break;
        case kPatchTrue:
            payload[0] = 0x52800020;  // MOV W0, #1
            payload[1] = 0xD65F03C0;
            words = 2;
            break;
        case kPatchNull:
            payload[0] = 0xD2800000;  // MOV X0, #0
            payload[1] = 0xD65F03C0;
            words = 2;
            break;
    }
    const size_t bytes = sizeof(uint32_t) * (size_t)words;
    if (!VirtualProtect(code, bytes)) return false;
    memcpy(code, payload, bytes);
    sys_icache_invalidate(code, bytes);
    return true;
}

bool TryPatchMethod(const char *ns, const char *cls, const char *method, int argc, PatchMode mode) {
    void *klass = FindClassByName(ns, cls);
    if (!klass) return false;
    void *m = FindMethodEx(klass, method, argc);
    if (!m) return false;
    void *code = _monoCompileMethod ? _monoCompileMethod(m) : nullptr;
    if (!code) return false;
    return WritePatch(code, mode);
}

void SetStaticBool(const char *ns, const char *cls, const char *field, bool value) {
    void *klass = FindClassByName(ns, cls);
    if (!klass || !field) return;
    void *f = _monoClassGetFieldFromName(klass, field);
    if (!f) return;
    const API &a = Get();
    if (!a.field_static_set_value) return;
    bool v = value;
    a.field_static_set_value(f, &v);
}

// 通过 static get_Instance() 拿到单例，再写它的实例字段。
void SetInstanceBool(const char *ns, const char *cls, const char *getter, int getterArgc,
                     const char *fieldName, bool value) {
    void *klass = FindClassByName(ns, cls);
    if (!klass) return;
    void *gm = FindMethodEx(klass, getter, getterArgc);
    void *f = _monoClassGetFieldFromName(klass, fieldName);
    if (!gm || !f || !_monoRuntimeInvoke || !_monoFieldSetValue) return;
    void *exc = nullptr;
    void *inst = _monoRuntimeInvoke(gm, nullptr, nullptr, &exc);
    if (!inst) return;
    bool v = value;
    _monoFieldSetValue(inst, f, &v);
}

bool InvokeStaticVoidBool(const char *ns, const char *cls, const char *method, bool arg) {
    void *klass = FindClassByName(ns, cls);
    if (!klass) return false;
    void *m = FindMethodEx(klass, method, 1);
    if (!m) return false;
    const API &a = Get();
    if (!a.runtime_invoke) return false;
    bool v = arg;
    void *params[1] = {&v};
    void *exc = nullptr;
    a.runtime_invoke(m, nullptr, params, &exc);
    return exc == nullptr;
}

// ---------------------------------------------------------------------------
//  pending 表
// ---------------------------------------------------------------------------
#define P_PATCH(ns, cls, meth, argc, mode) \
    Pending{kKindPatch, ns, cls, meth, nullptr, argc, nullptr, 0, mode, false, false, 0}
#define P_MULTI(ns, cls, meth, arr, mode) \
    Pending{kKindMulti, ns, cls, meth, nullptr, 0, arr, (int)(sizeof(arr) / sizeof(int)), mode, false, false, 0}
#define P_SBOOL(ns, cls, field, val) \
    Pending{kKindStaticBool, ns, cls, field, nullptr, 0, nullptr, 0, kPatchVoid, val, false, 0}
#define P_IBOOL(ns, cls, getter, field, val) \
    Pending{kKindInstanceBool, ns, cls, field, getter, 0, nullptr, 0, kPatchVoid, val, false, 0}
#define P_INVOKE(ns, cls, meth, val) \
    Pending{kKindInvokeBool, ns, cls, meth, nullptr, 1, nullptr, 0, kPatchVoid, val, false, 0}

void BuildPendingList() {
    static const int aLogan[] = {1, 2};
    static const int aOne[] = {1};
    static const int aMgr[] = {1, 2, 3};

    g_pending = {
        // ===== Themis 托管包装（TapTap.Themis / TapThemis）=====
        P_PATCH("TapTap.Themis", "TapThemis", "ReportException", 3, kPatchVoid),
        P_PATCH("TapTap.Themis", "TapThemis", "ReportLogError", 3, kPatchVoid),
        P_PATCH("TapTap.Themis", "TapThemis", "ReportCustomException", 4, kPatchVoid),
        P_PATCH("TapTap.Themis", "TapThemis", "ReportCustomExceptionEx", 6, kPatchVoid),
        P_PATCH("TapTap.Themis", "TapThemis", "AddCustomField", 2, kPatchVoid),
        P_PATCH("TapTap.Themis", "TapThemis", "TMCR", 2, kPatchVoid),
        P_PATCH("TapTap.Themis", "TapThemis", "U3d_ConfigAutoQuitApplication", 1, kPatchVoid),
        // 检测状态回调：不让游戏/模组层收到"环境不安全"的状态
        P_PATCH("TapTap.Themis", "TapThemisAgentIOS", "_onThemisStateCB", 2, kPatchVoid),
        // TapThemisAgentIOS 的 native 上报桥（ThemisSDK 真正的上报入口）
        P_PATCH("TapTap.Themis", "TapThemisAgentIOS", "_lib_report_exception_with_type", 5, kPatchVoid),
        P_PATCH("TapTap.Themis", "TapThemisAgentIOS", "_lib_report_custom_exception_ex", 6, kPatchVoid),
        P_PATCH("TapTap.Themis", "TapThemisAgentIOS", "_add_custom_field", 2, kPatchVoid),
        P_PATCH("TapTap.Themis", "TapThemisAgentIOS", "_lib_enable_debug_mode", 1, kPatchVoid),
        P_PATCH("TapTap.Themis", "TapThemisCallBackImp", "onHandleThemisState", 2, kPatchVoid),
        // ===== 引擎侧 Themis 管理类（全局命名空间）=====
        P_PATCH("", "ThemisSDKManager", "ReportException", 3, kPatchVoid),
        P_PATCH("", "ThemisSDKManager", "ReportLogError", 3, kPatchVoid),
        P_PATCH("", "ThemisSDKManager", "ReportCustomException", 4, kPatchVoid),
        P_PATCH("", "ThemisSDKManager", "ReportCustomExceptionEx", 6, kPatchVoid),
        P_PATCH("", "ThemisSDKManager", "AddCustomPlayer", 2, kPatchVoid),
        // ===== TapDB 的 Themis 工具类 =====
        P_PATCH("TapTap.TapDB.Mobile", "ThemisUtil", "ExceptionReport", 2, kPatchVoid),
        P_PATCH("TapTap.TapDB.Mobile", "ThemisUtil", "ReportException", 4, kPatchVoid),
        P_PATCH("TapTap.TapDB.Mobile", "ThemisUtil", "U3d_ConfigAutoQuitApplication", 1, kPatchVoid),
        // ===== Logan 日志上传 =====
        P_MULTI("", "Logan", "UploadLogs", aLogan, kPatchVoid),
        P_MULTI("", "LoganBridge", "UploadLogs", aOne, kPatchVoid),
        P_MULTI("", "LoganBridge", "UpLoadLogs", aOne, kPatchVoid),
        P_MULTI("", "LoganBridge", "uploadLogsByDay", aOne, kPatchVoid),
        P_MULTI("", "LoganManager", "UploadLogs", aMgr, kPatchVoid),
        // ===== XD.SDK.Report 举报 / 证据上传 =====
        P_PATCH("XD.SDK.Report", "XDGReport", "Report", 2, kPatchVoid),
        P_PATCH("XD.SDK.Report", "XDReportImpl", "Report", 2, kPatchVoid),
        P_PATCH("XD.SDK.Report", "XDReportImpl", "ReportAsync", 3, kPatchVoid),
        P_PATCH("XD.SDK.Report", "XDReportImpl", "UploadEvidence", 3, kPatchVoid),
        P_PATCH("XD.SDK.Report", "XDReportImpl", "UploadEvidenceBySignedOssUrl", 4, kPatchVoid),
        // ===== 崩溃上报 =====
        P_PATCH("ScriptsRefactory.BaseService.BugReporter", "BugReporter", "ReportError", 3, kPatchVoid),
        // ===== XDPerf 埋点（位置/UID/设备指纹/模拟器）=====
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "PostPosition", 3, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "ReportUid", 1, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "ReportXDPerfInfos", 0, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "ReportQuality", 2, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "ReportCustomQuality", 1, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "ReportTargetFrameRate", 1, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "PostNetworkLatency", 1, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "ReportSceneName", 1, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "ReportChannel", 1, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "ReportServerId", 1, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "ReportLanguage", 1, kPatchVoid),
        P_PATCH("XDPlugin.XDKWPerf", "XDPerf", "GetEmulatorType", 0, kPatchIntZero),
        // ===== 数据字段类 =====
        P_SBOOL("MonoGame.ScriptFramework", "CodeCheck", "IsMD5Match", true),
        P_IBOOL("XDFramework.Core", "LogSystem", "get_Instance", "canSubmit", false),
        P_IBOOL("XDFramework.Core", "LogSystem", "get_Instance", "captureScreenshot", false),
        P_MULTI("MonoGame", "TypeRegister", "CheckMD5", aOne, kPatchVoid),
        // ===== 开关调用（失败不致命）=====
        P_INVOKE("", "ThemisSDKManager", "SetSwitch", false),
        P_INVOKE("TapTap.Themis", "TapThemis", "U3d_ConfigAutoQuitApplication", false),
        P_INVOKE("TapTap.TapDB.Mobile", "ThemisUtil", "U3d_ConfigAutoQuitApplication", false),
    };
    g_applied = 0;
    g_pendingLeft = (int)g_pending.size();
}

// 尝试执行一条 pending，返回是否完成（成功）。
bool TryOne(Pending &p) {
    switch (p.kind) {
        case kKindPatch:
            return TryPatchMethod(p.ns, p.cls, p.name, p.argc, p.mode);
        case kKindMulti:
            for (int i = 0; i < p.argcCount; ++i) {
                if (TryPatchMethod(p.ns, p.cls, p.name, p.argcs[i], p.mode)) return true;
            }
            return false;
        case kKindStaticBool:
            if (!FindClassByName(p.ns, p.cls)) return false;
            SetStaticBool(p.ns, p.cls, p.name, p.bvalue);
            return true;
        case kKindInstanceBool:
            if (!FindClassByName(p.ns, p.cls)) return false;
            SetInstanceBool(p.ns, p.cls, p.extra, 0, p.name, p.bvalue);
            return true;
        case kKindInvokeBool:
            return InvokeStaticVoidBool(p.ns, p.cls, p.name, p.bvalue);
    }
    return false;
}

void ApplyPendingLocked() {
    for (Pending &p : g_pending) {
        if (p.done) continue;
        if (p.tries >= kMaxTries) continue;
        ++p.tries;
        if (TryOne(p)) {
            p.done = true;
            ++g_applied;
            LOGI(OBFUSCATE("[AntiBan] applied %s.%s.%s"), p.ns, p.cls, p.name);
        }
    }
    g_pendingLeft = 0;
    for (const Pending &p : g_pending) {
        if (!p.done && p.tries < kMaxTries) ++g_pendingLeft;
    }
}

void TryFrameHookLocked() {
    if (g_frameHookOk) return;
    if (g_frameTries++ >= kFrameMaxTries) return;
    if (!_monoCompileMethod) return;
    void *klass = FindClassByName("XD.Unity.Game", "GameApp");
    if (!klass) klass = FindClassByName("", "GameApp");
    if (!klass) return;
    void *method = FindMethodEx(klass, "Update", 0);
    if (!method) method = FindMethodEx(klass, "LateUpdate", 0);
    if (!method) return;
    void *target = _monoCompileMethod(method);
    if (!target) return;
    const int rc = DobbyHook(target, (void *)SMGameAppUpdate, (void **)&SMOldGameAppUpdate);
    g_frameHookOk = (rc == 0) && SMOldGameAppUpdate != nullptr;
    LOGI(OBFUSCATE("[AntiBan] GameApp.Update hook %s"), g_frameHookOk ? "ok" : "failed");
}

void RefreshStatusLocked() {
    char buf[192];
    snprintf(buf, sizeof(buf), "patched=%d pending=%d frame=%s", g_applied, g_pendingLeft,
             g_frameHookOk ? "on" : "off");
    g_status = buf;
}

// Bootstrap 首次调用：建表 + 一轮尝试。
void InstallOnce() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_listsReady) {
        BuildPendingList();
        g_listsReady = true;
    }
    AttachThread();
    ApplyPendingLocked();
    TryFrameHookLocked();
    RefreshStatusLocked();
    g_isAntiCheatActive = true;
    LOGI(OBFUSCATE("[AntiBan] first pass: %s"), g_status.c_str());
}

}  // namespace

// ============================================================================
//  对外接口
// ============================================================================
bool SMAntiBanHookFramePump() {
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (!g_listsReady) {
            BuildPendingList();
            g_listsReady = true;
        }
        TryFrameHookLocked();
    }
    return g_frameHookOk;
}

int SMAntiBanInstall() {
    std::call_once(g_initOnce, []() { InstallOnce(); });
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_applied;
}

// 渲染线程每秒调用：程序集是懒加载的，启动时没命中的补丁要等 image 上线再打。
void SMAntiBanTick() {
    static std::atomic<long long> lastMs{0};
    if (!g_listsReady) return;

    const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
    long long prev = lastMs.load(std::memory_order_relaxed);
    if (nowMs - prev < 1200) return;
    if (!lastMs.compare_exchange_strong(prev, nowMs, std::memory_order_relaxed)) return;

    std::lock_guard<std::mutex> lk(g_mtx);
    bool anyPending = false;
    for (const Pending &p : g_pending) {
        if (!p.done && p.tries < kMaxTries) {
            anyPending = true;
            break;
        }
    }
    if (!anyPending && g_frameHookOk) return;

    AttachThread();
    ApplyPendingLocked();
    TryFrameHookLocked();
    RefreshStatusLocked();
}

std::string SMAntiBanStatus() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_status.empty()) return std::string("init");
    if (g_pendingLeft > 0) {
        // 把仍未命中的前几个目标名带出来，方便实机截图定位。
        std::string extra;
        for (const Pending &p : g_pending) {
            if (p.done || p.tries < kMaxTries) continue;
            extra += p.cls;
            extra += ".";
            extra += p.name;
            extra += " ";
            if (extra.size() > 90) break;
        }
        if (!extra.empty()) return g_status + " miss:" + extra;
    }
    return g_status;
}
