#pragma once

#include <atomic>
#include <cstdint>

// ============================================================================
//  BubbleFeatures.h — 气泡自动化（移植自 PC 端 Bugtopia-Solo / 安卓 v1.6.2 补丁）
// ----------------------------------------------------------------------------
//  安卓补丁（BubbleFeatures.cpp）实现三件事：
//    1) 按 BPM 调 ActivityEventProtocolManager.CreateActivityBubble(Vector3) 刷气泡；
//    2) 对 CreateBubble / CreateVisitBubble / CreateActivityBubble /
//       CreateHideAndSeekBubble 做 Dobby detour，把位置矢量改写成玩家坐标；
//    3) 自动收集气泡（BubbleProtocolManager.GetBubbleAward）。
//
//  iOS 版这里补的是 1) 和 2)，3) 已有独立实现（OP_AUTO_COLLECT_BUBBLE 云 payload +
//  AutoBubbleLoopThread，见 Menu「自动收集气泡」）。
//
//  ⚠️ iOS 侧载（非越狱）构建里 Dobby 被禁用（Includes/Dobby/dobby.h 的
//  SMDisabledDobbyHook），所以第 2 项的"改写游戏自身生成的气泡"只在越狱构建
//  （-DSM_ALLOW_DOBBY_HOOK=1）里生效；侧载下仍可用 1)，因为我们自己的
//  CreateActivityBubble 调用本来就直接传玩家坐标。
// ============================================================================

namespace BubbleFeatures {

// 开启工作线程（开关由 Core/State.h 的 g_fastBubbleGenEnabled /
// g_bubbleSpawnAtPlayerEnabled 控制）。
void Start();
void Stop();

// 手动生成一颗气泡（立即执行，不需要工作线程）。
void ManualSpawn();

// UI 打开/切换开关时调用，确保线程与 detour 已安装。
void Refresh();

// 本进程已生成的气泡数（UI 显示 / 日志）。
extern std::atomic<uint64_t> g_generated;

} // namespace BubbleFeatures
