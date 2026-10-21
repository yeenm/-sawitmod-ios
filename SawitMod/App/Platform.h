#pragma once
// ============================================================================
//  Platform.h — iOS 平台能力（替代 Android 的 JNI/Context 调用）
// ----------------------------------------------------------------------------
//  Android 版里这些能力都靠 Java：
//    * 路径       -> /data/data/<pkg>/files、/storage/emulated/0/...
//    * HWID       -> Settings.Secure.ANDROID_ID
//    * 弹提示     -> Toast
//    * 打开链接   -> Intent(ACTION_VIEW)
//    * 结束进程   -> Process.killProcess
//    * 剪贴板     -> ClipboardManager
//  iOS 版统一走 Foundation，签名保持 C++ 可调用。
// ============================================================================

#include <cstdint>
#include <functional>
#include <string>

namespace SMPlatform {

// ---- 路径 ----
std::string DocumentDir();     // ~/Documents：崩溃日志、授权文件、导出数据
std::string LibraryDir();      // ~/Library：设置、缓存
std::string DataDir();         // Library/Application Support/SawitMod（可创建）
std::string BundlePath();
std::string BundleId();
std::string AppVersion();
std::string AppBuild();

// ---- 设备 ----
std::string DeviceModel();     // 例如 iPhone15,2
std::string SystemVersion();   // 例如 17.4
std::string DeviceName();      // 用户设备名（可能含隐私，仅诊断用）
// 稳定设备码：Keychain 里持久化的 UUID（卸载重装仍是同一个），失败时回落到
// identifierForVendor。服务端把 HWID 当设备绑定用，必须稳定。
std::string HWID();
// Android 版 MachineCode 的替代：短设备指纹（HWID 前 16 位 hex）。
std::string DeviceCode();

// ---- 行为 ----
void OpenURL(const std::string &url);
void Quit();                       // exit(0)
void Toast(const std::string &text);  // 屏幕顶部轻提示（不阻塞）
std::string Clipboard();

// 后台线程里调 Foundation（Keychain/UIDevice/路径）时包一层 autorelease pool。
void RunWithAutoreleasePool(const std::function<void()> &body);

// 崩溃现场定位：把“最后一步”写进 NSUserDefaults，闪退后下次启动可读回。
void SetBreadcrumb(const std::string &step);
std::string TakeBreadcrumb();
const char *BreadcrumbCStr();
void SetClipboard(const std::string &text);

// ---- 诊断 ----
bool IsDebuggerAttached();
std::string ProcessName();

}  // namespace SMPlatform
