#pragma once
// ============================================================================
//  OverlayWindow.h — 悬浮菜单窗口（iOS 版 SurfaceView + EGL 的替代者）
// ----------------------------------------------------------------------------
//  Android 版把 ImGui 画在游戏的 EGL surface 上（hook eglSwapBuffers），
//  点击靠 hook libinput 的 MotionEvent::copyFrom。
//
//  iOS 版完全换一种更安全的做法：
//    * 自建一个 UIWindow（level 高于游戏窗口），里面是 CAEAGLLayer + 独立
//      EAGLContext，用 CADisplayLink 以 60fps 画 ImGui
//    * 窗口背景透明，菜单以外区域的触摸通过 hitTest 返回 nil 直接穿透给游戏
//    * 不需要 hook 游戏的渲染/输入，互相不干扰
//
//  坐标约定：全部使用"像素"（renderbuffer 尺寸），触摸进来时按 contentsScale
//  从 point 换算成 pixel，再交给 ImGui。
// ============================================================================

#include <string>

namespace SMOverlay {

// 在主线程调用；重复调用是安全的。
bool Start();
void Stop();
bool IsRunning();

// 屏幕像素尺寸（渲染目标大小）。
float WidthPx();
float HeightPx();
float Scale();

// 菜单命中测试（像素坐标）。由 OverlayWindow 读取 Core/State.h 的
// g_MenuRect / g_MinRect —— 这两个矩形由 DrawMenu() 每帧更新。
bool PointInsideMenu(float px, float py);

// 轻提示（顶部浮层，1.6 秒淡出）。
void ShowToast(const std::string &text);

// 前后台切换：后台暂停 display link。
void SetForeground(bool foreground);

}  // namespace SMOverlay

// C 入口：App/Platform.mm 的 Toast 转发到这里。
void SMOverlayShowToast(const std::string &text);
