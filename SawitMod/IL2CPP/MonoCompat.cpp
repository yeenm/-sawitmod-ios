// ============================================================================
//  MonoCompat.cpp — mono_* 垫片：il2cpp / 真实 mono 混合分发
// ----------------------------------------------------------------------------
//  背景修正（证据见 dump-analysis-ios.md §7、ios-antiban-gap.md §2.3）：
//    原实现把全部 mono_* 映射成 il2cpp_*。但 iOS 包里 UnityFramework 同时静态
//    链接了 Mono 运行时（1476 个 mono_* 导出，dlsym 可取），游戏业务类都在
//    Mono 热更程序集（EcsClient / XDTDataAndProtocol / XDTLevelAndEntity ...）里，
//    il2cpp 的 domain 根本看不到它们 —— 于是 _monoImageLoaded("EcsClient") 恒
//    null，依赖它的功能整片失效。
//
//  本文件现在的规则（"混合分发"）：
//    * 每个垫片先判断入参句柄"是谁家的"（见 IL2CPP/MonoHot.h 的 IsMono*）。
//        - Mono 句柄  -> 调真实 mono_*（MonoHot 包装，内部负责线程 attach）
//        - 其它       -> 走原来的 il2cpp_* 实现（AOT 路径行为保持不变）
//    * `mono_image_loaded` / `mono_class_from_name(nullptr,...)` 这类"按名字找"
//      的入口：il2cpp 找不到时回退真实 mono（热更程序集），找到即打标；
//      少数已知只在热更层存在的程序集（MonoHot::IsPreferredImage）先试 mono。
//    * mono 符号不存在 / mono 运行时还没 init -> 所有 IsMono* 恒 false，
//      行为逐字节回到旧实现（Windows 编译自检、纯 AOT 机都不受影响）。
//
//  线程：真实 mono 调用前需要 mono_thread_attach(mono_get_root_domain())，
//  MonoHot 的每个包装函数内部都做了（thread_local 幂等），本文件不用管。
// ============================================================================

#include "IL2CPP/MonoCompat.h"

#include <cstring>
#include <unordered_map>

#include "ModUtils.h"
#include "IL2CPP/Il2CppApi.h"
#include "IL2CPP/MonoHot.h"
#include "Includes/Logger.h"

namespace {

using namespace SMIL2CPP;
using Hint = SMMonoHot::Hint;

// ---- 小工具：mono 路由计数（菜单/日志里能看到到底有多少调用走到 mono） ----
inline void NoteMono(bool ok = true) {
    if (ok) SMMonoHot::CountMonoRoute();
}

// ---- 基础映射（AOT 路径，保持原样） ----
void *MonoGetRootDomain() {
    const API &a = Get();
    return a.domain_get ? a.domain_get() : nullptr;
}

void *MonoThreadAttach(void *domain) {
    const API &a = Get();
    void *result = nullptr;
    if (a.thread_attach) result = a.thread_attach(domain ? domain : MonoGetRootDomain());
    // 与 il2cpp attach 并行维护 mono 线程状态：同一线程要调两套运行时。
    SMMonoHot::EnsureAttached();
    return result;
}

void *MonoDomainGet() {
    const API &a = Get();
    return a.domain_get ? a.domain_get() : nullptr;
}

// ---- image ----
void *MonoImageLoaded(const char *name) {
    if (!name || !*name) return nullptr;

    SMMonoHot::Init();
    SMMonoHot::Tick();

    // 已知只在热更层（或 mono 侧语义优先）的程序集：先试真实 mono。
    if (SMMonoHot::IsPreferredImage(name)) {
        if (void *image = SMMonoHot::ImageLoaded(name)) {
            NoteMono();
            return image;
        }
    }

    // 旧路径：il2cpp domain 里的 AOT 程序集。
    void *legacy = FindImage(name);
    if (legacy) {
        SMMonoHot::SetHint(Hint::Il2Cpp);
        SMMonoHot::CountIl2CppRoute();
        return legacy;
    }

    // 回退：热更层的真实 mono 程序集（名字带/不带 .dll 都试）。
    if (void *image = SMMonoHot::ImageLoaded(name)) {
        NoteMono();
        return image;
    }
    return nullptr;
}

const char *MonoImageGetName(void *image) {
    if (SMMonoHot::IsMonoImage(image)) {
        NoteMono();
        return SMMonoHot::ImageGetName(image);
    }
    const API &a = Get();
    return (a.image_get_name && image) ? a.image_get_name(image) : nullptr;
}

void *MonoImageGetTableInfo(void *image, int tableId) {
    // 旧实现：il2cpp 没有元数据表概念，把 image 自身当 table info 交给
    // mono_table_info_get_rows -> il2cpp_image_get_class_count（见下面 GetRows）。
    // 真实 mono image 则必须走真 mono_image_get_table_info，否则 MonoSymbolProbe
    // 之类的元数据遍历对热更程序集永远是空（报告 §7.1 的运行时 dump 就靠这条链）。
    if (SMMonoHot::IsMonoImage(image)) {
        void *tableInfo = SMMonoHot::ImageGetTableInfo(image, tableId);
        if (tableInfo) {
            NoteMono();
            return tableInfo;
        }
        return nullptr;
    }
    return image;
}

// ---- class ----
void *MonoClassFromName(void *image, const char *nameSpace, const char *className) {
    if (!className) return nullptr;
    const char *ns = nameSpace ? nameSpace : "";

    if (image) {
        if (SMMonoHot::IsMonoImage(image)) {
            void *klass = SMMonoHot::ClassFromName(image, ns, className);
            if (klass) NoteMono();
            return klass;
        }
        // AOT image：旧行为。
        const API &a = Get();
        if (!a.class_from_name) return nullptr;
        void *klass = a.class_from_name(image, ns, className);
        if (klass) {
            SMMonoHot::TagIl2CppClass(klass);
            SMMonoHot::SetHint(Hint::Il2Cpp);
            SMMonoHot::CountIl2CppRoute();
        }
        return klass;
    }

    // image 为空：旧实现按"全 il2cpp 镜像 + 命名空间"扫一遍，避免整条功能链
    // 直接断掉；现在再追加真实 mono 全镜像回退（热更类的唯一入口）。
    void *klass = FindClassAnywhere(ns, className);
    if (klass) {
        SMMonoHot::TagIl2CppClass(klass);
        SMMonoHot::SetHint(Hint::Il2Cpp);
        SMMonoHot::CountIl2CppRoute();
        return klass;
    }
    std::string hitImage;
    klass = SMMonoHot::FindClassAnywhere(ns, className, &hitImage);
    if (klass) NoteMono();
    return klass;
}

// ---- method ----
void *MonoClassGetMethodFromName(void *klass, const char *name, int paramCount) {
    if (!klass || !name) return nullptr;
    if (SMMonoHot::IsMonoClass(klass)) {
        void *method = SMMonoHot::ClassGetMethodFromName(klass, name, paramCount);
        if (method) NoteMono();
        return method;
    }
    const API &a = Get();
    if (!a.class_get_method_from_name) return nullptr;
    return a.class_get_method_from_name(klass, name, paramCount);
}

// ---- object ----
void *MonoObjectGetClass(void *obj) {
    if (!obj) return nullptr;

    // 先按 mono 语义判型（il2cpp 布局对 mono 对象会返回 MonoVTable*，不是类）。
    // 判型内部会先校验 MonoClass*，确认不了才落回 il2cpp 旧行为。
    void *monoClass = nullptr;
    if (SMMonoHot::ClassifyObject(obj, &monoClass)) {
        NoteMono();
        return monoClass;
    }

    const API &a = Get();
    if (!a.object_get_class) return nullptr;
    void *klass = a.object_get_class(obj);
    if (klass) {
        SMMonoHot::TagIl2CppClass(klass);
        SMMonoHot::SetHint(Hint::Il2Cpp);
        SMMonoHot::CountIl2CppRoute();
    }
    return klass;
}

// ---- field ----
void *MonoClassGetFieldFromName(void *klass, const char *name) {
    if (!klass || !name) return nullptr;
    if (SMMonoHot::IsMonoClass(klass)) {
        void *field = SMMonoHot::ClassGetFieldFromName(klass, name);
        if (field) NoteMono();
        return field;
    }
    const API &a = Get();
    if (!a.class_get_field_from_name) return nullptr;
    return a.class_get_field_from_name(klass, name);
}

void MonoFieldGetValue(void *obj, void *field, void *valuePtr) {
    if (!field) return;
    if (SMMonoHot::IsMonoField(field)) {
        SMMonoHot::FieldGetValue(obj, field, valuePtr);
        NoteMono();
        return;
    }
    const API &a = Get();
    if (a.field_get_value) a.field_get_value(obj, field, valuePtr);
}

void MonoFieldSetValue(void *obj, void *field, void *valuePtr) {
    if (!field) return;
    if (SMMonoHot::IsMonoField(field)) {
        SMMonoHot::FieldSetValue(obj, field, valuePtr);
        NoteMono();
        return;
    }
    const API &a = Get();
    if (a.field_set_value) a.field_set_value(obj, field, valuePtr);
}

void *MonoFieldGetValueObject(void * /*domain*/, void *field, void *obj) {
    if (!field) return nullptr;
    if (SMMonoHot::IsMonoField(field)) {
        void *value = SMMonoHot::FieldGetValueObject(field, obj);
        if (value) NoteMono();
        return value;
    }
    const API &a = Get();
    if (!a.field_get_value_object) return nullptr;
    return a.field_get_value_object(field, obj);
}

void *MonoClassGetFields(void *klass, void **iter) {
    if (!klass) return nullptr;
    if (SMMonoHot::IsMonoClass(klass))
        return SMMonoHot::ClassGetFields(klass, iter);
    const API &a = Get();
    if (!a.class_get_fields) return nullptr;
    return a.class_get_fields(klass, iter);
}

const char *MonoFieldName(void *field) {
    if (!field) return nullptr;
    if (SMMonoHot::IsMonoField(field)) return SMMonoHot::FieldGetName(field);
    const API &a = Get();
    return (a.field_get_name && field) ? a.field_get_name(field) : nullptr;
}

int MonoFieldGetOffset(void *field) {
    if (!field) return -1;
    if (SMMonoHot::IsMonoField(field)) return SMMonoHot::FieldGetOffset(field);
    const API &a = Get();
    return (a.field_get_offset && field) ? (int) a.field_get_offset(field) : -1;
}

void *MonoFieldGetType(void *field) {
    if (!field) return nullptr;
    if (SMMonoHot::IsMonoField(field)) return SMMonoHot::FieldGetType(field);
    const API &a = Get();
    return (a.field_get_type && field) ? a.field_get_type(field) : nullptr;
}

void *MonoClassGetVTable(void * /*domain*/, void *klass) {
    if (!klass) return nullptr;
    if (SMMonoHot::IsMonoClass(klass)) {
        void *vtable = SMMonoHot::ClassGetVTable(klass);
        if (vtable) NoteMono();
        return vtable;
    }
    // 旧实现把 class 本身当 vtable（il2cpp 的 field_static_* 不需要 vtable）。
    return klass;
}

void MonoFieldStaticGetValue(void *vtable, void *field, void *value) {
    if (!field) return;
    if (SMMonoHot::IsMonoField(field)) {
        SMMonoHot::FieldStaticGetValue(vtable, field, value);
        NoteMono();
        return;
    }
    const API &a = Get();
    if (a.field_static_get_value) a.field_static_get_value(field, value);
}

void MonoFieldStaticSetValue(void *vtable, void *field, void *value) {
    if (!field) return;
    if (SMMonoHot::IsMonoField(field)) {
        SMMonoHot::FieldStaticSetValue(vtable, field, value);
        NoteMono();
        return;
    }
    const API &a = Get();
    if (a.field_static_set_value) a.field_static_set_value(field, value);
}

// ---- invoke / compile ----
void *MonoRuntimeInvoke(void *method, void *obj, void **params, void **exc) {
    if (!method) return nullptr;
    if (SMMonoHot::IsMonoMethod(method)) {
        void *result = SMMonoHot::RuntimeInvoke(method, obj, params, exc);
        NoteMono();
        return result;
    }
    const API &a = Get();
    if (!a.runtime_invoke) return nullptr;
    return a.runtime_invoke(method, obj, params, exc);
}

void *MonoObjectUnbox(void *obj) {
    if (!obj) return nullptr;
    void *monoClass = nullptr;
    if (SMMonoHot::ClassifyObject(obj, &monoClass)) {
        NoteMono();
        return SMMonoHot::ObjectUnbox(obj);
    }
    const API &a = Get();
    if (!a.object_unbox) return nullptr;
    return a.object_unbox(obj);
}

// MethodInfo 的第一个字段就是 Il2CppMethodPointer（il2cpp 全版本如此）。
// 真实 Mono 的 MonoMethod 布局完全不同，必须走 mono_compile_method（报告 §4.1）。
void *MonoCompileMethod(void *method) {
    if (!method) return nullptr;
    if (SMMonoHot::IsMonoMethod(method)) {
        void *code = SMMonoHot::CompileMethod(method);
        if (code) NoteMono();
        return code;
    }
    return *(void **) method;
}

void *MonoObjectNew(void * /*domain*/, void *klass) {
    if (!klass) return nullptr;
    if (SMMonoHot::IsMonoClass(klass)) {
        void *obj = SMMonoHot::ObjectNew(klass);
        if (obj) NoteMono();
        return obj;
    }
    const API &a = Get();
    if (!a.object_new) return nullptr;
    return a.object_new(klass);
}

void *MonoRuntimeObjectInit(void *obj) {
    if (!obj) return nullptr;
    void *monoClass = nullptr;
    if (SMMonoHot::ClassifyObject(obj, &monoClass)) {
        NoteMono();
        return SMMonoHot::RuntimeObjectInit(obj);
    }
    const API &a = Get();
    if (!a.runtime_object_init) return nullptr;
    return a.runtime_object_init(obj);
}

// ---- assembly ----
void *MonoAssemblyOpen(const char *name, int *status) {
    if (SMMonoHot::Available()) {
        int monoStatus = 1;
        void *assembly = SMMonoHot::AssemblyOpen(name, &monoStatus);
        if (assembly) {
            if (status) *status = monoStatus;
            NoteMono();
            return assembly;
        }
        if (status) *status = 1;
        return nullptr;
    }
    if (status) *status = 1;  // 1 = 失败；AOT 侧没有磁盘上的托管程序集
    return nullptr;
}

void *MonoAssemblyGetImage(void *assembly) {
    if (SMMonoHot::IsMonoAssembly(assembly)) {
        NoteMono();
        return SMMonoHot::AssemblyGetImage(assembly);
    }
    return assembly;   // 旧实现：assembly 句柄就是 image 句柄
}

// ---- signature ----
void *MonoMethodSignature(void *method) {
    if (!method) return nullptr;
    if (SMMonoHot::IsMonoMethod(method))
        return SMMonoHot::MethodSignature(method);
    return method;   // il2cpp 没有 MonoMethodSignature 结构，method 本身当签名句柄
}

void *MonoSignatureGetParams(void *sig, void **iter) {
    if (!sig || !iter) return nullptr;
    if (SMMonoHot::IsMonoSignature(sig))
        return SMMonoHot::SignatureGetParams(sig, iter);
    // il2cpp 路径：iter 里存"下一个参数下标"。
    const API &a = Get();
    if (!a.method_get_param_count || !a.method_get_param) return nullptr;
    const uint32_t index = (uint32_t)(uintptr_t) *iter;
    const uint32_t count = a.method_get_param_count(sig);
    if (index >= count) return nullptr;
    *iter = (void *)(uintptr_t)(index + 1);
    return a.method_get_param(sig, index);
}

// ---- type ----
void *MonoClassFromMonoType(void *type) {
    if (!type) return nullptr;
    if (SMMonoHot::IsMonoType(type)) {
        void *klass = SMMonoHot::ClassFromMonoType(type);
        if (klass) NoteMono();
        return klass;
    }
    const API &a = Get();
    if (!a.class_from_type) return nullptr;
    return a.class_from_type(type);
}

void *MonoClassGetType(void *klass) {
    if (!klass) return nullptr;
    if (SMMonoHot::IsMonoClass(klass)) {
        void *type = SMMonoHot::ClassGetType(klass);
        if (type) NoteMono();
        return type;
    }
    const API &a = Get();
    return (a.class_get_type && klass) ? a.class_get_type(klass) : nullptr;
}

void *MonoTypeGetObject(void * /*domain*/, void *type) {
    if (!type) return nullptr;
    if (SMMonoHot::IsMonoType(type)) {
        void *obj = SMMonoHot::TypeGetObject(type);
        if (obj) NoteMono();
        return obj;
    }
    const API &a = Get();
    return (a.type_get_object && type) ? a.type_get_object(type) : nullptr;
}

int MonoTypeGetType(void *type) {
    if (!type) return -1;
    if (SMMonoHot::IsMonoType(type)) return SMMonoHot::TypeGetType(type);
    const API &a = Get();
    return (a.type_get_type && type) ? a.type_get_type(type) : -1;
}

int MonoClassValueSize(void *klass, uint32_t *align) {
    if (klass && SMMonoHot::IsMonoClass(klass)) {
        NoteMono();
        return SMMonoHot::ClassValueSize(klass, align);
    }
    if (align) *align = 4;
    return 0;
}

void *MonoClassGetParent(void *klass) {
    if (!klass) return nullptr;
    if (SMMonoHot::IsMonoClass(klass)) {
        void *parent = SMMonoHot::ClassGetParent(klass);
        if (parent) NoteMono();
        return parent;
    }
    const API &a = Get();
    if (!a.class_get_parent) return nullptr;
    void *parent = a.class_get_parent(klass);
    if (parent) SMMonoHot::TagIl2CppClass(parent);
    return parent;
}

const char *MonoClassGetName(void *klass) {
    if (!klass) return nullptr;
    if (SMMonoHot::IsMonoClass(klass)) return SMMonoHot::ClassGetName(klass);
    const API &a = Get();
    return (a.class_get_name && klass) ? a.class_get_name(klass) : nullptr;
}

const char *MonoClassGetNamespace(void *klass) {
    if (!klass) return nullptr;
    if (SMMonoHot::IsMonoClass(klass)) return SMMonoHot::ClassGetNamespace(klass);
    const API &a = Get();
    return (a.class_get_namespace && klass) ? a.class_get_namespace(klass) : nullptr;
}

void *MonoClassGetImage(void *klass) {
    if (!klass) return nullptr;
    if (SMMonoHot::IsMonoClass(klass)) {
        void *image = SMMonoHot::ClassGetImage(klass);
        if (image) NoteMono();
        return image;
    }
    const API &a = Get();
    return (a.class_get_image && klass) ? a.class_get_image(klass) : nullptr;
}

// ---- string ----
// 两套运行时的字符串不能互传：这里按"最近一次句柄解析命中的运行时"决定。
// 默认（Hint::None）保持旧行为 = il2cpp，绝不让 AOT 路径无端生成 mono 字符串。
void *MonoStringNew(void * /*domain*/, const char *text) {
    if (!text) return nullptr;
    const API &a = Get();
    const Hint hint = SMMonoHot::CurrentHint();
    const bool monoPreferred =
            (hint == Hint::Mono) || (a.string_new == nullptr && SMMonoHot::Available());

    if (monoPreferred && SMMonoHot::Available()) {
        if (void *str = SMMonoHot::StringNew(text)) {
            NoteMono();
            return str;
        }
    }
    if (a.string_new) {
        void *str = a.string_new(text);
        if (str) {
            SMMonoHot::CountIl2CppRoute();
            return str;
        }
    }
    if (hint != Hint::Il2Cpp && SMMonoHot::Available()) {
        if (void *str = SMMonoHot::StringNew(text)) {
            NoteMono();
            return str;
        }
    }
    return nullptr;
}

// ---- enum 迭代 ----
void *MonoClassGetMethods(void *klass, void **iter) {
    if (!klass) return nullptr;
    if (SMMonoHot::IsMonoClass(klass))
        return SMMonoHot::ClassGetMethods(klass, iter);
    const API &a = Get();
    if (!a.class_get_methods || !klass) return nullptr;
    return a.class_get_methods(klass, iter);
}

const char *MonoMethodGetName(void *method) {
    if (!method) return nullptr;
    if (SMMonoHot::IsMonoMethod(method)) return SMMonoHot::MethodGetName(method);
    const API &a = Get();
    return (a.method_get_name && method) ? a.method_get_name(method) : nullptr;
}

uint32_t MonoMethodGetFlags(void *method, uint32_t *iflags) {
    if (iflags) *iflags = 0;
    if (method && SMMonoHot::IsMonoMethod(method)) {
        NoteMono();
        return SMMonoHot::MethodGetFlags(method, iflags);
    }
    // il2cpp 的 flags 访问器是内部符号，能解析到就用，拿不到按"未知"处理。
    using FlagsFn = uint32_t (*)(void *, uint32_t *);
    static FlagsFn fn = (FlagsFn) Symbol("il2cpp_method_get_flags");
    if (fn && method) return fn(method, iflags);
    return 0;
}

// ---- 元数据表（MonoSymbolProbe 用） ----
int MonoTableInfoGetRows(void *tableInfo) {
    if (!tableInfo) return 0;
    if (SMMonoHot::IsMonoTableInfo(tableInfo)) {
        NoteMono();
        return SMMonoHot::TableInfoGetRows(tableInfo);
    }
    const API &a = Get();
    if (!a.image_get_class_count) return 0;
    return (int) a.image_get_class_count(tableInfo);
}

void *MonoClassGet(void *image, uint32_t typeToken) {
    if (!image) return nullptr;
    if (SMMonoHot::IsMonoImage(image)) {
        void *klass = SMMonoHot::ClassGet(image, typeToken);
        if (klass) NoteMono();
        return klass;
    }
    const API &a = Get();
    if (!a.image_get_class) return nullptr;
    const size_t index = (size_t)((typeToken & 0x00FFFFFFu) - 1u);
    return a.image_get_class(image, index);
}

// ---- 程序集枚举 ----
void MonoAssemblyForeach(void (*callback)(void *assembly, void *user), void *user) {
    if (!callback) return;
    if (SMMonoHot::Available()) {
        SMMonoHot::AssemblyForeach(callback, user);
        NoteMono();
        return;
    }
    // 旧实现：遍历 il2cpp domain 的程序集。
    const API &a = Get();
    if (!a.domain_get || !a.domain_get_assemblies) return;
    void *domain = a.domain_get();
    if (!domain) return;
    size_t count = 0;
    void **assemblies = (void **) a.domain_get_assemblies(domain, &count);
    if (!assemblies) return;
    for (size_t i = 0; i < count; ++i) {
        void *image = a.assembly_get_image ? a.assembly_get_image(assemblies[i]) : nullptr;
        if (image) callback(image, user);
    }
}

// ---- GC 句柄 ----
uint32_t MonoGcHandleNew(void *obj, int pinned) {
    if (!obj) return 0;
    void *monoClass = nullptr;
    if (SMMonoHot::ClassifyObject(obj, &monoClass)) {
        const uint32_t handle = SMMonoHot::GcHandleNew(obj, pinned);
        if (handle) {
            NoteMono();
            return handle;
        }
        return 0;
    }
    const API &a = Get();
    if (!a.gchandle_new) return 0;
    SMMonoHot::CountIl2CppRoute();
    return a.gchandle_new(obj, pinned != 0);
}

void MonoGcHandleFree(uint32_t handle) {
    if (!handle) return;
    if (SMMonoHot::IsMonoGcHandle(handle)) {
        SMMonoHot::GcHandleFree(handle);
        NoteMono();
        return;
    }
    const API &a = Get();
    if (a.gchandle_free) a.gchandle_free(handle);
}

// ---- 泛型（AOT 侧无法膨胀：旧行为返回 nullptr，只有 mono 类型句柄才走真 mono） ----
void *MonoMetadataGetGenericInst(int argc, void **argv) {
    if (argc <= 0 || !argv) return nullptr;
    void *result = SMMonoHot::MetadataGetGenericInst(argc, argv);
    if (result) NoteMono();
    return result;
}

void *MonoClassInflateGenericMethod(void *method, void *context) {
    if (!method) return nullptr;
    void *result = SMMonoHot::ClassInflateGenericMethod(method, context);
    if (result) NoteMono();
    return result;
}

void *MonoClassInflateGenericType(void *type, void *context) {
    if (!type) return nullptr;
    void *result = SMMonoHot::ClassInflateGenericType(type, context);
    if (result) NoteMono();
    return result;
}

void *MonoClassInflateGenericTypeChecked(void *type, void *context, void *err) {
    if (!type) return nullptr;
    void *result = SMMonoHot::ClassInflateGenericTypeChecked(type, context, err);
    if (result) NoteMono();
    return result;
}

void *MonoClassInflateGenericMethodChecked(void *method, void *context, void *err) {
    if (!method) return nullptr;
    void *result = SMMonoHot::ClassInflateGenericMethodChecked(method, context, err);
    if (result) NoteMono();
    return result;
}

// ---- 数组（mono_array_new 原本被 xdl_sym 直接解析到原生符号；现在进表走分发，
//      避免把 il2cpp 元素类/il2cpp domain 传给真实 mono 运行时） ----
void *MonoArrayNew(void * /*domain*/, void *elementClass, uintptr_t count) {
    if (!elementClass) return nullptr;
    if (SMMonoHot::IsMonoClass(elementClass)) {
        void *array = SMMonoHot::ArrayNew(elementClass, count);
        if (array) NoteMono();
        return array;
    }
    // 非 mono 元素类：il2cpp 没有 mono_array_new 等价物；旧实现会直接调原生
    // mono_array_new（跨运行时 100% 崩），这里改为返回 null 让上层降级。
    return nullptr;
}

// ---- 符号表（xdl_sym / SMMono::Lookup 的落点） ----
struct NameToFn {
    const char *name;
    void *fn;
};

#define SM_FN_ENTRY(n, f) \
    NameToFn {             \
        n, (void *)(f)     \
    }

const NameToFn kTable[] = {
    SM_FN_ENTRY("mono_get_root_domain", MonoGetRootDomain),
    SM_FN_ENTRY("mono_domain_get", MonoDomainGet),
    SM_FN_ENTRY("mono_thread_attach", MonoThreadAttach),
    SM_FN_ENTRY("mono_image_loaded", MonoImageLoaded),
    SM_FN_ENTRY("mono_class_from_name", MonoClassFromName),
    SM_FN_ENTRY("mono_class_get_method_from_name", MonoClassGetMethodFromName),
    SM_FN_ENTRY("mono_object_get_class", MonoObjectGetClass),
    SM_FN_ENTRY("mono_class_get_field_from_name", MonoClassGetFieldFromName),
    SM_FN_ENTRY("mono_field_get_value", MonoFieldGetValue),
    SM_FN_ENTRY("mono_field_set_value", MonoFieldSetValue),
    SM_FN_ENTRY("mono_field_get_value_object", MonoFieldGetValueObject),
    SM_FN_ENTRY("mono_runtime_invoke", MonoRuntimeInvoke),
    SM_FN_ENTRY("mono_object_unbox", MonoObjectUnbox),
    SM_FN_ENTRY("mono_compile_method", MonoCompileMethod),
    SM_FN_ENTRY("mono_object_new", MonoObjectNew),
    SM_FN_ENTRY("mono_assembly_open", MonoAssemblyOpen),
    SM_FN_ENTRY("mono_assembly_get_image", MonoAssemblyGetImage),
    SM_FN_ENTRY("mono_method_signature", MonoMethodSignature),
    SM_FN_ENTRY("mono_signature_get_params", MonoSignatureGetParams),
    SM_FN_ENTRY("mono_class_from_mono_type", MonoClassFromMonoType),
    SM_FN_ENTRY("mono_class_get_parent", MonoClassGetParent),
    SM_FN_ENTRY("mono_class_get_name", MonoClassGetName),
    SM_FN_ENTRY("mono_class_get_namespace", MonoClassGetNamespace),
    SM_FN_ENTRY("mono_class_get_image", MonoClassGetImage),
    SM_FN_ENTRY("mono_string_new", MonoStringNew),
    SM_FN_ENTRY("mono_class_get_fields", MonoClassGetFields),
    SM_FN_ENTRY("mono_field_get_name", MonoFieldName),
    SM_FN_ENTRY("mono_field_get_offset", MonoFieldGetOffset),
    SM_FN_ENTRY("mono_field_get_type", MonoFieldGetType),
    SM_FN_ENTRY("mono_class_vtable", MonoClassGetVTable),
    SM_FN_ENTRY("mono_field_static_get_value", MonoFieldStaticGetValue),
    SM_FN_ENTRY("mono_field_static_set_value", MonoFieldStaticSetValue),
    SM_FN_ENTRY("mono_class_get_type", MonoClassGetType),
    SM_FN_ENTRY("mono_type_get_object", MonoTypeGetObject),
    SM_FN_ENTRY("mono_class_value_size", MonoClassValueSize),
    SM_FN_ENTRY("mono_metadata_get_generic_inst", MonoMetadataGetGenericInst),
    SM_FN_ENTRY("mono_class_inflate_generic_method", MonoClassInflateGenericMethod),
    SM_FN_ENTRY("mono_class_inflate_generic_type", MonoClassInflateGenericType),
    SM_FN_ENTRY("mono_class_inflate_generic_type_checked",
                MonoClassInflateGenericTypeChecked),
    SM_FN_ENTRY("mono_class_inflate_generic_method_checked",
                MonoClassInflateGenericMethodChecked),
    SM_FN_ENTRY("mono_class_get_methods", MonoClassGetMethods),
    SM_FN_ENTRY("mono_method_get_name", MonoMethodGetName),
    SM_FN_ENTRY("mono_method_get_flags", MonoMethodGetFlags),
    SM_FN_ENTRY("mono_image_get_table_info", MonoImageGetTableInfo),
    SM_FN_ENTRY("mono_table_info_get_rows", MonoTableInfoGetRows),
    SM_FN_ENTRY("mono_class_get", MonoClassGet),
    SM_FN_ENTRY("mono_type_get_type", MonoTypeGetType),
    SM_FN_ENTRY("mono_runtime_object_init", MonoRuntimeObjectInit),
    SM_FN_ENTRY("mono_assembly_foreach", MonoAssemblyForeach),
    SM_FN_ENTRY("mono_gchandle_new", MonoGcHandleNew),
    SM_FN_ENTRY("mono_gchandle_free", MonoGcHandleFree),
    SM_FN_ENTRY("mono_array_new", MonoArrayNew),
};

#undef SM_FN_ENTRY

// mono_image_get_name 需要单独的签名（返回 const char*）。
const char *MonoImageGetNameFn(void *image) { return MonoImageGetName(image); }

bool g_installed = false;

}  // namespace

void *SMMonoLookup(const char *symbolName) {
    if (!symbolName) return nullptr;
    if (strcmp(symbolName, "mono_image_get_name") == 0) return (void *) MonoImageGetNameFn;
    if (strcmp(symbolName, "mono_signature_get_return_type") == 0) {
        // 返回类型：mono 签名走真 mono，il2cpp 签名走 il2cpp_method_get_return_type。
        static auto fn = [](void *sig) -> void * {
            if (SMMonoHot::IsMonoSignature(sig))
                return SMMonoHot::SignatureGetReturnType(sig);
            const API &a = Get();
            return (a.method_get_return_type && sig) ? a.method_get_return_type(sig)
                                                     : nullptr;
        };
        return (void *)(+fn);
    }
    for (const NameToFn &entry : kTable) {
        if (strcmp(entry.name, symbolName) == 0 && entry.fn) return entry.fn;
    }
    return nullptr;
}

namespace SMMono {

void *Lookup(const char *symbolName) { return SMMonoLookup(symbolName); }

bool RuntimeReady() { return SMIL2CPP::Ready(); }

bool Installed() { return g_installed; }

void InstallPointers() {
    // 先解析真实 mono 导出（只 dlsym，不执行任何 mono 代码）：
    // 后面每个垫片都靠 MonoHot 的句柄归属来决定分发方向。
    SMMonoHot::Init();

    // 每次调用都重装：il2cpp/mono 的符号在进程启动早期可能还没解析出来。
    _monoGetRootDomain = (MonoGetRootDomain_t) SMMonoLookup("mono_get_root_domain");
    _monoDomainGet = (MonoDomainGet_t) SMMonoLookup("mono_domain_get");
    _monoThreadAttach = (MonoThreadAttach_t) SMMonoLookup("mono_thread_attach");
    _monoImageLoaded = (MonoImageLoaded_t) SMMonoLookup("mono_image_loaded");
    _monoClassFromName = (MonoClassFromName_t) SMMonoLookup("mono_class_from_name");
    _monoClassGetMethodFromName =
            (MonoClassGetMethodFromName_t) SMMonoLookup("mono_class_get_method_from_name");
    _monoObjectGetClass = (MonoObjectGetClass_t) SMMonoLookup("mono_object_get_class");
    _monoClassGetFieldFromName =
            (MonoClassGetFieldFromName_t) SMMonoLookup("mono_class_get_field_from_name");
    _monoFieldGetValue = (MonoFieldGetValue_t) SMMonoLookup("mono_field_get_value");
    _monoFieldSetValue = (MonoFieldSetValue_t) SMMonoLookup("mono_field_set_value");
    _monoFieldGetValueObject =
            (MonoFieldGetValueObject_t) SMMonoLookup("mono_field_get_value_object");
    _monoRuntimeInvoke = (MonoRuntimeInvoke_t) SMMonoLookup("mono_runtime_invoke");
    _monoObjectUnbox = (MonoObjectUnbox_t) SMMonoLookup("mono_object_unbox");
    _monoCompileMethod = (MonoCompileMethod_t) SMMonoLookup("mono_compile_method");
    _monoObjectNew = (MonoObjectNew_t) SMMonoLookup("mono_object_new");
    _monoAssemblyOpen = (MonoAssemblyOpen_t) SMMonoLookup("mono_assembly_open");
    _monoAssemblyGetImage = (MonoAssemblyGetImage_t) SMMonoLookup("mono_assembly_get_image");
    _monoMethodSignature = (MonoMethodSignature_t) SMMonoLookup("mono_method_signature");
    _monoSignatureGetParams = (MonoSignatureGetParams_t) SMMonoLookup("mono_signature_get_params");
    _monoClassFromMonoType = (MonoClassFromMonoType_t) SMMonoLookup("mono_class_from_mono_type");
    _monoClassGetParent = (mono_class_get_parent_t) SMMonoLookup("mono_class_get_parent");
    _monoClassGetName = (mono_class_get_name_t) SMMonoLookup("mono_class_get_name");
    _monoStringNew = (mono_string_new_t) SMMonoLookup("mono_string_new");
    _monoClassGetFields = (MonoClassGetFields_t) SMMonoLookup("mono_class_get_fields");
    _monoFieldName = (MonoFieldName_t) SMMonoLookup("mono_field_get_name");
    _monoFieldGetOffset = (MonoFieldGetOffset_t) SMMonoLookup("mono_field_get_offset");
    _monoFieldGetType = (MonoFieldGetType_t) SMMonoLookup("mono_field_get_type");
    _monoClassGetVTable = (mono_class_vtable_t) SMMonoLookup("mono_class_vtable");
    _monoFieldStaticGetValue =
            (mono_field_static_get_value_t) SMMonoLookup("mono_field_static_get_value");
    _monoFieldStaticSetValue =
            (mono_field_static_set_value_t) SMMonoLookup("mono_field_static_set_value");
    _monoClassGetType = (void *(*)(void *)) SMMonoLookup("mono_class_get_type");
    _monoTypeGetObject = (void *(*)(void *, void *)) SMMonoLookup("mono_type_get_object");
    _monoClassValueSize = (mono_class_value_size_t) SMMonoLookup("mono_class_value_size");
    _mono_metadata_get_generic_inst =
            (mono_metadata_get_generic_inst_t) SMMonoLookup("mono_metadata_get_generic_inst");
    _mono_class_inflate_generic_method =
            (mono_class_inflate_generic_method_t) SMMonoLookup(
                    "mono_class_inflate_generic_method");

    g_installed = _monoGetRootDomain && _monoClassFromName && _monoRuntimeInvoke &&
                  _monoImageLoaded && _monoClassGetMethodFromName;
}

bool Probe() {
    const API &api = Get();
    const bool ready = api.domain_get && api.class_from_name && api.runtime_invoke;
    void *image = SMIL2CPP::MainGameImage();
    SMMonoHot::Init();
    SMLOG("[MonoCompat] il2cpp=%s image=%p mono-symbols=%s mono-domain=%s",
          ready ? "ready" : "missing", image,
          SMMonoHot::SymbolsReady() ? "ok" : "missing",
          SMMonoHot::Available() ? "ok" : "wait");
    return ready && image != nullptr;
}

}  // namespace SMMono
