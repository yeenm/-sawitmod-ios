// ============================================================================
//  AntiBan.cpp — iOS 端「过检测 / 防封」
// ----------------------------------------------------------------------------
//  原理：Android 版用 libil2cpp.so 的原生 RVA 去打 Themis/TapSDK 补丁；iOS 的
//  UnityFramework 是另一份编译产物，RVA 不能复用。这里改成**按托管类名/方法名
//  在运行时解析**，然后把方法体头两条指令改写成「立即返回」：
//
//      void  : RET                  (D65F03C0)
//      false : MOV W0,#0 ; RET      (52800000, D65F03C0)
//      true  : MOV W0,#1 ; RET
//      null  : MOV X0,#0 ; RET      (D2800000, D65F03C0)
//
//  这样既不碰游戏原生代码段里的固定偏移，也不会因为游戏小版本更新而错挂：
//  方法找不到就记一个 miss，其他补丁照常上。
//
//  拦截清单（全部来自 iOS dump.cs 的类/方法签名核对）：
//    1. Themis / TapSDK 检测与上报
//    2. Logan 日志上传
//    3. XD.SDK.Report 举报/证据上传
//    4. UploadManager / _UploadSystem 通用上传
//    5. BugReporter.ReportError
//  另外用类名解析装 GameApp.Update 帧泵（替代 OFF_GAMEAPP_UPDATE）。
// ============================================================================

#include "Core/AntiBan.h"

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
#include "Includes/obfuscate.h"
#include "Includes/OSCache.h"
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

struct Target {
    const char *ns;
    const char *cls;
    const char *method;
    int argc;
    PatchMode mode;
};

// ---- 运行期统计 ----
std::once_flag g_installOnce;
int g_patched = 0;
int g_missed = 0;
bool g_frameHookOk = false;
std::string g_status = "未安装";

void AttachThread() {
    if (_monoGetRootDomain && _monoThreadAttach) {
        _monoThreadAttach(_monoGetRootDomain());
    }
}

// 跨镜像找类：ns 传 "" 也能找到全局命名空间的类（shim 里会扫全部 image）。
void *FindClassByName(const char *ns, const char *name) {
    if (!name) return nullptr;
    void *klass = _monoClassFromName(nullptr, ns ? ns : "", name);
    return klass;
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
            payload[0] = 0x52800000;  // MOV W0, #0
            payload[1] = 0xD65F03C0;
            words = 2;
            break;
        case kPatchIntZero:
            payload[0] = 0x52800000;
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

bool PatchOne(const Target &t) {
    void *klass = FindClassByName(t.ns, t.cls);
    if (!klass) {
        LOGI(OBFUSCATE("[AntiBan] miss class %s.%s"), t.ns, t.cls);
        return false;
    }
    void *method = FindMethodEx(klass, t.method, t.argc);
    if (!method) {
        LOGI(OBFUSCATE("[AntiBan] miss method %s.%s.%s(%d)"), t.ns, t.cls, t.method, t.argc);
        return false;
    }
    void *code = _monoCompileMethod ? _monoCompileMethod(method) : nullptr;
    if (!code) return false;
    const bool ok = WritePatch(code, t.mode);
    if (ok) {
        LOGI(OBFUSCATE("[AntiBan] patched %s.%s.%s(%d) -> mode %d"),
             t.ns, t.cls, t.method, t.argc, (int)t.mode);
    }
    return ok;
}

// 一个方法可能有多个重载（例如 UploadLogs 有 1/2 个参数两种），逐个 argc 尝试。
bool PatchOverloads(const char *ns, const char *cls, const char *method,
                    const std::vector<int> &argcs, PatchMode mode) {
    for (int argc : argcs) {
        Target t{ns, cls, method, argc, mode};
        if (PatchOne(t)) {
            ++g_patched;
            return true;
        }
    }
    ++g_missed;
    return false;
}

void SetStaticBool(const char *ns, const char *cls, const char *field, bool value) {
    void *klass = FindClassByName(ns, cls);
    if (!klass || !field) return;
    void *f = _monoClassGetFieldFromName(klass, field);
    if (!f) {
        LOGI(OBFUSCATE("[AntiBan] miss field %s.%s.%s"), ns, cls, field);
        return;
    }
    const API &a = Get();
    if (!a.field_static_set_value) return;
    bool v = value;
    a.field_static_set_value(f, &v);
    LOGI(OBFUSCATE("[AntiBan] %s.%s.%s = %d"), ns, cls, field, (int)value);
}

// 调用一个 static void 方法（只支持 0/1 个 bool/int 参数，够用）。
bool InvokeStaticVoid(const char *ns, const char *cls, const char *method,
                      int argc, bool boolArg) {
    void *klass = FindClassByName(ns, cls);
    if (!klass) return false;
    void *m = FindMethodEx(klass, method, argc);
    if (!m) return false;
    const API &a = Get();
    if (!a.runtime_invoke) return false;
    void *exc = nullptr;
    if (argc == 0) {
        a.runtime_invoke(m, nullptr, nullptr, &exc);
    } else {
        bool v = boolArg;
        void *params[1] = {&v};
        a.runtime_invoke(m, nullptr, params, &exc);
    }
    LOGI(OBFUSCATE("[AntiBan] invoke %s.%s.%s(%d) exc=%p"), ns, cls, method, argc, exc);
    return exc == nullptr;
}

// ---------------------------------------------------------------------------
//  补丁清单
// ---------------------------------------------------------------------------
void ApplyThemisBypass() {
    // 1) Themis 托管包装（TapTap.Themis）：检测结果上报 / 玩家信息上报 / 输入特征采集
    static const Target kThemisTargets[] = {
        {"TapTap.Themis", "TapThemis", "ReportException", 3, kPatchVoid},
        {"TapTap.Themis", "TapThemis", "ReportLogError", 3, kPatchVoid},
        {"TapTap.Themis", "TapThemis", "ReportCustomException", 4, kPatchVoid},
        {"TapTap.Themis", "TapThemis", "ReportCustomExceptionEx", 6, kPatchVoid},
        {"TapTap.Themis", "TapThemis", "AddCustomField", 2, kPatchVoid},
        {"TapTap.Themis", "TapThemis", "TMCR", 2, kPatchVoid},
        {"TapTap.Themis", "TapThemis", "U3d_ConfigAutoQuitApplication", 1, kPatchVoid},
        // 检测状态回调：不让游戏/模组层收到"环境不安全"的状态
        {"TapTap.Themis", "TapThemisAgentIOS", "_onThemisStateCB", 2, kPatchVoid},
        // TapThemisAgentIOS 的 native 桥（ThemisSDK 真正的上报入口）
        {"TapTap.Themis", "TapThemisAgentIOS", "_lib_report_exception_with_type", 5, kPatchVoid},
        {"TapTap.Themis", "TapThemisAgentIOS", "_lib_report_custom_exception_ex", 6, kPatchVoid},
        {"TapTap.Themis", "TapThemisAgentIOS", "_add_custom_field", 2, kPatchVoid},
        {"TapTap.Themis", "TapThemisAgentIOS", "_lib_enable_debug_mode", 1, kPatchVoid},
        {"TapTap.Themis", "TapThemisCallBackImp", "onHandleThemisState", 2, kPatchVoid},
        // 2) 引擎侧 Themis 管理类（全局命名空间）
        {"", "ThemisSDKManager", "ReportException", 3, kPatchVoid},
        {"", "ThemisSDKManager", "ReportLogError", 3, kPatchVoid},
        {"", "ThemisSDKManager", "ReportCustomException", 4, kPatchVoid},
        {"", "ThemisSDKManager", "ReportCustomExceptionEx", 6, kPatchVoid},
        {"", "ThemisSDKManager", "AddCustomPlayer", 2, kPatchVoid},
        // 3) TapDB 的 Themis 工具类
        {"TapTap.TapDB.Mobile", "ThemisUtil", "ExceptionReport", 2, kPatchVoid},
        {"TapTap.TapDB.Mobile", "ThemisUtil", "ReportException", 4, kPatchVoid},
        {"TapTap.TapDB.Mobile", "ThemisUtil", "U3d_ConfigAutoQuitApplication", 1, kPatchVoid},
    };
    for (const Target &t : kThemisTargets) {
        if (PatchOne(t)) ++g_patched; else ++g_missed;
    }

    // 4) 关掉 Themis 自己的开关 + 不让它初始化完就自动上传
    SetStaticBool("TapTap.Themis", "TapThemis", "Open", false);
    InvokeStaticVoid("", "ThemisSDKManager", "SetSwitch", 1, false);
    InvokeStaticVoid("TapTap.Themis", "TapThemis", "U3d_ConfigAutoQuitApplication", 1, false);
    InvokeStaticVoid("TapTap.TapDB.Mobile", "ThemisUtil", "U3d_ConfigAutoQuitApplication", 1, false);
}

void ApplyReportBlock() {
    // ---- Logan 日志上传 ----
    PatchOverloads("", "Logan", "UploadLogs", {1, 2}, kPatchVoid);
    PatchOverloads("", "LoganBridge", "UploadLogs", {1}, kPatchVoid);
    PatchOverloads("", "LoganBridge", "UpLoadLogs", {1}, kPatchVoid);
    PatchOverloads("", "LoganBridge", "uploadLogsByDay", {1}, kPatchVoid);
    PatchOverloads("", "LoganManager", "UploadLogs", {1, 2, 3}, kPatchVoid);

    // ---- XD.SDK.Report 举报 / 证据上传 ----
    PatchOverloads("XD.SDK.Report", "XDGReport", "Report", {2}, kPatchVoid);
    PatchOverloads("XD.SDK.Report", "XDReportImpl", "Report", {2}, kPatchVoid);
    PatchOverloads("XD.SDK.Report", "XDReportImpl", "ReportAsync", {3}, kPatchVoid);
    PatchOverloads("XD.SDK.Report", "XDReportImpl", "UploadEvidence", {3}, kPatchVoid);
    PatchOverloads("XD.SDK.Report", "XDReportImpl", "UploadEvidenceBySignedOssUrl", {4}, kPatchVoid);

    // ---- 崩溃/异常上报 ----
    PatchOverloads("ScriptsRefactory.BaseService.BugReporter", "BugReporter", "ReportError", {3},
                   kPatchVoid);
}


// 通过 static get_Instance() 拿到单例，再写它的实例字段。
void SetInstanceBool(const char *ns, const char *cls, const char *getter, int getterArgc,
                     const char *fieldName, bool value) {
    void *klass = FindClassByName(ns, cls);
    if (!klass) {
        LOGI(OBFUSCATE("[AntiBan] miss class %s.%s"), ns, cls);
        return;
    }
    void *gm = FindMethodEx(klass, getter, getterArgc);
    void *f = _monoClassGetFieldFromName(klass, fieldName);
    if (!gm || !f || !_monoRuntimeInvoke || !_monoFieldSetValue) {
        LOGI(OBFUSCATE("[AntiBan] miss %s.%s instance getter/field"), cls, fieldName);
        return;
    }
    void *exc = nullptr;
    void *inst = _monoRuntimeInvoke(gm, nullptr, nullptr, &exc);
    if (!inst) {
        LOGI(OBFUSCATE("[AntiBan] %s.get_Instance() null"), cls);
        return;
    }
    bool v = value;
    _monoFieldSetValue(inst, f, &v);
    LOGI(OBFUSCATE("[AntiBan] %s.%s = %d (inst=%p)"), cls, fieldName, (int)value, inst);
}

// 性能/设备埋点（XDKWPerfSDK）：位置、UID、设备指纹、模拟器类型
void ApplyTelemetryBlock() {
    static const Target kPerfTargets[] = {
        {"XDPlugin.XDKWPerf", "XDPerf", "PostPosition", 3, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "ReportUid", 1, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "ReportXDPerfInfos", 0, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "ReportQuality", 2, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "ReportCustomQuality", 1, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "ReportTargetFrameRate", 1, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "PostNetworkLatency", 1, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "ReportSceneName", 1, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "ReportChannel", 1, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "ReportServerId", 1, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "ReportLanguage", 1, kPatchVoid},
        {"XDPlugin.XDKWPerf", "XDPerf", "GetEmulatorType", 0, kPatchIntZero},
    };
    for (const Target &t : kPerfTargets) {
        if (PatchOne(t)) ++g_patched; else ++g_missed;
    }

    // 日志系统：关掉「提交日志」与「日志附带截图」
    SetInstanceBool("XDFramework.Core", "LogSystem", "get_Instance", 0, "canSubmit", false);
    SetInstanceBool("XDFramework.Core", "LogSystem", "get_Instance", 0, "captureScreenshot", false);

    // Mono 程序集 MD5 校验：校验结果直接置 true，并让校验函数空转（防止覆盖）
    SetStaticBool("MonoGame.ScriptFramework", "CodeCheck", "IsMD5Match", true);
    PatchOverloads("MonoGame", "TypeRegister", "CheckMD5", {1}, kPatchVoid);
}

}  // namespace

// ============================================================================
//  对外接口
// ============================================================================
bool SMAntiBanHookFramePump() {
    if (g_frameHookOk) return true;
    if (!_monoCompileMethod) return false;

    void *klass = FindClassByName("XD.Unity.Game", "GameApp");
    if (!klass) {
        // dump 里 GameApp 在 GameApp.dll / 命名空间 XD.Unity.Game；
        // 兜底再按短名全镜像搜一次（ns 传空由 shim 扫全表）。
        klass = FindClassByName("", "GameApp");
    }
    if (!klass) {
        LOGI(OBFUSCATE("[AntiBan] GameApp class not found, frame pump hook skipped"));
        return false;
    }
    void *method = FindMethodEx(klass, "Update", 0);
    if (!method) method = FindMethodEx(klass, "LateUpdate", 0);
    if (!method) {
        LOGI(OBFUSCATE("[AntiBan] GameApp.Update not found"));
        return false;
    }
    void *target = _monoCompileMethod(method);
    if (!target) return false;

    const int rc = DobbyHook(target, (void *)SMGameAppUpdate, (void **)&SMOldGameAppUpdate);
    const bool ok = (rc == 0) && SMOldGameAppUpdate != nullptr;
    LOGI(OBFUSCATE("[AntiBan] GameApp.Update hook %s target=%p old=%p"),
         ok ? "ok" : "failed", target, (void *)SMOldGameAppUpdate);
    g_frameHookOk = ok;
    return ok;
}

int SMAntiBanInstall() {
    std::call_once(g_installOnce, []() {
        AttachThread();
        LOGI(OBFUSCATE("[AntiBan] install start"));

        ApplyThemisBypass();
        ApplyReportBlock();
        ApplyTelemetryBlock();

        // 帧泵：RVA 未标定时用它替代（Bootstrap 里优先调用）。
        SMAntiBanHookFramePump();

        // 与菜单的「反作弊模式」开关保持一致：默认视为已开启。
        g_isAntiCheatActive = true;

        char buf[160];
        snprintf(buf, sizeof(buf), "patched=%d missed=%d frame=%s",
                 g_patched, g_missed, g_frameHookOk ? "on" : "off");
        g_status = buf;
        LOGI(OBFUSCATE("[AntiBan] install done %s"), buf);
    });
    return g_patched;
}

std::string SMAntiBanStatus() { return g_status; }
