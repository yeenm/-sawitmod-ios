#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <chrono>
#include <thread>
#include <dlfcn.h>
#include "Global.h"
#include "ModUtils.h"
#include "NetAuth.h"
#include "obfuscate.h"
#include "Logger.h"
#include "AutoPumpkinSculpture.h"
#include "Includes/Dobby/dobby.h"
#include "Starcoolxdl/xdl.h"
// The RVA table comes in through Global.h (Offsets-cn.h); see the note in AutoPumpkinSculpture.h.

// Forward declaration
uint32_t GetSeedNetId(int staticId);

static inline void SandSleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// =========================================================================
// 100% IDENTICAL PORT OF PC SAWITMOD AUTO SAND SCULPTURE MODULE
// =========================================================================

struct SandRoughResult {
    uint32_t roughNetId = 0;
    std::vector<int> options;
};

struct SandBaseScanResult {
    uint32_t baseNetId = 0;
    int roughStaticId = 0;
    float distance = 999999.0f;
};

struct SandSculpturesComponentDataNative {
    int32_t baseStaticId = 0;
    int32_t roughStaticId = 0;
};

struct SandSculptureRoughComponentDataNative {
    int32_t staticId = 0;
    uint32_t ownerNetId = 0;
    void* options = nullptr;
};

struct SandFinishComponentDataNative {
    uint8_t onNewMake = 0;
};

// Bugtopia Mapping: SandItemToModelStaticId (index 0..14)
static const int s_BugtopiaSandItemToModelStaticId[15] = {
    0, 600100, 600101, 600108, 600113, 600102, 600103, 600104, 600111, 600107,
    600109, 600112, 600105, 600106, 600110
};

// Bugtopia Mapping: SandItemRarity (index 0..14)
static const int s_BugtopiaSandItemRarity[15] = {
    0, 1, 1, 2, 1, 1, 2, 3, 3, 4,
    5, 4, 4, 4, 5
};

// -------------------------------------------------------------------------
// Native Detour for PlayerStateSelectSandRough.EnableEnter(0) -> return 0
// Suppresses manual in-game pop-up selection window cleanly during Auto Sand
// -------------------------------------------------------------------------
typedef uint8_t (*tSandRoughEnableEnter)(void* self);
inline tSandRoughEnableEnter g_origSandRoughEnableEnter = nullptr;
inline bool g_sandRoughHookInstalled = false;

inline uint8_t hkSandRoughEnableEnter(void* self) {
    if (g_isAutoSandActive) {
        return 0; // Suppress dialog completely
    }
    if (g_origSandRoughEnableEnter) {
        return g_origSandRoughEnableEnter(self);
    }
    return 0;
}

inline bool TryInstallSandRoughSelectHook() {
    if (g_sandRoughHookInstalled) return true;

    bool ok = false;
    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
        const char* imgNames[] = { "XDTLevelAndEntity", "XDTLevelAndEntity.dll", "Client", "Client.dll" };
        void* klass = nullptr;
        for (const char* iname : imgNames) {
            void* img = _monoImageLoaded(iname);
            if (!img) continue;
            klass = _monoClassFromName(img, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"), OBFUSCATE("PlayerStateSelectSandRough"));
            if (klass) break;
        }
        if (!klass) return;

        void* m_enter = FindMethodInHierarchy(klass, OBFUSCATE("EnableEnter"), 0);
        if (!m_enter) return;

        if (!_monoCompileMethod) return;
        void* codePtr = _monoCompileMethod(m_enter);
        if (!codePtr) return;

        if (DobbyHook(codePtr, (void*)hkSandRoughEnableEnter, (void**)&g_origSandRoughEnableEnter) == RS_SUCCESS) {
            g_sandRoughHookInstalled = true;
            ok = true;
            LOGI("[Sand] Hooked PlayerStateSelectSandRough.EnableEnter @ %p", codePtr);
        } else {
            LOGI("[Sand] Failed to hook PlayerStateSelectSandRough.EnableEnter @ %p", codePtr);
        }
    });
    return ok;
}

// -------------------------------------------------------------------------
// System Module Resolver (identical to Bugtopia's TryResolveAuraMonoModule)
// -------------------------------------------------------------------------
inline void* TryResolveSystemModule(const char* fullTypeName) {
    std::string fullName = fullTypeName;
    std::string clsName = fullName;
    size_t lastDot = fullName.rfind('.');
    if (lastDot != std::string::npos) {
        clsName = fullName.substr(lastDot + 1);
    }

    // 1. Direct Managers dictionary lookup (fastest & most accurate in Heartopia)
    void* mgrInst = GetManagerInstanceNative(clsName.c_str());
    if (mgrInst) return mgrInst;

    void* rootDomain = _monoGetRootDomain();
    if (!rootDomain) return nullptr;

    std::string nsName = (lastDot != std::string::npos) ? fullName.substr(0, lastDot) : "";

    const char* imgNames[] = {
        "XDTBaseService", "XDTBaseService.dll",
        "XDTGame.Framework", "XDTGame.Framework.dll",
        "XDTGameSystem", "XDTGameSystem.dll",
        "XDTLevelAndEntity", "XDTLevelAndEntity.dll",
        "XDTDataAndProtocol", "XDTDataAndProtocol.dll",
        "Client", "Client.dll",
        "EcsClient", "EcsClient.dll"
    };

    void* klass = nullptr;
    for (const char* iname : imgNames) {
        void* img = _monoImageLoaded(iname);
        if (!img) continue;
        klass = _monoClassFromName(img, nsName.c_str(), clsName.c_str());
        if (klass) break;
    }
    if (!klass) return nullptr;

    const char* instanceMethods[] = { "get_Instance", "get_instance", "Instance", "GetInstance" };
    for (const char* mname : instanceMethods) {
        void* m = FindMethodInHierarchy(klass, mname, 0);
        if (m) {
            void* exc = nullptr;
            void* inst = _monoRuntimeInvoke(m, nullptr, nullptr, &exc);
            if (inst && !exc) return inst;
        }
    }

    const char* instanceFields[] = { "Instance", "instance", "_instance", "_manager", "manager" };
    for (const char* fname : instanceFields) {
        void* f = FindFieldInHierarchy(klass, fname);
        if (f) {
            void* vtable = _monoClassGetVTable ? _monoClassGetVTable(rootDomain, klass) : nullptr;
            if (vtable && _monoFieldStaticGetValue) {
                void* inst = nullptr;
                _monoFieldStaticGetValue(vtable, f, &inst);
                if (inst) return inst;
            }
        }
    }

    // Fallback: Managers.GetModule
    const char* mgrNames[] = { "XDTGame.Framework.Managers", "Managers", "XDTLevelAndEntity.Managers" };
    for (const char* mname : mgrNames) {
        std::string mFullName = mname;
        std::string mNs = "";
        std::string mCls = mFullName;
        size_t mDot = mFullName.rfind('.');
        if (mDot != std::string::npos) {
            mNs = mFullName.substr(0, mDot);
            mCls = mFullName.substr(mDot + 1);
        }
        for (const char* iname : imgNames) {
            void* img = _monoImageLoaded(iname);
            if (!img) continue;
            void* mgrKlass = _monoClassFromName(img, mNs.c_str(), mCls.c_str());
            if (mgrKlass) {
                void* m_getMod = FindMethodInHierarchy(mgrKlass, "GetModule", 1);
                void* sysType = _monoClassGetType ? _monoClassGetType(klass) : nullptr;
                void* sysTypeObj = (sysType && _monoTypeGetObject) ? _monoTypeGetObject(rootDomain, sysType) : nullptr;
                if (m_getMod && sysTypeObj) {
                    void* args[] = { sysTypeObj };
                    void* exc = nullptr;
                    void* mod = _monoRuntimeInvoke(m_getMod, nullptr, args, &exc);
                    if (mod && !exc) return mod;
                }
            }
        }
    }

    return nullptr;
}

// -------------------------------------------------------------------------
// RPC Protocol Wrappers
// -------------------------------------------------------------------------
inline void SandSend1(const char* method, uint32_t netId) {
    void* img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (!img) return;
    void* klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.SandSculpture"), OBFUSCATE("SandSculptureProtocolManager"));
    if (!klass) return;
    void* m = FindMethodInHierarchy(klass, method, 1);
    if (!m) return;
    uint32_t n = netId; void* args[] = { &n }; void* exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
    LOGI("[Sand] %s netId=%u exc=%p", method, netId, exc);
}

inline void SandFinishNative(uint32_t netId, int successCount, int perfectCount) {
    void* img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (!img) return;
    void* klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.SandSculpture"), OBFUSCATE("SandSculptureProtocolManager"));
    if (!klass) return;
    void* m = FindMethodInHierarchy(klass, OBFUSCATE("FinishMakingSandSculpture"), 3);
    if (!m) return;
    uint32_t n = netId; int sc = successCount, pc = perfectCount;
    void* args[] = { &n, &sc, &pc }; void* exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
    LOGI("[Sand] FinishMakingSandSculpture netId=%u success=%d perfect=%d exc=%p", netId, successCount, perfectCount, exc);
}

inline void SandChooseProductNative(uint32_t roughNetId, int productId) {
    void* img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (!img) return;
    void* klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.SandSculpture"), OBFUSCATE("SandSculptureProtocolManager"));
    if (!klass) return;
    void* m = FindMethodInHierarchy(klass, OBFUSCATE("ChooseSandSculptureProduct"), 2);
    if (!m) return;
    uint32_t n = roughNetId; int pid = productId;
    void* args[] = { &n, &pid }; void* exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
    LOGI("[Sand] ChooseSandSculptureProduct roughNetId=%u productId=%d exc=%p", roughNetId, productId, exc);
}

inline void CollectSandSculptureNative(uint32_t netId) {
    if (netId == 0) return;
    void* img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (!img) return;
    void* klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.GamePlay.Character"), OBFUSCATE("CharacterProtocolManager"));
    if (!klass) klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Character"), OBFUSCATE("CharacterProtocolManager"));
    if (!klass) klass = _monoClassFromName(img, "", OBFUSCATE("CharacterProtocolManager"));
    if (!klass) return;

    // Reset _optionType to 0 so PoseDeleteBuild is never blocked
    void* f_opt = FindFieldInHierarchy(klass, "_optionType");
    if (!f_opt) f_opt = FindFieldInHierarchy(klass, "optionType");
    if (f_opt && _monoClassGetVTable && _monoFieldStaticSetValue) {
        int zeroVal = 0;
        void* vtable = _monoClassGetVTable(_monoGetRootDomain(), klass);
        if (vtable) _monoFieldStaticSetValue(vtable, f_opt, &zeroVal);
    }

    void* m = FindMethodInHierarchy(klass, OBFUSCATE("PoseDeleteBuild"), 1);
    if (!m) return;
    uint32_t n = netId; void* args[] = { &n }; void* exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
    LOGI("[Sand] PoseDeleteBuild netId=%u exc=%p", netId, exc);
}

inline void ClearSelfSandRoughDataNative() {
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        void* imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
        if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
        if (!imgLevel) return;

        void* utilKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
        if (!utilKlass) return;
        void* m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
        if (!m_getSelf) return;

        void* exc = nullptr;
        void* selfPlayer = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, &exc);
        if (!selfPlayer || exc) return;

        void* spKlass = SafeMonoObjectGetClass(selfPlayer);
        if (!spKlass) return;
        void* f_dataComp = _monoClassGetFieldFromName(spKlass, OBFUSCATE("_dataComponent"));
        if (!f_dataComp) f_dataComp = _monoClassGetFieldFromName(spKlass, OBFUSCATE("dataComponent"));
        void* dataComp = nullptr;
        if (f_dataComp) _monoFieldGetValue(selfPlayer, f_dataComp, &dataComp);
        if (!dataComp) {
            void* m_getDc = FindMethodInHierarchy(spKlass, OBFUSCATE("get_dataComponent"), 0);
            if (m_getDc) dataComp = _monoRuntimeInvoke(m_getDc, selfPlayer, nullptr, nullptr);
        }
        if (dataComp) {
            void* dcKlass = SafeMonoObjectGetClass(dataComp);
            if (!dcKlass) return;
            void* m_setRough = FindMethodInHierarchy(dcKlass, OBFUSCATE("SetSandRoughData"), 1);
            if (m_setRough) {
                uint32_t zeroVal = 0;
                void* args[] = { &zeroVal };
                _monoRuntimeInvoke(m_setRough, dataComp, args, nullptr);
            }
        }
    });
}

inline void CloseSandModelDialogNative() {
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        const char* imgNames[] = { "XDTGameUI", "XDTGameUI.dll", "Client", "Client.dll", "Assembly-CSharp", "Assembly-CSharp.dll" };
        void* dlgKlass = nullptr;
        for (const char* iname : imgNames) {
            void* imgUI = _monoImageLoaded(iname);
            if (!imgUI) continue;
            dlgKlass = _monoClassFromName(imgUI, OBFUSCATE("XDTGame.UI.Panel"), OBFUSCATE("DialogueSimplePanel"));
            if (!dlgKlass) dlgKlass = _monoClassFromName(imgUI, "", OBFUSCATE("DialogueSimplePanel"));
            if (dlgKlass) break;
        }
        if (!dlgKlass) return;

        void* dlgType = _monoClassGetType ? _monoClassGetType(dlgKlass) : nullptr;
        void* dlgTypeObj = (dlgType && _monoTypeGetObject) ? _monoTypeGetObject(_monoGetRootDomain(), dlgType) : nullptr;
        if (!dlgTypeObj) return;

        void* uiMgrKlass = nullptr;
        for (const char* iname : imgNames) {
            void* imgUI = _monoImageLoaded(iname);
            if (!imgUI) continue;
            uiMgrKlass = _monoClassFromName(imgUI, OBFUSCATE("XDTGame.Core"), OBFUSCATE("UIManager"));
            if (!uiMgrKlass) uiMgrKlass = _monoClassFromName(imgUI, OBFUSCATE("XDTGame.UI.Framework"), OBFUSCATE("UIManager"));
            if (!uiMgrKlass) uiMgrKlass = _monoClassFromName(imgUI, OBFUSCATE("XDTGame.UI"), OBFUSCATE("UIManager"));
            if (uiMgrKlass) break;
        }
        void* uiMgrInst = nullptr;
        if (uiMgrKlass) {
            void* m_getInst = FindMethodInHierarchy(uiMgrKlass, OBFUSCATE("get_Instance"), 0);
            if (!m_getInst) m_getInst = FindMethodInHierarchy(uiMgrKlass, OBFUSCATE("get_instance"), 0);
            if (m_getInst) uiMgrInst = _monoRuntimeInvoke(m_getInst, nullptr, nullptr, nullptr);
        }
        if (!uiMgrInst) {
            uiMgrInst = GetManagerInstanceNative(OBFUSCATE("UIManager"));
            if (uiMgrInst) uiMgrKlass = SafeMonoObjectGetClass(uiMgrInst);
        }
        if (!uiMgrInst || !uiMgrKlass) return;

        void* m_getView = FindMethodInHierarchy(uiMgrKlass, OBFUSCATE("GetView"), 1);
        if (!m_getView) return;

        void* args[] = { dlgTypeObj };
        void* exc = nullptr;
        void* viewObj = _monoRuntimeInvoke(m_getView, uiMgrInst, args, &exc);
        if (viewObj && !exc) {
            void* vKlass = SafeMonoObjectGetClass(viewObj);
            if (vKlass) {
                void* m_close = FindMethodInHierarchy(vKlass, OBFUSCATE("CloseSelf"), 0);
                if (m_close) {
                    _monoRuntimeInvoke(m_close, viewObj, nullptr, nullptr);
                    LOGI("[Sand] Closed DialogueSimplePanel");
                }
            }
        }
    });
}

// -------------------------------------------------------------------------
// QTE Rounds & Feature Quality Open Checks
// -------------------------------------------------------------------------
inline int GetSandQteRoundCount(int roughStaticId) {
    if (roughStaticId <= 0) return 4;
    int rounds = 0;
    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
        const char* imgNames[] = { "EcsClient", "EcsClient.dll", "Client", "Client.dll" };
        void* img = nullptr;
        for (const char* iname : imgNames) {
            img = _monoImageLoaded(iname);
            if (img) break;
        }
        if (!img) return;

        void* tableDataKlass = _monoClassFromName(img, "", OBFUSCATE("TableData"));
        if (!tableDataKlass) tableDataKlass = _monoClassFromName(img, OBFUSCATE("EcsClient"), OBFUSCATE("TableData"));
        if (!tableDataKlass) tableDataKlass = _monoClassFromName(img, OBFUSCATE("XD.GameGerm.ExpParser"), OBFUSCATE("TableData"));
        if (!tableDataKlass) return;

        void* m_getSandrough = FindMethodInHierarchy(tableDataKlass, OBFUSCATE("GetSandrough"), 2);
        if (!m_getSandrough) m_getSandrough = FindMethodInHierarchy(tableDataKlass, OBFUSCATE("GetSandrough"), 1);
        if (!m_getSandrough) return;

        int sid = roughStaticId;
        uint8_t flag = 0;
        void* args2[] = { &sid, &flag };
        void* args1[] = { &sid };
        void* exc = nullptr;
        void* rowObj = nullptr;
        if (FindMethodInHierarchy(tableDataKlass, OBFUSCATE("GetSandrough"), 2)) {
            rowObj = _monoRuntimeInvoke(m_getSandrough, nullptr, args2, &exc);
        } else {
            rowObj = _monoRuntimeInvoke(m_getSandrough, nullptr, args1, &exc);
        }
        if (exc || !rowObj) return;

        void* rowKlass = SafeMonoObjectGetClass(rowObj);
        if (!rowKlass) return;
        void* f_interval = _monoClassGetFieldFromName(rowKlass, OBFUSCATE("interval"));
        void* intervalArr = nullptr;
        if (f_interval) {
            _monoFieldGetValue(rowObj, f_interval, &intervalArr);
        }
        if (!intervalArr) {
            void* m_interval = FindMethodInHierarchy(rowKlass, OBFUSCATE("get_interval"), 0);
            if (m_interval) intervalArr = _monoRuntimeInvoke(m_interval, rowObj, nullptr, nullptr);
        }
        if (intervalArr) {
            uintptr_t arrAddr = (uintptr_t)intervalArr;
            int len = SafeRead<int>(arrAddr + OFF_ARR_COUNT, 0);
            if (len > 0 && len <= 16) {
                rounds = len;
                LOGI("[Sand] TableSandrough(%d) interval length = %d", roughStaticId, rounds);
            }
        }
    });
    if (rounds <= 0) rounds = 4;
    return rounds;
}

inline bool IsSandQualityFeatureOpen() {
    bool isOpen = true;
    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
        void* featSys = TryResolveSystemModule("XDTGameSystem.GameplaySystem.FeatureOpen.FeatureOpenSystem");
        if (!featSys) featSys = GetManagerInstanceNative(OBFUSCATE("FeatureOpenSystem"));
        if (!featSys) return;

        void* fsKlass = SafeMonoObjectGetClass(featSys);
        if (!fsKlass) return;
        void* m_isOpen = FindMethodInHierarchy(fsKlass, OBFUSCATE("IsFeatureOpen"), 1);
        if (!m_isOpen) return;

        int featId = 300041;
        void* args[] = { &featId };
        void* exc = nullptr;
        void* res = _monoRuntimeInvoke(m_isOpen, featSys, args, &exc);
        if (!exc && res) {
            isOpen = (*(uint8_t*)_monoObjectUnbox(res) != 0);
            LOGI("[Sand] FeatureOpenSystem.IsFeatureOpen(300041) = %d", isOpen ? 1 : 0);
        }
    });
    return isOpen;
}

// -------------------------------------------------------------------------
// Hobby Level & Pictorial State (Identical to Bugtopia's Smart Model Picker)
// -------------------------------------------------------------------------
inline bool TryGetSandHobbyLevel(int& outLevel) {
    outLevel = 0;
    bool success = false;
    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
        void* hobbySys = TryResolveSystemModule("XDTGameSystem.GameplaySystem.Hobby.HobbySystem");
        if (!hobbySys) hobbySys = GetManagerInstanceNative(OBFUSCATE("HobbySystem"));
        if (!hobbySys) return;

        void* hsKlass = SafeMonoObjectGetClass(hobbySys);
        if (!hsKlass) return;
        void* m_getLevel = FindMethodInHierarchy(hsKlass, OBFUSCATE("GetHobbyLevel"), 1);
        if (!m_getLevel) return;

        int hobbyId = 140; // Sand sculpture hobby ID
        void* args[] = { &hobbyId };
        void* exc = nullptr;
        void* res = _monoRuntimeInvoke(m_getLevel, hobbySys, args, &exc);
        if (!exc && res) {
            outLevel = *(int*)_monoObjectUnbox(res);
            success = true;
        }
    });
    return success;
}

inline bool TryReadSandPictorialState(int staticId, int& outState, int& outStarRate) {
    outState = 0;
    outStarRate = 0;
    bool success = false;
    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
        void* picSys = TryResolveSystemModule("XDTGameSystem.GameplaySystem.Pictorial.PictorialSystem");
        if (!picSys) picSys = GetManagerInstanceNative(OBFUSCATE("PictorialSystem"));
        if (!picSys) return;

        void* psKlass = SafeMonoObjectGetClass(picSys);
        if (!psKlass) return;
        void* m_tryGet = FindMethodInHierarchy(psKlass, OBFUSCATE("TryGetPictorialData"), 2);
        if (!m_tryGet) return;

        int sid = staticId;
        void* picDataObj = nullptr;
        void* args[] = { &sid, &picDataObj };
        void* exc = nullptr;
        void* ret = _monoRuntimeInvoke(m_tryGet, picSys, args, &exc);
        if (exc || !ret) return;
        bool ok = *(bool*)_monoObjectUnbox(ret);
        if (!ok || !picDataObj) {
            // Not in pictorial dictionary -> Never collected! State 0 = Uncollected
            outState = 0;
            outStarRate = 0;
            success = true;
            return;
        }

        void* pdKlass = SafeMonoObjectGetClass(picDataObj);
        if (!pdKlass) return;
        void* f_state = FindFieldInHierarchy(pdKlass, OBFUSCATE("_state"));
        void* f_star = FindFieldInHierarchy(pdKlass, OBFUSCATE("_starRate"));
        if (f_state) _monoFieldGetValue(picDataObj, f_state, (void**)&outState);
        if (f_star) _monoFieldGetValue(picDataObj, f_star, (void**)&outStarRate);
        success = true;
    });
    return success;
}

inline int SandCurrentStarCap() {
    if (!IsSandQualityFeatureOpen()) {
        return 1;
    }
    int level = 0;
    if (TryGetSandHobbyLevel(level) && level > 0) {
        if (level >= 5) return 5;
        if (level >= 3) return 4;
        return 3;
    }
    return 5;
}

inline bool TryGetSandModelCollectValue(int modelStaticId, int starCap, int& outCollectValue) {
    outCollectValue = 2; // Default to uncollected (highest priority) if not maxed
    int state = 0;
    int starRate = 0;
    if (!TryReadSandPictorialState(modelStaticId, state, starRate)) {
        return false;
    }
    if (state != 2) {
        outCollectValue = 2; // Uncollected in album -> Top Priority!
    } else if (starRate < (starCap < 5 ? starCap : 5)) {
        outCollectValue = 1; // Stars not maxed yet -> Medium Priority
    } else {
        outCollectValue = 0; // Completely collected & maxed -> Lowest Priority
    }
    return true;
}

// Bugtopia Smart Option Scorer: SandPickModelOptionIndex
inline int SandPickModelOptionIndex(const std::vector<int>& options) {
    if (options.empty()) return 0;

    // If user manually chose a valid productId, prioritize it
    if (g_sandProductId > 0) {
        for (size_t i = 0; i < options.size(); i++) {
            if (options[i] == g_sandProductId) {
                LOGI("[Sand] Picked user-chosen productId=%d at index %zu", g_sandProductId, i);
                return (int)i;
            }
        }
    }

    int firstValidIndex = -1;
    int lowestValidId = 999999;
    int lowestAnyIndex = 0;
    int validCount = 0;

    for (size_t i = 0; i < options.size(); i++) {
        int opt = options[i];
        if (opt < options[lowestAnyIndex]) lowestAnyIndex = (int)i;
        if (opt >= 1 && opt <= 14) {
            validCount++;
            if (opt < lowestValidId) {
                lowestValidId = opt;
                firstValidIndex = (int)i;
            }
        }
    }

    if (validCount == 0) {
        LOGI("[Sand] model-pick: NO valid model (1..14) in options -> forced index %d", lowestAnyIndex);
        return lowestAnyIndex;
    }

    // Bugtopia Smart Pick logic
    int starCap = SandCurrentStarCap();
    int bestIndex = (firstValidIndex != -1) ? firstValidIndex : 0;
    long long bestScore = -999999999999LL;

    for (size_t j = 0; j < options.size(); j++) {
        int opt = options[j];
        if (opt >= 1 && opt <= 14) {
            int modelStaticId = s_BugtopiaSandItemToModelStaticId[opt];
            int rarity = s_BugtopiaSandItemRarity[opt];
            int collectValue = 0;
            TryGetSandModelCollectValue(modelStaticId, starCap, collectValue);

            // Priority score: collectValue (weight 40) | rarity (weight 32) | inverted opt
            long long score = ((long long)collectValue << 40) | ((long long)rarity << 32) | (uint32_t)(0x7FFFFFFF - opt);
            if (score > bestScore) {
                bestScore = score;
                bestIndex = (int)j;
            }
        }
    }

    LOGI("[Sand] model-pick: validCount=%d cap=%d -> selected index=%d (id=%d, rarity=%d)",
        validCount, starCap, bestIndex, options[bestIndex], s_BugtopiaSandItemRarity[options[bestIndex] >= 1 && options[bestIndex] <= 14 ? options[bestIndex] : 0]);
    return bestIndex;
}

// -------------------------------------------------------------------------
// Find Nearest Sand Base (SandSculpturesComponent scan within 60m)
// -------------------------------------------------------------------------
inline SandBaseScanResult FindNearestSandBaseNative(const std::unordered_set<uint32_t>& blacklist = {}) {
    SandBaseScanResult result;
    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
        void* imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        if (!imgData) imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
        void* imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
        if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
        if (!imgData && !imgLevel) return;

        Vector3 playerPos = { 0, 0, 0 };
        bool hasPlayerPos = false;
        void* utilKlass = imgLevel ? _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil")) : nullptr;
        void* selfPlayer = nullptr;
        if (utilKlass) {
            void* m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
            if (m_getSelf) {
                selfPlayer = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, nullptr);
            }
        }
        if (selfPlayer) {
            playerPos = NativeGetPosition(selfPlayer);
            hasPlayerPos = (playerPos.x != 0.0f || playerPos.y != 0.0f || playerPos.z != 0.0f);
        }

        float minDistance = 60.0f;
        uint32_t bestNetId = 0;
        int bestRoughStaticId = 0;

        // Prepare DataCenter.TryGetComponentData<SandSculpturesComponentData>
        void* closedTryGetSand = nullptr;
        if (imgData) {
            void* dcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("DataCenter"));
            void* scdKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("SandSculpturesComponentData"));
            if (dcKlass && scdKlass) {
                void* scdType = _monoClassGetType ? _monoClassGetType(scdKlass) : nullptr;
                void* openTryGet = FindMethodInHierarchy(dcKlass, OBFUSCATE("TryGetComponentData"), 2);
                if (openTryGet && scdType && _mono_metadata_get_generic_inst && _mono_class_inflate_generic_method) {
                    void* typeArgs[1] = { scdType };
                    void* methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
                    MonoGenericContext ctx = { nullptr, methodInst };
                    closedTryGetSand = _mono_class_inflate_generic_method(openTryGet, &ctx);
                }
            }
        }

        // METHOD 0: DataCenter._netEntitiesMap
        if (imgData && closedTryGetSand) {
            void* dcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("DataCenter"));
            void* m_getCur = dcKlass ? FindMethodInHierarchy(dcKlass, OBFUSCATE("get_CurrentData"), 0) : nullptr;
            if (m_getCur) {
                void* cur = _monoRuntimeInvoke(m_getCur, nullptr, nullptr, nullptr);
                if (cur) {
                    void* sysKlass = SafeMonoObjectGetClass(cur);
                    void* f_map = sysKlass ? _monoClassGetFieldFromName(sysKlass, OBFUSCATE("_netEntitiesMap")) : nullptr;
                    void* dict = nullptr;
                    if (f_map) _monoFieldGetValue(cur, f_map, &dict);
                    if (dict) {
                        void* dk = SafeMonoObjectGetClass(dict);
                        void* f_e = dk ? _monoClassGetFieldFromName(dk, OBFUSCATE("entries")) : nullptr;
                        if (!f_e && dk) f_e = _monoClassGetFieldFromName(dk, OBFUSCATE("_entries"));
                        void* arr = nullptr;
                        if (f_e) _monoFieldGetValue(dict, f_e, &arr);
                        if (arr) {
                            uintptr_t aa = (uintptr_t)arr;
                            int len = SafeRead<int>(aa + OFF_ARR_COUNT, 0);
                            if (len > 0 && len <= 100000) {
                                uintptr_t d = aa + OFF_ARR_DATA;
                                void* entitiesKlass = imgLevel ? _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("Entities")) : nullptr;
                                void* m_getAny = entitiesKlass ? FindMethodInHierarchy(entitiesKlass, OBFUSCATE("GetAnyEntity"), 1) : nullptr;

                                for (int i = 0; i < len; i++) {
                                    uintptr_t e = d + (uintptr_t)i * SZ_DICT_ENTRY;
                                    int next = SafeRead<int>(e + 4, -99);
                                    if (next < -1) continue;
                                    uint32_t nid = SafeRead<uint32_t>(e + OFF_ENTRY_KEY, 0);
                                    if (nid == 0) continue;
                                    if (blacklist.count(nid)) continue;

                                    SandSculpturesComponentDataNative cdData = { 0, 0 };
                                    void* args[2] = { &nid, &cdData };
                                    void* exc = nullptr;
                                    void* retBoxed = _monoRuntimeInvoke(closedTryGetSand, nullptr, args, &exc);
                                    if (retBoxed && !exc) {
                                        bool ok = *(bool*)_monoObjectUnbox(retBoxed);
                                        if (ok && (cdData.baseStaticId > 0 || cdData.roughStaticId > 0)) {
                                            float dist = 999999.0f;
                                            Vector3 entPos = { 0, 0, 0 };
                                            bool hasEntPos = false;
                                            if (m_getAny) {
                                                uint32_t argNid = nid;
                                                void* aArgs[] = { &argNid };
                                                void* ent = _monoRuntimeInvoke(m_getAny, nullptr, aArgs, nullptr);
                                                if (ent) {
                                                    entPos = NativeGetPosition(ent);
                                                    hasEntPos = (entPos.x != 0.0f || entPos.y != 0.0f || entPos.z != 0.0f);
                                                }
                                            }
                                            if (hasPlayerPos && hasEntPos) {
                                                float dx = entPos.x - playerPos.x;
                                                float dy = entPos.y - playerPos.y;
                                                float dz = entPos.z - playerPos.z;
                                                dist = sqrtf(dx * dx + dy * dy + dz * dz);
                                            }
                                            int rSid = cdData.roughStaticId > 0 ? cdData.roughStaticId : 600001;
                                            if (dist <= minDistance) {
                                                minDistance = dist;
                                                bestNetId = nid;
                                                bestRoughStaticId = rSid;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // METHOD 1: LevelObjectManager scan
        if (bestNetId == 0) {
            void* lomKlass = imgData ? _monoClassFromName(imgData, OBFUSCATE("ScriptsRefactory.LevelAndEntity"), OBFUSCATE("LevelObjectManager")) : nullptr;
            if (!lomKlass && imgLevel) lomKlass = _monoClassFromName(imgLevel, OBFUSCATE("ScriptsRefactory.LevelAndEntity"), OBFUSCATE("LevelObjectManager"));
            if (lomKlass) {
                void* lomInst = nullptr;
                void* m_getInst = FindMethodInHierarchy(lomKlass, OBFUSCATE("get_Instance"), 0);
                if (m_getInst) lomInst = _monoRuntimeInvoke(m_getInst, nullptr, nullptr, nullptr);
                if (!lomInst) {
                    void* f_inst = FindFieldInHierarchy(lomKlass, OBFUSCATE("_instance"));
                    if (f_inst && _monoClassGetVTable && _monoFieldStaticGetValue) {
                        void* vtable = _monoClassGetVTable(_monoGetRootDomain(), lomKlass);
                        if (vtable) _monoFieldStaticGetValue(vtable, f_inst, &lomInst);
                    }
                }
                if (lomInst) {
                    void* lomInstKlass = SafeMonoObjectGetClass(lomInst);
                    void* f_dict = lomInstKlass ? FindFieldInHierarchy(lomInstKlass, OBFUSCATE("_dictionary")) : nullptr;
                    void* dictObj = nullptr;
                    if (f_dict) _monoFieldGetValue(lomInst, f_dict, &dictObj);
                    if (dictObj) {
                        void* dictKlass = SafeMonoObjectGetClass(dictObj);
                        void* f_entries = dictKlass ? FindFieldInHierarchy(dictKlass, OBFUSCATE("entries")) : nullptr;
                        if (!f_entries && dictKlass) f_entries = FindFieldInHierarchy(dictKlass, OBFUSCATE("_entries"));
                        void* entriesArr = nullptr;
                        if (f_entries) _monoFieldGetValue(dictObj, f_entries, &entriesArr);
                        if (entriesArr) {
                            uintptr_t arrAddr = (uintptr_t)entriesArr;
                            int length = SafeRead<int>(arrAddr + OFF_ARR_COUNT, 0);
                            uintptr_t data = arrAddr + OFF_ARR_DATA;
                            for (int i = 0; i < length; i++) {
                                int next = SafeRead<int>(data + (i * SZ_DICT_ENTRY) + 4, -99);
                                if (next < -1) continue;
                                void* lo = *(void**)(data + (i * SZ_DICT_ENTRY) + OFF_ENTRY_VALUE);
                                if (!lo) continue;

                                void* loKlass = SafeMonoObjectGetClass(lo);
                                uint32_t ownerNetId = 0;
                                void* m_getOwner = loKlass ? FindMethodInHierarchy(loKlass, OBFUSCATE("get_ownerNetId"), 0) : nullptr;
                                if (m_getOwner) {
                                    void* r = _monoRuntimeInvoke(m_getOwner, lo, nullptr, nullptr);
                                    if (r) ownerNetId = *(uint32_t*)_monoObjectUnbox(r);
                                }
                                if (ownerNetId == 0) continue;
                                if (blacklist.count(ownerNetId)) continue;

                                Vector3 loPos = NativeGetPosition(lo);
                                int staticId = 0;
                                void* m_getSid = loKlass ? FindMethodInHierarchy(loKlass, OBFUSCATE("get_staticId"), 0) : nullptr;
                                if (m_getSid) {
                                    void* r = _monoRuntimeInvoke(m_getSid, lo, nullptr, nullptr);
                                    if (r) staticId = *(int*)_monoObjectUnbox(r);
                                }

                                bool isSandBase = (staticId == 600000 || (staticId >= 600000 && staticId < 610000));
                                if (!isSandBase) continue;

                                float dist = 0.0f;
                                if (hasPlayerPos) {
                                    float dx = loPos.x - playerPos.x;
                                    float dy = loPos.y - playerPos.y;
                                    float dz = loPos.z - playerPos.z;
                                    dist = sqrtf(dx * dx + dy * dy + dz * dz);
                                }
                                if (dist <= minDistance) {
                                    minDistance = dist;
                                    bestNetId = ownerNetId;
                                    bestRoughStaticId = 600001;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (bestNetId != 0) {
            result.baseNetId = bestNetId;
            result.roughStaticId = bestRoughStaticId;
            result.distance = minDistance;
            LOGI("[Sand] Found nearest sand base netId=%u roughStaticId=%d dist=%.2fm", bestNetId, bestRoughStaticId, minDistance);
        }
    });

    return result;
}

// -------------------------------------------------------------------------
// Helper: SandMonoArrayNew
// -------------------------------------------------------------------------
inline void* SandMonoArrayNew(void* domain, void* elemKlass, size_t count) {
    static void* (*p)(void*, void*, uintptr_t) = nullptr;
    if (!p) {
        void* h = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), 0);
        if (!h) h = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), RTLD_NOW);
        if (!h) h = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), XDL_TRY_FORCE_LOAD);
        if (h) {
            p = (void*(*)(void*, void*, uintptr_t)) xdl_sym(h, (const char*)OBFUSCATE("mono_array_new"), (size_t*)nullptr);
        }
    }
    if (!p) return nullptr;
    return p(domain, elemKlass, count);
}

// -------------------------------------------------------------------------
// Place Sand Base Native (Homeland local transform placement identical to Bugtopia)
// -------------------------------------------------------------------------
inline bool PlaceSandBaseNative() {
    bool success = false;
    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
        void* imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        if (!imgData) imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
        void* imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
        if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
        if (!imgData || !imgLevel) return;

        int wantSid = g_sandBaseStaticId;
        if (wantSid <= 0) wantSid = 600000;
        uint32_t bagItemNetId = GetSeedNetId(wantSid);
        if (bagItemNetId == 0 && wantSid != 600000) {
            bagItemNetId = GetSeedNetId(600000);
        }
        if (bagItemNetId == 0) {
            LOGI("[Sand] No sand base in backpack");
            return;
        }

        uint32_t buildRootNetId = 0;
        Vector3 playerPos = { 0, 0, 0 };

        void* utilKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
        if (!utilKlass) return;
        void* m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
        if (!m_getSelf) return;

        void* selfPlayer = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, nullptr);
        if (!selfPlayer) return;

        void* spKlass = SafeMonoObjectGetClass(selfPlayer);
        if (!spKlass) return;
        void* m_getFieldNetId = FindMethodInHierarchy(spKlass, OBFUSCATE("get_inFieldNetId"), 0);
        if (m_getFieldNetId) {
            void* fnObj = _monoRuntimeInvoke(m_getFieldNetId, selfPlayer, nullptr, nullptr);
            if (fnObj) buildRootNetId = *(uint32_t*)_monoObjectUnbox(fnObj);
        }
        if (buildRootNetId == 0) {
            void* f_inFieldNetId = _monoClassGetFieldFromName(spKlass, OBFUSCATE("_inFieldNetId"));
            if (!f_inFieldNetId) f_inFieldNetId = _monoClassGetFieldFromName(spKlass, OBFUSCATE("inFieldNetId"));
            if (f_inFieldNetId) _monoFieldGetValue(selfPlayer, f_inFieldNetId, (void**)&buildRootNetId);
        }
        if (buildRootNetId == 0) {
            void* f_dataComp = _monoClassGetFieldFromName(spKlass, OBFUSCATE("_dataComponent"));
            if (!f_dataComp) f_dataComp = _monoClassGetFieldFromName(spKlass, OBFUSCATE("dataComponent"));
            void* dataComp = nullptr;
            if (f_dataComp) _monoFieldGetValue(selfPlayer, f_dataComp, &dataComp);
            if (dataComp) {
                void* dcKlass = SafeMonoObjectGetClass(dataComp);
                if (dcKlass) {
                    void* f_dcField = _monoClassGetFieldFromName(dcKlass, OBFUSCATE("_inFieldNetId"));
                    if (!f_dcField) f_dcField = _monoClassGetFieldFromName(dcKlass, OBFUSCATE("inFieldNetId"));
                    if (f_dcField) _monoFieldGetValue(dataComp, f_dcField, (void**)&buildRootNetId);
                }
            }
        }
        if (buildRootNetId == 0) {
            LOGI("[Sand] Cannot resolve homeland field inFieldNetId");
            return;
        }

        playerPos = NativeGetPosition(selfPlayer);
        uint64_t putZoneId = ((uint64_t)buildRootNetId) | 0x100000000ULL;
        Vector3 forward = { 0, 0, 1 };
        int angleVal = 0;

        void* m_getEnt = FindMethodInHierarchy(spKlass, OBFUSCATE("get_entity"), 0);
        if (m_getEnt) {
            void* entObj = _monoRuntimeInvoke(m_getEnt, selfPlayer, nullptr, nullptr);
            if (entObj) {
                void* entKlass = SafeMonoObjectGetClass(entObj);
                if (entKlass) {
                    void* m_getFwd = FindMethodInHierarchy(entKlass, "get_forward", 0);
                    if (m_getFwd) {
                        void* fObj = _monoRuntimeInvoke(m_getFwd, entObj, nullptr, nullptr);
                        if (fObj) forward = *(Vector3*)_monoObjectUnbox(fObj);
                    }
                }
            }
        }
        forward.y = 0.0f;
        float fLen = sqrtf(forward.x * forward.x + forward.z * forward.z);
        if (fLen > 0.001f) {
            forward.x /= fLen;
            forward.z /= fLen;
            int yaw = (int)round(atan2f(forward.x, forward.z) * 180.0f / 3.14159265f);
            if (yaw < 0) yaw += 360;
            angleVal = yaw & 0x3FF;
        } else {
            forward.x = 0.0f; forward.y = 0.0f; forward.z = 1.0f;
        }

        Vector3 worldTarget = {
            playerPos.x + forward.x * 1.5f,
            playerPos.y,
            playerPos.z + forward.z * 1.5f
        };

        Vector3 localPos = worldTarget;
        ModMatrix4x4 w2l = ModMatrix4x4::Identity();
        if (TryGetFieldRootWorldToLocal(buildRootNetId, w2l)) {
            localPos = w2l.MultiplyPoint3x4(worldTarget);
        }

        void* protoKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Homeland"), OBFUSCATE("HomelandProtocolManager"));
        if (!protoKlass) return;
        void* m_sendBuild = FindMethodInHierarchy(protoKlass, OBFUSCATE("SendBuildBatchOperation"), 3);
        if (!m_sendBuild) return;

        void* placeDataKlass = nullptr;
        void* transDataKlass = nullptr;
        const char* imgNames[] = { "EcsClient", "Client", "XDTDataAndProtocol", "XDTLevelAndEntity" };
        for (const char* iname : imgNames) {
            void* simg = _monoImageLoaded(iname);
            if (!simg) simg = _monoImageLoaded((std::string(iname) + ".dll").c_str());
            if (!simg) continue;
            if (!placeDataKlass) placeDataKlass = _monoClassFromName(simg, "XDT.Scene.Shared.Modules.Build", "BuildPlaceData");
            if (!transDataKlass) transDataKlass = _monoClassFromName(simg, "XDT.Scene.Shared.Modules.Build", "BuildTransformData");
        }
        if (!placeDataKlass || !transDataKlass) return;

        void* rootDomain = _monoGetRootDomain();
        void* transDataObj = _monoObjectNew(rootDomain, transDataKlass);
        void* placeDataObj = _monoObjectNew(rootDomain, placeDataKlass);
        if (!transDataObj || !placeDataObj) return;

        void* corlib = _monoImageLoaded("mscorlib");
        if (!corlib) corlib = _monoImageLoaded("mscorlib.dll");
        if (!corlib) corlib = _monoImageLoaded("System.Private.CoreLib");
        void* ulongKlass = corlib ? _monoClassFromName(corlib, "System", "UInt64") : nullptr;
        void* ulongArr = ulongKlass ? SandMonoArrayNew(rootDomain, ulongKlass, 1) : nullptr;
        if (ulongArr) {
            uintptr_t arrData = (uintptr_t)ulongArr + OFF_ARR_DATA;
            *(uint64_t*)arrData = putZoneId;
        }

        void* f_levelObjNetId = _monoClassGetFieldFromName(transDataKlass, "LevelObjectNetId");
        void* f_localPos = _monoClassGetFieldFromName(transDataKlass, "LocalPos");
        void* f_angle = _monoClassGetFieldFromName(transDataKlass, "Angle");
        void* f_vlinkHasChange = _monoClassGetFieldFromName(transDataKlass, "VirtualLinkHasChange");
        void* f_vlink = _monoClassGetFieldFromName(transDataKlass, "VirtualLinkLevelObjectNetId");

        if (f_levelObjNetId) _monoFieldSetValue(transDataObj, f_levelObjNetId, &putZoneId);
        if (f_localPos) _monoFieldSetValue(transDataObj, f_localPos, &localPos);
        if (f_angle) _monoFieldSetValue(transDataObj, f_angle, &angleVal);
        uint8_t vlinkChangeVal = 1;
        if (f_vlinkHasChange) _monoFieldSetValue(transDataObj, f_vlinkHasChange, &vlinkChangeVal);
        if (f_vlink && ulongArr) _monoFieldSetValue(transDataObj, f_vlink, ulongArr);

        void* f_buildType = _monoClassGetFieldFromName(placeDataKlass, "BuildType");
        void* f_transformData = _monoClassGetFieldFromName(placeDataKlass, "TransformData");

        uint8_t bType = 0;
        if (f_buildType) _monoFieldSetValue(placeDataObj, f_buildType, &bType);
        if (f_transformData) {
            void* unboxedTrans = _monoObjectUnbox(transDataObj);
            if (unboxedTrans) _monoFieldSetValue(placeDataObj, f_transformData, unboxedTrans);
            else _monoFieldSetValue(placeDataObj, f_transformData, transDataObj);
        }

        uint32_t argBag = bagItemNetId;
        uint32_t argRoot = buildRootNetId;
        void* args[] = { &argBag, &argRoot, placeDataObj };
        void* exc = nullptr;
        _monoRuntimeInvoke(m_sendBuild, nullptr, args, &exc);
        if (!exc) {
            success = true;
            LOGI("[Sand] Successfully placed sand base bagItem=%u root=%u", bagItemNetId, buildRootNetId);
        }
    });
    return success;
}

// -------------------------------------------------------------------------
// Find Own Sand Rough Entity (Reads player dataComponent + ECS rough options)
// -------------------------------------------------------------------------
inline SandRoughResult FindOwnSandRoughNative() {
    SandRoughResult res;
    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
        void* imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        if (!imgData) imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
        void* imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
        if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
        if (!imgData || !imgLevel) return;

        uint32_t selfNetId = 0;
        void* selfPlayer = nullptr;
        void* utilKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
        if (utilKlass) {
            void* m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
            if (m_getSelf) {
                void* exc = nullptr;
                selfPlayer = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, &exc);
            }
        }
        void* pdcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol"), OBFUSCATE("PlayerDataCenter"));
        if (pdcKlass) {
            void* m_getSelfId = FindMethodInHierarchy(pdcKlass, OBFUSCATE("GetSelfNetPlayerId"), 0);
            if (m_getSelfId) {
                void* exc = nullptr;
                void* r = _monoRuntimeInvoke(m_getSelfId, nullptr, nullptr, &exc);
                if (r && !exc) selfNetId = *(uint32_t*)_monoObjectUnbox(r);
            }
        }

        uint32_t playerRoughNetId = 0;
        if (selfPlayer) {
            void* spKlass = SafeMonoObjectGetClass(selfPlayer);
            if (spKlass) {
                void* f_dataComp = _monoClassGetFieldFromName(spKlass, OBFUSCATE("_dataComponent"));
                if (!f_dataComp) f_dataComp = _monoClassGetFieldFromName(spKlass, OBFUSCATE("dataComponent"));
                void* dataComp = nullptr;
                if (f_dataComp) _monoFieldGetValue(selfPlayer, f_dataComp, &dataComp);
                if (!dataComp) {
                    void* m_getDc = FindMethodInHierarchy(spKlass, OBFUSCATE("get_dataComponent"), 0);
                    if (m_getDc) dataComp = _monoRuntimeInvoke(m_getDc, selfPlayer, nullptr, nullptr);
                }
                if (dataComp) {
                    void* dcKlass = SafeMonoObjectGetClass(dataComp);
                    if (dcKlass) {
                        void* f_rnid = _monoClassGetFieldFromName(dcKlass, OBFUSCATE("_sandRoughNetId"));
                        if (f_rnid) {
                            void* rnidObj = nullptr;
                            _monoFieldGetValue(dataComp, f_rnid, &rnidObj);
                            playerRoughNetId = (uint32_t)(uintptr_t)rnidObj;
                        }
                    }
                }
            }
        }

        if (playerRoughNetId != 0) {
            res.roughNetId = playerRoughNetId;
            if (imgData) {
                void* dcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("DataCenter"));
                void* roughDataKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("SandSculptureRoughComponentData"));
                if (dcKlass && roughDataKlass) {
                    void* roughDataType = _monoClassGetType ? _monoClassGetType(roughDataKlass) : nullptr;
                    void* openTryGet = FindMethodInHierarchy(dcKlass, OBFUSCATE("TryGetComponentData"), 2);
                    if (openTryGet && roughDataType && _mono_metadata_get_generic_inst && _mono_class_inflate_generic_method) {
                        void* typeArgs[1] = { roughDataType };
                        void* methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
                        MonoGenericContext ctx = { nullptr, methodInst };
                        void* closedTryGet = _mono_class_inflate_generic_method(openTryGet, &ctx);
                        if (closedTryGet) {
                            uint32_t nidVal = playerRoughNetId;
                            SandSculptureRoughComponentDataNative cdData = { 0, 0, nullptr };
                            void* args[2] = { &nidVal, &cdData };
                            void* exc = nullptr;
                            void* retBoxed = _monoRuntimeInvoke(closedTryGet, nullptr, args, &exc);
                            if (retBoxed && !exc) {
                                bool ok = *(bool*)_monoObjectUnbox(retBoxed);
                                if (ok && cdData.options) {
                                    uintptr_t aAddr = (uintptr_t)cdData.options;
                                    int optLen = SafeRead<int>(aAddr + OFF_ARR_COUNT, 0);
                                    if (optLen > 0 && optLen <= 64) {
                                        res.options.clear();
                                        for (int oi = 0; oi < optLen; oi++) {
                                            int optVal = SafeRead<int>(aAddr + OFF_ARR_DATA + oi * 4, 0);
                                            res.options.push_back(optVal);
                                        }
                                        LOGI("[Sand] DataCenter found roughNetId=%u, options=%zu", playerRoughNetId, res.options.size());
                                        return;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Fallback: Scan SandSculptureRoughComponent in ECS
        void* roughKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component"), OBFUSCATE("SandSculptureRoughComponent"));
        if (!roughKlass) roughKlass = _monoClassFromName(imgLevel, OBFUSCATE("ScriptsRefactory.LevelAndEntity.Gameplay.Component"), OBFUSCATE("SandSculptureRoughComponent"));
        if (roughKlass) {
            void* worldKlass = _monoClassFromName(imgLevel, "XDTGame.GAS.Client", OBFUSCATE("EntityWorld"));
            if (!worldKlass) worldKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("EntityWorld"));
            if (worldKlass) {
                void* roughType = _monoClassGetType ? _monoClassGetType(roughKlass) : nullptr;
                void* roughTypeObj = (roughType && _monoTypeGetObject) ? _monoTypeGetObject(_monoGetRootDomain(), roughType) : nullptr;
                void* sysObj = nullptr;
                if (roughTypeObj) {
                    void* m_getSys = FindMethodInHierarchy(worldKlass, OBFUSCATE("GetComponentSystem"), 1);
                    if (m_getSys) {
                        void* args[] = { roughTypeObj };
                        void* exc = nullptr;
                        sysObj = _monoRuntimeInvoke(m_getSys, nullptr, args, &exc);
                    }
                }
                if (sysObj) {
                    void* sysKlass = SafeMonoObjectGetClass(sysObj);
                    void* f_components = sysKlass ? FindFieldInHierarchy(sysKlass, OBFUSCATE("_components")) : nullptr;
                    void* dictObj = nullptr;
                    if (f_components) _monoFieldGetValue(sysObj, f_components, &dictObj);
                    if (dictObj) {
                        void* dictKlass = SafeMonoObjectGetClass(dictObj);
                        void* f_entries = dictKlass ? FindFieldInHierarchy(dictKlass, OBFUSCATE("entries")) : nullptr;
                        if (!f_entries && dictKlass) f_entries = FindFieldInHierarchy(dictKlass, OBFUSCATE("_entries"));
                        void* entriesArr = nullptr;
                        if (f_entries) _monoFieldGetValue(dictObj, f_entries, &entriesArr);
                        if (entriesArr) {
                            uintptr_t arrAddr = (uintptr_t)entriesArr;
                            int length = SafeRead<int>(arrAddr + OFF_ARR_COUNT, 0);
                            uintptr_t data = arrAddr + OFF_ARR_DATA;
                            for (int i = 0; i < length; i++) {
                                int next = SafeRead<int>(data + (i * SZ_DICT_ENTRY) + 4, -99);
                                if (next < -1) continue;
                                void* comp = *(void**)(data + (i * SZ_DICT_ENTRY) + OFF_ENTRY_VALUE);
                                if (!comp) continue;

                                void* cKlass = SafeMonoObjectGetClass(comp);
                                SandSculptureRoughComponentDataNative cdVal = { 0, 0, nullptr };
                                void* f_cd = cKlass ? _monoClassGetFieldFromName(cKlass, "_componentData") : nullptr;
                                if (f_cd) _monoFieldGetValue(comp, f_cd, (void**)&cdVal);

                                uint32_t ownerId = cdVal.ownerNetId;
                                uint32_t entNetId = 0;
                                void* m_getEnt = cKlass ? FindMethodInHierarchy(cKlass, "get_entity", 0) : nullptr;
                                if (m_getEnt) {
                                    void* eObj = _monoRuntimeInvoke(m_getEnt, comp, nullptr, nullptr);
                                    if (eObj) entNetId = NativeGetNetId(eObj);
                                }

                                if (selfNetId == 0 || ownerId == selfNetId || entNetId == playerRoughNetId) {
                                    if (entNetId != 0) res.roughNetId = entNetId;
                                    if (cdVal.options) {
                                        uintptr_t aAddr = (uintptr_t)cdVal.options;
                                        int optLen = SafeRead<int>(aAddr + OFF_ARR_COUNT, 0);
                                        if (optLen > 0 && optLen <= 64) {
                                            res.options.clear();
                                            for (int oi = 0; oi < optLen; oi++) {
                                                int optVal = SafeRead<int>(aAddr + OFF_ARR_DATA + oi * 4, 0);
                                                res.options.push_back(optVal);
                                            }
                                        }
                                    }
                                    LOGI("[Sand] ECS found roughNetId=%u, options=%zu", res.roughNetId, res.options.size());
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        }
    });
    return res;
}

// -------------------------------------------------------------------------
// Collect Finished Sculptures (Identical to Bugtopia's TryCollectOneFinishedSculpture)
// -------------------------------------------------------------------------
inline void* GetSandFinishComponentClass() {
    static void* s_klass = nullptr;
    if (s_klass) return s_klass;
    const char* imgNames[] = { "XDTLevelAndEntity", "XDTLevelAndEntity.dll", "ScriptsRefactory", "ScriptsRefactory.dll", "Client", "Client.dll" };
    for (const char* iname : imgNames) {
        void* img = _monoImageLoaded(iname);
        if (!img) continue;
        s_klass = _monoClassFromName(img, "XDTLevelAndEntity.Gameplay.Component", "SandFinishComponent");
        if (!s_klass) s_klass = _monoClassFromName(img, "ScriptsRefactory.LevelAndEntity.Gameplay.Component", "SandFinishComponent");
        if (!s_klass) s_klass = _monoClassFromName(img, "", "SandFinishComponent");
        if (s_klass) break;
    }
    return s_klass;
}

inline bool TryReadSandFinishOnNewMake(void* compObj, bool& outOnNewMake) {
    outOnNewMake = false;
    if (!compObj) return false;
    void* cKlass = SafeMonoObjectGetClass(compObj);
    if (!cKlass) return false;

    void* f_cd = FindFieldInHierarchy(cKlass, "_componentData");
    if (!f_cd) f_cd = FindFieldInHierarchy(cKlass, "componentData");
    if (f_cd) {
        SandFinishComponentDataNative cdVal = { 0 };
        _monoFieldGetValue(compObj, f_cd, (void**)&cdVal);
        outOnNewMake = (cdVal.onNewMake != 0);
        return true;
    }

    void* m_get = FindMethodInHierarchy(cKlass, "get_componentData", 0);
    if (m_get) {
        void* res = _monoRuntimeInvoke(m_get, compObj, nullptr, nullptr);
        if (res) {
            void* unboxed = _monoObjectUnbox(res);
            if (unboxed) {
                outOnNewMake = (*(uint8_t*)unboxed != 0);
                return true;
            }
        }
    }
    return false;
}

inline bool TryCollectOneFinishedSculptureNative(uint32_t& outNetId) {
    outNetId = 0;
    void* finishKlass = GetSandFinishComponentClass();
    if (!finishKlass) {
        LOGI("[Sand] SandFinishComponent class not resolved");
        return false;
    }

    std::vector<void*> components = GetComponentsOfClass(finishKlass);
    if (components.empty()) {
        return false;
    }

    for (void* comp : components) {
        if (!comp) continue;
        bool onNewMake = false;
        if (!TryReadSandFinishOnNewMake(comp, onNewMake) || !onNewMake) {
            continue;
        }

        uint32_t netId = ReadComponentNetId(comp);
        if (netId != 0) {
            LOGI("[Sand] Collecting finished sand sculpture netId=%u (onNewMake=1)", netId);
            CollectSandSculptureNative(netId);
            outNetId = netId;
            return true;
        }
    }
    return false;
}

// -------------------------------------------------------------------------
// Main Orchestrator Loop: RunAutoSandSculptureLoop
// Identical to Bugtopia's ProcessSandSculptureOnUpdate / SandApiState
// -------------------------------------------------------------------------
inline void RunAutoSandSculptureLoop() {
    LOGI("[Sand] === START BUGTOPIA-IDENTICAL AUTO SAND LOOP ===");

    std::string sandWire = WireForOp(OP_AUTO_SAND);
    if (!sandWire.empty()) {
        InvokeRequest req = FetchPayloadSync(sandWire);
        if (req.op != OP_AUTO_SAND || req.payloadRaw.empty() || !SesiAktif()) {
            LOGI("[Sand] Server authorization FAILED -> aborting Auto Sand");
            g_isAutoSandActive = false;
            return;
        }
    }

    TryInstallSandRoughSelectHook();

    int consecutiveNoBase = 0;
    std::unordered_set<uint32_t> localBlacklist;
    std::unordered_map<uint32_t, int> failCounts;

    while (g_isAutoSandActive) {
        // Keep hook active
        TryInstallSandRoughSelectHook();

        // 1. Scan for nearest sand base (radius 60m)
        SandBaseScanResult baseRes = FindNearestSandBaseNative(localBlacklist);

        if (baseRes.baseNetId == 0) {
            LOGI("[Sand] No sand base nearby. Checking backpack...");

            int wantSid = g_sandBaseStaticId;
            if (wantSid <= 0) wantSid = 600000;

            uint32_t bagItemNetId = 0;
            RunOnUnityThreadSync([&]() {
                _monoThreadAttach(_monoGetRootDomain());
                bagItemNetId = GetSeedNetId(wantSid);
                if (bagItemNetId == 0 && wantSid != 600000) {
                    bagItemNetId = GetSeedNetId(600000);
                }
            });

            if (bagItemNetId == 0) {
                LOGI("[Sand] No sand base left in backpack. Ending loop.");
                g_isAutoSandActive = false;
                break;
            }

            bool placed = PlaceSandBaseNative();
            if (placed) {
                LOGI("[Sand] Placed sand base from bag. Waiting for spawn...");
                for (int s = 0; s < 15; s++) {
                    if (!g_isAutoSandActive) break;
                    SandSleep(100);
                    baseRes = FindNearestSandBaseNative(localBlacklist);
                    if (baseRes.baseNetId != 0) {
                        LOGI("[Sand] Sand base spawned and detected! netId=%u", baseRes.baseNetId);
                        break;
                    }
                }
                if (baseRes.baseNetId == 0) {
                    consecutiveNoBase++;
                    if (consecutiveNoBase > 3) {
                        LOGI("[Sand] Failed to place / detect base 3 times. Stopping.");
                        g_isAutoSandActive = false;
                        break;
                    }
                    continue;
                }
            } else {
                consecutiveNoBase++;
                if (consecutiveNoBase > 3) {
                    LOGI("[Sand] Failed to place base 3 times. Stopping.");
                    g_isAutoSandActive = false;
                    break;
                }
                SandSleep(500);
                continue;
            }
        }

        consecutiveNoBase = 0;
        uint32_t cap = baseRes.baseNetId;
        LOGI("[Sand] Working on sand base netId=%u roughStaticId=%d", cap, baseRes.roughStaticId);

        // 2. Start making sand sculpture RPC
        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());
            SandSend1("StartMakingSandSculpture", cap);
        });

        // Bugtopia finish delay: ~0.5s (fast and safe)
        for (int d = 0; d < 5; d++) {
            if (!g_isAutoSandActive) break;
            SandSleep(100);
        }

        if (!g_isAutoSandActive) break;

        // 3. Dynamic Finish Score calculation (Bugtopia plannedSuccess / plannedPerfect)
        int rounds = GetSandQteRoundCount(baseRes.roughStaticId);
        bool qualityOpen = IsSandQualityFeatureOpen();
        int sc = 0;
        int pc = 0;
        if (qualityOpen) {
            sc = 0;
            pc = rounds; // Perfect rounds score
        } else {
            sc = 1;
            pc = 0;
        }
        if (g_sandSuccessCount > 0 || g_sandPerfectCount > 0) {
            sc = g_sandSuccessCount;
            pc = g_sandPerfectCount;
        }
        LOGI("[Sand] FinishMakingSandSculpture: base=%u rounds=%d qualityOpen=%d -> sc=%d pc=%d",
            cap, rounds, qualityOpen ? 1 : 0, sc, pc);

        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());
            SandFinishNative(cap, sc, pc);
        });

        // Immediately suppress dialog
        CloseSandModelDialogNative();

        if (!g_isAutoSandActive) break;

        // 4. Wait for Sand Rough entity & rolled product options (fast polling 50ms)
        SandRoughResult rough;
        for (int attempt = 0; attempt < 120; attempt++) {
            if (!g_isAutoSandActive) break;
            rough = FindOwnSandRoughNative();
            if (rough.roughNetId != 0 && !rough.options.empty()) {
                break;
            }
            SandSleep(50);
        }

        if (!g_isAutoSandActive) break;

        if (rough.roughNetId == 0 || rough.options.empty()) {
            LOGI("[Sand] Timeout waiting for rough entity for base %u (finish rejected?)", cap);
            failCounts[cap]++;
            if (failCounts[cap] >= 3) {
                localBlacklist.insert(cap);
                LOGI("[Sand] Base %u blacklisted after 3 failures.", cap);
            }
            SandSleep(200);
            continue;
        }

        uint32_t targetRoughNetId = rough.roughNetId;
        int pickIdx = SandPickModelOptionIndex(rough.options);
        int chosenProduct = rough.options[pickIdx];
        LOGI("[Sand] Rough netId=%u options count=%zu picked index=%d productId=%d",
            targetRoughNetId, rough.options.size(), pickIdx, chosenProduct);

        // 5. Choose Product RPC
        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());
            SandChooseProductNative(targetRoughNetId, chosenProduct);
        });
        SandSleep(100);

        if (!g_isAutoSandActive) break;

        // 6. Clear local rough data & close dialog UI
        ClearSelfSandRoughDataNative();
        CloseSandModelDialogNative();
        SandSleep(50);

        // 7. Auto Collect ONLY the finished sand sculpture (Bugtopia TryCollectOneFinishedSculpture)
        uint32_t collectedSculptureNetId = 0;
        for (int waitFinish = 0; waitFinish < 20; waitFinish++) {
            if (!g_isAutoSandActive) break;
            RunOnUnityThreadSync([&]() {
                _monoThreadAttach(_monoGetRootDomain());
                TryCollectOneFinishedSculptureNative(collectedSculptureNetId);
            });
            if (collectedSculptureNetId != 0) {
                LOGI("[Sand] Successfully collected finished sculpture netId=%u into backpack", collectedSculptureNetId);
                localBlacklist.insert(collectedSculptureNetId);
                break;
            }
            SandSleep(50);
        }

        // Blacklist the completed base to avoid repeat
        localBlacklist.insert(cap);
        localBlacklist.insert(targetRoughNetId);
        failCounts.erase(cap);
        SandSleep(100);
    }

    g_isAutoSandActive = false;
    LOGI("[Sand] === FULL AUTO SAND LOOP DONE ===");
}

inline void RunAutoSandSculptureOnce() {
    RunAutoSandSculptureLoop();
}
