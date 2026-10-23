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

std::string Clipboard() {
    NSString *s = [UIPasteboard generalPasteboard].string;
    return UTF8(s);
}


// ---- 崩溃现场面包屑 ----
static std::mutex g_bcMutex;
static char g_bcBuf[128] = {0};

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
    std::lock_guard<std::mutex> lk(g_bcMutex);
    snprintf(g_bcBuf, sizeof(g_bcBuf), "%s", step.c_str());
}

std::string TakeBreadcrumb() {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    NSString *v = [d stringForKey:@"sm_breadcrumb"];
    [d removeObjectForKey:@"sm_breadcrumb"];
    [d synchronize];
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
