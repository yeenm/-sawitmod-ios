// ============================================================================
//  FriendInteractUnlock.cpp — 解锁所有双人互动
//  移植自 PC 版 Bugtopia v3.2.5 → buddy/FriendInteractUnlockFeature.cs
// ----------------------------------------------------------------------------
//  和"解锁表情"不是同一个机关。单人动作/表情来自一份"已拥有"列表（服务器同步的
//  ECS 实体），可以自己造一份假的顶上；双人互动从来不查那份列表：FriendInteractPanel
//  直接遍历 TableData.TableFriendInteracts，一切由每一行的字段决定。
//
//    InitInteractData —— 这一行列不列出来？
//      gainCondition = Default(0)   -> 永远列
//                      Activity(1)  -> 只在 appearDate 的活动窗口内列   <- 本功能唯一打开的闸门
//                      Reward(2)    -> 拥有才列（本版表里 0 行，所以"伪造拥有列表"这条路是死路）
//      unlockType    = 0            -> 进"好友等级"桶
//                      其它         -> 需要 ActivityFriendShipPoint[activityEnum] >= friendshipLv，
//                                      而且字典里缺 key 会把这一行整个丢掉
//
//    RenderItems —— 这一行点得动吗？
//      available = (bucketLevel <= _friendComponent.FriendShipLevel)   <- 不动
//
//  这些都是普通字段，没有函数可 detour —— 但字段可以直接写。事件锁的行会被写成
//  gainCondition = 0（列出来）和 unlockType = 0（走等级桶，唯一会列东西的那条路）。
//  其它字段一个都不碰。
//
//  ⚠️ friendshipLv 绝对不动。PC 版实测把它拍成 0 会让全游戏的互动都变成可点，然后
//     服务器老老实实回 FriendShipLvNotEnough(601)，一次三十条，客户端连提示文案都
//     没有。等级是真实进度且服务端强校验，本地改它只会换来一张更长的列表。
//  ⚠️ isDisable 同样不动：被禁用的行是官方关掉的。
//
//  ⚠️ 纯客户端。点击时走的还是游戏自己的 SendFriendDuoAction，双人动画是服务器状态
//     （PlayerSocialData，两边都写）。单人动作可以本地补演，双人pose必须对方一起被
//     驱动，没有本地半边可以伪造。
// ============================================================================

#include "ModFeatures.h"
#include "ModUtils.h"
#include "MonoGameApi.h"
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>

using namespace MonoApi;

namespace {

// 表按"当前等级的字典"索引：本版一共 54 个 id（378 行 = 54 id × 7 个等级变体）。
// 探测范围比 54 大一点，将来表加行不用改代码。
constexpr int kFriendInteractMaxId = 120;

// 即使被活动窗口藏起来也不解锁的行。它们靠字段分不出来（16/32/33/50 看起来就是
// 普通动画），因为分流写在面板代码里，所以逐个 id 追到发起它的面板：
//   15  ChooseGiftPanel            礼物选择
//   16  赠礼（icon 516）
//   17  BattlePassFriendSellPanel  协助出售，带 OuterItemNetPair
//   18  BattlePassFriendShopPanel  协助购买，带 OuterItemNetPair
//   19  ChooseStickerPanel         贴纸，带 selectedStickers
//   32  ActivityMlpTreePanel       活动流程
//   33  ChooseBadgePanel           徽章赠礼
//   38  万圣节讨糖 —— 季节活动（interactionId 3034），要服务器侧活动开着
//   44  赠礼（连续状态 stateType=2 / isContinue=1）
//   50  ChooseInteractGiftPanel.Open(50, actId)，TableFriendInteractGift 里唯一的行
//
// ⚠️ 这份名单会在游戏更新后过期，更新后重新 grep 面板代码里的 `new FriendDoubleSocialEvent`。
constexpr int kFriendInteractExcludedIds[] = {15, 16, 17, 18, 19, 32, 33, 38, 44, 50};
constexpr int kFriendInteractExcludedCount =
        sizeof(kFriendInteractExcludedIds) / sizeof(kFriendInteractExcludedIds[0]);

// 重贴周期：换世界会换一份 LevelTableFriendInteracts（不同的行对象），所以开着的时候
// 定期重新扫一遍。120 次表查找对 Unity 线程来说可以忽略不计。
constexpr int kFriendInteractRecheckFrames = 600; // ~10 秒 @60fps

std::string g_friendInteractStatus = u8"未启用。";
int g_friendInteractListed = 0;
int g_friendInteractSkipped = 0;
int g_friendInteractFrameCounter = 0;
std::atomic<bool> g_friendInteractBusy{false};

void SetFriendStatus(const std::string &status) {
    std::lock_guard<std::mutex> lock(g_friendInteractUnlockMutex);
    g_friendInteractStatus = status;
}

bool IsExcludedId(int id) {
    for (int i = 0; i < kFriendInteractExcludedCount; ++i) {
        if (kFriendInteractExcludedIds[i] == id) return true;
    }
    return false;
}

const char *kFriendImages[] = {
        OBFUSCATE("EcsClient"), OBFUSCATE("EcsSystem"), OBFUSCATE("XDTLevelAndEntity"),
        OBFUSCATE("XDTDataAndProtocol"), OBFUSCATE("XDTGameSystem"), OBFUSCATE("XDTGameUI"),
        OBFUSCATE("XDTBaseService"), OBFUSCATE("Plugins"),
};
constexpr int kFriendImageCount = sizeof(kFriendImages) / sizeof(kFriendImages[0]);

void *FindFriendClass(const char *nameSpace, const char *className) {
    return FindClass(kFriendImages, kFriendImageCount, nameSpace, className, nullptr, true);
}

// 真正的补丁逻辑；必须跑在 Unity 线程上（会做 mono_runtime_invoke 和字段写）。
// 返回列出来的行数，-1 表示失败（失败原因写进 status）。
int ApplyFriendInteractUnlockInternal(std::string *status, bool verboseLog, bool unlockLevels) {
    if (!_monoRuntimeInvoke || !_monoClassGetFieldFromName || !_monoFieldSetValue) {
        *status = u8"Mono 反射 API 未就绪。";
        return -1;
    }

    // TableData 在全局命名空间 —— 和表情解锁用的是同一个类。
    void *tableData = FindFriendClass(OBFUSCATE(""), OBFUSCATE("TableData"));
    if (!tableData) {
        tableData = FindFriendClass(OBFUSCATE("XD.GameGerm.ExpParser"), OBFUSCATE("TableData"));
    }
    if (!tableData) {
        *status = u8"TableData 未找到。";
        return -1;
    }

    // 逐行取，而不是遍历字典：从托管侧枚举 Dictionary<int,T> 意味着对值类型做泛型
    // invoke，PC 版记录过这会直接把这个运行时搞崩。getter 是普通静态方法，取回来
    // 立刻写，中间不分配，指针不会被搬走。
    // 按"名字 + (int,bool) 签名"挑重载：下面要 invoke 120 次，签名挑错一个，
    // 参数就是错位的。
    void *getter = FindMethodBySignature(tableData, OBFUSCATE("GetFriendInteract"), "ib");
    if (!getter) {
        getter = FindMethodBySignature(tableData, OBFUSCATE("GetFriendInteract"), "ii");
    }
    if (!getter) {
        *status = u8"TableData.GetFriendInteract(int,bool) 未解析，候选：";
        *status += DescribeMethodsNamed(tableData, OBFUSCATE("GetFriendInteract"));
        LOGI(OBFUSCATE("[FriendInteract] %s"), status->c_str());
        return -1;
    }
    if (!MethodIsStatic(getter)) {
        // 表 getter 在 PC/Android 都是静态方法；实例方法会让下面 null self 的 invoke
        // 直接抛异常（看起来像"一行都读不到"）。这里明确报出来，别让人猜。
        *status = u8"TableData.GetFriendInteract 不是静态方法，拒绝调用。";
        LOGI(OBFUSCATE("[FriendInteract] %s"), status->c_str());
        return -1;
    }

    void *rowClass = FindFriendClass(OBFUSCATE(""), OBFUSCATE("TableFriendInteract"));
    if (!rowClass) {
        *status = u8"TableFriendInteract 未找到。";
        return -1;
    }

    void *gainField = FindFieldInHierarchy(rowClass, OBFUSCATE("gainCondition"));
    void *unlockField = FindFieldInHierarchy(rowClass, OBFUSCATE("unlockType"));
    if (!gainField || !unlockField) {
        // fail CLOSED：只打一半比不打更糟 —— 行列出来了却仍然点不动，用户看到的是
        // "这功能坏了"，而不是"功能没开"。
        *status = u8"TableFriendInteract 字段未解析（gainCondition="
                  + std::string(gainField ? "OK" : "NULL") + u8", unlockType="
                  + std::string(unlockField ? "OK" : "NULL") + u8"）。";
        return -1;
    }

    // 诊断字段（只读，用来把面板里的一行翻译回 id）
    void *iconField = FindFieldInHierarchy(rowClass, OBFUSCATE("motionDisplayIcon"));
    void *sortField = FindFieldInHierarchy(rowClass, OBFUSCATE("sortPriority"));
    void *levelField = FindFieldInHierarchy(rowClass, OBFUSCATE("friendshipLv"));
    void *disableField = FindFieldInHierarchy(rowClass, OBFUSCATE("isDisable"));

    int patched = 0;
    int leveled = 0;
    int skippedExcluded = 0;
    int rowsSeen = 0;
    uint8_t needException = 0;
    int zero = 0;

    for (int id = 1; id <= kFriendInteractMaxId; ++id) {
        int probeId = id;
        void *args[2] = {&probeId, &needException};
        void *exception = nullptr;
        void *row = _monoRuntimeInvoke(getter, nullptr, args, &exception);
        if (exception || !row) continue; // 这个版本/等级没有这个互动

        ++rowsSeen;

        // 物品转移和季节性活动按要求保持锁着。
        if (IsExcludedId(id)) {
            ++skippedExcluded;
            continue;
        }

        int gain = 0;
        _monoFieldGetValue(row, gainField, &gain);

        if (verboseLog) {
            int icon = 0, sort = 0, level = 0, disabled = 0;
            if (iconField) _monoFieldGetValue(row, iconField, &icon);
            if (sortField) _monoFieldGetValue(row, sortField, &sort);
            if (levelField) _monoFieldGetValue(row, levelField, &level);
            if (disableField) _monoFieldGetValue(row, disableField, &disabled);
            LOGI(OBFUSCATE("[FriendInteract] id=%d icon=%d sort=%d wasLv=%d wasGain=%d%s"),
                 id, icon, sort, level, gain, disabled != 0 ? " wasDISABLED" : "");
        }

        // ① 事件锁：gainCondition=0（活动窗口外也列）+ unlockType=0（走等级桶）。
        //    只动"被事件锁住"的行；其它行游戏本来就按自己的规则列出来。
        if (gain != 0) {
            _monoFieldSetValue(row, gainField, &zero);   // Default：活动窗口外也列
            _monoFieldSetValue(row, unlockField, &zero); // 等级桶，不再看活动点数
            ++patched;
        }

        // ② 等级锁（可选，用户单独开的那一项）：friendshipLv 拍成 0。
        //    panel 的判断是 `bucketLevel <= FriendShipLevel`，bucketLevel 就是这一行
        //    的 friendshipLv，所以拍 0 之后所有双人动作都会变成"可点"。
        //
        //    ⚠️ 这只是把客户端的门槛拿掉：真正越级的那几个，服务器会照旧回
        //       FriendShipLvNotEnough(601)（PC 版实测过，客户端连提示文案都没有）。
        //       等级内、以及活动锁的那批，服务器是认的。
        if (unlockLevels && levelField) {
            int level = 0;
            _monoFieldGetValue(row, levelField, &level);
            if (level != 0) {
                _monoFieldSetValue(row, levelField, &zero);
                ++leveled;
            }
        }
        // isDisable 一律不动：被禁用的行是官方关掉的。
    }

    g_friendInteractListed = patched;
    g_friendInteractSkipped = skippedExcluded;

    if (unlockLevels) {
        *status = u8"事件锁 " + std::to_string(patched) + u8" 行；等级锁 " + std::to_string(leveled)
                  + u8" 行已拍平（跳过 " + std::to_string(skippedExcluded)
                  + u8" 个物品/季节项）。越级的那几个服务器仍会回 601。";
    } else if (patched > 0) {
        *status = u8"已列出 " + std::to_string(patched) + u8" 个事件锁互动（跳过 "
                  + std::to_string(skippedExcluded) + u8" 个物品/季节项，好友等级未改动）。";
    } else if (rowsSeen > 0) {
        *status = u8"没有需要打开的事件锁行（扫描 " + std::to_string(rowsSeen)
                  + u8" 行，已跳过 " + std::to_string(skippedExcluded) + u8" 个）。";
    } else {
        *status = u8"当前等级表里没读到任何互动行（可能还没进世界）。";
    }
    return patched + leveled;
}

} // namespace

// 每帧由 GameApp_Update（Unity 线程）调用。
void ProcessFriendInteractUnlockOnUpdate() {
    if (!g_friendInteractUnlockEnabled) {
        g_friendInteractFrameCounter = 0;
        return;
    }
    if (g_friendInteractBusy.load(std::memory_order_acquire)) return;

    if (++g_friendInteractFrameCounter < kFriendInteractRecheckFrames) return;
    g_friendInteractFrameCounter = 0;

    if (!_monoGetRootDomain || !_monoGetRootDomain()) return;
    // 没进世界时不碰表：登录/加载阶段等级字典还没建好，这时候去 invoke 表 getter
    // 是纯粹的赌运气。
    if (!IsGameWorldReadyNative()) return;
    if (_monoThreadAttach) _monoThreadAttach(_monoGetRootDomain());

    // 定期重贴：TableFriendInteracts 是按等级分字典的，换世界会换成另一批行对象，
    // 上一份补丁在新世界里没有意义。
    const int before = g_friendInteractListed;
    std::string status;
    const int now = ApplyFriendInteractUnlockInternal(&status, false, g_friendInteractLevelUnlockEnabled);
    if (now >= 0 && now != before) {
        SetFriendStatus(status);
        LOGI(OBFUSCATE("[FriendInteract] 定期重贴: %s"), status.c_str());
    }
}

// 菜单按钮：按下时立即执行一次（自己转 Unity 线程）。
void ApplyFriendInteractUnlockNative() {
    if (g_friendInteractBusy.exchange(true)) return;
    RunOnUnityThreadAsync([]() {
        std::string status = u8"Mono 未就绪。";
        if (_monoGetRootDomain && _monoGetRootDomain()) {
            if (!IsGameWorldReadyNative()) {
                status = u8"还没进世界：先进游戏再按（登录界面上写表会闪退）。";
            } else if (_monoThreadAttach) {
                _monoThreadAttach(_monoGetRootDomain());
                const int patched = ApplyFriendInteractUnlockInternal(&status, true, g_friendInteractLevelUnlockEnabled);
                LOGI(OBFUSCATE("[FriendInteract] 应用结果 patched=%d: %s"), patched,
                     status.c_str());
            } else {
                status = u8"mono_thread_attach 不可用。";
            }
        }
        SetFriendStatus(status);
        g_friendInteractFrameCounter = 0;
        g_friendInteractBusy.store(false, std::memory_order_release);
    });
}

std::string GetFriendInteractUnlockStatus() {
    std::lock_guard<std::mutex> lock(g_friendInteractUnlockMutex);
    return g_friendInteractStatus;
}
