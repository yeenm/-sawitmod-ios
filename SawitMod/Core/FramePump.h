#pragma once
// ============================================================================
//  FramePump.h — Unity 帧回调入口（实现见 Core/FramePump.cpp）
// ----------------------------------------------------------------------------
//  GameApp.Update 被 DobbyHook 到 SMGameAppUpdate；原函数指针存 SMOldGameAppUpdate。
//  安装逻辑在 App/Bootstrap.mm（偏移来自 Core/Offsets-iOS.h）。
// ============================================================================

// 每帧泵：处理 Unity 任务队列、功能 OnUpdate、雷达扫描等。
void SMRunUnityFramePump();

// 被 hook 的 GameApp.Update 替身。
void SMGameAppUpdate(void *instance);

// 真 GameApp.Update（hook 时写入）。
extern void (*SMOldGameAppUpdate)(void *);
