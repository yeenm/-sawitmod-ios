// ============================================================================
//  MonoCompat.cpp — mono_* 到 il2cpp_* 的垫片实现
// ----------------------------------------------------------------------------
//  只做"同一件事换个名字"的映射。凡是 il2cpp 没有等价能力（泛型膨胀、
//  assembly_open、vtable 静态字段取址）的，一律返回空值并由上层降级 ——
//  Android 版对应功能本来就依赖 Mono 运行时，iOS 版必须按 il2cpp 重写。
// ============================================================================

#include "IL2CPP/MonoCompat.h"

#include <cstring>
#include <unordered_map>

#include "ModUtils.h"
#include "IL2CPP/Il2CppApi.h"
#include "Includes/Logger.h"

namespace {

using namespace SMIL2CPP;

// ---- 基础映射 ----
void *MonoGetRootDomain() {
    const API &a = Get();
    return a.domain_get ? a.domain_get() : nullptr;
}

void *MonoThreadAttach(void *domain) {
    const API &a = Get();
    if (!a.thread_attach) return nullptr;
    return a.thread_attach(domain ? domain : MonoGetRootDomain());
}

void *MonoImageLoaded(const char *name) {
    if (!name) return nullptr;
    return FindImage(name);
}

void *MonoClassFromName(void *image, const char *nameSpace, const char *className) {
    const API &a = Get();
    if (!className) return nullptr;
    if (!image) {
        // 游戏程序集名在 iOS 上不一定叫 EcsClient.dll（那是 Android 热更名），
        // 找不到镜像时按 全镜像 + 命名空间 搜一次，避免整条功能链直接断掉。
        return FindClassAnywhere(nameSpace, className);
    }
    if (!a.class_from_name) return nullptr;
    return a.class_from_name(image, nameSpace ? nameSpace : "", className);
}

void *MonoClassGetMethodFromName(void *klass, const char *name, int paramCount) {
    const API &a = Get();
    if (!a.class_get_method_from_name || !klass || !name) return nullptr;
    return a.class_get_method_from_name(klass, name, paramCount);
}

void *MonoObjectGetClass(void *obj) {
    const API &a = Get();
    if (!a.object_get_class || !obj) return nullptr;
    return a.object_get_class(obj);
}

void *MonoClassGetFieldFromName(void *klass, const char *name) {
    const API &a = Get();
    if (!a.class_get_field_from_name || !klass || !name) return nullptr;
    return a.class_get_field_from_name(klass, name);
}

void MonoFieldGetValue(void *obj, void *field, void *valuePtr) {
    const API &a = Get();
    if (a.field_get_value && field) a.field_get_value(obj, field, valuePtr);
}

void MonoFieldSetValue(void *obj, void *field, void *valuePtr) {
    const API &a = Get();
    if (a.field_set_value && field) a.field_set_value(obj, field, valuePtr);
}

void *MonoFieldGetValueObject(void * /*domain*/, void *field, void *obj) {
    const API &a = Get();
    if (!a.field_get_value_object || !field) return nullptr;
    return a.field_get_value_object(field, obj);
}

void *MonoRuntimeInvoke(void *method, void *obj, void **params, void **exc) {
    const API &a = Get();
    if (!a.runtime_invoke || !method) return nullptr;
    return a.runtime_invoke(method, obj, params, exc);
}

void *MonoObjectUnbox(void *obj) {
    const API &a = Get();
    if (!a.object_unbox || !obj) return nullptr;
    return a.object_unbox(obj);
}

// MethodInfo 的第一个字段就是 Il2CppMethodPointer（il2cpp 全版本如此）。
// Android 版 _monoCompileMethod 用于把方法变成可 hook 的原生地址。
void *MonoCompileMethod(void *method) {
    if (!method) return nullptr;
    return *(void **)method;
}

void *MonoObjectNew(void * /*domain*/, void *klass) {
    const API &a = Get();
    if (!a.object_new || !klass) return nullptr;
    return a.object_new(klass);
}

void *MonoDomainGet() { return MonoGetRootDomain(); }

void *MonoAssemblyOpen(const char * /*name*/, int *status) {
    if (status) *status = 1;  // 1 = 失败；iOS 没有磁盘上的托管程序集
    return nullptr;
}

void *MonoAssemblyGetImage(void *assembly) { return assembly; }

void *MonoMethodSignature(void *method) { return method; }

// il2cpp 没有 MonoMethodSignature 结构，这里把 method 本身当签名句柄，
// iter 里存"下一个参数下标"。
void *MonoSignatureGetParams(void *sig, void **iter) {
    const API &a = Get();
    if (!sig || !iter || !a.method_get_param_count || !a.method_get_param) return nullptr;
    const uint32_t index = (uint32_t)(uintptr_t)*iter;
    const uint32_t count = a.method_get_param_count(sig);
    if (index >= count) return nullptr;
    *iter = (void *)(uintptr_t)(index + 1);
    return a.method_get_param(sig, index);
}

void *MonoClassFromMonoType(void *type) {
    const API &a = Get();
    if (!a.class_from_type || !type) return nullptr;
    return a.class_from_type(type);
}

void *MonoClassGetParent(void *klass) {
    const API &a = Get();
    if (!a.class_get_parent || !klass) return nullptr;
    return a.class_get_parent(klass);
}

const char *MonoClassGetName(void *klass) {
    const API &a = Get();
    return (a.class_get_name && klass) ? a.class_get_name(klass) : nullptr;
}

const char *MonoClassGetNamespace(void *klass) {
    const API &a = Get();
    return (a.class_get_namespace && klass) ? a.class_get_namespace(klass) : nullptr;
}

void *MonoClassGetImage(void *klass) {
    const API &a = Get();
    return (a.class_get_image && klass) ? a.class_get_image(klass) : nullptr;
}

void *MonoStringNew(void * /*domain*/, const char *text) {
    const API &a = Get();
    if (!a.string_new || !text) return nullptr;
    return a.string_new(text);
}

void *MonoClassGetFields(void *klass, void **iter) {
    const API &a = Get();
    if (!a.class_get_fields || !klass) return nullptr;
    return a.class_get_fields(klass, iter);
}

const char *MonoFieldName(void *field) {
    const API &a = Get();
    return (a.field_get_name && field) ? a.field_get_name(field) : nullptr;
}

int MonoFieldGetOffset(void *field) {
    const API &a = Get();
    return (a.field_get_offset && field) ? (int)a.field_get_offset(field) : -1;
}

void *MonoFieldGetType(void *field) {
    const API &a = Get();
    return (a.field_get_type && field) ? a.field_get_type(field) : nullptr;
}

void *MonoClassVTable(void * /*domain*/, void *klass) { return klass; }

void MonoFieldStaticGetValue(void * /*vtable*/, void *field, void *value) {
    const API &a = Get();
    if (a.field_static_get_value && field) a.field_static_get_value(field, value);
}

void MonoFieldStaticSetValue(void * /*vtable*/, void *field, void *value) {
    const API &a = Get();
    if (a.field_static_set_value && field) a.field_static_set_value(field, value);
}

void *MonoClassGetType(void *klass) {
    const API &a = Get();
    return (a.class_get_type && klass) ? a.class_get_type(klass) : nullptr;
}

void *MonoTypeGetObject(void * /*domain*/, void *type) {
    const API &a = Get();
    return (a.type_get_object && type) ? a.type_get_object(type) : nullptr;
}

int MonoClassValueSize(void * /*klass*/, uint32_t *align) {
    if (align) *align = 4;
    return 0;
}

// il2cpp 没有 MonoGenericContext；返回空让调用点走"跳过泛型"的分支。
void *MonoMetadataGetGenericInst(int /*argc*/, void ** /*argv*/) { return nullptr; }
void *MonoClassInflateGenericMethod(void * /*method*/, void * /*context*/) { return nullptr; }
void *MonoClassInflateGenericType(void * /*type*/, void * /*context*/) { return nullptr; }
void *MonoClassInflateGenericTypeChecked(void * /*type*/, void * /*context*/, void * /*err*/) {
    return nullptr;
}
void *MonoClassInflateGenericMethodChecked(void * /*method*/, void * /*context*/, void * /*err*/) {
    return nullptr;
}

void *MonoClassGetMethods(void *klass, void **iter) {
    const API &a = Get();
    if (!a.class_get_methods || !klass) return nullptr;
    return a.class_get_methods(klass, iter);
}

const char *MonoMethodGetName(void *method) {
    const API &a = Get();
    return (a.method_get_name && method) ? a.method_get_name(method) : nullptr;
}

uint32_t MonoMethodGetFlags(void *method, uint32_t *iflags) {
    if (iflags) *iflags = 0;
    // il2cpp 的 flags 访问器是内部符号，能解析到就用，拿不到按"未知"处理。
    using FlagsFn = uint32_t (*)(void *, uint32_t *);
    static FlagsFn fn = (FlagsFn)Symbol("il2cpp_method_get_flags");
    if (fn && method) return fn(method, iflags);
    return 0;
}

void *MonoImageGetTableInfo(void *image, int /*tableId*/) { return image; }

int MonoTableInfoGetRows(void *tableInfo) {
    const API &a = Get();
    if (!a.image_get_class_count || !tableInfo) return 0;
    return (int)a.image_get_class_count(tableInfo);
}

void *MonoClassGet(void *image, uint32_t typeToken) {
    const API &a = Get();
    if (!a.image_get_class || !image) return nullptr;
    const size_t index = (size_t)((typeToken & 0x00FFFFFFu) - 1u);
    return a.image_get_class(image, index);
}

int MonoTypeGetType(void *type) {
    const API &a = Get();
    return (a.type_get_type && type) ? a.type_get_type(type) : -1;
}

void *MonoRuntimeObjectInit(void *obj) {
    const API &a = Get();
    if (!a.runtime_object_init || !obj) return nullptr;
    return a.runtime_object_init(obj);
}

void MonoAssemblyForeach(void (*callback)(void *assembly, void *user), void *user) {
    const API &a = Get();
    if (!callback || !a.domain_get || !a.domain_get_assemblies) return;
    void *domain = a.domain_get();
    if (!domain) return;
    size_t count = 0;
    void **assemblies = (void **)a.domain_get_assemblies(domain, &count);
    if (!assemblies) return;
    for (size_t i = 0; i < count; ++i) {
        void *image = a.assembly_get_image ? a.assembly_get_image(assemblies[i]) : nullptr;
        if (image) callback(image, user);
    }
}

uint32_t MonoGcHandleNew(void *obj, int pinned) {
    const API &a = Get();
    if (!a.gchandle_new || !obj) return 0;
    return a.gchandle_new(obj, pinned != 0);
}

void MonoGcHandleFree(uint32_t handle) {
    const API &a = Get();
    if (a.gchandle_free && handle) a.gchandle_free(handle);
}

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
    SM_FN_ENTRY("mono_signature_get_return_type", nullptr),
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
    SM_FN_ENTRY("mono_class_vtable", MonoClassVTable),
    SM_FN_ENTRY("mono_field_static_get_value", MonoFieldStaticGetValue),
    SM_FN_ENTRY("mono_field_static_set_value", MonoFieldStaticSetValue),
    SM_FN_ENTRY("mono_class_get_type", MonoClassGetType),
    SM_FN_ENTRY("mono_type_get_object", MonoTypeGetObject),
    SM_FN_ENTRY("mono_class_value_size", MonoClassValueSize),
    SM_FN_ENTRY("mono_metadata_get_generic_inst", MonoMetadataGetGenericInst),
    SM_FN_ENTRY("mono_class_inflate_generic_method", MonoClassInflateGenericMethod),
    SM_FN_ENTRY("mono_class_inflate_generic_type", MonoClassInflateGenericType),
    SM_FN_ENTRY("mono_class_inflate_generic_type_checked", MonoClassInflateGenericTypeChecked),
    SM_FN_ENTRY("mono_class_inflate_generic_method_checked", MonoClassInflateGenericMethodChecked),
    SM_FN_ENTRY("mono_class_get_methods", MonoClassGetMethods),
    SM_FN_ENTRY("mono_method_get_name", MonoMethodGetName),
    SM_FN_ENTRY("mono_method_get_flags", MonoMethodGetFlags),
    SM_FN_ENTRY("mono_image_get_name", nullptr),
    SM_FN_ENTRY("mono_image_get_table_info", MonoImageGetTableInfo),
    SM_FN_ENTRY("mono_table_info_get_rows", MonoTableInfoGetRows),
    SM_FN_ENTRY("mono_class_get", MonoClassGet),
    SM_FN_ENTRY("mono_type_get_type", MonoTypeGetType),
    SM_FN_ENTRY("mono_runtime_object_init", MonoRuntimeObjectInit),
    SM_FN_ENTRY("mono_assembly_foreach", MonoAssemblyForeach),
    SM_FN_ENTRY("mono_gchandle_new", MonoGcHandleNew),
    SM_FN_ENTRY("mono_gchandle_free", MonoGcHandleFree),
    SM_FN_ENTRY("mono_image_get_name_fn", nullptr),
};

#undef SM_FN_ENTRY

// mono_image_get_name 需要单独的签名（返回 const char*）。
const char *MonoImageGetName(void *image) {
    const API &a = Get();
    return (a.image_get_name && image) ? a.image_get_name(image) : nullptr;
}

bool g_installed = false;

}  // namespace

void *SMMonoLookup(const char *symbolName) {
    if (!symbolName) return nullptr;
    if (strcmp(symbolName, "mono_image_get_name") == 0) return (void *)MonoImageGetName;
    if (strcmp(symbolName, "mono_signature_get_return_type") == 0) {
        // 返回类型：用 il2cpp_method_get_return_type 顶替。
        static auto fn = [](void *sig) -> void * {
            const API &a = Get();
            return (a.method_get_return_type && sig) ? a.method_get_return_type(sig) : nullptr;
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
    // 每次调用都重装：il2cpp 的符号在进程启动早期可能还没解析出来。
    _monoGetRootDomain = (MonoGetRootDomain_t)SMMonoLookup("mono_get_root_domain");
    _monoDomainGet = (MonoDomainGet_t)SMMonoLookup("mono_domain_get");
    _monoThreadAttach = (MonoThreadAttach_t)SMMonoLookup("mono_thread_attach");
    _monoImageLoaded = (MonoImageLoaded_t)SMMonoLookup("mono_image_loaded");
    _monoClassFromName = (MonoClassFromName_t)SMMonoLookup("mono_class_from_name");
    _monoClassGetMethodFromName =
        (MonoClassGetMethodFromName_t)SMMonoLookup("mono_class_get_method_from_name");
    _monoObjectGetClass = (MonoObjectGetClass_t)SMMonoLookup("mono_object_get_class");
    _monoClassGetFieldFromName =
        (MonoClassGetFieldFromName_t)SMMonoLookup("mono_class_get_field_from_name");
    _monoFieldGetValue = (MonoFieldGetValue_t)SMMonoLookup("mono_field_get_value");
    _monoFieldSetValue = (MonoFieldSetValue_t)SMMonoLookup("mono_field_set_value");
    _monoFieldGetValueObject =
        (MonoFieldGetValueObject_t)SMMonoLookup("mono_field_get_value_object");
    _monoRuntimeInvoke = (MonoRuntimeInvoke_t)SMMonoLookup("mono_runtime_invoke");
    _monoObjectUnbox = (MonoObjectUnbox_t)SMMonoLookup("mono_object_unbox");
    _monoCompileMethod = (MonoCompileMethod_t)SMMonoLookup("mono_compile_method");
    _monoObjectNew = (MonoObjectNew_t)SMMonoLookup("mono_object_new");
    _monoAssemblyOpen = (MonoAssemblyOpen_t)SMMonoLookup("mono_assembly_open");
    _monoAssemblyGetImage = (MonoAssemblyGetImage_t)SMMonoLookup("mono_assembly_get_image");
    _monoMethodSignature = (MonoMethodSignature_t)SMMonoLookup("mono_method_signature");
    _monoSignatureGetParams = (MonoSignatureGetParams_t)SMMonoLookup("mono_signature_get_params");
    _monoClassFromMonoType = (MonoClassFromMonoType_t)SMMonoLookup("mono_class_from_mono_type");
    _monoClassGetParent = (mono_class_get_parent_t)SMMonoLookup("mono_class_get_parent");
    _monoClassGetName = (mono_class_get_name_t)SMMonoLookup("mono_class_get_name");
    _monoStringNew = (mono_string_new_t)SMMonoLookup("mono_string_new");
    _monoClassGetFields = (MonoClassGetFields_t)SMMonoLookup("mono_class_get_fields");
    _monoFieldName = (MonoFieldName_t)SMMonoLookup("mono_field_get_name");
    _monoFieldGetOffset = (MonoFieldGetOffset_t)SMMonoLookup("mono_field_get_offset");
    _monoFieldGetType = (MonoFieldGetType_t)SMMonoLookup("mono_field_get_type");
    _monoClassGetVTable = (mono_class_vtable_t)SMMonoLookup("mono_class_vtable");
    _monoFieldStaticGetValue =
        (mono_field_static_get_value_t)SMMonoLookup("mono_field_static_get_value");
    _monoFieldStaticSetValue =
        (mono_field_static_set_value_t)SMMonoLookup("mono_field_static_set_value");
    _monoClassGetType = (void *(*)(void *))SMMonoLookup("mono_class_get_type");
    _monoTypeGetObject = (void *(*)(void *, void *))SMMonoLookup("mono_type_get_object");
    _monoClassValueSize = (mono_class_value_size_t)SMMonoLookup("mono_class_value_size");
    _mono_metadata_get_generic_inst =
        (mono_metadata_get_generic_inst_t)SMMonoLookup("mono_metadata_get_generic_inst");
    _mono_class_inflate_generic_method =
        (mono_class_inflate_generic_method_t)SMMonoLookup("mono_class_inflate_generic_method");

    g_installed = _monoGetRootDomain && _monoClassFromName && _monoRuntimeInvoke &&
                  _monoImageLoaded && _monoClassGetMethodFromName;
}

bool Probe() {
    const API &api = Get();
    const bool ready = api.domain_get && api.class_from_name && api.runtime_invoke;
    void *image = SMIL2CPP::MainGameImage();
    SMLOG("[MonoCompat] il2cpp=%s image=%p", ready ? "ready" : "missing", image);
    return ready && image != nullptr;
}

}  // namespace SMMono
