#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 State.h 的 extern 声明生成 State.cpp 的定义部分。

用法： python scripts/gen_state.py
输出： SawitMod/Core/State.cpp = 头部 + 自动定义 + State_manual.inc

为什么要有这个脚本：State.h 是从 Android 的 Global.h 移植来的，里面有近 300 个
全局变量。手抄一遍不仅慢，而且每次加变量都要两边改。生成器只处理"变量声明"，
函数原型与需要初值的表跳过（后者写在 State_manual.inc）。
"""
import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "SawitMod", "Core", "State.h")
MANUAL = os.path.join(ROOT, "SawitMod", "Core", "State_manual.inc")
OUTPUT = os.path.join(ROOT, "SawitMod", "Core", "State.cpp")

# 这些名字由 State_manual.inc 提供（需要显式初值或手写定义）
SKIP = {
    "farmNames", "farmNamesZh", "g_homeCoords", "originalVehicleNames",
    "originalVehicleIds", "targetVehicleNames", "targetVehicleIds",
    "selectedOriginalIdx", "selectedTargetIdx", "g_ownerNetIds", "libLoaded", "workPaths", "g_drawStatus", "g_selectedImageIdx",
    "g_cachedConfigManager", "g_cachedLevelScriptableConfig",
    "g_cachedPlayerMotionConfig", "g_StolenInteractSystem",
    "g_MenuRect", "g_MinRect", "g_MenuBoundsMutex", "g_MenuMinimized",
    "g_RequestedUiScale", "g_RealScreenWidth", "g_RealScreenHeight",
    "g_AppInForeground", "g_GameWorldReady", "g_unityFramePumpReady",
    "g_premiumWoodOnePass",
    "g_autoFishRodId",
    "g_autoFishRadius",
    "g_autoFishTeleport",
    "g_autoFishGap",
    "g_autoFishCatchGraceTicks",
    "g_autoFishBiteTimeoutTicks",
    "g_autoFishEquipDelayTicks",
    "g_autoFishHoverBetweenCasts",
    "g_autoFishHoverOriginalGravity",
    "g_autoFishSkipCastAnim",
    "g_autoFishSkipCatchAnim",
    "g_autoFishStatus",
    "g_autoFishTargetStatus",
    "isInstantFishing",
    "g_fishThrowDist",
    "g_lureBallCooldownSec",
    "g_lureBallStatus",
    "g_fishBaitIntervalMs",
    "g_fishBaitStatus",
    "g_fishBaitStatusZh",
    "g_birdScaredCount",
    "g_birdAutoScareMaxed",
    "g_sandPreferRare",
    "g_sandLastPickProduct",
    "g_sandLastPickModel",
    "g_birdCatchCooldown",
    "g_birdScanRange",
    "g_birdMinPhotoDist",
    "g_birdInvokeCooldown",
    "g_pumpkinScore",
    "g_pumpkinScoreCount",
}

HEAD = """// ============================================================================
//  State.cpp — 全局状态定义（由 State.h 的 extern 逐条落地）
// ----------------------------------------------------------------------------
//  ⚠️ 这个文件由 scripts/gen_state.py 生成：新增全局变量请改 State.h 再跑脚本。
//     需要初值的表/手写定义在 State_manual.inc。
// ============================================================================

#include "Core/State.h"

#include "App/Defaults.h"
#include "Includes/obfuscate.h"

"""


def main() -> None:
    text = io.open(HEADER, encoding="utf-8").read()
    defs, seen = [], set()
    for line in text.splitlines():
        raw = line.strip()
        # 去掉行尾注释再判断
        code = raw.split("//", 1)[0].strip()
        if not code.startswith("extern ") or code.startswith('extern "C"'):
            continue
        if "(" in code:  # 函数原型
            continue
        body = code[len("extern "):].rstrip(";").strip()
        m = re.match(r"^(.*?)\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?$", body)
        if not m:
            print("skip(unparsed):", raw)
            continue
        typ, name, arr = m.group(1).strip(), m.group(2), m.group(3) or ""
        if name in SKIP or name in seen:
            continue
        seen.add(name)
        defs.append(f"{typ} {name}{arr};")

    manual = io.open(MANUAL, encoding="utf-8-sig").read()
    out = HEAD + "\n".join(defs) + "\n\n" + manual
    io.open(OUTPUT, "w", encoding="utf-8", newline="\n").write(out)
    print(f"State.cpp: {len(defs)} definitions, {len(out.splitlines())} lines")


if __name__ == "__main__":
    main()

# CI_BUILD_TRIGGER: macos runner switch rebuild trigger
