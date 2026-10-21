#pragma once
// ============================================================================
//  OSCache.h — 指令缓存同步（arm64 代码补丁后必须做）
// ----------------------------------------------------------------------------
//  不要用 __builtin___clear_cache：在 arm64 上 clang 会生成对 ___clear_cache
//  的调用，而 Theos 用 -undefined dynamic_lookup 链接 dylib 时 ld64 拒绝从
//  dyld shared cache 动态绑定这个符号，构建直接失败：
//      ld: -undefined dynamic_lookup cannot be used to find '___clear_cache'
//  iOS 的正确做法是 sys_icache_invalidate（libkern/OSCacheControl.h）。
// ============================================================================

#include <cstddef>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#else
// 非 Apple 平台（本地 MSVC 语法/链接自检）给个空实现。
inline void sys_icache_invalidate(void *start, size_t len) {
    (void)start;
    (void)len;
}
#endif
