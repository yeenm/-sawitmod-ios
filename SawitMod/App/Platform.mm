// ============================================================================
//  Platform.mm — iOS 平台能力实现
// ============================================================================

#include "App/Platform.h"
#include "App/OverlayWindow.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <UIKit/UIKit.h>
#import <sys/sysctl.h>
#import <sys/proc.h>
#import <sys/utsname.h>
#import <unistd.h>

#include <mach-o/dyld.h>
#include <cstdio>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <pthread.h>
#include <mutex>
#include <cstring>

namespace {

NSString *kKeychainService = @"com.sawit.modmenu";
NSString *kKeychainAccount = @"hwid";

NSString *KeychainRead(NSString *service, NSString *account) {
    NSDictionary *query = @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : service,
        (__bridge id)kSecAttrAccount : account,
        (__bridge id)kSecReturnData : @YES,
        (__bridge id)kSecMatchLimit : (__bridge id)kSecMatchLimitOne,
    };
    CFTypeRef result = nullptr;
    if (SecItemCopyMatching((__bridge CFDictionaryRef)query, &result) != errSecSuccess) return nil;
    NSData *data = (__bridge_transfer NSData *)result;
    if (!data) return nil;
    return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
}

BOOL KeychainWrite(NSString *service, NSString *account, NSString *value) {
    NSData *data = [value dataUsingEncoding:NSUTF8StringEncoding];
    NSDictionary *query = @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : service,
        (__bridge id)kSecAttrAccount : account,
    };
    SecItemDelete((__bridge CFDictionaryRef)query);
    NSMutableDictionary *add = [query mutableCopy];
    add[(__bridge id)kSecValueData] = data;
    add[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleAfterFirstUnlock;
    return SecItemAdd((__bridge CFDictionaryRef)add, nullptr) == errSecSuccess;
}

std::string UTF8(NSString *s) {
    if (!s) return std::string();
    const char *c = [s UTF8String];
    return c ? std::string(c) : std::string();
}

NSString *NSStringFromStd(const std::string &s) {
    return [[NSString alloc] initWithBytes:s.data()
                                    length:s.size()
                                  encoding:NSUTF8StringEncoding] ?: @"";
}

NSString *DocumentsPath() {
    static NSString *path = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        path = dirs.firstObject ?: NSTemporaryDirectory();
    });
    return path;
}

}  // namespace

namespace SMPlatform {

std::string DocumentDir() { return UTF8(DocumentsPath()); }

std::string LibraryDir() { return UTF8(NSTemporaryDirectory()); }

std::string DataDir() {
    // 注意：渲染线程和登录线程都会取这个路径，必须线程安全。
    // 之前是 static std::string + if (!empty)，两个线程同时首次调用会撕裂 std::string。
    static std::string cached;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,
                                                            NSUserDomainMask, YES);
        NSString *base = dirs.firstObject ?: DocumentsPath();
        NSString *dir = [base stringByAppendingPathComponent:@"SawitMod"];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        cached = UTF8(dir);
    });
    return cached;
}

std::string BundlePath() { return UTF8([[NSBundle mainBundle] bundlePath]); }

std::string BundleId() { return UTF8([[NSBundle mainBundle] bundleIdentifier]); }

std::string AppVersion() {
    NSString *v = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    return UTF8(v ?: @"unknown");
}

std::string AppBuild() {
    NSString *v = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"];
    return UTF8(v ?: @"0");
}

std::string DeviceModel() {
    struct utsname u;
    if (uname(&u) == 0) return std::string(u.machine);
    return "unknown";
}

std::string SystemVersion() { return UTF8([UIDevice currentDevice].systemVersion); }

std::string DeviceName() { return UTF8([UIDevice currentDevice].name); }

std::string HWID() {
    // ⚠️ 不用 Keychain：侧载/自签 App 的 entitlements 里通常没有
    // keychain-access-groups，第一次 SecItemCopyMatching 就可能被 securityd
    // 直接 SIGKILL（无 crash 文件、表现就是“点登录卡密秒退”）。
    // 设备码改成「Documents 旁边的 DataDir 里存一份 UUID 文本」，卸载重装会变，
    // 但注入场景够用，也不会碰系统安全组件。
    static std::string cached;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSString *dir = [NSString stringWithUTF8String:DataDir().c_str()];
        NSString *file = [dir stringByAppendingPathComponent:@"hwid.txt"];
        NSString *stored = [NSString stringWithContentsOfFile:file
                                                     encoding:NSUTF8StringEncoding
                                                        error:nil];
        stored = [stored stringByTrimmingCharactersInSet:
                             [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (stored.length >= 8) {
            cached = UTF8(stored);
            return;
        }
        NSString *vendor = [[[UIDevice currentDevice] identifierForVendor] UUIDString];
        NSString *value = vendor.length > 0 ? vendor : [[NSUUID UUID] UUIDString];
        NSString *clean = [[value stringByReplacingOccurrencesOfString:@"-" withString:@""]
            lowercaseString];
        [clean writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:nil];
        cached = UTF8(clean);
    });
    return cached.empty() ? "UNKNOWN_HWID" : cached;
}

std::string DeviceCode() {
    const std::string hwid = HWID();
    if (hwid.size() >= 16) return hwid.substr(0, 16);
    return hwid;
}

void OpenURL(const std::string &url) {
    NSString *str = NSStringFromStd(url);
    NSURL *nsurl = [NSURL URLWithString:str];
    if (!nsurl) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        [[UIApplication sharedApplication] openURL:nsurl
                                           options:@{}
                                 completionHandler:nil];
    });
}

void Quit() {
    dispatch_async(dispatch_get_main_queue(), ^{
        // 先尝试干净退出：主线程 runloop 停掉再 _exit，避免 atexit 里的析构崩。
        _exit(0);
    });
    usleep(300 * 1000);
    _exit(0);
}

void Toast(const std::string &text) {
    // 轻提示放在 Overlay 的独立窗口上，避免和菜单的 GL 视图抢图层。
    SMOverlayShowToast(text);
}

void NotifyAuthFailure(const std::string &reason) {
    // 安卓端在卡密验证失败时会把面板返回的原因顶到状态栏/Toast。
    // iOS 侧统一走这个回调：文案固定前缀 + 原因，后台线程也能安全调用
    // （SMOverlayShowToast 内部已 dispatch 到主线程）。
    std::string body = reason;
    if (body.empty()) body = u8"未知原因（面板未返回信息）";
    Toast(std::string(u8"卡密验证失败：") + body);
}

std::string Clipboard() {
    NSString *s = [UIPasteboard generalPasteboard].string;
    return UTF8(s);
}


// ---- 崩溃现场面包屑 ----
static std::mutex g_bcMutex;
static char g_bcBuf[128] = {0};

// ---- 轻量面包屑环形缓冲（崩溃时由 CrashHandler 读出，无需加锁无分配）----
static constexpr int kBcRingSize = 16;
static constexpr int kBcMsgLen = 96;
static constexpr int kBcTagLen = 24;
static char g_bcRingMsg[kBcRingSize][kBcMsgLen];
static char g_bcRingTag[kBcRingSize][kBcTagLen];
static std::atomic<unsigned> g_bcRingIdx{0};

static unsigned long long CurrentThreadTag() {
    return (unsigned long long)(uintptr_t)pthread_self() * 2654435761ULL;
}

int BreadcrumbRingDump(std::string *outLines, int maxLines) {
    if (!outLines || maxLines <= 0) return 0;
    const unsigned idx = g_bcRingIdx.load(std::memory_order_relaxed);
    int count = 0;
    for (int i = 1; i <= kBcRingSize && count < maxLines; ++i) {
        const unsigned slot = (idx + kBcRingSize - i) % kBcRingSize;
        if (g_bcRingMsg[slot][0] == 0) break;
        char line[160];
        snprintf(line, sizeof(line), "%s | %s", g_bcRingTag[slot], g_bcRingMsg[slot]);
        outLines[count++] = line;
    }
    return count;
}

// ---- 最近步骤落盘（闪退重启后仍能读到）----
// 环形缓冲是纯内存态，进程一死就没了；而用户恰恰是崩溃后重启才看菜单。
// 所以每次轻量打点都顺手把 ring 的最新 8 条覆盖写到 DataDir()/recent_steps.txt。
// 只 fopen("w")+fputs+fclose，不做 fsync（页缓存就够活过 SIGSEGV/SIGKILL，
// 又不阻塞每帧热路径）；200ms 内的重复调用直接合并跳过。
static const char *RecentStepsFilePath() {
    static std::string path;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        path = DataDir() + "/recent_steps.txt";
    });
    return path.c_str();
}

static void PersistRecentSteps() {
    // 在锁内取一份 ring 快照（8 条 snprintf，微秒级），再在锁外写文件，
    // 避免和热路径上的打点抢锁时把 fopen 也包进临界区。
    std::string lines[12];
    int n = 0;
    {
        std::lock_guard<std::mutex> lk(g_bcMutex);
        n = BreadcrumbRingDump(lines, 12);
    }
    if (n <= 0) return;
    FILE *fp = fopen(RecentStepsFilePath(), "w");
    if (!fp) return;
    for (int i = 0; i < n; ++i) {
        fputs(lines[i].c_str(), fp);
        fputc('\n', fp);
    }
    fclose(fp);
}

int LoadRecentStepsFromFile(std::string *outLines, int maxLines) {
    if (!outLines || maxLines <= 0) return 0;
    FILE *fp = fopen(RecentStepsFilePath(), "r");
    if (!fp) return 0;
    int count = 0;
    char buf[256];
    while (count < maxLines && fgets(buf, sizeof(buf), fp)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        if (n == 0) continue;  // 跳过空行（上一版留下的多余换行）
        outLines[count++] = buf;
    }
    fclose(fp);
    return count;
}



void SetBreadcrumb(const std::string &step) {
    {
        std::lock_guard<std::mutex> lk(g_bcMutex);
        snprintf(g_bcBuf, sizeof(g_bcBuf), "%s", step.c_str());
    }
    NSString *key = @"sm_breadcrumb";
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setObject:[NSString stringWithUTF8String:step.c_str()] forKey:key];
    [d synchronize];

    // 再写一份到文件并 fsync：SIGKILL（被系统杀）时 NSUserDefaults 不一定落盘，
    // 文件里这条就是下一次启动能看到的“最后一步”。
    NSString *path = [NSString stringWithUTF8String:(DataDir() + "/last_step.txt").c_str()];
    FILE *fp = fopen([path fileSystemRepresentation], "w");
    if (fp) {
        fputs(step.c_str(), fp);
        fflush(fp);
        fsync(fileno(fp));
        fclose(fp);
    }
}


void SetBreadcrumbLight(const std::string &step) {
    {
        std::lock_guard<std::mutex> lk(g_bcMutex);
        snprintf(g_bcBuf, sizeof(g_bcBuf), "%s", step.c_str());
        const unsigned slot = g_bcRingIdx.fetch_add(1, std::memory_order_relaxed) % kBcRingSize;
        snprintf(g_bcRingMsg[slot], kBcMsgLen, "%s", step.c_str());
        snprintf(g_bcRingTag[slot], kBcTagLen, "t%llx", CurrentThreadTag() & 0xFFFFFULL);
    }
    // 同步写文件（不做 fsync）：写进内核页缓存就够——SIGSEGV/SIGKILL 后
    // 下一次启动能从 last_step.txt 读回来显示在菜单上（上一版只写内存，
    // 菜单只能读到很久以前的重面包屑，定位不到真实崩溃点）。
    static NSString *stepPath = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        stepPath = [NSString stringWithUTF8String:(DataDir() + "/last_step.txt").c_str()];
    });
    if (!stepPath) return;
    FILE *fp = fopen([stepPath fileSystemRepresentation], "w");
    if (fp) {
        fputs(step.c_str(), fp);
        fclose(fp);
    }

    // 崩溃前最后几步一条都不能丢：每次打点都整段覆盖写 recent_steps.txt
    PersistRecentSteps();
}

std::string TakeBreadcrumb() {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    NSString *v = [d stringForKey:@"sm_breadcrumb"];
    [d removeObjectForKey:@"sm_breadcrumb"];
    [d synchronize];
    // 轻量打点（last_step.txt）比 NSUserDefaults 新：优先用它。
    {
        NSString *lp = [NSString stringWithUTF8String:(DataDir() + "/last_step.txt").c_str()];
        NSString *lf = [NSString stringWithContentsOfFile:lp
                                                encoding:NSUTF8StringEncoding
                                                   error:nil];
        lf = [lf stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (lf.length > 0) v = lf;
    }
    if (v.length == 0) {
        // NSUserDefaults 没写进去（被 SIGKILL 打断）时，退回文件里的最后一步。
        NSString *path = [NSString stringWithUTF8String:(DataDir() + "/last_step.txt").c_str()];
        NSString *f = [NSString stringWithContentsOfFile:path
                                                encoding:NSUTF8StringEncoding
                                                   error:nil];
        f = [f stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (f.length > 0) v = f;
    }
    [[NSFileManager defaultManager] removeItemAtPath:
        [NSString stringWithUTF8String:(DataDir() + "/last_step.txt").c_str()] error:nil];
    return v ? UTF8(v) : std::string();
}

const char *BreadcrumbCStr() { return g_bcBuf; }

void RunWithAutoreleasePool(const std::function<void()> &body) {
    @autoreleasepool {
        if (body) body();
    }
}

void SetClipboard(const std::string &text) {
    NSString *s = NSStringFromStd(text);
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIPasteboard generalPasteboard].string = s;
    });
}

bool IsDebuggerAttached() {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    struct kinfo_proc info;
    memset(&info, 0, sizeof(info));
    size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0) return false;
    return (info.kp_proc.p_flag & P_TRACED) != 0;
}

std::string ProcessName() { return UTF8([NSProcessInfo processInfo].processName); }

}  // namespace SMPlatform
