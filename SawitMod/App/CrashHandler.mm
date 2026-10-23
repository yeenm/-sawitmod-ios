// ============================================================================
//  CrashHandler.mm — 崩溃日志
// ----------------------------------------------------------------------------
//  Java 版 CrashHandler 抓的是托管异常；iOS 上真正致命的是信号（SIGSEGV/
//  SIGABRT/SIGBUS/SIGILL/SIGFPE/SIGTRAP）。这里注册 NSSetUncaughtExceptionHandler
//  + 信号处理器，用 write() 直接写文件，再恢复默认处理并重新抛出，保证系统
//  自己的崩溃报告还能生成。
//
//  日志落在 ~/Documents/mod_menu_crash_<yyyy_MM_dd-HH_mm_ss>.txt，内容与 Java
//  版对齐：时间、机型、系统版本、App 版本、完整调用栈。
//
//  ⚠️ 信号处理器里不能分配内存（std::string / NSString 都可能死锁），所以
//     所有设备信息、Documents 路径都在 Install() 时预先取好，处理器只用
//     snprintf + write + backtrace_symbols_fd。
// ============================================================================

#include "App/CrashHandler.h"

#import <Foundation/Foundation.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "App/Platform.h"

namespace {

char g_crashPath[512] = {0};
int g_crashFd = -1;
struct sigaction g_oldActions[6];

// Install() 时预取，供信号处理器只读使用。
char g_docDir[512] = {0};
char g_deviceModel[64] = {0};
char g_osVersion[64] = {0};
char g_appVersion[128] = {0};
char g_bundleId[128] = {0};

const char *SignalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS: return "SIGBUS";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGTRAP: return "SIGTRAP";
        default: return "SIGNAL";
    }
}

void WriteRaw(const char *text) {
    if (!text) return;
    if (g_crashFd >= 0) {
        (void)write(g_crashFd, text, strlen(text));
    }
}

void WriteHeader(int sig) {
    char line[512];
    const time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y_%m_%d-%H_%M_%S", &tmNow);

    WriteRaw("************* Crash Head ****************\n");
    snprintf(line, sizeof(line), "Time Of Crash      : %s\n", stamp);
    WriteRaw(line);
    snprintf(line, sizeof(line), "Device Model       : %s\n", g_deviceModel);
    WriteRaw(line);
    snprintf(line, sizeof(line), "System Version     : iOS %s\n", g_osVersion);
    WriteRaw(line);
    snprintf(line, sizeof(line), "App VersionName    : %s\n", g_appVersion);
    WriteRaw(line);
    snprintf(line, sizeof(line), "App BundleId       : %s\n", g_bundleId);
    WriteRaw(line);
    snprintf(line, sizeof(line), "Signal             : %s (%d)\n", SignalName(sig), sig);
    WriteRaw(line);
    snprintf(line, sizeof(line), "Last Step          : %s\n",
             SMPlatform::BreadcrumbCStr());
    WriteRaw(line);
    WriteRaw("--- recent steps (newest first) ---\n");
    {
        std::string steps[10];
        const int n = SMPlatform::BreadcrumbRingDump(steps, 10);
        for (int i = 0; i < n; ++i) {
            WriteRaw(steps[i].c_str());
            WriteRaw("\n");
        }
    }
    WriteRaw("************* Crash Head ****************\n\n");
}

void OpenCrashFile(void) {
    if (g_crashFd >= 0) return;
    const time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y_%m_%d-%H_%M_%S", &tmNow);
    snprintf(g_crashPath, sizeof(g_crashPath), "%s/mod_menu_crash_%s.txt", g_docDir, stamp);
    g_crashFd = open(g_crashPath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
}

void SignalHandler(int sig) {
    OpenCrashFile();
    WriteHeader(sig);
    WriteRaw("Stack trace:\n");
    if (g_crashFd >= 0) {
        void *frames[64];
        const int count = backtrace(frames, 64);
        backtrace_symbols_fd(frames, count, g_crashFd);
        WriteRaw("\n");
        close(g_crashFd);
        g_crashFd = -1;
    }

    // 恢复默认处理并重新抛，让系统崩溃报告与我们的日志同时存在。
    signal(sig, SIG_DFL);
    raise(sig);
}

void InstallSignal(int sig, int index) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(sig, &action, &g_oldActions[index]);
}

}  // namespace

namespace SMCrash {

void Install() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    // 预取设备/路径信息（此时还能安全分配内存）。
    snprintf(g_docDir, sizeof(g_docDir), "%s", SMPlatform::DocumentDir().c_str());
    snprintf(g_deviceModel, sizeof(g_deviceModel), "%s", SMPlatform::DeviceModel().c_str());
    snprintf(g_osVersion, sizeof(g_osVersion), "%s", SMPlatform::SystemVersion().c_str());
    snprintf(g_appVersion, sizeof(g_appVersion), "%s (%s)", SMPlatform::AppVersion().c_str(),
             SMPlatform::AppBuild().c_str());
    snprintf(g_bundleId, sizeof(g_bundleId), "%s", SMPlatform::BundleId().c_str());

    NSSetUncaughtExceptionHandler([](NSException *exception) {
        OpenCrashFile();
        WriteHeader(0);
        WriteRaw("Uncaught Objective-C exception:\n");
        WriteRaw([[exception description] UTF8String]);
        WriteRaw("\n");
        if (g_crashFd >= 0) {
            void *frames[64];
            const int count = backtrace(frames, 64);
            backtrace_symbols_fd(frames, count, g_crashFd);
            close(g_crashFd);
            g_crashFd = -1;
        }
    });

    InstallSignal(SIGSEGV, 0);
    InstallSignal(SIGABRT, 1);
    InstallSignal(SIGBUS, 2);
    InstallSignal(SIGILL, 3);
    InstallSignal(SIGFPE, 4);
    InstallSignal(SIGTRAP, 5);
}

const char *LastCrashPath() { return g_crashPath[0] ? g_crashPath : nullptr; }

}  // namespace SMCrash
