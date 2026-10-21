// ============================================================================
//  FeatureDispatch.cpp — 托管方法补丁与动态调用（Main.cpp 617~1150 行移植）
// ----------------------------------------------------------------------------
//  这里是从 Android 版 Main.cpp 原样搬来的：
//    * PatchMethodReturnNop/True/False/Int：把托管方法改成常量返回
//    * ExecuteDynamic：按 "程序集/命名空间/类/方法签名" 反射调用
//    * 聊天消息 hook（Hooked_SendMessage / EnsureChatHookInstalled）
//  依赖 mono_* 兼容层（IL2CPP/MonoCompat.cpp）：iOS 上是 il2cpp 的同名能力。
// ============================================================================

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <strings.h>
#include <thread>
#include <vector>

#include "ModFeatures.h"
#include "MonoGameApi.h"
#include "NetAuth.h"
#include "Weiyan.h"
#include "LocalCatalog.h"
#include "PayloadVM.h"
#include "ModUtils.h"
#include "Core/State.h"
#include "Global.h"
#include "Includes/Dobby/dobby.h"
#include "Includes/Logger.h"
#include "Includes/OSCache.h"
#include "Includes/obfuscate.h"
#include "MonoString.h"
#include "VirboxTargets.h"

// Clang/GCC 有内建指令缓存刷新；MSVC（只用于本地语法自检）没有，降级为空。
#if !defined(__clang__) && !defined(__GNUC__)
#ifndef __builtin___clear_cache
#define __builtin___clear_cache(begin, end) ((void)0)
#endif
#endif

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/imgui.h"
bool PatchMethodReturnInt(const char *asmName, const char *ns, const char *clsName,
                          const char *methodName, int paramCount, int returnValue) {
    if (!_monoCompileMethod) { LOGE("[PATCH] _monoCompileMethod NULL (symbol tak ada)"); return false; }
    void *img = _monoImageLoaded(asmName);
    std::string assemblyWithDll;
    if (!img) {
        const size_t len = strlen(asmName);
        const bool hasDll = len >= 4 && strcasecmp(asmName + len - 4, ".dll") == 0;
        if (!hasDll) {
            assemblyWithDll = std::string(asmName) + ".dll";
            img = _monoImageLoaded(assemblyWithDll.c_str());
        }
    }
    if (!img) return false;
    void *klass = _monoClassFromName(img, ns, clsName);
    if (!klass)  { LOGE("[PATCH] klass NULL: %s.%s", ns, clsName); return false; }
    void *method = _monoClassGetMethodFromName(klass, methodName, paramCount);
    if (!method) {
        method = FindMethodInHierarchy(klass, methodName, paramCount);
    }
    if (!method) { LOGE("[PATCH] method NULL: %s(%d)", methodName, paramCount); return false; }
    void *nativeAddress = _monoCompileMethod(method);
    LOGI("[PATCH] %s -> method=%p native=%p", methodName, method, nativeAddress);
    if (!nativeAddress) { LOGE("[PATCH] nativeAddress NULL (IL2CPP: mono_compile_method gagal)"); return false; }
    uint32_t payload[2] = { (uint32_t)(0x52800000u | (returnValue << 5)), 0xD65F03C0u };
    if (!VirtualProtect(nativeAddress, sizeof(payload))) { LOGE("[PATCH] mprotect gagal"); return false; }
    memcpy(nativeAddress, payload, sizeof(payload));
    sys_icache_invalidate(nativeAddress, sizeof(payload));
    LOGI("[PATCH][OK] %s.%s -> %d", clsName, methodName, returnValue);
    return true;
}

// Membuat method void langsung kembali pada ARM64. Nama ReturnNop dipertahankan
// agar sama dengan versi PC, tetapi instruksi yang benar adalah RET.
bool PatchMethodReturnNop(const char *asmName, const char *ns, const char *clsName,
                          const char *methodName, int paramCount) {
    if (!_monoImageLoaded || !_monoClassFromName ||
        !_monoClassGetMethodFromName || !_monoCompileMethod) {
        LOGE("[PatchReturnNop] Mono API belum siap");
        return false;
    }

    void *img = _monoImageLoaded(asmName);
    std::string assemblyWithDll;
    if (!img) {
        const size_t len = strlen(asmName);
        const bool alreadyHasDll = len >= 4 && strcasecmp(asmName + len - 4, ".dll") == 0;
        if (!alreadyHasDll) {
            assemblyWithDll = std::string(asmName) + ".dll";
            img = _monoImageLoaded(assemblyWithDll.c_str());
        }
    }
    if (!img) {
        LOGE("[PatchReturnNop] assembly NULL: %s", asmName);
        return false;
    }

    void *klass = _monoClassFromName(img, ns, clsName);
    if (!klass) {
        LOGE("[PatchReturnNop] class NULL: %s.%s", ns, clsName);
        return false;
    }

    void *method = _monoClassGetMethodFromName(klass, methodName, paramCount);
    if (!method) {
        method = FindMethodInHierarchy(klass, methodName, paramCount);
    }
    if (!method) {
        LOGE("[PatchReturnNop] method NULL: %s.%s::%s/%d",
             ns, clsName, methodName, paramCount);
        return false;
    }

    void *nativeAddress = _monoCompileMethod(method);
    if (!nativeAddress) {
        LOGE("[PatchReturnNop] native address NULL: %s::%s/%d",
             clsName, methodName, paramCount);
        return false;
    }

#if defined(__aarch64__)
    const uint32_t payload = 0xD65F03C0u; // RET
    const size_t payloadSize = sizeof(payload);
#else
    LOGE("[PatchReturnNop] ABI tidak didukung; fitur ini membutuhkan arm64-v8a");
    return false;
#endif

    if (!VirtualProtect(nativeAddress, payloadSize)) {
        LOGE("[PatchReturnNop] mprotect gagal @ %p", nativeAddress);
        return false;
    }

    memcpy(nativeAddress, &payload, payloadSize);
    sys_icache_invalidate(nativeAddress, payloadSize);
//    LOGI("[PatchReturnNop] Berhasil: %s | %s.%s::%s/%d @ %p",
//         asmName, ns, clsName, methodName, paramCount, nativeAddress);
    return true;
}

void AutoPatchReturnNopNative() {
    struct NopTarget {
        const char *assemblyName;
        const char *nameSpace;
        const char *className;
        const char *methodName;
        int paramCount;
    };

    static const NopTarget targets[] = {
            {"MsgPackFormatters", "Message.Formatters.XDT.Scene.Shared.Data.Scriptable",
                    PATCH_1, "Serialize", 3},
            {"MsgPackFormatters", "Message.Formatters.XDT.Scene.Shared.Data.Scriptable",
                    PATCH_1, "Deserialize", 2},
            {"MsgPackFormatters", "Message.Formatters.XDT.Scene.Shared.Data.Scriptable",
                    PATCH_2, "Serialize", 3},
            {"MsgPackFormatters", "Message.Formatters.XDT.Scene.Shared.Data.Scriptable",
                    PATCH_2, "Deserialize", 2},
    };

    int successCount = 0;
    for (const NopTarget &target : targets) {
        if (PatchMethodReturnNop(target.assemblyName, target.nameSpace,
                                 target.className, target.methodName,
                                 target.paramCount)) {
            ++successCount;
        } else {
//            LOGE("[AutoPatchReturnNop] Gagal: %s | %s.%s::%s/%d",
//                 target.assemblyName, target.nameSpace, target.className,
//                 target.methodName, target.paramCount);
        }
    }

    LOGI("[AutoPatchReturnNop] Selesai: %d/%zu target berhasil",
         successCount, sizeof(targets) / sizeof(targets[0]));
}

bool PatchMethodReturnTrue(const char *asmName, const char *ns, const char *clsName,
                           const char *methodName, int paramCount) {
    try {
        void *img = _monoImageLoaded(asmName);
        std::string assemblyWithDll;
        if (!img) {
            const size_t len = strlen(asmName);
            const bool hasDll = len >= 4 && strcasecmp(asmName + len - 4, ".dll") == 0;
            if (!hasDll) {
                assemblyWithDll = std::string(asmName) + ".dll";
                img = _monoImageLoaded(assemblyWithDll.c_str());
            }
        }
        if (!img) return false;

        void *klass = _monoClassFromName(img, ns, clsName);
        if (!klass) return false;
        void *method = _monoClassGetMethodFromName(klass, methodName, paramCount);
        if (!method) {
            method = FindMethodInHierarchy(klass, methodName, paramCount);
        }
        if (!method) return false;
        void *nativeAddress = _monoCompileMethod(method);
        if (!nativeAddress) return false;

        uint32_t payload[2];
        payload[0] = 0x52800020;
        payload[1] = 0xD65F03C0;
        if (VirtualProtect(nativeAddress, sizeof(payload))) {
            memcpy(nativeAddress, payload, sizeof(payload));
            sys_icache_invalidate(nativeAddress, sizeof(payload));
            LOGI(OBFUSCATE("<color=lime>[OK]</color> %s.%s -> Force TRUE!"), clsName, methodName);
            return true;
        }
    }
    catch (...) {
        LOGI(OBFUSCATE("<color=red>[ERROR]</color> Crash saat patch '%s'"), methodName);
    }
    return false;
}

bool PatchMethodReturnFalse(const char *asmName, const char *ns, const char *clsName,
                            const char *methodName, int paramCount) {
    try {
        void *img = _monoImageLoaded(asmName);
        std::string assemblyWithDll;
        if (!img) {
            const size_t len = strlen(asmName);
            const bool hasDll = len >= 4 && strcasecmp(asmName + len - 4, ".dll") == 0;
            if (!hasDll) {
                assemblyWithDll = std::string(asmName) + ".dll";
                img = _monoImageLoaded(assemblyWithDll.c_str());
            }
        }
        if (!img) return false;

        void *klass = _monoClassFromName(img, ns, clsName);
        if (!klass) return false;
        void *method = _monoClassGetMethodFromName(klass, methodName, paramCount);
        if (!method) {
            method = FindMethodInHierarchy(klass, methodName, paramCount);
        }
        if (!method) return false;
        void *nativeAddress = _monoCompileMethod(method);
        if (!nativeAddress) return false;

        // --- PAYLOAD ARM64 UNTUK RETURN FALSE ---
        uint32_t payload[2];
        payload[0] = 0x52800000; // MOV W0, #0  (Set return value menjadi 0 / false)
        payload[1] = 0xD65F03C0; // RET         (Kembali dari fungsi)

        if (VirtualProtect(nativeAddress, sizeof(payload))) {
            memcpy(nativeAddress, payload, sizeof(payload));
            sys_icache_invalidate(nativeAddress, sizeof(payload));
            LOGI(OBFUSCATE("<color=lime>[OK]</color> %s.%s -> Force FALSE!"), clsName, methodName);
            return true;
        }
    }
    catch (...) {
        LOGI(OBFUSCATE("<color=red>[ERROR]</color> Crash saat patch '%s'"), methodName);
    }
    return false;
}

extern "C" VIRBOX_TARGET_ATTR void *Virbox_ExecuteDynamic(
        std::string dllName, std::string nameSpace, std::string className,
        std::string methodName, std::string paramInput, bool isStatic,
        std::string instancePtrHexStr) {
    TrimString(dllName);
    TrimString(nameSpace);
    TrimString(className);
    TrimString(methodName);
    if (dllName == (const char *) OBFUSCATE("PATCH")) {
        // Pastikan thread tersambung ke IL2CPP sebelum patching
        if (_monoGetRootDomain)
            _monoThreadAttach(_monoGetRootDomain());
        // Pisahkan banyak patch sekaligus berdasarkan karakter '#'
        std::stringstream ssPatches(paramInput);
        std::string singlePatch;
        while (std::getline(ssPatches, singlePatch, '#')) {
            TrimString(singlePatch);
            if (singlePatch.empty())
                continue;
            // Pisahkan detail setiap patch berdasarkan karakter '|'
            std::stringstream ssArgs(singlePatch);
            std::string pAsm, pNs, pCls, pMeth, pCountStr, pType, pValStr;
            std::getline(ssArgs, pAsm, '|');
            std::getline(ssArgs, pNs, '|');
            std::getline(ssArgs, pCls, '|');
            std::getline(ssArgs, pMeth, '|');
            std::getline(ssArgs, pCountStr, '|');
            std::getline(ssArgs, pType, '|');
            std::getline(ssArgs, pValStr, '|'); // Jika tipenya 'bool', ini akan kosong

            int pCount = 0;
            try {
                pCount = std::stoi(pCountStr);
            }
            catch (...) {
            }

            if (pType == (const char *) OBFUSCATE("bool")) {
                int pVal = 1;
                try { if (!pValStr.empty()) pVal = std::stoi(pValStr); } catch (...) { pVal = 1; }
                if (pVal == 0)
                    PatchMethodReturnFalse(pAsm.c_str(), pNs.c_str(), pCls.c_str(), pMeth.c_str(), pCount);
                else
                    PatchMethodReturnTrue(pAsm.c_str(), pNs.c_str(), pCls.c_str(), pMeth.c_str(), pCount);
            } else if (pType == (const char *) OBFUSCATE("int")) {
                int pVal = 0;
                try {
                    pVal = std::stoi(pValStr);
                }
                catch (...) {
                }
                PatchMethodReturnInt(pAsm.c_str(), pNs.c_str(), pCls.c_str(), pMeth.c_str(), pCount,
                                     pVal);
            }
        }
        return nullptr;
        // Hentikan fungsi di sini karena ini adalah Patch, bukan Invoke biasa
    }
    // ========================================================
    if (!methodName.empty() && methodName[0] == ':') {
        methodName = methodName.substr(1);
    }
    size_t parenPos = methodName.find('(');
    if (parenPos != std::string::npos) {
        methodName = methodName.substr(0, parenPos);
    }

    if (methodName == (const char *) OBFUSCATE("AutoMoveTransfer") || methodName == (const char *) OBFUSCATE("Transfer2Entity")) {
        EnsureTeleportAntiCrashPatched();
    }

    if (className == (const char *) OBFUSCATE("EntityHelper") && methodName == (const char *) OBFUSCATE("AutoMoveTransfer")) {
        Vector3 targetPos = { 0.0f, 0.0f, 0.0f };
        std::string clean = paramInput;
        if (clean.length() >= 3 && (clean.substr(0, 3) == "v3:" || clean.substr(0, 3) == "V3:")) clean = clean.substr(3);
        std::replace(clean.begin(), clean.end(), ',', '.');
        std::vector<std::string> nums;
        size_t nS = 0, nE;
        while ((nE = clean.find_first_of("|;", nS)) != std::string::npos) {
            nums.push_back(clean.substr(nS, nE - nS));
            nS = nE + 1;
        }
        if (nS < clean.length()) nums.push_back(clean.substr(nS));
        try { if (nums.size() > 0) targetPos.x = std::stof(nums[0]); } catch (...) {}
        try { if (nums.size() > 1) targetPos.y = std::stof(nums[1]); } catch (...) {}
        try { if (nums.size() > 2) targetPos.z = std::stof(nums[2]); } catch (...) {}

        AutoMoveTransferDirect(targetPos);
        return nullptr;
    }

    if (className == (const char *) OBFUSCATE("EntityHelper") && methodName == (const char *) OBFUSCATE("Transfer2Entity")) {
        uint32_t netId = 0;
        try { if (!paramInput.empty()) netId = (uint32_t)std::stoul(paramInput); } catch (...) {}
        if (netId != 0) {
            Transfer2EntityDirect(netId);
            return nullptr;
        }
    }

    if (dllName.length() >= 4 &&
        dllName.substr(dllName.length() - 4) == (const char *) OBFUSCATE(".dll")) {
        dllName = dllName.substr(0, dllName.length() - 4);
    }

    void *domain = _monoGetRootDomain();
    if (!domain) {
        LOGE(OBFUSCATE("[INVOKE] Root Domain NULL"));
        return nullptr;
    }

    _monoThreadAttach(domain);

    void *image = _monoImageLoaded(dllName.c_str());
    if (!image) {
        LOGE(OBFUSCATE("[INVOKE] Gagal load Assembly: %s"), dllName.c_str());
        return nullptr;
    }

    void *targetClass = _monoClassFromName(image, nameSpace.c_str(), className.c_str());
    if (!targetClass) {
        LOGE(OBFUSCATE("[INVOKE] Class '%s' tidak ditemukan"), className.c_str());
        return nullptr;
    }

    std::string safeInput = "";
    for (size_t i = 0; i < paramInput.length(); i++) {
        if (paramInput[i] == ',') {
            bool leftIsDigit = (i > 0 && isdigit(paramInput[i - 1]));
            bool rightIsDigit = (i < paramInput.length() - 1 && isdigit(paramInput[i + 1]));
            if (leftIsDigit && rightIsDigit) {
                safeInput += '.';
            } else {
                safeInput += ';';
            }
        } else {
            safeInput += paramInput[i];
        }
    }

    std::vector<void *> args;
    std::vector<void *> allocated;
    if (!safeInput.empty()) {
        std::stringstream ss(safeInput);
        std::string token;
        while (std::getline(ss, token, ';')) {
            TrimString(token);
            if (token.empty())
                continue;
            if (token.rfind((const char *) OBFUSCATE("v3:"), 0) == 0) {
                std::string coords = token.substr(3);
                std::stringstream ss2(coords);
                std::string num;
                float *v3 = (float *) malloc(sizeof(float) * 3);
                int i = 0;
                while (std::getline(ss2, num, '|') && i < 3) {
                    TrimString(num);
                    std::replace(num.begin(), num.end(), ',', '.');
                    try {
                        v3[i++] = std::stof(num);
                    }
                    catch (...) {
                    }
                }
                args.push_back(v3);
                allocated.push_back(v3);
                continue;
            }

            if (token.back() == 'f' || token.back() == 'F' ||
                token.find('.') != std::string::npos || token.find(',') != std::string::npos) {
                float *val = (float *) malloc(sizeof(float));
                std::string cleanFloat = token;
                if (cleanFloat.back() == 'f' || cleanFloat.back() == 'F')
                    cleanFloat.pop_back();
                std::replace(cleanFloat.begin(), cleanFloat.end(), ',', '.');
                try {
                    *val = std::stof(cleanFloat);
                }
                catch (...) {
                    *val = 0.0f;
                }
                args.push_back(val);
                allocated.push_back(val);
                continue;
            }

            if (isdigit(token[0]) || (token[0] == '-' && isdigit(token[1]))) {
                std::string cleanInt = token;
                if (cleanInt.back() == 'u' || cleanInt.back() == 'U')
                    cleanInt.pop_back();
                uint32_t *val = (uint32_t *) malloc(sizeof(uint32_t));
                try {
                    *val = (uint32_t) std::stoull(cleanInt);
                }
                catch (...) {
                    *val = 0;
                }
                args.push_back(val);
                allocated.push_back(val);
                continue;
            }

            if (token == (const char *) OBFUSCATE("true") ||
                token == (const char *) OBFUSCATE("false")) {
                bool *val = (bool *) malloc(sizeof(bool));
                *val = (token == (const char *) OBFUSCATE("true"));
                args.push_back(val);
                allocated.push_back(val);
                continue;
            }

            void *monoStr = _monoStringNew(domain, token.c_str());
            args.push_back(monoStr);
        }
    }

    int paramCount = args.size();
    void *method = FindMethodInHierarchy(targetClass, methodName.c_str(), paramCount);
    if (!method) {
        LOGE(OBFUSCATE("[INVOKE] Method '%s' dengan %d parameter tidak ditemukan"),
             methodName.c_str(), paramCount);
        for (void *p: allocated)
            free(p);
        return nullptr;
    }

    void **invokeArgs = args.empty() ? nullptr : args.data();
    void *objPtr = nullptr;
    if (!isStatic) {
        if (!instancePtrHexStr.empty())
            objPtr = (void *) strtoull(instancePtrHexStr.c_str(), nullptr, 16);
        if (!objPtr) {
            LOGE(OBFUSCATE("[INVOKE] Butuh Instance Pointer untuk method non-static!"));
            for (void *p: allocated)
                free(p);
            return nullptr;
        }
    }

    void *exc = nullptr;
    void *finalResult = _monoRuntimeInvoke(method, objPtr, invokeArgs, &exc);

    if (exc) {
        LOGE(OBFUSCATE("[INVOKE] Terjadi EXCEPTION dari dalam game saat mengeksekusi Method!"));
    }

    for (void *p: allocated)
        free(p);
    return finalResult;
}

void *ExecuteDynamic(std::string dllName, std::string nameSpace, std::string className,
                     std::string methodName, std::string paramInput, bool isStatic,
                     std::string instancePtrHexStr) {
    return Virbox_ExecuteDynamic(std::move(dllName), std::move(nameSpace),
            std::move(className), std::move(methodName), std::move(paramInput),
            isStatic, std::move(instancePtrHexStr));
}

// ==========================================================
// AUTO COLOR CHAT (RICH TEXT UNICODE BYPASS - ANDROID)
// ==========================================================
inline std::string FormatColorChatMessage(const std::string& inputMessage) {
    if (inputMessage.empty()) return "";

    static const char* s_colorHexes[8] = {
        "#FF0000", // Red
        "#FFC0CB", // Pink
        "#0088FF", // Blue
        "#00CC44", // Green
        "#FFDD00", // Yellow
        "#BB44FF", // Purple
        "#FF8800", // Orange
        "#FFFFFF"  // White
    };

    std::string chosenHex = "#0088FF";
    if (g_chatColorMode == 0) {
        int r = rand() % 8;
        chosenHex = s_colorHexes[r];
    }
    else if (g_chatColorMode >= 1 && g_chatColorMode <= 8) {
        chosenHex = s_colorHexes[g_chatColorMode - 1];
    }
    else if (g_chatColorMode == 9) {
        std::string hex = g_chatCustomHex;
        if (!hex.empty() && hex[0] != '#') hex = "#" + hex;
        if (hex.length() == 7) chosenHex = hex;
        else chosenHex = "#00FFFF";
    }

    std::string formatted = u8"<ɣůŬůɲ=" + chosenHex + u8">";
    formatted += inputMessage;
    formatted += u8"</ɣůŬůɲ>";

    return formatted;
}

inline std::string MonoStringToUTF8(void* monoStr) {
    if (!monoStr) return "";
    monoString* ms = (monoString*)monoStr;
    return ms->get_string();
}

typedef void (*SendMessage_t)(void* messageMonoStr, int32_t channelType);
static SendMessage_t g_origSendMessage = nullptr;
static std::atomic<bool> g_chatHookInstalled{ false };
static std::atomic<bool> g_chatHookingInProgress{ false };

static void Hooked_SendMessage(void* messageMonoStr, int32_t channelType) {
    static thread_local bool s_inHook = false;
    if (s_inHook || !g_autoColorChatActive || !messageMonoStr) {
        if (g_origSendMessage) g_origSendMessage(messageMonoStr, channelType);
        return;
    }

    s_inHook = true;

    std::string text = MonoStringToUTF8(messageMonoStr);
    if (text.empty() || text.find(u8"<ɣůŬůɲ=") != std::string::npos) {
        if (g_origSendMessage) g_origSendMessage(messageMonoStr, channelType);
        s_inHook = false;
        return;
    }

    std::string formatted = FormatColorChatMessage(text);
    void* domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (domain && _monoThreadAttach) {
        _monoThreadAttach(domain);
    }

    void* newMonoStr = (domain && _monoStringNew) ? _monoStringNew(domain, formatted.c_str()) : nullptr;
    if (newMonoStr && g_origSendMessage) {
        g_origSendMessage(newMonoStr, channelType);
    } else if (g_origSendMessage) {
        g_origSendMessage(messageMonoStr, channelType);
    }

    s_inHook = false;
}

bool EnsureChatHookInstalled() {
    if (g_chatHookInstalled.load(std::memory_order_acquire)) return true;
    if (g_chatHookingInProgress.exchange(true)) return false;

    void* domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!domain) {
        g_chatHookingInProgress.store(false);
        return false;
    }
    if (_monoThreadAttach) _monoThreadAttach(domain);

    void* img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (!img) {
        g_chatHookingInProgress.store(false);
        return false;
    }

    void* klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Social"), OBFUSCATE("ChatProtocolManager"));
    if (!klass) {
        g_chatHookingInProgress.store(false);
        return false;
    }

    void* method = _monoClassGetMethodFromName(klass, OBFUSCATE("SendMessage"), 2);
    if (!method) {
        g_chatHookingInProgress.store(false);
        return false;
    }

    void* nativeAddr = _monoCompileMethod ? _monoCompileMethod(method) : nullptr;
    if (!nativeAddr) {
        g_chatHookingInProgress.store(false);
        return false;
    }

    int hookRes = DobbyHook(nativeAddr, (void*)Hooked_SendMessage, (void**)&g_origSendMessage);
    if (hookRes == 0 || g_origSendMessage != nullptr) {
        VirtualProtect(nativeAddr, 0x1000);
        g_chatHookInstalled.store(true, std::memory_order_release);
        g_chatHookingInProgress.store(false);
        LOGI(OBFUSCATE("[AutoColorChat] Hooked ChatProtocolManager::SendMessage successfully (native=%p)"), nativeAddr);
        return true;
    }

    g_chatHookingInProgress.store(false);
    return false;
}

void ExecuteTableSwappingNative(std::string dllName, std::string nameSpace, std::string className,
                                int myCarId, int targetCarId) {
    _monoThreadAttach(_monoGetRootDomain());

    // 1. Cari Class TableData menggunakan data rahasia dari Backend
    void *imgProtocol = _monoImageLoaded(dllName.c_str());
    if (!imgProtocol)
        return;

    void *klassTableData = _monoClassFromName(imgProtocol, nameSpace.c_str(), className.c_str());
    if (!klassTableData) {
        // LOGE("[-] Class %s tidak ditemukan!", className.c_str());
        return;
    }

    // 2. Ambil Static Field 'TableCars'
    void *f_TableCars = _monoClassGetFieldFromName(klassTableData, OBFUSCATE("TableCars"));
    if (!f_TableCars) {
        // LOGE("[-] Field TableCars tidak ditemukan!");
        return;
    }

    void *vtable = _monoClassGetVTable(_monoGetRootDomain(), klassTableData);
    void *dictPtr = nullptr;
    _monoFieldStaticGetValue(vtable, f_TableCars, &dictPtr);

    if (!dictPtr) {
        // LOGE("[-] Dictionary TableCars masih kosong (null)!");
        return;
    }

    // 3. Bongkar Array _entries di dalam Dictionary
    void *dictKlass = _monoObjectGetClass(dictPtr);
    void *f_entries = _monoClassGetFieldFromName(dictKlass, OBFUSCATE("_entries"));
    if (!f_entries)
        f_entries = _monoClassGetFieldFromName(dictKlass, OBFUSCATE("entries"));
    if (!f_entries)
        return;

    void *entriesArray = nullptr;
    _monoFieldGetValue(dictPtr, f_entries, &entriesArray);
    if (!entriesArray)
        return;

    int arrayCap = *(int *) ((uint8_t *) entriesArray + 0x18);
    uint8_t *baseEntryPtr = (uint8_t *) entriesArray + 0x20;

    void *targetObjPtr = nullptr;
    void *myObjAddr = nullptr;

    // 4. Looping mencari Key dan mengekstrak Pointer
    for (int i = 0; i < arrayCap; i++) {
        int hashCode = *(int *) (baseEntryPtr + (i * 24));
        if (hashCode >= 0) {
            int key = *(int *) (baseEntryPtr + (i * 24) + 8);

            if (key == targetCarId) {
                targetObjPtr = *(void **) (baseEntryPtr + (i * 24) + 16);
            } else if (key == myCarId) {
                myObjAddr = (void *) (baseEntryPtr + (i * 24) + 16);
            }
        }
    }

    // 5. EKSEKUSI PENUKARAN (SWAPPING DI RAM)
    if (targetObjPtr && myObjAddr) {
        *(void **) myObjAddr = targetObjPtr;
        // LOGI("[+] BINGO! Data %d di RAM berhasil ditimpa dengan %d!", myCarId, targetCarId);
        g_vehicleSwapSuccess = true;
        g_vehicleSwapTime = std::chrono::steady_clock::now();
    } else {
        // LOGE("[-] Gagal menukar. Pastikan ID MyCar (%d) dan TargetCar (%d) valid.", myCarId, targetCarId);
    }
}


struct DummyVector2Int {
    int x;
    int y;
};

int g_selectedImageIdx = 0;
std::string g_drawStatus = u8"未开始";

// AutoDraw.cpp 通过这个写状态（菜单里显示的就是它）
void SetDrawStatusText(const std::string &text) {
    std::lock_guard<std::mutex> lock(g_emoteUnlockMutex);
    g_drawStatus = text;
}

// ---------------------------------------------------------------------------
//  自动绘画：名字来源与离线兜底
// ---------------------------------------------------------------------------
// 这个功能原本完全依赖服务器下发的 op=20 payload（一份"键 -> 真实类名/方法名"的
// JSON）。当前离线构建的 dump 里**没有** op=20 的信封 —— 见 LocalCatalogData.h 的
// kEmbeddedMissingPayloadWires，9db079ae8797 就在里面。于是 g_drawPn 是空的，
// ExecuteInjectJpgToCanvasNative 第一行 `if (g_drawPn.empty()) return;` 直接退出：
// 按了没反应，也不报错。这就是"自动绘画没效果"的原因。
//
// 兜底策略：
//   * UnityEngine 那一组名字是固定的（Texture2D / get_width / GetRawTextureData…），
//     直接内置；
//   * 游戏侧那几个类（CanvasPainter / DrawingPanel / UIManager）和成员名，payload
//     里没有就按"全镜像找类 + 名字子串找成员"再找一遍（见 DrawFind* 辅助）。
// payload 里有的键优先用 payload。
static const char *DrawNameDefault(const char *key) {
    if (!key) return nullptr;
    struct DrawNamePair {
        const char *key;
        const char *value;
    };
    static const DrawNamePair kDefaults[] = {
            {"n_ueNs", "UnityEngine"},
            {"n_imgEngine", "UnityEngine.CoreModule"},
            {"n_imgCoreModule", "UnityEngine.CoreModule"},
            {"n_texture", "Texture"},
            {"n_texture2d", "Texture2D"},
            {"n_getWidth", "get_width"},
            {"n_getHeight", "get_height"},
            {"n_getFormat", "get_format"},
            {"n_getRawTex", "GetRawTextureData"},
            {"n_imageConversion", "ImageConversion"},
            {"n_loadImage", "LoadImage"},
            {"n_imgMscorlib", "mscorlib"},
            {"n_imgCoreLib", "System.Private.CoreLib"},
            {"n_sysNs", "System"},
            {"n_byte", "Byte"},
            {"n_imgLevel", "XDTLevelAndEntity"},
            {"n_imgAsmCs", "Assembly-CSharp"},
            {"n_imgGameUI", "XDTGameUI"},
            {"n_canvasPainter", "CanvasPainter"},
            {"n_createAlpha", "CreateAlphaTexture"},
            {"n_uiManager", "UIManager"},
            {"n_getInstance", "get_Instance"},
            {"n_drawingPanel", "DrawingPanel"},
            {"n_getView", "GetView"},
            {"n_fPainter", "painter"},
            {"n_fDrawRegion", "DrawRegion"},
            {"n_fDrawRegionBack", "<DrawRegion>k__BackingField"},
            {"n_beginDraw", "BeginDraw"},
            {"n_enableSave", "EnableSave"},
            {"n_setPixel", "SetPixelColor"},
            {"n_recordChange", "RecordChange"},
            {"n_apply", "Apply"},
    };
    for (const DrawNamePair &pair: kDefaults) {
        if (strcmp(pair.key, key) == 0) return pair.value;
    }
    return nullptr;
}

// 类查找：有命名空间就正规查；没有（payload 没给）就按简单类名全镜像找。
static void *DrawFindClass(void *image, const char *nameSpace, const char *className) {
    if (!className || !*className) return nullptr;
    if (image && nameSpace && *nameSpace) {
        void *klass = _monoClassFromName(image, nameSpace, className);
        if (klass) return klass;
    }
    return MonoApi::FindClass(nullptr, 0, nameSpace ? nameSpace : "", className, nullptr, true);
}

// 方法查找：先按名字+参数个数；找不到再按"名字含子串"扫一遍同类方法。
static void *DrawFindMethod(void *klass, const char *methodName, int argCount,
                            const char *nameSubstring) {
    if (!klass) return nullptr;
    if (methodName && *methodName) {
        void *method = FindMethodInHierarchy(klass, methodName, argCount);
        if (method) return method;
    }
    if (!nameSubstring || !*nameSubstring) return nullptr;
    MonoApi::MonoClassGetMethodsFn getMethods = MonoApi::ClassGetMethods();
    MonoApi::MonoMethodGetNameFn getName = MonoApi::MethodGetName();
    if (!getMethods || !getName) return nullptr;
    void *iter = nullptr;
    void *candidate = nullptr;
    while ((candidate = getMethods(klass, &iter)) != nullptr) {
        const char *name = getName(candidate);
        if (!name || !strstr(name, nameSubstring)) continue;
        if (argCount >= 0 && MonoApi::MethodParamCount(candidate) != argCount) continue;
        return candidate;
    }
    return nullptr;
}

// 字段查找：先按名字；找不到再按"名字含子串"扫一遍同类字段。
static void *DrawFindField(void *klass, const char *fieldName, const char *nameSubstring) {
    if (!klass) return nullptr;
    if (fieldName && *fieldName) {
        void *field = FindFieldInHierarchy(klass, fieldName);
        if (field) return field;
    }
    if (!nameSubstring || !*nameSubstring || !_monoClassGetFields || !_monoFieldName) {
        return nullptr;
    }
    void *iter = nullptr;
    void *field = nullptr;
    while ((field = _monoClassGetFields(klass, &iter)) != nullptr) {
        const char *name = _monoFieldName(field);
        if (name && strstr(name, nameSubstring)) return field;
    }
    return nullptr;
}



// 取名字：payload 优先，缺了落内置默认（离线 dump 没有 op=20 的信封）。
static inline std::string DrawN(const char* k) {
    std::string value = extractJsonString(g_drawPn, k);
    if (!value.empty()) return value;
    const char* fallback = DrawNameDefault(k);
    return fallback ? std::string(fallback) : std::string();
}

void ExecuteInjectJpgToCanvasNative(std::string imagePath) {
    // 离线构建没有 op=20 的 payload：不再直接 return，改成用内置默认名继续跑，
    // 每一步的结果写进 g_drawStatus（菜单里能看到），不再"点了没反应"。
    if (g_drawPn.empty()) {
        LOGI(OBFUSCATE("[DRAW] op=20 payload 缺失（离线 dump 没这个信封），用内置默认名"));
    }
    g_drawStatus = u8"正在注入图片…";
    // Tangkap imagePath (by value) ke dalam lambda agar aman di thread terpisah
    RunOnUnityThreadAsync([imagePath]() {
        void *domain = _monoGetRootDomain();
        if (!domain) return;
        _monoThreadAttach(domain);

        LOGI(OBFUSCATE("[*] MEMULAI INJEKSI GAMBAR (METODE TARGETED REGION)..."));

        void *exc = nullptr;
        void *imgCore = _monoImageLoaded(DrawN("n_imgEngine").c_str());
        if (!imgCore) imgCore = _monoImageLoaded(DrawN("n_ueNs").c_str());
        void *imgCoreModule = _monoImageLoaded(DrawN("n_imgCoreModule").c_str());
        if (!imgCoreModule) imgCoreModule = imgCore;

        void *imgUI = _monoImageLoaded(DrawN("n_imgLevel").c_str());
        if (!imgUI) imgUI = _monoImageLoaded(DrawN("n_imgAsmCs").c_str());
        void *imgGamePanel = _monoImageLoaded(DrawN("n_imgGameUI").c_str());
        // --- CHECKPOINT 1: AMBIL CONFIG & LUT ---
        if (!imgCore) {
            LOGE(OBFUSCATE("[DRAW] UnityEngine/EngineWrapper DLL tidak loaded"));
            return;
        }
        if (!imgUI) {
            LOGE(OBFUSCATE("[DRAW] XDTLevelAndEntity DLL tidak loaded"));
            return;
        }
        if (!imgGamePanel) {
            LOGE(OBFUSCATE("[DRAW] XDTGameUI DLL tidak loaded"));
            return;
        }

        void *drawingConfigInst = GetDrawingConfigInstanceNative();
        if (!drawingConfigInst) {
            LOGE(OBFUSCATE("[DRAW] DrawingConfig null"));
            return;
        }

        void *colorLutTexObj = ExtractColorLutTextureNative(drawingConfigInst);
        if (!colorLutTexObj) {
            LOGE(OBFUSCATE("[DRAW] ColorLut texture null (belum di-load game)"));
            return;
        }

        void *textureKlass = DrawFindClass(imgCoreModule, DrawN("n_ueNs").c_str(),
                                           DrawN("n_texture").c_str());
        void *texture2DKlass = DrawFindClass(imgCoreModule, DrawN("n_ueNs").c_str(),
                                             DrawN("n_texture2d").c_str());
        if (!textureKlass || !texture2DKlass) {
            g_drawStatus = u8"失败：找不到 UnityEngine.Texture/Texture2D";
            LOGE(OBFUSCATE("[DRAW] Texture/Texture2D tidak ketemu"));
            return;
        }
        void *m_getWidth = DrawFindMethod(textureKlass, DrawN("n_getWidth").c_str(), 0, "width");
        void *m_getHeight = DrawFindMethod(textureKlass, DrawN("n_getHeight").c_str(), 0, "height");
        void *m_getFormat = DrawFindMethod(texture2DKlass, DrawN("n_getFormat").c_str(), 0, "format");
        void *m_getRawTexData = DrawFindMethod(texture2DKlass, DrawN("n_getRawTex").c_str(), 0,
                                               "RawTexture");

        if (!m_getWidth || !m_getHeight || !m_getFormat || !m_getRawTexData) {
            g_drawStatus = u8"失败：Texture2D 的 width/height/format/RawTextureData 取不到";
            LOGE(OBFUSCATE("[DRAW] Texture 方法不全"));
            return;
        }

        int lutWidth = *(int *) _monoObjectUnbox(
                _monoRuntimeInvoke(m_getWidth, colorLutTexObj, nullptr, &exc));
        int lutFormat = *(int *) _monoObjectUnbox(
                _monoRuntimeInvoke(m_getFormat, colorLutTexObj, nullptr, &exc));
        int lutBpp = (lutFormat == 3) ? 3 : 4;

        void *lutRawDataObj = _monoRuntimeInvoke(m_getRawTexData, colorLutTexObj, nullptr, &exc);
        if (!lutRawDataObj) {
            LOGE(OBFUSCATE("[-] Gagal mengambil Raw Data ColorLut!"));
            return;
        }
        uint8_t *lutBytesStart = (uint8_t *) lutRawDataObj + (sizeof(void *) == 8 ? 0x20 : 0x10);

        // --- CHECKPOINT 2 & 3: BACA GAMBAR DARI PATH DINAMIS ---
        std::ifstream file(imagePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            LOGE(OBFUSCATE("[-] Gagal buka file di %s. Alasan: %s"), imagePath.c_str(),
                 strerror(errno));
            return;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> imageBytes(size);
        if (!file.read((char *) imageBytes.data(), size)) {
            LOGE(OBFUSCATE("[-] Gagal membaca byte file gambar"));
            return;
        }

        void *painterKlass = DrawFindClass(imgUI, DrawN("n_drawNs").c_str(), DrawN("n_canvasPainter").c_str());
        if (!painterKlass) painterKlass = DrawFindClass(imgUI, "", "CanvasPainter");
        if (!painterKlass) {
            g_drawStatus = u8"失败：找不到 CanvasPainter 类";
            LOGE(OBFUSCATE("[DRAW] class CanvasPainter tidak ketemu"));
            return;
        }
        void *m_CreateAlpha = DrawFindMethod(painterKlass, DrawN("n_createAlpha").c_str(), 2,
                                             "AlphaTexture");
        if (!m_CreateAlpha) {
            g_drawStatus = u8"失败：CanvasPainter.CreateAlphaTexture(int,int) 找不到";
            LOGE(OBFUSCATE("[DRAW] CreateAlphaTexture tidak ketemu"));
            return;
        }
        int dDim = 2;
        void *ctorArgs[2] = {&dDim, &dDim};
        void *dummyTexObj = _monoRuntimeInvoke(m_CreateAlpha, nullptr, ctorArgs, &exc);
        if (!dummyTexObj || exc) {
            g_drawStatus = u8"失败：CreateAlphaTexture(2,2) 返回空";
            LOGE(OBFUSCATE("[DRAW] gagal membuat dummy texture"));
            return;
        }

        void *conversionKlass = DrawFindClass(imgCore, DrawN("n_ueNs").c_str(),
                                              DrawN("n_imageConversion").c_str());
        if (!conversionKlass)
            conversionKlass = DrawFindClass(imgCoreModule, DrawN("n_ueNs").c_str(),
                                            DrawN("n_imageConversion").c_str());
        if (!conversionKlass) conversionKlass = DrawFindClass(imgCore, "", "ImageConversion");
        if (!conversionKlass) {
            g_drawStatus = u8"失败：找不到 UnityEngine.ImageConversion 类";
            LOGE(OBFUSCATE("[DRAW] class ImageConversion tidak ketemu"));
            return;
        }
        void *m_LoadImage = DrawFindMethod(conversionKlass, DrawN("n_loadImage").c_str(), 2,
                                           "LoadImage");
        if (!m_LoadImage) {
            g_drawStatus = u8"失败：ImageConversion.LoadImage(Texture2D,byte[]) 找不到";
            LOGE(OBFUSCATE("[DRAW] ImageConversion.LoadImage tidak ketemu"));
            return;
        }

        static void *(*_mono_array_new)(void *, void *, uintptr_t) = nullptr;
        if (!_mono_array_new) {
            // FIX: flag 0 dulu (pola yang dipakai SetupMono & Pet feed dan terbukti jalan),
            // RTLD_NOW / XDL_TRY_FORCE_LOAD baru sebagai fallback.
            void *hMono = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), 0);
            if (!hMono) hMono = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), RTLD_NOW);
            if (!hMono) hMono = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), XDL_TRY_FORCE_LOAD);
            if (!hMono) {
                LOGE(OBFUSCATE("[DRAW] libmonosgen-2.0.so tidak ditemukan via xdl_open"));
                return;
            }
            _mono_array_new = (void *(*)(void *, void *, uintptr_t)) xdl_sym(hMono, (const char*)OBFUSCATE("mono_array_new"), (size_t*)nullptr);
            if (!_mono_array_new) {
                // Sebagian build hanya expose simbol ini lewat full symbol table.
                void *hMono2 = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), XDL_TRY_FORCE_LOAD);
                if (hMono2)
                    _mono_array_new = (void *(*)(void *, void *, uintptr_t)) xdl_sym(hMono2, (const char*)OBFUSCATE("mono_array_new"), (size_t*)nullptr);
            }
        }
        if (!_mono_array_new) {
            LOGE(OBFUSCATE("[DRAW] mono_array_new tidak ter-resolve"));
            return;
        }

        void *mscorlib = _monoImageLoaded(DrawN("n_imgMscorlib").c_str());
        if (!mscorlib) mscorlib = _monoImageLoaded(DrawN("n_imgCoreLib").c_str());
        void *byteKlass = mscorlib ? _monoClassFromName(mscorlib, DrawN("n_sysNs").c_str(),
                                                       DrawN("n_byte").c_str()) : nullptr;
        if (!mscorlib || !byteKlass) {
            g_drawStatus = u8"失败：mscorlib / System.Byte 取不到";
            LOGE(OBFUSCATE("[DRAW] mscorlib/Byte tidak ketemu"));
            return;
        }

        void *monoByteArray = _mono_array_new(domain, byteKlass, imageBytes.size());
        uint8_t *destStart = (uint8_t *) monoByteArray + (sizeof(void *) == 8 ? 0x20 : 0x10);
        memcpy(destStart, imageBytes.data(), imageBytes.size());

        void *loadArgs[2] = {dummyTexObj, monoByteArray};
        _monoRuntimeInvoke(m_LoadImage, nullptr, loadArgs, &exc);

        int imgWidth = *(int *) _monoObjectUnbox(
                _monoRuntimeInvoke(m_getWidth, dummyTexObj, nullptr, &exc));
        int imgHeight = *(int *) _monoObjectUnbox(
                _monoRuntimeInvoke(m_getHeight, dummyTexObj, nullptr, &exc));
        int jpgFormat = *(int *) _monoObjectUnbox(
                _monoRuntimeInvoke(m_getFormat, dummyTexObj, nullptr, &exc));
        int jpgBpp = (jpgFormat == 3) ? 3 : 4;

        void *jpgRawDataObj = _monoRuntimeInvoke(m_getRawTexData, dummyTexObj, nullptr, &exc);
        uint8_t *jpgBytesStart = (uint8_t *) jpgRawDataObj + (sizeof(void *) == 8 ? 0x20 : 0x10);

        // --- CHECKPOINT 4: DAPATKAN KANVAS UTAMA ---
        void *uiManagerKlass = DrawFindClass(imgGamePanel, DrawN("n_uiCoreNs").c_str(),
                                             DrawN("n_uiManager").c_str());
        if (!uiManagerKlass) uiManagerKlass = DrawFindClass(imgGamePanel, "", "UIManager");
        if (!uiManagerKlass) {
            g_drawStatus = u8"失败：找不到 UIManager 类";
            LOGE(OBFUSCATE("[DRAW] class UIManager tidak ketemu"));
            return;
        }
        void *m_getUiInst = DrawFindMethod(uiManagerKlass, DrawN("n_getInstance").c_str(), 0,
                                           "Instance");
        void *uiManagerObj = m_getUiInst
                             ? _monoRuntimeInvoke(m_getUiInst, nullptr, nullptr, &exc)
                             : nullptr;
        if (!uiManagerObj || exc) {
            g_drawStatus = u8"失败：UIManager.Instance 取不到";
            LOGE(OBFUSCATE("[DRAW] UIManager.Instance null"));
            return;
        }

        void *panelKlass = DrawFindClass(imgGamePanel, DrawN("n_panelNs").c_str(),
                                         DrawN("n_drawingPanel").c_str());
        if (!panelKlass) panelKlass = DrawFindClass(imgGamePanel, "", "DrawingPanel");
        if (!panelKlass) {
            g_drawStatus = u8"失败：找不到 DrawingPanel 类";
            LOGE(OBFUSCATE("[DRAW] class DrawingPanel tidak ketemu"));
            return;
        }
        void *panelType = _monoClassGetType(panelKlass);
        void *m_GetView = DrawFindMethod(uiManagerKlass, DrawN("n_getView").c_str(), 0, "GetView");
        if (!m_GetView) {
            g_drawStatus = u8"失败：UIManager.GetView 找不到";
            LOGE(OBFUSCATE("[DRAW] UIManager.GetView tidak ketemu"));
            return;
        }

        void *tArgs[1] = {panelType};
        void *mInst = _mono_metadata_get_generic_inst(1, tArgs);
        MonoGenericContext ctx = {nullptr, mInst};
        void *closedGetView = _mono_class_inflate_generic_method(m_GetView, &ctx);

        void *drawingPanelObj = _monoRuntimeInvoke(closedGetView, uiManagerObj, nullptr, &exc);
        if (!drawingPanelObj) {
            g_drawStatus = u8"失败：DrawingPanel 没打开（先打开画板界面）";
            LOGE(OBFUSCATE("[-] DrawingPanel tidak terbuka/ditemukan!"));
            return;
        }

        void *f_painter = DrawFindField(panelKlass, DrawN("n_fPainter").c_str(), "ainter");
        if (!f_painter) {
            g_drawStatus = u8"失败：DrawingPanel 里的 painter 字段找不到";
            LOGE(OBFUSCATE("[DRAW] field painter tidak ketemu"));
            return;
        }
        void *canvasPainterObj = nullptr;
        _monoFieldGetValue(drawingPanelObj, f_painter, &canvasPainterObj);
        if (!canvasPainterObj) {
            g_drawStatus = u8"失败：CanvasPainter 对象为空";
            LOGE(OBFUSCATE("[-] Object CanvasPainter null!"));
            return;
        }

        // --- CHECKPOINT 5: MENGAMBIL KOORDINAT REGION AKTIF ---
        void *f_DrawRegion = DrawFindField(painterKlass, DrawN("n_fDrawRegion").c_str(),
                                           "DrawRegion");
        // FIX: auto-property dikompilasi jadi backing field, sama seperti versi PC
        if (!f_DrawRegion)
            f_DrawRegion = DrawFindField(painterKlass, DrawN("n_fDrawRegionBack").c_str(),
                                         "DrawRegion");
        if (!f_DrawRegion) {
            g_drawStatus = u8"失败：CanvasPainter 的 DrawRegion 字段找不到";
            LOGE(OBFUSCATE("[DRAW] field DrawRegion tidak ketemu"));
            return;
        }

        int rectData[4] = {0, 0, 0, 0};
        _monoFieldGetValue(canvasPainterObj, f_DrawRegion, &rectData);

        int regionX = rectData[0];
        int regionY = rectData[1];
        int regionW = rectData[2];
        int regionH = rectData[3];

        // FIX: batas atas seperti versi PC, supaya nilai sampah tidak bikin loop raksasa
        if (regionW <= 0 || regionH <= 0 || regionW > 3000 || regionH > 3000) {
            g_drawStatus = u8"失败：画布区域无效(" + std::to_string(regionW) + u8"x"
                           + std::to_string(regionH) + u8")，先在画板上摸一下";
            LOGE(OBFUSCATE("[DRAW] DrawRegion tidak valid (%d,%d %dx%d) -> sentuh kanvas 1x dulu"),
                 regionX, regionY, regionW, regionH);
            return;
        }

        // --- CHECKPOINT 6: SUNTIKKAN KE DALAM REGION & REKAM ---
        void *m_BeginDraw = DrawFindMethod(painterKlass, DrawN("n_beginDraw").c_str(), 0, "BeginDraw");
        if (m_BeginDraw) _monoRuntimeInvoke(m_BeginDraw, canvasPainterObj, nullptr, &exc);

        void *m_EnableSave = DrawFindMethod(painterKlass, DrawN("n_enableSave").c_str(), 0, "EnableSave");
        if (m_EnableSave) _monoRuntimeInvoke(m_EnableSave, canvasPainterObj, nullptr, &exc);

        void *m_SetPixelColor = DrawFindMethod(painterKlass, DrawN("n_setPixel").c_str(), 2,
                                               "SetPixel");
        if (!m_SetPixelColor) {
            g_drawStatus = u8"失败：CanvasPainter.SetPixelColor(Vector2Int,byte) 找不到";
            LOGE(OBFUSCATE("[DRAW] SetPixelColor tidak ketemu"));
            return;
        }
        int injectedCount = 0;

        for (int y = 0; y < regionH; y++) {
            for (int x = 0; x < regionW; x++) {
                int jpgX = (int) ((float) x / regionW * imgWidth);
                int jpgY = (int) ((float) y / regionH * imgHeight);
                if (jpgX >= imgWidth) jpgX = imgWidth - 1;
                if (jpgY >= imgHeight) jpgY = imgHeight - 1;

                int jpgPixelIdx = (jpgY * imgWidth + jpgX) * jpgBpp;
                uint8_t targetR = jpgBytesStart[jpgPixelIdx];
                uint8_t targetG = jpgBytesStart[jpgPixelIdx + 1];
                uint8_t targetB = jpgBytesStart[jpgPixelIdx + 2];

                uint8_t finalSolidColorId = 124;
                float minDistance = 999999.0f;

                for (int pId = 0; pId < lutWidth; pId++) {
                    int lutPixelIdx = pId * lutBpp;
                    uint8_t palR = lutBytesStart[lutPixelIdx];
                    uint8_t palG = lutBytesStart[lutPixelIdx + 1];
                    uint8_t palB = lutBytesStart[lutPixelIdx + 2];
                    uint8_t palA = (lutBpp == 4) ? lutBytesStart[lutPixelIdx + 3] : 255;

                    if (palA < 128) continue;

                    float rDiff = (targetR - palR) / 255.0f;
                    float gDiff = (targetG - palG) / 255.0f;
                    float bDiff = (targetB - palB) / 255.0f;
                    float distance = (rDiff * rDiff) + (gDiff * gDiff) + (bDiff * bDiff);

                    if (distance < minDistance) {
                        minDistance = distance;
                        finalSolidColorId = (uint8_t) (128 | pId);
                    }
                }

                if (finalSolidColorId == 124) continue;

                DummyVector2Int pos = {regionX + x, regionY + y};
                void *setArgs[2] = {&pos, &finalSolidColorId};

                _monoRuntimeInvoke(m_SetPixelColor, canvasPainterObj, setArgs, &exc);
                injectedCount++;
            }
        }

        void *m_RecordChange = DrawFindMethod(painterKlass, DrawN("n_recordChange").c_str(), 0,
                                              "RecordChange");
        if (m_RecordChange) _monoRuntimeInvoke(m_RecordChange, canvasPainterObj, nullptr, &exc);

        void *m_Apply = DrawFindMethod(painterKlass, DrawN("n_apply").c_str(), 0, "Apply");
        if (m_Apply) _monoRuntimeInvoke(m_Apply, canvasPainterObj, nullptr, &exc);

        g_drawStatus = u8"已注入 " + std::to_string(injectedCount) + u8" 个像素"
                       + (injectedCount > 0 ? u8"（画板上应能看到图案）"
                                            : u8"（一个像素都没写进去：图片或调色板不匹配）");
        LOGI(OBFUSCATE("[+] SUKSES! %d Piksel Tinta Solid disuntikkan dari %s"), injectedCount,
             imagePath.c_str());
    });
}


void SwitchMapNative(int roomId, std::string dllName, std::string nameSpace, std::string className,
                     std::string methodName) {

    if (dllName.length() >= 4 &&
        dllName.substr(dllName.length() - 4) == (const char *) OBFUSCATE(".dll")) {
        dllName = dllName.substr(0, dllName.length() - 4);
    }

    _monoThreadAttach(_monoGetRootDomain());

    void *imgFramework = _monoImageLoaded((const char *) OBFUSCATE("XDTBaseService"));
    if (!imgFramework)
        imgFramework = _monoImageLoaded((const char *) OBFUSCATE("XDTGame.Framework"));
    if (!imgFramework) {
        return;
    }

    void *klassManagers = _monoClassFromName(imgFramework,
                                             (const char *) OBFUSCATE("XDTGame.Framework"),
                                             (const char *) OBFUSCATE("Managers"));

    void *imgTarget = _monoImageLoaded(dllName.c_str());
    if (!imgTarget) {
        return;
    }

    void *klassTarget = _monoClassFromName(imgTarget, nameSpace.c_str(), className.c_str());
    if (!klassTarget) {
        return;
    }

    void *moduleInst = nullptr;
    void *exc = nullptr;

    // --- CARA 1: GetModule(Type) ---
    void *m_GetModule = FindMethodInHierarchy(klassManagers, (const char *) OBFUSCATE("GetModule"),
                                              1);
    if (m_GetModule) {
        void *typeObject = _monoTypeGetObject(_monoGetRootDomain(), _monoClassGetType(klassTarget));
        void *argsMod[1] = {typeObject};
        moduleInst = _monoRuntimeInvoke(m_GetModule, nullptr, argsMod, &exc);
    }

    // --- CARA 2: Get<T>() ---
    if (!moduleInst) {
        void *m_Get = FindMethodInHierarchy(klassManagers, (const char *) OBFUSCATE("Get"), 0);
        if (m_Get) {
            void *typeArgs[1] = {_monoClassGetType(klassTarget)};
            void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
            MonoGenericContext ctx = {nullptr, methodInst};
            void *closedGet = _mono_class_inflate_generic_method(m_Get, &ctx);

            exc = nullptr;
            moduleInst = _monoRuntimeInvoke(closedGet, nullptr, nullptr, &exc);
        }
    }

    if (!moduleInst) {
        return;
    }

    void *m_Transfer = FindMethodInHierarchy(klassTarget, methodName.c_str(), 3);
    if (!m_Transfer) {
        return;
    }

    int argRoomId = roomId;
    int argTeleportId = 0;
    void *argTeamStrategy = nullptr;

    void *args[3] = {&argRoomId, &argTeleportId, argTeamStrategy};

    exc = nullptr;
    _monoRuntimeInvoke(m_Transfer, moduleInst, args, &exc);
}


using GameAppUpdateFn = void (*)(void* instance);
static GameAppUpdateFn old_GameApp_Update = nullptr;

// Android kembali memakai GameApp_Update sebagai frame pump.
