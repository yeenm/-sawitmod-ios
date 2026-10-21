//
//  KittyMemory.cpp — iOS 实现
//
//  与 Android 版的差别只在"怎么拿到模块范围"和"怎么改页权限"，
//  函数签名、返回值、ProcMap 结构完全一致（MemoryPatch 不用改）。
//

#include "KittyMemory.h"

#include <cstring>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>

namespace KittyMemory {

namespace {

size_t PageStart(uintptr_t addr) {
    const size_t page = (size_t)_SYS_PAGE_SIZE_;
    return addr & ~(page - 1);
}

size_t PageEnd(uintptr_t addr) {
    const size_t page = (size_t)_SYS_PAGE_SIZE_;
    return (addr & ~(page - 1)) + page;
}

bool GetProtection(uintptr_t addr, vm_prot_t *prot) {
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

// 找到 img 的 __TEXT 段虚拟长度，用来算 endAddr。
size_t TextVMSize(const struct mach_header_64 *hdr) {
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

size_t ImageVMSize(const struct mach_header_64 *hdr) {
    if (!hdr) return 0;
    size_t text = TextVMSize(hdr);
    const uint8_t *p = (const uint8_t *)(hdr + 1);
    uint64_t maxEnd = 0;
    for (uint32_t i = 0; i < hdr->ncmds; ++i) {
        const struct load_command *lc = (const struct load_command *)p;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            uint64_t end = seg->vmaddr + seg->vmsize;
            if (end > maxEnd) maxEnd = end;
        }
        p += lc->cmdsize;
    }
    if (maxEnd == 0) return text;
    return (size_t)maxEnd;
}

}  // namespace

bool ProtectAddr(void *addr, size_t length, int protection) {
    if (!addr || length == 0) return false;
    const uintptr_t start = PageStart((uintptr_t)addr);
    const uintptr_t end = PageEnd((uintptr_t)addr + length - 1);
    kern_return_t kr = vm_protect(mach_task_self(), (vm_address_t)start,
                                  (vm_size_t)(end - start), false,
                                  (vm_prot_t)protection);
    return kr == KERN_SUCCESS;
}

Memory_Status memWrite(void *addr, const void *buffer, size_t len) {
    if (!addr) return INV_ADDR;
    if (!buffer) return INV_BUF;
    if (len == 0) return INV_LEN;

    vm_prot_t oldProt = 0;
    if (!GetProtection((uintptr_t)addr, &oldProt)) return INV_ADDR;

    if (!ProtectAddr(addr, len, oldProt | VM_PROT_WRITE | VM_PROT_READ)) return INV_PROT;

    memcpy(addr, buffer, len);
    if (oldProt & VM_PROT_EXECUTE) {
        __builtin___clear_cache((char *)addr, (char *)addr + len);
    }

    ProtectAddr(addr, len, oldProt);
    return SUCCESS;
}

Memory_Status memRead(void *buffer, const void *addr, size_t len) {
    if (!addr) return INV_ADDR;
    if (!buffer) return INV_BUF;
    if (len == 0) return INV_LEN;
    memcpy(buffer, addr, len);
    return SUCCESS;
}

std::string read2HexStr(const void *addr, size_t len) {
    std::string out;
    if (!addr || len == 0) return out;
    out.resize(len * 2);
    static const char *hexd = "0123456789ABCDEF";
    const uint8_t *p = (const uint8_t *)addr;
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = hexd[p[i] >> 4];
        out[i * 2 + 1] = hexd[p[i] & 0x0F];
    }
    return out;
}

ProcMap getLibraryMap(const char *libraryName) {
    ProcMap map{};
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const char *name = _dyld_get_image_name(i);
        if (!name) continue;
        bool match = false;
        if (libraryName && *libraryName) {
            match = strstr(name, libraryName) != nullptr;
        } else {
            match = (i == 0);
        }
        if (!match) continue;

        const struct mach_header_64 *hdr = (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (!hdr) continue;

        map.startAddr = (void *)hdr;
        map.length = ImageVMSize(hdr);
        map.endAddr = (void *)((uintptr_t)hdr + (map.length ? map.length : 1));
        map.perms = "rwx";
        map.offset = (long)_dyld_get_image_vmaddr_slide(i);
        map.dev = "";
        map.inode = 0;
        map.pathname = name;
        return map;
    }
    return map;
}

uintptr_t getAbsoluteAddress(const char *libraryName, uintptr_t relativeAddr, bool useCache) {
    (void)useCache;
    ProcMap map = getLibraryMap(libraryName);
    if (!map.isValid()) return 0;
    return (uintptr_t)map.startAddr + relativeAddr;
}

}  // namespace KittyMemory
