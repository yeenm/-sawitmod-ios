// ============================================================================
//  Log.mm — 日志落地（Includes/Logger.h 的实现）
// ----------------------------------------------------------------------------
//  LOGI/LOGE 默认是空宏（见 Includes/Logger.h 的 SM_ENABLE_LOG 开关），
//  打开后走这里 -> NSLog。注入进程里 NSLog 会进系统日志，用 Console.app 或
//  idevicesyslog 都能看到。
// ============================================================================

#import <Foundation/Foundation.h>

#include <cstdarg>
#include <cstdio>

extern "C" void SMLogPrintV(const char *level, const char *fmt, ...) {
    if (!fmt) return;
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    NSLog(@"[SawitMod/%s] %s", level ? level : "I", buffer);
}
