#pragma once
// ============================================================================
//  ModUtils.h — iOS 移植版（原 Android jni/ModUtils.h）
// ----------------------------------------------------------------------------
//  改动只有三处：
//    1. Global.h -> Core/State.h（去掉 JNI 依赖）
//    2. targetLibName：libil2cpp.so -> UnityFramework
//    3. 追加 iOS 侧的少量声明（游戏镜像基址/路径）
//  其余声明、Mono 函数指针 typedef、结构体全部保持原样：功能代码（
//  ModFeatures.cpp 等）照旧引用同名符号，实现由 IL2CPP/MonoCompat.cpp 提供。
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <atomic>
#include <thread>
#include "Core/State.h"
#include "Vector3.h"
#define targetLibName "UnityFramework"   // iOS: Unity 代码在主二进制/UnityFramework 内
std::vector<uint8_t> hexToBytes(const std::string &hex);
void unpad_pkcs7(std::string &s);
std::vector<uint8_t> parseHex(std::string hex);

// --- ECDH Dynamic Session Key Functions ---
void* GenerateEphemeralEcdhKey(std::string& outClientPubKeyHex);
void FreeEphemeralEcdhKey(void* ecKey);
std::string DecryptDynamicPayload(const std::string& encryptedData,
                                  void* clientPrivKey,
                                  const std::string& serverPubKeyHex,
                                  const std::string& nonce,
                                  const std::string& hwid);

#include <algorithm>
#include <cctype>

struct MonoGenericContext {
    void *class_inst;
    void *method_inst;
};

typedef void *(*MonoGetRootDomain_t)();
typedef void *(*MonoThreadAttach_t)(void *domain);
typedef void *(*MonoImageLoaded_t)(const char *name);
typedef void *(*MonoClassFromName_t)(void *image, const char *nameSpace, const char *className);
typedef void *(*MonoClassGetMethodFromName_t)(void *klass, const char *name, int paramCount);
typedef void *(*MonoObjectGetClass_t)(void *obj);
typedef void *(*MonoClassGetFieldFromName_t)(void *klass, const char *name);
typedef void (*MonoFieldGetValue_t)(void *obj, void *field, void *valuePtr);
typedef void (*MonoFieldSetValue_t)(void *obj, void *field, void *valuePtr);
typedef void *(*MonoFieldGetValueObject_t)(void *domain, void *field, void *obj);
typedef void *(*MonoRuntimeInvoke_t)(void *method, void *obj, void **params, void **exc);
typedef void *(*MonoObjectUnbox_t)(void *obj);
typedef void *(*MonoCompileMethod_t)(void *method);
typedef void *(*MonoObjectNew_t)(void *domain, void *klass);
typedef void *(*MonoDomainGet_t)();
typedef void *(*MonoAssemblyOpen_t)(const char *name, int *status);
typedef void *(*MonoAssemblyGetImage_t)(void *assembly);
typedef void *(*MonoMethodSignature_t)(void *method);
typedef void *(*MonoSignatureGetParams_t)(void *sig, void **iter);
typedef void *(*MonoClassFromMonoType_t)(void *type);
typedef void *(*mono_class_get_parent_t)(void *klass);
typedef void *(*MonoClassGetFields_t)(void *klass, void **iter);
typedef const char *(*MonoFieldName_t)(void *field);
typedef int (*MonoFieldGetOffset_t)(void *field);
typedef const char *(*mono_class_get_name_t)(void *klass);
typedef void *(*mono_string_new_t)(void *domain, const char *text);
typedef void *(*mono_class_vtable_t)(void *domain, void *klass);
typedef void (*mono_field_static_get_value_t)(void *vtable, void *field, void *value);
typedef void (*mono_field_static_set_value_t)(void *vtable, void *field, void *value);
typedef void *(*mono_metadata_get_generic_inst_t)(int argc, void **type_argv);
typedef void *(*mono_class_inflate_generic_method_t)(void *method, MonoGenericContext *context);
typedef void *(*MonoFieldGetType_t)(void *field);
typedef int (*mono_class_value_size_t)(void *klass, uint32_t *align);

// EKSPOS VARIABEL GLOBAL MONO KE SEMUA FILE
extern mono_metadata_get_generic_inst_t _mono_metadata_get_generic_inst;
extern mono_class_inflate_generic_method_t _mono_class_inflate_generic_method;
extern mono_class_vtable_t _monoClassGetVTable;
extern mono_field_static_get_value_t _monoFieldStaticGetValue;
extern mono_field_static_set_value_t _monoFieldStaticSetValue;
extern MonoClassGetFields_t _monoClassGetFields;
extern MonoFieldName_t _monoFieldName;
extern MonoFieldGetOffset_t _monoFieldGetOffset;
extern MonoFieldGetType_t _monoFieldGetType;
extern mono_class_value_size_t _monoClassValueSize;
extern mono_class_get_parent_t _monoClassGetParent;
extern mono_string_new_t _monoStringNew;
extern mono_class_get_name_t _monoClassGetName;
extern MonoMethodSignature_t _monoMethodSignature;
extern MonoSignatureGetParams_t _monoSignatureGetParams;
extern MonoClassFromMonoType_t _monoClassFromMonoType;
extern MonoObjectNew_t _monoObjectNew;
extern MonoDomainGet_t _monoDomainGet;
extern MonoAssemblyOpen_t _monoAssemblyOpen;
extern MonoAssemblyGetImage_t _monoAssemblyGetImage;
extern MonoCompileMethod_t _monoCompileMethod;
extern MonoGetRootDomain_t _monoGetRootDomain;
extern MonoThreadAttach_t _monoThreadAttach;
extern MonoImageLoaded_t _monoImageLoaded;
extern MonoClassFromName_t _monoClassFromName;
extern MonoClassGetMethodFromName_t _monoClassGetMethodFromName;
extern MonoObjectGetClass_t _monoObjectGetClass;
extern MonoClassGetFieldFromName_t _monoClassGetFieldFromName;
extern MonoFieldGetValue_t _monoFieldGetValue;
extern MonoFieldSetValue_t _monoFieldSetValue;
extern MonoFieldGetValueObject_t _monoFieldGetValueObject;
extern MonoRuntimeInvoke_t _monoRuntimeInvoke;
extern MonoObjectUnbox_t _monoObjectUnbox;
extern void *(*_monoClassGetType)(void *klass);
extern void *(*_monoTypeGetObject)(void *domain, void *type);

inline void TrimString(std::string &s) {
    if (s.empty())
        return;
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char ch) { return !std::isspace(ch); })
                    .base(),
            s.end());
}

// --- License file ---
void LoadLicenseLocally();

// --- Manual Teleport save points ---
std::string GetSavePointPath();
void SaveSavePoints();
void LoadSavePoints();

// --- Memory / IL2CPP core ---
uintptr_t getExecutableBaseAddress(const char *libName);
bool VirtualProtect(void *address, size_t size);
//bool SetupMono();
void *FindMethodInHierarchy(void *klass, const char *name, int paramCount);
void *FindFieldInHierarchy(void *klass, const char *name);
void *InflateGetComponent(void *img, const char *ns, const char *klassName, void *m_GetComponent);

// --- Native object helper ---
uint32_t NativeGetNetId(void *obj);
Vector3 NativeGetPosition(void *obj);
std::string ReadMonoStringNative(void *stringPtr);

// NOTE: UnboxToUint aslinya 'inline' -> tetap didefinisikan langsung di header.
inline uint32_t UnboxToUint(void *objPtr) {
    if (!objPtr)
        return 0;
    return *(uint32_t *) ((uintptr_t) objPtr + OFF_MONO_OBJ_HEADER);
}

float GetVector3Distance(Vector3 a, Vector3 b);

// --- Unity thread task helper ---
extern std::atomic<std::thread::id> g_unityThreadId;
void RunOnUnityThreadSync(std::function<void()> task);
void RunOnUnityThreadAsync(std::function<void()> task);
void *GetManagerInstanceNative(const char *targetManagerName);
void GetPlayerNetIdNative();

// --- Bot sleep helper ---
void SafeBotSleep(float seconds);

// --- Config manager helper (dipakai lintas fitur) ---
void *GetConfigManagerInstance();
void *GetLevelScriptableConfigInstanceNative();
void *GetPlayerMotionConfigNative();

// --- File / misc ---
void ScanImagesInFolder();
void ScanInstrumentFolder();  // scan folder .bin instrument (Music Player)
void ScanPhotoFolder();       // scan folder gambar (Import Picture / Avatar)
void *GetDrawingConfigInstanceNative();
void OpenSystemFileManagerNative();
void OpenURL(const char *url);

// --- Camera / ESP math helper ---
void *GetMainCameraNative();
Vector3 WorldToScreenNative(void *camera, Vector3 worldPos);

void *NativeGetObj(void *obj, std::vector<const char *> methodNames, std::vector<const char *> fieldNames);
std::string GetAndroidClipboard();
// --- Dynamic & Patching Helper ---
bool PatchMethodReturnNop(const char *asmName, const char *ns, const char *clsName, const char *methodName, int paramCount);
bool PatchMethodReturnTrue(const char *asmName, const char *ns, const char *clsName, const char *methodName, int paramCount);
bool PatchMethodReturnFalse(const char *asmName, const char *ns, const char *clsName, const char *methodName, int paramCount);
bool PatchMethodReturnInt(const char *asmName, const char *ns, const char *clsName, const char *methodName, int paramCount, int returnValue);

void *ExecuteDynamic(std::string dllName, std::string nameSpace, std::string className, std::string methodName, std::string paramInput, bool isStatic, std::string instancePtrHexStr = "");
// ============================================================================
//  iOS 追加：镜像基址与路径
// ============================================================================
// 游戏代码所在镜像的基址（等价 Android 的 il2cppBase）。
uintptr_t SMGetGameImageBase();
// 应用可写目录（License / 导出文件），等价 /data/data/<pkg>/files。
std::string SMAppDataDir();
