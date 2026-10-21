#pragma once
// ============================================================================
//  Bootstrap.h — iOS 侧启动编排（等价 Android 版 Main.cpp 的 hack_thread）
// ----------------------------------------------------------------------------
//  启动顺序（每一步都不阻塞主线程）：
//    1. 装崩溃处理器（越早越好）
//    2. 等 UIApplication 起来 -> 主线程创建悬浮窗口（ImGui 立即可以画）
//    3. 读本地授权文件；离线构建装本地会话 + 内嵌功能目录
//    4. 等 il2cpp 运行时（dlsym 到 il2cpp_* + 主程序集加载完成）
//    5. 安装 mono->il2cpp 兼容层指针
//    6. 挂钩 GameApp.Update（帧泵）——偏移来自 Core/Offsets-iOS.h，未标定时跳过
//    7. 启动设备遥测（ClientControlAgent）
// ============================================================================

namespace SMBootstrap {

// 由 dylib 的 constructor 调用一次。
void Start();

// 是否已连上 il2cpp 运行时。
bool RuntimeReady();

}  // namespace SMBootstrap
