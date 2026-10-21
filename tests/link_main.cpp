// ============================================================================
//  link_main.cpp — 链接自检的入口
// ----------------------------------------------------------------------------
//  不做真实功能，只引用几个跨模块入口，保证链接器把 C++ 目标文件全部拉进来
//  （/OPT:NOREF 在脚本里设置）。真正的目的是让链接器报出"声明了没定义"的符号。
// ============================================================================

#include <cstdio>

#include "Core/State.h"
#include "Core/FramePump.h"
#include "ModFeatures.h"
#include "NetAuth.h"

int main() {
    printf("sawitmod link check\n");
    printf("features=%zu\n", featureList.size());
    g_MenuMinimized = false;
    return 0;
}
