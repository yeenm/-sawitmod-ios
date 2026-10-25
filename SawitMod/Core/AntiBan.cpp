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
#include "IL2CPP/MonoHot.h"
#include "IL2CPP/MonoCompat.h"
#include "Includes/Dobby/dobby.h"
#include "Includes/Logger.h"
#include "Includes/OSCache.h"
#include "Includes/obfuscate.h"
#include "App/Platform.h"
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
    kKindPatch = 0,       // ⚠️ 仅越狱设备可行；侧载 iOS 上必然失败，默认不加入 pending
    kKindMulti,
    kKindStaticBool,
    kKindInstanceBool,
    kKindInvokeBool,
    kKindStaticNull,      // 静态对象字段置 null（摘掉回调/消息对象）
    kKindStaticInt,       // 静态枚举/整型字段写值
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
    long long ivalue;
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
// 防封默认不启用：启动阶段改 Themis/日志字段会干扰游戏的初始化状态机,
// 实测会把资源加载卡住。玩家进世界后从菜单手动启用。
bool g_armed = false;

constexpr int kMaxTries = 80;   // 每秒左右一次 → 约 1 分 20 秒窗口
constexpr int kFrameMaxTries = 30;

void AttachThread() {
    if (_monoGetRootDomain && _monoThreadAttach) {
        _monoThreadAttach(_monoGetRootDomain());
    }
}

// 只在 **il2cpp AOT 层**找类。
// 不能走 MonoCompat 的混合分发：iOS 包里 Themis 相关类在热更 mono 层也有
// 同名副本，一旦解析到 mono 那份，写静态字段/调用都会跨运行时崩
// （实测 ab:ThemisUtil._autoQuitApplicationAfterReport）。
void *FindClassByName(const char *ns, const char *name) {
    if (!name) return nullptr;
    return FindClassAnywhere(ns ? ns : "", name);
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
    SMPlatform::SetBreadcrumb(("ab:" + std::string(cls) + "." + field).c_str());
    void *klass = FindClassByName(ns, cls);
    if (!klass || !field) return;
    const API &a = Get();
    if (!a.class_get_field_from_name || !a.field_static_set_value) return;
    void *f = a.class_get_field_from_name(klass, field);
    if (!f) return;
    bool v = value;
    a.field_static_set_value(f, &v);
}

void SetStaticNull(const char *ns, const char *cls, const char *field) {
    SMPlatform::SetBreadcrumb(("ab:" + std::string(cls) + "." + field).c_str());
    void *klass = FindClassByName(ns, cls);
    if (!klass || !field) return;
    const API &a = Get();
    if (!a.class_get_field_from_name || !a.field_static_set_value) return;
    void *f = a.class_get_field_from_name(klass, field);
    if (!f) return;
    void *nullValue = nullptr;
    a.field_static_set_value(f, &nullValue);
}

void SetStaticInt(const char *ns, const char *cls, const char *field, long long value) {
    SMPlatform::SetBreadcrumb(("ab:" + std::string(cls) + "." + field).c_str());
    void *klass = FindClassByName(ns, cls);
    if (!klass || !field) return;
    const API &a = Get();
    if (!a.class_get_field_from_name || !a.field_static_set_value) return;
    void *f = a.class_get_field_from_name(klass, field);
    if (!f) return;
    int v = (int)value;
    a.field_static_set_value(f, &v);
}

// 通过 static get_Instance() 拿到单例，再写它的实例字段。
void SetInstanceBool(const char *ns, const char *cls, const char *getter, int getterArgc,
                     const char *fieldName, bool value) {
    SMPlatform::SetBreadcrumb(("ab:" + std::string(cls) + "." + fieldName).c_str());
    void *klass = FindClassByName(ns, cls);
    if (!klass) return;
    const API &a = Get();
    if (!a.class_get_method_from_name || !a.runtime_invoke ||
        !a.class_get_field_from_name || !a.field_set_value) {
        return;
    }
    void *gm = a.class_get_method_from_name(klass, getter, getterArgc);
    void *f = a.class_get_field_from_name(klass, fieldName);
    if (!gm || !f) return;
    void *exc = nullptr;
    void *inst = a.runtime_invoke(gm, nullptr, nullptr, &exc);
    if (!inst) return;
    bool v = value;
    a.field_set_value(inst, f, &v);
}

bool InvokeStaticVoidBool(const char *ns, const char *cls, const char *method, bool arg) {
    SMPlatform::SetBreadcrumb(("ab:invoke:" + std::string(cls) + "." + method).c_str());
    void *klass = FindClassByName(ns, cls);
    if (!klass) return false;
    void *m = FindMethodEx(klass, method, 1);
    if (!m) return false;
    // mono 热更层的方法一律不主动调用：实测 TapThemis.U3d_ConfigAutoQuitApplication
    // 在 mono 侧 invoke 会把进程带走；反正字段面已把相关开关关掉。
    if (SMMonoHot::IsMonoMethod(m)) {
        LOGI(OBFUSCATE("[AntiBan] skip mono invoke %s.%s"), cls, method);
        return false;
    }
    if (!_monoRuntimeInvoke) return false;
    bool v = arg;
    void *params[1] = {&v};
    void *exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, params, &exc);
    return exc == nullptr;
}

// ---------------------------------------------------------------------------
//  pending 表
// ---------------------------------------------------------------------------
#define P_PATCH(ns, cls, meth, argc, mode) \
    Pending{kKindPatch, ns, cls, meth, nullptr, argc, nullptr, 0, mode, false, false, 0, 0}
#define P_SNULL(ns, cls, field) \
    Pending{kKindStaticNull, ns, cls, field, nullptr, 0, nullptr, 0, kPatchVoid, false, false, 0, 0}
#define P_SINT(ns, cls, field, val) \
    Pending{kKindStaticInt, ns, cls, field, nullptr, 0, nullptr, 0, kPatchVoid, false, false, 0, (long long)(val)}
#define P_MULTI(ns, cls, meth, arr, mode) \
    Pending{kKindMulti, ns, cls, meth, nullptr, 0, arr, (int)(sizeof(arr) / sizeof(int)), mode, false, false, 0, 0}
#define P_SBOOL(ns, cls, field, val) \
    Pending{kKindStaticBool, ns, cls, field, nullptr, 0, nullptr, 0, kPatchVoid, val, false, 0, 0}
#define P_IBOOL(ns, cls, getter, field, val) \
    Pending{kKindInstanceBool, ns, cls, field, getter, 0, nullptr, 0, kPatchVoid, val, false, 0, 0}
#define P_INVOKE(ns, cls, meth, val) \
    Pending{kKindInvokeBool, ns, cls, meth, nullptr, 1, nullptr, 0, kPatchVoid, val, false, 0, 0}

void BuildPendingList() {
    // ⚠️ 非越狱侧载 iOS 上 __TEXT 受代码签名保护，inline patch（含 Dobby）都不可行
    // （实测：所有方法体补丁 0 命中，而字段/调用类补丁全部成功）。
    // 因此这里只做**数据面**拦截：写 __DATA 里的开关/字段，副作用最小也最稳。
    g_pending = {
        // ===== Themis / TapSDK：把总开关和自动行为全部关掉 =====
        P_SBOOL("TapTap.Themis", "TapThemis", "Open", false),
        P_SBOOL("TapTap.Themis", "TapThemis", "_autoQuitApplicationAfterReport", false),
        P_SBOOL("TapTap.Themis", "TapThemis", "_debugMode", false),
        // 摘掉 native -> 托管 的状态/消息回调对象
        // 引擎侧 Themis 管理类：摘掉附加消息对象（心跳/额外上报数据）
        // TapDB 的 Themis 工具类：关自动退出/调试/日志回调
        P_SBOOL("TapTap.TapDB.Mobile", "ThemisUtil", "_autoQuitApplicationAfterReport", false),
        P_SBOOL("TapTap.TapDB.Mobile", "ThemisUtil", "enableDebug", false),
        P_SBOOL("TapTap.TapDB.Mobile", "ThemisUtil", "_uncaughtAutoReportOnce", false),
        // 日志系统字段写在 iOS 上会崩（get_Instance 返回对象字段布局异常），已移除。
        // ===== Mono 程序集防篡改校验：直接标记通过 =====
        // CodeCheck.IsMD5Match：实测写这个静态字段会崩（类元数据归属异常），
        // 放弃这一条；Mono 程序集 MD5 校验交给下面的 TypeRegister 补丁。
        // ===== 主动调用类补丁（这些能真正执行代码，不依赖改指令）=====
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
            // 保守：只在 il2cpp AOT 方法上调用；mono 热更方法不主动 invoke
            // （跨运行时调用是崩溃高发点，字段面通常已经够用）。
            return InvokeStaticVoidBool(p.ns, p.cls, p.name, p.bvalue);
        case kKindStaticNull:
            if (!FindClassByName(p.ns, p.cls)) return false;
            SetStaticNull(p.ns, p.cls, p.name);
            return true;
        case kKindStaticInt:
            if (!FindClassByName(p.ns, p.cls)) return false;
            SetStaticInt(p.ns, p.cls, p.name, p.ivalue);
            return true;
    }
    return false;
}

void ApplyPendingLocked() {
    int budget = 4;   // 每轮最多 4 条，别让类名扫描把主线程卡住
    for (Pending &p : g_pending) {
        if (budget <= 0) break;
        if (p.done) continue;
        if (p.tries >= kMaxTries) continue;
        ++p.tries;
        --budget;
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
    // 侧载 iOS：无法分配可执行内存做 inline hook。帧泵改由 CADisplayLink（主线程）
    // 驱动，见 App/OverlayWindow.mm 的 SMRunUnityFramePump() 调用。
    return;
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

int g_rxApplied = 0;

struct RuntimeActionState {
    bool done;
    int tries;
    uint64_t lastTryMs;
};
enum { kRxThemis = 0, kRxLogan = 1, kRxInputCheat = 2, kRxConfig = 3, kRxCount = 4 };
RuntimeActionState g_rx[kRxCount] = {};
const char *kRxName[kRxCount] = {"themis", "logan", "inputcheat", "cfg"};

void RefreshStatusLocked() {
    char buf[192];
    snprintf(buf, sizeof(buf), "fields=%d pending=%d armed=%s pump=%s auto=%d%d%d%d", g_applied,
             g_pendingLeft, g_armed ? "on" : "off",
             g_unityFramePumpReady.load(std::memory_order_relaxed) ? "link" : "off",
             g_rx[kRxThemis].done ? 1 : 0, g_rx[kRxLogan].done ? 1 : 0,
             g_rx[kRxInputCheat].done ? 1 : 0, g_rx[kRxConfig].done ? 1 : 0);
    g_status = buf;
}

// Bootstrap 首次调用：建表 + 一轮尝试。
void InstallOnce() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_listsReady) {
        BuildPendingList();
        g_listsReady = true;
    }
    // 只建表，不写任何字段（启动阶段写 Themis 开关会卡住资源加载）。
    AttachThread();
    RefreshStatusLocked();
    LOGI(OBFUSCATE("[AntiBan] armed table built: %s"), g_status.c_str());
}


// ===========================================================================
//  运行时数据面「拦上报 / 过检测」（侧载 iOS 上真实可执行的防封面）
// ---------------------------------------------------------------------------
//  侧载 iOS 的 __TEXT 受代码签名保护，Android 那套「方法体写 RET / mov w0,#0」
//  在 iOS 上全部失效（FeatureDispatch 的 PatchMethodReturn* 在 Apple 直接返回
//  false）。能真正落地的只有两类：
//    ① 调游戏自己暴露的开关 API（游戏自身正常逻辑里也会调）；
//    ② 写数据面字段（总开关 / 反作弊阈值），由游戏自己的逻辑读取。
//
//  四个动作（全部对着反编译的热更源码核对过）：
//    themis       EngineWrapper.ThemisSDKManager.SetSwitch(false)
//                 —— 游戏自己在 PackageUpdatePanel 里也是用它关掉 Themis 的
//    logan        EngineWrapper.LoganManager.open = false
//                 —— 原生 Logan 日志管道总开关（日志落盘/上传都从这里走）
//    inputcheat   XDTLevelAndEntity…InputCheatManager.Enable = false
//                 —— 输入行为反作弊：采集 + "#INPUTANTICHEAT" 上报的总开关（默认 true）
//    anticheatcfg GameConfig.CollectAntiCheating / MovementAntiCheating 阈值放宽
//                 —— 与安卓版 Virbox_PatchAntiCheat 相同的字段面处理
//
//  每个动作失败只降级不崩：类/方法/实例任一为空就跳过，下一轮重试；成功后
//  每 15~30 秒复核一次（游戏切场景/重登可能把开关改回去）。
// ===========================================================================
uint64_t NowMsRx() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

// 只接受真实 mono 方法（AOT 方法句柄不能保证静态/实例与参数类型，盲 invoke 会崩）。
bool RxMonoMethodFlags(void *method, bool *isStatic) {
    if (isStatic) *isStatic = false;
    if (!method || !SMMonoHot::IsMonoMethod(method)) return false;
    const uint32_t flags = SMMonoHot::MethodGetFlags(method, nullptr);
    if (isStatic) *isStatic = (flags & 0x10u) != 0;   // MONO_METHOD_ATTR_STATIC
    return true;
}

bool RxInvokeVoidBool(void *method, void *instance, bool arg) {
    if (!method || !_monoRuntimeInvoke) return false;
    bool v = arg;
    void *params[1] = {&v};
    void *exc = nullptr;
    _monoRuntimeInvoke(method, instance, params, &exc);
    return exc == nullptr;
}

void *RxFindMonoClass(const char *imageName, const char *ns, const char *cls) {
    if (!_monoImageLoaded || !_monoClassFromName) return nullptr;
    void *img = _monoImageLoaded(imageName);
    if (!img) return nullptr;
    return _monoClassFromName(img, ns ? ns : "", cls);
}

// ---- 动作 1：关掉 Themis 检测开关（游戏自己在更新弹窗里也这么干）----
bool RxApplyThemisSwitchOff() {
    void *klass = RxFindMonoClass((const char *) OBFUSCATE("EngineWrapper"), "",
                                  (const char *) OBFUSCATE("ThemisSDKManager"));
    if (!klass) return false;
    void *m = FindMethodInHierarchy(klass, (const char *) OBFUSCATE("SetSwitch"), 1);
    bool isStatic = false;
    if (!RxMonoMethodFlags(m, &isStatic) || !isStatic) return false;
    if (!RxInvokeVoidBool(m, nullptr, false)) return false;
    SMPlatform::SetBreadcrumbLight("ab:rx:themis-off");
    return true;
}

// ---- 动作 2：关掉 Logan 日志总开关（原生管道，日志落盘+上传的总闸）----
bool RxApplyLoganOff() {
    void *klass = RxFindMonoClass((const char *) OBFUSCATE("EngineWrapper"), "",
                                  (const char *) OBFUSCATE("LoganManager"));
    if (!klass) return false;
    void *m = FindMethodInHierarchy(klass, (const char *) OBFUSCATE("set_open"), 1);
    bool isStatic = false;
    if (!RxMonoMethodFlags(m, &isStatic)) return false;
    if (!isStatic || !RxInvokeVoidBool(m, nullptr, false)) return false;
    SMPlatform::SetBreadcrumbLight("ab:rx:logan-off");
    return true;
}

// ---- 动作 3：关掉输入行为反作弊（采集 + #INPUTANTICHEAT 上报）----
bool RxApplyInputCheatOff() {
    void *klass = RxFindMonoClass((const char *) OBFUSCATE("XDTLevelAndEntity"),
                                  (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.InputCheatManager"),
                                  (const char *) OBFUSCATE("InputCheatManager"));
    if (!klass) return false;

    // InputCheatManager : DataModule<InputCheatManager>，单例在父类的静态属性 Instance 上。
    void *instance = nullptr;
    void *cur = klass;
    for (int depth = 0; cur && depth < 5 && !instance; ++depth) {
        void *getter = _monoClassGetMethodFromName
                           ? _monoClassGetMethodFromName(cur, (const char *) OBFUSCATE("get_Instance"), 0)
                           : nullptr;
        if (!getter) getter = FindMethodInHierarchy(cur, (const char *) OBFUSCATE("get_Instance"), 0);
        bool staticGetter = false;
        if (getter && RxMonoMethodFlags(getter, &staticGetter) && staticGetter) {
            void *exc = nullptr;
            instance = _monoRuntimeInvoke(getter, nullptr, nullptr, &exc);
            if (exc) instance = nullptr;
        }
        cur = _monoClassGetParent(cur);
    }
    if (!instance && GetManagerInstanceNative) {
        instance = GetManagerInstanceNative((const char *) OBFUSCATE("InputCheatManager"));
    }
    if (!instance) return false;

    // 优先走游戏自己的属性 setter（auto-property，一行赋值，最安全）。
    void *setter = FindMethodInHierarchy(klass, (const char *) OBFUSCATE("set_Enable"), 1);
    bool setterStatic = false;
    if (setter && RxMonoMethodFlags(setter, &setterStatic) && !setterStatic) {
        bool v = false;
        void *params[1] = {&v};
        void *exc = nullptr;
        _monoRuntimeInvoke(setter, instance, params, &exc);
        if (exc == nullptr) {
            SMPlatform::SetBreadcrumbLight("ab:rx:inputcheat-off");
            return true;
        }
    }
    // 兜底：直接写自动属性的 backing field。
    void *field = _monoClassGetFieldFromName
                      ? _monoClassGetFieldFromName(klass, (const char *) OBFUSCATE("<Enable>k__BackingField"))
                      : nullptr;
    if (field && _monoFieldSetValue) {
        bool v = false;
        _monoFieldSetValue(instance, field, &v);
        SMPlatform::SetBreadcrumbLight("ab:rx:inputcheat-field");
        return true;
    }
    return false;
}

// ---- 动作 4：放宽游戏内反作弊阈值（采集距离/CD/移速；移动反作弊采样/上报）----
bool RxApplyAntiCheatConfig() {
    void *configMgr = GetConfigManagerInstance();
    if (!configMgr) return false;
    void *mgrKlass = _monoObjectGetClass(configMgr);
    if (!mgrKlass) return false;
    void *getGameConfig = FindMethodInHierarchy(mgrKlass, (const char *) OBFUSCATE("get_GameConfig"), 0);
    if (!getGameConfig) return false;
    void *exc = nullptr;
    void *gameConfig = _monoRuntimeInvoke(getGameConfig, configMgr, nullptr, &exc);
    if (!gameConfig || exc) return false;

    int applied = 0;
    auto instanceFieldObject = [&](void *ownerKlass, void *owner, const char *name) -> void * {
        void *field = FindFieldInHierarchy(ownerKlass, name);
        if (!field || !_monoFieldGetValue) return nullptr;
        void *value = nullptr;
        _monoFieldGetValue(owner, field, &value);
        return value;
    };
    auto setFloat = [&](void *ownerKlass, void *owner, const char *name, float v) {
        void *field = FindFieldInHierarchy(ownerKlass, name);
        if (field && _monoFieldSetValue) {
            _monoFieldSetValue(owner, field, &v);
            ++applied;
        }
    };
    auto setBool = [&](void *ownerKlass, void *owner, const char *name, bool v) {
        void *field = FindFieldInHierarchy(ownerKlass, name);
        if (field && _monoFieldSetValue) {
            _monoFieldSetValue(owner, field, &v);
            ++applied;
        }
    };
    auto setInt = [&](void *ownerKlass, void *owner, const char *name, int v) {
        void *field = FindFieldInHierarchy(ownerKlass, name);
        if (field && _monoFieldSetValue) {
            _monoFieldSetValue(owner, field, &v);
            ++applied;
        }
    };

    void *gameConfigKlass = _monoObjectGetClass(gameConfig);
    // CollectAntiCheating：采集距离/无CD/移速可达性（与安卓版逐字段一致）
    void *collect = instanceFieldObject(gameConfigKlass, gameConfig,
                                        (const char *) OBFUSCATE("CollectAntiCheating"));
    if (collect) {
        void *ck = _monoObjectGetClass(collect);
        setFloat(ck, collect, (const char *) OBFUSCATE("Distance"), 9999.0f);
        setFloat(ck, collect, (const char *) OBFUSCATE("CloseDist"), 9999.0f);
        setBool(ck, collect, (const char *) OBFUSCATE("IsNoCdInCloseDist"), true);
        setFloat(ck, collect, (const char *) OBFUSCATE("MaxSpeed"), 9999.0f);
        setFloat(ck, collect, (const char *) OBFUSCATE("SpeedTolerance"), 9999.0f);
        setFloat(ck, collect, (const char *) OBFUSCATE("Slack"), 9999.0f);
        setBool(ck, collect, (const char *) OBFUSCATE("WarnOnly"), true);
    }
    // MovementAntiCheating：移动速度采样/异常比例/上报间隔（PC 3.2.5 新增，安卓旧版没有）
    void *movement = instanceFieldObject(gameConfigKlass, gameConfig,
                                         (const char *) OBFUSCATE("MovementAntiCheating"));
    if (movement) {
        void *mk = _monoObjectGetClass(movement);
        setFloat(mk, movement, (const char *) OBFUSCATE("SpeedThresholdWalk"), 9999.0f);
        setFloat(mk, movement, (const char *) OBFUSCATE("SpeedThresholdOnVehicle"), 9999.0f);
        setFloat(mk, movement, (const char *) OBFUSCATE("AbnormalRatio"), 9999.0f);
        setInt(mk, movement, (const char *) OBFUSCATE("ReportInterval"), 3600);
    }
    if (applied > 0) {
        SMPlatform::SetBreadcrumbLight("ab:rx:cfg");
        return true;
    }
    return false;
}

bool RxTry(int index) {
    switch (index) {
        case kRxThemis: return RxApplyThemisSwitchOff();
        case kRxLogan: return RxApplyLoganOff();
        case kRxInputCheat: return RxApplyInputCheatOff();
        case kRxConfig: return RxApplyAntiCheatConfig();
        default: return false;
    }
}

// 当前线程是不是 Unity 主线程（运行时动作会 invoke 游戏方法，必须主线程）。
bool RxOnUnityThread() {
    const std::thread::id unity = g_unityThreadId.load(std::memory_order_relaxed);
    return unity != std::thread::id() && std::this_thread::get_id() == unity;
}

// 执行（或复核）运行时动作。forceAll=true 时忽略节流、立即全跑一遍。
void ApplyRuntimeActionsLocked(bool forceAll) {
    if (!forceAll && !g_isAntiCheatActive) return;
    // 方法 invoke 只能在 Unity 线程做；启动早期（Bootstrap 后台线程）先只做字段写，
    // 等悬浮窗的第一帧（主线程）由 SMAntiBanTick 自动补跑这些动作。
    if (!forceAll && !RxOnUnityThread()) return;
    const uint64_t now = NowMsRx();
    for (int i = 0; i < kRxCount; ++i) {
        RuntimeActionState &state = g_rx[i];
        const uint64_t intervalMs = state.done ? (i == kRxConfig ? 30000ULL : 15000ULL)
                                               : (i == kRxConfig ? 3000ULL : 1500ULL);
        if (!forceAll && state.lastTryMs != 0 && now - state.lastTryMs < intervalMs) continue;
        state.lastTryMs = now;
        if (state.tries < 1000000000) ++state.tries;
        if (RxTry(i)) {
            if (!state.done) {
                state.done = true;
                ++g_rxApplied;
                LOGI(OBFUSCATE("[AntiBan] runtime action ok: %s"), kRxName[i]);
            }
        } else if (state.tries == 1 || (state.tries % 10) == 0) {
            LOGI(OBFUSCATE("[AntiBan] runtime action pending: %s (tries=%d)"), kRxName[i],
                 state.tries);
        }
    }
}

}

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
    if (!g_listsReady || !g_armed) return;

    const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
    long long prev = lastMs.load(std::memory_order_relaxed);
    if (nowMs - prev < 1200) return;
    if (!lastMs.compare_exchange_strong(prev, nowMs, std::memory_order_relaxed)) return;

    std::lock_guard<std::mutex> lk(g_mtx);
    AttachThread();
    ApplyPendingLocked();
    TryFrameHookLocked();
    ApplyRuntimeActionsLocked(false);
    RefreshStatusLocked();
}

void SMAntiBanArm() {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_armed = true;
    g_isAntiCheatActive = true;
    AttachThread();
    ApplyPendingLocked();
    ApplyRuntimeActionsLocked(false);
    RefreshStatusLocked();
    LOGI(OBFUSCATE("[AntiBan] armed: %s"), g_status.c_str());
}

bool SMAntiBanArmed() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_armed;
}

int SMAntiBanApplyRuntimeOnce() {
    std::lock_guard<std::mutex> lk(g_mtx);
    AttachThread();
    ApplyRuntimeActionsLocked(true);
    RefreshStatusLocked();
    int n = 0;
    for (int i = 0; i < kRxCount; ++i) {
        if (g_rx[i].done) ++n;
    }
    return n;
}

std::string SMAntiBanRuntimeStatus() {
    std::lock_guard<std::mutex> lk(g_mtx);
    std::string out;
    for (int i = 0; i < kRxCount; ++i) {
        if (!out.empty()) out += " ";
        out += kRxName[i];
        out += "=";
        out += g_rx[i].done ? "ok" : (g_rx[i].tries > 0 ? "wait" : "idle");
    }
    return out;
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
