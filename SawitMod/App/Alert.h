#pragma once
// ============================================================================
//  Alert.h — 原生弹窗/输入框（替代 Java AlertDialog/Toast）
// ----------------------------------------------------------------------------
//  Android 版有两处"用 Java 弹窗"的地方：
//    1) NetAuth.showInputDialog()：输入卡密（需要系统键盘）
//    2) 各类 Toast / 错误提示
//  iOS 上用 UIAlertController：键盘、暗色适配、焦点都由系统处理，不会像
//  ImGui 输入框那样在注入进程里抢不到输入法。
// ============================================================================

#include <functional>
#include <string>

namespace SMAlert {

// 三键/两键提示
void Show(const std::string &title, const std::string &message,
          const std::string &okTitle = "OK");

// 两键确认
void Confirm(const std::string &title, const std::string &message,
             const std::string &okTitle, const std::string &cancelTitle,
             std::function<void(bool)> callback);

// 卡密输入框：点 OK 后把文本回传（在后台队列里回调）
void ShowInput(const std::string &title, const std::string &hint,
               const std::string &current,
               std::function<void(const std::string &)> callback);

// 轻提示（1.6 秒，不阻塞）
void Toast(const std::string &text);

}  // namespace SMAlert
