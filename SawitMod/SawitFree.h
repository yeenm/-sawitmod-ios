#pragma once

// ===================== 免费功能（不需要卡密） =====================
// 采集类的三个 wire，卡密没通过也允许派发。wire id 取自内嵌 dump：
//   features[53] 采摘所有植物   op=27
//   features[24] 瞬间采集触发器 op=11
//   features[23] 自动收集气泡   op=10
// 放在单独头文件里（纯 inline），这样门禁（NetAuth）与界面（Main）
// 以及设备侧测试共用同一份清单，不会出现两边不一致。
#include <string>

namespace SawitFree {

inline constexpr const char* kPickAllPlant = "28f1cdc36ba4";   // 采摘所有植物
inline constexpr const char* kInstantFarm = "fce95012bfed";    // 瞬间采集触发器
inline constexpr const char* kCollectBubble = "ebaba749de66";  // 自动收集气泡

inline bool IsFree(const std::string& wireId) {
    return wireId == kPickAllPlant || wireId == kInstantFarm ||
           wireId == kCollectBubble;
}

}  // namespace SawitFree
