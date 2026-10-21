// ============================================================================
//  Bootstrap.mm — iOS 启动编排
// ----------------------------------------------------------------------------
//  Android 版的 hack_thread 做了这些事：等 libil2cpp -> 打 Themis/SDK 补丁 ->
//  拿 JavaVM/Context -> 装 eglSwapBuffers/input hook -> 等 Mono -> 挂
//  GameApp.Update。iOS 版没有 Java 层、也不 hook 游戏渲染，对应关系：
//
//    libil2cpp.so            -> UnityFramework / 主二进制（dyld）
//    Themis/SDK 补丁          -> 可选 native 补丁（Offsets-iOS.h 标定后启用）
//    JavaVM/Context           -> 不需要
//    eglSwapBuffers/input     -> 自建 UIWindow + CAEAGLLayer（OverlayWindow.mm）
//    Mono 就绪                -> il2cpp 符号可解析 + 主程序集已加载
//    GameApp.Update hook      -> 同位置挂钩（偏移需按 iOS 包重新标定）
// ============================================================================

#include "App/Bootstrap.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <atomic>
#include <csignal>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "App/CrashHandler.h"
#include "App/OverlayWindow.h"
#include "App/Platform.h"
#include "ClientControlAgent.h"
#include "ModFeatures.h"
#include "ModUtils.h"
#include "Core/Offsets-iOS.h"
#include "Core/State.h"
#include "Global.h"
#include "IL2CPP/Il2CppApi.h"
#include "IL2CPP/MonoCompat.h"
#include "Includes/Dobby/dobby.h"
#include "Includes/Logger.h"
#include "Includes/Utils.h"
#include "LocalCatalog.h"
#include "NetAuth.h"
#include "Version.h"

#include "Core/FramePump.h"

namespace {

std::atomic<bool> g_runtimeReady{false};

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

bool WaitForIl2Cpp(int timeoutSeconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (SMIL2CPP::Ready() && SMIL2CPP::MainGameImage() != nullptr) {
            // 运行时符号齐了不等于托管元数据可用，再让 Unity 走几帧。
            SleepMs(300);
            return true;
        }
        SleepMs(250);
    }
    return false;
}

// 安装 GameApp.Update 的帧泵 hook。偏移 0 表示还没按 iOS 包标定：
// 此时不挂钩，菜单照样能开，只是"每帧逻辑"类功能（自动采集/雷达等）不会跑。
bool InstallFramePumpHook() {
    if (OFF_GAMEAPP_UPDATE == 0) {
        NSLog(@"[SawitMod] OFF_GAMEAPP_UPDATE 未标定，跳过 GameApp.Update hook");
        return false;
    }
    const uintptr_t base = SMImage::FindImageBase("UnityFramework");
    if (base == 0) return false;
    const uintptr_t target = base + OFF_GAMEAPP_UPDATE;
    if (target < base) return false;
    const int rc = DobbyHook((void *)target, (void *)SMGameAppUpdate,
                             (void **)&SMOldGameAppUpdate);
    const bool ok = (rc == 0) && SMOldGameAppUpdate != nullptr;
    NSLog(@"[SawitMod] GameApp.Update hook %@ (target=%p)", ok ? @"ok" : @"failed",
          (void *)target);
    return ok;
}

void BootstrapThread() {
    // 1) 窗口：等 App 的 delegate 起来再创建（iOS 13+ 必须有 windowScene）。
    //    冷启动时 scene 可能晚到，这里最多重试 ~10 秒。
    SleepMs(1500);
    dispatch_async(dispatch_get_main_queue(), ^{
        __block int attempt = 0;
        __block void (^tryStart)(void);
        tryStart = ^{
            if (SMOverlay::Start()) return;
            if (++attempt >= 20) {
                NSLog(@"[SawitMod] overlay 启动失败：windowScene 一直不可用");
                return;
            }
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)),
                           dispatch_get_main_queue(), tryStart);
        };
        tryStart();
    });

    // 2) 本地授权：Android 版在 SetupJavaJNI 之后立刻读，这里同样尽早。
    LoadLicenseLocally();

#if SAWIT_OFFLINE_MODE
    // 离线构建：不请求服务器，装本地会话 + 内嵌功能目录（免费功能才点得动）。
    if (!SesiAktif()) {
        LoginOfflineSession(false);
    }
    LocalCatalog::InstallEmbedded();
#endif

    // 3) il2cpp：等符号与主程序集就绪（最多 120 秒，不阻塞主线程）。
    if (!WaitForIl2Cpp(120)) {
        NSLog(@"[SawitMod] il2cpp 未就绪，托管相关功能保持关闭");
        return;
    }

    SMMono::InstallPointers();
    SMMono::Probe();
    g_runtimeReady.store(true);

    // 4) 帧泵 hook：失败不影响菜单。
    const bool pumpReady = InstallFramePumpHook();
    g_unityFramePumpReady.store(pumpReady);

    // 5) 遥测/远程控制：必须在运行时就绪之后。
    ClientControlAgent::InitializeAfterMono();

    NSLog(@"[SawitMod] bootstrap done (pump=%@)", pumpReady ? @"on" : @"off");
}

}  // namespace

namespace SMBootstrap {

void Start() {
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;

    // 裸 socket 遇到对端关闭会发 SIGPIPE，默认动作是杀进程；
    // iOS 上等价于"闪退"。全局忽略，配合 Weiyan 里的 SO_NOSIGPIPE。
    signal(SIGPIPE, SIG_IGN);

    // 上次闪退停在哪一步（没有则为空）
    g_prevBreadcrumb = SMPlatform::TakeBreadcrumb();
    SMPlatform::SetBreadcrumb("boot");

    SMCrash::Install();

    std::thread([]() {
        @autoreleasepool {
            BootstrapThread();
        }
    }).detach();
}

bool RuntimeReady() { return g_runtimeReady.load(); }

}  // namespace SMBootstrap
