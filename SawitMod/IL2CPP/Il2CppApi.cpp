// ============================================================================
//  Il2CppApi.cpp — il2cpp 导出符号解析（dlsym）
// ============================================================================

#include "IL2CPP/Il2CppApi.h"

#include <dlfcn.h>

#include <cstring>
#include <strings.h>
#include <mutex>
#include <sstream>
#include <vector>

#include "App/Platform.h"
#include "Includes/Logger.h"

namespace SMIL2CPP {
namespace {

template <typename T>
void Bind(T &fn, const char *name) {
    fn = reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
    if (!fn) {
        // UnityFramework 在部分 iOS 构建里以 RTLD_LOCAL 载入，再试一次主程序句柄。
        void *mainHandle = dlopen(nullptr, RTLD_LAZY);
        if (mainHandle) fn = reinterpret_cast<T>(dlsym(mainHandle, name));
    }
}

void Resolve(API &api) {
    memset(&api, 0, sizeof(api));
    Bind(api.domain_get, "il2cpp_domain_get");
    Bind(api.domain_get_assemblies, "il2cpp_domain_get_assemblies");
    Bind(api.thread_attach, "il2cpp_thread_attach");
    Bind(api.thread_current, "il2cpp_thread_current");
    Bind(api.assembly_get_image, "il2cpp_assembly_get_image");
    Bind(api.image_get_name, "il2cpp_image_get_name");
    Bind(api.image_get_class_count, "il2cpp_image_get_class_count");
    Bind(api.image_get_class, "il2cpp_image_get_class");
    Bind(api.class_from_name, "il2cpp_class_from_name");
    Bind(api.class_get_name, "il2cpp_class_get_name");
    Bind(api.class_get_namespace, "il2cpp_class_get_namespace");
    Bind(api.class_get_parent, "il2cpp_class_get_parent");
    Bind(api.class_get_image, "il2cpp_class_get_image");
    Bind(api.class_get_method_from_name, "il2cpp_class_get_method_from_name");
    Bind(api.class_get_methods, "il2cpp_class_get_methods");
    Bind(api.class_get_fields, "il2cpp_class_get_fields");
    Bind(api.class_get_field_from_name, "il2cpp_class_get_field_from_name");
    Bind(api.class_get_type, "il2cpp_class_get_type");
    Bind(api.class_from_type, "il2cpp_class_from_type");
    Bind(api.method_get_name, "il2cpp_method_get_name");
    Bind(api.method_get_param_count, "il2cpp_method_get_param_count");
    Bind(api.method_get_param, "il2cpp_method_get_param");
    Bind(api.method_get_return_type, "il2cpp_method_get_return_type");
    Bind(api.method_get_class, "il2cpp_method_get_class");
    Bind(api.field_get_name, "il2cpp_field_get_name");
    Bind(api.field_get_offset, "il2cpp_field_get_offset");
    Bind(api.field_get_type, "il2cpp_field_get_type");
    Bind(api.field_get_value, "il2cpp_field_get_value");
    Bind(api.field_set_value, "il2cpp_field_set_value");
    Bind(api.field_static_get_value, "il2cpp_field_static_get_value");
    Bind(api.field_static_set_value, "il2cpp_field_static_set_value");
    Bind(api.field_get_value_object, "il2cpp_field_get_value_object");
    Bind(api.object_get_class, "il2cpp_object_get_class");
    Bind(api.object_new, "il2cpp_object_new");
    Bind(api.object_unbox, "il2cpp_object_unbox");
    Bind(api.string_new, "il2cpp_string_new");
    Bind(api.string_length, "il2cpp_string_length");
    Bind(api.string_chars, "il2cpp_string_chars");
    Bind(api.type_get_object, "il2cpp_type_get_object");
    Bind(api.type_get_type, "il2cpp_type_get_type");
    Bind(api.runtime_invoke, "il2cpp_runtime_invoke");
    Bind(api.runtime_object_init, "il2cpp_runtime_object_init");
    Bind(api.runtime_object_init, "il2cpp_runtime_object_init_exception");
    Bind(api.gchandle_new, "il2cpp_gchandle_new");
    Bind(api.gchandle_free, "il2cpp_gchandle_free");
    Bind(api.gchandle_get_target, "il2cpp_gchandle_get_target");
}

}  // namespace

const API &Get() {
    static API api;
    static std::once_flag once;
    std::call_once(once, []() { Resolve(api); });
    return api;
}

bool Ready() {
    const API &a = Get();
    return a.domain_get && a.class_from_name && a.runtime_invoke && a.object_get_class;
}

void *Symbol(const char *name) {
    if (!name) return nullptr;
    void *sym = dlsym(RTLD_DEFAULT, name);
    if (!sym) {
        void *mainHandle = dlopen(nullptr, RTLD_LAZY);
        if (mainHandle) sym = dlsym(mainHandle, name);
    }
    return sym;
}

bool EnsureThreadAttached() {
    const API &a = Get();
    if (!a.domain_get || !a.thread_attach) return false;
    void *domain = a.domain_get();
    if (!domain) return false;
    static thread_local bool attached = false;
    if (attached) return true;
    if (a.thread_current && a.thread_current()) {
        attached = true;
        return true;
    }
    attached = a.thread_attach(domain) != nullptr;
    return attached;
}

void *FindImage(const char *name) {
    const API &a = Get();
    if (!a.domain_get || !a.domain_get_assemblies || !a.image_get_name) return nullptr;
    void *domain = a.domain_get();
    if (!domain) return nullptr;
    size_t count = 0;
    void **assemblies = (void **)a.domain_get_assemblies(domain, &count);
    if (!assemblies) return nullptr;

    const bool wantDll = name && strstr(name, ".dll") != nullptr;
    for (size_t i = 0; i < count; ++i) {
        void *image = a.assembly_get_image ? a.assembly_get_image(assemblies[i]) : nullptr;
        if (!image) continue;
        const char *imageName = a.image_get_name(image);
        if (!imageName) continue;
        if (strcmp(imageName, name) == 0) return image;
        if (!wantDll) {
            std::string withDll(imageName);
            if (strcmp(imageName, name) == 0) return image;
        }
        // "Assembly-CSharp" 与 "Assembly-CSharp.dll" 视为同一个；大小写不敏感。
        const size_t len = strlen(name);
        if (strncasecmp(imageName, name, len) == 0) {
            const char *tail = imageName + len;
            if (*tail == '\0' || strcasecmp(tail, ".dll") == 0) return image;
        }
    }
    return nullptr;
}

void *FindClassAnywhere(const char *ns, const char *name, std::string *outImage) {
    const API &a = Get();
    if (!a.class_from_name || !name) return nullptr;

    // 先走常见镜像：命中率高且不扫全表。
    static const char *kPreferred[] = {
        "Assembly-CSharp.dll", "Assembly-CSharp", "UnityEngine.dll", "UnityEngine",
        "mscorlib.dll", "mscorlib", "System.Private.CoreLib", "System.Core.dll",
    };
    for (const char *imageName : kPreferred) {
        void *image = FindImage(imageName);
        if (!image) continue;
        if (void *klass = a.class_from_name(image, ns ? ns : "", name)) {
            if (outImage) *outImage = imageName;
            return klass;
        }
    }

    // 再扫全镜像。
    if (!a.domain_get_assemblies) return nullptr;
    void *domain = a.domain_get();
    if (!domain) return nullptr;
    size_t count = 0;
    void **assemblies = (void **)a.domain_get_assemblies(domain, &count);
    if (!assemblies) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        void *image = a.assembly_get_image ? a.assembly_get_image(assemblies[i]) : nullptr;
        if (!image) continue;
        void *klass = a.class_from_name(image, ns ? ns : "", name);
        if (klass) {
            if (outImage && a.image_get_name) *outImage = a.image_get_name(image);
            return klass;
        }
    }
    return nullptr;
}

void *MainGameImage() {
    if (void *image = FindImage("Assembly-CSharp.dll")) return image;
    if (void *image = FindImage("Assembly-CSharp")) return image;

    const API &a = Get();
    if (!a.domain_get || !a.domain_get_assemblies || !a.image_get_name) return nullptr;
    void *domain = a.domain_get();
    if (!domain) return nullptr;
    size_t count = 0;
    void **assemblies = (void **)a.domain_get_assemblies(domain, &count);
    if (!assemblies) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        void *image = a.assembly_get_image ? a.assembly_get_image(assemblies[i]) : nullptr;
        if (!image) continue;
        const char *name = a.image_get_name(image);
        if (!name) continue;
        if (strstr(name, "Assembly-CSharp")) return image;
    }
    return nullptr;
}

std::string DescribeImages(size_t maxCount) {
    const API &a = Get();
    std::string out;
    if (!a.domain_get || !a.domain_get_assemblies || !a.image_get_name) return out;
    void *domain = a.domain_get();
    if (!domain) return out;
    size_t count = 0;
    void **assemblies = (void **)a.domain_get_assemblies(domain, &count);
    if (!assemblies) return out;
    std::ostringstream oss;
    for (size_t i = 0; i < count && i < maxCount; ++i) {
        void *image = a.assembly_get_image ? a.assembly_get_image(assemblies[i]) : nullptr;
        if (!image) continue;
        const char *name = a.image_get_name(image);
        if (name) oss << name << (i + 1 < count ? ", " : "");
    }
    return oss.str();
}

}  // namespace SMIL2CPP
