//
//  KittyMemory.h — iOS 版（API 与 MJx0/KittyMemory 保持一致）
//
//  Android 版靠 /proc/self/maps 拿模块范围、mprotect 改页权限；iOS 没有
//  /proc，对应实现：
//    * 模块范围 -> dyld 镜像表（_dyld_image_count / _dyld_get_image_header）
//    * 改页权限 -> mach_vm_protect / vm_protect
//    * 代码页写完后 __builtin___clear_cache 同步指令缓存
//

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/vm_map.h>

#define _SYS_PAGE_SIZE_ (sysconf(_SC_PAGE_SIZE))

#define _PAGE_START_OF_(x) ((uintptr_t)x & ~(uintptr_t)(_SYS_PAGE_SIZE_ - 1))
#define _PAGE_END_OF_(x, len) (_PAGE_START_OF_((uintptr_t)x + (len) - 1))
#define _PAGE_LEN_OF_(x, len) (_PAGE_END_OF_(x, len) - _PAGE_START_OF_(x) + _SYS_PAGE_SIZE_)
#define _PAGE_OFFSET_OF_(x) ((uintptr_t)x - _PAGE_START_OF_(x))

#define _PROT_RWX_ (PROT_READ | PROT_WRITE | PROT_EXEC)
#define _PROT_RX_ (PROT_READ | PROT_EXEC)

#define EMPTY_VEC_OFFSET std::vector<int>()

namespace KittyMemory {

typedef enum {
    FAILED = 0,
    SUCCESS = 1,
    INV_ADDR = 2,
    INV_LEN = 3,
    INV_BUF = 4,
    INV_PROT = 5
} Memory_Status;

struct ProcMap {
    void *startAddr;
    void *endAddr;
    size_t length;
    std::string perms;
    long offset;
    std::string dev;
    int inode;
    std::string pathname;

    bool isValid() { return (startAddr != NULL && endAddr != NULL && !pathname.empty()); }
};

// 改一段内存的保护属性
bool ProtectAddr(void *addr, size_t length, int protection);

// 写内存（自动临时放开写权限，写完恢复；代码页会刷 icache）
Memory_Status memWrite(void *addr, const void *buffer, size_t len);

// 读内存
Memory_Status memRead(void *buffer, const void *addr, size_t len);

// 读成 hex 字符串
std::string read2HexStr(const void *addr, size_t len);

template <typename Type>
Type readMultiPtr(void *ptr, std::vector<int> offsets) {
    Type defaultVal = {};
    if (ptr == NULL) return defaultVal;
    uintptr_t finalPtr = reinterpret_cast<uintptr_t>(ptr);
    int offsetsSize = offsets.size();
    if (offsetsSize > 0) {
        for (int i = 0; finalPtr != 0 && i < offsetsSize; i++) {
            if (i == (offsetsSize - 1)) return *reinterpret_cast<Type *>(finalPtr + offsets[i]);
            finalPtr = *reinterpret_cast<uintptr_t *>(finalPtr + offsets[i]);
        }
    }
    if (finalPtr == 0) return defaultVal;
    return *reinterpret_cast<Type *>(finalPtr);
}

template <typename Type>
bool writeMultiPtr(void *ptr, std::vector<int> offsets, Type val) {
    if (ptr == NULL) return false;
    uintptr_t finalPtr = reinterpret_cast<uintptr_t>(ptr);
    int offsetsSize = offsets.size();
    if (offsetsSize > 0) {
        for (int i = 0; finalPtr != 0 && i < offsetsSize; i++) {
            if (i == (offsetsSize - 1)) {
                *reinterpret_cast<Type *>(finalPtr + offsets[i]) = val;
                return true;
            }
            finalPtr = *reinterpret_cast<uintptr_t *>(finalPtr + offsets[i]);
        }
    }
    if (finalPtr == 0) return false;
    *reinterpret_cast<Type *>(finalPtr) = val;
    return true;
}

template <typename Type>
Type readPtr(void *ptr) {
    Type defaultVal = {};
    if (ptr == NULL) return defaultVal;
    return *reinterpret_cast<Type *>(ptr);
}

template <typename Type>
bool writePtr(void *ptr, Type val) {
    if (ptr == NULL) return false;
    *reinterpret_cast<Type *>(ptr) = val;
    return true;
}

// 取已加载模块范围（dyld 镜像，名字做子串匹配）
ProcMap getLibraryMap(const char *libraryName);

// 模块内 RVA -> 绝对地址
uintptr_t getAbsoluteAddress(const char *libraryName, uintptr_t relativeAddr,
                             bool useCache = false);

}  // namespace KittyMemory
