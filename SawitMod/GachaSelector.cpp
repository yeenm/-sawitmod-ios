#include "ModFeatures.h"
#include "ModUtils.h"
#include "Includes/obfuscate.h"
#include "ByNameModding/Tools.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace {

template<typename T>
T ReadValue(uintptr_t address, T fallback) {
    return address ? *reinterpret_cast<T *>(address) : fallback;
}

} // namespace

void ScanGachaPoolsNative() {
    g_gachaPoolScanBusy.store(true, std::memory_order_release);
    std::vector<GachaPoolEntry> found;

    void *domain = _monoGetRootDomain();
    if (domain) _monoThreadAttach(domain);
    void *image = _monoImageLoaded(OBFUSCATE("EcsClient"));
    void *tableData = image ? _monoClassFromName(image, OBFUSCATE(""), OBFUSCATE("TableData")) : nullptr;
    if (!tableData && image)
        tableData = _monoClassFromName(image, OBFUSCATE("XD.GameGerm.ExpParser"), OBFUSCATE("TableData"));

    void *dict = nullptr;
    if (tableData && domain) {
        void *field = _monoClassGetFieldFromName(tableData, OBFUSCATE("TableGachaPools"));
        void *vtable = _monoClassGetVTable(domain, tableData);
        if (field && vtable) _monoFieldStaticGetValue(vtable, field, &dict);
    }

    if (dict) {
        void *dictClass = _monoObjectGetClass(dict);
        void *entriesField = dictClass ? _monoClassGetFieldFromName(dictClass, OBFUSCATE("_entries")) : nullptr;
        if (!entriesField && dictClass)
            entriesField = _monoClassGetFieldFromName(dictClass, OBFUSCATE("entries"));
        void *entries = nullptr;
        if (entriesField) _monoFieldGetValue(dict, entriesField, &entries);

        const int length = entries ? ReadValue<int>(reinterpret_cast<uintptr_t>(entries) + 0x18, 0) : 0;
        const uintptr_t data = entries ? reinterpret_cast<uintptr_t>(entries) + 0x20 : 0;
        for (int i = 0; i < length; ++i) {
            const uintptr_t slot = data + static_cast<uintptr_t>(i) * 24;
            if (ReadValue<int>(slot, -1) < 0) continue;
            const int key = ReadValue<int>(slot + 8, 0);
            void *pool = ReadValue<void *>(slot + 16, nullptr);
            if (!pool || key <= 0) continue;

            void *poolClass = _monoObjectGetClass(pool);
            if (!poolClass) continue;
            GachaPoolEntry entry;
            entry.id = key;
            if (void *field = _monoClassGetFieldFromName(poolClass, OBFUSCATE("id")))
                _monoFieldGetValue(pool, field, &entry.id);
            if (void *field = _monoClassGetFieldFromName(poolClass, OBFUSCATE("classId")))
                _monoFieldGetValue(pool, field, &entry.classId);
            if (void *field = _monoClassGetFieldFromName(poolClass, OBFUSCATE("sortId")))
                _monoFieldGetValue(pool, field, &entry.sortId);

            void *getName = FindMethodInHierarchy(poolClass, OBFUSCATE("get_name"), 0);
            if (getName) {
                void *exc = nullptr;
                void *result = _monoRuntimeInvoke(getName, pool, nullptr, &exc);
                if (!exc && result) entry.name = ReadMonoStringNative(result);
            }
            void *getType = FindMethodInHierarchy(poolClass, OBFUSCATE("get_poolType"), 0);
            if (getType) {
                void *exc = nullptr;
                void *result = _monoRuntimeInvoke(getType, pool, nullptr, &exc);
                if (!exc && result)
                    entry.poolType = *reinterpret_cast<int *>(_monoObjectUnbox(result));
            }
            if (entry.name.empty()) entry.name = std::string("Pool #") + std::to_string(entry.id);
            found.push_back(std::move(entry));
        }
    }

    std::sort(found.begin(), found.end(), [](const GachaPoolEntry &a, const GachaPoolEntry &b) {
        if (a.sortId != b.sortId) return a.sortId < b.sortId;
        return a.id < b.id;
    });
    {
        std::lock_guard<std::mutex> lock(g_gachaPoolsMutex);
        g_gachaPools = std::move(found);
        bool selectedExists = false;
        for (const auto &pool: g_gachaPools) {
            if (pool.id == g_selectedGachaPoolId) {
                selectedExists = true;
                break;
            }
        }
        if (!selectedExists)
            g_selectedGachaPoolId = g_gachaPools.empty() ? 0 : g_gachaPools.front().id;
    }
    g_gachaPoolScanBusy.store(false, std::memory_order_release);
}

void OpenGachaRewardsNative(int poolId, int selectedTab) {
    if (poolId <= 0) return;
    void *domain = _monoGetRootDomain();
    if (domain) _monoThreadAttach(domain);
    void *image = _monoImageLoaded(OBFUSCATE("XDTGameUI"));
    void *panel = image ? _monoClassFromName(
        image, OBFUSCATE("XDTGame.UI.Panel"), OBFUSCATE("GashaponPoolPanel")) : nullptr;
    void *open = panel ? FindMethodInHierarchy(panel, OBFUSCATE("Open"), 2) : nullptr;
    if (!open) return;

    int id = poolId;
    int tab = selectedTab;
    void *args[] = {&id, &tab};
    void *exc = nullptr;
    _monoRuntimeInvoke(open, nullptr, args, &exc);
}
