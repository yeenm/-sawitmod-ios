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
// 卡密登录失败回调（对齐安卓端）：把失败原因统一拼成「卡密验证失败：<原因>」
// 再用悬浮轻提示弹出来。reason 为空时用兜底文案。可从后台线程调用。
void NotifyAuthFailure(const std::string &reason);
std::string Clipboard();

// 后台线程里调 Foundation（Keychain/UIDevice/路径）时包一层 autorelease pool。
void RunWithAutoreleasePool(const std::function<void()> &body);

// 崩溃现场定位：把“最后一步”写进 NSUserDefaults，闪退后下次启动可读回。
void SetBreadcrumb(const std::string &step);
// 轻量版：只更新内存缓冲（崩溃处理器会把它写进崩溃报告头），
// 不做 fsync/NSUserDefaults，可放在每帧/功能热路径上。
void SetBreadcrumbLight(const std::string &step);
// 崩溃用：最近 N 条轻量面包屑（带线程 tag），崩溃处理器逐条写进报告。
int BreadcrumbRingDump(std::string *outLines, int maxLines);
// 重启后取证：SetBreadcrumbLight 每次都会把最近 8 条轻量面包屑
// （格式 "tXXXX | step"，每行一条，新→旧）落到 DataDir()/recent_steps.txt。
// 环形缓冲是纯内存态，闪退重启后会被清空；菜单靠这个接口把「上次运行的
// 最近步骤」读回来显示。最多读 maxLines 条（顺序与文件一致，新→旧）；
// 文件不存在（首次安装/从未打点）返回 0。
int LoadRecentStepsFromFile(std::string *outLines, int maxLines);
std::string TakeBreadcrumb();
const char *BreadcrumbCStr();
void SetClipboard(const std::string &text);

// ---- 诊断 ----
bool IsDebuggerAttached();
std::string ProcessName();

}  // namespace SMPlatform
