#pragma once
// ============================================================================
//  Logger.h — iOS 版日志垫片
// ----------------------------------------------------------------------------
//  Android 工程里到处是 LOGI()/LOGE()（格式串 + 变参，有的调用还多塞了一个
//  无用参数，例如 LOGE("Error Getting JNI", 1)）。iOS 没有 android/log.h。
//
//  这里保持宏名与调用形态不变，默认编译成空语句，不产生任何代码：
//  注入进程里日志没有消费者，保留它只会拖慢热路径。需要排查时把
//  SM_ENABLE_LOG 打开（或者在 Xcode 里给 dylib 加该宏），日志走 NSLog。
// ============================================================================

#ifndef SM_ENABLE_LOG
#define SM_ENABLE_LOG 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

void SMLogPrintV(const char *level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#if SM_ENABLE_LOG

#define LOGI(...) SMLogPrintV("I", __VA_ARGS__)
#define LOGD(...) SMLogPrintV("D", __VA_ARGS__)
#define LOGW(...) SMLogPrintV("W", __VA_ARGS__)
#define LOGE(...) SMLogPrintV("E", __VA_ARGS__)
#define SMLOG(...) SMLogPrintV("I", __VA_ARGS__)

#else

// 空语句：实参不会被求值，因此 OBFUSCATE(...) 之类的临时对象也不会生成。
#define LOGI(...) ((void)0)
#define LOGD(...) ((void)0)
#define LOGW(...) ((void)0)
#define LOGE(...) ((void)0)
#define SMLOG(...) ((void)0)

#endif
