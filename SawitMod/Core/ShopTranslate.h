#pragma once
// ============================================================================
//  ShopTranslate.h — 商店名英译中（Main.cpp 第 328 行起的 inline 帮助函数）
// ----------------------------------------------------------------------------
//  从 Android 版 Main.cpp 原样搬来：菜单"远程商店"列表在中文界面下用这套映射。
// ============================================================================

#include <string>

#include "Global.h"
inline std::string TranslateShopName(const std::string& enName) {
    if (g_language != LANG_CN) return enName;
    if (enName == "General") return u8"杂货商店";
    if (enName == "Mall") return u8"未知";
    if (enName == "Dress") return u8"多萝西装扮店";
    if (enName == "Bob Daily") return u8"波叔日常家具";
    if (enName == "Bob Weekly") return u8"波叔本周家具";
    if (enName == "Emoji") return u8"友爱商店";
    if (enName == "Rug") return u8"地毯商店";
    if (enName == "Postcard") return u8"明信片商店";
    if (enName == "Event Snow") return u8"潮流爱好";
    if (enName == "Garden") return u8"园艺商店";
    if (enName == "Fishing") return u8"钓鱼商店";
    if (enName == "Cooking") return u8"烹饪商店";
    if (enName == "Pet") return u8"宠物商店";
    if (enName == "Bird") return u8"观鸟商店";
    if (enName == "Insect") return u8"捕捉商店";
    if (enName == "Vehicle") return u8"载具商店";
    if (enName == "Music") return u8"音乐商店";
    if (enName == "Bob Store") return u8"特殊家装商店";
    if (enName == "Doris 1") return u8"幸运商店1";
    if (enName == "Doris 2") return u8"幸运商店2";
    if (enName == "Purchase") return u8"时尚生活";
    if (enName == "Island") return u8"海岛利用申请";
    if (enName == "BP Purchase") return u8"潮流季商店";
    if (enName == "Meteor") return u8"流星商店";
    if (enName == "Research") return u8"研究室成果商店";
    return enName;
}
