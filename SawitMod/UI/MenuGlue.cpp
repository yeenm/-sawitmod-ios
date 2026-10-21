// ============================================================================
//  MenuGlue.cpp — 字体/主题/水印 + 菜单渲染入口
// ----------------------------------------------------------------------------
//  Android 版这些逻辑在 Main.cpp 的 RenderImGuiFrame() 里（创建 ImGui 上下文
//  之后立即 SetupFonts/ApplyStyle，每帧 DrawMenu + RenderESPOverlay + 水印）。
//  iOS 版把渲染循环放到 App/OverlayWindow.mm，这里只保留"画什么"。
// ============================================================================

#include "UI/MenuGlue.h"

#include <cmath>
#include <string>

#include "ModUtils.h"
#include "Core/State.h"
#include "NetAuth.h"
#include "Resources/NotoSansSC.h"
#include "Version.h"

// CyloneUI 的 LoadTexture 直接用 GL；iOS 用 GLES3（与 imgui_impl_opengl3 一致）。
#if defined(IMGUI_IMPL_OPENGL_ES3)
#  if defined(__APPLE__)
#    include <OpenGLES/ES3/gl.h>
#  else
#    include <GLES3/gl3.h>
#  endif
#endif
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/custom/ActiveUI.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

// UI/Menu.cpp
void DrawMenu();
// Core/FramePump.cpp
void RenderESPOverlay();

namespace {

// Android 版 SAWIT_UI_FONT_SIZE_PX 之外还会乘 dpi；这里保持同一算法。
void BuildFontsAndStyle() {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    // 中文字形用 NotoSansSC 全量合并；拉丁字母由 CyloneUI 的 Inter 提供。
    // 注意：Android 版是在 SetupFonts 内部做这两步，这里交给它，保持一致。
    ActiveUI::SetupFonts((void *)NotoSansSC_Regular,
                         (int)sizeof(NotoSansSC_Regular));
    ActiveUI::LoadImages();
    ActiveUI::ApplyStyle();
}

}  // namespace

namespace SMMenu {

void Init(float widthPx, float heightPx, float scale) {
    (void)scale;

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    // 后端不实现"移动 OS 指针"，开着会让触摸位置在某些设备上跳。
    io.ConfigNavMoveSetMousePos = false;

    // DPI：以屏幕短边为基准，2K/4K 屏不会把 UI 放大成两倍。
    const float shortSide = (widthPx < heightPx) ? widthPx : heightPx;
    ActiveUI::device_dpi = ImClamp(shortSide / 1080.0f, 0.72f, 1.0f);
    ActiveUI::user_scale = LoadSavedUiScale();
    ActiveUI::dpi = ActiveUI::device_dpi * ActiveUI::user_scale;

    ActiveUI::window_pos = ImVec2(widthPx * 0.05f, heightPx * 0.06f);
    ActiveUI::window_size = ImVec2(0.f, 0.f);

    BuildFontsAndStyle();
    io.FontGlobalScale = 1.0f;
}

void ApplyScale(float scale) {
    if (scale < 0.25f) scale = 0.25f;
    if (scale > 2.00f) scale = 2.00f;

    const float oldDpi = ImMax(0.01f, ActiveUI::dpi);
    ActiveUI::user_scale = scale;
    ActiveUI::dpi = ActiveUI::device_dpi * ActiveUI::user_scale;
    const float ratio = ActiveUI::dpi / oldDpi;

    ActiveUI::window_size.x *= ratio;
    ActiveUI::window_size.y *= ratio;

    BuildFontsAndStyle();
    ImGui::GetIO().FontGlobalScale = 1.0f;
    SaveUiScale(scale);
}

void Draw() {
    DrawMenu();
    RenderESPOverlay();

    // ---- 授权水印：取卡密后 5 位，左下角，不接收输入 ----
    // 与 Android 版 RenderImGuiFrame 里的水印逻辑一致（同一位置/同一透明度）。
    ImGuiIO &io = ImGui::GetIO();
    std::string licenseSuffix = "00000";
    if (SesiAktif()) {
        const size_t licenseLength = strnlen(license_input, sizeof(license_input));
        if (licenseLength > 0) {
            const size_t take = (licenseLength < 5) ? licenseLength : 5;
            licenseSuffix.assign(license_input + licenseLength - take, take);
            if (licenseSuffix.size() < 5)
                licenseSuffix.insert(0, 5 - licenseSuffix.size(), '0');
        }
    }
    const float watermarkPad = ImMax(10.0f, 12.0f * ActiveUI::dpi);
    const ImVec2 watermarkSize = ImGui::CalcTextSize(licenseSuffix.c_str());
    const ImVec2 watermarkPos(watermarkPad,
                              ImMax(watermarkPad, io.DisplaySize.y - watermarkSize.y - watermarkPad));
    ImDrawList *foreground = ImGui::GetForegroundDrawList();
    foreground->AddText(watermarkPos + ImVec2(1.0f, 1.0f), IM_COL32(0, 0, 0, 120),
                        licenseSuffix.c_str());
    foreground->AddText(watermarkPos, IM_COL32(255, 255, 255, 145), licenseSuffix.c_str());
}

}  // namespace SMMenu
