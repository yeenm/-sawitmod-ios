// ============================================================================
//  fake_dual_runtime.cpp — MonoHot / MonoCompat 混合分发逻辑的离线自测
// ----------------------------------------------------------------------------
//  为什么需要它：
//    check_link.ps1 只能证明"能编译、能链接"，证明不了**分发逻辑**对：
//    句柄归属、mono 对象判型（含把 AOT 对象安全地排除掉）、按运行时选择
//    string_new、SelfTest 命中统计……这些在 Windows 上没法用真的 mono/il2cpp
//    验证。本文件用两套**假运行时**（假 dlsym 返回假 mono_*/il2cpp_* 实现）
//    把 IL2CPP/MonoHot.cpp + IL2CPP/MonoCompat.cpp 完整跑一遍。
//
//  它验证的契约（与 mono-bridge-impl.md 的"分发规则"一一对应）：
//    1. mono 句柄 -> 真 mono_*；il2cpp 句柄 -> 原 il2cpp_*（各自计数分开）。
//    2. 对象判型：Mono 对象走 mono_object_get_class；AOT 对象即使取到的
//       "疑似 MonoClass*" 名字像类名，也会被 image 归属校验否掉、落回 il2cpp。
//    3. mono_image_loaded：热更程序集名优先 mono、AOT 名走 il2cpp、带/不带
//       .dll 都能命中、mscorlib -> System.Private.CoreLib 兼容重映射。
//    4. mono_string_new 跟随最近一次句柄解析的运行时（Hint），AOT 路径不被
//       悄悄换成 mono 字符串。
//    5. 泛型膨胀：mono 类型句柄才走真 mono，AOT 类型句柄保持返回 null。
//    6. SelfTest / StatusString 的命中数、错误码、路由计数。
//
//  构建/运行（Windows，仅本地自测，不进 check_link 的正式列表）：
//    cl /nologo /utf-8 /std:c++17 /EHsc /I ..\..\tests\stub /I ..\..\SawitMod ...
//        fake_dual_runtime.cpp ..\..\SawitMod\IL2CPP\MonoHot.cpp
//        ..\..\SawitMod\IL2CPP\MonoCompat.cpp ..\..\SawitMod\IL2CPP\Il2CppApi.cpp
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "ModUtils.h"
#include "IL2CPP/Il2CppApi.h"
#include "IL2CPP/MonoCompat.h"
#include "IL2CPP/MonoHot.h"

// ===========================================================================
//  假 mono 运行时
// ===========================================================================
namespace fake_mono {

struct FImage {
    const char *name;      // "EcsClient.dll"
};

struct FClass {
    const char *name;      // "TableData"
    const char *nameSpace; // "XD.GameGerm.ExpParser"
    FImage *image;
    FClass *parent;
};

struct FMethod {
    const char *name;
    int argc;
    FClass *klass;
    const char *returnTypeName;
};

struct FField {
    const char *name;
    int offset;
    FClass *klass;
};

struct FVTable {
    FClass *klass;
};

struct FObject {
    FVTable *vtable;     // mono 对象头：vtable -> klass
    void *monitor;
};

struct FAssembly {
    FImage *image;
};

struct FType {
    FClass *klass;
};

struct FSignature {
    FMethod *method;
};

// 指针白名单：假运行时对"不认识"的指针一律返回 null/失败，而不是去解引用它。
// 真机上这一步由 MonoHot 的 SIGSEGV/SIGBUS 栅栏兜（见 MonoHot.cpp），Windows
// 自测没有栅栏，所以用白名单把"野指针访问"变成"返回失败"，逻辑等价。
bool IsKnownClass(void *p);
bool IsKnownImage(void *p);
bool IsKnownMethod(void *p);
bool IsKnownField(void *p);

// ---- 假数据 ----
FImage g_imageEcs = {"EcsClient.dll"};
FImage g_imageProtocol = {"XDTDataAndProtocol.dll"};
FImage g_imageLevel = {"XDTLevelAndEntity.dll"};
FImage g_imageGameSystem = {"XDTGameSystem.dll"};
FImage g_imageGameUI = {"XDTGameUI.dll"};
FImage g_imageBaseService = {"XDTBaseService.dll"};
FImage g_imageEngineWrapper = {"EngineWrapper.dll"};
FImage g_imageShared = {"MonoShared.dll"};
FImage g_imageBridge = {"ScriptBridge.dll"};
FImage g_imageTypeRegister = {"TypeRegister.dll"};
FImage g_imageCoreLib = {"System.Private.CoreLib.dll"};
FImage g_imageMscorlibAlias = {"mscorlib.dll"};   // 故意不存在于 mono 侧（只测别名重映射）

FAssembly g_assemblies[] = {
        {&g_imageEcs},      {&g_imageProtocol}, {&g_imageLevel},
        {&g_imageGameSystem}, {&g_imageGameUI}, {&g_imageBaseService},
        {&g_imageEngineWrapper}, {&g_imageShared}, {&g_imageBridge},
        {&g_imageTypeRegister}, {&g_imageCoreLib},
};

FClass g_clsTableDataGlobal = {"TableData", "", &g_imageEcs, nullptr};
FClass g_clsTableDataExp = {"TableData", "XD.GameGerm.ExpParser", &g_imageEcs, nullptr};
FClass g_clsPlayerDataCenter = {"PlayerDataCenter", "XDTDataAndProtocol", &g_imageProtocol, nullptr};
FClass g_clsChatProtocol = {"ChatProtocolManager",
                           "XDTDataAndProtocol.ProtocolService.Social", &g_imageProtocol, nullptr};
FClass g_clsInteractSystem = {"InteractSystem", "XDTLevelAndEntity.BaseSystem",
                              &g_imageLevel, nullptr};
FClass g_clsManagers = {"Managers", "XDTGame.Framework", &g_imageBaseService, nullptr};
FClass g_clsString = {"String", "System", &g_imageCoreLib, nullptr};
FClass g_clsInt32 = {"Int32", "System", &g_imageCoreLib, nullptr};

FMethod g_mGetSelfNetPlayerId = {"GetSelfNetPlayerId", 0, &g_clsPlayerDataCenter, "Int32"};
FMethod g_mGetInstance = {"get_Instance", 0, &g_clsInteractSystem, "Object"};
FMethod g_mGetAllManagers = {"GetAllManagers", 0, &g_clsManagers, "Object"};
FMethod g_mSendMessage = {"SendMessage", 2, &g_clsChatProtocol, "Void"};

FField g_fTableData = {"TableData", 0x10, &g_clsManagers};

FType g_typeInteractSystem{&g_clsInteractSystem};

FObject g_monoObjectA{};   // 运行期初始化
FClass g_monoObjectClass = {"SomeEntity", "XDTLevelAndEntity", &g_imageLevel, nullptr};

void *g_domain = reinterpret_cast<void *>(0x1000);
int g_threadAttachCount = 0;
int g_runtimeInvokeMonoCount = 0;
int g_stringNewCount = 0;
int g_objectNewCount = 0;
int g_compileCount = 0;
int g_gchandleNewCount = 0;
int g_genericInstCount = 0;

FClass *FindClassInImage(FImage *image, const char *ns, const char *name) {
    FClass *all[] = {&g_clsTableDataGlobal, &g_clsTableDataExp, &g_clsPlayerDataCenter,
                     &g_clsChatProtocol,   &g_clsInteractSystem, &g_clsManagers,
                     &g_clsString,         &g_clsInt32};
    for (FClass *klass : all) {
        if (klass->image != image) continue;
        if (strcmp(klass->name, name) != 0) continue;
        const char *klassNs = klass->nameSpace ? klass->nameSpace : "";
        const char *wantNs = ns ? ns : "";
        if (strcmp(klassNs, wantNs) != 0) continue;
        return klass;
    }
    return nullptr;
}

FMethod *FindMethod(FClass *klass, const char *name, int argc) {
    FMethod *all[] = {&g_mGetSelfNetPlayerId, &g_mGetInstance, &g_mGetAllManagers,
                      &g_mSendMessage};
    for (FMethod *method : all) {
        if (method->klass != klass) continue;
        if (strcmp(method->name, name) != 0) continue;
        if (argc >= 0 && method->argc != argc) continue;
        return method;
    }
    return nullptr;
}

bool IsKnownClass(void *p) {
    if (!p) return false;
    FClass *all[] = {&g_clsTableDataGlobal, &g_clsTableDataExp, &g_clsPlayerDataCenter,
                     &g_clsChatProtocol,   &g_clsInteractSystem, &g_clsManagers,
                     &g_clsString,         &g_clsInt32,          &g_monoObjectClass};
    for (FClass *klass : all) {
        if (static_cast<void *>(klass) == p) return true;
    }
    return false;
}

bool IsKnownImage(void *p) {
    if (!p) return false;
    FImage *all[] = {&g_imageEcs,        &g_imageProtocol, &g_imageLevel,
                     &g_imageGameSystem, &g_imageGameUI,   &g_imageBaseService,
                     &g_imageEngineWrapper, &g_imageShared, &g_imageBridge,
                     &g_imageTypeRegister, &g_imageCoreLib};
    for (FImage *image : all) {
        if (static_cast<void *>(image) == p) return true;
    }
    return false;
}

bool IsKnownMethod(void *p) {
    if (!p) return false;
    FMethod *all[] = {&g_mGetSelfNetPlayerId, &g_mGetInstance, &g_mGetAllManagers,
                      &g_mSendMessage};
    for (FMethod *method : all) {
        if (static_cast<void *>(method) == p) return true;
    }
    return false;
}

bool IsKnownField(void *p) {
    return p && static_cast<void *>(&g_fTableData) == p;
}

}  // namespace fake_mono

// ===========================================================================
//  假 il2cpp 运行时（只需要 Il2CppApi 用到的那些）
// ===========================================================================
namespace fake_il2cpp {

struct FImage {
    const char *name;
};

struct FClass {
    const char *name;
    const char *nameSpace;
    FImage *image;
    FClass *parent;
};

struct FMethod {
    const char *name;
    FClass *klass;
    int argc;
};

struct FObject {
    FClass *klass;   // il2cpp 对象头：klass 直接在偏移 0
    void *monitor;
};

FImage g_imageUnityEngine = {"UnityEngine.CoreModule.dll"};
FImage g_imageAssemblyCSharp = {"Assembly-CSharp.dll"};
FClass g_clsCamera = {"Camera", "UnityEngine", &g_imageUnityEngine, nullptr};
FClass g_clsScreen = {"Screen", "UnityEngine", &g_imageUnityEngine, nullptr};
FClass g_clsThemis = {"ThemisSDKManager", "", &g_imageAssemblyCSharp, nullptr};
FMethod g_mGetMain = {"get_main", &g_clsCamera, 0};
FMethod g_mGetWidth = {"get_width", &g_clsScreen, 0};

FImage *g_images[] = {&g_imageUnityEngine, &g_imageAssemblyCSharp};
FObject g_aotObject{};
int g_runtimeInvokeIl2CppCount = 0;
int g_stringNewIl2CppCount = 0;

FClass *FindClass(FImage *image, const char *ns, const char *name) {
    FClass *all[] = {&g_clsCamera, &g_clsScreen, &g_clsThemis};
    for (FClass *klass : all) {
        if (klass->image != image) continue;
        if (strcmp(klass->name, name) != 0) continue;
        const char *klassNs = klass->nameSpace ? klass->nameSpace : "";
        if (strcmp(klassNs, ns ? ns : "") != 0) continue;
        return klass;
    }
    return nullptr;
}

FMethod *FindMethod(FClass *klass, const char *name) {
    FMethod *all[] = {&g_mGetMain, &g_mGetWidth};
    for (FMethod *method : all) {
        if (method->klass == klass && strcmp(method->name, name) == 0) return method;
    }
    return nullptr;
}

}  // namespace fake_il2cpp

// ===========================================================================
//  假函数实现（C 链接签名必须和 MonoHot / Il2CppApi 的 typedef 一致）
// ===========================================================================
using namespace fake_mono;

extern "C" {
// ---- mono ----
void *FakeMono_GetRootDomain() { return g_domain; }
void *FakeMono_DomainGet() { return g_domain; }
void *FakeMono_ThreadAttach(void *) {
    ++g_threadAttachCount;
    return reinterpret_cast<void *>(0x2000);
}
void *FakeMono_ImageLoaded(const char *name) {
    if (!name) return nullptr;
    FImage *all[] = {&g_imageEcs,    &g_imageProtocol, &g_imageLevel,
                     &g_imageGameSystem, &g_imageGameUI, &g_imageBaseService,
                     &g_imageEngineWrapper, &g_imageShared, &g_imageBridge,
                     &g_imageTypeRegister, &g_imageCoreLib};
    for (FImage *image : all) {
        if (strcmp(image->name, name) == 0) return image;
    }
    return nullptr;
}
const char *FakeMono_ImageGetName(void *image) {
    if (!IsKnownImage(image)) return nullptr;   // 野指针 -> null（真机由信号栅栏兜）
    return static_cast<FImage *>(image)->name;
}
void *FakeMono_ImageGetTableInfo(void *image, int) {
    return IsKnownImage(image) ? image : nullptr;
}
int FakeMono_TableInfoGetRows(void *) { return 0; }
void *FakeMono_AssemblyGetImage(void *assembly) {
    return assembly ? static_cast<FAssembly *>(assembly)->image : nullptr;
}
void FakeMono_AssemblyForeach(void (*cb)(void *, void *), void *user) {
    if (!cb) return;
    for (FAssembly &assembly : g_assemblies) cb(&assembly, user);
}
void *FakeMono_DomainAssemblyOpen(void *, const char *) { return nullptr; }
void *FakeMono_AssemblyOpen(const char *, void *) { return nullptr; }
void *FakeMono_ClassFromName(void *image, const char *ns, const char *name) {
    if (!image || !name) return nullptr;
    return FindClassInImage(static_cast<FImage *>(image), ns, name);
}
const char *FakeMono_ClassGetName(void *klass) {
    if (!IsKnownClass(klass)) return nullptr;
    return static_cast<FClass *>(klass)->name;
}
const char *FakeMono_ClassGetNamespace(void *klass) {
    if (!IsKnownClass(klass)) return nullptr;
    return static_cast<FClass *>(klass)->nameSpace;
}
void *FakeMono_ClassGetParent(void *klass) {
    if (!IsKnownClass(klass)) return nullptr;
    return static_cast<FClass *>(klass)->parent;
}
void *FakeMono_ClassGetImage(void *klass) {
    if (!IsKnownClass(klass)) return nullptr;
    return static_cast<FClass *>(klass)->image;
}
void *FakeMono_ClassGetMethodFromName(void *klass, const char *name, int argc) {
    if (!klass || !name) return nullptr;
    return FindMethod(static_cast<FClass *>(klass), name, argc);
}
void *FakeMono_ClassGetMethods(void *, void **) { return nullptr; }
void *FakeMono_ClassGetFields(void *, void **) { return nullptr; }
void *FakeMono_ClassGetFieldFromName(void *klass, const char *name) {
    if (!klass || !name) return nullptr;
    if (static_cast<FClass *>(klass) == &g_clsManagers && strcmp(name, "TableData") == 0)
        return &g_fTableData;
    return nullptr;
}
void *FakeMono_ClassGetType(void *klass) {
    if (static_cast<FClass *>(klass) == &g_clsInteractSystem) return &g_typeInteractSystem;
    return nullptr;
}
void *FakeMono_ClassFromMonoType(void *type) {
    return type ? static_cast<FType *>(type)->klass : nullptr;
}
void *FakeMono_ClassVTable(void *, void *klass) {
    static FVTable vtables[8];
    static int next = 0;
    (void) klass;
    if (next >= 8) next = 0;
    vtables[next].klass = static_cast<FClass *>(klass);
    return &vtables[next++];
}
int FakeMono_ClassValueSize(void *, uint32_t *align) {
    if (align) *align = 8;
    return 16;
}
void *FakeMono_ClassGet(void *image, uint32_t) {
    return image ? static_cast<void *>(&g_clsTableDataGlobal) : nullptr;
}
const char *FakeMono_MethodGetName(void *method) {
    return method ? static_cast<FMethod *>(method)->name : nullptr;
}
uint32_t FakeMono_MethodGetFlags(void *, uint32_t *iflags) {
    if (iflags) *iflags = 0;
    return 0x0010;
}
void *FakeMono_MethodSignature(void *method) {
    static FSignature sig;
    sig.method = static_cast<FMethod *>(method);
    return &sig;
}
void *FakeMono_SignatureGetParams(void *, void **iter) {
    if (iter) *iter = nullptr;
    return nullptr;
}
void *FakeMono_SignatureGetReturnType(void *) { return &g_typeInteractSystem; }
void *FakeMono_CompileMethod(void *) {
    ++g_compileCount;
    return reinterpret_cast<void *>(0x3000);
}
void *FakeMono_MethodGetUnmanagedThunk(void *) { return nullptr; }
const char *FakeMono_FieldGetName(void *field) {
    return field ? static_cast<FField *>(field)->name : nullptr;
}
int FakeMono_FieldGetOffset(void *field) {
    return field ? static_cast<FField *>(field)->offset : -1;
}
void *FakeMono_FieldGetType(void *) { return &g_typeInteractSystem; }
void *FakeMono_FieldGetParent(void *field) {
    return field ? static_cast<FField *>(field)->klass : nullptr;
}
void FakeMono_FieldGetValue(void *, void *, void *value) {
    if (value) *static_cast<int *>(value) = 0x1234;
}
void FakeMono_FieldSetValue(void *, void *, void *value) {
    if (value) *static_cast<int *>(value) = 0x4321;
}
void *FakeMono_FieldGetValueObject(void *, void *) {
    return &g_monoObjectA;
}
void FakeMono_FieldStaticGetValue(void *, void *, void *value) {
    if (value) *static_cast<void **>(value) = &g_monoObjectA;
}
void FakeMono_FieldStaticSetValue(void *, void *, void *) {}
void *FakeMono_ObjectGetClass(void *obj) {
    if (!obj) return nullptr;
    FObject *object = static_cast<FObject *>(obj);
    return object->vtable ? object->vtable->klass : nullptr;
}
void *FakeMono_ObjectNew(void *, void *klass) {
    ++g_objectNewCount;
    static FObject objects[4];
    static int next = 0;
    if (next >= 4) next = 0;
    objects[next].vtable = static_cast<FVTable *>(FakeMono_ClassVTable(nullptr, klass));
    objects[next].monitor = nullptr;
    return &objects[next++];
}
void *FakeMono_ObjectUnbox(void *obj) { return obj; }
void *FakeMono_StringNew(void *, const char *) {
    ++g_stringNewCount;
    static FObject strings[4];
    static int next = 0;
    if (next >= 4) next = 0;
    strings[next].vtable = nullptr;
    return &strings[next++];
}
void *FakeMono_ArrayNew(void *, void *, uintptr_t) {
    static FObject arrays[2];
    return &arrays[0];
}
void *FakeMono_RuntimeInvoke(void *method, void *, void **, void **exc) {
    ++g_runtimeInvokeMonoCount;
    if (exc) *exc = nullptr;
    if (method && static_cast<FMethod *>(method) == &g_mGetInstance) return &g_monoObjectA;
    return nullptr;
}
void FakeMono_RuntimeObjectInit(void *) {}
void *FakeMono_TypeGetObject(void *, void *) { return &g_monoObjectA; }
int FakeMono_TypeGetType(void *) { return 0x12; }
uint32_t FakeMono_GcHandleNew(void *, int) { return 1000u + static_cast<uint32_t>(++g_gchandleNewCount); }
void FakeMono_GcHandleFree(uint32_t) {}
void *FakeMono_MetadataGetGenericInst(int, void **) {
    ++g_genericInstCount;
    static FType inst;
    return &inst;
}
void *FakeMono_InflateGenericType(void *, void *) { return &g_typeInteractSystem; }
void *FakeMono_InflateGenericMethod(void *, void *) { return nullptr; }
void *FakeMono_InflateTypeChecked(void *, void *, void *) { return &g_typeInteractSystem; }
void *FakeMono_InflateMethodChecked(void *, void *, void *) { return nullptr; }

// ---- il2cpp ----
void *FakeIl2Cpp_DomainGet() { return reinterpret_cast<void *>(0x4000); }
void *FakeIl2Cpp_DomainGetAssemblies(void *, size_t *count) {
    static void *assemblies[2];
    assemblies[0] = &fake_il2cpp::g_images[0];
    assemblies[1] = &fake_il2cpp::g_images[1];
    (void) assemblies;
    if (count) *count = 2;
    return fake_il2cpp::g_images;
}
void *FakeIl2Cpp_ThreadAttach(void *) { return reinterpret_cast<void *>(0x5000); }
void *FakeIl2Cpp_ThreadCurrent() { return reinterpret_cast<void *>(0x5000); }
void *FakeIl2Cpp_AssemblyGetImage(void *assembly) { return assembly; }
const char *FakeIl2Cpp_ImageGetName(void *image) {
    return image ? static_cast<fake_il2cpp::FImage *>(image)->name : nullptr;
}
size_t FakeIl2Cpp_ImageGetClassCount(void *) { return 3; }
void *FakeIl2Cpp_ImageGetClass(void *image, size_t index) {
    fake_il2cpp::FClass *all[] = {&fake_il2cpp::g_clsCamera, &fake_il2cpp::g_clsScreen,
                                  &fake_il2cpp::g_clsThemis};
    for (auto *klass : all) {
        if (klass->image == image && index-- == 0) return klass;
    }
    return nullptr;
}
void *FakeIl2Cpp_ClassFromName(void *image, const char *ns, const char *name) {
    if (!image || !name) return nullptr;
    return fake_il2cpp::FindClass(static_cast<fake_il2cpp::FImage *>(image), ns, name);
}
const char *FakeIl2Cpp_ClassGetName(void *klass) {
    return klass ? static_cast<fake_il2cpp::FClass *>(klass)->name : nullptr;
}
const char *FakeIl2Cpp_ClassGetNamespace(void *klass) {
    return klass ? static_cast<fake_il2cpp::FClass *>(klass)->nameSpace : nullptr;
}
void *FakeIl2Cpp_ClassGetParent(void *klass) {
    return klass ? static_cast<fake_il2cpp::FClass *>(klass)->parent : nullptr;
}
void *FakeIl2Cpp_ClassGetImage(void *klass) {
    return klass ? static_cast<fake_il2cpp::FClass *>(klass)->image : nullptr;
}
void *FakeIl2Cpp_ClassGetMethodFromName(void *klass, const char *name, int) {
    if (!klass || !name) return nullptr;
    return fake_il2cpp::FindMethod(static_cast<fake_il2cpp::FClass *>(klass), name);
}
void *FakeIl2Cpp_ClassGetMethods(void *, void **) { return nullptr; }
void *FakeIl2Cpp_ClassGetFields(void *, void **) { return nullptr; }
void *FakeIl2Cpp_ClassGetFieldFromName(void *, const char *) { return nullptr; }
void *FakeIl2Cpp_ClassGetType(void *klass) { return klass; }
void *FakeIl2Cpp_ClassFromType(void *type) { return type; }
const char *FakeIl2Cpp_MethodGetName(void *method) {
    return method ? static_cast<fake_il2cpp::FMethod *>(method)->name : nullptr;
}
uint32_t FakeIl2Cpp_MethodGetParamCount(void *) { return 0; }
void *FakeIl2Cpp_MethodGetParam(void *, uint32_t) { return nullptr; }
void *FakeIl2Cpp_MethodGetReturnType(void *) { return nullptr; }
void *FakeIl2Cpp_MethodGetClass(void *method) {
    return method ? static_cast<fake_il2cpp::FMethod *>(method)->klass : nullptr;
}
const char *FakeIl2Cpp_FieldGetName(void *) { return nullptr; }
size_t FakeIl2Cpp_FieldGetOffset(void *) { return 0; }
void *FakeIl2Cpp_FieldGetType(void *) { return nullptr; }
void FakeIl2Cpp_FieldGetValue(void *, void *, void *) {}
void FakeIl2Cpp_FieldSetValue(void *, void *, void *) {}
void FakeIl2Cpp_FieldStaticGetValue(void *, void *) {}
void FakeIl2Cpp_FieldStaticSetValue(void *, void *) {}
void *FakeIl2Cpp_FieldGetValueObject(void *, void *) { return nullptr; }
void *FakeIl2Cpp_ObjectGetClass(void *obj) {
    return obj ? static_cast<fake_il2cpp::FObject *>(obj)->klass : nullptr;
}
void *FakeIl2Cpp_ObjectNew(void *) { return &fake_il2cpp::g_aotObject; }
void *FakeIl2Cpp_ObjectUnbox(void *obj) { return obj; }
void *FakeIl2Cpp_StringNew(const char *) {
    ++fake_il2cpp::g_stringNewIl2CppCount;
    static char buffer[64];
    return buffer;
}
int32_t FakeIl2Cpp_StringLength(void *) { return 0; }
uint16_t *FakeIl2Cpp_StringChars(void *) { return nullptr; }
void *FakeIl2Cpp_TypeGetObject(void *type) { return type; }
int FakeIl2Cpp_TypeGetType(void *) { return 0x12; }
void *FakeIl2Cpp_RuntimeInvoke(void *, void *, void **, void **exc) {
    ++fake_il2cpp::g_runtimeInvokeIl2CppCount;
    if (exc) *exc = nullptr;
    return nullptr;
}
void *FakeIl2Cpp_RuntimeObjectInit(void *obj) { return obj; }
uint32_t FakeIl2Cpp_GcHandleNew(void *, bool) { return 77; }
void FakeIl2Cpp_GcHandleFree(uint32_t) {}
void *FakeIl2Cpp_GcHandleGetTarget(uint32_t) { return nullptr; }

// ---- dlsym / dlopen ----
void *fake_dlopen(const char *, int) { return reinterpret_cast<void *>(0x1); }
void *fake_dlsym(void *, const char *name);   // 定义在下面（需要类型表）
int fake_dlclose(void *) { return 0; }
char *fake_dlerror() { return nullptr; }
}

// dlsym 名字表：把 mono_* / il2cpp_* 名字映射到假实现。
struct NameFn {
    const char *name;
    void *fn;
};

#define FN(n_name, fn) \
    NameFn { n_name, reinterpret_cast<void *>(fn) }
static const NameFn kFakeSymbols[] = {
        FN("mono_get_root_domain", FakeMono_GetRootDomain),
        FN("mono_domain_get", FakeMono_DomainGet),
        FN("mono_thread_attach", FakeMono_ThreadAttach),
        FN("mono_image_loaded", FakeMono_ImageLoaded),
        FN("mono_image_get_name", FakeMono_ImageGetName),
        FN("mono_image_get_table_info", FakeMono_ImageGetTableInfo),
        FN("mono_table_info_get_rows", FakeMono_TableInfoGetRows),
        FN("mono_assembly_get_image", FakeMono_AssemblyGetImage),
        FN("mono_assembly_foreach", FakeMono_AssemblyForeach),
        FN("mono_domain_assembly_open", FakeMono_DomainAssemblyOpen),
        FN("mono_assembly_open", FakeMono_AssemblyOpen),
        FN("mono_class_from_name", FakeMono_ClassFromName),
        FN("mono_class_get_name", FakeMono_ClassGetName),
        FN("mono_class_get_namespace", FakeMono_ClassGetNamespace),
        FN("mono_class_get_parent", FakeMono_ClassGetParent),
        FN("mono_class_get_image", FakeMono_ClassGetImage),
        FN("mono_class_get_method_from_name", FakeMono_ClassGetMethodFromName),
        FN("mono_class_get_methods", FakeMono_ClassGetMethods),
        FN("mono_class_get_fields", FakeMono_ClassGetFields),
        FN("mono_class_get_field_from_name", FakeMono_ClassGetFieldFromName),
        FN("mono_class_get_type", FakeMono_ClassGetType),
        FN("mono_class_from_mono_type", FakeMono_ClassFromMonoType),
        FN("mono_class_vtable", FakeMono_ClassVTable),
        FN("mono_class_value_size", FakeMono_ClassValueSize),
        FN("mono_class_get", FakeMono_ClassGet),
        FN("mono_method_get_name", FakeMono_MethodGetName),
        FN("mono_method_get_flags", FakeMono_MethodGetFlags),
        FN("mono_method_signature", FakeMono_MethodSignature),
        FN("mono_signature_get_params", FakeMono_SignatureGetParams),
        FN("mono_signature_get_return_type", FakeMono_SignatureGetReturnType),
        FN("mono_compile_method", FakeMono_CompileMethod),
        FN("mono_method_get_unmanaged_thunk", FakeMono_MethodGetUnmanagedThunk),
        FN("mono_field_get_name", FakeMono_FieldGetName),
        FN("mono_field_get_offset", FakeMono_FieldGetOffset),
        FN("mono_field_get_type", FakeMono_FieldGetType),
        FN("mono_field_get_parent", FakeMono_FieldGetParent),
        FN("mono_field_get_value", FakeMono_FieldGetValue),
        FN("mono_field_set_value", FakeMono_FieldSetValue),
        FN("mono_field_get_value_object", FakeMono_FieldGetValueObject),
        FN("mono_field_static_get_value", FakeMono_FieldStaticGetValue),
        FN("mono_field_static_set_value", FakeMono_FieldStaticSetValue),
        FN("mono_object_get_class", FakeMono_ObjectGetClass),
        FN("mono_object_new", FakeMono_ObjectNew),
        FN("mono_object_unbox", FakeMono_ObjectUnbox),
        FN("mono_string_new", FakeMono_StringNew),
        FN("mono_array_new", FakeMono_ArrayNew),
        FN("mono_runtime_invoke", FakeMono_RuntimeInvoke),
        FN("mono_runtime_object_init", FakeMono_RuntimeObjectInit),
        FN("mono_type_get_object", FakeMono_TypeGetObject),
        FN("mono_type_get_type", FakeMono_TypeGetType),
        FN("mono_gchandle_new", FakeMono_GcHandleNew),
        FN("mono_gchandle_free", FakeMono_GcHandleFree),
        FN("mono_metadata_get_generic_inst", FakeMono_MetadataGetGenericInst),
        FN("mono_class_inflate_generic_type", FakeMono_InflateGenericType),
        FN("mono_class_inflate_generic_method", FakeMono_InflateGenericMethod),
        FN("mono_class_inflate_generic_type_checked", FakeMono_InflateTypeChecked),
        FN("mono_class_inflate_generic_method_checked", FakeMono_InflateMethodChecked),
        // il2cpp
        FN("il2cpp_domain_get", FakeIl2Cpp_DomainGet),
        FN("il2cpp_domain_get_assemblies", FakeIl2Cpp_DomainGetAssemblies),
        FN("il2cpp_thread_attach", FakeIl2Cpp_ThreadAttach),
        FN("il2cpp_thread_current", FakeIl2Cpp_ThreadCurrent),
        FN("il2cpp_assembly_get_image", FakeIl2Cpp_AssemblyGetImage),
        FN("il2cpp_image_get_name", FakeIl2Cpp_ImageGetName),
        FN("il2cpp_image_get_class_count", FakeIl2Cpp_ImageGetClassCount),
        FN("il2cpp_image_get_class", FakeIl2Cpp_ImageGetClass),
        FN("il2cpp_class_from_name", FakeIl2Cpp_ClassFromName),
        FN("il2cpp_class_get_name", FakeIl2Cpp_ClassGetName),
        FN("il2cpp_class_get_namespace", FakeIl2Cpp_ClassGetNamespace),
        FN("il2cpp_class_get_parent", FakeIl2Cpp_ClassGetParent),
        FN("il2cpp_class_get_image", FakeIl2Cpp_ClassGetImage),
        FN("il2cpp_class_get_method_from_name", FakeIl2Cpp_ClassGetMethodFromName),
        FN("il2cpp_class_get_methods", FakeIl2Cpp_ClassGetMethods),
        FN("il2cpp_class_get_fields", FakeIl2Cpp_ClassGetFields),
        FN("il2cpp_class_get_field_from_name", FakeIl2Cpp_ClassGetFieldFromName),
        FN("il2cpp_class_get_type", FakeIl2Cpp_ClassGetType),
        FN("il2cpp_class_from_type", FakeIl2Cpp_ClassFromType),
        FN("il2cpp_method_get_name", FakeIl2Cpp_MethodGetName),
        FN("il2cpp_method_get_param_count", FakeIl2Cpp_MethodGetParamCount),
        FN("il2cpp_method_get_param", FakeIl2Cpp_MethodGetParam),
        FN("il2cpp_method_get_return_type", FakeIl2Cpp_MethodGetReturnType),
        FN("il2cpp_method_get_class", FakeIl2Cpp_MethodGetClass),
        FN("il2cpp_field_get_name", FakeIl2Cpp_FieldGetName),
        FN("il2cpp_field_get_offset", FakeIl2Cpp_FieldGetOffset),
        FN("il2cpp_field_get_type", FakeIl2Cpp_FieldGetType),
        FN("il2cpp_field_get_value", FakeIl2Cpp_FieldGetValue),
        FN("il2cpp_field_set_value", FakeIl2Cpp_FieldSetValue),
        FN("il2cpp_field_static_get_value", FakeIl2Cpp_FieldStaticGetValue),
        FN("il2cpp_field_static_set_value", FakeIl2Cpp_FieldStaticSetValue),
        FN("il2cpp_field_get_value_object", FakeIl2Cpp_FieldGetValueObject),
        FN("il2cpp_object_get_class", FakeIl2Cpp_ObjectGetClass),
        FN("il2cpp_object_new", FakeIl2Cpp_ObjectNew),
        FN("il2cpp_object_unbox", FakeIl2Cpp_ObjectUnbox),
        FN("il2cpp_string_new", FakeIl2Cpp_StringNew),
        FN("il2cpp_string_length", FakeIl2Cpp_StringLength),
        FN("il2cpp_string_chars", FakeIl2Cpp_StringChars),
        FN("il2cpp_type_get_object", FakeIl2Cpp_TypeGetObject),
        FN("il2cpp_type_get_type", FakeIl2Cpp_TypeGetType),
        FN("il2cpp_runtime_invoke", FakeIl2Cpp_RuntimeInvoke),
        FN("il2cpp_runtime_object_init", FakeIl2Cpp_RuntimeObjectInit),
        FN("il2cpp_gchandle_new", FakeIl2Cpp_GcHandleNew),
        FN("il2cpp_gchandle_free", FakeIl2Cpp_GcHandleFree),
        FN("il2cpp_gchandle_get_target", FakeIl2Cpp_GcHandleGetTarget),
};

extern "C" void *dlsym(void *, const char *name) {
    if (!name) return nullptr;
    for (const NameFn &entry : kFakeSymbols) {
        if (strcmp(entry.name, name) == 0) return entry.fn;
    }
    return nullptr;
}
extern "C" void *dlopen(const char *, int) { return reinterpret_cast<void *>(0x1); }
extern "C" int dlclose(void *) { return 0; }
extern "C" char *dlerror() { return nullptr; }

// MonoCompat.cpp 需要这些全局句柄（正常由 ModUtils.cpp 定义）。
mono_metadata_get_generic_inst_t _mono_metadata_get_generic_inst = nullptr;
mono_class_inflate_generic_method_t _mono_class_inflate_generic_method = nullptr;
mono_class_vtable_t _monoClassGetVTable = nullptr;
mono_field_static_get_value_t _monoFieldStaticGetValue = nullptr;
mono_field_static_set_value_t _monoFieldStaticSetValue = nullptr;
MonoClassGetFields_t _monoClassGetFields = nullptr;
MonoFieldName_t _monoFieldName = nullptr;
MonoFieldGetOffset_t _monoFieldGetOffset = nullptr;
MonoFieldGetType_t _monoFieldGetType = nullptr;
mono_class_value_size_t _monoClassValueSize = nullptr;
mono_class_get_parent_t _monoClassGetParent = nullptr;
mono_string_new_t _monoStringNew = nullptr;
mono_class_get_name_t _monoClassGetName = nullptr;
MonoMethodSignature_t _monoMethodSignature = nullptr;
MonoSignatureGetParams_t _monoSignatureGetParams = nullptr;
MonoClassFromMonoType_t _monoClassFromMonoType = nullptr;
MonoObjectNew_t _monoObjectNew = nullptr;
MonoDomainGet_t _monoDomainGet = nullptr;
MonoAssemblyOpen_t _monoAssemblyOpen = nullptr;
MonoAssemblyGetImage_t _monoAssemblyGetImage = nullptr;
MonoCompileMethod_t _monoCompileMethod = nullptr;
MonoGetRootDomain_t _monoGetRootDomain = nullptr;
MonoThreadAttach_t _monoThreadAttach = nullptr;
MonoImageLoaded_t _monoImageLoaded = nullptr;
MonoClassFromName_t _monoClassFromName = nullptr;
MonoClassGetMethodFromName_t _monoClassGetMethodFromName = nullptr;
MonoObjectGetClass_t _monoObjectGetClass = nullptr;
MonoClassGetFieldFromName_t _monoClassGetFieldFromName = nullptr;
MonoFieldGetValue_t _monoFieldGetValue = nullptr;
MonoFieldSetValue_t _monoFieldSetValue = nullptr;
MonoFieldGetValueObject_t _monoFieldGetValueObject = nullptr;
MonoRuntimeInvoke_t _monoRuntimeInvoke = nullptr;
MonoObjectUnbox_t _monoObjectUnbox = nullptr;
void *(*_monoClassGetType)(void *klass) = nullptr;
void *(*_monoTypeGetObject)(void *domain, void *type) = nullptr;

// ===========================================================================
//  测试主体
// ===========================================================================
namespace {

int g_failed = 0;
int g_passed = 0;

void Check(bool ok, const char *what) {
    if (ok) {
        ++g_passed;
        printf("  PASS  %s\n", what);
    } else {
        ++g_failed;
        printf("  FAIL  %s\n", what);
    }
}

}  // namespace

int main() {
    using namespace SMMonoHot;
    printf("== MonoHot / MonoCompat 双运行时假体自测 ==\n");

    Check(Init(), "Init 解析出 mono 符号");
    Check(SymbolsReady(), "SymbolsReady");
    Check(Available(), "Available（假 root domain 非空）");

    // SelfTest：假运行时里注册了 10 个探针镜像中的 10 个？至少 EcsClient /
    // XDTDataAndProtocol / XDTLevelAndEntity / XDTBaseService 等都在。
    const bool selfOk = SelfTest();
    Status st = GetStatus();
    printf("  [status] asm=%d img=%d/%d cls=%d/%d method=%d/%d\n", st.assemblies,
           st.imagesHit, st.imagesProbe, st.classesHit, st.classesProbe, st.methodsHit,
           st.methodsProbe);
    Check(selfOk, "SelfTest 命中热更程序集 + 类");
    Check(st.imagesHit >= 8, "SelfTest 镜像命中 >= 8");
    Check(st.classesHit >= 5, "SelfTest 类命中 >= 5");
    Check(st.methodsHit >= 2, "SelfTest 方法命中 >= 2");
    Check(HotReady(), "HotReady");
    Check(StatusString().find("READY") != std::string::npos, "StatusString 显示 READY");

    // ---- install pointers（_monoXxx 全局） ----
    SMMono::InstallPointers();
    Check(SMMono::Installed(), "SMMono::InstallPointers 成功");
    Check(SMMono::Probe(), "SMMono::Probe");

    // ---- 镜像路由 ----
    void *monoImage = _monoImageLoaded("XDTDataAndProtocol");
    Check(monoImage != nullptr, "mono_image_loaded(XDTDataAndProtocol) -> mono");
    Check(IsMonoImage(monoImage), "热更 image 被打上 mono 归属");
    Check(_monoImageLoaded("EcsClient") != nullptr, "mono_image_loaded(EcsClient) 无 .dll 也能命中");
    void *aotImage = _monoImageLoaded("UnityEngine.CoreModule");
    Check(aotImage != nullptr, "mono_image_loaded(UnityEngine.CoreModule) -> AOT");
    Check(!IsMonoImage(aotImage), "AOT image 不带 mono 归属");

    // ---- 类/方法/调用路由（mono 路径） ----
    void *monoClass = _monoClassFromName(monoImage, "XDTDataAndProtocol", "PlayerDataCenter");
    Check(monoClass == &g_clsPlayerDataCenter, "mono_class_from_name -> mono class");
    Check(IsMonoClass(monoClass), "mono class 打标");
    void *monoMethod = _monoClassGetMethodFromName(monoClass, "GetSelfNetPlayerId", 0);
    Check(monoMethod == &g_mGetSelfNetPlayerId, "mono_class_get_method_from_name -> mono method");
    Check(IsMonoMethod(monoMethod), "mono method 打标");
    const int invokeBefore = g_runtimeInvokeMonoCount;
    _monoRuntimeInvoke(monoMethod, nullptr, nullptr, nullptr);
    Check(g_runtimeInvokeMonoCount == invokeBefore + 1, "mono_runtime_invoke 走真 mono");
    void *compiled = _monoCompileMethod(monoMethod);
    Check(compiled == reinterpret_cast<void *>(0x3000) && g_compileCount == 1,
          "mono_compile_method 走真 mono（不是 MethodInfo 首字段）");

    // ---- 类/方法/调用路由（AOT 路径保持不变） ----
    void *aotClass = _monoClassFromName(aotImage, "UnityEngine", "Camera");
    Check(aotClass == &fake_il2cpp::g_clsCamera, "mono_class_from_name -> AOT class");
    Check(!IsMonoClass(aotClass), "AOT class 不带 mono 归属");
    void *aotMethod = _monoClassGetMethodFromName(aotClass, "get_main", 0);
    Check(aotMethod == &fake_il2cpp::g_mGetMain, "AOT method 走 il2cpp 表");
    const int aotInvokeBefore = fake_il2cpp::g_runtimeInvokeIl2CppCount;
    _monoRuntimeInvoke(aotMethod, nullptr, nullptr, nullptr);
    Check(fake_il2cpp::g_runtimeInvokeIl2CppCount == aotInvokeBefore + 1,
          "AOT runtime_invoke 仍走 il2cpp");

    // ---- 对象判型 ----
    g_monoObjectA.vtable = static_cast<FVTable *>(FakeMono_ClassVTable(nullptr, &g_monoObjectClass));
    g_monoObjectA.monitor = nullptr;
    void *objClass = _monoObjectGetClass(&g_monoObjectA);
    Check(objClass == &g_monoObjectClass, "mono 对象 -> mono_object_get_class");
    fake_il2cpp::g_aotObject.klass = &fake_il2cpp::g_clsCamera;
    fake_il2cpp::g_aotObject.monitor = nullptr;
    void *aotObjClass = _monoObjectGetClass(&fake_il2cpp::g_aotObject);
    Check(aotObjClass == &fake_il2cpp::g_clsCamera, "AOT 对象 -> il2cpp_object_get_class");
    Check(!ClassifyObject(&fake_il2cpp::g_aotObject, &objClass),
          "AOT 对象不会被误判成 mono 对象");

    // 另一个"从未解析过"的 AOT 类：走完整的"疑似 MonoClass* -> 受控校验 -> 拒绝"
    // 路径（真机上由信号栅栏兜野指针，假运行时由白名单兜）。
    fake_il2cpp::FObject unknownAotObject{&fake_il2cpp::g_clsThemis, nullptr};
    void *unknownClass = nullptr;
    Check(!ClassifyObject(&unknownAotObject, &unknownClass),
          "未登记的 AOT 类：mono 校验失败 -> 落回 il2cpp");

    // ---- 字段路由 ----
    void *baseImage = _monoImageLoaded("XDTBaseService");
    void *cfgClass = _monoClassFromName(baseImage, "XDTGame.Framework", "Managers");
    Check(cfgClass == &g_clsManagers, "XDTBaseService/Managers -> mono class");
    void *tableField = _monoClassGetFieldFromName(cfgClass, "TableData");
    Check(tableField == &g_fTableData, "mono field 解析");
    Check(IsMonoField(tableField), "mono field 打标");
    void *staticValue = nullptr;
    void *vtable = _monoClassGetVTable(nullptr, cfgClass);
    Check(vtable != nullptr, "mono_class_vtable 返回非空");
    _monoFieldStaticGetValue(vtable, tableField, &staticValue);
    Check(staticValue == &g_monoObjectA, "mono_field_static_get_value 走真 mono");

    // ---- 字符串运行时选择 ----
    void *monoStr = _monoStringNew(nullptr, "hotfix-string");
    Check(g_stringNewCount == 1, "mono 上下文里 mono_string_new 走真 mono");
    Check(monoStr != nullptr, "mono 字符串非空");
    // 解析一个 AOT 类后，hint 切回 il2cpp，字符串必须重新走 il2cpp（AOT 路径不变）
    _monoClassFromName(aotImage, "UnityEngine", "Screen");
    const int il2cppStrBefore = fake_il2cpp::g_stringNewIl2CppCount;
    _monoStringNew(nullptr, "aot-string");
    Check(fake_il2cpp::g_stringNewIl2CppCount == il2cppStrBefore + 1,
          "AOT 上下文里字符串仍走 il2cpp_string_new");

    // ---- 泛型 ----
    void *monoType = _monoClassGetType(&g_clsInteractSystem);
    Check(IsMonoType(monoType), "mono_class_get_type 打标 type");
    void *typeArgs[1] = {monoType};
    const int genericBefore = g_genericInstCount;
    Check(_mono_metadata_get_generic_inst(1, typeArgs) != nullptr,
          "mono 类型 -> 真 mono_metadata_get_generic_inst");
    Check(g_genericInstCount == genericBefore + 1, "泛型实例化走真 mono");
    void *aotTypeArgs[1] = {aotClass};   // AOT 类不是 mono type
    Check(_mono_metadata_get_generic_inst(1, aotTypeArgs) == nullptr,
          "AOT 类型 -> 泛型保持旧行为（null）");

    // ---- GC 句柄 ----
    using GcNewFn = uint32_t (*)(void *, int);
    using GcFreeFn = void (*)(uint32_t);
    GcNewFn gcNew = reinterpret_cast<GcNewFn>(SMMono::Lookup("mono_gchandle_new"));
    GcFreeFn gcFree = reinterpret_cast<GcFreeFn>(SMMono::Lookup("mono_gchandle_free"));
    uint32_t monoHandle = gcNew ? gcNew(&g_monoObjectA, 0) : 0;
    Check(monoHandle != 0 && IsMonoGcHandle(monoHandle), "mono 对象拿 mono gchandle");
    if (monoHandle && gcFree) gcFree(monoHandle);
    Check(!IsMonoGcHandle(monoHandle), "mono gchandle 释放后从归属表移除");

    // ---- 程序集枚举 ----
    int assemblyCount = 0;
    SMMonoHot::AssemblyForeach(
            [](void *assembly, void *user) {
                (void) assembly;
                ++*static_cast<int *>(user);
            },
            &assemblyCount);
    Check(assemblyCount == static_cast<int>(sizeof(g_assemblies) / sizeof(g_assemblies[0])),
          "mono_assembly_foreach 枚举 mono 程序集");

    printf("\n== 结果：%d passed, %d failed ==\n", g_passed, g_failed);
    printf("--- StatusString ---\n%s\n--------------------\n", StatusString().c_str());
    return g_failed == 0 ? 0 : 1;
}
