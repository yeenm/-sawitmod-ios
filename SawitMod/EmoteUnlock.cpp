// ============================================================================
//  EmoteUnlock.cpp — 解锁全部表情动作（面板显示全部 + 未获得的也能演）
//  移植自 PC 版 Bugtopia v3.2.5 → buddy/EmoteUnlockFeature.cs
// ----------------------------------------------------------------------------
//  ── 为什么是两个半边 ───────────────────────────────────────────────────────
//  一个表情不是本地动画，而是一次"请求/回执"往返：
//
//    EmojiPanel -> EmotionCommand -> SocialReqTask.OnStart
//         发送 SendSingleActionNetworkCommand{Id, IsLoopSingle}
//    服务器回 PlayerSocialEvent -> SocialReqTask.OnResp 在【本地】演：
//         循环   -> PlayerStateSingleShow.SetData(true, id, loop)
//         一次性 -> player.Cast(PlayerSocialActionArg{socialType})
//
//  本地那一半才是真正动的，而且它是客户端写的（PC 版实测：这么演出来的表情别人
//  也看得见，因为 PlayerSingleShowStatus / CastActionEvent 是客户端权威的网络状态）。
//  但游戏不会为"没买过的 id"跑本地那一半：服务器不回执，SocialReqTask 空转 2 秒
//  然后放弃。所以：
//
//    HALF 1（面板）EmojiPanel 的行来自 ExpressionActionProtocolManager
//                  .GetExpressionActions() -> ExpressionActionClientService
//                  .ExpressionActionComponent()，那是服务器同步下来的 ECS 实体过滤
//                  结果。它根本不看 IsExpressionActionObtained，所以翻那个标志位
//                  一行都不会多。我们直接把这个 getter 换掉，返回一份"包含所有
//                  id"的预造列表。
//    HALF 2（点击）SendSingleAction / SendEmotion 拦下来记录，下一帧在 Unity 线程
//                  上本地补演一次（走游戏自己的 SocialReqTask.OnResp 回放链路；
//                  表情气泡则自己派发 EventCenter.DispatchEvent<SocialFaceEvent>）。
//
//  ── hook 为什么长这样 ──────────────────────────────────────────────────────
//  两个/三个 native hook 都是"无回调"的：一个只返回主线程早就造好的指针，另外
//  两个只把两个标量写进原子变量。这条限制不是风格问题 —— PC 版有过"在 detour
//  回调里回调 Mono 导致冻死再崩进程"的复盘结论，所以这里 hook 体内绝不进 Mono。
//  发送类 hook 保留原调用（继续走游戏原版逻辑），只在旁边记一笔。
//
//  这几处是 Mono JIT detour，不是 GameAssembly/.text 补丁 —— 游戏一换世界/换
//  程序集，JIT 指针就会变；所以每帧校验 _monoCompileMethod 的结果，变了就重挂。
// ============================================================================

#include "ModFeatures.h"
#include "ModUtils.h"
#include "MonoGameApi.h"
#include "Includes/Dobby/dobby.h"
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

using namespace MonoApi;

namespace {

// ── ExpressionActionType：面板按这个分桶。1 = Expression(表情)，2 = SingleAction(动作)
constexpr int kEmoteTypeExpression = 1;
constexpr int kEmoteTypeSingleAction = 2;

// 两张表都从 1 开始连续；表里没有的 id 会被跳过，所以将来表变短只会少几行。
constexpr int kEmoteMaxSingleActionId = 400;
constexpr int kEmoteMaxExpressionId = 400;

// ── 状态 ────────────────────────────────────────────────────────────────────
void *g_emoteListObject = nullptr;      // 主线程造的 List<ExpressionActionComponent>
void *g_emoteComponentClass = nullptr;  // ExpressionActionComponent（值类型）
void *g_emoteServiceClass = nullptr;    // ExpressionActionClientService
void *g_emoteListMethod = nullptr;      // 面板 getter（第一个命中的）
void *g_emoteSendMethod = nullptr;      // CharacterProtocolManager.SendSingleAction
void *g_emoteFaceMethod = nullptr;      // CharacterProtocolManager.SendEmotion
void *g_emoteHookedSendAddress = nullptr;
void *g_emoteHookedFaceAddress = nullptr;
void *g_emoteHookedDomain = nullptr;
std::atomic<void *> g_emoteServiceSelf{nullptr}; // hook 记录：面板调用时的 service 实例
uint32_t g_emoteListPin = 0;            // 钉住列表，hook 才能一直返回同一个地址
bool g_emoteListMethodIsStatic = false; // 面板 getter 是不是静态方法

// 面板 getter 可能不止一个（PC 版是从 ExpressionActionClientService
// .ExpressionActionComponent() 拿列表，但 Android 版可能走 ExpressionActionProtocolManager
// 的另一个 getter）。这里给每个候选 getter 一个槽位：同一个 hook 体返回同一份列表，
// 谁被面板调用都能顶上。
constexpr int kMaxListHooks = 6;
struct EmoteListHookSlot {
    void *address = nullptr;
    void *original = nullptr;
    bool isStatic = true;
};
EmoteListHookSlot g_emoteListSlots[kMaxListHooks];
int g_emoteListHookCount = 0;

std::atomic<bool> g_emoteHookInstalled{false};
std::atomic<bool> g_emoteInstallRunning{false};
std::atomic<bool> g_emotePendingBodySet{false};
std::atomic<int> g_emotePendingBodyId{0};
std::atomic<int> g_emotePendingBodyLoop{0};
std::atomic<bool> g_emotePendingFaceSet{false};
std::atomic<int> g_emotePendingFaceId{0};

int g_emoteListedSingle = 0;
int g_emoteListedExpression = 0;
std::string g_emoteRuntimeStatus = u8"未启用。";

// 原始函数（Dobby hook 的 trampoline）
typedef void *(*EmoteListGetFn)(void *self);
typedef void (*EmoteSendFn)(int socialType, int isLoopSingle);
typedef void (*EmoteFaceFn)(int emojiId);
EmoteListGetFn g_originalEmoteList = nullptr;   // 第一个 getter 的原始实现（owned 查询用）
EmoteSendFn g_originalEmoteSend = nullptr;
EmoteFaceFn g_originalEmoteFace = nullptr;

void SetEmoteStatus(const std::string &status) {
    std::lock_guard<std::mutex> lock(g_emoteUnlockMutex);
    g_emoteRuntimeStatus = status;
}

std::string GetEmoteStatusLocked() {
    return g_emoteRuntimeStatus;
}

// ── 关键字面 ────────────────────────────────────────────────────────────────
// 这些名字来自 PC 版；Android 包体是同一套逻辑程序集，但程序集名字可能变，所以
// 每个类都给一串候选镜像 + 最后退回"全镜像搜索"（MonoApi::FindClass）。
const char *kGameImages[] = {
        OBFUSCATE("EcsClient"), OBFUSCATE("EcsSystem"), OBFUSCATE("XDTLevelAndEntity"),
        OBFUSCATE("XDTDataAndProtocol"), OBFUSCATE("XDTGameSystem"), OBFUSCATE("XDTGameUI"),
        OBFUSCATE("XDTBaseService"), OBFUSCATE("Plugins"),
};
constexpr int kGameImageCount = sizeof(kGameImages) / sizeof(kGameImages[0]);

void *FindClassInGameImages(const char *nameSpace, const char *className,
                            std::string *outImage = nullptr) {
    return FindClass(kGameImages, kGameImageCount, nameSpace, className, outImage, true);
}

// TableData 在全局命名空间；PC 版和 Android 版都这么取。
void *ResolveTableData() {
    void *tableData = FindClassInGameImages(OBFUSCATE(""), OBFUSCATE("TableData"));
    if (!tableData)
        tableData = FindClassInGameImages(OBFUSCATE("XD.GameGerm.ExpParser"),
                                          OBFUSCATE("TableData"));
    return tableData;
}

// 表行查找：TableData.GetXxx(int id, bool needException = false)
void *FetchTableRow(const char *getterName, int id) {
    void *tableData = ResolveTableData();
    if (!tableData) return nullptr;
    void *getter = FindMethod(tableData, getterName, {2, 1});
    if (!getter || !_monoRuntimeInvoke) return nullptr;

    int probeId = id;
    uint8_t needException = 0;
    void *args[2] = {&probeId, &needException};
    void *exception = nullptr;
    void *row = _monoRuntimeInvoke(getter, nullptr, args, &exception);
    if (exception || !row) return nullptr;
    return row;
}

// TableSingleaction.isLoop —— 是属性（public int isLoop => _isLoop;）套一个私有 byte
// 字段：问字段名 "isLoop" 什么都没有，按 int 读 1 字节字段又会多拉 3 字节。
bool EmoteTableIsLoop(int id) {
    void *row = FetchTableRow(OBFUSCATE("GetSingleaction"), id);
    if (!row) return false;

    uint8_t isLoopByte = 0;
    if (ReadByteField(row, OBFUSCATE("_isLoop"), &isLoopByte)) return isLoopByte != 0;

    void *rowClass = _monoObjectGetClass ? _monoObjectGetClass(row) : nullptr;
    void *getter = rowClass ? FindMethodInHierarchy(rowClass, OBFUSCATE("get_isLoop"), 0)
                            : nullptr;
    int value = 0;
    if (getter && InvokeReturningIntNoArg(getter, row, &value)) return value != 0;
    return false;
}

// 这份 id 在【原版】列表里吗？在 => 服务器会正常回执，本地就不用补演，免得演两遍。
//
// 原版列表要从 trampoline 拿（我们自己的 getter 已经被换掉了）。读法有两种：
//   1) List<T> 的私有 T[] _items + 结构体大小，直接按内存读元素（快，不装箱）；
//   2) 退化成 get_Count + get_Item(i) 反射调用。
// 两条都失败就返回"未知"，调用方按 fail-open 处理（= PC 版行为：本地补演）。
enum class EmoteOwned { No, Yes, Unknown };

EmoteOwned EmoteOwnedByGame(int actionType, int id) {
    if (!g_emoteServiceSelf.load(std::memory_order_acquire) || !g_originalEmoteList
        || !g_emoteComponentClass) {
        return EmoteOwned::Unknown;
    }

    void *self = g_emoteListMethodIsStatic
            ? nullptr
            : g_emoteServiceSelf.load(std::memory_order_acquire);
    void *originalList = g_originalEmoteList(self);
    if (!originalList) return EmoteOwned::Unknown;

    void *listClass = _monoObjectGetClass ? _monoObjectGetClass(originalList) : nullptr;
    if (!listClass) return EmoteOwned::Unknown;

    // 元素里两个字段的偏移（mono_field_get_offset 含 16 字节的 MONO_FIELD_HEADER）
    void *typeField = _monoClassGetFieldFromName(g_emoteComponentClass,
                                                 OBFUSCATE("ExpressionActionType"));
    void *idField = _monoClassGetFieldFromName(g_emoteComponentClass, OBFUSCATE("Id"));
    if (!typeField || !idField || !_monoFieldGetOffset || !_monoClassValueSize) {
        return EmoteOwned::Unknown;
    }

    const int typeOffset = _monoFieldGetOffset(typeField) - MONO_FIELD_HEADER;
    const int idOffset = _monoFieldGetOffset(idField) - MONO_FIELD_HEADER;
    uint32_t alignment = 0;
    const int elementSize = _monoClassValueSize(g_emoteComponentClass, &alignment);
    if (elementSize <= 0 || typeOffset < 0 || idOffset < 0
        || typeOffset + 4 > elementSize || idOffset + 4 > elementSize) {
        return EmoteOwned::Unknown;
    }

    void *itemsField = _monoClassGetFieldFromName(listClass, OBFUSCATE("_items"));
    if (!itemsField) return EmoteOwned::Unknown;

    void *itemsArray = nullptr;
    _monoFieldGetValue(originalList, itemsField, &itemsArray);
    if (!itemsArray) return EmoteOwned::Unknown;

    const int count = *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(itemsArray)
                                               + OFF_ARR_COUNT);
    const uint8_t *data = reinterpret_cast<uint8_t *>(itemsArray) + OFF_ARR_DATA;
    if (count < 0 || count > 4096) return EmoteOwned::Unknown;

    for (int i = 0; i < count; ++i) {
        const uint8_t *element = data + static_cast<size_t>(i) * elementSize;
        int elementType = 0;
        int elementId = 0;
        memcpy(&elementType, element + typeOffset, sizeof(int));
        memcpy(&elementId, element + idOffset, sizeof(int));
        if (elementId == id && (elementType == actionType || elementType == 0)) {
            return EmoteOwned::Yes;
        }
    }
    return EmoteOwned::No;
}

// ── HALF 1：造"全 id"列表 ───────────────────────────────────────────────────
// 一行的 id 必须在游戏自己的表里存在（TableData.GetXxx 返回 null = 这个版本没有
// 这个 id），否则面板拿不到行数据会画出坏格子。所以这里 fail-CLOSED：拿不到验证
// 用的 getter 就整块放弃，绝不列一堆没表数据的 id。
int AppendEmoteRows(void *listObject, void *addMethod, int actionType, int maxId,
                    const char *tableGetterName) {
    void *tableData = ResolveTableData();
    if (!tableData) {
        LOGI(OBFUSCATE("[EmoteUnlock] TableData 解析失败，放弃列表构建"));
        return 0;
    }

    void *getter = FindMethod(tableData, tableGetterName, {2, 1});
    if (!getter) {
        LOGI(OBFUSCATE("[EmoteUnlock] %s(int,bool) 解析失败 —— 不列未经验证的 id"),
             tableGetterName);
        return 0;
    }

    uint32_t alignment = 0;
    const int elementSize = _monoClassValueSize ? _monoClassValueSize(g_emoteComponentClass,
                                                                     &alignment) : 0;
    void *typeField = _monoClassGetFieldFromName(g_emoteComponentClass,
                                                 OBFUSCATE("ExpressionActionType"));
    void *idField = _monoClassGetFieldFromName(g_emoteComponentClass, OBFUSCATE("Id"));
    if (!typeField || !idField || elementSize <= 0) {
        LOGI(OBFUSCATE("[EmoteUnlock] ExpressionActionComponent 字段解析失败"));
        return 0;
    }

    void *element = _monoObjectNew ? _monoObjectNew(_monoGetRootDomain(), g_emoteComponentClass)
                                   : nullptr;
    if (!element) {
        LOGI(OBFUSCATE("[EmoteUnlock] 值类型分配失败"));
        return 0;
    }

    const int typeOffset = _monoFieldGetOffset(typeField) - MONO_FIELD_HEADER;
    const int idOffset = _monoFieldGetOffset(idField) - MONO_FIELD_HEADER;
    uint8_t *elementData = reinterpret_cast<uint8_t *>(UnboxValue(element));
    if (!elementData || typeOffset < 0 || idOffset < 0) return 0;
    // 写之前先确认两个 int 真的落在结构体里：偏移算错就是往托管堆上越界写，
    // 那种崩法连日志都不会有。
    if (typeOffset + (int) sizeof(int) > elementSize
        || idOffset + (int) sizeof(int) > elementSize) {
        LOGI(OBFUSCATE("[EmoteUnlock] 字段偏移越界（size=%d type=%d id=%d），放弃"),
             elementSize, typeOffset, idOffset);
        return 0;
    }

    int added = 0;
    uint8_t needException = 0;

    for (int id = 1; id <= maxId; ++id) {
        int probeId = id;
        void *probeArgs[2] = {&probeId, &needException};
        void *exception = nullptr;
        void *row = _monoRuntimeInvoke(getter, nullptr, probeArgs, &exception);
        if (exception || !row) continue;   // 这个版本/等级没有这个 id

        memcpy(elementData + typeOffset, &actionType, sizeof(int));
        memcpy(elementData + idOffset, &id, sizeof(int));

        // List<T>.Add(T) 的值类型参数要的是"指向原始结构体"的指针。
        void *addArgs[1] = {elementData};
        exception = nullptr;
        _monoRuntimeInvoke(addMethod, listObject, addArgs, &exception);
        if (exception) break;
        ++added;
    }
    return added;
}

bool BuildEmoteUnlockList(std::string *status) {
    g_emoteComponentClass = FindClassInGameImages(
            OBFUSCATE("XDT.Scene.Shared.Modules.ExpressionAction"),
            OBFUSCATE("ExpressionActionComponent"));
    if (!g_emoteComponentClass) {
        *status = u8"ExpressionActionComponent 类没找到。";
        return false;
    }

    std::string componentImage;
    void *componentImageHandle = ClassGetImage() ? ClassGetImage()(g_emoteComponentClass)
                                                 : nullptr;
    if (componentImageHandle) componentImage = SafeImageName(componentImageHandle);

    std::string bclName = OBFUSCATE("System.Collections.Generic.List`1[[");
    bclName += static_cast<const char *>(OBFUSCATE("XDT.Scene.Shared.Modules.ExpressionAction.ExpressionActionComponent"));
    if (!componentImage.empty()) {
        bclName += static_cast<const char *>(OBFUSCATE(", "));
        bclName += componentImage;
    }
    bclName += static_cast<const char *>(OBFUSCATE("]]"));

    void *listObject = CreateGenericList(g_emoteComponentClass, bclName);
    if (!listObject) {
        *status = u8"List<ExpressionActionComponent> 创建失败。";
        return false;
    }

    void *listClass = _monoObjectGetClass(listObject);
    void *addMethod = listClass ? FindMethodInHierarchy(listClass, OBFUSCATE("Add"), 1)
                                : nullptr;
    if (!addMethod) {
        *status = u8"List.Add 解析失败。";
        return false;
    }

    g_emoteListedSingle = AppendEmoteRows(listObject, addMethod, kEmoteTypeSingleAction,
                                          kEmoteMaxSingleActionId, OBFUSCATE("GetSingleaction"));
    g_emoteListedExpression = AppendEmoteRows(listObject, addMethod, kEmoteTypeExpression,
                                              kEmoteMaxExpressionId, OBFUSCATE("GetExpression"));
    if (g_emoteListedSingle + g_emoteListedExpression <= 0) {
        *status = u8"没有可列的 id（表 getter 不可用）。";
        return false;
    }

    // 钉住：hook 可能在任何线程上被调用，未钉住的对象会被 sgen 搬走。
    g_emoteListPin = PinObject(listObject);
    if (!g_emoteListPin) {
        *status = u8"列表 GC 句柄创建失败。";
        return false;
    }

    g_emoteListObject = listObject;
    *status = u8"列表已就绪（动作 " + std::to_string(g_emoteListedSingle) + u8" 条，表情 "
              + std::to_string(g_emoteListedExpression) + u8" 条）。";
    return true;
}

// ── hook 本体：不回调 Mono、不分配 ─────────────────────────────────────────
// 每个 getter 槽位一个模板实例（Dobby 要求每个 hook 一个独立的函数地址）。
// 内容只有"记一个指针 + 返回早就在主线程造好的列表"，绝不在 hook 里进 Mono。
template <int Slot>
void *Hook_EmoteListSlot(void *self) {
    if (!g_emoteListSlots[Slot].isStatic)
        g_emoteServiceSelf.store(self, std::memory_order_release);
    // 只在功能打开且列表造好时替换；关掉开关立刻回到原版（hook 本身留着，
    // 动 JIT 页的代价比留一个直通 detour 大）。
    void *list = g_emoteHookInstalled.load(std::memory_order_acquire)
            ? g_emoteListObject
            : nullptr;
    if (list) return list;
    EmoteListGetFn original = reinterpret_cast<EmoteListGetFn>(g_emoteListSlots[Slot].original);
    return original ? original(self) : nullptr;
}

void Hook_EmoteSendSingleAction(int socialType, int isLoopSingle) {
    // 保留原版发送：服务器仍然按自己的规则决定给不给状态；我们只在旁边记一笔，
    // 下一帧如果确认"没拥有"，再本地补演一次。
    if (g_originalEmoteSend) g_originalEmoteSend(socialType, isLoopSingle);
    g_emotePendingBodyId.store(socialType, std::memory_order_release);
    g_emotePendingBodyLoop.store(isLoopSingle, std::memory_order_release);
    g_emotePendingBodySet.store(true, std::memory_order_release);
}

void Hook_EmoteSendEmotion(int emojiId) {
    if (g_originalEmoteFace) g_originalEmoteFace(emojiId);
    g_emotePendingFaceId.store(emojiId, std::memory_order_release);
    g_emotePendingFaceSet.store(true, std::memory_order_release);
}

// 槽位 -> hook 体
void *EmoteListHookBodyAt(int slot) {
    switch (slot) {
        case 0: return reinterpret_cast<void *>(&Hook_EmoteListSlot<0>);
        case 1: return reinterpret_cast<void *>(&Hook_EmoteListSlot<1>);
        case 2: return reinterpret_cast<void *>(&Hook_EmoteListSlot<2>);
        case 3: return reinterpret_cast<void *>(&Hook_EmoteListSlot<3>);
        case 4: return reinterpret_cast<void *>(&Hook_EmoteListSlot<4>);
        case 5: return reinterpret_cast<void *>(&Hook_EmoteListSlot<5>);
        default: return nullptr;
    }
}

// 把一个"0 参数 + 返回 List<...>"的 getter 挂上（返回我们的全 id 列表）。
bool HookOneListGetter(void *method, bool isStatic, std::string *hookedName) {
    if (!method || g_emoteListHookCount >= kMaxListHooks || !_monoCompileMethod) return false;
    const int slot = g_emoteListHookCount;
    void *address = _monoCompileMethod(method);
    if (!address) return false;

    void *hookBody = EmoteListHookBodyAt(slot);
    if (!hookBody) return false;

    g_emoteListSlots[slot].isStatic = isStatic;
    if (DobbyHook(address, hookBody,
                  reinterpret_cast<void **>(&g_emoteListSlots[slot].original)) != RS_SUCCESS) {
        return false;
    }
    VirtualProtect(address, 0x4000);
    g_emoteListSlots[slot].address = address;
    if (slot == 0) {
        g_emoteListMethod = method;
        // 第一个槽位顺带留一份原始实现给 owned 查询用
        g_originalEmoteList = reinterpret_cast<EmoteListGetFn>(g_emoteListSlots[0].original);
        g_emoteListMethodIsStatic = isStatic;
    }
    ++g_emoteListHookCount;
    if (hookedName) *hookedName = MethodGetName() ? MethodGetName()(method) : "?";
    LOGI(OBFUSCATE("[EmoteUnlock] hook 面板 getter #%d %s @ %p (static=%d)"),
         slot, hookedName ? hookedName->c_str() : "?", address, isStatic ? 1 : 0);
    return true;
}

// 在一个类里把所有"0 参数 + 返回 List<...>"的 getter 都挂上（名字不用猜）。
int HookListGettersInClass(void *klass, const char *requiredNameSubstring) {
    MonoClassGetMethodsFn getMethods = ClassGetMethods();
    MonoMethodGetNameFn getName = MethodGetName();
    if (!klass || !getMethods || !getName) return 0;

    int hooked = 0;
    void *iter = nullptr;
    void *method = nullptr;
    while ((method = getMethods(klass, &iter)) != nullptr) {
        if (g_emoteListHookCount >= kMaxListHooks) break;
        const char *methodName = getName(method);
        if (!methodName) continue;
        if (!MethodParamTypeCodes(method).empty()) continue;      // 只看无参
        if (!MethodReturnsGenericList(method)) continue;          // 只看返回 List 的
        if (requiredNameSubstring && *requiredNameSubstring
            && !strstr(methodName, requiredNameSubstring)) {
            continue;
        }
        std::string hookedName;
        if (HookOneListGetter(method, MethodIsStatic(method), &hookedName)) ++hooked;
    }
    return hooked;
}

// ── 安装 / 卸载 ────────────────────────────────────────────────────────────
bool InstallEmoteHooks(std::string *status) {
    void *serviceClass = FindClassInGameImages(OBFUSCATE("ClientSystem.ExpressionAction"),
                                               OBFUSCATE("ExpressionActionClientService"));
    void *protocolClass = FindClassInGameImages(
            OBFUSCATE("XDTDataAndProtocol.ProtocolService.GamePlay.Character"),
            OBFUSCATE("CharacterProtocolManager"));
    if (!serviceClass || !protocolClass) {
        *status = u8"服务类解析失败（ExpressionActionClientService="
                  + std::string(serviceClass ? "OK" : "NULL") + u8", CharacterProtocolManager="
                  + std::string(protocolClass ? "OK" : "NULL") + u8"）。";
        return false;
    }

    // ── 面板 getter：不赌方法名，按"0 参数 + 返回 List<...>"认 ───────────────
    // PC 版是从 ExpressionActionClientService.ExpressionActionComponent() 拿列表；
    // Android 版可能挂在 ExpressionActionProtocolManager 上，也可能换了名字。所以：
    //   1) 已知名字的 getter 先挂；
    //   2) 再把服务类里所有"0 参数 + 返回 List"的 getter 都挂上（名字不用猜）；
    //   3) 还是 0 个的话，去 ExpressionActionProtocolManager / 全镜像找同样形状的。
    std::string listProbe = DescribeMethodsNamed(serviceClass,
                                                 OBFUSCATE("ExpressionActionComponent"));
    LOGI(OBFUSCATE("[EmoteUnlock] ExpressionActionComponent 候选: %s"), listProbe.c_str());
    if (g_emoteListHookCount == 0) {
        void *named = FindMethodBySignature(serviceClass,
                                            OBFUSCATE("ExpressionActionComponent"), "");
        if (named) {
            std::string hookedName;
            HookOneListGetter(named, MethodIsStatic(named), &hookedName);
        }
    }
    if (g_emoteListHookCount == 0) {
        const int hooked = HookListGettersInClass(serviceClass, nullptr);
        LOGI(OBFUSCATE("[EmoteUnlock] 服务类里按形状挂了 %d 个 List getter"), hooked);
    }
    if (g_emoteListHookCount == 0) {
        void *protocolExpression = FindClassInGameImages(
                OBFUSCATE("ClientSystem.ExpressionAction"),
                OBFUSCATE("ExpressionActionProtocolManager"));
        if (!protocolExpression) {
            protocolExpression = FindClass(nullptr, 0, OBFUSCATE(""),
                                           OBFUSCATE("ExpressionActionProtocolManager"));
        }
        if (protocolExpression) {
            const int hooked = HookListGettersInClass(protocolExpression, nullptr);
            LOGI(OBFUSCATE("[EmoteUnlock] ExpressionActionProtocolManager 里按形状挂了 %d 个"),
                 hooked);
        }
    }

    // ── 点击 getter：名字 + (int,bool)/(int) 签名 ────────────────────────────
    void *sendMethod = FindMethodBySignature(protocolClass, OBFUSCATE("SendSingleAction"), "ib");
    if (!sendMethod)
        sendMethod = FindMethodBySignature(protocolClass, OBFUSCATE("SendSingleAction"), "ii");
    void *faceMethod = FindMethodBySignature(protocolClass, OBFUSCATE("SendEmotion"), "i");
    if (!sendMethod) {
        std::string owner;
        sendMethod = FindMethodAcrossImages(kGameImages, kGameImageCount,
                                            OBFUSCATE("SendSingleAction"), "ib",
                                            OBFUSCATE("ProtocolManager"), &owner);
        if (sendMethod) LOGI(OBFUSCATE("[EmoteUnlock] SendSingleAction 兜底命中: %s"), owner.c_str());
    }
    if (!faceMethod) {
        std::string owner;
        faceMethod = FindMethodAcrossImages(kGameImages, kGameImageCount,
                                            OBFUSCATE("SendEmotion"), "i",
                                            OBFUSCATE("ProtocolManager"), &owner);
        if (faceMethod) LOGI(OBFUSCATE("[EmoteUnlock] SendEmotion 兜底命中: %s"), owner.c_str());
    }

    if (g_emoteListHookCount == 0) {
        *status = u8"没找到任何面板 getter（0 参数、返回 List 的方法一个都没有）。"
                  u8"ExpressionActionComponent 候选：";
        *status += listProbe;
        LOGI(OBFUSCATE("[EmoteUnlock] %s"), status->c_str());
        return false;
    }
    if (!sendMethod || !faceMethod) {
        *status = u8"面板 getter 已挂 " + std::to_string(g_emoteListHookCount)
                  + u8" 个，但发送方法没解析（send=" + std::string(sendMethod ? "OK" : "NULL")
                  + u8" face=" + std::string(faceMethod ? "OK" : "NULL") + u8"）。";
        LOGI(OBFUSCATE("[EmoteUnlock] SendSingleAction 候选: %s"),
             DescribeMethodsNamed(protocolClass, OBFUSCATE("SendSingleAction")).c_str());
        LOGI(OBFUSCATE("[EmoteUnlock] SendEmotion 候选: %s"),
             DescribeMethodsNamed(protocolClass, OBFUSCATE("SendEmotion")).c_str());
        // 面板那一半已经挂上了（列表能列全），点击那一半失败就不挂发送钩子。
        return true;
    }

    // 签名形状必须先确认：detour 挂上去以后参数寄存器就定死了，静态/实例判断错
    // 等于把 this 当 id 用，直接把进程写崩。宁可拒绝挂载并把原因报出来。
    const bool sendIsStatic = MethodIsStatic(sendMethod);
    const bool faceIsStatic = MethodIsStatic(faceMethod);
    if (!sendIsStatic || !faceIsStatic) {
        *status = u8"面板 getter 已挂，但发送方法不是静态方法（send="
                  + std::string(sendIsStatic ? "static" : "instance") + u8", face="
                  + std::string(faceIsStatic ? "static" : "instance")
                  + u8"），不挂发送钩子以免参数错位。";
        LOGI(OBFUSCATE("[EmoteUnlock] %s"), status->c_str());
        return true;
    }

    if (!_monoCompileMethod) {
        *status = u8"mono_compile_method 不可用。";
        return false;
    }

    // 先把发送方法 JIT 出来再挂（Dobby 挂完会把 JIT 页设成 RX，之后再让 Mono 往
    // 同一页上编译新方法会 SEGV_ACCERR —— BuildFeatures.h 里踩过这个坑）。
    void *sendAddress = _monoCompileMethod(sendMethod);
    void *faceAddress = _monoCompileMethod(faceMethod);
    if (!sendAddress || !faceAddress) {
        *status = u8"面板 getter 已挂，但 mono_compile_method(发送) 返回空。";
        return true;
    }

    g_emoteServiceClass = serviceClass;
    g_emoteSendMethod = sendMethod;
    g_emoteFaceMethod = faceMethod;
    g_emoteHookedDomain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;

    int hooked = g_emoteListHookCount;
    if (DobbyHook(sendAddress, reinterpret_cast<void *>(&Hook_EmoteSendSingleAction),
                  reinterpret_cast<void **>(&g_originalEmoteSend)) == RS_SUCCESS) {
        VirtualProtect(sendAddress, 0x4000);
        g_emoteHookedSendAddress = sendAddress;
        ++hooked;
    }
    if (DobbyHook(faceAddress, reinterpret_cast<void *>(&Hook_EmoteSendEmotion),
                  reinterpret_cast<void **>(&g_originalEmoteFace)) == RS_SUCCESS) {
        VirtualProtect(faceAddress, 0x4000);
        g_emoteHookedFaceAddress = faceAddress;
        ++hooked;
    }

    LOGI(OBFUSCATE("[EmoteUnlock] hook 结果 list=%d send=%p face=%p 合计=%d"),
         g_emoteListHookCount, sendAddress, faceAddress, hooked);
    return g_emoteListHookCount > 0;
}

// 表情气泡：游戏自己不为锁着的 id 回执，所以直接派发同一个事件。
// SocialFaceEvent { uint netId; int emojiId; } —— 8 字节自然布局，参数是 in T，
// 所以参数槽里放的是指向值的指针。
bool DispatchFaceEmoteLocal(int emojiId) {
    if (!_monoRuntimeInvoke) return false;

    void *eventCenterClass = FindClassInGameImages(OBFUSCATE("XDTGame.Core"),
                                                   OBFUSCATE("EventCenter"));
    void *faceEventClass = FindClassInGameImages(OBFUSCATE("XDTDataAndProtocol.Events.Player"),
                                                 OBFUSCATE("SocialFaceEvent"));
    if (!eventCenterClass || !faceEventClass) {
        LOGI(OBFUSCATE("[EmoteUnlock] EventCenter/SocialFaceEvent 解析失败"));
        return false;
    }

    void *openMethod = FindMethod(eventCenterClass, OBFUSCATE("DispatchEvent"), {1});
    if (!openMethod) {
        LOGI(OBFUSCATE("[EmoteUnlock] EventCenter.DispatchEvent 解析失败"));
        return false;
    }
    if (!MethodIsStatic(openMethod)) {
        LOGI(OBFUSCATE("[EmoteUnlock] DispatchEvent 不是静态方法，跳过（避免空 this 崩）"));
        return false;
    }

    void *inflated = InflateGenericMethodForClass(openMethod, faceEventClass);
    if (!inflated) {
        LOGI(OBFUSCATE("[EmoteUnlock] DispatchEvent<SocialFaceEvent> 膨胀失败"));
        return false;
    }
    // 膨胀出来的方法必须真的是"一个结构体参数"（in T），否则后面按值指针传参
    // 就是错的 —— 宁可跳过，不要拿错签名去 invoke。
    if (!SignatureMatches(inflated, "v")) {
        LOGI(OBFUSCATE("[EmoteUnlock] DispatchEvent<SocialFaceEvent> 签名不符（期望 v，实为 %s）"),
             MethodParamTypeCodes(inflated).c_str());
        return false;
    }

    uint32_t selfNetId = 0;
    void *playerDataClass = FindClassInGameImages(OBFUSCATE("XDTDataAndProtocol"),
                                                  OBFUSCATE("PlayerDataCenter"));
    void *getNetId = playerDataClass
            ? FindMethodInHierarchy(playerDataClass, OBFUSCATE("GetSelfNetPlayerId"), 0)
            : nullptr;
    if (getNetId) {
        void *exception = nullptr;
        void *boxed = _monoRuntimeInvoke(getNetId, nullptr, nullptr, &exception);
        if (!exception && boxed) {
            memcpy(&selfNetId, UnboxValue(boxed), sizeof(uint32_t));
        }
    }
    if (selfNetId == 0) {
        LOGI(OBFUSCATE("[EmoteUnlock] 自身 netId 拿不到，气泡事件跳过"));
        return false;
    }

    struct SocialFaceEvent {
        uint32_t netId;
        int32_t emojiId;
    } payload{selfNetId, emojiId};
    void *args[1] = {&payload};
    void *exception = nullptr;
    _monoRuntimeInvoke(inflated, nullptr, args, &exception);
    if (exception) {
        LOGI(OBFUSCATE("[EmoteUnlock] SocialFaceEvent 派发异常"));
        return false;
    }
    return true;
}

// 本地补演一个身体表情/动作（Unity 线程）。
// 返回 0 = 失败，1 = 本地补演成功，2 = 该 id 本来就有（服务器会回执，游戏自己演）。
int PlayEmoteLocal(int actionId, int isLoopArg) {
    if (actionId <= 0) return 0;

    const EmoteOwned owned = EmoteOwnedByGame(kEmoteTypeSingleAction, actionId);
    if (owned == EmoteOwned::Yes) {
        // 服务器会正常回执，游戏自己会演；再补一次就是演两遍。
        return 2;
    }

    // SocialReqTask 判循环用的是 `singleaction.isLoop != 0 || resp.isLoopSingle`——
    // 表说了算。面板从普通页签点循环表情时传的 isLoopSingle 是 false，只信调用方
    // 的参数会把循环表情送进一次性 Cast 分支，结果是"接受了但什么都不动"。
    const bool tableLoop = EmoteTableIsLoop(actionId);
    const bool useLoop = tableLoop || (isLoopArg != 0);
    return PlaySocialActionLocalNative(actionId, useLoop) ? 1 : 0;
}

void DrainEmotePending() {
    if (g_emotePendingFaceSet.exchange(false, std::memory_order_acq_rel)) {
        const int faceId = g_emotePendingFaceId.load(std::memory_order_acquire);
        const bool shown = DispatchFaceEmoteLocal(faceId);
        SetEmoteStatus(shown ? u8"表情气泡已本地派发：" + std::to_string(faceId)
                             : u8"表情气泡派发失败：" + std::to_string(faceId));
    }

    if (!g_emotePendingBodySet.exchange(false, std::memory_order_acq_rel)) return;
    const int actionId = g_emotePendingBodyId.load(std::memory_order_acquire);
    const int loopArg = g_emotePendingBodyLoop.load(std::memory_order_acquire);
    const int played = PlayEmoteLocal(actionId, loopArg);
    if (played == 1)
        SetEmoteStatus(u8"已本地补演动作/表情：" + std::to_string(actionId));
    else if (played == 2)
        SetEmoteStatus(u8"已拥有的表情，交给游戏原版演出：" + std::to_string(actionId));
    else
        SetEmoteStatus(u8"本地演出失败（接口或表缺失）：" + std::to_string(actionId));
    LOGI(OBFUSCATE("[EmoteUnlock] body id=%d loopArg=%d result=%d"),
         actionId, loopArg, played);
}

void ResetEmoteHookState() {
    g_emoteHookInstalled.store(false, std::memory_order_release);
    g_emoteInstallRunning.store(false, std::memory_order_release);
    for (int i = 0; i < kMaxListHooks; ++i) {
        g_emoteListSlots[i].address = nullptr;
        g_emoteListSlots[i].original = nullptr;
        g_emoteListSlots[i].isStatic = true;
    }
    g_emoteListHookCount = 0;
    g_emoteListMethod = nullptr;
    g_originalEmoteList = nullptr;
    g_emoteHookedSendAddress = nullptr;
    g_emoteHookedFaceAddress = nullptr;
    g_originalEmoteSend = nullptr;
    g_originalEmoteFace = nullptr;
    g_emoteServiceSelf.store(nullptr, std::memory_order_release);
}

} // namespace

// ============================================================================
//  对外接口
// ============================================================================

// 每帧由 GameApp_Update（Unity 线程）调用。
void ProcessEmoteUnlockOnUpdate() {
    if (!g_emoteUnlockEnabled) {
        if (g_emoteHookInstalled.load(std::memory_order_acquire)) {
            g_emoteHookInstalled.store(false, std::memory_order_release);
            SetEmoteStatus(u8"已关闭（hook 保留到重启，避免动 JIT 页）。");
        }
        g_emotePendingBodySet.store(false, std::memory_order_release);
        g_emotePendingFaceSet.store(false, std::memory_order_release);
        return;
    }

    // 换域（回登录/热更）后旧的 JIT 指针作废：不 DobbyDestroy（那块内存可能已经
    // 被回收），直接丢掉状态重挂。
    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (domain && g_emoteHookedDomain && domain != g_emoteHookedDomain
        && g_emoteHookInstalled.load(std::memory_order_acquire)) {
        LOGI(OBFUSCATE("[EmoteUnlock] Mono 域变化，重新安装 hook"));
        ResetEmoteHookState();
        g_emoteListObject = nullptr;
        g_emoteComponentClass = nullptr;
    }

    if (!g_emoteHookInstalled.load(std::memory_order_acquire)) {
        // 世界就绪探测是托管调用，没必要每帧做：~30 帧查一次（约半秒）。
        static int worldCheckCounter = 0;
        if (++worldCheckCounter < 30) return;
        worldCheckCounter = 0;
        if (g_emoteInstallRunning.exchange(true)) return;

        std::string status;
        bool ok = false;
        if (!_monoGetRootDomain || !_monoGetRootDomain()) {
            status = u8"Mono 域还没起来。";
        } else if (!IsGameWorldReadyNative()) {
            // 登录界面/加载中不碰 hook：登录流程自己会调这些方法，签名稍有出入
            // 就是当场闪退。进世界后下一帧会自动补上。
            status = u8"还没进世界（等到进入游戏后自动安装）。";
            g_emoteInstallRunning.store(false, std::memory_order_release);
            SetEmoteStatus(status);
            return;
        } else if (_monoThreadAttach) {
            _monoThreadAttach(_monoGetRootDomain());
            std::string listStatus;
            if (!g_emoteListObject && !BuildEmoteUnlockList(&listStatus)) {
                status = u8"列表构建失败：" + listStatus;
            } else {
                std::string hookStatus;
                ok = InstallEmoteHooks(&hookStatus);
                status = ok ? (u8"已启用 —— " + listStatus) : (u8"挂载失败：" + hookStatus);
            }
        } else {
            status = u8"mono_thread_attach 不可用。";
        }

        if (ok) {
            g_emoteHookInstalled.store(true, std::memory_order_release);
        }
        SetEmoteStatus(status);
        LOGI(OBFUSCATE("[EmoteUnlock] %s"), status.c_str());
        g_emoteInstallRunning.store(false, std::memory_order_release);
        return;
    }

    // JIT 指针变了（重编译/热更）就重挂：这是 Mono JIT detour 必须做的巡检。
    if (_monoCompileMethod && g_emoteListMethod && g_emoteListSlots[0].address) {
        void *current = _monoCompileMethod(g_emoteListMethod);
        if (current && current != g_emoteListSlots[0].address) {
            LOGI(OBFUSCATE("[EmoteUnlock] JIT 指针变化 %p -> %p，重新挂载"),
                 g_emoteListSlots[0].address, current);
            ResetEmoteHookState();
            return;
        }
    }

    DrainEmotePending();
}

// 菜单按钮：手动重新安装（换世界后如果没自动接上）。
void ReinstallEmoteUnlockHooksNative() {
    if (g_emoteInstallRunning.exchange(true)) return;
    RunOnUnityThreadAsync([]() {
        std::string status;
        if (!_monoGetRootDomain || !_monoGetRootDomain()) {
            status = u8"Mono 未就绪。";
        } else if (!IsGameWorldReadyNative()) {
            status = u8"还没进世界：先进游戏再按（登录界面挂 hook 会闪退）。";
        } else if (_monoThreadAttach) {
            _monoThreadAttach(_monoGetRootDomain());
            std::string listStatus;
            if (BuildEmoteUnlockList(&listStatus)) {
                std::string hookStatus;
                const bool ok = InstallEmoteHooks(&hookStatus);
                g_emoteHookInstalled.store(ok, std::memory_order_release);
                status = ok ? (u8"重装完成 —— " + listStatus) : (u8"重装失败：" + hookStatus);
            } else {
                status = u8"列表构建失败：" + listStatus;
            }
        } else {
            status = u8"mono_thread_attach 不可用。";
        }
        SetEmoteStatus(status);
        LOGI(OBFUSCATE("[EmoteUnlock] reinstall: %s"), status.c_str());
        g_emoteInstallRunning.store(false, std::memory_order_release);
    });
}

// 菜单按钮：手动试演一个 id（不走面板，直接跑本地那一半）。
void PlayEmoteUnlockLocalNowNative(int actionId, bool forceLoop) {
    RunOnUnityThreadAsync([actionId, forceLoop]() {
        if (_monoGetRootDomain && _monoThreadAttach) _monoThreadAttach(_monoGetRootDomain());
        const bool ok = PlaySocialActionLocalNative(actionId, forceLoop);
        SetEmoteStatus(ok ? (u8"试演成功：" + std::to_string(actionId))
                          : (u8"试演失败：" + std::to_string(actionId)));
        LOGI(OBFUSCATE("[EmoteUnlock] 试演 id=%d ok=%d"), actionId, ok ? 1 : 0);
    });
}

std::string GetEmoteUnlockStatus() {
    std::lock_guard<std::mutex> lock(g_emoteUnlockMutex);
    return GetEmoteStatusLocked();
}
