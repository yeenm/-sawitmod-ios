#pragma once
// ============================================================================
//  PeerEsp.h — 同行绘制（iOS 版）
// ----------------------------------------------------------------------------
//  与安卓 v1.8.7/v1.8.8 的 PeerEsp 同源，移植要点：
//    * 名单层：POST /api/peer/register + /api/peer/list（自建面板，卡密+设备码鉴权）
//      另支持本地兜底文件 peer_tags.json（PC 版写的就是这个格式）
//    * 扫描层：Unity 线程上枚举 _netEntitiesMap 的 netId，用游戏的
//      PlayerProtocolManager.TryGetPlayerShortId / TryGetPlayerEncodeShortId
//      把 netId 换成「原始 shortId / 名片 Id」，再和名单做双路匹配
//    * 投影层：Camera.WorldToScreenPoint + 背后 1-x/1-y 翻转 + 相机直线距离
//    * 绘制层：iOS 自己有 ImGui 悬浮层（CADisplayLink 每帧渲染），直接用
//      ImGui 的背景 drawList 画金色方框 / 贴边箭头，不需要 hook 游戏渲染
//
//  和原有「视觉 ESP（雷达）」的关系：
//    完全独立。原有 g_radarEnabled / g_radarList / RenderESPOverlay 一行不改，
//    同行绘制有自己的开关、自己的实体扫描、自己的绘制列表（见 .cpp 里的
//    g_drawList），所以「绘制同行」不开时行为与改动前完全一致。
// ============================================================================

#include <string>

// ---- 每帧（Unity 线程，由 Core/FramePump.cpp 的帧泵调用）----
//  内部自带节流：名单同步 6~30s、实体扫描 300ms、投影每帧。
//  未登录卡密 / 开关关闭 / 还没进世界时什么都不做。
void PeerEspFrameTick();

// ---- 每帧（渲染线程，由 UI/MenuGlue.cpp 在 ImGui NewFrame 之后调用）----
//  只读 .cpp 里的投影结果，不碰任何 mono 调用。
void PeerEspRenderOverlay();

// ---- 开关（菜单用；默认开，与安卓一致）----
bool PeerEspDrawEnabled();
void PeerEspSetDrawEnabled(bool enabled);

// ---- 名单查询（raw shortId，数字）----
bool PeerEspHasPeer(long long shortId);
std::string PeerEspLabel(long long shortId);

// ---- 名单查询（名片 Id，例如 4s15w01）----
bool PeerEspHasPeerUid(const std::string &uid);
std::string PeerEspLabelForUid(const std::string &uid);

// ---- 扫描期学到「raw shortId <-> 名片 Id」后回填（云名单里只留 uid 的账号靠它）----
void PeerEspRememberProfile(long long shortId, const std::string &encodedUid);

// ---- 菜单：立即同步一次 / 名单条数 / 状态行 / 诊断文本 ----
void PeerEspSyncNow();
int PeerEspPeerCount();
std::string PeerEspStatusLine();
std::string PeerEspDiagnostics();
// 诊断用：在 Unity 线程上把依赖符号/自身身份解析一次（「符号自检」按钮调用）。
void PeerEspPrime();
