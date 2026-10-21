// ============================================================================
//  xdl.cpp — iOS 上的 xdl 兼容层实现
// ----------------------------------------------------------------------------
//  handle 是静态表里的占位结构，不参与资源管理；xdl_close 什么都不做。
//  符号解析顺序：
//    1. mono_*  -> MonoCompat 垫片（把 mono API 映射到 il2cpp）
//    2. 其它     -> dlsym(RTLD_DEFAULT)，再试 dlopen(NULL)
// ============================================================================

#include "Starcoolxdl/xdl.h"

#include <dlfcn.h>
#include <cstring>
#include <mach-o/dyld.h>

#include "IL2CPP/MonoCompat.h"

namespace {

struct XdlHandle {
    const char *name;
    void *image;  // 主镜像 header
};

XdlHandle g_il2cppHandle{"libil2cpp.so", nullptr};
XdlHandle g_monoHandle{"libmonosgen-2.0.so", nullptr};
XdlHandle g_genericHandle{"", nullptr};

XdlHandle *HandleFor(const char *name) {
    if (!name) return &g_genericHandle;
    if (strstr(name, "mono")) return &g_monoHandle;
    if (strstr(name, "il2cpp") || strstr(name, "UnityFramework") || strstr(name, "unity"))
        return &g_il2cppHandle;
    return &g_genericHandle;
}

}  // namespace

extern "C" void *xdl_open(const char *name, int flags) {
    (void)flags;
    XdlHandle *h = HandleFor(name);
    if (!h->image && _dyld_image_count() > 0)
        h->image = (void *)_dyld_get_image_header(0);
    return (void *)h;
}

extern "C" void *xdl_sym(void *handle, const char *name, size_t *size) {
    if (size) *size = 0;
    if (!name) return nullptr;

    // mono_* 一律走兼容层：iOS 没有 libmonosgen，游戏是纯 il2cpp。
    if (strncmp(name, "mono_", 5) == 0 || strncmp(name, "mono", 4) == 0) {
        if (void *fn = SMMonoLookup(name)) return fn;
    }

    void *sym = dlsym(RTLD_DEFAULT, name);
    if (!sym) {
        void *mainHandle = dlopen(nullptr, RTLD_LAZY);
        if (mainHandle) sym = dlsym(mainHandle, name);
    }
    return sym;
}

extern "C" void *xdl_dsym(void *handle, const char *name, size_t *size) {
    return xdl_sym(handle, name, size);
}

extern "C" int xdl_close(void *handle) {
    (void)handle;
    return 0;
}

extern "C" int xdl_info(void *handle, int request, void *info) {
    if (request != XDL_DI_DLINFO || !info) return -1;
    xdl_info_t *out = (xdl_info_t *)info;
    memset(out, 0, sizeof(*out));

    XdlHandle *h = (XdlHandle *)handle;
    if (!h || !h->image) {
        out->dli_fbase = (void *)_dyld_get_image_header(0);
        out->dli_fname = _dyld_image_count() > 0 ? _dyld_get_image_name(0) : "";
        return 0;
    }
    out->dli_fbase = h->image;
    out->dli_fname = h->name;
    return 0;
}
