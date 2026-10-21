// ============================================================================
//  Tools.cpp — iOS 版读写/Hook/特征码工具
// ============================================================================

#include "ByNameModding/Tools.h"

#include <cstring>
#include <mach-o/dyld.h>
#include <mach/vm_map.h>
#include <mach/mach.h>
#include <random>

#include "Includes/Utils.h"

#if !defined(__clang__) && !defined(__GNUC__)
// 仅本地语法自检（MSVC）需要：iOS 上这些都由系统头提供。
#ifndef __builtin___clear_cache
#define __builtin___clear_cache(begin, end) ((void)0)
#endif
typedef unsigned int mach_msg_type_number_t;
#endif

namespace {

size_t PageStart(uintptr_t addr) {
    static const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    return addr & ~(page - 1);
}

bool SetProt(uintptr_t addr, size_t len, vm_prot_t prot) {
    const uintptr_t start = PageStart(addr);
    const uintptr_t end = PageStart(addr + len - 1) + (size_t)sysconf(_SC_PAGESIZE);
    kern_return_t kr = vm_protect(mach_task_self(), (vm_address_t)start,
                                  (vm_size_t)(end - start), false, prot);
    return kr == KERN_SUCCESS;
}

bool CurrentProt(uintptr_t addr, vm_prot_t *prot) {
    vm_address_t region = (vm_address_t)PageStart(addr);
    vm_size_t regionSize = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t kr = vm_region_64(mach_task_self(), &region, &regionSize,
                                    VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info,
                                    &count, &object);
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
    if (kr != KERN_SUCCESS) return false;
    *prot = info.protection;
    return true;
}

}  // namespace

namespace Tools {

void Hook(void *target, void *replace, void **backup) {
    if (!target || !replace) return;
    DobbyHook(target, replace, backup);
}

bool Read(void *addr, void *buffer, size_t length) {
    if (!addr || !buffer || !length) return false;
    memcpy(buffer, addr, length);
    return true;
}

bool Write(void *addr, void *buffer, size_t length) {
    if (!addr || !buffer || !length) return false;
    vm_prot_t prot = 0;
    if (!CurrentProt((uintptr_t)addr, &prot)) return false;
    const vm_prot_t want = prot | VM_PROT_WRITE | VM_PROT_READ;
    if (!SetProt((uintptr_t)addr, length, want)) return false;
    memcpy(addr, buffer, length);
    // 代码页要同步指令缓存，否则 arm64 上可能执行到旧指令。
    if (prot & VM_PROT_EXECUTE) {
        __builtin___clear_cache((char *)addr, (char *)addr + length);
    }
    SetProt((uintptr_t)addr, length, prot);
    return true;
}

bool ReadAddr(void *addr, void *buffer, size_t length) {
    if (!IsPtrValid(addr)) return false;
    return Read(addr, buffer, length);
}

bool WriteAddr(void *addr, void *buffer, size_t length) {
    if (!IsPtrValid(addr)) return false;
    return Write(addr, buffer, length);
}

bool PVM_ReadAddr(void *addr, void *buffer, size_t length) { return ReadAddr(addr, buffer, length); }
bool PVM_WriteAddr(void *addr, void *buffer, size_t length) { return WriteAddr(addr, buffer, length); }

bool IsPtrValid(void *addr) {
    if (!addr) return false;
    uintptr_t p = (uintptr_t)addr;
    return p > 0x100000000ULL && p < 0x00007FFFFFFFFFFFULL;
}

uintptr_t GetBaseAddress(const char *name) { return SMImage::FindImageBase(name); }

uintptr_t GetEndAddress(const char *name) {
    const mach_header_64 *hdr = SMImage::FindHeader(name);
    if (!hdr) return 0;
    return (uintptr_t)hdr + SMImage::ImageTextSize(hdr);
}

uintptr_t FindPattern(const char *lib, const char *pattern) {
    uintptr_t base = GetBaseAddress(lib);
    uintptr_t end = GetEndAddress(lib);
    if (!base || end <= base) return 0;

    // 特征码格式： "AA BB ?? CC"，空格分隔，?? 为通配。
    uint8_t bytes[64];
    uint8_t mask[64];
    size_t count = 0;
    const char *p = pattern;
    while (*p && count < sizeof(bytes)) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (p[0] == '?' && p[1] == '?') {
            bytes[count] = 0;
            mask[count] = 0;
            count++;
            p += 2;
            continue;
        }
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = hex(p[0]);
        int lo = p[1] ? hex(p[1]) : -1;
        if (hi < 0 || lo < 0) break;
        bytes[count] = (uint8_t)((hi << 4) | lo);
        mask[count] = 0xFF;
        count++;
        p += 2;
    }
    if (count == 0) return 0;

    const uint8_t *start = (const uint8_t *)base;
    const size_t total = (size_t)(end - base);
    for (size_t i = 0; i + count <= total; ++i) {
        bool match = true;
        for (size_t j = 0; j < count; ++j) {
            if (mask[j] && start[i + j] != bytes[j]) {
                match = false;
                break;
            }
        }
        if (match) return base + i;
    }
    return 0;
}

std::string RandomString(const int len) {
    static const char alpha[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::mt19937 gen((uint32_t)time(nullptr) ^ (uint32_t)(uintptr_t)&alpha);
    std::uniform_int_distribution<> dist(0, (int)sizeof(alpha) - 2);
    std::string out;
    out.reserve((size_t)len);
    for (int i = 0; i < len; ++i) out.push_back(alpha[dist(gen)]);
    return out;
}

}  // namespace Tools
