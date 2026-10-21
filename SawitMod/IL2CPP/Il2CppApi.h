#pragma once
// ============================================================================
//  Il2CppApi.h — iOS 上的 il2cpp 运行时接口解析
// ----------------------------------------------------------------------------
//  Android 版通过 ByNameModding/fake_dlfcn + xdl 在 libil2cpp.so 里找符号；
//  iOS 的 Unity 把 il2cpp 导出的 C API 放进主二进制（或 UnityFramework），
//  dlsym(RTLD_DEFAULT, "il2cpp_class_from_name") 就能拿到。
//
//  这里把用到的 il2cpp_* 一次性解析成函数指针表：
//    * 缺符号不会崩，对应参数为 nullptr，调用点自行降级
//    * 线程附着（il2cpp_thread_attach）是硬前提，后台线程调托管 API 前必须做
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace SMIL2CPP {

struct Domain;
struct Assembly;
struct Image;
struct Class;
struct Method;
struct Field;
struct Type;
struct Object;
struct String;
struct Array;
struct Exception;

struct API {
    // domain / thread
    void *(*domain_get)();
    void *(*domain_get_assemblies)(void *domain, size_t *count);
    void *(*thread_attach)(void *domain);
    void *(*thread_current)();
    // assembly / image
    void *(*assembly_get_image)(void *assembly);
    const char *(*image_get_name)(void *image);
    size_t (*image_get_class_count)(void *image);
    void *(*image_get_class)(void *image, size_t index);
    // class
    void *(*class_from_name)(void *image, const char *ns, const char *name);
    const char *(*class_get_name)(void *klass);
    const char *(*class_get_namespace)(void *klass);
    void *(*class_get_parent)(void *klass);
    void *(*class_get_image)(void *klass);
    void *(*class_get_method_from_name)(void *klass, const char *name, int argc);
    void *(*class_get_methods)(void *klass, void **iter);
    void *(*class_get_fields)(void *klass, void **iter);
    void *(*class_get_field_from_name)(void *klass, const char *name);
    void *(*class_get_type)(void *klass);
    void *(*class_from_type)(void *type);
    // method
    const char *(*method_get_name)(void *method);
    uint32_t (*method_get_param_count)(void *method);
    void *(*method_get_param)(void *method, uint32_t index);
    void *(*method_get_return_type)(void *method);
    void *(*method_get_class)(void *method);
    // field
    const char *(*field_get_name)(void *field);
    size_t (*field_get_offset)(void *field);
    void *(*field_get_type)(void *field);
    void (*field_get_value)(void *obj, void *field, void *out);
    void (*field_set_value)(void *obj, void *field, void *value);
    void (*field_static_get_value)(void *field, void *out);
    void (*field_static_set_value)(void *field, void *value);
    void *(*field_get_value_object)(void *field, void *obj);
    // object / string / type
    void *(*object_get_class)(void *obj);
    void *(*object_new)(void *klass);
    void *(*object_unbox)(void *obj);
    void *(*string_new)(const char *text);
    int32_t (*string_length)(void *str);
    uint16_t *(*string_chars)(void *str);
    void *(*type_get_object)(void *type);
    int (*type_get_type)(void *type);
    // invoke / gc
    void *(*runtime_invoke)(void *method, void *obj, void **params, void **exc);
    void *(*runtime_object_init)(void *obj);
    uint32_t (*gchandle_new)(void *obj, bool pinned);
    void (*gchandle_free)(uint32_t handle);
    void *(*gchandle_get_target)(uint32_t handle);
};

// 懒解析；未就绪时返回全 nullptr 的表。
const API &Get();

// 运行时是否可用（核心符号齐全）。
bool Ready();

// 按名字取任意导出符号（dlsym），找不到返回 nullptr。
void *Symbol(const char *name);

// 确保当前线程已附着到 il2cpp 域；幂等，返回 false 表示运行时还没起来。
bool EnsureThreadAttached();

// 按名字找已加载的程序集镜像（"Assembly-CSharp" / "Assembly-CSharp.dll" 都行）。
void *FindImage(const char *name);

// 遍历所有镜像找类；outImage 返回命中的镜像名。找不到返回 nullptr。
void *FindClassAnywhere(const char *ns, const char *name, std::string *outImage = nullptr);

// 游戏主程序集（Unity 的 Assembly-CSharp）；找不到时返回第一个非系统镜像。
void *MainGameImage();

// 诊断用：列出前 N 个镜像名。
std::string DescribeImages(size_t maxCount = 32);

}  // namespace SMIL2CPP
