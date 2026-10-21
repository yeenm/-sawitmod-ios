#pragma once
// ============================================================================
//  CrashHandler.h — 崩溃日志（替代 Java CrashHandler）
// ----------------------------------------------------------------------------
//  Java 版靠 Thread.setDefaultUncaughtExceptionHandler 抓托管异常，写
//  /storage/emulated/0/Documents/mod_menu_crash_*.txt。
//
//  iOS 上进程崩溃主要来自 Mach 异常/信号（SIGSEGV/SIGABRT/SIGBUS/SIGILL/
//  SIGFPE/SIGTRAP）。这里注册 NSSetUncaughtExceptionHandler + 信号处理器，
//  用 async-signal-safe 的 write() + backtrace_symbols_fd() 落盘到
//  ~/Documents/mod_menu_crash_<time>.txt，然后恢复默认处理并重新抛出。
// ============================================================================

namespace SMCrash {

// 在 dylib 初始化早期调用（越早越好，能覆盖启动期崩溃）。
void Install();

// 最近一次崩溃日志路径（没有则为空）。
const char *LastCrashPath();

}  // namespace SMCrash
