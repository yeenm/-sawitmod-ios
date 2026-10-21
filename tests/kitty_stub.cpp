// ============================================================================
//  kitty_stub.cpp — 链接自检用的 KittyMemory 实现（仅 Windows）
// ----------------------------------------------------------------------------
//  真实的 ios/SawitMod/KittyMemory/KittyMemory.cpp 依赖 Mach vm_* API；这里给
//  出同签名的空实现，让 MemoryPatch.cpp / KittyUtils.cpp 也能参与链接检查。
// ============================================================================

#include <cstring>
#include <string>

#include "KittyMemory/KittyMemory.h"

namespace KittyMemory {

bool ProtectAddr(void *, size_t, int) { return false; }

Memory_Status memWrite(void *addr, const void *buffer, size_t len) {
    if (!addr) return INV_ADDR;
    if (!buffer) return INV_BUF;
    if (len == 0) return INV_LEN;
    memcpy(addr, buffer, len);
    return SUCCESS;
}

Memory_Status memRead(void *buffer, const void *addr, size_t len) {
    if (!addr) return INV_ADDR;
    if (!buffer) return INV_BUF;
    if (len == 0) return INV_LEN;
    memcpy(buffer, addr, len);
    return SUCCESS;
}

std::string read2HexStr(const void *, size_t) { return std::string(); }

ProcMap getLibraryMap(const char *) { return ProcMap(); }

uintptr_t getAbsoluteAddress(const char *, uintptr_t relativeAddr, bool) {
    return relativeAddr;
}

}  // namespace KittyMemory
