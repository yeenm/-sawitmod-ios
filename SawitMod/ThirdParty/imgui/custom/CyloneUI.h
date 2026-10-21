#pragma once

// stb_image 的实现放在 ThirdParty/stb_image_impl.cpp 里：
// 这个头会被多个 .cpp 包含（Menu.cpp / MenuGlue.cpp），
// 把 STB_IMAGE_IMPLEMENTATION 放在头里会产生重复符号。
#include "../stb_image.h"
#include "../imgui_internal.h"
#include "Version.h"   // SAWIT_UI_BOLD_FONT / SAWIT_UI_FONT_SIZE_PX
#include "BoldFont.h"
#include "data/fonts.h"
#include "data/images.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Cylone {

inline ImVec4 Col(int r, int g, int b, int a = 255) {
    return ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
}

// =====================================================================
//  米色（奶油）配色
// =====================================================================
// 暖米底 + 茶棕点缀：窗口外壳奶白、面板亚麻、控件米黄、高亮焦糖，
// 文字用深棕（浅色主题不能再用白字）。整套界面只有这一支焦糖色做强调。
inline const ImVec4 kLayout = Col(247, 242, 231);     // 窗口外壳底（奶白 #F7F2E7）
inline const ImVec4 kAccent = Col(158, 104, 48);      // 焦糖 #9E6830
inline const ImVec4 kAccentHover = Col(180, 122, 60); // 焦糖 hover
inline const ImVec4 kOnAccent = Col(255, 252, 246);   // 焦糖/危险色块上的文字（暖白）
inline const ImVec4 kWhite = Col(255, 253, 248);      // 仅用于深色块上的文字
inline const ImVec4 kBlack = Col(58, 58, 56);         // 正文深灰 #3B342A
inline const ImVec4 kTextInactive = Col(110, 108, 104);// 次要文字（中灰 #6E6C68）
inline const ImVec4 kChild = Col(238, 231, 216);      // 面板 / 子窗口底（亚麻 #EEE7D8）
inline const ImVec4 kButton = Col(226, 214, 192);     // 控件底（米黄 #E2D6C0）
inline const ImVec4 kButtonHover = Col(210, 196, 170);// 控件 hover（深一档米黄）
inline const ImVec4 kLang = Col(243, 237, 226);
inline const ImVec4 kBorder = Col(203, 188, 162, 200);// 边框（浅茶 #CBBCA2）
inline const ImVec4 kText = Col(58, 58, 56);          // 正文（深灰 #3A3A38）

// 语义色：浅米底上要用深一点的一档，否则黄/青/浅灰这类在米色上看不清。
inline const ImVec4 kOk = Col(30, 118, 60);           // 成功 / 已启用
inline const ImVec4 kWarn = Col(168, 112, 12);        // 注意 / 待处理 / 次数
inline const ImVec4 kErr = Col(172, 46, 38);          // 错误 / 失败
inline const ImVec4 kInfo = Col(32, 96, 132);         // 信息 / 状态标题
inline const ImVec4 kMuted = Col(116, 114, 110);      // 弱化说明文字（中灰）
inline const ImVec4 kDanger = Col(158, 58, 52);       // 危险按钮底（配暖白字）

inline float dpi = 1.f;
inline float device_dpi = 1.f;
inline float user_scale = 1.f;
inline int tab = 0;
inline int tab_stored = 0;
inline float tab_alpha = 1.f;
// 卡密验证通过后才会出现「功能」「公告」两页；由 Main.cpp 每帧在 BeginChrome 之前写入。
inline bool features_unlocked = false;
inline bool notice_ready = false;   // 公告页在拉取中时导航上给个提示点

// ---- 轻提示（例如「云下发接收成功」）：画在窗口顶部居中，几秒后自动淡出 ----
inline std::string toast_text;
inline float toast_timer = 0.0f;
inline float toast_total = 0.0f;
inline void ShowToast(const std::string& text, float seconds = 3.0f) {
    toast_text = text;
    toast_timer = seconds;
    toast_total = seconds;
}
inline ImVec2 window_pos = ImVec2(40.f, 40.f);
inline ImVec2 window_size = ImVec2(0.f, 0.f);
inline bool assets_ready = false;
inline bool initialized = false;

inline ImTextureID img_logo = 0;
inline ImTextureID img_owl = 0;
inline ImTextureID img_us = 0;
inline ImTextureID img_ru = 0;

inline ImFont* font_inter = nullptr;
inline ImFont* font_icon = nullptr;
inline ImFont* font_display = nullptr;
inline ImFont* font_display_sm = nullptr;

inline float S(float v) { return roundf(v * dpi); }
inline ImVec2 S2(float x, float y) { return ImVec2(S(x), S(y)); }

inline ImU32 U32(const ImVec4& c, float a = 1.f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * a));
}

inline ImTextureID LoadTexture(const unsigned char* data, size_t size) {
    if (!data || size == 0) return 0;
    int w = 0, h = 0, comp = 0;
    unsigned char* image = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
    if (!image) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(image);
    return (ImTextureID)(intptr_t)tex;
}

inline ImFont* AddFontMem(std::vector<unsigned char>& bytes, float size, const ImWchar* ranges) {
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(bytes.data(), (int)bytes.size(), size, &cfg, ranges);
}

inline void SetupFonts(void* notoData, int notoSize) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    // 加粗：包一层 stb_truetype 的 loader（字形 alpha 膨胀，见 custom/BoldFont.h）。
    // 必须在 Clear() 之后、AddFont* 之前设置。开关：Version.h 的 SAWIT_UI_BOLD_FONT。
#if SAWIT_UI_BOLD_FONT
    if (const ImFontLoader* boldLoader = BoldFont::Loader())
        io.Fonts->SetFontLoader(boldLoader);
#endif
    const ImWchar* latin = io.Fonts->GetGlyphRangesCyrillic();
    // 字号：Version.h 的 SAWIT_UI_FONT_SIZE_PX（原版 19，现在 23）。
    float body = S(SAWIT_UI_FONT_SIZE_PX);
    if (body < SAWIT_UI_FONT_SIZE_PX) body = SAWIT_UI_FONT_SIZE_PX;
    font_inter = AddFontMem(inter, body, latin);
    if (notoData && notoSize > 0 && font_inter) {
        ImFontConfig merge;
        merge.MergeMode = true;
        merge.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(notoData, notoSize, body, &merge, io.Fonts->GetGlyphRangesChineseFull());
    }
    // 图标字体已从界面移除：不再往图集里塞 icon font，界面只剩文字与原生控件。
    font_icon = nullptr;
    // 展示字体也只在旧版首页动画标题里用过，一并停掉。
    font_display = nullptr;
    font_display_sm = nullptr;
    io.Fonts->Build();
}

inline void LoadImages() {
    // 新版界面不再使用任何图片/图标资源（标题栏、导航、状态栏全部是文字）。
    // 函数保留是为了 Main.cpp 的调用点不用改；不创建 GL 纹理，省显存。
    img_logo = 0;
    img_owl = 0;
    img_us = 0;
    img_ru = 0;
    assets_ready = true;
}

inline void ApplyStyle() {
    ImGuiStyle& st = ImGui::GetStyle();

    // 先套官方 light 主题（浅底深字），再把整块换成米色系。
    // 几何一律保持官方那种直角 / 细边 / 紧凑间距，观感就是原版 imgui。
    ImGui::StyleColorsLight();

    st.WindowRounding = S(0.f);
    st.ChildRounding = S(0.f);
    st.PopupRounding = S(0.f);
    st.FrameRounding = S(2.f);
    st.GrabRounding = S(2.f);
    st.ScrollbarRounding = S(2.f);
    st.TabRounding = S(2.f);
    st.WindowBorderSize = S(1.f);
    st.ChildBorderSize = S(1.f);
    st.PopupBorderSize = S(1.f);
    st.FrameBorderSize = S(0.f);
    st.WindowPadding = S2(8, 8);
    st.FramePadding = S2(8, 6);
    st.ItemSpacing = S2(8, 6);
    st.ItemInnerSpacing = S2(6, 4);
    // 触摸用的滚动条：加粗 + 加大最小抓手（原来 18/16 太难戳中）
    st.ScrollbarSize = S(34.f);
    st.ScrollbarPadding = S(2.f);
    st.GrabMinSize = S(64.f);
    st.WindowMinSize = S2(420, 280);
    st.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    st.SeparatorSize = S(1.f);
    st.TabBarBorderSize = S(1.f);

    ImVec4* c = st.Colors;
    // 整块换成米色系：底、面板、边框、文字、控件、滑块、滚动条、选中态
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextInactive;
    c[ImGuiCol_WindowBg] = ImVec4(kLayout.x, kLayout.y, kLayout.z, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(kChild.x, kChild.y, kChild.z, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(kChild.x, kChild.y, kChild.z, 1.00f);
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(kLang.x, kLang.y, kLang.z, 1.00f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(kButton.x, kButton.y, kButton.z, 1.00f);
    c[ImGuiCol_TableBorderStrong] = kBorder;
    c[ImGuiCol_TableBorderLight] = ImVec4(kBorder.x, kBorder.y, kBorder.z, 0.45f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);
    // 控件：米黄底 + 焦糖高亮，和自绘按钮（kButton / kAccent）保持同一套色
    c[ImGuiCol_FrameBg] = ImVec4(kButton.x, kButton.y, kButton.z, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.28f);
    c[ImGuiCol_FrameBgActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f);
    c[ImGuiCol_Button] = ImVec4(kButton.x, kButton.y, kButton.z, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.72f);
    c[ImGuiCol_ButtonActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 1.00f);
    c[ImGuiCol_Header] = ImVec4(kButton.x, kButton.y, kButton.z, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    c[ImGuiCol_HeaderActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.85f);
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.95f);
    c[ImGuiCol_SliderGrabActive] = kAccent;
    c[ImGuiCol_Separator] = ImVec4(kBorder.x, kBorder.y, kBorder.z, 0.90f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.70f);
    c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = ImVec4(kBorder.x, kBorder.y, kBorder.z, 0.70f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.65f);
    c[ImGuiCol_ResizeGripActive] = kAccent;
    c[ImGuiCol_ScrollbarBg] = ImVec4(kChild.x, kChild.y, kChild.z, 1.00f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(kBorder.x, kBorder.y, kBorder.z, 0.85f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.60f);
    c[ImGuiCol_ScrollbarGrabActive] = kAccent;
    c[ImGuiCol_Tab] = ImVec4(kButton.x, kButton.y, kButton.z, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.60f);
    c[ImGuiCol_TabSelected] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.90f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    c[ImGuiCol_NavCursor] = kAccent;
}

inline void Easing(float& value, float target, float speed) {
    value = value + (target - value) * ImMin(1.f, ImGui::GetIO().DeltaTime * speed);
}

inline bool InvisibleHit(const char* id, const ImVec2& min, const ImVec2& max) {
    ImGui::SetCursorScreenPos(min);
    return ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
}

inline void DrawImage(ImDrawList* dl, ImTextureID tex, ImVec2 a, ImVec2 b, ImU32 col) {
    if (!tex) return;
    dl->AddImage(tex, a, b, ImVec2(0, 0), ImVec2(1, 1), col);
}

inline void TextClip(ImDrawList* dl, ImFont* font, float size, ImVec2 a, ImVec2 b,
                     ImU32 col, const char* text, ImVec2 align) {
    if (!text) return;
    ImFont* use = font ? font : ImGui::GetFont();
    float fs = size > 0.f ? size : use->LegacySize;
    ImVec2 ts = use->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
    ImVec2 p(a.x + (b.x - a.x - ts.x) * align.x, a.y + (b.y - a.y - ts.y) * align.y);
    dl->AddText(use, fs, p, col, text);
}

// =====================================================================
//  顶部导航（纯文字，无图标字体）
// =====================================================================
// 旧版是左侧竖排图标 rail，现在是窗口顶部一排文字选项卡：用原生 Button，
// 选中项用官方蓝实心块。所有图标字形（icon font）已从界面移除。
inline bool NavTab(const char* label, int index, float x, float y, float w) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    char id[64];
    snprintf(id, sizeof(id), "%s##mod_nav_%d", label, index);
    const bool active = (tab_stored == index);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
    }
    const bool pressed = ImGui::Button(id, ImVec2(w, S(26.f)));
    if (active) ImGui::PopStyleColor(2);
    if (pressed) tab_stored = index;
    return active;
}

// 状态栏里的文字链接（SmallButton 就是经典 imgui 的那种小按钮）。
inline bool TextLink(const char* label) {
    return ImGui::SmallButton(label);
}

inline void DrawAnimatedTitle(const char* line1, const char* line2, ImVec2 origin) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* f1 = font_display_sm ? font_display_sm : ImGui::GetFont();
    ImFont* f2 = font_display ? font_display : ImGui::GetFont();
    float t = (float)ImGui::GetTime();
    ImVec2 p = origin;
    for (const char* s = line1; s && *s;) {
        char buf[8] = {};
        unsigned char c = (unsigned char)*s;
        int n = 1;
        if ((c & 0x80) == 0) n = 1;
        else if ((c & 0xE0) == 0xC0) n = 2;
        else if ((c & 0xF0) == 0xE0) n = 3;
        else n = 1;
        memcpy(buf, s, (size_t)n);
        s += n;
        float phase = (float)(s - line1) * 0.18f;
        float a = powf((sinf(t * 3.f + phase) + 1.f) * 0.5f, 2.f);
        float fs = f1->LegacySize;
        ImVec2 ts = f1->CalcTextSizeA(fs, FLT_MAX, 0.f, buf);
        dl->AddText(f1, fs, p, U32(kAccent, ImClamp(a, 0.25f, 1.f)), buf);
        p.x += ts.x;
    }
    p = ImVec2(origin.x, origin.y + S(42.f));
    for (const char* s = line2; s && *s;) {
        char buf[8] = {};
        unsigned char c = (unsigned char)*s;
        int n = 1;
        if ((c & 0x80) == 0) n = 1;
        else if ((c & 0xE0) == 0xC0) n = 2;
        else if ((c & 0xF0) == 0xE0) n = 3;
        else n = 1;
        memcpy(buf, s, (size_t)n);
        s += n;
        float phase = (float)(s - line2) * 0.18f;
        float a = powf((sinf(t * 3.f + phase) + 1.f) * 0.5f, 2.f);
        float fs = f2->LegacySize;
        ImVec2 ts = f2->CalcTextSizeA(fs, FLT_MAX, 0.f, buf);
        dl->AddText(f2, fs, p, U32(kText, ImClamp(a, 0.25f, 1.f)), buf);
        p.x += ts.x;
    }
}

inline bool KeyField(const char* hint, char* buf, int buf_size, ImVec2 min, ImVec2 size) {
    // 直接用 ImGui 原生输入框：蓝灰 FrameBg + 方角，就是经典 imgui 的样子。
    ImGui::SetCursorScreenPos(min);
    ImGui::PushItemWidth(size.x);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, S2(8, 10));
    bool changed = ImGui::InputTextWithHint("##wwd_key", hint, buf, buf_size);
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();
    return changed;
}

inline bool SignInButton(const char* label, bool loading, ImVec2 min, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max = min + size;
    bool pressed = InvisibleHit("##wwd_signin", min, max);
    bool hover = ImGui::IsItemHovered();
    // 焦糖实心块：hover 提亮，按下变深，没有渐变。
    const ImU32 fill = U32(hover ? kAccentHover : kAccent);
    dl->AddRectFilled(min, max, fill, 0.f);
    dl->AddRect(min, max, U32(kAccent), 0.f, 0, S(1.f));
    ImVec2 clip_min = min, clip_max = max;
    dl->PushClipRect(clip_min, clip_max, true);
    if (loading) {
        static float progress = 0.f;
        progress = ImMin(1.f, progress + ImGui::GetIO().DeltaTime * 0.55f);
        ImVec2 line = S2(133, 8);
        ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        ImVec2 a(c.x - line.x * 0.5f, c.y - line.y * 0.5f);
        ImVec2 b(c.x + line.x * 0.5f, c.y + line.y * 0.5f);
        dl->AddRectFilled(a, b, U32(kLayout), 0.f);
        dl->AddRectFilled(ImVec2(a.x + S(1), a.y + S(1)),
                          ImVec2(a.x + S(1) + (line.x - S(2)) * progress, b.y - S(1)),
                          U32(kAccent), 0.f);
    } else {
        TextClip(dl, font_inter, S(16.f), min, max, U32(kOnAccent), label, ImVec2(0.5f, 0.5f));
    }
    dl->PopClipRect();
    return pressed && !loading;
}

inline bool GhostButton(const char* id, const char* icon, const char* label, ImVec2 min, ImVec2 size) {
    (void)icon;   // 图标字形已移除，只保留文字标签
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max = min + size;
    bool pressed = InvisibleHit(id, min, max);
    bool hover = ImGui::IsItemHovered();
    dl->AddRectFilled(min, max, U32(hover ? kButtonHover : kChild), 0.f);
    dl->AddRect(min, max, U32(kBorder), 0.f, 0, S(1.f));
    TextClip(dl, font_inter, S(16.f), min, max, U32(kText), label, ImVec2(0.5f, 0.5f));
    return pressed;
}

inline bool Pill(const char* id, const char* label, bool active, ImVec2 min, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max = min + size;
    bool pressed = InvisibleHit(id, min, max);
    dl->AddRectFilled(min, max, U32(active ? kButton : kChild), 0.f);
    dl->AddRect(min, max, U32(active ? kAccent : kBorder), 0.f, 0, S(1.f));
    TextClip(dl, font_inter, S(16.f), min, max, U32(active ? kText : kTextInactive), label, ImVec2(0.5f, 0.5f));
    return pressed;
}

struct FrameInfo {
    ImVec2 win_min;
    ImVec2 win_max;
    ImVec2 content_min;
    ImVec2 content_max;
    ImVec2 image_min;
    ImVec2 image_max;
    bool show_image;
};

// =====================================================================
//  新版布局：标题栏 → 顶部选项卡 → 页头 → 内容 → 底部状态栏
// =====================================================================
// 和旧版（左侧图标 rail + 居中页头 + 右下角语言方块）完全不同：
//   * 导航搬到窗口顶部的文字选项卡；
//   * 页头改成「左标题 + 右副标题 + 一条分隔线」；
//   * 语言切换与外部链接搬到窗口底部状态栏；
//   * 内容区占满整宽，不再给 rail 留 78px。
// 保留的只有窗口拖动 / 缩放命中逻辑（用户看不见，改了反而容易出问题）。
namespace detail {
inline bool (*g_open_url)(const char*) = nullptr;
inline bool g_open_url_set = false;
}

inline FrameInfo BeginChrome(const char* title, const char* brand, bool* minimize,
                             bool* is_english, bool (*open_url)(const char*)) {
    FrameInfo info{};
    ImGuiIO& io = ImGui::GetIO();
    detail::g_open_url = open_url;
    detail::g_open_url_set = true;
    Easing(tab_alpha, (tab != tab_stored) ? 0.f : 1.f, 10.f);
    if (tab_alpha < 0.02f) tab = tab_stored;

    ImVec2 def_size = S2(820, 520);
    if (window_size.x < 8.f || window_size.y < 8.f)
        window_size = def_size;

    // Pulihkan ukuran yang telanjur terlalu besar dari build sebelumnya.
    const ImVec2 min_window = S2(420, 280);
    const ImVec2 max_window(
            ImMax(min_window.x, io.DisplaySize.x - S(16.f)),
            ImMax(min_window.y, io.DisplaySize.y - S(16.f)));
    window_size.x = ImClamp(window_size.x, min_window.x, max_window.x);
    window_size.y = ImClamp(window_size.y, min_window.y, max_window.y);
    ImGui::SetNextWindowSizeConstraints(min_window, max_window);
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Once);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Once);
    ImGui::Begin("##mod_root", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoResize);

    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();

    // Jaga window tetap berada di area layar pada setiap frame, bukan hanya saat
    // sedang di-drag. Ini juga memulihkan posisi setelah rotasi, perubahan scale,
    // restore dari minimize, atau resolusi surface berubah.
    // Ukuran tidak pernah diubah oleh koreksi ini.
    const float max_pos_x = ImMax(0.0f, io.DisplaySize.x - sz.x);
    const float max_pos_y = ImMax(0.0f, io.DisplaySize.y - sz.y);
    ImVec2 clamped_pos(ImClamp(p.x, 0.0f, max_pos_x),
                       ImClamp(p.y, 0.0f, max_pos_y));
    if (ImAbs(clamped_pos.x - p.x) > 0.5f ||
        ImAbs(clamped_pos.y - p.y) > 0.5f) {
        ImGui::SetWindowPos(clamped_pos);
        p = clamped_pos;
    }

    window_pos = p;
    window_size = sz;
    info.win_min = p;
    info.win_max = p + sz;

    const float title_h = S(46.f);
    const float nav_h = S(48.f);
    const float head_h = S(40.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + sz, U32(kLayout), 0.f);
    dl->AddRect(p + S2(0.5f, 0.5f), p + sz - S2(0.5f, 0.5f), U32(kBorder), 0.f, 0, S(1.f));

    // ---------------- 1) 标题栏：品牌 + 版本 + 最小化 ----------------
    ImVec2 title_min = p;
    ImVec2 title_max(p.x + sz.x, p.y + title_h);
    dl->AddRectFilled(title_min, title_max, U32(kChild), 0.f);
    dl->AddLine(ImVec2(title_min.x, title_max.y), ImVec2(title_max.x, title_max.y),
                U32(kBorder), S(1.f));
    TextClip(dl, font_inter, S(18.f), title_min + S2(12, 0), title_max,
             U32(kText), brand, ImVec2(0.f, 0.5f));

    ImVec2 owl_min(title_max.x - S(40), title_min.y + S(6));
    ImVec2 owl_max(title_max.x - S(10), title_max.y - S(6));
    const bool owl_hover = ImGui::IsMouseHoveringRect(owl_min, owl_max, false);
    dl->AddRectFilled(owl_min, owl_max, U32(owl_hover ? kButton : kLayout), 0.f);
    dl->AddRect(owl_min, owl_max, U32(kBorder), 0.f, 0, S(1.f));
    dl->AddLine(ImVec2(owl_min.x + S(8), (owl_min.y + owl_max.y) * 0.5f),
                ImVec2(owl_max.x - S(8), (owl_min.y + owl_max.y) * 0.5f), U32(kText), S(2.f));
    TextClip(dl, font_inter, S(16.f), ImVec2(title_min.x + S(12), title_min.y),
             ImVec2(owl_min.x - S(8), title_max.y), U32(kTextInactive), title, ImVec2(1.f, 0.5f));
    if (InvisibleHit("##mod_minimize", owl_min, owl_max) && minimize)
        *minimize = true;

    // ---------------- 2) 顶部文字选项卡 ----------------
    ImVec2 nav_min(p.x, title_max.y);
    ImVec2 nav_max(p.x + sz.x, nav_min.y + nav_h);
    dl->AddRectFilled(nav_min, nav_max, U32(kLayout), 0.f);
    dl->AddLine(ImVec2(nav_min.x, nav_max.y), ImVec2(nav_max.x, nav_max.y),
                U32(kBorder), S(1.f));

    // 页面：0 主页 / 1 功能（免费功能 + 卡密解锁的完整列表）/ 2 公告 / 3 设置
    // 四页都常显：功能页里免费部分不需要卡密，公告页也不需要。
    const char* nav_labels[4] = {
            *is_english ? "Home" : u8"主页",
            *is_english ? "Features" : u8"功能",
            *is_english ? "Notice" : u8"公告",
            *is_english ? "Settings" : u8"设置"};
    const bool nav_locked[4] = {false, false, false, false};
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, S2(12, 4));
        float nav_x = nav_min.x + S(8);
        for (int i = 0; i < 4; ++i) {
            if (nav_locked[i] && !features_unlocked) continue;
            const float w = ImGui::CalcTextSize(nav_labels[i]).x + S(28.f);
            NavTab(nav_labels[i], i, nav_x, nav_min.y + S(4), w);
            nav_x += w + S(4);
        }
        // 未解锁时在导航右侧说明会员功能的解锁条件
        if (!features_unlocked) {
            TextClip(dl, font_inter, S(12.f),
                     ImVec2(nav_min.x, nav_min.y), ImVec2(nav_max.x - S(10.f), nav_max.y),
                     U32(kTextInactive), u8"会员功能需卡密验证", ImVec2(1.f, 0.5f));
        }
        ImGui::PopStyleVar();
    }

    // ---------------- 3) 页头：左标题 + 右副标题 + 分隔线 ----------------
    ImVec2 head_min(p.x + S(12), nav_max.y);
    ImVec2 head_max(p.x + sz.x - S(12), nav_max.y + head_h);
    const char* page_title = (tab == 0) ? (*is_english ? "Home" : u8"主页") :
                             (tab == 1) ? (*is_english ? "Features" : u8"功能") :
                             (tab == 2) ? (*is_english ? "Notice" : u8"公告") :
                             (*is_english ? "Settings" : u8"设置");
    const char* page_sub = (tab == 0) ? (*is_english ? "Card key" : u8"卡密与授权") :
                           (tab == 1) ? (*is_english ? "Feature list" : u8"功能列表") :
                           (tab == 2) ? (*is_english ? "Server notice" : u8"服务器公告") :
                           (*is_english ? "Interface scale" : u8"界面缩放");
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tab_alpha);
    TextClip(dl, font_inter, S(19.f), head_min, head_max, U32(kAccent), page_title, ImVec2(0.f, 0.5f));
    TextClip(dl, font_inter, S(16.f), head_min, head_max, U32(kTextInactive), page_sub, ImVec2(1.f, 0.5f));
    dl->AddLine(ImVec2(head_min.x, head_max.y), ImVec2(head_max.x, head_max.y),
                U32(kBorder), S(1.f));
    ImGui::PopStyleVar();

    // ---------------- 拖动（标题栏）与缩放（右下角）命中的旧逻辑 ----------------
    // State gesture dibuat eksklusif: satu sentuhan hanya boleh drag header ATAU resize.
    static bool dragging = false;
    static bool resize_armed = false;
    static bool resizing = false;
    static ImVec2 drag_start_mouse{};
    static ImVec2 drag_start_pos{};
    static ImVec2 resize_start_mouse{};
    static ImVec2 resize_start_size{};
    static ImVec2 resize_start_pos{};

    ImVec2 grip_hit_min = info.win_max - S2(64, 64);
    ImVec2 grip_hit_max = info.win_max;
    bool grip_hover = ImGui::IsMouseHoveringRect(grip_hit_min, grip_hit_max, false);
    bool over_minimize = ImGui::IsMouseHoveringRect(owl_min, owl_max, false);
    bool title_hover = ImGui::IsMouseHoveringRect(title_min, title_max, false) &&
                       !over_minimize && !grip_hover;

    if (grip_hover && io.MouseClicked[0]) {
        dragging = false;
        resize_armed = true;
        resizing = false;
        resize_start_mouse = io.MousePos;
        resize_start_size = sz;
        resize_start_pos = p;
    } else if (title_hover && io.MouseClicked[0] && !resize_armed) {
        dragging = true;
        drag_start_mouse = io.MousePos;
        drag_start_pos = p;
    }

    if (!io.MouseDown[0]) {
        dragging = false;
        resize_armed = false;
        resizing = false;
    }

    if (dragging && io.MouseDown[0]) {
        const ImVec2 drag = io.MousePos - drag_start_mouse;
        ImVec2 next_pos = drag_start_pos + drag;
        next_pos.x = ImClamp(next_pos.x, 0.0f, ImMax(0.0f, io.DisplaySize.x - sz.x));
        next_pos.y = ImClamp(next_pos.y, 0.0f, ImMax(0.0f, io.DisplaySize.y - sz.y));
        ImGui::SetWindowPos(next_pos);
        window_pos = next_pos;
    }

    if (resize_armed && io.MouseDown[0]) {
        const ImVec2 drag = io.MousePos - resize_start_mouse;
        const float drag_threshold = S(6.f);
        if (!resizing && (ImAbs(drag.x) >= drag_threshold || ImAbs(drag.y) >= drag_threshold))
            resizing = true;

        if (resizing) {
            const float min_w = S(420.f);
            const float min_h = S(280.f);
            const float max_w = ImMax(min_w, io.DisplaySize.x - resize_start_pos.x - S(16.f));
            const float max_h = ImMax(min_h, io.DisplaySize.y - resize_start_pos.y - S(16.f));
            ImVec2 next(ImClamp(resize_start_size.x + drag.x, min_w, max_w),
                        ImClamp(resize_start_size.y + drag.y, min_h, max_h));

            // Kunci sisi kiri dan atas; hanya kanan dan bawah yang bergerak.
            ImGui::SetWindowPos(resize_start_pos);
            ImGui::SetWindowSize(next);
            window_pos = resize_start_pos;
            window_size = next;
        }
    }

    // 内容区：整宽，底部给状态栏留位置。
    const float status_h = S(40.f);
    info.show_image = false;
    info.content_min = ImVec2(p.x + S(12), head_max.y + S(6));
    info.content_max = ImVec2(p.x + sz.x - S(12), p.y + sz.y - status_h - S(6));
    info.image_min = info.image_max = info.content_max;
    return info;
}

inline void BeginContent(const FrameInfo& info, bool allow_scroll = true) {
    ImGui::SetCursorScreenPos(info.content_min);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, S2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tab_alpha);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground;
    if (!allow_scroll)
        flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##mod_content", info.content_max - info.content_min,
                      ImGuiChildFlags_AlwaysUseWindowPadding, flags);
}

// 分组标题：图标字形已全部去掉，只留文字 + 一条分隔线（官方 SeparatorText 的观感）。
inline void Section(const char* icon, const char* text) {
    (void)icon;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = S(24.f);
    const float avail = ImGui::GetContentRegionAvail().x;
    TextClip(dl, font_inter, S(18.f), p, p + ImVec2(avail, h), U32(kAccent), text, ImVec2(0.f, 0.5f));
    ImGui::Dummy(ImVec2(0.f, h));
    ImGui::Separator();
    ImGui::Spacing();
}

inline bool FeatButton(const char* icon, const char* label, const ImVec2& size = ImVec2(-1, 0)) {
    (void)icon;
    ImVec2 use = size;
    if (use.y <= 0.f) use.y = S(44.f);
    return ImGui::Button(label, use);
}

inline bool FeatCheckbox(const char* icon, const char* label, bool* v) {
    (void)icon;
    return ImGui::Checkbox(label, v);
}

// 产品分类：旧版是内容区最左边的一条竖排 rail，现在改成内容顶上的一排文字按钮，
// 内容区因此变成整宽（不再往右缩进 78px）。
inline void DrawProductCategoryRail(int* selected, bool english, FrameInfo& info) {
    if (!selected || tab != 1) return;
    const char* labels_en[] = { "UI", "Game", "Player", "Teleport", "Misc", "Manual" };
    const char* labels_zh[] = { u8"界面", u8"游戏", u8"玩家", u8"传送", u8"杂项", u8"手动" };
    const int values[] = { 1, 2, 3, 4, 5, 6 };
    const int count = 6;

    const float h = S(30.f);
    const float area_w = info.content_max.x - info.content_min.x;
    const float gap = S(4.f);
    float w = (area_w - gap * (count - 1)) / (float)count;
    if (w < S(52.f)) w = S(52.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 strip_min = info.content_min;
    ImVec2 strip_max(strip_min.x + area_w, strip_min.y + h);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, S2(6, 5));
    for (int i = 0; i < count; ++i) {
        ImVec2 pos(strip_min.x + (w + gap) * (float)i, strip_min.y);
        char id[48];
        snprintf(id, sizeof(id), "%s##prod_cat_%d", english ? labels_en[i] : labels_zh[i], i);
        ImGui::SetCursorScreenPos(pos);
        const bool active = (*selected == values[i]);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
        }
        if (ImGui::Button(id, ImVec2(w, h))) *selected = values[i];
        if (active) ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar();
    dl->AddLine(ImVec2(strip_min.x, strip_max.y + S(2)),
                ImVec2(strip_max.x, strip_max.y + S(2)), U32(kBorder), S(1.f));

    // 内容从分类条下面开始。
    info.content_min.y = strip_max.y + S(6);
}

inline void DrawGameSubBar(int* selected, bool english) {
    if (!selected) return;
    const char* labels_en[] = { "Life", "Hunt", "Farm", "Move", "Extra" };
    const char* labels_zh[] = { u8"生活", u8"狩猎", u8"种植", u8"移动", u8"其他" };
    const int count = 5;
    float h = S(34.f);
    float gap = S(4.f);
    float avail = ImGui::GetContentRegionAvail().x;
    float w = (avail - gap * (count - 1)) / (float)count;
    if (w < S(56.f)) w = S(56.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    for (int i = 0; i < count; i++) {
        ImVec2 pos = start + ImVec2((w + gap) * (float)i, 0.f);
        ImVec2 max = pos + ImVec2(w, h);
        char id[32];
        snprintf(id, sizeof(id), "##game_sub_bar_%d", i);
        ImGui::SetCursorScreenPos(pos);
        if (ImGui::InvisibleButton(id, ImVec2(w, h)))
            *selected = i;
        bool active = (*selected == i);
        bool hover = ImGui::IsItemHovered();
        dl->AddRectFilled(pos, max, U32(active ? kButton : (hover ? kButtonHover : kChild)), 0.f);
        dl->AddRect(pos, max, U32(active ? kAccent : kBorder), 0.f, 0, S(1.f));
        TextClip(dl, font_inter, S(16.f), pos, max,
                 U32(active ? kText : kTextInactive),
                 english ? labels_en[i] : labels_zh[i], ImVec2(0.5f, 0.5f));
    }
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(avail, h + S(10.f)));
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
            ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
            ImGui::PushStyleColor(ImGuiCol_Text, kOnAccent);
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

inline void DrawRightRail(int* selected, float height = 0.f) {
    if (!selected) return;
    const char* labels[] = { "Life", "Hunt", "Farm", "Move", "More", "Weather", "Action", "Camera", "Chat", "Build" };
    const int count = 10;
    float h = height > 1.f ? height : ImGui::GetContentRegionAvail().y;
    ImVec2 rail_sz(S(72.f), h);
    ImGui::BeginChild("##mod_right_rail", rail_sz, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    dl->AddRectFilled(wp, wp + ws, U32(kChild), 0.f);
    dl->AddRect(wp, wp + ws, U32(kBorder), 0.f, 0, S(1.f));
    ImVec2 origin = wp + S2(8, 10);
    for (int i = 0; i < count; i++) {
        ImVec2 pos = origin + ImVec2(0.f, S(52.f) * (float)i);
        ImVec2 max = pos + S2(56, 46);
        char id[32];
        snprintf(id, sizeof(id), "##right_tab_%d", i);
        ImGui::SetCursorScreenPos(pos);
        if (ImGui::InvisibleButton(id, S2(56, 46)))
            *selected = i;
        bool active = (*selected == i);
        bool hover = ImGui::IsItemHovered();
        dl->AddRectFilled(pos, max, U32(active ? kButton : (hover ? kButtonHover : kChild)), 0.f);
        dl->AddRect(pos, max, U32(active ? kAccent : kBorder), 0.f, 0, S(1.f));
        TextClip(dl, font_inter, S(18.f), pos, max,
                 U32(active ? kText : kTextInactive), labels[i], ImVec2(0.5f, 0.5f));
    }
    ImGui::EndChild();
}

inline void EndContent(bool allow_scroll = true) {
    if (allow_scroll && ImGui::IsWindowHovered() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

// 底部状态栏：语言切换 + 两个外链（都是文字按钮）+ 右下角缩放三角。
inline void EndChrome(const FrameInfo& info, bool* is_english = nullptr) {
    const float status_h = S(40.f);
    ImVec2 s_min(info.win_min.x, info.win_max.y - status_h);
    ImVec2 s_max(info.win_max.x, info.win_max.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(s_min, s_max, U32(kChild), 0.f);
    dl->AddLine(ImVec2(s_min.x, s_min.y), ImVec2(s_max.x, s_min.y), U32(kBorder), S(1.f));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, S2(8, 3));
    float x = s_min.x + S(10);
    const float y = s_min.y + S(4);
    (void)is_english;

    // 中英切换按钮已移除：界面固定中文，状态栏只留 QQ 群一个入口。
    // 「获取卡密」按钮已按需求删除（卡密统一走主页的卡密验证）。
    const char* group_label = ANDROID_JOIN_GROUP_LABEL_CN;

    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (TextLink(group_label) && detail::g_open_url_set && detail::g_open_url) {
        detail::g_open_url(ANDROID_GROUP_URL);
    }
    (void)x;
    ImGui::PopStyleVar();

    // 右下角缩放提示（命中区在 BeginChrome 里，比图形大得多，方便手指）
    ImVec2 grip_max(info.win_max.x - S(6), info.win_max.y - S(6));
    ImVec2 grip_min(grip_max.x - S(14), grip_max.y - S(14));
    dl->AddTriangleFilled(ImVec2(grip_min.x, grip_max.y), grip_max,
                          ImVec2(grip_max.x, grip_min.y), U32(kBorder));

    // ---------------- 轻提示（云下发接收成功之类）----------------
    if (toast_timer > 0.0f && !toast_text.empty()) {
        toast_timer -= ImGui::GetIO().DeltaTime;
        const float elapsed = toast_total - toast_timer;
        float a = 1.0f;
        if (elapsed < 0.22f) a = elapsed / 0.22f;                        // 淡入
        if (toast_timer < 0.45f) a = toast_timer / 0.45f;                // 淡出
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;

        ImFont* f = ImGui::GetFont();
        const float fs = S(19.f);
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, toast_text.c_str());
        const float cx = (s_min.x + s_max.x) * 0.5f;
        const float cy = info.win_min.y + S(86.f);
        const float padx = S(18.f), pady = S(9.f);
        ImVec2 bmin(cx - ts.x * 0.5f - padx, cy - ts.y * 0.5f - pady);
        ImVec2 bmax(cx + ts.x * 0.5f + padx, cy + ts.y * 0.5f + pady);
        dl->AddRectFilled(bmin, bmax, U32(kChild, 0.97f * a), S(4.f));
        dl->AddRect(bmin, bmax, U32(kAccent, 0.85f * a), S(4.f), 0, S(1.f));
        dl->AddText(f, fs, ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f), U32(kText, a),
                    toast_text.c_str());
        if (toast_timer <= 0.0f) toast_text.clear();
    }
    ImGui::End();
}

// 最小化状态：屏幕上的一个小方条，点一下恢复完整菜单。
// （曾试过"ImGui::Begin 长条 + 标题栏"的方案，用户觉得不合适，已回退到这一版。）
inline bool DrawMinimized(bool* restore, ImVec2* out_min = nullptr, ImVec2* out_max = nullptr) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(10.f, io.DisplaySize.y * 0.5f - S(28.f)), ImGuiCond_FirstUseEver);
    ImGui::Begin("##wwd_min", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoResize);
    ImVec2 size = S2(176, 52);
    ImVec2 mini_pos = ImGui::GetWindowPos();
    ImVec2 mini_clamped(
            ImClamp(mini_pos.x, 0.0f, ImMax(0.0f, io.DisplaySize.x - size.x)),
            ImClamp(mini_pos.y, 0.0f, ImMax(0.0f, io.DisplaySize.y - size.y)));
    if (ImAbs(mini_clamped.x - mini_pos.x) > 0.5f ||
        ImAbs(mini_clamped.y - mini_pos.y) > 0.5f) {
        ImGui::SetWindowPos(mini_clamped);
    }
    ImVec2 min = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min, min + size, U32(kButton), 0.f);
    dl->AddRect(min, min + size, U32(kAccent), 0.f, 0, S(1.f));
    TextClip(dl, font_inter, S(18.f), min + S2(12, 0), min + size - S2(30, 0),
             U32(kText), ANDROID_BRAND_NAME, ImVec2(0.f, 0.5f));
    // 右侧展开小方块
    ImVec2 box_min(min.x + size.x - S(27.f), min.y + S(18.f));
    ImVec2 box_max(min.x + size.x - S(12.f), min.y + S(33.f));
    dl->AddRect(box_min, box_max, U32(kAccent), 0.f, 0, S(1.5f));

    if (out_min) *out_min = min;
    if (out_max) *out_max = min + size;

    bool pressed = InvisibleHit("##wwd_minbtn", min, min + size);
    static bool mini_dragging = false;
    static ImVec2 mini_start_mouse{};
    static ImVec2 mini_start_pos{};
    if (ImGui::IsItemActivated()) {
        mini_dragging = false;
        mini_start_mouse = io.MousePos;
        mini_start_pos = ImGui::GetWindowPos();
    }
    if (ImGui::IsItemActive() && io.MouseDown[0]) {
        const ImVec2 drag = io.MousePos - mini_start_mouse;
        if (!mini_dragging && (ImAbs(drag.x) > S(7.f) || ImAbs(drag.y) > S(7.f)))
            mini_dragging = true;
        if (mini_dragging) {
            ImVec2 next = mini_start_pos + drag;
            next.x = ImClamp(next.x, 0.0f, ImMax(0.0f, io.DisplaySize.x - size.x));
            next.y = ImClamp(next.y, 0.0f, ImMax(0.0f, io.DisplaySize.y - size.y));
            ImGui::SetWindowPos(next);
        }
    }
    const bool gesture_was_drag = mini_dragging;
    if (pressed && !gesture_was_drag) {
        if (restore) *restore = true;
    }
    if (!io.MouseDown[0]) mini_dragging = false;
    ImGui::Dummy(size);
    ImGui::End();
    return true;
}

} // namespace Cylone
