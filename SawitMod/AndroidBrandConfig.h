#pragma once

// Ubah SATU nilai ini sebelum build:
#define ANDROID_BRAND_SAWIT  1
#define ANDROID_BRAND_NEWBIE 2
#define ANDROID_ACTIVE_BRAND ANDROID_BRAND_SAWIT

#if ANDROID_ACTIVE_BRAND == ANDROID_BRAND_SAWIT
#  define ANDROID_IS_SAWIT 1
#  define ANDROID_IS_NEWBIE 0

// PENTING: ANDROID_CLIENT_BRAND ikut dikirim ke server saat login/payload
// (body request). Nilainya HARUS tetap "SAWIT" walau teks UI sudah diganti,
// kalau diubah server akan menolak sesi.
inline constexpr const char* ANDROID_CLIENT_BRAND = "SAWIT";

// ---- Teks yang tampil di UI ----
inline constexpr const char* ANDROID_BRAND_NAME = u8"婉婉的爹";
inline constexpr const char* ANDROID_BRAND_HOME_TITLE = u8"婉婉的爹";
inline constexpr const char* ANDROID_BRAND_FOOTER_EN = u8"婉婉的爹 @ 2026";
inline constexpr const char* ANDROID_BRAND_FOOTER_CN = u8"婉婉的爹 @ 2026 保留所有权利";

// Informasi dan tautan (tetap mendukung EN/CN).
//inline constexpr const char* ANDROID_GROUP_TEXT_EN = "Join QQ: 1105978541";
//inline constexpr const char* ANDROID_GROUP_TEXT_CN = u8"加入QQ：1105978541";
//inline constexpr const char* ANDROID_JOIN_GROUP_LABEL_EN = "Join Group QQ";
//inline constexpr const char* ANDROID_JOIN_GROUP_LABEL_CN = u8"加入群组";
//inline constexpr const char* ANDROID_GET_KEY_LABEL_EN = "Get Key";
//inline constexpr const char* ANDROID_GET_KEY_LABEL_CN = u8"获取卡密";
//inline constexpr const char* ANDROID_GROUP_URL = "https://qm.qq.com/q/ETSliSKsqk";
//inline constexpr const char* ANDROID_GET_KEY_URL = "https://fakajs.cn/shop/heartopia";

inline constexpr const char* ANDROID_GROUP_TEXT_EN = "QQ group: 点击加入";
inline constexpr const char* ANDROID_GROUP_TEXT_CN = u8"加入 QQ 群获取最新版本";
inline constexpr const char* ANDROID_JOIN_GROUP_LABEL_EN = u8"加入QQ群";
inline constexpr const char* ANDROID_JOIN_GROUP_LABEL_CN = u8"加入QQ群";
inline constexpr const char* ANDROID_GET_KEY_LABEL_EN = u8"获取卡密";
inline constexpr const char* ANDROID_GET_KEY_LABEL_CN = u8"获取卡密";
inline constexpr const char* ANDROID_GROUP_URL = "https://qm.qq.com/q/V13JI9One4";
inline constexpr const char* ANDROID_GET_KEY_URL = "https://ys.42bb.cn/shop/heartopia";

#elif ANDROID_ACTIVE_BRAND == ANDROID_BRAND_NEWBIE
#  define ANDROID_IS_SAWIT 0
#  define ANDROID_IS_NEWBIE 1

inline constexpr const char* ANDROID_CLIENT_BRAND = "NEWBIE";
inline constexpr const char* ANDROID_BRAND_NAME = u8"婉婉魔法";
inline constexpr const char* ANDROID_BRAND_HOME_TITLE = u8"婉婉魔法";
inline constexpr const char* ANDROID_BRAND_FOOTER_EN = u8"婉婉魔法 @ 2026 保留所有权利";
inline constexpr const char* ANDROID_BRAND_FOOTER_CN = u8"婉婉魔法 @ 2026 保留所有权利";

// Informasi dan tautan Newbie. Semua label hanya berbahasa China.
inline constexpr const char* ANDROID_GROUP_TEXT_EN = u8"加入QQ群：1101242913";
inline constexpr const char* ANDROID_GROUP_TEXT_CN = u8"加入QQ群：1101242913";
inline constexpr const char* ANDROID_JOIN_GROUP_LABEL_EN = u8"加入群组";
inline constexpr const char* ANDROID_JOIN_GROUP_LABEL_CN = u8"加入群组";
inline constexpr const char* ANDROID_GET_KEY_LABEL_EN = u8"获取卡密";
inline constexpr const char* ANDROID_GET_KEY_LABEL_CN = u8"获取卡密";
inline constexpr const char* ANDROID_GROUP_URL = "https://qm.qq.com/q/BP3WDACviU";
inline constexpr const char* ANDROID_GET_KEY_URL = "https://q3g.66url.cn/cBx";

#else
#  error "ANDROID_ACTIVE_BRAND harus ANDROID_BRAND_SAWIT atau ANDROID_BRAND_NEWBIE"
#endif
