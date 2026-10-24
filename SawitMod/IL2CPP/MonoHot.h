#pragma once
// ============================================================================
//  MonoHot.h — iOS 内嵌 Mono 运行时 / 热更程序集桥接层
// ----------------------------------------------------------------------------
//  背景（证据见 dump-analysis-ios.md §7、ios-antiban-gap.md §2.3）：
//    iOS 包不是纯 il2cpp —— UnityFramework 同时导出 1476 个 mono_* 和 232 个
//    il2cpp_*（都能 dlsym），游戏业务类（EcsClient / XDTDataAndProtocol /
//    XDTLevelAndEntity / XDTGameUI / XDTBaseService ...）都在运行时加载的
//    Mono 热更程序集里，il2cpp 的 domain 里看不到它们。原来的 MonoCompat.cpp
//    把 _monoXxx 全部垫成 il2cpp_*，于是 _monoImageLoaded("EcsClient") 恒为
//    null，依赖它的功能整片失效。
//
//  本模块只做两件事：
//    1. 用 dlsym(RTLD_DEFAULT, "mono_xxx") 解析真实 Mono 导出，维护"真 mono"
//       函数表；缺符号不影响 il2cpp 旧路径，逐符号降级。
//    2. 维护句柄归属（image/class/method/field/type/object/signature/vtable/
//       gchandle），供 MonoCompat.cpp 的垫片按句柄自动分发：
//         mono 句柄  -> 真实 mono_*
//         其它句柄  -> 原来的 il2cpp_*（AOT 路径行为保持不变）
//
//  ⚠️ 线程：mono 调用前必须 mono_thread_attach(mono_get_root_domain())。
//     下面所有包装函数内部都做了 EnsureAttached()（thread_local 幂等），
//     调用方（MonoCompat 垫片、Bootstrap、菜单自检）不需要自己 attach。
//
//  ⚠️ 对象判型：Mono 对象不能盲目喂给 il2cpp_object_get_class —— 两套对象头
//     布局不同，il2cpp 侧拿到的是 MonoVTable* 而不是类。所以 ClassifyObject()
//     先按 mono 语义取类，再用"句柄归属表 + 受控栅栏校验"确认；确认不了才落回
//     il2cpp 旧行为。受控校验在 iOS 上用 sigsetjmp/siglongjmp 兜住野指针读越界
//     （见 MonoHot.cpp 的 SM_MONOHOT_SIGNAL_GUARD）。
//
//  ⚠️ 一个进程里两套运行时并存，跨运行时传对象 = 随机 SIGSEGV（不是返回错误）：
//     * 本层只保证"句柄是谁家的，就用谁家的 API 操作"；
//     * 把 il2cpp 字符串传进 Mono 方法、把 Mono 对象传给 il2cpp 方法这类
//       业务级错误本层兜不住，见 mono-bridge-impl.md 的"可能崩的场景"。
// ============================================================================

#include <cstdint>
#include <string>

namespace SMMonoHot {

// ---------------------------------------------------------------------------
//  生命周期
// ---------------------------------------------------------------------------

// 幂等：解析 mono_* 真实符号（dlsym）+ 安装对象判型信号栅栏（仅 Apple）。
// 可以在 il2cpp 就绪之前调用 —— 只解析符号，不执行任何 mono 代码。
// 返回 true 表示"核心符号齐"，不代表 mono 运行时已初始化。
bool Init();

// 符号是否解析完成（等价 Init() == true，不探测 domain）。
bool SymbolsReady();

// 是否可安全调用 mono：符号齐 + mono_get_root_domain() 非空
// （即游戏已调用过 mono_jit_init / 走到 StartMono）。
bool Available();

// mono 根 domain；未初始化返回 nullptr。
void *RootDomain();

// 当前线程 attach 到 mono；幂等、失败可重试。
void EnsureAttached();

// 探测目标热更程序集 / 类 / 方法并记录状态（供菜单与日志确认命中）。
// mono 未就绪时只记录原因并返回 false，不会崩。
bool SelfTest();

// 节流版 SelfTest：shim 热路径顺手调用，mono 晚就绪时自动补跑（≥3s 一次），
// 全部探针命中后不再重复。
void Tick();

// 菜单 / 日志用：多行状态（命中数 + 明细 + 路由计数）。
std::string StatusString();

// 至少命中 1 个热更程序集 + 1 个热更类（雷达/绘画等功能的硬前提）。
bool HotReady();

// ---------------------------------------------------------------------------
//  路由计数（给菜单看"到底有多少调用真的走到了 mono"）
// ---------------------------------------------------------------------------
void CountMonoRoute();
void CountIl2CppRoute();

// ---------------------------------------------------------------------------
//  句柄分类（MonoCompat 分发用）
// ---------------------------------------------------------------------------
bool IsMonoImage(void *p);
bool IsMonoClass(void *p);
bool IsMonoMethod(void *p);
bool IsMonoField(void *p);
bool IsMonoType(void *p);
bool IsMonoAssembly(void *p);
bool IsMonoSignature(void *p);
bool IsMonoTableInfo(void *p);

// 对象判型：true = Mono 对象，*outMonoClass 为它的 MonoClass*；
// false = 按旧行为当 il2cpp 对象处理。
bool ClassifyObject(void *obj, void **outMonoClass);

// 把 il2cpp 路径产出的类回填到"AOT 归属"表，让对象判型第二次起不再走受控校验。
void TagIl2CppClass(void *klass);

// ---------------------------------------------------------------------------
//  运行时提示：给没有句柄可判的 API（mono_string_new 等）做默认运行时选择
//  None = 还没结论（按旧行为 = il2cpp）；Mono/Il2Cpp = 最近一次解析命中的运行时。
// ---------------------------------------------------------------------------
enum class Hint { None, Il2Cpp, Mono };
void SetHint(Hint hint);
Hint CurrentHint();

// ---------------------------------------------------------------------------
//  名字判定：MonoCompat::MonoImageLoaded 用它决定"先 mono 还是先 il2cpp"
// ---------------------------------------------------------------------------
bool IsPreferredImage(const char *name);

// ---------------------------------------------------------------------------
//  真实 mono_* 包装（内部统一 EnsureAttached + 打标 + 计数由调用方负责）
// ---------------------------------------------------------------------------
void *ImageLoaded(const char *name);
const char *ImageGetName(void *image);
void *FindClassAnywhere(const char *nameSpace, const char *className,
                        std::string *outImage = nullptr);

void *ClassFromName(void *image, const char *nameSpace, const char *className);
void *ClassGetMethodFromName(void *klass, const char *name, int paramCount);
void *ClassGetFieldFromName(void *klass, const char *name);
void *ClassGetParent(void *klass);
const char *ClassGetName(void *klass);
const char *ClassGetNamespace(void *klass);
void *ClassGetImage(void *klass);
void *ClassFromMonoType(void *type);
void *ClassGetType(void *klass);
void *ClassGetVTable(void *klass);
int ClassValueSize(void *klass, uint32_t *align);
void *ClassGetFields(void *klass, void **iter);
void *ClassGetMethods(void *klass, void **iter);
void *ClassGet(void *image, uint32_t typeToken);

void *MethodSignature(void *method);
void *SignatureGetParams(void *signature, void **iter);
void *SignatureGetReturnType(void *signature);
const char *MethodGetName(void *method);
uint32_t MethodGetFlags(void *method, uint32_t *iflags);
void *CompileMethod(void *method);

void *FieldGetValueObject(void *field, void *obj);
void FieldGetValue(void *obj, void *field, void *valuePtr);
void FieldSetValue(void *obj, void *field, void *valuePtr);
void *FieldGetType(void *field);
const char *FieldGetName(void *field);
int FieldGetOffset(void *field);
void FieldStaticGetValue(void *vtable, void *field, void *valuePtr);
void FieldStaticSetValue(void *vtable, void *field, void *valuePtr);

void *ObjectGetClass(void *obj);
void *ObjectNew(void *klass);
void *ObjectUnbox(void *obj);
void *RuntimeInvoke(void *method, void *obj, void **params, void **exc);
void *RuntimeObjectInit(void *obj);
void *StringNew(const char *text);
void *ArrayNew(void *elementClass, uintptr_t count);
// mono 数组：长度 / 指定元素大小后的首元素地址（il2cpp 数组用固定偏移，
// mono 数组布局不同，取 entries 时必须走这两个）。
int ArrayLength(void *array);
void *ArrayAddrWithSize(void *array, int elementSize, uintptr_t index);
void *TypeGetObject(void *type);
int TypeGetType(void *type);

// 类型细节（造 List<T> 需要）：方法签名里 `ref T` 参数的 MonoType 是
// MONO_TYPE_BYREF(0x10) 包一层，mono_class_from_mono_type 对它返回 null，
// 必须先解包拿元素类型。TypeGetUnderlyingType 只在符号存在时可用。
bool TypeIsByRef(void *type);
void *TypeGetByRefElement(void *type);
void *TypeGetUnderlyingType(void *type);

void *AssemblyOpen(const char *name, int *status);
void *AssemblyGetImage(void *assembly);
void AssemblyForeach(void (*callback)(void *assembly, void *user), void *user);
const char *ImageGetNameSafe(void *image);
void *ImageGetTableInfo(void *image, int tableId);
int TableInfoGetRows(void *tableInfo);

uint32_t GcHandleNew(void *obj, int pinned);
void GcHandleFree(uint32_t handle);
bool IsMonoGcHandle(uint32_t handle);

// 泛型：只有确认是 mono 类型句柄时才走真 mono，AOT 类型句柄一律返回 nullptr
// （保持旧行为：il2cpp 无法膨胀泛型，上层走降级分支）。
void *MetadataGetGenericInst(int argc, void **typeArgv);
void *ClassInflateGenericMethod(void *method, void *context);
void *ClassInflateGenericType(void *type, void *context);
void *ClassInflateGenericMethodChecked(void *method, void *context, void *error);
void *ClassInflateGenericTypeChecked(void *type, void *context, void *error);

// ---------------------------------------------------------------------------
//  诊断
// ---------------------------------------------------------------------------

struct Status {
    bool symbolsReady = false;   // 符号解析完成
    bool coreReady = false;      // 核心符号（image/class/method/invoke/object）齐
    bool domainReady = false;    // mono_get_root_domain() 非空
    bool attached = false;       // 当前线程已 attach
    bool preferredImagesOk = true;
    int symbolsResolved = 0;
    int symbolsTotal = 0;
    int assemblies = 0;          // 枚举到的 mono 程序集数量（证明热更层真的在跑）
    int imagesHit = 0;
    int imagesProbe = 0;
    int classesHit = 0;
    int classesProbe = 0;
    int methodsHit = 0;
    int methodsProbe = 0;
    int lastError = 0;           // 0=OK，其它见 MonoHot.cpp
    std::string detail;          // 多行明细
    uint64_t routesMono = 0;
    uint64_t routesIl2Cpp = 0;
    uint64_t compileHits = 0;
};

Status GetStatus();

// 失败原因（StatusString 里会带上）
const char *LastErrorString();

}  // namespace SMMonoHot

// 任务书里的短名字（"加一个 MonoHot::SelfTest()"）：等价别名，代码里用哪个都行。
namespace MonoHot = SMMonoHot;
