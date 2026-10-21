#pragma once
// ============================================================================
//  Utils.h — iOS 版镜像/地址工具
// ----------------------------------------------------------------------------
//  Android 版靠 /proc/self/maps 找 libil2cpp.so 的基址；iOS 没有 /proc，
//  改用 dyld 镜像表（_dyld_image_count / _dyld_get_image_header）。
//
//  - 游戏本体在 iOS 上是主可执行文件（或 UnityFramework.framework）。
//  - targetLibName 默认取 "UnityFramework"；如果是旧式单二进制 Unity，
//    FindImageBase 会回落到主可执行文件。
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

#include "Includes/Logger.h"
#include "Includes/obfuscate.h"

#ifndef targetLibName
#define targetLibName "UnityFramework"
#endif

namespace SMImage {

// 返回包含 needle 的镜像头；needle 为空时返回主可执行文件。
inline const struct mach_header_64 *FindHeader(const char *needle) {
    const uint32_t count = _dyld_image_count();
    if (needle && *needle) {
        for (uint32_t i = 0; i < count; ++i) {
            const char *name = _dyld_get_image_name(i);
            if (!name) continue;
            if (strstr(name, needle)) {
                return (const struct mach_header_64 *)_dyld_get_image_header(i);
            }
        }
        // "libil2cpp.so" / "libmonosgen-2.0.so" 这类 Android 名字在 iOS 上不存在：
        // 回落到主二进制（Unity 的 il2cpp 代码通常静态链进主程序或 UnityFramework）。
    }
    if (count > 0) return (const struct mach_header_64 *)_dyld_get_image_header(0);
    return nullptr;
}

inline uintptr_t FindImageBase(const char *needle) {
    return (uintptr_t)FindHeader(needle);
}

// 镜像的 __TEXT 段大小（用于范围判断，可选）。
inline size_t ImageTextSize(const struct mach_header_64 *hdr) {
    if (!hdr) return 0;
    const uint8_t *p = (const uint8_t *)(hdr + 1);
    for (uint32_t i = 0; i < hdr->ncmds; ++i) {
        const struct load_command *lc = (const struct load_command *)p;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            if (strcmp(seg->segname, "__TEXT") == 0) return (size_t)seg->vmsize;
        }
        p += lc->cmdsize;
    }
    return 0;
}

inline bool ImageLoaded(const char *name) {
    return FindHeader(name) != nullptr;
}

}  // namespace SMImage

// Android 版签名兼容：findLibrary / getAbsoluteAddress / isLibraryLoaded
inline uintptr_t findLibrary(const char *library) {
    return SMImage::FindImageBase(library);
}

inline uintptr_t getAbsoluteAddress(const char *libraryName, uintptr_t relativeAddr) {
    uintptr_t base = SMImage::FindImageBase(libraryName);
    if (base == 0) return 0;
    return base + relativeAddr;
}

inline bool isLibraryLoaded(const char *libraryName) {
    return SMImage::ImageLoaded(libraryName);
}

inline uintptr_t string2Offset(const char *c) {
    return (uintptr_t)strtoull(c, nullptr, 16);
}

// Android 的 isGameLibLoaded 走 JNI 返回 jboolean；iOS 直接在 C++ 里问。
extern bool libLoaded;
inline bool isGameLibLoaded() { return libLoaded; }

namespace ToastLength {
inline const int LENGTH_LONG = 1;
inline const int LENGTH_SHORT = 0;
}
