#pragma once
// ============================================================================
//  xdl.h — iOS 版 xdl 兼容层
// ----------------------------------------------------------------------------
//  Android 工程里所有跨 so 的符号解析都用 xdl（Starcoolxdl）。iOS 的镜像
//  是 Mach-O，_dyld 与 dlsym 已经够用；这里保持同名 API，源码不用动：
//
//    xdl_open("libil2cpp.so")            -> 主镜像句柄（Unity/UnityFramework）
//    xdl_open("libmonosgen-2.0.so")      -> mono 兼容层句柄（il2cpp 垫片）
//    xdl_sym(h, "il2cpp_domain_get")     -> dlsym(RTLD_DEFAULT) 或 mono 垫片表
//    xdl_sym(h, "mono_class_from_name")  -> mono 垫片（见 IL2CPP/MonoCompat.cpp）
//    xdl_info(h, XDL_DI_DLINFO, &info)   -> dladdr 填 dli_fbase
// ============================================================================

#include <stddef.h>
#include <stdint.h>

#define XDL_DEFAULT 0x0001
#define XDL_TRY_FORCE_LOAD 0x0002
#define XDL_ALWAYS_FORCE_LOAD 0x0003

#define XDL_DI_DLINFO 1

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} xdl_info_t;

#ifdef __cplusplus
extern "C" {
#endif

void *xdl_open(const char *name, int flags);
void *xdl_sym(void *handle, const char *name, size_t *size);
void *xdl_dsym(void *handle, const char *name, size_t *size);
int xdl_close(void *handle);
int xdl_info(void *handle, int request, void *info);

#ifdef __cplusplus
}
#endif
