#pragma once
// ============================================================================
//  MenuGlue.h — 菜单与渲染循环之间的胶合层
// ----------------------------------------------------------------------------
//  UI/Menu.cpp 里是从 Android 版原样搬过来的 DrawMenu()；字体/DPI/主题这些
//  需要 GL 上下文和 ImGui 上下文的初始化放在 MenuGlue.cpp 里。
//
//  调用顺序（见 App/OverlayWindow.mm）：
//      ImGui::CreateContext()
//      ImGui_ImplOpenGL3_Init("#version 300 es")
//      SMMenu::Init(w, h, scale)      <- 字体、样式、DPI
//      每帧：SMMenu::Draw()
// ============================================================================

namespace SMMenu {

// 初始化字体/主题/DPI。必须在 GL 上下文与 ImGui 上下文都就绪后调用。
void Init(float widthPx, float heightPx, float scale);

// 每帧绘制：菜单窗口 + ESP 叠加 + 授权水印。
void Draw();

// 用户改了界面缩放（设置页）后调用，重新构建字体。
void ApplyScale(float scale);

}  // namespace SMMenu
