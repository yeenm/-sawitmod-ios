#pragma once
// ============================================================================
//  MonoGameApi.h — 游戏热更程序集（Mono）反射小工具
// ----------------------------------------------------------------------------
//  这个游戏是 il2cpp + Mono 混合：引擎在 libil2cpp.so，游戏逻辑（EcsClient /
//  XDTGameUI / XDTDataAndProtocol ...）是运行时加载的 Mono 程序集，磁盘上是
//  XDENCODE 容器，进程内解密。所以操作游戏类只能走 libmonosgen-2.0.so 的
//  mono_* 反射 API —— 也就是 ModUtils.h 里那批 _monoXxx 全局指针。
//
//  这里补三件 SetupMono（Main.cpp）没解析、但两个解锁功能必须用的东西：
//    1) 按"任意已加载镜像"找类：mono_assembly_foreach + mono_class_from_name。
//       PC 版（Bugtopia）用的就是这个 AuraMono 全镜像搜索；Android 上程序集名
//       随版本变化，写死一个镜像名迟早会失效。
//    2) 泛型相关：mono_class_get_type / mono_metadata_get_generic_inst /
//       mono_class_inflate_generic_type，用来造 List<T> 和膨胀 DispatchEvent<T>。
//    3) GC 句柄：把我们在主线程造出来的 List 钉住，hook 里才能一直返回同一个指针。
//
//  所有解析都是懒加载 + 缓存，第一次用到才 xdl_sym。
// ============================================================================

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "Global.h"
#include "ModUtils.h"
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Starcoolxdl/xdl.h"

namespace MonoApi {

// ---------------------------------------------------------------------------
//  懒加载导出
// ---------------------------------------------------------------------------
inline void *MonoHandle() {
    static void *handle = nullptr;
    if (!handle) {
        handle = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), 0);
        if (!handle)
            handle = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), XDL_TRY_FORCE_LOAD);
    }
    return handle;
}

inline void *Sym(const char *name) {
    void *handle = MonoHandle();
    return handle ? xdl_sym(handle, name, nullptr) : nullptr;
}

template<typename T>
inline T TypedSym(const char *name) {
    return reinterpret_cast<T>(Sym(name));
}

using MonoImageGetNameFn        = const char *(*)(void *image);
using MonoClassGetNamespaceFn   = const char *(*)(void *klass);
using MonoClassGetImageFn       = void *(*)(void *klass);
using MonoAssemblyForeachFn     = void (*)(void (*callback)(void *assembly, void *user), void *user);
using MonoGcHandleNewFn         = uint32_t (*)(void *obj, int pinned);
using MonoGcHandleFreeFn        = void (*)(uint32_t handle);
using MonoClassInflateTypeFn    = void *(*)(void *type, MonoGenericContext *context);
using MonoClassGetMethodsFn     = void *(*)(void *klass, void **iter);
using MonoMethodGetNameFn       = const char *(*)(void *method);
using MonoMethodGetFlagsFn      = uint32_t (*)(void *method, uint32_t *iflags);
using MonoMethodSignatureFn     = void *(*)(void *method);
using MonoSignatureParamCountFn = uint32_t (*)(void *signature);
using MonoImageTableInfoFn      = void *(*)(void *image, int table_id);
using MonoTableInfoRowsFn       = int (*)(void *table_info);
using MonoClassGetFn            = void *(*)(void *image, uint32_t type_token);
using MonoTypeGetTypeFn         = int (*)(void *type);
using MonoRuntimeObjectInitFn   = void (*)(void *object);
using MonoInflateTypeCheckedFn  = void *(*)(void *type, MonoGenericContext *context,
                                            void *error);
using MonoInflateMethodCheckedFn = void *(*)(void *method, MonoGenericContext *context,
                                             void *error);

inline MonoImageGetNameFn ImageGetName() {
    static auto fn = TypedSym<MonoImageGetNameFn>(OBFUSCATE("mono_image_get_name"));
    return fn;
}

inline MonoClassGetNamespaceFn ClassGetNamespace() {
    static auto fn = TypedSym<MonoClassGetNamespaceFn>(OBFUSCATE("mono_class_get_namespace"));
    return fn;
}

inline MonoClassGetImageFn ClassGetImage() {
    static auto fn = TypedSym<MonoClassGetImageFn>(OBFUSCATE("mono_class_get_image"));
    return fn;
}

inline MonoAssemblyForeachFn AssemblyForeach() {
    static auto fn = TypedSym<MonoAssemblyForeachFn>(OBFUSCATE("mono_assembly_foreach"));
    return fn;
}

inline MonoGcHandleNewFn GcHandleNew() {
    static auto fn = TypedSym<MonoGcHandleNewFn>(OBFUSCATE("mono_gchandle_new"));
    return fn;
}

inline MonoGcHandleFreeFn GcHandleFree() {
    static auto fn = TypedSym<MonoGcHandleFreeFn>(OBFUSCATE("mono_gchandle_free"));
    return fn;
}

inline MonoClassInflateTypeFn ClassInflateGenericType() {
    static auto fn = TypedSym<MonoClassInflateTypeFn>(OBFUSCATE("mono_class_inflate_generic_type"));
    return fn;
}

inline MonoClassGetMethodsFn ClassGetMethods() {
    static auto fn = TypedSym<MonoClassGetMethodsFn>(OBFUSCATE("mono_class_get_methods"));
    return fn;
}

inline MonoMethodGetNameFn MethodGetName() {
    static auto fn = TypedSym<MonoMethodGetNameFn>(OBFUSCATE("mono_method_get_name"));
    return fn;
}

inline MonoMethodGetFlagsFn MethodGetFlags() {
    static auto fn = TypedSym<MonoMethodGetFlagsFn>(OBFUSCATE("mono_method_get_flags"));
    return fn;
}

inline MonoImageTableInfoFn ImageGetTableInfo() {
    static auto fn = TypedSym<MonoImageTableInfoFn>(OBFUSCATE("mono_image_get_table_info"));
    return fn;
}

inline MonoTableInfoRowsFn TableInfoGetRows() {
    static auto fn = TypedSym<MonoTableInfoRowsFn>(OBFUSCATE("mono_table_info_get_rows"));
    return fn;
}

inline MonoClassGetFn ClassGet() {
    static auto fn = TypedSym<MonoClassGetFn>(OBFUSCATE("mono_class_get"));
    return fn;
}

inline MonoTypeGetTypeFn TypeGetType() {
    static auto fn = TypedSym<MonoTypeGetTypeFn>(OBFUSCATE("mono_type_get_type"));
    return fn;
}

inline MonoRuntimeObjectInitFn RuntimeObjectInit() {
    static auto fn = TypedSym<MonoRuntimeObjectInitFn>(OBFUSCATE("mono_runtime_object_init"));
    return fn;
}

// MonoError 是不透明结构，给一块足够大的清零缓冲即可（出错时 Mono 只往里写状态，
// 我们不做 cleanup，最多泄漏一点点 —— 比让非 _checked 版本 g_assert 直接 abort 强）。
struct MonoErrorBuffer {
    uint8_t bytes[128];
    MonoErrorBuffer() { memset(bytes, 0, sizeof(bytes)); }
};

inline void *InflateGenericTypeChecked(void *type, MonoGenericContext *context) {
    static auto fn = TypedSym<MonoInflateTypeCheckedFn>(
            OBFUSCATE("mono_class_inflate_generic_type_checked"));
    if (fn) {
        MonoErrorBuffer error;
        return fn(type, context, error.bytes);
    }
    static auto legacy = TypedSym<MonoClassInflateTypeFn>(
            OBFUSCATE("mono_class_inflate_generic_type"));
    return legacy ? legacy(type, context) : nullptr;
}

inline void *InflateGenericMethodChecked(void *method, MonoGenericContext *context) {
    static auto fn = TypedSym<MonoInflateMethodCheckedFn>(
            OBFUSCATE("mono_class_inflate_generic_method_checked"));
    if (fn) {
        MonoErrorBuffer error;
        return fn(method, context, error.bytes);
    }
    if (!_mono_class_inflate_generic_method) return nullptr;
    return _mono_class_inflate_generic_method(method, context);
}

// --- 返回值类型 ---
inline void *SignatureGetReturnType(void *signature) {
    using Fn = void *(*)(void *signature);
    static auto fn = TypedSym<Fn>(OBFUSCATE("mono_signature_get_return_type"));
    return (fn && signature) ? fn(signature) : nullptr;
}

// 方法返回值的 MonoTypeEnum（拿不到返回 -1）
inline int MethodReturnTypeCode(void *method) {
    if (!method || !_monoMethodSignature) return -1;
    void *returnType = SignatureGetReturnType(_monoMethodSignature(method));
    MonoTypeGetTypeFn typeGetType = TypeGetType();
    if (!returnType || !typeGetType) return -1;
    return typeGetType(returnType);
}

// 返回类型是不是 List<...>（MONO_TYPE_GENERICINST = 0x15 且类名是 List`1）。
// 用来"认得出表情面板要的那种 getter"，而不必赌方法名。
inline bool MethodReturnsGenericList(void *method) {
    if (!method || MethodReturnTypeCode(method) != 0x15) return false;
    void *returnType = SignatureGetReturnType(_monoMethodSignature(method));
    void *klass = (_monoClassFromMonoType && returnType) ? _monoClassFromMonoType(returnType)
                                                         : nullptr;
    const char *name = (klass && _monoClassGetName) ? _monoClassGetName(klass) : nullptr;
    return name && strcmp(name, "List`1") == 0;
}

// mono_class_get_methods 的迭代器签名依赖 mono_method_signature 的参数个数，这里走
// 已经解析好的 _monoMethodSignature / _monoSignatureGetParams 组合，避免再拉一个导出。
inline int MethodParamCount(void *method) {
    if (!method || !_monoMethodSignature) return -1;
    void *signature = _monoMethodSignature(method);
    if (!signature) return -1;
    void *iter = nullptr;
    int count = 0;
    while (_monoSignatureGetParams(signature, &iter) != nullptr) ++count;
    return count;
}

inline bool MethodIsStatic(void *method) {
    MonoMethodGetFlagsFn flags = MethodGetFlags();
    if (!flags || !method) return false;
    uint32_t iflags = 0;
    // MONO_METHOD_FLAGS_STATIC = 0x0010
    return (flags(method, &iflags) & 0x0010u) != 0;
}

// ---------------------------------------------------------------------------
//  参数签名（用于"选对重载"，这是 hook 前必须过的一关）
// ---------------------------------------------------------------------------
// MonoTypeEnum 里我们关心的几个：BOOLEAN=0x02 I4=0x08 U4=0x09 I8=0x0a U8=0x0b
// STRING=0x0e VALUETYPE=0x11 CLASS=0x12 I=0x18 U=0x19。
// 期望串用单字符表示：'i' = 任意整数（I4/U4/I/U），'b' = bool，'s' = string，
// 'v' = 值类型/结构体，'o' = 引用类型，'?' = 不关心。
inline char TypeCodeChar(int monoTypeEnum) {
    switch (monoTypeEnum) {
        case 0x02: return 'b';   // BOOLEAN
        case 0x08:               // I4
        case 0x09:               // U4
        case 0x0a:               // I8
        case 0x0b:               // U8
        case 0x18:               // I
        case 0x19: return 'i';   // U
        case 0x0e: return 's';   // STRING
        case 0x11: return 'v';   // VALUETYPE
        case 0x12: return 'o';   // CLASS
        default: return '?';
    }
}

// 返回该方法参数类型的字符序列，例如 "ib"；拿不到签名时返回空串。
inline std::string MethodParamTypeCodes(void *method) {
    if (!method || !_monoMethodSignature || !_monoSignatureGetParams) return std::string();
    MonoTypeGetTypeFn typeGetType = TypeGetType();
    if (!typeGetType) return std::string();
    void *signature = _monoMethodSignature(method);
    if (!signature) return std::string();

    std::string codes;
    void *iter = nullptr;
    void *type = nullptr;
    while ((type = _monoSignatureGetParams(signature, &iter)) != nullptr) {
        codes += TypeCodeChar(typeGetType(type));
        if (codes.size() > 16) break;
    }
    return codes;
}

// expected 形如 "ib"：长度必须一致，'?' 通配。
inline bool SignatureMatches(void *method, const char *expected) {
    if (!method || !expected) return false;
    const std::string actual = MethodParamTypeCodes(method);
    if (actual.empty()) return false;                     // 拿不到签名 -> 不认
    const size_t expectedLen = strlen(expected);
    if (actual.size() != expectedLen) return false;
    for (size_t i = 0; i < expectedLen; ++i) {
        const char want = expected[i];
        if (want == '?') continue;
        if (want != actual[i]) return false;
    }
    return true;
}

// 按"名字 + 参数签名"在类（含父类）里挑方法：同名重载一堆，光靠参数个数挑
// 会挑到签名完全不同的那个 —— 挂上去就是拿 this 当 id 用，直接崩进程。
inline void *FindMethodBySignature(void *klass, const char *name, const char *expected) {
    if (!klass || !name || !expected) return nullptr;
    MonoClassGetMethodsFn getMethods = ClassGetMethods();
    MonoMethodGetNameFn getName = MethodGetName();
    if (getMethods && getName) {
        void *current = klass;
        int depth = 0;
        while (current && depth++ < 8) {
            void *iter = nullptr;
            void *method = nullptr;
            while ((method = getMethods(current, &iter)) != nullptr) {
                const char *methodName = getName(method);
                if (!methodName || strcmp(methodName, name) != 0) continue;
                if (SignatureMatches(method, expected)) return method;
            }
            current = _monoClassGetParent ? _monoClassGetParent(current) : nullptr;
        }
        return nullptr;
    }
    // 拿不到 mono_class_get_methods 时退回"按个数找"，但必须签名对得上。
    if (!_monoClassGetMethodFromName) return nullptr;
    void *method = _monoClassGetMethodFromName(klass, name, (int) strlen(expected));
    if (!method) method = _monoClassGetMethodFromName(klass, name, -1);
    return (method && SignatureMatches(method, expected)) ? method : nullptr;
}


// 把类里所有同名方法的签名拼成一行，用于诊断（"方法名/签名到底长什么样"）。
inline std::string DescribeMethodsNamed(void *klass, const char *name) {
    MonoClassGetMethodsFn getMethods = ClassGetMethods();
    MonoMethodGetNameFn getName = MethodGetName();
    if (!klass || !getMethods || !getName) return std::string();
    std::string out;
    void *iter = nullptr;
    void *method = nullptr;
    while ((method = getMethods(klass, &iter)) != nullptr) {
        const char *methodName = getName(method);
        if (!methodName || strcmp(methodName, name) != 0) continue;
        if (!out.empty()) out += " | ";
        out += name;
        out += "(";
        out += MethodParamTypeCodes(method);
        out += ")";
        out += MethodIsStatic(method) ? " static" : " inst";
        if (out.size() > 400) break;
    }
    return out;
}

// ---------------------------------------------------------------------------
//  镜像 / 类 / 方法 解析
// ---------------------------------------------------------------------------
inline std::string SafeImageName(void *image) {
    MonoImageGetNameFn fn = ImageGetName();
    const char *name = (fn && image) ? fn(image) : nullptr;
    return name ? std::string(name) : std::string();
}

// mono_image_loaded 只认程序集名（不带 .dll），两个都试一遍。
inline void *LoadedImage(const char *name) {
    if (!name || !*name || !_monoImageLoaded) return nullptr;
    void *image = _monoImageLoaded(name);
    if (image) return image;
    std::string withDll(name);
    withDll += static_cast<const char *>(OBFUSCATE(".dll"));
    return _monoImageLoaded(withDll.c_str());
}

inline std::string ClassFullName(void *klass) {
    if (!klass) return std::string("<null>");
    MonoClassGetNamespaceFn nsFn = ClassGetNamespace();
    const char *name = _monoClassGetName ? _monoClassGetName(klass) : nullptr;
    const char *ns = nsFn ? nsFn(klass) : nullptr;
    std::string full;
    if (ns && *ns) {
        full = ns;
        full += '.';
    }
    full += name ? name : "<noname>";
    return full;
}

// 先按"候选镜像顺序"找，找不到再退到全镜像搜索（游戏更新挪了程序集也不会失效）。
// images 为 nullptr / count 为 0 时直接走全镜像搜索。
inline void *FindClass(const char *const *images,
                       int imageCount,
                       const char *nameSpace,
                       const char *className,
                       std::string *outImage = nullptr,
                       bool allowAnyImage = true) {
    if (!_monoClassFromName || !className) return nullptr;
    const char *ns = nameSpace ? nameSpace : "";

    for (int i = 0; images && i < imageCount; ++i) {
        void *image = LoadedImage(images[i]);
        if (!image) continue;
        void *klass = _monoClassFromName(image, ns, className);
        if (klass) {
            if (outImage) *outImage = SafeImageName(image);
            return klass;
        }
    }

    if (!allowAnyImage) return nullptr;

    MonoAssemblyForeachFn forEach = AssemblyForeach();
    if (!forEach || !_monoAssemblyGetImage) return nullptr;

    struct Search {
        const char *nameSpace;
        const char *className;
        void *klass;
        std::string image;
    } search{ns, className, nullptr, std::string()};

    // 回调里只做 mono_class_from_name（纯元数据查询，不分配、不进 GC）。
    forEach([](void *assembly, void *user) {
        Search *state = static_cast<Search *>(user);
        if (!state || state->klass) return;
        void *image = _monoAssemblyGetImage(assembly);
        if (!image) return;
        void *klass = _monoClassFromName(image, state->nameSpace, state->className);
        if (klass) {
            state->klass = klass;
            state->image = SafeImageName(image);
        }
    }, &search);

    if (search.klass && outImage) *outImage = search.image;
    return search.klass;
}

// 按参数个数候选表找方法；-1 表示"任意参数个数"（mono_class_get_method_from_name
// 的约定），放在最后当兜底。
inline void *FindMethod(void *klass, const char *name,
                        std::initializer_list<int> arities) {
    if (!klass || !name || !_monoClassGetMethodFromName) return nullptr;
    for (int arity: arities) {
        void *method = _monoClassGetMethodFromName(klass, name, arity);
        if (method) return method;
    }
    void *method = _monoClassGetMethodFromName(klass, name, -1);
    if (method) return method;
    return FindMethodInHierarchy(klass, name, -1);
}

// ---------------------------------------------------------------------------
//  泛型 / 对象构造
// ---------------------------------------------------------------------------
// List<T>：先用 BCL 的 Type.GetType + Activator.CreateInstance 造（PC 版走的就是这条
// 路，类型名不对只会返回 null，不会 abort）；失败再退回 Mono 原生膨胀。
//
// ⚠️ 膨胀泛型**类**时，类型实参必须放 MonoGenericContext::class_inst —— 放成
// method_inst 的话 List`1[T] 里的 T（MONO_TYPE_VAR）不会被替换，拿到的是"开放泛型
// 类"，接着 mono_runtime_object_init 会直接 abort：
//   * Assertion at mono/metadata/object.c:127 ... 'System.Collections.Generic.List`1[T]'
//     is not fully instantiated.
// 两个槽位都填上，是为了兼容不同 Mono 版本对 VAR/MVAR 的取用习惯。
inline void *InflateGenericClass(void *openClass, void *typeArgument) {
    if (!openClass || !typeArgument) return nullptr;
    if (!_monoClassGetType || !_mono_metadata_get_generic_inst || !_monoClassFromMonoType)
        return nullptr;

    void *openType = _monoClassGetType(openClass);
    if (!openType) return nullptr;

    void *typeArgs[1] = {typeArgument};
    void *genericInst = _mono_metadata_get_generic_inst(1, typeArgs);
    if (!genericInst) return nullptr;

    MonoGenericContext context{};
    context.class_inst = genericInst;    // 类级泛型参数在这里
    context.method_inst = genericInst;

    void *inflatedType = InflateGenericTypeChecked(openType, &context);
    if (!inflatedType || inflatedType == openType) return nullptr;  // 没膨胀成功

    void *inflatedClass = _monoClassFromMonoType(inflatedType);
    if (!inflatedClass || inflatedClass == openClass) return nullptr;
    return inflatedClass;
}

// 把开放泛型方法膨胀到具体事件类型（EventCenter.DispatchEvent<T>）。
inline void *InflateGenericMethodForClass(void *openMethod, void *typeClass) {
    if (!openMethod || !typeClass || !_monoClassGetType
        || !_mono_metadata_get_generic_inst) {
        return nullptr;
    }

    void *typeArgument = _monoClassGetType(typeClass);
    if (!typeArgument) return nullptr;

    void *typeArgs[1] = {typeArgument};
    void *genericInst = _mono_metadata_get_generic_inst(1, typeArgs);
    if (!genericInst) return nullptr;

    MonoGenericContext context{};
    context.class_inst = nullptr;
    context.method_inst = genericInst;
    return InflateGenericMethodChecked(openMethod, &context);
}

// BCL 兜底：Type.GetType(程序集限定名) + Activator.CreateInstance(Type)。
// 对应 PC 版 TryCreateEmoteUnlockList。
inline void *CreateBclInstance(const std::string &assemblyQualifiedName) {
    if (!_monoStringNew || !_monoRuntimeInvoke || !_monoObjectUnbox) return nullptr;
    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!domain) return nullptr;

    const char *corlibNames[] = {OBFUSCATE("mscorlib"), OBFUSCATE("System.Private.CoreLib"),
                                 OBFUSCATE("System.Runtime")};
    void *typeClass = nullptr;
    void *activatorClass = nullptr;
    for (const char *corlibName: corlibNames) {
        void *corlib = LoadedImage(corlibName);
        if (!corlib) continue;
        if (!typeClass)
            typeClass = _monoClassFromName(corlib, OBFUSCATE("System"), OBFUSCATE("Type"));
        if (!activatorClass)
            activatorClass = _monoClassFromName(corlib, OBFUSCATE("System"),
                                                OBFUSCATE("Activator"));
        if (typeClass && activatorClass) break;
    }
    if (!typeClass || !activatorClass) return nullptr;

    void *getType = FindMethod(typeClass, OBFUSCATE("GetType"), {1});
    void *createInstance = FindMethod(activatorClass, OBFUSCATE("CreateInstance"), {1});
    if (!getType || !createInstance) return nullptr;

    void *nameString = _monoStringNew(domain, assemblyQualifiedName.c_str());
    if (!nameString) return nullptr;

    void *getTypeArgs[1] = {nameString};
    void *exception = nullptr;
    void *typeObject = _monoRuntimeInvoke(getType, nullptr, getTypeArgs, &exception);
    if (exception || !typeObject) return nullptr;

    void *createArgs[1] = {typeObject};
    exception = nullptr;
    void *instance = _monoRuntimeInvoke(createInstance, nullptr, createArgs, &exception);
    if (exception) return nullptr;
    return instance;
}

// 造一个 List<T> 实例。
// 路线 1（首选，PC 版同款）：BCL 的 Type.GetType + Activator.CreateInstance ——
//   类型名不对只是返回 null，不会 abort，而且构造函数一定跑了（List 的内部数组
//   _items 会被正确初始化）。
// 路线 2（兜底）：Mono 原生膨胀泛型类 + mono_object_new + mono_runtime_object_init
//   （mono_object_new 不跑 ctor，必须补 init，否则 _items 是 null）。
//   这条路只有在确认类真的是"闭合"泛型实例时才会走 —— 拿开放泛型类去 init 会 abort。
inline void *CreateGenericList(void *elementClass, const std::string &bclFallbackName) {
    if (!elementClass) return nullptr;

    if (!bclFallbackName.empty()) {
        void *list = CreateBclInstance(bclFallbackName);
        if (list) return list;
        LOGI(OBFUSCATE("[MonoApi] BCL 造 List 失败，改走 Mono 膨胀路线"));
    }

    if (!_monoObjectNew) return nullptr;
    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!domain) return nullptr;

    const char *corlibNames[] = {OBFUSCATE("mscorlib"), OBFUSCATE("System.Private.CoreLib"),
                                 OBFUSCATE("System.Runtime")};
    MonoRuntimeObjectInitFn objectInit = RuntimeObjectInit();
    if (!objectInit) return nullptr;   // 没有 init 就不能用 mono_object_new 造 List

    for (const char *corlibName: corlibNames) {
        void *corlib = LoadedImage(corlibName);
        if (!corlib) continue;
        void *listOpen = _monoClassFromName(corlib, OBFUSCATE("System.Collections.Generic"),
                                            OBFUSCATE("List`1"));
        if (!listOpen) continue;
        void *listClass = InflateGenericClass(listOpen, _monoClassGetType(elementClass));
        if (!listClass) continue;      // 膨胀失败就换下一个 corlib，绝不用开放泛型类
        void *list = _monoObjectNew(domain, listClass);
        if (!list) continue;
        objectInit(list);              // 跑 .ctor()：_items = Array.Empty<T>()
        return list;
    }
    return nullptr;
}

inline uint32_t PinObject(void *object) {
    MonoGcHandleNewFn pin = GcHandleNew();
    if (!pin || !object) return 0;
    return pin(object, 1); // pinned = true：hook 里要一直返回同一个地址
}

inline void UnpinObject(uint32_t handle) {
    MonoGcHandleFreeFn unpin = GcHandleFree();
    if (unpin && handle) unpin(handle);
}

// ---------------------------------------------------------------------------
//  值类型读取（List<T> 元素 / 表行的字段）
// ---------------------------------------------------------------------------
inline bool ReadInt32Field(void *object, const char *fieldName, int *out) {
    if (!object || !fieldName || !out || !_monoClassGetFieldFromName || !_monoObjectGetClass
        || !_monoFieldGetValue) {
        return false;
    }
    void *klass = _monoObjectGetClass(object);
    void *field = klass ? FindFieldInHierarchy(klass, fieldName) : nullptr;
    if (!field) return false;
    int value = 0;
    _monoFieldGetValue(object, field, &value);
    *out = value;
    return true;
}

inline bool ReadBoolField(void *object, const char *fieldName, bool *out) {
    if (!object || !out) return false;
    int value = 0;
    if (!ReadInt32Field(object, fieldName, &value)) return false;
    *out = value != 0;
    return true;
}

// 只读 1 字节的字段（例如 TableSingleaction 的私有 byte _isLoop）。
inline bool ReadByteField(void *object, const char *fieldName, uint8_t *out) {
    if (!object || !fieldName || !out || !_monoClassGetFieldFromName || !_monoObjectGetClass
        || !_monoFieldGetValue) {
        return false;
    }
    void *klass = _monoObjectGetClass(object);
    void *field = klass ? FindFieldInHierarchy(klass, fieldName) : nullptr;
    if (!field) return false;
    uint8_t value = 0;
    _monoFieldGetValue(object, field, &value);
    *out = value;
    return true;
}

inline bool WriteInt32Field(void *object, void *field, int value) {
    if (!object || !field || !_monoFieldSetValue) return false;
    _monoFieldSetValue(object, field, &value);
    return true;
}

// 调用无参实例/静态方法拿一个 int（List<T>.get_Count 之类）。
inline bool InvokeReturningIntNoArg(void *method, void *instance, int *out) {
    if (!method || !_monoRuntimeInvoke || !out) return false;
    void *exception = nullptr;
    void *result = _monoRuntimeInvoke(method, instance, nullptr, &exception);
    if (exception || !result) return false;
    int value = 0;
    if (_monoObjectUnbox) {
        memcpy(&value, _monoObjectUnbox(result), sizeof(int));
    } else {
        memcpy(&value, reinterpret_cast<uint8_t *>(result) + 2 * sizeof(void *), sizeof(int));
    }
    *out = value;
    return true;
}

// 调用 arity 1 的实例方法返回托管对象（List<T>.get_Item -> 装箱的 T）。
inline void *InvokeReturningObject(void *method, void *instance, int argument) {
    if (!method || !_monoRuntimeInvoke) return nullptr;
    void *args[1] = {&argument};
    void *exception = nullptr;
    void *result = _monoRuntimeInvoke(method, instance, args, &exception);
    return exception ? nullptr : result;
}

// 装箱值类型取数据指针（mono_object_unbox 的兜底：跳过 2 个指针的对象头）。
inline void *UnboxValue(void *boxed) {
    if (!boxed) return nullptr;
    if (_monoObjectUnbox) return _monoObjectUnbox(boxed);
    return reinterpret_cast<uint8_t *>(boxed) + 2 * sizeof(void *);
}

// 调用 arity 1 的实例方法拿一个 int（表行 getter / List.get_Item 之类）。
inline bool InvokeReturningInt(void *method, void *instance, int argument, int *out) {
    if (!method || !_monoRuntimeInvoke || !out) return false;
    void *args[1] = {&argument};
    void *exception = nullptr;
    void *result = _monoRuntimeInvoke(method, instance, args, &exception);
    if (exception || !result) return false;
    int value = 0;
    if (_monoObjectUnbox) {
        memcpy(&value, _monoObjectUnbox(result), sizeof(int));
    } else {
        memcpy(&value, reinterpret_cast<uint8_t *>(result) + 2 * sizeof(void *), sizeof(int));
    }
    *out = value;
    return true;
}


// 全镜像按"名字 + 签名"找方法：直接路径解析不到时的兜底（程序集挪了、命名空间写了
// 别名都还能找到）。找到时把所在类名写进 outClass。
inline void *FindMethodAcrossImages(const char *const *images, int imageCount,
                                    const char *methodName, const char *signature,
                                    const char *classNameHint, std::string *outClass = nullptr,
                                    int maxClasses = 40000) {
    MonoImageTableInfoFn tableInfo = ImageGetTableInfo();
    MonoTableInfoRowsFn rows = TableInfoGetRows();
    MonoClassGetFn classGet = ClassGet();
    if (!tableInfo || !rows || !classGet) return nullptr;

    int visited = 0;
    for (int i = 0; images && i < imageCount; ++i) {
        void *image = LoadedImage(images[i]);
        if (!image) continue;
        void *typeDefTable = tableInfo(image, 0x02);   // MONO_TABLE_TYPEDEF
        if (!typeDefTable) continue;
        const int rowCount = rows(typeDefTable);
        for (int rid = 1; rid <= rowCount && visited < maxClasses; ++rid, ++visited) {
            void *klass = classGet(image, 0x02000000u | static_cast<uint32_t>(rid));
            if (!klass) continue;
            const char *name = _monoClassGetName ? _monoClassGetName(klass) : nullptr;
            if (!name) continue;
            if (classNameHint && *classNameHint && !strstr(name, classNameHint)) continue;
            void *method = FindMethodBySignature(klass, methodName, signature);
            if (!method) continue;
            if (outClass) *outClass = ClassFullName(klass);
            return method;
        }
    }
    return nullptr;
}

// 把类里所有"0 参数 + 返回 List<...>"的方法名收出来（表情面板的 getter 就是这种形状）。
inline std::vector<std::string> ListGetterMethodNames(void *klass) {
    std::vector<std::string> names;
    MonoClassGetMethodsFn getMethods = ClassGetMethods();
    MonoMethodGetNameFn getName = MethodGetName();
    if (!klass || !getMethods || !getName) return names;
    void *iter = nullptr;
    void *method = nullptr;
    while ((method = getMethods(klass, &iter)) != nullptr) {
        const char *methodName = getName(method);
        if (!methodName) continue;
        if (!MethodParamTypeCodes(method).empty()) continue;   // 只看无参
        if (!MethodReturnsGenericList(method)) continue;
        names.push_back(methodName);
        if (names.size() > 24) break;
    }
    return names;
}

} // namespace MonoApi
