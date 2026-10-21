#include "ModFeatures.h"
#include "ModUtils.h"
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace {

template<typename T>
T ReadActionValue(uintptr_t address, T fallback) {
    return address ? *reinterpret_cast<T *>(address) : fallback;
}

void SetPlayerActionStatus(const std::string &status) {
    std::lock_guard<std::mutex> lock(g_playerActionsMutex);
    g_playerActionStatus = status;
}

void *LoadedImage(const char *name) {
    if (!_monoImageLoaded) return nullptr;
    void *image = _monoImageLoaded(name);
    if (image) return image;
    std::string dllName(name);
    dllName += ".dll";
    return _monoImageLoaded(dllName.c_str());
}

void *ResolveSelfPlayer() {
    void *image = LoadedImage(OBFUSCATE("XDTLevelAndEntity"));
    void *klass = image ? _monoClassFromName(image,
            OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
            OBFUSCATE("EntityUtil")) : nullptr;
    void *method = klass ? FindMethodInHierarchy(klass, OBFUSCATE("GetSelfPlayer"), 0) : nullptr;
    if (!method) return nullptr;
    void *exc = nullptr;
    void *player = _monoRuntimeInvoke(method, nullptr, nullptr, &exc);
    return exc ? nullptr : player;
}

} // namespace

void ScanPlayerActionsNative() {
    if (g_playerActionBusy.exchange(true, std::memory_order_acq_rel)) return;

    std::vector<PlayerActionEntry> found;
    std::string status;
    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (domain && _monoThreadAttach) _monoThreadAttach(domain);

    void *image = LoadedImage(OBFUSCATE("EcsClient"));
    void *tableData = image ? _monoClassFromName(image, OBFUSCATE(""), OBFUSCATE("TableData")) : nullptr;
    if (!tableData && image)
        tableData = _monoClassFromName(image, OBFUSCATE("XD.GameGerm.ExpParser"), OBFUSCATE("TableData"));

    void *dict = nullptr;
    void *field = tableData ? _monoClassGetFieldFromName(tableData,
            OBFUSCATE("TableSingleactions")) : nullptr;
    void *vtable = (field && domain && _monoClassGetVTable)
            ? _monoClassGetVTable(domain, tableData) : nullptr;
    if (field && vtable && _monoFieldStaticGetValue)
        _monoFieldStaticGetValue(vtable, field, &dict);

    if (!dict) {
        status = u8"动作表不可用，请先进入游戏世界。";
    } else {
        void *dictClass = _monoObjectGetClass(dict);
        void *entriesField = dictClass ? _monoClassGetFieldFromName(dictClass,
                OBFUSCATE("_entries")) : nullptr;
        if (!entriesField && dictClass)
            entriesField = _monoClassGetFieldFromName(dictClass, OBFUSCATE("entries"));
        void *entries = nullptr;
        if (entriesField) _monoFieldGetValue(dict, entriesField, &entries);

        const int length = entries
                ? ReadActionValue<int>(reinterpret_cast<uintptr_t>(entries) + OFF_ARR_COUNT, 0) : 0;
        const uintptr_t data = entries
                ? reinterpret_cast<uintptr_t>(entries) + OFF_ARR_DATA : 0;
        for (int i = 0; i < length; ++i) {
            const uintptr_t slot = data + static_cast<uintptr_t>(i) * SZ_DICT_ENTRY;
            if (ReadActionValue<int>(slot, -1) < 0) continue;
            const int key = ReadActionValue<int>(slot + OFF_ENTRY_KEY, 0);
            void *action = ReadActionValue<void *>(slot + OFF_ENTRY_VALUE, nullptr);
            if (!action || key <= 0) continue;

            void *klass = _monoObjectGetClass(action);
            if (!klass) continue;
            PlayerActionEntry entry;
            entry.id = key;
            if (void *f = _monoClassGetFieldFromName(klass, OBFUSCATE("id")))
                _monoFieldGetValue(action, f, &entry.id);

            uint8_t loop = 0;
            if (void *f = _monoClassGetFieldFromName(klass, OBFUSCATE("_isLoop")))
                _monoFieldGetValue(action, f, &loop);
            entry.isLoop = loop != 0;

            uint8_t rarity = 0;
            if (void *f = _monoClassGetFieldFromName(klass, OBFUSCATE("_rarity")))
                _monoFieldGetValue(action, f, &rarity);
            entry.rarity = static_cast<int>(rarity);

            if (void *f = _monoClassGetFieldFromName(klass, OBFUSCATE("resName"))) {
                void *text = nullptr;
                _monoFieldGetValue(action, f, &text);
                if (text) entry.resource = ReadMonoStringNative(text);
            }
            if (void *getName = FindMethodInHierarchy(klass, OBFUSCATE("get_name"), 0)) {
                void *exc = nullptr;
                void *text = _monoRuntimeInvoke(getName, action, nullptr, &exc);
                if (!exc && text) entry.name = ReadMonoStringNative(text);
            }
            if (entry.name.empty() || entry.name == "NULL" || entry.name == "Invalid_String")
                entry.name = entry.resource.empty()
                        ? std::string("Action #") + std::to_string(entry.id)
                        : entry.resource;
            found.push_back(std::move(entry));
        }

        std::sort(found.begin(), found.end(), [](const PlayerActionEntry &a,
                                                 const PlayerActionEntry &b) {
            if (a.name != b.name) return a.name < b.name;
            return a.id < b.id;
        });
        found.erase(std::unique(found.begin(), found.end(),
                [](const PlayerActionEntry &a, const PlayerActionEntry &b) {
                    return a.id == b.id;
                }), found.end());
        status = found.empty() ? "No single actions found." :
                 std::to_string(found.size()) + " local action(s) loaded.";
    }

    {
        std::lock_guard<std::mutex> lock(g_playerActionsMutex);
        g_playerActions = std::move(found);
        g_playerActionStatus = status;
        const bool selectedExists = std::any_of(g_playerActions.begin(), g_playerActions.end(),
                [](const PlayerActionEntry &entry) {
                    return entry.id == g_selectedPlayerActionId;
                });
        if (!selectedExists)
            g_selectedPlayerActionId = g_playerActions.empty() ? 0 : g_playerActions.front().id;
    }
    LOGI("[PlayerAction] scan selesai: %s", status.c_str());
    g_playerActionBusy.store(false, std::memory_order_release);
}

// 本地玩家能取到 = 已经进世界（表数据、社交服务这时才是可用的）。
// 两个解锁功能在装 hook / 写表之前都要过这一关：登录界面就挂 detour 的话，
// 登录流程自己会调用这些方法，签名哪怕差一个参数就是当场闪退。
bool IsGameWorldReadyNative() {
    return ResolveSelfPlayer() != nullptr;
}

// 走游戏自己那条本地回放链路演一个社交动作：
//   SocialReqTask(player, id, loop)  ->  OnResp(PlayerSocialEvent{netId, id, loop})
// 这就是服务器回执到达时客户端做的事，只是这里由我们自己喂回执，所以没买过的
// 动作/表情也能在本地演出来（PC 版 EmoteUnlockFeature 的 HALF 2 同一个套路）。
//
// ⚠️ 必须在 Unity 线程上调用：内部会 _monoRuntimeInvoke 并分配托管对象。
bool PlaySocialActionLocalNative(int actionId, bool forceLoop) {
    if (actionId <= 0) return false;

    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!domain) return false;
    if (_monoThreadAttach) _monoThreadAttach(domain);

    void *levelImage = LoadedImage(OBFUSCATE("XDTLevelAndEntity"));
    void *dataImage = LoadedImage(OBFUSCATE("XDTDataAndProtocol"));
    void *taskClass = levelImage ? _monoClassFromName(levelImage,
            OBFUSCATE("XDTLevelAndEntity.Gameplay.Interaction"),
            OBFUSCATE("SocialReqTask")) : nullptr;
    void *ctor = taskClass ? FindMethodInHierarchy(taskClass, OBFUSCATE(".ctor"), 3) : nullptr;
    void *onResp = taskClass ? FindMethodInHierarchy(taskClass, OBFUSCATE("OnResp"), 1) : nullptr;
    void *playerData = dataImage ? _monoClassFromName(dataImage,
            OBFUSCATE("XDTDataAndProtocol"), OBFUSCATE("PlayerDataCenter")) : nullptr;
    void *getNetId = playerData ? FindMethodInHierarchy(playerData,
            OBFUSCATE("GetSelfNetPlayerId"), 0) : nullptr;
    void *player = ResolveSelfPlayer();

    if (!player || !taskClass || !ctor || !onResp || !getNetId || !_monoObjectNew)
        return false;

    void *exc = nullptr;
    void *boxedNetId = _monoRuntimeInvoke(getNetId, nullptr, nullptr, &exc);
    uint32_t netId = (!exc && boxedNetId && _monoObjectUnbox)
            ? *reinterpret_cast<uint32_t *>(_monoObjectUnbox(boxedNetId)) : 0;
    if (exc || netId == 0) return false;

    void *task = _monoObjectNew(domain, taskClass);
    if (!task) return false;

    int id = actionId;
    uint8_t loop = forceLoop ? 1 : 0;
    void *ctorArgs[] = {player, &id, &loop};
    _monoRuntimeInvoke(ctor, task, ctorArgs, &exc);
    if (exc) return false;

    struct PlayerSocialEvent {
        uint32_t playerNetId;
        int32_t socialType;
        uint8_t isLoopSingle;
        uint8_t padding[3];
    } response{netId, actionId, loop, {0, 0, 0}};
    void *responseArgs[] = {&response};
    _monoRuntimeInvoke(onResp, task, responseArgs, &exc);
    return exc == nullptr;
}

void PlayPlayerActionNative(int actionId, bool forceLoop) {
    if (actionId <= 0 || g_playerActionBusy.exchange(true, std::memory_order_acq_rel)) return;

    RunOnUnityThreadAsync([actionId, forceLoop]() {
        std::string status;
        if (!_monoGetRootDomain || !_monoGetRootDomain()) {
            status = "Mono domain is unavailable.";
        } else if (!ResolveSelfPlayer()) {
            status = u8"本地玩家不可用，请先进入游戏世界。";
        } else if (PlaySocialActionLocalNative(actionId, forceLoop)) {
            status = std::string("Local action played: ID ") + std::to_string(actionId);
        } else {
            status = u8"本地动作播放失败（接口不可用或托管异常）。";
        }
        SetPlayerActionStatus(status);
        LOGI("[PlayerAction] play id=%d loop=%d status=%s",
             actionId, forceLoop ? 1 : 0, status.c_str());
        g_playerActionBusy.store(false, std::memory_order_release);
    });
}

void StopPlayerActionNative() {
    if (g_playerActionBusy.exchange(true, std::memory_order_acq_rel)) return;

    RunOnUnityThreadAsync([]() {
        void *player = ResolveSelfPlayer();
        std::string status = u8"本地玩家不可用。";
        if (player) {
            void *playerClass = _monoObjectGetClass(player);
            void *cancel = playerClass ? FindMethodInHierarchy(playerClass,
                    OBFUSCATE("OnLeftJoystickCanceled"), 0) : nullptr;
            if (cancel) {
                void *exc = nullptr;
                _monoRuntimeInvoke(cancel, player, nullptr, &exc);
                status = exc ? "Local stop failed." : "Local action stopped.";
            } else {
                status = u8"本地停止接口不可用。";
            }
        }
        SetPlayerActionStatus(status);
        LOGI("[PlayerAction] stop status=%s", status.c_str());
        g_playerActionBusy.store(false, std::memory_order_release);
    });
}
