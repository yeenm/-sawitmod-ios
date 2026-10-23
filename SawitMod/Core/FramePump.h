#pragma once
// ============================================================================
//  FramePump.h — Unity 帧回调入口（实现见 Core/FramePump.cpp）
// ----------------------------------------------------------------------------
//  GameApp.Update 被 DobbyHook 到 SMGameAppUpdate；原函数指针存 SMOldGameAppUpdate。
//  安装逻辑在 App/Bootstrap.mm（偏移来自 Core/Offsets-iOS.h）。
// ============================================================================

// 是否启用每帧泵。侧载 iOS 上帧泵由 CADisplayLink 主线程驱动，
// 但在加载画面/热更资源阶段跑会卡住主进程，因此默认关闭，
// 由菜单「设置 → 启用每帧功能」在世界加载完成后手动打开。
extern bool g_framePumpArmed;

// 每帧泵：处理 Unity 任务队列、功能 OnUpdate、雷达扫描等。
void SMRunUnityFramePump();

// 被 hook 的 GameApp.Update 替身。
void SMGameAppUpdate(void *instance);

// 真 GameApp.Update（hook 时写入）。
extern void (*SMOldGameAppUpdate)(void *);
