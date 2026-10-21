#pragma once
// ============================================================================
//  BoldFont.h — 让菜单字体"加粗"（字形 alpha 膨胀）
// ----------------------------------------------------------------------------
//  ImGui 1.92 没有合成加粗；工程里也只有 Regular 字重（Inter + Noto Sans SC），
//  设备 /system/fonts 上同样没有中文粗体（只有 NotoSansCJK-Regular.ttc）。塞一个
//  粗体 CJK 字体进 .so 要 +10MB 左右，不划算。
//
//  这里改包一层 stb_truetype 的 ImFontLoader：字形位图写进图集之后、上传 GPU
//  之前，对这块矩形做一次 3x3 取最大值的 alpha 膨胀 —— 笔画朝四周各长半个像素，
//  观感就是加粗。
//
//  为什么必须挂在 loader 上：1.92 的字形是"用到才栅格化"，一次性处理整张图集
//  覆盖不到后面才出现的字（道具名、动作名、玩家名这类动态中文）。
//  FontBakedLoadGlyph 是每个字形唯一的落点。
//
//  只动图集分配器划给这个字形的矩形：不越界、不吃到邻居的字形。
// ============================================================================

#include "../imgui.h"
#include "../imgui_internal.h"

#include <cstring>
#include <vector>

namespace BoldFont {

// 膨胀次数：1 = 常规加粗（默认），2 = 更粗（小字号可能笔画粘连，慎用）。
inline int &Passes() {
    static int passes = 1;
    return passes;
}

inline bool &Enabled() {
    static bool enabled = true;
    return enabled;
}

namespace detail {

// 对一块 alpha 平面做一次 3x3 取最大值膨胀。取样只在矩形内部，边界不外扩。
inline void DilateAlpha(unsigned char *base, int pitch, int bytesPerPixel, int alphaOffset,
                        int w, int h) {
    if (!base || w < 2 || h < 2 || pitch <= 0) return;

    thread_local std::vector<unsigned char> source;
    source.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        const unsigned char *row = base + static_cast<size_t>(y) * pitch;
        for (int x = 0; x < w; ++x)
            source[static_cast<size_t>(y) * w + x] =
                    row[static_cast<size_t>(x) * bytesPerPixel + alphaOffset];
    }

    for (int y = 0; y < h; ++y) {
        unsigned char *row = base + static_cast<size_t>(y) * pitch;
        for (int x = 0; x < w; ++x) {
            unsigned char best = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int ny = y + dy;
                if (ny < 0 || ny >= h) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    if (nx < 0 || nx >= w) continue;
                    const unsigned char value = source[static_cast<size_t>(ny) * w + nx];
                    if (value > best) best = value;
                }
            }
            row[static_cast<size_t>(x) * bytesPerPixel + alphaOffset] = best;
        }
    }
}

inline bool BoldFontBakedLoadGlyph(ImFontAtlas *atlas, ImFontConfig *src, ImFontBaked *baked,
                                   void *loaderData, ImWchar codepoint, ImFontGlyph *outGlyph,
                                   float *outAdvanceX) {
    static const ImFontLoader *stb = nullptr;
    if (!stb) stb = ImFontAtlasGetFontLoaderForStbTruetype();
    if (!stb || !stb->FontBakedLoadGlyph) return false;

    const bool ok = stb->FontBakedLoadGlyph(atlas, src, baked, loaderData, codepoint, outGlyph,
                                            outAdvanceX);
    if (!ok || !outGlyph || !outGlyph->Visible) return ok;
    if (!Enabled() || Passes() <= 0 || !atlas) return ok;

    ImTextureData *texture = atlas->TexData;
    if (!texture || !texture->Pixels || texture->BytesPerPixel <= 0) return ok;

    ImTextureRect *rect = ImFontAtlasPackGetRect(atlas, outGlyph->PackId);
    if (!rect || rect->w < 2 || rect->h < 2) return ok;

    // 写之前再确认一次边界：膨胀是原地改图集纹理，越界就是踩别人的字形。
    if (rect->x + rect->w > texture->Width || rect->y + rect->h > texture->Height) return ok;

    unsigned char *base =
            static_cast<unsigned char *>(texture->GetPixelsAt(rect->x, rect->y));
    if (!base) return ok;

    const int alphaOffset = (texture->BytesPerPixel == 4) ? 3 : 0;
    for (int pass = 0; pass < Passes(); ++pass)
        DilateAlpha(base, texture->GetPitch(), texture->BytesPerPixel, alphaOffset,
                    rect->w, rect->h);
    return ok;
}

} // namespace detail

// 加粗 loader（进程内单例）。必须在 io.Fonts->Clear() 之后、AddFont* 之前设置。
inline const ImFontLoader *Loader() {
    static ImFontLoader loader;
    static bool ready = false;
    if (!ready) {
        const ImFontLoader *stb = ImFontAtlasGetFontLoaderForStbTruetype();
        if (!stb || !stb->FontBakedLoadGlyph) return nullptr;
        loader = *stb;                       // 整份拷贝，只替换字形加载这一格
        loader.Name = "stb_truetype+bold";
        loader.FontBakedLoadGlyph = detail::BoldFontBakedLoadGlyph;
        ready = true;
    }
    return &loader;
}

} // namespace BoldFont
