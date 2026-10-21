#pragma once
// ============================================================================
//  dobby.h — Dobby 在 iOS/arm64 上的最小声明
// ----------------------------------------------------------------------------
//  与上游 Dobby（jidongae/dobby）导出的 C API 一致。工程里只用到：
//    - DobbyHook             安装 inline hook（arm64 支持 iOS/AArch64）
//    - DobbySymbolResolver   在指定镜像里找符号（image_name 传 nullptr 表示全进程）
//
//  说明：这里把 replace/origin 的形参声明成 void*，是因为这份源码里所有调用点
//  都传的是强转后的裸地址（Android 用的 A64HookFunction 也是 void* 形态）。
//  上游 Dobby 用的是 dobby_dummy_func_t；两种写法在 C++ 里混用会报类型不匹配，
//  统一成 void* 后 Android 移植过来的调用点不用改。
//
//  链接 <ios>/SawitMod/ThirdParty/Dobby/libdobby.a（需自行编译 ios/arm64 目标，
//  详见 docs/构建与注入.md）。
// ============================================================================

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dobby_dummy_func_t)();

// 返回 0 表示成功（兼容上游的 RS_SUCCESS）；失败时 origin_func 不一定被写入。
#define RS_SUCCESS 0
#define RS_FAILED 1

int DobbyHook(void *address, void *replace_func, void **origin_func);

// 卸载 DobbyHook 装的 hook（可选）。
int DobbyUnhook(void *address, void *origin_func);

// 在 image_name 中解析 symbol_name。image_name 为 NULL 时搜索所有已加载镜像。
void *DobbySymbolResolver(const char *image_name, const char *symbol_name);

// 销毁 Dobby 为该地址保存的 trampoline。
int DobbyDestroy(void *address);

#ifdef __cplusplus
}
#endif
