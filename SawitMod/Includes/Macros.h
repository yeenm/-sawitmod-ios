#pragma once
// ============================================================================
//  Macros.h — HOOK / PATCH 宏（iOS 版）
// ----------------------------------------------------------------------------
//  Android 版区分 arm64（And64InlineHook / A64HookFunction）与 armv7
//  （CydiaSubstrate）。iOS 上目标只有 arm64，统一走 Dobby——A64HookFunction
//  就是把 Android 的 AArch64 inline hook 包了一层，语义相同。
//
//  地址解析：Android 是 base + RVA（基址取自 /proc/self/maps），iOS 由
//  SMImage::FindImageBase 走 dyld，RVA 语义不变。
// ============================================================================

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "Includes/Dobby/dobby.h"
#include "Includes/Logger.h"
#include "Includes/Utils.h"
#include "KittyMemory/MemoryPatch.h"

inline void hook(void *offset, void *ptr, void **orig) {
    if (!offset) {
        LOGE("hook: null target");
        return;
    }
    DobbyHook(offset, (dobby_dummy_func_t)ptr, (dobby_dummy_func_t *)orig);
}

#define HOOK(offset, ptr, orig) \
    hook((void *)getAbsoluteAddress(targetLibName, string2Offset(offset)), (void *)ptr, (void **)&orig)
#define HOOK_LIB(lib, offset, ptr, orig) \
    hook((void *)getAbsoluteAddress(lib, string2Offset(offset)), (void *)ptr, (void **)&orig)

#define HOOK_NO_ORIG(offset, ptr) \
    hook((void *)getAbsoluteAddress(targetLibName, string2Offset(offset)), (void *)ptr, NULL)
#define HOOK_LIB_NO_ORIG(lib, offset, ptr) \
    hook((void *)getAbsoluteAddress(lib, string2Offset(offset)), (void *)ptr, NULL)

// 符号方式：iOS 的 Unity 导出 il2cpp_* 与游戏 C++ 符号，dlsym(RTLD_DEFAULT) 即可。
#define HOOKSYM(sym, ptr, org) hook(dlsym(RTLD_DEFAULT, sym), (void *)ptr, (void **)&org)
#define HOOKSYM_LIB(lib, sym, ptr, org) hook(dlsym(RTLD_DEFAULT, sym), (void *)ptr, (void **)&org)

#define HOOKSYM_NO_ORIG(sym, ptr) hook(dlsym(RTLD_DEFAULT, sym), (void *)ptr, NULL)
#define HOOKSYM_LIB_NO_ORIG(lib, sym, ptr) hook(dlsym(RTLD_DEFAULT, sym), (void *)ptr, NULL)

inline std::vector<MemoryPatch> &SMMemoryPatches() {
    static std::vector<MemoryPatch> patches;
    return patches;
}
inline std::vector<uint64_t> &SMPatchOffsets() {
    static std::vector<uint64_t> offsets;
    return offsets;
}

// Patching a offset without switch.
inline void patchOffset(const char *fileName, uint64_t offset, std::string hexBytes, bool isOn) {
    MemoryPatch patch = MemoryPatch::createWithHex(fileName, offset, hexBytes);
    auto &memoryPatches = SMMemoryPatches();
    auto &offsetVector = SMPatchOffsets();

    if (std::find(offsetVector.begin(), offsetVector.end(), offset) != offsetVector.end()) {
        auto itr = std::find(offsetVector.begin(), offsetVector.end(), offset);
        patch = memoryPatches[std::distance(offsetVector.begin(), itr)];
    } else {
        memoryPatches.push_back(patch);
        offsetVector.push_back(offset);
    }

    if (!patch.isValid()) {
        LOGE("Failing offset: 0x%llu, please re-check the hex", (unsigned long long)offset);
        return;
    }
    if (isOn) {
        if (!patch.Modify()) LOGE("patch failed at 0x%llx", (unsigned long long)offset);
    } else {
        if (!patch.Restore()) LOGE("restore failed at 0x%llx", (unsigned long long)offset);
    }
}

inline void patchOffsetSym(uintptr_t absolute_address, std::string hexBytes, bool isOn) {
    MemoryPatch patch = MemoryPatch::createWithHex(absolute_address, hexBytes);
    auto &memoryPatches = SMMemoryPatches();
    auto &offsetVector = SMPatchOffsets();

    if (std::find(offsetVector.begin(), offsetVector.end(), absolute_address) != offsetVector.end()) {
        auto itr = std::find(offsetVector.begin(), offsetVector.end(), absolute_address);
        patch = memoryPatches[std::distance(offsetVector.begin(), itr)];
    } else {
        memoryPatches.push_back(patch);
        offsetVector.push_back(absolute_address);
    }

    if (!patch.isValid()) {
        LOGE("Failing absolute: 0x%llx", (unsigned long long)absolute_address);
        return;
    }
    if (isOn) {
        if (!patch.Modify()) LOGE("patch failed at 0x%llx", (unsigned long long)absolute_address);
    } else {
        if (!patch.Restore()) LOGE("restore failed at 0x%llx", (unsigned long long)absolute_address);
    }
}

#define PATCH(offset, hex) patchOffset(targetLibName, string2Offset(offset), hex, true)
#define PATCH_LIB(lib, offset, hex) patchOffset(lib, string2Offset(offset), hex, true)

#define PATCH_SYM(sym, hex) patchOffsetSym((uintptr_t)dlsym(RTLD_DEFAULT, sym), hex, true)
#define PATCH_LIB_SYM(lib, sym, hex) patchOffsetSym((uintptr_t)dlsym(RTLD_DEFAULT, sym), hex, true)

#define PATCH_SWITCH(offset, hex, boolean) patchOffset(targetLibName, string2Offset(offset), hex, boolean)
#define PATCH_LIB_SWITCH(lib, offset, hex, boolean) patchOffset(lib, string2Offset(offset), hex, boolean)

#define PATCH_SYM_SWITCH(sym, hex, boolean) \
    patchOffsetSym((uintptr_t)dlsym(RTLD_DEFAULT, sym), hex, boolean)
#define PATCH_LIB_SYM_SWITCH(lib, sym, hex, boolean) \
    patchOffsetSym((uintptr_t)dlsym(RTLD_DEFAULT, sym), hex, boolean)

#define RESTORE(offset) patchOffset(targetLibName, string2Offset(offset), "", false)
#define RESTORE_LIB(lib, offset) patchOffset(lib, string2Offset(offset), "", false)

#define RESTORE_SYM(sym) patchOffsetSym((uintptr_t)dlsym(RTLD_DEFAULT, sym), "", false)
#define RESTORE_LIB_SYM(lib, sym) patchOffsetSym((uintptr_t)dlsym(RTLD_DEFAULT, sym), "", false)
