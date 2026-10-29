#pragma once

// =====================================================================
//  BUILD FLAGS
// =====================================================================

// 卡密验证开关 (card-key verification switch)
//
//   1 = 关闭卡密验证：不输入卡密、不请求服务器，直接安装一个本地会话，
//       菜单照常打开，无需 key。给作者自用/测试用。
//   0 = 正常模式：必须登录卡密，走服务端 ECDH 校验。
//
// 注意：服务端下发的功能 payload（菜单里除"免费功能"以外的那些）仍然要
// 向服务器取。关掉验证只是省掉登录这一步，这些功能会因为拿不到 payload
// 而显示 Server payload unavailable (offline)，这是设计如此，不是 bug。
//
// 覆盖方式（优先级从高到低）：
//   1. ndk-build SAWIT_OFFLINE_MODE=0                        编译期
//   2. APP_CFLAGS / APP_CPPFLAGS 里 -DSAWIT_OFFLINE_MODE=0    编译期
//   3. 自己改这一行的默认值                                    编译期
#ifndef SAWIT_OFFLINE_MODE
#define SAWIT_OFFLINE_MODE 0
#endif

// 本地功能目录开关（离线的核心）
//
//   1 = 用内嵌的云端 dump 目录 + payload 信封：WireForOp(OP_*) 有值、
//       featureList 有 96 项、商店/烹饪/传送/载具替换这些信封齐的功能点得动。
//   0 = 只信服务器，payload 一律走 HTTP。
//
// 默认跟随 SAWIT_OFFLINE_MODE：离线构建必须为 1，否则菜单是空的。
// 在线版也保留内嵌功能目录（卡密通过后 LoginOfflineSession 安装）；
// 否则 WireForOp(OP_*) 全空，菜单里的功能点了没反应。
// 与安卓修复版一致：SAWIT_LOCAL_FEATURES = 1。
#ifndef SAWIT_LOCAL_FEATURES
#  define SAWIT_LOCAL_FEATURES 1
#endif

// 1 = 本地目录里没有该 wire 的信封时不再请求服务器（离线构建的正确行为：
//     网络本来就不通，请求只会白等一轮超时）。
// 0 = 本地没有就回落到 HTTP 取，可用于"离线目录 + 在线补缺"的混合构建。
// 在线版：本地目录没有的 wire 回落到服务器取（与安卓修复版一致 = 0）。
#ifndef SAWIT_LOCAL_PAYLOAD_ONLY
#  define SAWIT_LOCAL_PAYLOAD_ONLY 0
#endif

// 卡密校验通过后是否允许本地会话/内嵌目录落地（LoginOfflineSession）。
// 在线验证本身由 Weiyan 卡密负责；这个开关只决定进游戏后功能能不能用。
#ifndef SAWIT_ALLOW_LOCAL_SESSION
#  define SAWIT_ALLOW_LOCAL_SESSION 1
#endif

// 界面与请求体里显示的版本号。离线/在线都用 v1.1。
// 注意：payload 绑定串里的 version 不受这里影响 —— 内嵌 dump 信封各自带着
// 抓包时的 version（见 SAWIT_DUMP_BIND_VERSION 与 LocalCatalogData.h），
// 改这个显示版本不会让离线回放失效。
#define SAWIT_VERSION "v1.1"

// 悬浮窗标题栏显示的版本（只影响界面，不参与 payload 绑定/服务端校验）
#define SAWIT_UI_VERSION "v1.2 iOS"

// ---------------------------------------------------------------------
// 界面字体（加粗 / 加大）
// ---------------------------------------------------------------------
// SAWIT_UI_BOLD_FONT
//   0 = 原版字重（默认：字形膨胀这条路实测没必要，字体直接放大更稳）
//   1 = 走"字形 alpha 膨胀"的加粗 loader（imgui/custom/BoldFont.h）
// SAWIT_UI_FONT_SIZE_PX
//   正文字号（逻辑像素，实际还会乘 dpi）。原版是 19。
#ifndef SAWIT_UI_BOLD_FONT
#define SAWIT_UI_BOLD_FONT 0
#endif
#ifndef SAWIT_UI_FONT_SIZE_PX
#define SAWIT_UI_FONT_SIZE_PX 26.0f
#endif

#define SAWIT_CHANNEL "stable"

// 内嵌 dump 信封的绑定串（SHA256(nonce|hwid|wire|version|channel|op)）里用的
// version/channel，即抓包时客户端发出的那一组。换 dump 时由
// tools/gen_local_catalog.py 重新探测并写进 LocalCatalogData.h。
// ※ 这个值必须保持与抓包时一致（5.6.0），不要跟着界面的版本号改。
#define SAWIT_DUMP_BIND_VERSION "5.6.0"
#define SAWIT_DUMP_BIND_CHANNEL "stable"
