// ============================================================================
//  MonoHot.cpp — 真实 Mono 运行时桥接层实现
// ----------------------------------------------------------------------------
//  三块内容：
//    1) 符号解析：dlsym(RTLD_DEFAULT, "mono_*")（走 Il2CppApi 的 Symbol() 封装，
//       它已经处理过 UnityFramework 以 RTLD_LOCAL 载入时的 dlopen(nullptr) 退路）。
//    2) 句柄归属表：image/class/method/field/type/assembly/signature/vtable/
//       tableinfo/gchandle 各一张（std::shared_mutex 保护，读多写少）。
//    3) 分发包装：所有 mono_* 调用统一在内部 EnsureAttached()，并把结果打标，
//       供 MonoCompat.cpp 的垫片按句柄决定"走真实 mono 还是走 il2cpp"。
//
//  设计取舍（与报告 §7.2 / §3.2 对应）：
//    * mono 和 il2cpp 是两套独立运行时，对象头布局不同。Mono 对象喂给
//      il2cpp_object_get_class 会拿到 MonoVTable*（不是类），后续必崩；所以
//      对象判型必须先 mono 后 il2cpp，而且 mono 结论要靠"类句柄归属 + 受控
//      校验"确认，不能只信一个裸指针。
//    * 受控校验用 sigsetjmp/siglongjmp 兜野指针读越界（iOS）。只在"第一次
//      遇到某个类"时发生，之后命中归属表直接短路；不在热路径上反复装栅栏。
//    * 所有包装在 mono 未就绪（symbolsReady/domainReady 任一为 false）时
//      一律返回 nullptr/false，让 MonoCompat 落回原来的 il2cpp 行为 ——
//      没有 mono 的环境（例如 Windows 编译自检、纯 AOT 机）行为不变。
// ============================================================================

#include "IL2CPP/MonoHot.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <strings.h>

#include "IL2CPP/Il2CppApi.h"
#include "Includes/Logger.h"

// 对象判型的信号栅栏：只在 Apple（真机/iOS 模拟器）编译。
// 关掉它 = 受控校验退化成普通调用（野指针直接崩），仅用于排查栅栏本身的干扰。
#ifndef SM_MONOHOT_SIGNAL_GUARD
#define SM_MONOHOT_SIGNAL_GUARD 1
#endif

#if defined(__APPLE__) && SM_MONOHOT_SIGNAL_GUARD
#define SM_MONOHOT_HAVE_SIGNAL_GUARD 1
#include <setjmp.h>
#include <signal.h>
#endif

namespace SMMonoHot {
namespace {

// ===========================================================================
//  1. Mono API 表
// ===========================================================================
using MonoGetRootDomainFn = void *(*)();
using MonoDomainGetFn = void *(*)();
using MonoThreadAttachFn = void *(*)(void *);
using MonoImageLoadedFn = void *(*)(const char *);
using MonoImageGetNameFn = const char *(*)(void *);
using MonoImageGetTableInfoFn = void *(*)(void *, int);
using MonoTableInfoGetRowsFn = int (*)(void *);
using MonoAssemblyGetImageFn = void *(*)(void *);
using MonoAssemblyForeachFn = void (*)(void (*)(void *, void *), void *);
using MonoDomainAssemblyOpenFn = void *(*)(void *, const char *);
using MonoAssemblyOpenFn = void *(*)(const char *, void *);
using MonoClassFromNameFn = void *(*)(void *, const char *, const char *);
using MonoClassGetNameFn = const char *(*)(void *);
using MonoClassGetNamespaceFn = const char *(*)(void *);
using MonoClassGetParentFn = void *(*)(void *);
using MonoClassGetImageFn = void *(*)(void *);
using MonoClassGetMethodFromNameFn = void *(*)(void *, const char *, int);
using MonoClassGetMethodsFn = void *(*)(void *, void **);
using MonoClassGetFieldsFn = void *(*)(void *, void **);
using MonoClassGetFieldFromNameFn = void *(*)(void *, const char *);
using MonoClassGetTypeFn = void *(*)(void *);
using MonoClassFromMonoTypeFn = void *(*)(void *);
using MonoClassVTableFn = void *(*)(void *, void *);
using MonoClassValueSizeFn = int (*)(void *, uint32_t *);
using MonoClassGetFn = void *(*)(void *, uint32_t);
using MonoMethodGetNameFn = const char *(*)(void *);
using MonoMethodGetFlagsFn = uint32_t (*)(void *, uint32_t *);
using MonoMethodSignatureFn = void *(*)(void *);
using MonoSignatureGetParamsFn = void *(*)(void *, void **);
using MonoSignatureGetReturnTypeFn = void *(*)(void *);
using MonoCompileMethodFn = void *(*)(void *);
using MonoMethodGetUnmanagedThunkFn = void *(*)(void *);
using MonoFieldGetNameFn = const char *(*)(void *);
using MonoFieldGetOffsetFn = int (*)(void *);
using MonoFieldGetTypeFn = void *(*)(void *);
using MonoFieldGetParentFn = void *(*)(void *);
using MonoFieldGetValueFn = void (*)(void *, void *, void *);
using MonoFieldSetValueFn = void (*)(void *, void *, void *);
using MonoFieldGetValueObjectFn = void *(*)(void *, void *, void *);
using MonoFieldStaticGetValueFn = void (*)(void *, void *, void *);
using MonoFieldStaticSetValueFn = void (*)(void *, void *, void *);
using MonoObjectGetClassFn = void *(*)(void *);
using MonoObjectNewFn = void *(*)(void *, void *);
using MonoObjectUnboxFn = void *(*)(void *);
using MonoStringNewFn = void *(*)(void *, const char *);
using MonoArrayNewFn = void *(*)(void *, void *, uintptr_t);
using MonoArrayLengthFn = int (*)(void *);
using MonoArrayAddrWithSizeFn = void *(*)(void *, int, uintptr_t);
using MonoRuntimeInvokeFn = void *(*)(void *, void *, void **, void **);
using MonoRuntimeObjectInitFn = void (*)(void *);
using MonoTypeGetObjectFn = void *(*)(void *, void *);
using MonoTypeGetTypeFn = int (*)(void *);
using MonoTypeGetUnderlyingFn = void *(*)(void *);
using MonoGcHandleNewFn = uint32_t (*)(void *, int);
using MonoGcHandleFreeFn = void (*)(uint32_t);
using MonoMetadataGetGenericInstFn = void *(*)(int, void **);
using MonoInflateGenericTypeFn = void *(*)(void *, void *);
using MonoInflateGenericMethodFn = void *(*)(void *, void *);
using MonoInflateTypeCheckedFn = void *(*)(void *, void *, void *);
using MonoInflateMethodCheckedFn = void *(*)(void *, void *, void *);

struct MonoApi {
    MonoGetRootDomainFn get_root_domain = nullptr;
    MonoDomainGetFn domain_get = nullptr;
    MonoThreadAttachFn thread_attach = nullptr;
    MonoImageLoadedFn image_loaded = nullptr;
    MonoImageGetNameFn image_get_name = nullptr;
    MonoImageGetTableInfoFn image_get_table_info = nullptr;
    MonoTableInfoGetRowsFn table_info_get_rows = nullptr;
    MonoAssemblyGetImageFn assembly_get_image = nullptr;
    MonoAssemblyForeachFn assembly_foreach = nullptr;
    MonoDomainAssemblyOpenFn domain_assembly_open = nullptr;
    MonoAssemblyOpenFn assembly_open = nullptr;
    MonoClassFromNameFn class_from_name = nullptr;
    MonoClassGetNameFn class_get_name = nullptr;
    MonoClassGetNamespaceFn class_get_namespace = nullptr;
    MonoClassGetParentFn class_get_parent = nullptr;
    MonoClassGetImageFn class_get_image = nullptr;
    MonoClassGetMethodFromNameFn class_get_method_from_name = nullptr;
    MonoClassGetMethodsFn class_get_methods = nullptr;
    MonoClassGetFieldsFn class_get_fields = nullptr;
    MonoClassGetFieldFromNameFn class_get_field_from_name = nullptr;
    MonoClassGetTypeFn class_get_type = nullptr;
    MonoClassFromMonoTypeFn class_from_mono_type = nullptr;
    MonoClassVTableFn class_vtable = nullptr;
    MonoClassValueSizeFn class_value_size = nullptr;
    MonoClassGetFn class_get = nullptr;
    MonoMethodGetNameFn method_get_name = nullptr;
    MonoMethodGetFlagsFn method_get_flags = nullptr;
    MonoMethodSignatureFn method_signature = nullptr;
    MonoSignatureGetParamsFn signature_get_params = nullptr;
    MonoSignatureGetReturnTypeFn signature_get_return_type = nullptr;
    MonoCompileMethodFn compile_method = nullptr;
    MonoMethodGetUnmanagedThunkFn method_get_unmanaged_thunk = nullptr;
    MonoFieldGetNameFn field_get_name = nullptr;
    MonoFieldGetOffsetFn field_get_offset = nullptr;
    MonoFieldGetTypeFn field_get_type = nullptr;
    MonoFieldGetParentFn field_get_parent = nullptr;
    MonoFieldGetValueFn field_get_value = nullptr;
    MonoFieldSetValueFn field_set_value = nullptr;
    MonoFieldGetValueObjectFn field_get_value_object = nullptr;
    MonoFieldStaticGetValueFn field_static_get_value = nullptr;
    MonoFieldStaticSetValueFn field_static_set_value = nullptr;
    MonoObjectGetClassFn object_get_class = nullptr;
    MonoObjectNewFn object_new = nullptr;
    MonoObjectUnboxFn object_unbox = nullptr;
    MonoStringNewFn string_new = nullptr;
    MonoArrayNewFn array_new = nullptr;
    MonoArrayLengthFn array_length = nullptr;
    MonoArrayAddrWithSizeFn array_addr_with_size = nullptr;
    MonoRuntimeInvokeFn runtime_invoke = nullptr;
    MonoRuntimeObjectInitFn runtime_object_init = nullptr;
    MonoTypeGetObjectFn type_get_object = nullptr;
    MonoTypeGetTypeFn type_get_type = nullptr;
    MonoTypeGetUnderlyingFn type_get_underlying_type = nullptr;
    MonoGcHandleNewFn gchandle_new = nullptr;
    MonoGcHandleFreeFn gchandle_free = nullptr;
    MonoMetadataGetGenericInstFn metadata_get_generic_inst = nullptr;
    MonoInflateGenericTypeFn class_inflate_generic_type = nullptr;
    MonoInflateGenericMethodFn class_inflate_generic_method = nullptr;
    MonoInflateTypeCheckedFn class_inflate_generic_type_checked = nullptr;
    MonoInflateMethodCheckedFn class_inflate_generic_method_checked = nullptr;
};

MonoApi g_api;
std::once_flag g_initOnce;
std::atomic<bool> g_symbolsReady{false};
std::atomic<bool> g_coreReady{false};
std::atomic<bool> g_initialized{false};
std::atomic<int> g_symbolsResolved{0};
std::atomic<int> g_symbolsTotal{0};
std::atomic<void *> g_rootDomainCache{nullptr};

// 核心符号：缺任何一个都不算"能安全调 mono"。
// 只把**分发逻辑离不开的**几个算核心；其余（object_new / string_new / 泛型 /
// 字段遍历…）缺了就让对应垫片单独降级，不能因为一个次要符号缺失把整个桥关掉。
bool CheckCore() {
    return g_api.get_root_domain && g_api.thread_attach && g_api.image_loaded &&
           g_api.class_from_name && g_api.class_get_method_from_name &&
           g_api.runtime_invoke && g_api.object_get_class &&
           g_api.class_get_field_from_name;
}

template <typename T>
void Bind(T &fn, const char *name) {
    fn = reinterpret_cast<T>(SMIL2CPP::Symbol(name));
    ++g_symbolsTotal;
    if (fn) ++g_symbolsResolved;
}

void ResolveSymbols() {
    Bind(g_api.get_root_domain, "mono_get_root_domain");
    Bind(g_api.domain_get, "mono_domain_get");
    Bind(g_api.thread_attach, "mono_thread_attach");
    Bind(g_api.image_loaded, "mono_image_loaded");
    Bind(g_api.image_get_name, "mono_image_get_name");
    Bind(g_api.image_get_table_info, "mono_image_get_table_info");
    Bind(g_api.table_info_get_rows, "mono_table_info_get_rows");
    Bind(g_api.assembly_get_image, "mono_assembly_get_image");
    Bind(g_api.assembly_foreach, "mono_assembly_foreach");
    Bind(g_api.domain_assembly_open, "mono_domain_assembly_open");
    Bind(g_api.assembly_open, "mono_assembly_open");
    Bind(g_api.class_from_name, "mono_class_from_name");
    Bind(g_api.class_get_name, "mono_class_get_name");
    Bind(g_api.class_get_namespace, "mono_class_get_namespace");
    Bind(g_api.class_get_parent, "mono_class_get_parent");
    Bind(g_api.class_get_image, "mono_class_get_image");
    Bind(g_api.class_get_method_from_name, "mono_class_get_method_from_name");
    Bind(g_api.class_get_methods, "mono_class_get_methods");
    Bind(g_api.class_get_fields, "mono_class_get_fields");
    Bind(g_api.class_get_field_from_name, "mono_class_get_field_from_name");
    Bind(g_api.class_get_type, "mono_class_get_type");
    Bind(g_api.class_from_mono_type, "mono_class_from_mono_type");
    Bind(g_api.class_vtable, "mono_class_vtable");
    Bind(g_api.class_value_size, "mono_class_value_size");
    Bind(g_api.class_get, "mono_class_get");
    Bind(g_api.method_get_name, "mono_method_get_name");
    Bind(g_api.method_get_flags, "mono_method_get_flags");
    Bind(g_api.method_signature, "mono_method_signature");
    Bind(g_api.signature_get_params, "mono_signature_get_params");
    Bind(g_api.signature_get_return_type, "mono_signature_get_return_type");
    Bind(g_api.compile_method, "mono_compile_method");
    Bind(g_api.method_get_unmanaged_thunk, "mono_method_get_unmanaged_thunk");
    Bind(g_api.field_get_name, "mono_field_get_name");
    Bind(g_api.field_get_offset, "mono_field_get_offset");
    Bind(g_api.field_get_type, "mono_field_get_type");
    Bind(g_api.field_get_parent, "mono_field_get_parent");
    Bind(g_api.field_get_value, "mono_field_get_value");
    Bind(g_api.field_set_value, "mono_field_set_value");
    Bind(g_api.field_get_value_object, "mono_field_get_value_object");
    Bind(g_api.field_static_get_value, "mono_field_static_get_value");
    Bind(g_api.field_static_set_value, "mono_field_static_set_value");
    Bind(g_api.object_get_class, "mono_object_get_class");
    Bind(g_api.object_new, "mono_object_new");
    Bind(g_api.object_unbox, "mono_object_unbox");
    Bind(g_api.string_new, "mono_string_new");
    Bind(g_api.array_new, "mono_array_new");
    Bind(g_api.array_length, "mono_array_length");
    Bind(g_api.array_addr_with_size, "mono_array_addr_with_size");
    Bind(g_api.runtime_invoke, "mono_runtime_invoke");
    Bind(g_api.runtime_object_init, "mono_runtime_object_init");
    Bind(g_api.type_get_object, "mono_type_get_object");
    Bind(g_api.type_get_type, "mono_type_get_type");
    // 缺这个符号不影响任何既有路径：只在 byref/枚举解包时当额外一次机会。
    Bind(g_api.type_get_underlying_type, "mono_type_get_underlying_type");
    Bind(g_api.gchandle_new, "mono_gchandle_new");
    Bind(g_api.gchandle_free, "mono_gchandle_free");
    Bind(g_api.metadata_get_generic_inst, "mono_metadata_get_generic_inst");
    Bind(g_api.class_inflate_generic_type, "mono_class_inflate_generic_type");
    Bind(g_api.class_inflate_generic_method, "mono_class_inflate_generic_method");
    Bind(g_api.class_inflate_generic_type_checked,
         "mono_class_inflate_generic_type_checked");
    Bind(g_api.class_inflate_generic_method_checked,
         "mono_class_inflate_generic_method_checked");

    // 降级：个别 mono 版本只导出 mono_domain_get / 名字不同，能拿到就用它当
    // root domain（mono_domain_get 在已 attach 线程上返回同一个 root domain）。
    if (!g_api.get_root_domain && g_api.domain_get) {
        g_api.get_root_domain = g_api.domain_get;
        ++g_symbolsResolved;
    }

    g_coreReady.store(CheckCore());
    g_symbolsReady.store(g_coreReady.load());
    SMLOG("[MonoHot] mono symbols %d/%d core=%d", g_symbolsResolved.load(),
          g_symbolsTotal.load(), g_coreReady.load() ? 1 : 0);
}

// ===========================================================================
//  2. 对象判型信号栅栏（仅 Apple）
// ===========================================================================
#if defined(SM_MONOHOT_HAVE_SIGNAL_GUARD)
constexpr int kGuardStack = 4;
thread_local sigjmp_buf t_guardBufs[kGuardStack];
thread_local int t_guardDepth = 0;

struct sigaction g_prevActions[2];
std::atomic<bool> g_guardInstalled{false};
std::once_flag g_guardOnce;

void ChainToPreviousHandler(int sig, struct sigaction *prev) {
    if (prev && prev->sa_handler != SIG_DFL && prev->sa_handler != SIG_IGN &&
        prev->sa_handler != nullptr) {
        prev->sa_handler(sig);   // 交给 CrashHandler 写日志并重新 raise
        return;
    }
    static const struct sigaction kDefaultAction = [] {
        struct sigaction a;
        memset(&a, 0, sizeof(a));
        a.sa_handler = SIG_DFL;
        sigemptyset(&a.sa_mask);
        return a;
    }();
    sigaction(sig, &kDefaultAction, nullptr);
    raise(sig);
}

void MonoHotSignalHandler(int sig) {
    const int depth = t_guardDepth;
    if (depth > 0) {
        t_guardDepth = depth - 1;
        siglongjmp(t_guardBufs[depth - 1], 1);
    }
    ChainToPreviousHandler(sig, sig == SIGBUS ? &g_prevActions[1] : &g_prevActions[0]);
}

void InstallSignalGuard() {
    std::call_once(g_guardOnce, []() {
        auto install = [](int sig, int index) {
            struct sigaction action;
            memset(&action, 0, sizeof(action));
            action.sa_handler = MonoHotSignalHandler;
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_RESTART;
            sigaction(sig, &action, &g_prevActions[index]);
        };
        // 只接管"读野指针"这一类可恢复信号；SIGABRT/SIGILL 之类仍然交给
        // CrashHandler（在 ChainToPreviousHandler 里转发）。
        install(SIGSEGV, 0);
        install(SIGBUS, 1);
        g_guardInstalled.store(true);
        SMLOG("[MonoHot] signal guard installed (SIGSEGV/SIGBUS)");
    });
}

template <typename Fn>
bool GuardCall(Fn &&fn) {
    if (g_guardInstalled.load()) {
        const int slot = t_guardDepth;
        if (slot >= kGuardStack) return false;
        t_guardDepth = slot + 1;
        if (sigsetjmp(t_guardBufs[slot], 1) != 0) return false;  // 野指针读越界
        bool ok = false;
        try {
            ok = fn();
        } catch (...) {
            ok = false;
        }
        t_guardDepth = slot;
        return ok;
    }
    try {
        return fn();
    } catch (...) {
        return false;
    }
}
#else
template <typename Fn>
bool GuardCall(Fn &&fn) {
    try {
        return fn();
    } catch (...) {
        return false;
    }
}
void InstallSignalGuard() {}
#endif

// ===========================================================================
//  3. 句柄归属表
// ===========================================================================
//  读多写少（每个新句柄只写一次，之后全命中）；shared_mutex 的读锁开销
//  可接受，且避免热路径上的数据竞争。
struct TagSet {
    void Mark(const void *p) {
        if (!p) return;
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            if (set_.find(p) != set_.end()) return;
        }
        std::unique_lock<std::shared_mutex> lock(mutex_);
        set_.insert(p);
    }
    bool Has(const void *p) const {
        if (!p) return false;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return set_.find(p) != set_.end();
    }
    size_t Size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return set_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_set<const void *> set_;
};

TagSet g_monoImages;
TagSet g_monoClasses;
TagSet g_monoMethods;
TagSet g_monoFields;
TagSet g_monoTypes;
TagSet g_monoAssemblies;
TagSet g_monoSignatures;
TagSet g_monoVTables;
TagSet g_monoTableInfos;
TagSet g_il2cppClasses;

std::mutex g_gcHandleMutex;
std::unordered_set<uint32_t> g_monoGcHandles;

void TagMonoImage(void *p) { g_monoImages.Mark(p); }
void TagMonoClass(void *p) { g_monoClasses.Mark(p); }
void TagMonoMethod(void *p) { g_monoMethods.Mark(p); }
void TagMonoField(void *p) { g_monoFields.Mark(p); }
void TagMonoType(void *p) { g_monoTypes.Mark(p); }
void TagMonoAssembly(void *p) { g_monoAssemblies.Mark(p); }
void TagMonoSignature(void *p) { g_monoSignatures.Mark(p); }
void TagMonoVTable(void *p) { g_monoVTables.Mark(p); }
void TagMonoTableInfo(void *p) { g_monoTableInfos.Mark(p); }
void TagMonoGcHandle(uint32_t h) {
    if (!h) return;
    std::lock_guard<std::mutex> lock(g_gcHandleMutex);
    g_monoGcHandles.insert(h);
}
bool TakeMonoGcHandle(uint32_t h) {
    if (!h) return false;
    std::lock_guard<std::mutex> lock(g_gcHandleMutex);
    return g_monoGcHandles.erase(h) != 0;
}

// ===========================================================================
//  4. 运行时状态 / 计数 / 自检快照
// ===========================================================================
std::atomic<uint64_t> g_routesMono{0};
std::atomic<uint64_t> g_routesIl2Cpp{0};
std::atomic<uint64_t> g_compileHits{0};
std::atomic<int64_t> g_lastSelfTestMs{0};
std::atomic<bool> g_selfTestComplete{false};
std::mutex g_selfTestMutex;
std::mutex g_classCacheMutex;
thread_local Hint g_threadHint = Hint::None;

enum ErrorCode {
    kErrNone = 0,
    kErrNoSymbols = 1,
    kErrNoDomain = 2,
    kErrNoAssemblies = 3,
    kErrNoHotImage = 4,
    kErrNoHotClass = 5,
};
const char *ErrorName(int code) {
    switch (code) {
        case kErrNone: return "ok";
        case kErrNoSymbols: return "no-mono-symbols";
        case kErrNoDomain: return "mono-domain-not-ready";
        case kErrNoAssemblies: return "no-mono-assemblies";
        case kErrNoHotImage: return "no-hot-assembly-image";
        case kErrNoHotClass: return "no-hot-class";
        default: return "unknown";
    }
}

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ===========================================================================
//  5. 图片索引（mono_assembly_foreach）
// ===========================================================================
std::mutex g_imageIndexMutex;
std::vector<void *> g_images;
std::vector<std::string> g_imageNames;
int64_t g_imageIndexAt = 0;
bool g_imageIndexBuilt = false;
std::atomic<int> g_assemblyCount{0};

void TagEverythingCallback(void *assembly, void *user) {
    auto *ctx = static_cast<std::pair<std::vector<void *> *, std::vector<std::string> *> *>(user);
    if (!assembly || !ctx) return;
    TagMonoAssembly(assembly);
    if (!g_api.assembly_get_image) return;
    void *image = g_api.assembly_get_image(assembly);
    if (!image) return;
    TagMonoImage(image);
    const char *name = g_api.image_get_name ? g_api.image_get_name(image) : nullptr;
    ctx->first->push_back(image);
    ctx->second->push_back(name ? std::string(name) : std::string());
}

void EnsureImageIndex(bool force) {
    if (!Available() || !g_api.assembly_foreach || !g_api.assembly_get_image) return;
    std::lock_guard<std::mutex> lock(g_imageIndexMutex);
    const int64_t now = NowMs();
    if (!force && g_imageIndexBuilt && now - g_imageIndexAt < 2000) return;
    std::vector<void *> images;
    std::vector<std::string> names;
    std::pair<std::vector<void *> *, std::vector<std::string> *> ctx{&images, &names};
    images.reserve(160);
    names.reserve(160);
    g_api.assembly_foreach(TagEverythingCallback, &ctx);
    g_images.swap(images);
    g_imageNames.swap(names);
    g_imageIndexAt = now;
    g_imageIndexBuilt = true;
    g_assemblyCount.store(static_cast<int>(g_images.size()));
    SMLOG("[MonoHot] mono assemblies=%zu", g_images.size());
}

void SnapshotImages(std::vector<void *> *images, std::vector<std::string> *names) {
    if (!images || !names) return;
    std::lock_guard<std::mutex> lock(g_imageIndexMutex);
    *images = g_images;
    *names = g_imageNames;
}

bool ImageNameRegistered(const char *name) {
    if (!name || !*name) return false;
    EnsureImageIndex(false);   // 索引不可用时函数内部直接返回，不影响判断
    std::lock_guard<std::mutex> lock(g_imageIndexMutex);
    if (!g_imageIndexBuilt || g_imageNames.empty()) return true;  // 索引不可用：交给形状校验
    for (const std::string &known : g_imageNames) {
        if (strcasecmp(known.c_str(), name) == 0) return true;
    }
    return false;
}

// ===========================================================================
//  6. 字符串 / 类名形状校验（对象判型用，防止把 AOT 对象认成 Mono 对象）
// ===========================================================================
bool CopyPrintable(const char *src, char *out, size_t cap) {
    if (!src || !out || cap < 2) return false;
    size_t i = 0;
    for (; i + 1 < cap; ++i) {
        const char c = src[i];
        if (c == '\0') break;
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f || u >= 0x80) return false;
        out[i] = c;
    }
    if (i == 0 || i + 1 >= cap) return false;
    out[i] = '\0';
    return true;
}

bool LooksLikeImageName(const char *name) {
    if (!name || !*name) return false;
    const size_t len = strlen(name);
    if (len < 3 || len > 127) return false;
    bool hasDot = false;
    for (size_t i = 0; i < len; ++i) {
        const char c = name[i];
        if (c == '.') {
            hasDot = true;
            continue;
        }
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '+' ||
              c == '`'))
            return false;
    }
    if (strstr(name, "...")) return false;
    // Mono image 名一般带 .dll；少数单文件模块可能没有点号，放宽一点但仍要求有内容。
    return hasDot || len >= 4;
}

bool LooksLikeTypeName(const char *name) {
    if (!name || !*name) return false;
    const size_t len = strlen(name);
    if (len > 240) return false;
    const char first = name[0];
    if (!(isalpha(static_cast<unsigned char>(first)) || first == '_' || first == '<' ||
          first == '`' || first == '$'))
        return false;
    for (size_t i = 1; i < len; ++i) {
        const char c = name[i];
        if (isalnum(static_cast<unsigned char>(c))) continue;
        switch (c) {
            case '_': case '.': case '`': case '<': case '>': case '+': case '$':
            case '[': case ']': case ',': case ' ': case '/': case '-':
                continue;
            default:
                return false;
        }
    }
    return true;
}

// ===========================================================================
//  7. 对象判型核心
// ===========================================================================
//  校验一个"疑似 MonoClass*"：读 class 名 + image 名，两者都得像托管标识符；
//  image 名必须出现在真的 mono 程序集清单里（防止把 il2cpp 的 image 指针误判）。
//  整个过程包在信号栅栏里：野指针读越界 -> 返回 false，不杀进程。
bool ValidateMonoClass(void *candidate, std::string *outInfo) {
    if (!candidate || !g_api.class_get_name || !g_api.class_get_image ||
        !g_api.image_get_name)
        return false;

    char className[128] = {0};
    char imageName[128] = {0};
    const bool readOk = GuardCall([&]() -> bool {
        const char *clsName = g_api.class_get_name(candidate);
        if (!CopyPrintable(clsName, className, sizeof(className))) return false;
        void *image = g_api.class_get_image(candidate);
        if (!image) return false;
        const char *imgName = g_api.image_get_name(image);
        if (!CopyPrintable(imgName, imageName, sizeof(imageName))) return false;
        return true;
    });
    if (!readOk) return false;
    if (!LooksLikeTypeName(className)) return false;
    if (!LooksLikeImageName(imageName)) return false;
    if (!ImageNameRegistered(imageName)) return false;

    TagMonoClass(candidate);
    if (outInfo) {
        *outInfo = std::string(className) + " [" + imageName + "]";
    }
    return true;
}

// ===========================================================================
//  8. 真实 mono 包装
// ===========================================================================
void *RootDomainUncached() {
    if (!g_api.get_root_domain) return nullptr;
    return g_api.get_root_domain();
}

void *ImageLoadedImpl(const char *name) {
    if (!name || !*name || !g_api.image_loaded) return nullptr;
    // 1) 原样
    void *image = g_api.image_loaded(name);
    if (image) return image;
    // 2) 带 / 去 .dll
    char buffer[256];
    const size_t len = strlen(name);
    if (len > 0 && len < sizeof(buffer) - 8) {
        if (strcasecmp(name + (len >= 4 ? len - 4 : 0), ".dll") != 0) {
            snprintf(buffer, sizeof(buffer), "%s.dll", name);
            image = g_api.image_loaded(buffer);
            if (image) return image;
        } else {
            memcpy(buffer, name, len - 4);
            buffer[len - 4] = '\0';
            image = g_api.image_loaded(buffer);
            if (image) return image;
        }
    }
    // 3) mscorlib 兼容重映射：热更层是 CoreCLR 风格（System.Private.CoreLib），
    //    老代码按 NetFramework 名字找 corlib 时把它接到 mono 的 corelib 上。
    if (strncasecmp(name, "mscorlib", 8) == 0) {
        static const char *kCorelibAliases[] = {
                "System.Private.CoreLib", "System.Private.CoreLib.dll",
                "System.Runtime", "System.Runtime.dll",
        };
        for (const char *alias : kCorelibAliases) {
            image = g_api.image_loaded(alias);
            if (image) return image;
        }
    }
    return nullptr;
}

// 名字归一化（小写、去目录、去 .dll/.bytes 后缀）——用于"热更程序集优先"判断。
std::string NormalizeImageName(const char *name) {
    if (!name) return std::string();
    std::string out(name);
    const size_t slash = out.find_last_of("/\\");
    if (slash != std::string::npos) out = out.substr(slash + 1);
    if (out.size() > 6 && (out.compare(out.size() - 6, 6, ".bytes") == 0))
        out.resize(out.size() - 6);
    if (out.size() > 4 && (out.compare(out.size() - 4, 4, ".dll") == 0))
        out.resize(out.size() - 4);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return out;
}

// ===========================================================================
//  9. 自检探针表（名字来自 dump-analysis-ios.md §7.1 / ios-antiban-gap.md §3.2）
// ===========================================================================
struct ImageProbe {
    const char *name;
};
const ImageProbe kProbeImages[] = {
        {"EcsClient"},
        {"XDTDataAndProtocol"},
        {"XDTLevelAndEntity"},
        {"XDTGameSystem"},
        {"XDTGameUI"},
        {"XDTBaseService"},
        {"EngineWrapper"},
        {"MonoShared"},
        {"ScriptBridge"},
        {"TypeRegister"},
};
constexpr int kProbeImageCount = static_cast<int>(sizeof(kProbeImages) / sizeof(kProbeImages[0]));

struct ClassProbe {
    const char *image;
    const char *nameSpace;
    const char *className;
    const char *methodName;   // 可空
    int methodArgc;
};
const ClassProbe kProbeClasses[] = {
        {"XDTDataAndProtocol", "XDTDataAndProtocol", "PlayerDataCenter",
         "GetSelfNetPlayerId", 0},
        {"XDTDataAndProtocol", "XDTDataAndProtocol.ComponentsData", "DataCenter", nullptr, 0},
        {"XDTDataAndProtocol", "XDTDataAndProtocol.ProtocolService.Social",
         "ChatProtocolManager", nullptr, 0},
        {"XDTDataAndProtocol", "XDTDataAndProtocol.ProtocolService.GamePlay.Character",
         "CharacterProtocolManager", nullptr, 0},
        {"EcsClient", "", "TableData", nullptr, 0},
        {"EcsClient", "XD.GameGerm.ExpParser", "TableData", nullptr, 0},
        {"XDTLevelAndEntity", "XDTLevelAndEntity.BaseSystem", "InteractSystem",
         "get_Instance", 0},
        {"XDTLevelAndEntity", "XDTLevelAndEntity.Gameplay.Component", "Player", nullptr, 0},
        {"XDTBaseService", "XDTGame.Framework", "Managers", "GetAllManagers", 0},
};
constexpr int kProbeClassCount = static_cast<int>(sizeof(kProbeClasses) / sizeof(kProbeClasses[0]));

// ===========================================================================
//  10. 状态快照
// ===========================================================================
std::mutex g_statusMutex;
Status g_status;

}  // namespace

// ===========================================================================
//  公开接口：生命周期
// ===========================================================================
bool Init() {
    if (g_initialized.load(std::memory_order_acquire)) return g_symbolsReady.load();
    std::call_once(g_initOnce, []() {
        ResolveSymbols();
        InstallSignalGuard();
        g_initialized.store(true, std::memory_order_release);
    });
    return g_symbolsReady.load();
}

bool SymbolsReady() { return Init(); }

void *RootDomain() {
    if (!Init() || !g_api.get_root_domain) return nullptr;
    void *cached = g_rootDomainCache.load(std::memory_order_acquire);
    if (cached) return cached;
    void *domain = RootDomainUncached();
    if (domain) {
        g_rootDomainCache.store(domain, std::memory_order_release);
        SMLOG("[MonoHot] mono root domain=%p", domain);
    }
    return domain;
}

bool Available() {
    if (!Init()) return false;
    if (!g_coreReady.load()) return false;
    return RootDomain() != nullptr;
}

void EnsureAttached() {
    static thread_local int state = 0;          // 0=未知 1=已 attach 2=失败
    static thread_local int64_t nextRetryMs = 0;
    if (state == 1) return;
    if (!Available() || !g_api.thread_attach) return;
    const int64_t now = NowMs();
    if (state == 2 && now < nextRetryMs) return;  // attach 失败不要每次调用都重试
    void *domain = RootDomain();
    if (!domain) return;
    void *thread = g_api.thread_attach(domain);
    if (thread) {
        state = 1;
    } else {
        state = 2;
        nextRetryMs = now + 2000;
    }
}

// ===========================================================================
//  公开接口：计数 / 提示 / 分类
// ===========================================================================
void CountMonoRoute() { ++g_routesMono; }
void CountIl2CppRoute() { ++g_routesIl2Cpp; }

void SetHint(Hint hint) { g_threadHint = hint; }

Hint CurrentHint() { return g_threadHint; }

bool IsMonoImage(void *p) { return g_monoImages.Has(p); }
bool IsMonoClass(void *p) { return g_monoClasses.Has(p); }
bool IsMonoMethod(void *p) { return g_monoMethods.Has(p); }
bool IsMonoField(void *p) { return g_monoFields.Has(p); }
bool IsMonoType(void *p) { return g_monoTypes.Has(p); }
bool IsMonoAssembly(void *p) { return g_monoAssemblies.Has(p); }
bool IsMonoSignature(void *p) { return g_monoSignatures.Has(p); }
bool IsMonoTableInfo(void *p) { return g_monoTableInfos.Has(p); }

void TagIl2CppClass(void *klass) { g_il2cppClasses.Mark(klass); }

bool ClassifyObject(void *obj, void **outMonoClass) {
    if (outMonoClass) *outMonoClass = nullptr;
    if (!obj) return false;
    if (!Init() || !g_coreReady.load()) return false;
    // mono 运行时还没初始化：所有对象都按旧行为（il2cpp）处理，一次 mono 调用都不做。
    if (!Available()) return false;
    EnsureAttached();

    // ① 先按 mono 语义取类。`obj->vtable->klass` 对 il2cpp 对象也同样安全
    //    （obj[0] 与 obj[0][0] 在两套对象头里都是可读指针）。
    void *monoClass = g_api.object_get_class ? g_api.object_get_class(obj) : nullptr;
    if (monoClass && IsMonoClass(monoClass)) {
        if (outMonoClass) *outMonoClass = monoClass;
        return true;
    }

    // ② 已知是 AOT 对象：直接走旧路径（AOT 类句柄由 MonoCompat 的 il2cpp 命中回填）。
    const SMIL2CPP::API &icpp = SMIL2CPP::Get();
    void *icppClass = icpp.object_get_class ? icpp.object_get_class(obj) : nullptr;
    if (icppClass && g_il2cppClasses.Has(icppClass)) return false;

    // ③ 第一次遇到这个类：受控校验 mono 语义下的类指针。
    if (monoClass) {
        std::string info;
        if (ValidateMonoClass(monoClass, &info)) {
            if (outMonoClass) *outMonoClass = monoClass;
            return true;
        }
    }

    if (icppClass) TagIl2CppClass(icppClass);
    return false;
}

// ===========================================================================
//  公开接口：镜像 / 类
// ===========================================================================
bool IsPreferredImage(const char *name) {
    // 热更层独有（加密 15 个 + 明文 BCL/第三方）或与 AOT 同名但 mono 优先的名字。
    // 其余名字保持旧顺序：先 il2cpp，找不到再回退 mono。
    static const char *kPreferredPrefixes[] = {
            "ecsclient", "ecssystem", "enginewrapper", "monoshared", "monounitask",
            "msgpackformatters", "scriptbridge", "xdtbaseservice", "xdtdataandprotocol",
            "xdtgamesystem", "xdtgameui", "xdtlevelandentity", "xdtviewbase",
            "esdk_obs_", "typeregister", "dnsclient", "mscorlib",
            "system.private.corelib", "system.runtime", "system.collections",
            "system.linq", "system.text", "system.net", "system.reflection",
            "system.threading", "system.io", "system.memory", "system.buffers",
            "system.diagnostics", "system.globalization", "system.numerics",
    };
    const std::string norm = NormalizeImageName(name);
    if (norm.empty()) return false;
    for (const char *prefix : kPreferredPrefixes) {
        if (norm.compare(0, strlen(prefix), prefix) == 0) return true;
    }
    return false;
}

void *ImageLoaded(const char *name) {
    if (!name || !*name) return nullptr;
    if (!Available()) return nullptr;
    EnsureAttached();
    if (void *image = ImageLoadedImpl(name)) {
        TagMonoImage(image);
        SetHint(Hint::Mono);
        return image;
    }
    // 精确名没命中：扫一遍真实已加载的 mono image（程序集名可能带版本/后缀差异）。
    EnsureImageIndex(false);
    std::vector<void *> images;
    std::vector<std::string> names;
    SnapshotImages(&images, &names);
    const std::string want = NormalizeImageName(name);
    for (size_t i = 0; i < names.size(); ++i) {
        if (NormalizeImageName(names[i].c_str()) == want) {
            TagMonoImage(images[i]);
            SetHint(Hint::Mono);
            return images[i];
        }
    }
    // 刚加载的热更 DLL：强制刷一次索引再试。
    EnsureImageIndex(true);
    SnapshotImages(&images, &names);
    for (size_t i = 0; i < names.size(); ++i) {
        if (NormalizeImageName(names[i].c_str()) == want) {
            TagMonoImage(images[i]);
            SetHint(Hint::Mono);
            return images[i];
        }
    }
    return nullptr;
}

const char *ImageGetName(void *image) {
    if (!IsMonoImage(image) || !g_api.image_get_name) return nullptr;
    return g_api.image_get_name(image);
}

const char *ImageGetNameSafe(void *image) { return ImageGetName(image); }

void *ClassFromName(void *image, const char *nameSpace, const char *className) {
    if (!className || !IsMonoImage(image)) return nullptr;
    if (!Available() || !g_api.class_from_name) return nullptr;
    EnsureAttached();
    void *klass = g_api.class_from_name(image, nameSpace ? nameSpace : "", className);
    if (klass) {
        TagMonoClass(klass);
        SetHint(Hint::Mono);
    }
    return klass;
}

void *FindClassAnywhere(const char *nameSpace, const char *className,
                        std::string *outImage) {
    if (!className || !*className) return nullptr;
    if (!Available() || !g_api.class_from_name) return nullptr;
    EnsureAttached();
    const char *ns = nameSpace ? nameSpace : "";
    const std::string key = std::string(ns) + "|" + className;
    const int64_t now = NowMs();

    {
        std::lock_guard<std::mutex> lock(g_classCacheMutex);
        static std::unordered_map<std::string, std::pair<void *, std::string>> cache;
        static std::unordered_map<std::string, int64_t> missExpire;
        auto hit = cache.find(key);
        if (hit != cache.end()) {
            if (outImage) *outImage = hit->second.second;
            return hit->second.first;
        }
        auto miss = missExpire.find(key);
        if (miss != missExpire.end() && now < miss->second) return nullptr;

        std::vector<void *> images;
        std::vector<std::string> names;
        SnapshotImages(&images, &names);
        if (images.empty()) {
            EnsureImageIndex(true);
            SnapshotImages(&images, &names);
        }
        for (size_t i = 0; i < images.size(); ++i) {
            void *klass = g_api.class_from_name(images[i], ns, className);
            if (!klass) continue;
            TagMonoClass(klass);
            SetHint(Hint::Mono);
            if (cache.size() > 512) cache.clear();
            cache[key] = std::make_pair(klass, names[i]);
            if (outImage) *outImage = names[i];
            return klass;
        }
        // 可能热更 DLL 是刚加载的：强制重建索引再扫一次。
        EnsureImageIndex(true);
        SnapshotImages(&images, &names);
        for (size_t i = 0; i < images.size(); ++i) {
            void *klass = g_api.class_from_name(images[i], ns, className);
            if (!klass) continue;
            TagMonoClass(klass);
            SetHint(Hint::Mono);
            if (cache.size() > 512) cache.clear();
            cache[key] = std::make_pair(klass, names[i]);
            if (outImage) *outImage = names[i];
            return klass;
        }
        if (missExpire.size() > 256) missExpire.clear();
        missExpire[key] = now + 5000;
        return nullptr;
    }
}

// ===========================================================================
//  公开接口：类 / 方法 / 字段 / 对象
// ===========================================================================
void *ClassGetMethodFromName(void *klass, const char *name, int paramCount) {
    if (!IsMonoClass(klass) || !name || !Available() || !g_api.class_get_method_from_name)
        return nullptr;
    EnsureAttached();
    void *method = g_api.class_get_method_from_name(klass, name, paramCount);
    if (method) {
        TagMonoMethod(method);
        SetHint(Hint::Mono);
    }
    return method;
}

void *ClassGetFieldFromName(void *klass, const char *name) {
    if (!IsMonoClass(klass) || !name || !Available() || !g_api.class_get_field_from_name)
        return nullptr;
    EnsureAttached();
    void *field = g_api.class_get_field_from_name(klass, name);
    if (field) {
        TagMonoField(field);
        SetHint(Hint::Mono);
    }
    return field;
}

void *ClassGetParent(void *klass) {
    if (!IsMonoClass(klass) || !Available() || !g_api.class_get_parent) return nullptr;
    EnsureAttached();
    void *parent = g_api.class_get_parent(klass);
    if (parent) TagMonoClass(parent);
    return parent;
}

const char *ClassGetName(void *klass) {
    if (!IsMonoClass(klass) || !g_api.class_get_name) return nullptr;
    return g_api.class_get_name(klass);
}

const char *ClassGetNamespace(void *klass) {
    if (!IsMonoClass(klass) || !g_api.class_get_namespace) return nullptr;
    return g_api.class_get_namespace(klass);
}

void *ClassGetImage(void *klass) {
    if (!IsMonoClass(klass) || !Available() || !g_api.class_get_image) return nullptr;
    void *image = g_api.class_get_image(klass);
    if (image) TagMonoImage(image);
    return image;
}

void *ClassFromMonoType(void *type) {
    if (!IsMonoType(type) || !Available() || !g_api.class_from_mono_type) return nullptr;
    EnsureAttached();
    void *klass = g_api.class_from_mono_type(type);
    if (klass) TagMonoClass(klass);
    return klass;
}

void *ClassGetType(void *klass) {
    if (!IsMonoClass(klass) || !Available() || !g_api.class_get_type) return nullptr;
    EnsureAttached();
    void *type = g_api.class_get_type(klass);
    if (type) TagMonoType(type);
    return type;
}

void *ClassGetVTable(void *klass) {
    if (!IsMonoClass(klass) || !Available() || !g_api.class_vtable) return nullptr;
    EnsureAttached();
    void *domain = RootDomain();
    if (!domain) return nullptr;
    void *vtable = g_api.class_vtable(domain, klass);
    if (vtable) TagMonoVTable(vtable);
    return vtable;
}

int ClassValueSize(void *klass, uint32_t *align) {
    if (!IsMonoClass(klass) || !Available() || !g_api.class_value_size) return 0;
    EnsureAttached();
    uint32_t localAlign = 0;
    const int size = g_api.class_value_size(klass, align ? align : &localAlign);
    return size;
}

void *ClassGetFields(void *klass, void **iter) {
    if (!IsMonoClass(klass) || !iter || !Available() || !g_api.class_get_fields)
        return nullptr;
    EnsureAttached();
    void *field = g_api.class_get_fields(klass, iter);
    if (field) TagMonoField(field);
    return field;
}

void *ClassGetMethods(void *klass, void **iter) {
    if (!IsMonoClass(klass) || !iter || !Available() || !g_api.class_get_methods)
        return nullptr;
    EnsureAttached();
    void *method = g_api.class_get_methods(klass, iter);
    if (method) TagMonoMethod(method);
    return method;
}

void *ClassGet(void *image, uint32_t typeToken) {
    if (!IsMonoImage(image) || !Available() || !g_api.class_get) return nullptr;
    EnsureAttached();
    void *klass = g_api.class_get(image, typeToken);
    if (klass) TagMonoClass(klass);
    return klass;
}

void *MethodSignature(void *method) {
    if (!IsMonoMethod(method) || !Available() || !g_api.method_signature) return nullptr;
    EnsureAttached();
    void *signature = g_api.method_signature(method);
    if (signature) TagMonoSignature(signature);
    return signature;
}

void *SignatureGetParams(void *signature, void **iter) {
    if (!IsMonoSignature(signature) || !iter || !Available() ||
        !g_api.signature_get_params)
        return nullptr;
    EnsureAttached();
    void *type = g_api.signature_get_params(signature, iter);
    // ★ 参数类型也要打标：否则 MonoClassFromMonoType 走 il2cpp，List<T> 建不出来
    if (type) TagMonoType(type);
    return type;
}

void *SignatureGetReturnType(void *signature) {
    if (!IsMonoSignature(signature) || !Available() ||
        !g_api.signature_get_return_type)
        return nullptr;
    void *type = g_api.signature_get_return_type(signature);
    if (type) TagMonoType(type);
    return type;
}

const char *MethodGetName(void *method) {
    if (!IsMonoMethod(method) || !g_api.method_get_name) return nullptr;
    return g_api.method_get_name(method);
}

uint32_t MethodGetFlags(void *method, uint32_t *iflags) {
    if (!IsMonoMethod(method) || !g_api.method_get_flags) {
        if (iflags) *iflags = 0;
        return 0;
    }
    return g_api.method_get_flags(method, iflags);
}

void *CompileMethod(void *method) {
    if (!IsMonoMethod(method) || !Available()) return nullptr;
    EnsureAttached();
    void *code = nullptr;
    // iOS 的 mono 是 IL 解释器（mono_ee_interp_init），mono_compile_method 一般
    // 返回解释器入口桩；失败时退到 unmanaged thunk（报告 §7.3 建议同一用途）。
    if (g_api.compile_method) code = g_api.compile_method(method);
    if (!code && g_api.method_get_unmanaged_thunk)
        code = g_api.method_get_unmanaged_thunk(method);
    if (code) ++g_compileHits;
    return code;
}

void *FieldGetValueObject(void *field, void *obj) {
    if (!IsMonoField(field) || !Available() || !g_api.field_get_value_object)
        return nullptr;
    EnsureAttached();
    return g_api.field_get_value_object(RootDomain(), field, obj);
}

void FieldGetValue(void *obj, void *field, void *valuePtr) {
    if (!IsMonoField(field) || !valuePtr || !Available() || !g_api.field_get_value)
        return;
    EnsureAttached();
    g_api.field_get_value(obj, field, valuePtr);
}

void FieldSetValue(void *obj, void *field, void *valuePtr) {
    if (!IsMonoField(field) || !valuePtr || !Available() || !g_api.field_set_value)
        return;
    EnsureAttached();
    g_api.field_set_value(obj, field, valuePtr);
}

void *FieldGetType(void *field) {
    if (!IsMonoField(field) || !Available() || !g_api.field_get_type) return nullptr;
    void *type = g_api.field_get_type(field);
    if (type) TagMonoType(type);
    return type;
}

const char *FieldGetName(void *field) {
    if (!IsMonoField(field) || !g_api.field_get_name) return nullptr;
    return g_api.field_get_name(field);
}

int FieldGetOffset(void *field) {
    if (!IsMonoField(field) || !g_api.field_get_offset) return -1;
    return g_api.field_get_offset(field);
}

void FieldStaticGetValue(void *vtable, void *field, void *valuePtr) {
    if (!IsMonoField(field) || !valuePtr || !Available() || !g_api.field_static_get_value)
        return;
    EnsureAttached();
    void *metatable = vtable;
    if (!g_monoVTables.Has(metatable)) {
        void *parent = g_api.field_get_parent ? g_api.field_get_parent(field) : nullptr;
        if (parent && g_api.class_vtable) {
            metatable = g_api.class_vtable(RootDomain(), parent);
            if (metatable) TagMonoVTable(metatable);
        } else {
            metatable = nullptr;
        }
    }
    if (metatable) g_api.field_static_get_value(metatable, field, valuePtr);
}

void FieldStaticSetValue(void *vtable, void *field, void *valuePtr) {
    if (!IsMonoField(field) || !valuePtr || !Available() || !g_api.field_static_set_value)
        return;
    EnsureAttached();
    void *metatable = vtable;
    if (!g_monoVTables.Has(metatable)) {
        void *parent = g_api.field_get_parent ? g_api.field_get_parent(field) : nullptr;
        if (parent && g_api.class_vtable) {
            metatable = g_api.class_vtable(RootDomain(), parent);
            if (metatable) TagMonoVTable(metatable);
        } else {
            metatable = nullptr;
        }
    }
    if (metatable) g_api.field_static_set_value(metatable, field, valuePtr);
}

void *ObjectGetClass(void *obj) {
    if (!obj || !Available() || !g_api.object_get_class) return nullptr;
    EnsureAttached();
    void *klass = g_api.object_get_class(obj);
    if (klass) TagMonoClass(klass);
    return klass;
}

void *ObjectNew(void *klass) {
    if (!IsMonoClass(klass) || !Available() || !g_api.object_new) return nullptr;
    EnsureAttached();
    void *domain = RootDomain();
    if (!domain) return nullptr;
    return g_api.object_new(domain, klass);
}

void *ObjectUnbox(void *obj) {
    if (!obj || !Available() || !g_api.object_unbox) return nullptr;
    EnsureAttached();
    return g_api.object_unbox(obj);
}

void *RuntimeInvoke(void *method, void *obj, void **params, void **exc) {
    if (!IsMonoMethod(method) || !Available() || !g_api.runtime_invoke) return nullptr;
    EnsureAttached();
    return g_api.runtime_invoke(method, obj, params, exc);
}

void *RuntimeObjectInit(void *obj) {
    if (!obj || !Available() || !g_api.runtime_object_init) return nullptr;
    EnsureAttached();
    g_api.runtime_object_init(obj);
    return obj;
}

void *StringNew(const char *text) {
    if (!text || !Available() || !g_api.string_new) return nullptr;
    EnsureAttached();
    void *domain = RootDomain();
    if (!domain) return nullptr;
    return g_api.string_new(domain, text);
}


int ArrayLength(void *array) {
    if (!array || !Available() || !g_api.array_length) return 0;
    EnsureAttached();
    return g_api.array_length(array);
}

void *ArrayAddrWithSize(void *array, int elementSize, uintptr_t index) {
    if (!array || !Available() || !g_api.array_addr_with_size) return nullptr;
    EnsureAttached();
    return g_api.array_addr_with_size(array, elementSize, index);
}

void *ArrayNew(void *elementClass, uintptr_t count) {
    if (!IsMonoClass(elementClass) || !Available() || !g_api.array_new) return nullptr;
    EnsureAttached();
    void *domain = RootDomain();
    if (!domain) return nullptr;
    return g_api.array_new(domain, elementClass, count);
}

void *TypeGetObject(void *type) {
    if (!IsMonoType(type) || !Available() || !g_api.type_get_object) return nullptr;
    EnsureAttached();
    void *domain = RootDomain();
    if (!domain) return nullptr;
    return g_api.type_get_object(domain, type);
}

int TypeGetType(void *type) {
    if (!IsMonoType(type) || !g_api.type_get_type) return -1;
    return g_api.type_get_type(type);
}

// ---------------------------------------------------------------------------
//  类型细节：byref / 底层类型
// ---------------------------------------------------------------------------
//  mono 的方法签名里 `ref T` 有两种可能表示：
//    ① type 字段直接是 MONO_TYPE_BYREF(0x10)，元素类型挂在 union 的 data 上；
//    ② type 字段是元素类型本身，byref 只体现在标志位里（新 mono 的解析路径）。
//  ① 能解包成元素 MonoType，拿去 mono_class_from_mono_type 就能拿到真类；
//  ② 没有可解包的指针，调用方直接用"已知的元素类"另造实例（见 ModFeatures 的
//    SMCreateMonoListForType）。所以这里只报 ①，绝不猜标志位（猜错会把普通
//    类型当 byref 解引用，读到垃圾指针）。
bool TypeIsByRef(void *type) {
    if (!IsMonoType(type) || !g_api.type_get_type) return false;
    return g_api.type_get_type(type) == 0x10;   // MONO_TYPE_BYREF
}

void *TypeGetByRefElement(void *type) {
    if (!TypeIsByRef(type) || !Available()) return nullptr;
    EnsureAttached();
    void *element = nullptr;
    const bool readOk = GuardCall([type, &element]() {
        element = *reinterpret_cast<void **>(type);   // union data.byref
        return element != nullptr;
    });
    if (!readOk || !element || element == type) return nullptr;
    // 形状校验：元素类型的类型码必须落在合法 ELEMENT_TYPE 区间（0x01..0x1e），
    // 否则当解包失败 —— 宁可不造 List，也不要拿野指针去问 mono。
    int elementTypeCode = -1;
    if (!g_api.type_get_type) return nullptr;
    if (!GuardCall([element, &elementTypeCode]() {
            elementTypeCode = g_api.type_get_type(element);
            return true;
        })) {
        return nullptr;
    }
    if (elementTypeCode <= 0 || elementTypeCode > 0x1e) return nullptr;
    TagMonoType(element);
    return element;
}

void *TypeGetUnderlyingType(void *type) {
    if (!IsMonoType(type) || !Available() || !g_api.type_get_underlying_type)
        return nullptr;
    EnsureAttached();
    void *underlying = g_api.type_get_underlying_type(type);
    if (underlying) TagMonoType(underlying);
    return underlying;
}

void *AssemblyOpen(const char *name, int *status) {
    if (status) *status = 1;
    if (!name || !*name || !Available()) return nullptr;
    EnsureAttached();
    void *domain = RootDomain();
    void *assembly = nullptr;
    if (domain && g_api.domain_assembly_open)
        assembly = g_api.domain_assembly_open(domain, name);
    if (!assembly && g_api.assembly_open) {
        int openStatus = 1;
        assembly = g_api.assembly_open(name, &openStatus);
        if (assembly) {
            if (status) *status = openStatus;
        }
    } else if (assembly) {
        if (status) *status = 0;
    }
    if (assembly) {
        TagMonoAssembly(assembly);
        SetHint(Hint::Mono);
    }
    return assembly;
}

void *AssemblyGetImage(void *assembly) {
    if (!IsMonoAssembly(assembly) || !Available() || !g_api.assembly_get_image)
        return nullptr;
    EnsureAttached();
    void *image = g_api.assembly_get_image(assembly);
    if (image) TagMonoImage(image);
    return image;
}

void AssemblyForeach(void (*callback)(void *assembly, void *user), void *user) {
    if (!callback || !Available() || !g_api.assembly_foreach) return;
    EnsureAttached();
    struct Ctx {
        void (*callback)(void *, void *);
        void *user;
    };
    Ctx ctx{callback, user};
    g_api.assembly_foreach(
            [](void *assembly, void *rawUser) {
                auto *inner = static_cast<Ctx *>(rawUser);
                if (!inner || !inner->callback || !assembly) return;
                TagMonoAssembly(assembly);
                inner->callback(assembly, inner->user);
            },
            &ctx);
}

void *ImageGetTableInfo(void *image, int tableId) {
    if (!IsMonoImage(image) || !Available() || !g_api.image_get_table_info)
        return nullptr;
    void *info = g_api.image_get_table_info(image, tableId);
    if (info) TagMonoTableInfo(info);
    return info;
}

int TableInfoGetRows(void *tableInfo) {
    if (!g_monoTableInfos.Has(tableInfo) || !Available() ||
        !g_api.table_info_get_rows)
        return 0;
    return g_api.table_info_get_rows(tableInfo);
}

uint32_t GcHandleNew(void *obj, int pinned) {
    if (!obj || !Available() || !g_api.gchandle_new) return 0;
    EnsureAttached();
    const uint32_t handle = g_api.gchandle_new(obj, pinned);
    if (handle) TagMonoGcHandle(handle);
    return handle;
}

void GcHandleFree(uint32_t handle) {
    if (!handle || !g_api.gchandle_free) return;
    if (TakeMonoGcHandle(handle)) g_api.gchandle_free(handle);
}

bool IsMonoGcHandle(uint32_t handle) {
    if (!handle) return false;
    std::lock_guard<std::mutex> lock(g_gcHandleMutex);
    return g_monoGcHandles.find(handle) != g_monoGcHandles.end();
}

// ===========================================================================
//  公开接口：泛型（只在确认是 mono 类型句柄时才走真 mono）
// ===========================================================================
void *MetadataGetGenericInst(int argc, void **typeArgv) {
    if (argc <= 0 || !typeArgv || !Available() || !g_api.metadata_get_generic_inst)
        return nullptr;
    for (int i = 0; i < argc; ++i) {
        if (!IsMonoType(typeArgv[i])) return nullptr;   // 含 AOT/未知类型：保持旧行为（失败）
    }
    EnsureAttached();
    return g_api.metadata_get_generic_inst(argc, typeArgv);
}

void *ClassInflateGenericMethod(void *method, void *context) {
    if (!IsMonoMethod(method) || !Available() ||
        !g_api.class_inflate_generic_method)
        return nullptr;
    EnsureAttached();
    void *result = g_api.class_inflate_generic_method(method, context);
    // ★ 膨胀出来的 MonoMethod* 必须登记成 mono 句柄：否则 MonoCompat 会把
    //   它当 il2cpp 方法走 il2cpp_runtime_invoke（跨运行时 SIGSEGV），
    //   而且 MethodGetFlags 会因 !IsMonoMethod 恒返回 0（静态/实例判断全错）。
    if (result) TagMonoMethod(result);
    return result;
}

void *ClassInflateGenericType(void *type, void *context) {
    if (!IsMonoType(type) || !Available() || !g_api.class_inflate_generic_type)
        return nullptr;
    EnsureAttached();
    void *result = g_api.class_inflate_generic_type(type, context);
    if (result) TagMonoType(result);
    return result;
}

void *ClassInflateGenericMethodChecked(void *method, void *context, void *error) {
    if (!IsMonoMethod(method) || !Available()) return nullptr;
    EnsureAttached();
    void *checkedResult = nullptr;
    if (g_api.class_inflate_generic_method_checked)
        checkedResult = g_api.class_inflate_generic_method_checked(method, context, error);
    else if (g_api.class_inflate_generic_method)
        checkedResult = g_api.class_inflate_generic_method(method, context);
    if (checkedResult) TagMonoMethod(checkedResult);
    return checkedResult;
    return nullptr;
}

void *ClassInflateGenericTypeChecked(void *type, void *context, void *error) {
    if (!IsMonoType(type) || !Available()) return nullptr;
    EnsureAttached();
    if (g_api.class_inflate_generic_type_checked)
        return g_api.class_inflate_generic_type_checked(type, context, error);
    if (g_api.class_inflate_generic_type)
        return g_api.class_inflate_generic_type(type, context);
    return nullptr;
}

// ===========================================================================
//  公开接口：自检 / 状态
// ===========================================================================
bool SelfTest() {
    std::unique_lock<std::mutex> testLock(g_selfTestMutex, std::try_to_lock);
    if (!testLock.owns_lock()) return false;
    g_lastSelfTestMs.store(NowMs());

    Status snapshot;
    snapshot.symbolsReady = Init();
    snapshot.coreReady = g_coreReady.load();
    snapshot.symbolsResolved = g_symbolsResolved.load();
    snapshot.symbolsTotal = g_symbolsTotal.load();

    int errorCode = kErrNone;
    if (!snapshot.symbolsReady) {
        errorCode = kErrNoSymbols;
    } else if (!Available()) {
        errorCode = kErrNoDomain;
    } else {
        EnsureAttached();
        EnsureImageIndex(true);
        snapshot.assemblies = g_assemblyCount.load();
        if (snapshot.assemblies <= 0) errorCode = kErrNoAssemblies;

        // ---- 热更程序集 ----
        std::string imageLine;
        for (const ImageProbe &probe : kProbeImages) {
            void *image = ImageLoaded(probe.name);
            ++snapshot.imagesProbe;
            if (!image) {
                imageLine += "  ";
                imageLine += probe.name;
                imageLine += "=NULL\n";
                continue;
            }
            ++snapshot.imagesHit;
            imageLine += "  ";
            imageLine += probe.name;
            imageLine += "=OK";
        }
        // ---- 热更类 / 方法 ----
        std::string classLine;
        std::string methodLine;
        for (const ClassProbe &probe : kProbeClasses) {
            ++snapshot.classesProbe;
            void *image = ImageLoaded(probe.image);
            void *klass = image ? ClassFromName(image, probe.nameSpace, probe.className)
                                : nullptr;
            if (!klass) {
                classLine += "  ";
                classLine += probe.className;
                classLine += "=NULL\n";
                continue;
            }
            ++snapshot.classesHit;
            classLine += "  ";
            classLine += probe.className;
            classLine += "=OK";
            if (probe.methodName) {
                ++snapshot.methodsProbe;
                void *method = ClassGetMethodFromName(klass, probe.methodName,
                                                      probe.methodArgc);
                if (method) {
                    ++snapshot.methodsHit;
                    methodLine += "  ";
                    methodLine += probe.className;
                    methodLine += ".";
                    methodLine += probe.methodName;
                    methodLine += "=OK";
                }
            }
        }
        if (snapshot.classesHit <= 0 && errorCode == kErrNone) errorCode = kErrNoHotClass;
        if (snapshot.imagesHit <= 0 && errorCode == kErrNone) errorCode = kErrNoHotImage;

        snapshot.detail = "img:" + std::to_string(snapshot.imagesHit) + "/" +
                          std::to_string(snapshot.imagesProbe) + " cls:" +
                          std::to_string(snapshot.classesHit) + "/" +
                          std::to_string(snapshot.classesProbe) + " method:" +
                          std::to_string(snapshot.methodsHit) + "/" +
                          std::to_string(snapshot.methodsProbe) + " asm:" +
                          std::to_string(snapshot.assemblies) + "\n" + imageLine +
                          (classLine.empty() ? std::string() : classLine + "\n") +
                          (methodLine.empty() ? std::string() : methodLine + "\n");
    }

    snapshot.lastError = errorCode;
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        snapshot.domainReady = g_rootDomainCache.load() != nullptr;
        snapshot.routesMono = g_routesMono.load();
        snapshot.routesIl2Cpp = g_routesIl2Cpp.load();
        snapshot.compileHits = g_compileHits.load();
        snapshot.preferredImagesOk =
                (snapshot.imagesHit > 0 && snapshot.classesHit > 0);
        g_status = snapshot;
    }
    // 全部探针命中后停止自动重跑（后续程序集加载仍可被业务调用触发解析）。
    if (snapshot.imagesHit >= snapshot.imagesProbe && snapshot.imagesProbe > 0 &&
        snapshot.classesHit >= snapshot.classesProbe && snapshot.classesProbe > 0) {
        g_selfTestComplete.store(true);
    }
    SMLOG("[MonoHot] selftest err=%s images=%d/%d classes=%d/%d methods=%d/%d",
          ErrorName(errorCode), snapshot.imagesHit, snapshot.imagesProbe,
          snapshot.classesHit, snapshot.classesProbe, snapshot.methodsHit,
          snapshot.methodsProbe);
    return snapshot.imagesHit > 0 && snapshot.classesHit > 0;
}

void Tick() {
    if (!Init()) return;
    if (g_selfTestComplete.load(std::memory_order_relaxed)) return;
    if (!Available()) return;   // mono 还没起来：不在 shim 热路径上做任何事
    const int64_t now = NowMs();
    if (now - g_lastSelfTestMs.load(std::memory_order_relaxed) < 3000) return;
    SelfTest();
}

bool HotReady() {
    Status s = GetStatus();
    return s.imagesHit > 0 && s.classesHit > 0;
}

const char *LastErrorString() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return ErrorName(g_status.lastError);
}

Status GetStatus() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    Status s = g_status;
    s.symbolsReady = g_symbolsReady.load();
    s.coreReady = g_coreReady.load();
    s.domainReady = g_rootDomainCache.load() != nullptr;
    s.symbolsResolved = g_symbolsResolved.load();
    s.symbolsTotal = g_symbolsTotal.load();
    s.assemblies = g_assemblyCount.load();
    s.routesMono = g_routesMono.load();
    s.routesIl2Cpp = g_routesIl2Cpp.load();
    s.compileHits = g_compileHits.load();
    return s;
}

std::string StatusString() {
    Status s = GetStatus();
    const char *state = "NO-SYMBOLS";
    if (s.symbolsReady && !s.coreReady) state = "PARTIAL-SYMBOLS";
    else if (s.coreReady && !s.domainReady) state = "WAIT-DOMAIN";
    else if (s.domainReady) state = "READY";

    char head[768];
    snprintf(head, sizeof(head),
             "MonoHot: %s symbols=%d/%d asm=%d img=%d/%d cls=%d/%d method=%d/%d "
             "mono=%llu il2cpp=%llu compile=%llu err=%s",
             state, s.symbolsResolved, s.symbolsTotal, s.assemblies, s.imagesHit,
             s.imagesProbe, s.classesHit, s.classesProbe, s.methodsHit,
             s.methodsProbe, (unsigned long long) s.routesMono,
             (unsigned long long) s.routesIl2Cpp, (unsigned long long) s.compileHits,
             ErrorName(s.lastError));
    std::string out(head);
    if (!s.detail.empty()) {
        out += "\n";
        out += s.detail;
    } else if (s.domainReady) {
        out += "\n  selftest: not-run（等待 shim 热路径 / 菜单「运行自检」触发）";
    }
    return out;
}

}  // namespace SMMonoHot
