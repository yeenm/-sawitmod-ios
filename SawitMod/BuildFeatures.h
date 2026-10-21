#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <dlfcn.h>
#include "Global.h"
#include "ModUtils.h"
#include "Logger.h"
#include "AutoPumpkinSculpture.h"
#include "Includes/Dobby/dobby.h"

// =========================================================================
// BUILD FEATURES MODULE (ANDROID ARM64 NON-BLOCKING & HARDENED)
// 1. No surface limit (_IsAlignmentPosInArea detour)
// 2. No range/height limit (OutOfBoundsTesting.Test detour)
// 3. Bypass Overlap (IntersectionTesting.Test + OverlapCompleteWithSlab detour)
// 4. Free place & rotate for all items (IsFreePlaceRotateBlackListed detour + blacklist clear)
// =========================================================================

// -------------------------------------------------------------------------
// 1. No Surface Limit Hook: Alignment._IsAlignmentPosInArea (3 parameters)
// In AAPCS64 (ARM64 ABI), Vector3 is an HFA passed in floating-point registers s0-s2.
// -------------------------------------------------------------------------
typedef uint8_t (*t_IsAlignmentPosInArea)(void* self, Vector3 worldPos, void* quatRef, void* placeBox);
inline t_IsAlignmentPosInArea g_origIsAlignmentPosInArea = nullptr;

inline uint8_t Hook_IsAlignmentPosInArea(void* self, Vector3 worldPos, void* quatRef, void* placeBox) {
    if (g_noSurfaceLimit) {
        return 1;
    }
    if (g_origIsAlignmentPosInArea) {
        return g_origIsAlignmentPosInArea(self, worldPos, quatRef, placeBox);
    }
    return 1;
}

// -------------------------------------------------------------------------
// 2. No Range/Height Limit Hook: OutOfBoundsTesting.Test (1 parameter)
// -------------------------------------------------------------------------
typedef int (*t_OutOfBoundsTest)(void* ctx);
inline t_OutOfBoundsTest g_origOutOfBoundsTest = nullptr;

inline int Hook_OutOfBoundsTest(void* ctx) {
    if (g_noRangeHeightLimit) {
        return 0; // 0 = ErrorCode.Success (no boundary violation)
    }
    if (g_origOutOfBoundsTest) {
        return g_origOutOfBoundsTest(ctx);
    }
    return 0;
}

// -------------------------------------------------------------------------
// 3. Bypass Overlap Hooks:
//    a) IntersectionTesting.Test (3 parameters)
//    b) BuildSingle.OverlapCompleteWithSlab (1 parameter)
// -------------------------------------------------------------------------
typedef int (*t_IntersectionTest)(void* ctx, void* collectElements, void* collectColliders);
inline t_IntersectionTest g_origIntersectionTest = nullptr;

inline int Hook_IntersectionTest(void* ctx, void* collectElements, void* collectColliders) {
    if (g_bypassOverlap) {
        return 0; // 0 = ErrorCode.Success (no intersection)
    }
    if (g_origIntersectionTest) {
        return g_origIntersectionTest(ctx, collectElements, collectColliders);
    }
    return 0;
}

typedef uint8_t (*t_OverlapCompleteWithSlab)(void* self, void** outOverlapElement);
inline t_OverlapCompleteWithSlab g_origOverlapCompleteWithSlab = nullptr;

inline uint8_t Hook_OverlapCompleteWithSlab(void* self, void** outOverlapElement) {
    if (g_bypassOverlap) {
        if (outOverlapElement) {
            *outOverlapElement = nullptr;
        }
        return 1; // 1 = allowed
    }
    if (g_origOverlapCompleteWithSlab) {
        return g_origOverlapCompleteWithSlab(self, outOverlapElement);
    }
    return 0;
}

// -------------------------------------------------------------------------
// 4. Free Place & Rotate Hook: BuildModule.IsFreePlaceRotateBlackListed(int32)
// -------------------------------------------------------------------------
typedef uint8_t (*t_IsFreePlaceRotateBlackListed)(void* self, int32_t staticId);
inline t_IsFreePlaceRotateBlackListed g_origIsFreePlaceRotateBlackListed = nullptr;

inline uint8_t Hook_IsFreePlaceRotateBlackListed(void* self, int32_t staticId) {
    if (g_freePlaceRotate) {
        return 0; // 0 = false (not blacklisted)
    }
    if (g_origIsFreePlaceRotateBlackListed) {
        return g_origIsFreePlaceRotateBlackListed(self, staticId);
    }
    return 0;
}

// -------------------------------------------------------------------------
// Helper: FindClassInBuildImages
// -------------------------------------------------------------------------
inline void* FindClassInBuildImages(const char* nameSpace, const char* className) {
    const char* imgNames[] = {
        "XDTLevelAndEntity", "XDTLevelAndEntity.dll",
        "XDTDataAndProtocol", "XDTDataAndProtocol.dll",
        "XDTGameSystem", "XDTGameSystem.dll",
        "XDTGameUI", "XDTGameUI.dll",
        "XDTBaseService", "XDTBaseService.dll",
        "Client", "Client.dll",
        "EcsClient", "EcsClient.dll",
        "Assembly-CSharp", "Assembly-CSharp.dll"
    };
    for (const char* iname : imgNames) {
        void* img = _monoImageLoaded(iname);
        if (!img) continue;
        void* klass = _monoClassFromName(img, nameSpace, className);
        if (klass) return klass;
    }
    return nullptr;
}

// -------------------------------------------------------------------------
// EnsureBuildHooksInstalled (NON-BLOCKING via RunOnUnityThreadAsync)
// Never blocks RenderThread, avoiding OpenGL/UnityMain swapBuffers deadlocks.
// -------------------------------------------------------------------------
inline std::atomic<bool> g_buildHooksInstalled{ false };
inline std::atomic<bool> g_buildHookingInProgress{ false };

inline void EnsureBuildHooksInstalled() {
    if (g_buildHooksInstalled.load(std::memory_order_acquire)) return;
    if (g_buildHookingInProgress.exchange(true)) return;

    RunOnUnityThreadAsync([]() {
        void* rootDomain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
        if (!rootDomain) {
            g_buildHookingInProgress.store(false);
            return;
        }
        if (_monoThreadAttach) {
            _monoThreadAttach(rootDomain);
        }

        // STEP 1: Find all target classes
        void* klassAlignment = FindClassInBuildImages("XDTLevelAndEntity.Core.Craft", "Alignment");
        void* klassOOB = FindClassInBuildImages("XDTLevelAndEntity.Core.Craft", "OutOfBoundsTesting");
        void* klassIntersection = FindClassInBuildImages("XDTLevelAndEntity.Core.Craft", "IntersectionTesting");
        void* klassBuildSingle = FindClassInBuildImages("XDTLevelAndEntity.Core.Craft", "BuildSingle");
        void* klassBuildModule = FindClassInBuildImages("XDTGUI.Module.Build", "BuildModule");
        if (!klassBuildModule) klassBuildModule = FindClassInBuildImages("XDTLevelAndEntity", "BuildModule");

        // STEP 2: Find all methods
        void* m_alignment = klassAlignment ? FindMethodInHierarchy(klassAlignment, "_IsAlignmentPosInArea", 3) : nullptr;
        void* m_oob = klassOOB ? FindMethodInHierarchy(klassOOB, "Test", 1) : nullptr;
        void* m_intersection = klassIntersection ? FindMethodInHierarchy(klassIntersection, "Test", 3) : nullptr;
        void* m_slab = klassBuildSingle ? FindMethodInHierarchy(klassBuildSingle, "OverlapCompleteWithSlab", 1) : nullptr;
        void* m_freePlace = klassBuildModule ? FindMethodInHierarchy(klassBuildModule, "IsFreePlaceRotateBlackListed", 1) : nullptr;

        if (!_monoCompileMethod) {
            g_buildHookingInProgress.store(false);
            return;
        }

        // STEP 3: JIT-compile ALL methods FIRST while the Mono JIT code heap pages are in their normal state!
        // This is critical: if Dobby hooks a method first, it sets the memory page to RX (read-only for writes).
        // If another method is compiled afterwards onto the same JIT memory page, Mono's memcpy fails with SEGV_ACCERR.
        void* code_alignment = m_alignment ? _monoCompileMethod(m_alignment) : nullptr;
        void* code_oob = m_oob ? _monoCompileMethod(m_oob) : nullptr;
        void* code_intersection = m_intersection ? _monoCompileMethod(m_intersection) : nullptr;
        void* code_slab = m_slab ? _monoCompileMethod(m_slab) : nullptr;
        void* code_freePlace = m_freePlace ? _monoCompileMethod(m_freePlace) : nullptr;

        int hooksCreated = 0;

        // STEP 4: Hook each method and IMMEDIATELY restore PROT_READ | PROT_WRITE | PROT_EXEC via VirtualProtect.
        // Dobby leaves hooked pages as PROT_READ | PROT_EXEC. On Mono JIT, any subsequent write/thunk/JIT emit
        // on that page crashes with SEGV_ACCERR in memcpy unless VirtualProtect restores write access!
        if (code_alignment && !g_origIsAlignmentPosInArea) {
            if (DobbyHook(code_alignment, (void*)&Hook_IsAlignmentPosInArea, (void**)&g_origIsAlignmentPosInArea) == RS_SUCCESS) {
                VirtualProtect(code_alignment, 0x4000);
                hooksCreated++;
                LOGI("[Build] Hooked Alignment._IsAlignmentPosInArea @ %p", code_alignment);
            }
        }

        if (code_oob && !g_origOutOfBoundsTest) {
            if (DobbyHook(code_oob, (void*)&Hook_OutOfBoundsTest, (void**)&g_origOutOfBoundsTest) == RS_SUCCESS) {
                VirtualProtect(code_oob, 0x4000);
                hooksCreated++;
                LOGI("[Build] Hooked OutOfBoundsTesting.Test @ %p", code_oob);
            }
        }

        if (code_intersection && !g_origIntersectionTest) {
            if (DobbyHook(code_intersection, (void*)&Hook_IntersectionTest, (void**)&g_origIntersectionTest) == RS_SUCCESS) {
                VirtualProtect(code_intersection, 0x4000);
                hooksCreated++;
                LOGI("[Build] Hooked IntersectionTesting.Test @ %p", code_intersection);
            }
        }

        if (code_slab && !g_origOverlapCompleteWithSlab) {
            if (DobbyHook(code_slab, (void*)&Hook_OverlapCompleteWithSlab, (void**)&g_origOverlapCompleteWithSlab) == RS_SUCCESS) {
                VirtualProtect(code_slab, 0x4000);
                hooksCreated++;
                LOGI("[Build] Hooked BuildSingle.OverlapCompleteWithSlab @ %p", code_slab);
            }
        }

        if (code_freePlace && !g_origIsFreePlaceRotateBlackListed) {
            if (DobbyHook(code_freePlace, (void*)&Hook_IsFreePlaceRotateBlackListed, (void**)&g_origIsFreePlaceRotateBlackListed) == RS_SUCCESS) {
                VirtualProtect(code_freePlace, 0x4000);
                hooksCreated++;
                LOGI("[Build] Hooked BuildModule.IsFreePlaceRotateBlackListed @ %p", code_freePlace);
            }
        }

        if (hooksCreated > 0) {
            g_buildHooksInstalled.store(true, std::memory_order_release);
        }
        g_buildHookingInProgress.store(false);
    });
}

// -------------------------------------------------------------------------
// 4. Free Place & Rotate for All Items (Module Instance + HashSets)
// -------------------------------------------------------------------------
inline void* GetBuildModuleInstance() {
    void* rootDomain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!rootDomain) return nullptr;

    void* buildModClass = FindClassInBuildImages("XDTGUI.Module.Build", "BuildModule");
    if (!buildModClass) return nullptr;

    void* frameworkImage = _monoImageLoaded(OBFUSCATE("XDTBaseService"));
    if (!frameworkImage) frameworkImage = _monoImageLoaded(OBFUSCATE("XDTGame.Framework"));
    void* managersClass = frameworkImage ? _monoClassFromName(
            frameworkImage, OBFUSCATE("XDTGame.Framework"), OBFUSCATE("Managers")) : nullptr;
    if (!managersClass) managersClass = FindClassInBuildImages("XDTGame.Framework", "Managers");
    if (!managersClass) managersClass = FindClassInBuildImages("XDTLevelAndEntity", "Managers");
    if (!managersClass) return nullptr;

    void* m_getMod = FindMethodInHierarchy(managersClass, "GetModule", 1);
    void* sysType = _monoClassGetType ? _monoClassGetType(buildModClass) : nullptr;
    void* sysTypeObj = (sysType && _monoTypeGetObject) ? _monoTypeGetObject(rootDomain, sysType) : nullptr;
    if (!m_getMod || !sysTypeObj) return nullptr;

    void* args[] = { sysTypeObj };
    void* exc = nullptr;
    void* modInst = _monoRuntimeInvoke(m_getMod, nullptr, args, &exc);
    if (exc || !modInst) return nullptr;
    return modInst;
}

inline void ApplyFreePlaceRotateUnlock(bool enable) {
    void* rootDomain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!rootDomain) return;
    if (_monoThreadAttach) _monoThreadAttach(rootDomain);

    void* modInst = GetBuildModuleInstance();
    if (!modInst) return;

    void* modClass = SafeMonoObjectGetClass(modInst);
    if (!modClass) return;

    void* m_init = FindMethodInHierarchy(modClass, "InitFreePlaceRotateBlackList", 0);

    if (enable) {
        // Ensure blacklist is initialized
        if (m_init) {
            void* exc = nullptr;
            _monoRuntimeInvoke(m_init, modInst, nullptr, &exc);
        }

        // Clear _freePlaceRotateStaticIdBlackList
        void* f_static = FindFieldInHierarchy(modClass, "_freePlaceRotateStaticIdBlackList");
        if (f_static) {
            void* staticList = nullptr;
            _monoFieldGetValue(modInst, f_static, &staticList);
            if (staticList) {
                void* kl = SafeMonoObjectGetClass(staticList);
                void* m_clear = kl ? FindMethodInHierarchy(kl, "Clear", 0) : nullptr;
                if (m_clear) {
                    void* exc = nullptr;
                    _monoRuntimeInvoke(m_clear, staticList, nullptr, &exc);
                }
            }
        }

        // Clear _freePlaceRotateEntityTypeBlackList
        void* f_entity = FindFieldInHierarchy(modClass, "_freePlaceRotateEntityTypeBlackList");
        if (f_entity) {
            void* entityList = nullptr;
            _monoFieldGetValue(modInst, f_entity, &entityList);
            if (entityList) {
                void* kl = SafeMonoObjectGetClass(entityList);
                void* m_clear = kl ? FindMethodInHierarchy(kl, "Clear", 0) : nullptr;
                if (m_clear) {
                    void* exc = nullptr;
                    _monoRuntimeInvoke(m_clear, entityList, nullptr, &exc);
                }
            }
        }
    } else {
        // Restore default game blacklist
        if (m_init) {
            void* exc = nullptr;
            _monoRuntimeInvoke(m_init, modInst, nullptr, &exc);
        }
    }
}

inline std::atomic<bool> g_freePlaceWorkerRunning{ false };

inline void SetFreePlaceRotateUnlock(bool enabled) {
    g_freePlaceRotate = enabled;
    EnsureBuildHooksInstalled();
    if (enabled) {
        if (!g_freePlaceWorkerRunning.exchange(true)) {
            std::thread([]() {
                while (g_freePlaceRotate) {
                    RunOnUnityThreadAsync([]() {
                        ApplyFreePlaceRotateUnlock(true);
                    });
                    for (int i = 0; i < 15 && g_freePlaceRotate; ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
                // Toggled off: restore default blacklist
                RunOnUnityThreadAsync([]() {
                    ApplyFreePlaceRotateUnlock(false);
                });
                g_freePlaceWorkerRunning.store(false);
            }).detach();
        }
    } else {
        RunOnUnityThreadAsync([]() {
            ApplyFreePlaceRotateUnlock(false);
        });
    }
}

namespace ModFeatures {
    using ::EnsureBuildHooksInstalled;
    using ::SetFreePlaceRotateUnlock;
    using ::ApplyFreePlaceRotateUnlock;
}
