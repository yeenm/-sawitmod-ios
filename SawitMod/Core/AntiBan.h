#pragma once
// ============================================================================
//  AntiBan.h — iOS 端「过检测 / 防封」模块
// ----------------------------------------------------------------------------
//  Android 版靠 libil2cpp.so 里的原生 RVA 去打 Themis/TapSDK 的补丁；iOS 的
//  UnityFramework 是另一份编译产物，RVA 不可复用。这里改成**按托管类名/方法名
//  在运行时解析**（il2cpp_class_from_name / il2cpp_class_get_method_from_name），
//  再把方法体首几条指令改写成立即返回 —— 与游戏版本、二进制布局解耦。
//
//  覆盖：
//    1. Themis/TapSDK 检测与上报（TapThemis / ThemisSDKManager / ThemisUtil /
//       TapThemisAgentIOS 回调），阻断检测结果上报、关掉 Themis 开关
//    2. 日志上传（Logan / LoganBridge / LoganManager.UploadLogs）
//    3. 举报/证据上传（XD.SDK.Report.XDGReport / XDReportImpl）
//    4. 通用文件上传（UploadManager / _UploadSystem.Upload）
//    5. BugReporter.ReportError
//    6. GameApp.Update 帧泵（类名解析后 DobbyHook，替代 OFFSET 方案）
//
//  全部幂等：重复调用只应用一次。
// ============================================================================

#include <string>

// 安装防封/过检测（il2cpp 就绪后调用；幂等）。返回成功打上的补丁数。
int SMAntiBanInstall();

// 仅安装 GameApp.Update 帧泵 hook（类名解析版，不需要 RVA）。
bool SMAntiBanHookFramePump();

// 运行期状态/统计（菜单展示用）。
std::string SMAntiBanStatus();
