// ============================================================================
//  BubbleFeatures.cpp
//  移植自 PC 端 Bugtopia-Solo（yslf.dll 的 BubbleFeaturePatches /
//  ProcessBubbleFeatureMonoRuntimeEffects / ProcessBubbleSpawnAtPlayerOnUpdate）
//  与安卓 v1.6.2 补丁 src/BubbleFeatures.cpp。
//
//  三段逻辑：
//    1) 快速气泡生成：按 BPM 调 ActivityEventProtocolManager.CreateActivityBubble(Vector3)；
//       拿不到就退到 BubbleProtocolManager.CreateBubble / CreateVisitBubble。
//    2) 气泡生成于玩家位置：对 Create*Bubble 的已编译方法做 Dobby detour，把
//       传入的 Vector3 改写成玩家坐标 —— 这一项只能改写"游戏自己生成"的气泡。
//    3) 自动收集气泡：本文件不负责，沿用 Menu 里已有的 OP_AUTO_COLLECT_BUBBLE
//       云 payload（AutoBubbleLoopThread）。
//
//  iOS 侧载构建：Dobby 在 Includes/Dobby/dobby.h 里被替换成失败桩，第 2) 项
//  自动变成 no-op（不会崩，也不会生效）；第 1) 项不依赖 hook，一直可用。
// ============================================================================

#include "BubbleFeatures.h"

#include "ModFeatures.h"
#include "ModUtils.h"
#include "MonoGameApi.h"
#include "Core/State.h"
#include "Includes/Dobby/dobby.h"
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

using namespace MonoApi;

namespace BubbleFeatures {

std::atomic<uint64_t> g_generated{0};

namespace {

// ------------------------------- Mono API ---------------------------------
struct BubbleApi {
    bool resolved = false;
    bool failed = false;

    void *activityClass = nullptr;
    void *createActivity = nullptr;

    void *bubbleClass = nullptr;
    void *createBubble = nullptr;
    void *createVisit = nullptr;

    void *hideClass = nullptr;
    void *createHide = nullptr;
};

BubbleApi g_api;
std::mutex g_apiMutex;

std::atomic<bool> g_workerRunning{false};
std::atomic<uint64_t> g_workerGeneration{0};

// detour 用的坐标（arm64 上 float 读写是原子的）
std::atomic<bool> g_spawnArmed{false};
float g_spawnX = 0.0f;
float g_spawnY = 0.0f;
float g_spawnZ = 0.0f;

// ------------------------------ detour slots -------------------------------
// 目标方法签名：CreateXxxBubble(Vector3)。PC/安卓都按"参数是指向 Vector3 的
// 指针"处理（struct 间接传递）。侧载构建里 DobbyHook 是失败桩，这段不会执行；
// 越狱构建属于实验路径，崩了就把 g_bubbleSpawnAtPlayerEnabled 关掉。
using SpawnFn = void (*)(void *posPtr);

void *g_hookTargets[4] = {nullptr, nullptr, nullptr, nullptr};
SpawnFn g_hookOrig[4] = {nullptr, nullptr, nullptr, nullptr};
bool g_hookInstalled[4] = {false, false, false, false};

void SpawnHookImpl(int slot, void *posPtr) {
    if (posPtr && g_spawnArmed.load(std::memory_order_relaxed)) {
        float *vec = static_cast<float *>(posPtr);
        vec[0] = g_spawnX;
        vec[1] = g_spawnY;
        vec[2] = g_spawnZ;
    }
    if (g_hookOrig[slot]) g_hookOrig[slot](posPtr);
}

void SpawnHook0(void *p) { SpawnHookImpl(0, p); }
void SpawnHook1(void *p) { SpawnHookImpl(1, p); }
void SpawnHook2(void *p) { SpawnHookImpl(2, p); }
void SpawnHook3(void *p) { SpawnHookImpl(3, p); }

void *const kSpawnHooks[4] = {
        reinterpret_cast<void *>(&SpawnHook0),
        reinterpret_cast<void *>(&SpawnHook1),
        reinterpret_cast<void *>(&SpawnHook2),
        reinterpret_cast<void *>(&SpawnHook3),
};

// ------------------------------- helpers -----------------------------------
void AttachMono() {
    if (_monoGetRootDomain && _monoThreadAttach) {
        void *domain = _monoGetRootDomain();
        if (domain) _monoThreadAttach(domain);
    }
}

void *LoadedImageCompat(const char *name) {
    if (!_monoImageLoaded) return nullptr;
    void *img = _monoImageLoaded(name);
    if (img) return img;
    std::string withDll = std::string(name) + ".dll";
    return _monoImageLoaded(withDll.c_str());
}

// 0.6.1 之后部分协议管理器可能被挪到别的程序集，这里按"候选镜像 + 全镜像兜底"
// 解析，跟 EmoteUnlock 的 FindClassInGameImages 同一套路。
void *FindClassAnyImage(const char *nameSpace, const char *className) {
    static const char *kImages[] = {
            OBFUSCATE("XDTDataAndProtocol"), OBFUSCATE("XDTLevelAndEntity"),
            OBFUSCATE("EcsClient"), OBFUSCATE("EcsSystem"),
            OBFUSCATE("Assembly-CSharp"), OBFUSCATE("XDTGameSystem"),
            OBFUSCATE("Plugins"), OBFUSCATE("XDTBaseService"),
    };
    constexpr int kImageCount = sizeof(kImages) / sizeof(kImages[0]);
    return FindClass(kImages, kImageCount, nameSpace, className, nullptr, true);
}

bool ResolveApi() {
    std::lock_guard<std::mutex> lock(g_apiMutex);
    if (g_api.resolved) return true;
    if (g_api.failed) return false;

    AttachMono();

    void *dataImage = LoadedImageCompat(OBFUSCATE("XDTDataAndProtocol"));
    if (!dataImage) {
        LOGI(OBFUSCATE("[Bubble] XDTDataAndProtocol 未加载，稍后重试"));
        return false;   // 不置 failed：进世界后程序集才齐
    }

    // ActivityEventProtocolManager.CreateActivityBubble(Vector3)
    g_api.activityClass = FindClassAnyImage(
            OBFUSCATE("XDTDataAndProtocol.ProtocolService.ActivityEvent"),
            OBFUSCATE("ActivityEventProtocolManager"));
    if (g_api.activityClass) {
        g_api.createActivity = FindMethodInHierarchy(
                g_api.activityClass, OBFUSCATE("CreateActivityBubble"), 1);
    }

    // BubbleProtocolManager.CreateBubble(Vector3) / CreateVisitBubble(Vector3)
    g_api.bubbleClass = FindClassAnyImage(
            OBFUSCATE("XDTDataAndProtocol.ProtocolService.Bubble"),
            OBFUSCATE("BubbleProtocolManager"));
    if (g_api.bubbleClass) {
        g_api.createBubble = FindMethodInHierarchy(
                g_api.bubbleClass, OBFUSCATE("CreateBubble"), 1);
        g_api.createVisit = FindMethodInHierarchy(
                g_api.bubbleClass, OBFUSCATE("CreateVisitBubble"), 1);
    }

    // HideAndSeekProtocolManager.CreateHideAndSeekBubble(Vector3)
    g_api.hideClass = FindClassAnyImage(
            OBFUSCATE("XDTDataAndProtocol.ProtocolService.HideAndSeek"),
            OBFUSCATE("HideAndSeekProtocolManager"));
    if (g_api.hideClass) {
        g_api.createHide = FindMethodInHierarchy(
                g_api.hideClass, OBFUSCATE("CreateHideAndSeekBubble"), 1);
    }

    g_api.resolved = (g_api.createActivity || g_api.createBubble);
    if (!g_api.resolved) {
        LOGI(OBFUSCATE("[Bubble] 没找到 CreateActivityBubble/CreateBubble"));
        g_api.failed = true;
        return false;
    }

    LOGI(OBFUSCATE("[Bubble] api ready: activity=%p bubble=%p visit=%p hide=%p"),
         g_api.createActivity, g_api.createBubble, g_api.createVisit, g_api.createHide);
    return true;
}

// 活动计时器清零：PC 端每次生成前清零，避免活动计时器把连续生成吞掉。
void ResetActivityTimeCounter() {
    void *module = GetManagerInstanceNative(OBFUSCATE("ActivityEventModule"));
    if (!module || !_monoObjectGetClass) return;
    void *klass = _monoObjectGetClass(module);
    if (!klass) return;
    void *field = _monoClassGetFieldFromName(klass, OBFUSCATE("_timeCounter"));
    if (!field) field = _monoClassGetFieldFromName(klass, OBFUSCATE("timeCounter"));
    if (!field || !_monoFieldSetValue) return;
    float zero = 0.0f;
    _monoFieldSetValue(module, field, &zero);
}

bool GetPlayerPosition(Vector3 &out) {
    out = {0.0f, 0.0f, 0.0f};
    void *levelImage = LoadedImageCompat(OBFUSCATE("XDTLevelAndEntity"));
    if (!levelImage) return false;
    void *util = _monoClassFromName(
            levelImage,
            OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
            OBFUSCATE("EntityUtil"));
    if (!util) return false;
    void *getSelf = FindMethodInHierarchy(util, OBFUSCATE("GetSelfPlayer"), 0);
    if (!getSelf) return false;
    void *self = _monoRuntimeInvoke(getSelf, nullptr, nullptr, nullptr);
    if (!self) return false;
    out = NativeGetPosition(self);
    return out.x != 0.0f || out.y != 0.0f || out.z != 0.0f;
}

bool InvokeSpawn(void *method, const Vector3 &pos) {
    if (!method || !_monoRuntimeInvoke) return false;
    Vector3 copy = pos;
    void *args[1] = {&copy};
    void *exception = nullptr;
    _monoRuntimeInvoke(method, nullptr, args, &exception);
    return exception == nullptr;
}

// 必须在 Unity 线程上调用（内部会进 Mono、分配托管对象）。
bool SpawnAtPlayer() {
    if (!ResolveApi()) return false;
    Vector3 pos;
    if (!GetPlayerPosition(pos)) return false;
    pos.y += 0.35f;

    ResetActivityTimeCounter();

    const void *order[4] = {g_api.createActivity, g_api.createBubble,
                            g_api.createVisit, g_api.createHide};
    for (const void *method : order) {
        if (!method) continue;
        if (InvokeSpawn(const_cast<void *>(method), pos)) {
            g_generated.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

void UpdateSpawnArm() {
    if (!g_bubbleSpawnAtPlayerEnabled) {
        g_spawnArmed.store(false, std::memory_order_relaxed);
        return;
    }
    Vector3 pos;
    if (!GetPlayerPosition(pos)) {
        g_spawnArmed.store(false, std::memory_order_relaxed);
        return;
    }
    g_spawnX = pos.x;
    g_spawnY = pos.y + 0.35f;
    g_spawnZ = pos.z;
    g_spawnArmed.store(true, std::memory_order_release);
}

void InstallDetours() {
#if defined(SM_ALLOW_DOBBY_HOOK)
    if (!_monoCompileMethod) return;
    if (!ResolveApi()) return;

    void *methods[4] = {
            g_api.createBubble,
            g_api.createVisit,
            g_api.createActivity,
            g_api.createHide,
    };

    AttachMono();
    for (int i = 0; i < 4; ++i) {
        if (g_hookInstalled[i] || !methods[i]) continue;
        void *native = _monoCompileMethod(methods[i]);
        if (!native) continue;
        g_hookTargets[i] = native;
        int rc = DobbyHook(native, kSpawnHooks[i], reinterpret_cast<void **>(&g_hookOrig[i]));
        if (rc == RS_SUCCESS && g_hookOrig[i]) {
            g_hookInstalled[i] = true;
            LOGI(OBFUSCATE("[Bubble] detour %d -> %p"), i, native);
        } else {
            g_hookOrig[i] = nullptr;
            LOGI(OBFUSCATE("[Bubble] DobbyHook 失败 slot=%d rc=%d"), i, rc);
        }
    }
#else
    // 侧载构建：Dobby 被全局禁用，这里只记录一次提示，不做任何 hook。
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
        LOGI(OBFUSCATE("[Bubble] 侧载构建无 inline hook：游戏自身气泡的位置改写不可用"
                       "（本 mod 生成的气泡仍然生成在玩家位置）"));
    }
#endif
}

void WorkerLoop(uint64_t generation) {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    double accumulator = 0.0;

    while (g_workerRunning.load(std::memory_order_acquire) &&
           generation == g_workerGeneration.load(std::memory_order_acquire)) {
        auto now = clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (dt < 0.0) dt = 0.0;
        if (dt > 1.0) dt = 1.0;   // 切后台回来不要一次爆一屏

        const bool fast = g_fastBubbleGenEnabled;
        const bool spawnAtPlayer = g_bubbleSpawnAtPlayerEnabled;

        if (spawnAtPlayer) {
            RunOnUnityThreadAsync([]() {
                InstallDetours();
                UpdateSpawnArm();
            });
        } else {
            g_spawnArmed.store(false, std::memory_order_relaxed);
        }

        if (fast) {
            float rate = g_bubbleBubblesPerMinute;
            if (rate < 0.0f) rate = 0.0f;
            if (rate > 100.0f) rate = 100.0f;
            if (rate > 0.0f) {
                accumulator += static_cast<double>(rate) / 60.0 * dt;
                int budget = 0;
                while (accumulator >= 1.0 && budget < 8) {
                    accumulator -= 1.0;
                    ++budget;
                    RunOnUnityThreadAsync([]() { SpawnAtPlayer(); });
                }
                if (budget >= 8) accumulator = 0.0;
            }
        } else {
            accumulator = 0.0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace

void Start() {
    if (g_workerRunning.load(std::memory_order_acquire)) return;
    g_workerRunning.store(true, std::memory_order_release);
    uint64_t generation = g_workerGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    std::thread([generation]() { WorkerLoop(generation); }).detach();
    LOGI(OBFUSCATE("[Bubble] worker started gen=%llu"),
         static_cast<unsigned long long>(generation));
}

void Stop() {
    g_workerRunning.store(false, std::memory_order_release);
    g_workerGeneration.fetch_add(1, std::memory_order_acq_rel);
    g_spawnArmed.store(false, std::memory_order_relaxed);
}

void Refresh() {
    if (g_fastBubbleGenEnabled || g_bubbleSpawnAtPlayerEnabled) Start();
}

void ManualSpawn() {
    RunOnUnityThreadAsync([]() {
        if (!SpawnAtPlayer()) {
            LOGI(OBFUSCATE("[Bubble] 手动生成失败（世界/接口未就绪）"));
        }
    });
}

} // namespace BubbleFeatures
