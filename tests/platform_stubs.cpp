// ============================================================================
//  platform_stubs.cpp — 链接自检用的平台符号桩
// ----------------------------------------------------------------------------
//  只给 scripts/check_link.ps1 用：在 Windows 上把 C++ 侧全部目标文件链接成
//  一个可执行文件，用来发现"声明了但没人定义"的符号。iOS 构建不使用本文件。
// ============================================================================

#include <cstring>
#include <string>

#include "App/Defaults.h"
#include "App/HttpClient.h"
#include "App/Platform.h"
#include "Core/Crypto.h"
#include "Includes/Dobby/dobby.h"

// ---- Dobby ----
extern "C" int DobbyHook(void *, void *, void **) { return 0; }
extern "C" int DobbyUnhook(void *, void *) { return 0; }
extern "C" void *DobbySymbolResolver(const char *, const char *) { return nullptr; }
extern "C" int DobbyDestroy(void *) { return 0; }

// ---- SMPlatform ----
namespace SMPlatform {
std::string DocumentDir() { return "C:/tmp/docs"; }
std::string LibraryDir() { return "C:/tmp/lib"; }
std::string DataDir() { return "C:/tmp/data"; }
std::string BundlePath() { return "C:/tmp/app"; }
std::string BundleId() { return "com.stub.app"; }
std::string AppVersion() { return "1.1"; }
std::string AppBuild() { return "1"; }
std::string DeviceModel() { return "iPhone0,0"; }
std::string SystemVersion() { return "17.0"; }
std::string DeviceName() { return "stub"; }
std::string HWID() { return "0123456789abcdef"; }
std::string DeviceCode() { return "0123456789abcdef"; }
void OpenURL(const std::string &) {}
void Quit() {}
void Toast(const std::string &) {}
std::string Clipboard() { return std::string(); }
void SetClipboard(const std::string &) {}
void RunWithAutoreleasePool(const std::function<void()> &body) { if (body) body(); }
void SetBreadcrumb(const std::string &) {}
std::string TakeBreadcrumb() { return std::string(); }
const char *BreadcrumbCStr() { return ""; }
bool IsDebuggerAttached() { return false; }
std::string ProcessName() { return "stub"; }
}  // namespace SMPlatform

// ---- SMHttp ----
namespace SMHttp {
Response Post(const std::string &, const std::string &, const std::string &, int) { return Response(); }
Response Get(const std::string &, int) { return Response(); }
std::string PostJson(const std::string &, const std::string &, int) { return std::string(); }
}  // namespace SMHttp

// ---- SMDefaults ----
namespace SMDefaults {
bool GetBool(const std::string &, bool def) { return def; }
void SetBool(const std::string &, bool) {}
int GetInt(const std::string &, int def) { return def; }
void SetInt(const std::string &, int) {}
std::string GetString(const std::string &, const std::string &def) { return def; }
void SetString(const std::string &, const std::string &) {}
void Remove(const std::string &) {}
void Synchronize() {}
}  // namespace SMDefaults

// ---- SMCrypto：Apple 侧（ECDH/ECDSA）在 iOS 由 CryptoApple.mm 提供 ----
namespace SMCrypto {
void *EcdhGenerateEphemeral(std::string &out) {
    out.clear();
    return nullptr;
}
void EcdhFree(void *) {}
bool EcdhComputeShared(void *, const std::string &, uint8_t out[32]) {
    memset(out, 0, 32);
    return false;
}
bool EcdsaVerifyP256(const std::vector<uint8_t> &, const uint8_t[32],
                     const std::vector<uint8_t> &) {
    return false;
}
}  // namespace SMCrypto

// ---- 崩溃处理器 ----
namespace SMCrash {
void Install() {}
const char *LastCrashPath() { return nullptr; }
}  // namespace SMCrash

// ---- libSystem（iOS 由 dyld/libSystem 提供；Windows 自检给空实现）----
extern "C" void *dlopen(const char *, int) { return nullptr; }
extern "C" void *dlsym(void *, const char *) { return nullptr; }
extern "C" int dlclose(void *) { return 0; }
extern "C" char *dlerror() { return nullptr; }

extern "C" unsigned int _dyld_image_count(void) { return 0; }
extern "C" const char *_dyld_get_image_name(unsigned int) { return nullptr; }
extern "C" const void *_dyld_get_image_header(unsigned int) { return nullptr; }
extern "C" intptr_t _dyld_get_image_vmaddr_slide(unsigned int) { return 0; }
