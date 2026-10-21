#pragma once

#include "../imgui.h"
#include "../imgui_internal.h"
#include "../../Version.h"   // SAWIT_UI_BOLD_FONT / SAWIT_UI_FONT_SIZE_PX
#include "BoldFont.h"
#include <cmath>
#include <cstdio>
#include <cfloat>

// Premium Newbie chrome. This target stays independent from CyloneUI and its
// embedded Sawit assets; the animated GIF remains the visual background.
namespace NewbieUI {

inline float dpi = 1.0f;
inline float device_dpi = 1.0f;
inline float user_scale = 1.0f;
inline int tab = 0;
inline int tab_stored = 0;
inline ImVec2 window_pos(70.0f, 55.0f);
inline ImVec2 window_size(960.0f, 600.0f);

inline float S(float value) { return std::round(value * dpi); }
inline ImVec2 S2(float x, float y) { return ImVec2(S(x), S(y)); }
inline float Clamp01(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
inline float Smooth(float current, float target, float speed = 12.0f) {
    const float dt = ImGui::GetIO().DeltaTime > 0.0f ? ImGui::GetIO().DeltaTime : (1.0f / 60.0f);
    return current + (target - current) * Clamp01(dt * speed);
}
inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    t = Clamp01(t);
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
inline ImU32 U32(const ImVec4& color, float alpha = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
}

inline const ImVec4 kVoid       = ImVec4(0.018f, 0.016f, 0.035f, 1.00f);
inline const ImVec4 kSurface    = ImVec4(0.055f, 0.046f, 0.092f, 1.00f);
inline const ImVec4 kSurfaceHi  = ImVec4(0.105f, 0.083f, 0.165f, 1.00f);
inline const ImVec4 kAccent     = ImVec4(0.655f, 0.345f, 1.000f, 1.00f);
inline const ImVec4 kAccentHot  = ImVec4(0.820f, 0.500f, 1.000f, 1.00f);
inline const ImVec4 kMint       = ImVec4(0.220f, 0.965f, 0.720f, 1.00f);
inline const ImVec4 kText       = ImVec4(0.965f, 0.955f, 1.000f, 1.00f);
inline const ImVec4 kTextSoft   = ImVec4(0.655f, 0.635f, 0.740f, 1.00f);
inline const ImVec4 kBorder     = ImVec4(0.620f, 0.420f, 0.900f, 0.40f);

inline void SetupFonts(void* notoData, int notoSize) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    // 与 SawitUI 保持一致：加粗 loader + 更大的字号（开关见 Version.h）。
#if SAWIT_UI_BOLD_FONT
    if (const ImFontLoader* boldLoader = BoldFont::Loader())
        io.Fonts->SetFontLoader(boldLoader);
#endif
    float body = S(SAWIT_UI_FONT_SIZE_PX - 1.0f);
    if (body < SAWIT_UI_FONT_SIZE_PX - 1.0f) body = SAWIT_UI_FONT_SIZE_PX - 1.0f;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    if (notoData && notoSize > 0)
        io.Fonts->AddFontFromMemoryTTF(notoData, notoSize, body, &cfg,
                                      io.Fonts->GetGlyphRangesChineseFull());
    else
        io.Fonts->AddFontDefault();
    io.Fonts->Build();
}
inline void LoadImages() {}

inline void ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = S(16.0f);
    style.ChildRounding = S(13.0f);
    style.FrameRounding = S(9.0f);
    style.PopupRounding = S(12.0f);
    style.GrabRounding = S(10.0f);
    style.TabRounding = S(9.0f);
    style.ScrollbarRounding = S(10.0f);
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = S2(12, 9);
    style.ItemSpacing = S2(10, 10);
    style.ItemInnerSpacing = S2(8, 6);
    style.CellPadding = S2(10, 8);
    style.IndentSpacing = S(19.0f);
    // Area sentuh scrollbar dibuat setebal SawitUI agar nyaman digeser di Android.
    style.ScrollbarSize = S(36.0f);
    style.GrabMinSize = S(28.0f);
    style.WindowMinSize = S2(620, 400);
    style.AntiAliasedLines = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill = true;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = kText;
    c[ImGuiCol_TextDisabled]         = kTextSoft;
    c[ImGuiCol_WindowBg]             = ImVec4(kVoid.x, kVoid.y, kVoid.z, 0.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(kSurface.x, kSurface.y, kSurface.z, 0.52f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.045f, 0.035f, 0.075f, 0.98f);
    c[ImGuiCol_Border]               = kBorder;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = ImVec4(0.080f, 0.065f, 0.125f, 0.90f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.145f, 0.105f, 0.215f, 0.96f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.190f, 0.120f, 0.285f, 1.00f);
    c[ImGuiCol_TitleBg]              = kSurface;
    c[ImGuiCol_TitleBgActive]        = kSurfaceHi;
    c[ImGuiCol_CheckMark]            = kMint;
    c[ImGuiCol_SliderGrab]           = kAccentHot;
    c[ImGuiCol_SliderGrabActive]     = kMint;
    c[ImGuiCol_Button]               = ImVec4(0.125f, 0.090f, 0.190f, 0.92f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.370f, 0.190f, 0.570f, 0.96f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.555f, 0.275f, 0.825f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.24f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.42f);
    c[ImGuiCol_HeaderActive]         = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.62f);
    c[ImGuiCol_Separator]            = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.52f);
    c[ImGuiCol_SeparatorActive]      = kAccent;
    c[ImGuiCol_ResizeGrip]           = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.20f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.65f);
    c[ImGuiCol_ResizeGripActive]     = kAccent;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0.08f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    c[ImGuiCol_ScrollbarGrabActive]  = kAccent;
    c[ImGuiCol_Tab]                  = ImVec4(kSurface.x, kSurface.y, kSurface.z, 0.75f);
    c[ImGuiCol_TabHovered]           = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f);
    c[ImGuiCol_TabSelected]          = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.60f);
    c[ImGuiCol_NavHighlight]         = kMint;
}

struct FrameInfo {
    ImVec2 win_min{};
    ImVec2 win_max{};
    ImVec2 content_min{};
    ImVec2 content_max{};
    ImVec2 image_min{};
    ImVec2 image_max{};
    bool show_image = false;
};

inline FrameInfo BeginChrome(const char* title, const char* version, bool* request_min,
                             bool* is_english, bool (*)(const char*)) {
    FrameInfo info{};
    ImGuiIO& io = ImGui::GetIO();
    if (window_size.x < S(8) || window_size.y < S(8))
        window_size = S2(960, 600);

    // Ukuran selalu dibatasi oleh surface aktif agar menu tidak berada di luar
    // layar setelah rotate, perubahan resolusi, atau perubahan UI scale.
    const ImVec2 maxWindow(ImMax(S(320), io.DisplaySize.x - S(16)),
                           ImMax(S(260), io.DisplaySize.y - S(16)));
    const ImVec2 minWindow(ImMin(S(620), maxWindow.x),
                           ImMin(S(400), maxWindow.y));
    window_size.x = ImClamp(window_size.x, minWindow.x, maxWindow.x);
    window_size.y = ImClamp(window_size.y, minWindow.y, maxWindow.y);
    ImGui::SetNextWindowSizeConstraints(minWindow, maxWindow);
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Once);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Once);
    ImGui::Begin("##newbie_premium_root", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoResize);

    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();
    ImVec2 clampedPos(ImClamp(p.x, 0.0f, ImMax(0.0f, io.DisplaySize.x - sz.x)),
                      ImClamp(p.y, 0.0f, ImMax(0.0f, io.DisplaySize.y - sz.y)));
    if (ImAbs(clampedPos.x - p.x) > 0.5f || ImAbs(clampedPos.y - p.y) > 0.5f) {
        ImGui::SetWindowPos(clampedPos);
        p = clampedPos;
    }
    window_pos = p;
    window_size = sz;
    info.win_min = p;
    info.win_max = p + sz;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = S(16.0f);

    // Background statis premium. NewbieUI tidak lagi memuat atau mendekode GIF.
    dl->AddRectFilled(p, p + sz, U32(kVoid), radius);
    dl->AddRectFilledMultiColor(p, p + sz,
        IM_COL32(24, 13, 52, 255), IM_COL32(10, 24, 45, 255),
        IM_COL32(7, 8, 22, 255), IM_COL32(17, 9, 38, 255));

    // Glass, vignette, and subtle color wash preserve contrast.
    dl->AddRectFilledMultiColor(p, p + sz,
        IM_COL32(10, 7, 25, 172), IM_COL32(12, 8, 30, 116),
        IM_COL32(5, 5, 17, 208), IM_COL32(8, 5, 22, 190));
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + S(84)), IM_COL32(13, 10, 31, 220),
                      radius, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilledMultiColor(p, ImVec2(p.x + sz.x, p.y + S(3)),
        U32(kAccent), U32(kMint), U32(kMint), U32(kAccent));
    dl->AddRect(p + S2(1, 1), p + sz - S2(1, 1), U32(kBorder, 0.90f), radius, 0, S(1.0f));
    dl->AddRect(p + S2(3, 3), p + sz - S2(3, 3), IM_COL32(255, 255, 255, 12), radius - S(2), 0, S(1.0f));

    // Brand mark and typography.
    const ImVec2 markMin = p + S2(22, 21);
    const ImVec2 markMax = markMin + S2(42, 42);
    dl->AddRectFilled(markMin, markMax, U32(kAccent, 0.92f), S(12));
    dl->AddRect(markMin, markMax, U32(kMint, 0.75f), S(12), 0, S(1.2f));
    const char* monogram = "N";
    ImVec2 monoSize = ImGui::CalcTextSize(monogram);
    dl->AddText(markMin + (markMax - markMin - monoSize) * 0.5f, U32(kText), monogram);
    dl->AddText(p + S2(76, 20), U32(kText), title ? title : "NEWBIE MOD");
    dl->AddText(p + S2(76, 44), U32(kTextSoft), version ? version : "");

    const char* en[] = { "HOME", "PRODUCT", "INFO", "SETTINGS" };
    const char* zh[] = { u8"\u4e3b\u9875", u8"\u529f\u80fd", u8"\u4fe1\u606f", u8"\u8bbe\u7f6e" };
    // 索引与 Main.cpp 的 ActiveUI::tab 分支一一对应：0 主页 / 1 功能 / 2 信息 / 3 设置。
    const int tabValues[] = { 0, 1, 2, 3 };
    static float navAnim[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float navW = S(100);
    const float navGap = S(7);
    const float navH = S(42);
    const float navStart = p.x + sz.x - (navW + navGap) * 4.0f - S(16);

    // Tombol minimize khusus Android. Parameter request_min diteruskan ke
    // DrawMenu sehingga menu penuh diganti dengan tombol restore yang kecil.
    const ImVec2 minButtonMin(navStart - S(53), p.y + S(22));
    const ImVec2 minButtonMax = minButtonMin + S2(41, 40);
    ImGui::SetCursorScreenPos(minButtonMin);
    if (ImGui::InvisibleButton("##newbie_minimize", minButtonMax - minButtonMin) && request_min)
        *request_min = true;
    const bool minHovered = ImGui::IsItemHovered();
    dl->AddRectFilled(minButtonMin, minButtonMax,
                      U32(minHovered ? kSurfaceHi : kSurface, minHovered ? 0.96f : 0.72f), S(10));
    dl->AddRect(minButtonMin, minButtonMax,
                U32(minHovered ? kMint : kBorder, minHovered ? 0.85f : 0.52f), S(10), 0, S(1));
    const float lineY = minButtonMin.y + S(25);
    dl->AddLine(ImVec2(minButtonMin.x + S(12), lineY),
                ImVec2(minButtonMax.x - S(12), lineY), U32(kText), S(2));

    for (int i = 0; i < 4; ++i) {
        ImVec2 nmin(navStart + i * (navW + navGap), p.y + S(21));
        ImVec2 nmax(nmin.x + navW, nmin.y + navH);
        ImGui::SetCursorScreenPos(nmin);
        char id[32]; std::snprintf(id, sizeof(id), "##newbie_nav_%d", i);
        if (ImGui::InvisibleButton(id, nmax - nmin)) tab_stored = tabValues[i];
        const bool active = tab_stored == tabValues[i];
        const bool hovered = ImGui::IsItemHovered();
        navAnim[i] = Smooth(navAnim[i], active ? 1.0f : (hovered ? 0.46f : 0.0f), 14.0f);

        const ImVec4 fill = Mix(ImVec4(kSurface.x, kSurface.y, kSurface.z, 0.30f),
                                ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.78f), navAnim[i]);
        dl->AddRectFilled(nmin, nmax, U32(fill), S(11));
        dl->AddRect(nmin, nmax, U32(Mix(kBorder, kMint, navAnim[i]), 0.50f + navAnim[i] * 0.35f),
                    S(11), 0, S(1.0f));
        if (navAnim[i] > 0.02f) {
            const float indicatorW = (navW - S(30)) * navAnim[i];
            const float center = (nmin.x + nmax.x) * 0.5f;
            dl->AddRectFilled(ImVec2(center - indicatorW * 0.5f, nmax.y - S(3)),
                              ImVec2(center + indicatorW * 0.5f, nmax.y), U32(kMint), S(2));
        }
        const char* label = (is_english && !*is_english) ? zh[i] : en[i];
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(nmin.x + (navW - textSize.x) * 0.5f,
                           nmin.y + (navH - textSize.y) * 0.5f),
                    U32(Mix(kTextSoft, kText, navAnim[i])), label);
    }
    tab = tab_stored;

    // Gesture dibuat eksklusif: satu sentuhan hanya menjadi drag header ATAU
    // resize dari kanan bawah. Ini penting pada input sentuh Android.
    static bool dragging = false;
    static bool resizeArmed = false;
    static bool resizing = false;
    static ImVec2 dragStartMouse{};
    static ImVec2 dragStartPos{};
    static ImVec2 resizeStartMouse{};
    static ImVec2 resizeStartSize{};
    static ImVec2 resizeStartPos{};

    const ImVec2 gripHitMin = info.win_max - S2(72, 72);
    const ImVec2 gripHitMax = info.win_max;
    const bool gripHover = ImGui::IsMouseHoveringRect(gripHitMin, gripHitMax, false);
    const bool overMinimize = ImGui::IsMouseHoveringRect(minButtonMin, minButtonMax, false);
    const bool titleHover = ImGui::IsMouseHoveringRect(
            p, ImVec2(minButtonMin.x - S(10), p.y + S(84)), false) &&
            !overMinimize && !gripHover;

    if (gripHover && io.MouseClicked[0]) {
        dragging = false;
        resizeArmed = true;
        resizing = false;
        resizeStartMouse = io.MousePos;
        resizeStartSize = sz;
        resizeStartPos = p;
    } else if (titleHover && io.MouseClicked[0] && !resizeArmed) {
        dragging = true;
        dragStartMouse = io.MousePos;
        dragStartPos = p;
    }

    if (!io.MouseDown[0]) {
        dragging = false;
        resizeArmed = false;
        resizing = false;
    }

    if (dragging && io.MouseDown[0]) {
        const ImVec2 delta = io.MousePos - dragStartMouse;
        ImVec2 next = dragStartPos + delta;
        next.x = ImClamp(next.x, 0.0f, ImMax(0.0f, io.DisplaySize.x - sz.x));
        next.y = ImClamp(next.y, 0.0f, ImMax(0.0f, io.DisplaySize.y - sz.y));
        ImGui::SetWindowPos(next);
        window_pos = next;
    }

    if (resizeArmed && io.MouseDown[0]) {
        const ImVec2 delta = io.MousePos - resizeStartMouse;
        if (!resizing && (ImAbs(delta.x) >= S(6) || ImAbs(delta.y) >= S(6)))
            resizing = true;
        if (resizing) {
            const float maxW = ImMax(minWindow.x, io.DisplaySize.x - resizeStartPos.x - S(8));
            const float maxH = ImMax(minWindow.y, io.DisplaySize.y - resizeStartPos.y - S(8));
            ImVec2 next(ImClamp(resizeStartSize.x + delta.x, minWindow.x, maxW),
                        ImClamp(resizeStartSize.y + delta.y, minWindow.y, maxH));
            ImGui::SetWindowPos(resizeStartPos);
            ImGui::SetWindowSize(next);
            window_pos = resizeStartPos;
            window_size = next;
        }
    }

    // Grip besar dan jelas di kanan bawah; hit area 72x72 untuk layar sentuh.
    const ImVec2 gripMin = info.win_max - S2(49, 49);
    const ImVec2 gripMax = info.win_max - S2(6, 6);
    dl->AddTriangleFilled(ImVec2(gripMin.x, gripMax.y), gripMax,
                          ImVec2(gripMax.x, gripMin.y),
                          U32(gripHover ? kMint : kAccent, gripHover ? 1.0f : 0.88f));
    dl->AddLine(ImVec2(gripMin.x + S(9), gripMax.y - S(5)),
                ImVec2(gripMax.x - S(5), gripMin.y + S(9)), U32(kText, 0.90f), S(2));

    info.content_min = p + S2(18, 102);
    info.content_max = p + sz - S2(18, 18);
    info.image_min = info.image_max = info.content_max;
    return info;
}

inline void DrawProductCategoryRail(int* selected, bool english, FrameInfo& info) {
    if (!selected || tab_stored != 1) return;

    const char* en[] = { "UI", "GAME", "PLAYER", "TELEPORT", "MISC", "MANUAL" };
    const char* zh[] = { u8"\u754c\u9762", u8"\u6e38\u620f", u8"\u73a9\u5bb6", u8"\u4f20\u9001", u8"\u6742\u9879", u8"\u624b\u52a8" };
    const char* badges[] = { "01", "02", "03", "04", "05", "06" };
    const int values[] = { 1, 2, 3, 4, 5, 6 };
    static float categoryAnim[6] = {};

    const float width = S(154);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 railMin = info.content_min;
    const ImVec2 railMax(railMin.x + width, info.content_max.y);

    dl->AddRectFilled(railMin, railMax, IM_COL32(15, 11, 34, 192), S(14));
    dl->AddRect(railMin, railMax, U32(kBorder, 0.62f), S(14), 0, S(1));
    dl->AddText(railMin + S2(14, 14), U32(kTextSoft), english ? "CATEGORIES" : u8"\u5206\u7c7b");
    dl->AddLine(railMin + S2(14, 39), ImVec2(railMax.x - S(14), railMin.y + S(39)), U32(kBorder, 0.45f));

    for (int i = 0; i < 6; ++i) {
        ImVec2 bmin(railMin.x + S(9), railMin.y + S(51) + i * S(57));
        ImVec2 bmax(railMax.x - S(9), bmin.y + S(47));
        ImGui::SetCursorScreenPos(bmin);
        char id[32]; std::snprintf(id, sizeof(id), "##newbie_category_%d", i);
        if (ImGui::InvisibleButton(id, bmax - bmin)) *selected = values[i];
        const bool active = *selected == values[i];
        const bool hovered = ImGui::IsItemHovered();
        categoryAnim[i] = Smooth(categoryAnim[i], active ? 1.0f : (hovered ? 0.38f : 0.0f));

        const ImVec4 fill = Mix(ImVec4(kSurface.x, kSurface.y, kSurface.z, 0.20f),
                                ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.66f), categoryAnim[i]);
        dl->AddRectFilled(bmin, bmax, U32(fill), S(10));
        if (active) dl->AddRectFilled(bmin, ImVec2(bmin.x + S(3), bmax.y), U32(kMint), S(2));

        const ImVec2 badgeMin = bmin + S2(10, 11);
        const ImVec2 badgeMax = badgeMin + S2(26, 25);
        dl->AddRectFilled(badgeMin, badgeMax, U32(active ? kMint : kSurfaceHi, active ? 0.92f : 0.75f), S(7));
        ImVec2 badgeText = ImGui::CalcTextSize(badges[i]);
        dl->AddText(badgeMin + (badgeMax - badgeMin - badgeText) * 0.5f,
                    U32(active ? kVoid : kTextSoft), badges[i]);

        const char* label = english ? en[i] : zh[i];
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(bmin.x + S(45), bmin.y + (S(47) - labelSize.y) * 0.5f),
                    U32(Mix(kTextSoft, kText, categoryAnim[i])), label);
    }
    info.content_min.x = railMax.x + S(14);
}

inline void BeginContent(const FrameInfo& info, bool allow_scroll = true) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 shadowOffset = S2(0, 4);
    dl->AddRectFilled(info.content_min + shadowOffset, info.content_max + shadowOffset,
                      IM_COL32(0, 0, 0, 72), S(14));
    dl->AddRectFilled(info.content_min, info.content_max, IM_COL32(13, 10, 31, 186), S(14));
    dl->AddRect(info.content_min, info.content_max, U32(kBorder, 0.58f), S(14), 0, S(1));
    dl->AddLine(info.content_min + S2(16, 1), ImVec2(info.content_max.x - S(16), info.content_min.y + S(1)),
                U32(kAccent, 0.52f), S(1));

    ImGui::SetCursorScreenPos(info.content_min);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, S2(18, 17));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(14));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground;
    if (!allow_scroll)
        flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##newbie_premium_content", info.content_max - info.content_min,
                      ImGuiChildFlags_AlwaysUseWindowPadding, flags);
}

inline void DrawCompactSubTabs(int* selected, const char* const* labels,
                               const char* const* secondaryLabels, int count, int columns = 5) {
    if (!selected || !labels || count <= 0) return;
    if (columns < 1) columns = 1;

    const float gap = S(8);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float width = (avail - gap * (columns - 1)) / columns;
    const float height = S(44);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const int rows = (count + columns - 1) / columns;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int i = 0; i < count; ++i) {
        const int row = i / columns;
        const int col = i % columns;
        const ImVec2 pos = start + ImVec2((width + gap) * col, (height + gap) * row);
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(i);
        const char* label = labels[i];
        if (secondaryLabels && secondaryLabels[i] && secondaryLabels[i][0] != '\0') label = labels[i];
        if (ImGui::InvisibleButton("##premium_subtab", ImVec2(width, height))) *selected = i;
        const bool active = *selected == i;
        const bool hovered = ImGui::IsItemHovered();
        const ImVec4 fill = active ? ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.72f)
                                  : (hovered ? ImVec4(kSurfaceHi.x, kSurfaceHi.y, kSurfaceHi.z, 0.90f)
                                             : ImVec4(kSurface.x, kSurface.y, kSurface.z, 0.72f));
        const ImVec2 rmin = ImGui::GetItemRectMin();
        const ImVec2 rmax = ImGui::GetItemRectMax();
        dl->AddRectFilled(rmin, rmax, U32(fill), S(10));
        dl->AddRect(rmin, rmax, U32(active ? kMint : kBorder, active ? 0.75f : 0.42f), S(10), 0, S(1));
        if (active) dl->AddCircleFilled(ImVec2(rmin.x + S(13), (rmin.y + rmax.y) * 0.5f), S(3), U32(kMint));
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(rmin.x + (width - textSize.x) * 0.5f + (active ? S(4) : 0),
                           rmin.y + (height - textSize.y) * 0.5f), U32(active ? kText : kTextSoft), label);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(avail, rows * height + (rows - 1) * gap + S(10)));
}

inline void EndContent(bool allow_scroll = true) {
    if (allow_scroll && ImGui::IsWindowHovered() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

inline const ImVec4 kTextInactive = kTextSoft;

inline void Section(const char* icon, const char* text) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + S2(30, 30), U32(kAccent, 0.72f), S(8));
    const char* mark = (icon && *icon) ? icon : "N";
    ImVec2 ms = ImGui::CalcTextSize(mark);
    dl->AddText(p + (S2(30, 30) - ms) * 0.5f, U32(kText), mark);
    dl->AddText(p + S2(40, 6), U32(kAccentHot), text ? text : "");
    ImGui::Dummy(S2(0, 38));
}

inline void DrawAnimatedTitle(const char* line1, const char* line2, ImVec2 origin) {
    const float pulse = 0.72f + 0.28f * std::sin((float)ImGui::GetTime() * 2.2f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(origin, U32(kAccentHot, pulse), line1 ? line1 : "");
    dl->AddText(origin + S2(0, 42), U32(kText), line2 ? line2 : "");
}

inline bool KeyField(const char* hint, char* buf, int bufSize, ImVec2 min, ImVec2 size) {
    ImGui::SetCursorScreenPos(min);
    ImGui::PushItemWidth(size.x);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, S2(12, 10));
    bool changed = ImGui::InputTextWithHint("##newbie_key", hint, buf, bufSize);
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();
    return changed;
}

inline bool SignInButton(const char* label, bool loading, ImVec2 min, ImVec2 size) {
    ImGui::SetCursorScreenPos(min);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.82f));
    bool pressed = ImGui::Button(loading ? u8"处理中..." : label, size);
    ImGui::PopStyleColor();
    return pressed && !loading;
}

inline bool GhostButton(const char* id, const char*, const char* label, ImVec2 min, ImVec2 size) {
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(id);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopID();
    return pressed;
}

inline bool Pill(const char* id, const char* label, bool active, ImVec2 min, ImVec2 size) {
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(id);
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.76f));
    bool pressed = ImGui::Button(label, size);
    if (active) ImGui::PopStyleColor();
    ImGui::PopID();
    return pressed;
}

inline void DrawGameSubGrid(int* selected, bool english) {
    if (!selected) return;
    const char* labels_en[] = { "Life", "Hunt", "Farm", "Move", "More", "Weather", "Action", "Camera", "Chat", "Build" };
    const char* labels_zh[] = { u8"生活", u8"狩猎", u8"种植", u8"移动", u8"其他", u8"天气", u8"动作", u8"视角", u8"聊天", u8"建筑" };
    constexpr int count = 10;

    const float gap = S(7.f);
    float minWidth = 0.0f;
    for (int i = 0; i < count; ++i) {
        const char* text = english ? labels_en[i] : labels_zh[i];
        minWidth = ImMax(minWidth, ImGui::CalcTextSize(text).x + S(28.f));
    }
    const float height = S(42.f);
    const float available = ImGui::GetContentRegionAvail().x;
    int columns = (int)((available + gap) / (minWidth + gap));
    columns = ImClamp(columns, 1, count);
    const float width = (available - gap * (float)(columns - 1)) / (float)columns;

    for (int i = 0; i < count; ++i) {
        ImGui::PushID(0x47414D45 + i);
        const bool active = *selected == i;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.82f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_Text, kText);
        }
        const char* label = english ? labels_en[i] : labels_zh[i];
        if (ImGui::Button(label, ImVec2(width, height)))
            *selected = i;
        if (active) ImGui::PopStyleColor(3);
        ImGui::PopID();
        if ((i + 1) % columns != 0 && i + 1 < count)
            ImGui::SameLine(0.f, gap);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

inline void DrawRightRail(int* selected, float height = 0.0f) {
    if (!selected) return;
    const char* labels[] = { u8"生活", u8"狩猎", u8"种植", u8"移动", u8"其他", u8"天气", u8"动作", u8"视角", u8"聊天", u8"建筑" };
    const char* badges[] = { "01", "02", "03", "04", "05", "06", "07", "08", "09", "10" };
    const float railHeight = height > 1.0f ? height : ImGui::GetContentRegionAvail().y;

    // Dimensi sama dengan SawitUI: rail 72 dan tombol 56x52. Main.cpp memang
    // menyediakan ruang 76 + gap 8, sehingga rail tidak lagi terpotong.
    ImGui::BeginChild("##newbie_right_rail", S2(72, railHeight),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();
    dl->AddRectFilled(wp, wp + ws, IM_COL32(15, 11, 34, 205), S(12));
    dl->AddRect(wp, wp + ws, U32(kBorder, 0.62f), S(12), 0, S(1));

    const ImVec2 origin = wp + S2(8, 10);
    for (int i = 0; i < 10; ++i) {
        const ImVec2 bmin = origin + ImVec2(0, S(58) * (float)i);
        const ImVec2 bmax = bmin + S2(56, 52);
        ImGui::SetCursorScreenPos(bmin);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##newbie_game_sub", bmax - bmin))
            *selected = i;
        const bool active = *selected == i;
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(bmin, bmax,
                          U32(active ? kAccent : (hovered ? kSurfaceHi : kSurface),
                              active ? 0.82f : 0.76f), S(10));
        dl->AddRect(bmin, bmax, U32(active ? kMint : kBorder, active ? 0.85f : 0.38f),
                    S(10), 0, S(1));
        const ImVec2 badgeSize = ImGui::CalcTextSize(badges[i]);
        dl->AddText(ImVec2(bmin.x + (S(56) - badgeSize.x) * 0.5f, bmin.y + S(8)),
                    U32(active ? kMint : kTextSoft), badges[i]);
        const ImVec2 textSize = ImGui::CalcTextSize(labels[i]);
        dl->AddText(ImVec2(bmin.x + (S(56) - textSize.x) * 0.5f, bmin.y + S(29)),
                    U32(active ? kText : kTextSoft), labels[i]);
        ImGui::PopID();
    }
    ImGui::EndChild();
}

inline bool DrawMinimized(bool* restore, ImVec2* outMin = nullptr, ImVec2* outMax = nullptr) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 size = S2(176, 52);
    ImGui::SetNextWindowPos(ImVec2(S(10), io.DisplaySize.y * 0.5f - S(28)),
                            ImGuiCond_FirstUseEver);
    ImGui::Begin("##newbie_min", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoResize);

    // Clamp dilakukan setiap frame agar tombol tetap terlihat setelah rotasi,
    // perubahan resolusi, atau drag hingga tepi layar.
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 clamped(ImClamp(windowPos.x, 0.0f, ImMax(0.0f, io.DisplaySize.x - size.x)),
                   ImClamp(windowPos.y, 0.0f, ImMax(0.0f, io.DisplaySize.y - size.y)));
    if (ImAbs(clamped.x - windowPos.x) > 0.5f || ImAbs(clamped.y - windowPos.y) > 0.5f) {
        ImGui::SetWindowPos(clamped);
        windowPos = clamped;
    }

    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min, min + size, U32(kAccent, 0.90f), S(12));
    dl->AddRect(min, min + size, U32(kMint, 0.80f), S(12), 0, S(1));
    dl->AddText(min + S2(16, 17), U32(kText), u8"婉婉魔法");
    const ImVec2 boxMin(min.x + size.x - S(27), min.y + S(18));
    const ImVec2 boxMax(min.x + size.x - S(12), min.y + S(33));
    dl->AddRect(boxMin, boxMax, U32(kText), S(2), 0, S(2));

    ImGui::SetCursorScreenPos(min);
    const bool pressed = ImGui::InvisibleButton("##newbie_restore", size);
    static bool miniDragging = false;
    static ImVec2 startMouse{};
    static ImVec2 startPos{};
    if (ImGui::IsItemActivated()) {
        miniDragging = false;
        startMouse = io.MousePos;
        startPos = ImGui::GetWindowPos();
    }
    if (ImGui::IsItemActive() && io.MouseDown[0]) {
        const ImVec2 delta = io.MousePos - startMouse;
        if (!miniDragging && (ImAbs(delta.x) > S(7) || ImAbs(delta.y) > S(7)))
            miniDragging = true;
        if (miniDragging) {
            ImVec2 next = startPos + delta;
            next.x = ImClamp(next.x, 0.0f, ImMax(0.0f, io.DisplaySize.x - size.x));
            next.y = ImClamp(next.y, 0.0f, ImMax(0.0f, io.DisplaySize.y - size.y));
            ImGui::SetWindowPos(next);
        }
    }
    if (pressed && !miniDragging && restore)
        *restore = true;
    if (!io.MouseDown[0])
        miniDragging = false;

    if (outMin) *outMin = min;
    if (outMax) *outMax = min + size;
    ImGui::Dummy(size);
    ImGui::End();
    return true;
}

inline void EndChrome(const FrameInfo&, bool* = nullptr) {
    // Newbie is intentionally Chinese-only; no runtime language control.
    ImGui::End();
}

} // namespace NewbieUI
