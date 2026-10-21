#include "ModFeatures.h"
#include "ModUtils.h"
#include "Includes/obfuscate.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace {

template<typename T>
T ReadWeatherValue(uintptr_t address, T fallback) {
    return address ? *reinterpret_cast<T *>(address) : fallback;
}

void SetWeatherStatus(const std::string &status) {
    std::lock_guard<std::mutex> lock(g_weatherTypesMutex);
    g_weatherStatus = status;
}

void *ResolveLevelWeatherSystem(void **outClass = nullptr) {
    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (!domain) return nullptr;
    if (_monoThreadAttach) _monoThreadAttach(domain);

    void *levelImage = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!levelImage) levelImage = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
    void *weatherClass = levelImage ? _monoClassFromName(
            levelImage, OBFUSCATE("XDTLevelAndEntity.BaseSystem.Weather"),
            OBFUSCATE("LevelWeatherSystem")) : nullptr;
    if (outClass) *outClass = weatherClass;
    if (!weatherClass || !_monoClassGetType || !_monoTypeGetObject) return nullptr;

    void *frameworkImage = _monoImageLoaded(OBFUSCATE("XDTBaseService"));
    if (!frameworkImage) frameworkImage = _monoImageLoaded(OBFUSCATE("XDTGame.Framework"));
    void *managers = frameworkImage ? _monoClassFromName(
            frameworkImage, OBFUSCATE("XDTGame.Framework"), OBFUSCATE("Managers")) : nullptr;
    void *getModule = managers ? FindMethodInHierarchy(
            managers, OBFUSCATE("GetModule"), 1) : nullptr;
    if (!getModule) return nullptr;

    void *typeObject = _monoTypeGetObject(domain, _monoClassGetType(weatherClass));
    if (!typeObject) return nullptr;
    void *args[] = {typeObject};
    void *exc = nullptr;
    void *instance = _monoRuntimeInvoke(getModule, nullptr, args, &exc);
    return exc ? nullptr : instance;
}

} // namespace

void ScanWeatherTypesNative() {
    g_weatherScanBusy.store(true, std::memory_order_release);
    std::vector<WeatherTypeEntry> found;
    std::string status;

    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (domain && _monoThreadAttach) _monoThreadAttach(domain);
    void *image = _monoImageLoaded(OBFUSCATE("EcsClient"));
    void *tableData = image ? _monoClassFromName(image, OBFUSCATE(""), OBFUSCATE("TableData")) : nullptr;
    if (!tableData && image)
        tableData = _monoClassFromName(image, OBFUSCATE("XD.GameGerm.ExpParser"), OBFUSCATE("TableData"));

    void *dict = nullptr;
    void *getWeatherTypes = tableData ? FindMethodInHierarchy(
            tableData, OBFUSCATE("get_TableWeatherTypes"), 0) : nullptr;
    if (getWeatherTypes) {
        void *exc = nullptr;
        dict = _monoRuntimeInvoke(getWeatherTypes, nullptr, nullptr, &exc);
        if (exc) dict = nullptr;
    }

    if (!dict) {
        status = u8"天气表不可用，请先进入游戏世界。";
    } else {
        void *dictClass = _monoObjectGetClass(dict);
        void *entriesField = dictClass ? _monoClassGetFieldFromName(dictClass, OBFUSCATE("_entries")) : nullptr;
        if (!entriesField && dictClass)
            entriesField = _monoClassGetFieldFromName(dictClass, OBFUSCATE("entries"));
        void *entries = nullptr;
        if (entriesField) _monoFieldGetValue(dict, entriesField, &entries);

        const int length = entries ? ReadWeatherValue<int>(reinterpret_cast<uintptr_t>(entries) + OFF_ARR_COUNT, 0) : 0;
        const uintptr_t data = entries ? reinterpret_cast<uintptr_t>(entries) + OFF_ARR_DATA : 0;
        for (int i = 0; i < length; ++i) {
            const uintptr_t slot = data + static_cast<uintptr_t>(i) * SZ_DICT_ENTRY;
            if (ReadWeatherValue<int>(slot, -1) < 0) continue;
            const int key = ReadWeatherValue<int>(slot + OFF_ENTRY_KEY, 0);
            void *weather = ReadWeatherValue<void *>(slot + OFF_ENTRY_VALUE, nullptr);
            if (!weather || key <= 0) continue;

            void *klass = _monoObjectGetClass(weather);
            if (!klass) continue;
            WeatherTypeEntry entry;
            entry.id = key;
            if (void *field = _monoClassGetFieldFromName(klass, OBFUSCATE("id")))
                _monoFieldGetValue(weather, field, &entry.id);
            if (void *field = _monoClassGetFieldFromName(klass, OBFUSCATE("weatherClass")))
                _monoFieldGetValue(weather, field, &entry.weatherClass);
            if (void *field = _monoClassGetFieldFromName(klass, OBFUSCATE("isFunctionWeather")))
                _monoFieldGetValue(weather, field, &entry.isFunctionWeather);
            if (void *field = _monoClassGetFieldFromName(klass, OBFUSCATE("buildModePreview")))
                _monoFieldGetValue(weather, field, &entry.buildModePreview);

            auto readName = [&](const char *getter) -> std::string {
                void *method = FindMethodInHierarchy(klass, getter, 0);
                if (!method) return {};
                void *exc = nullptr;
                void *value = _monoRuntimeInvoke(method, weather, nullptr, &exc);
                return (!exc && value) ? ReadMonoStringNative(value) : std::string{};
            };
            entry.nameDay = readName(OBFUSCATE("get_nameDay"));
            entry.nameNight = readName(OBFUSCATE("get_nameNight"));
            if (entry.nameDay.empty() || entry.nameDay == "NULL" || entry.nameDay == "Invalid_String")
                entry.nameDay = std::string("Weather #") + std::to_string(entry.id);
            if (entry.nameNight.empty() || entry.nameNight == "NULL" || entry.nameNight == "Invalid_String")
                entry.nameNight = entry.nameDay;
            found.push_back(std::move(entry));
        }

        std::sort(found.begin(), found.end(), [](const WeatherTypeEntry &a, const WeatherTypeEntry &b) {
            return a.id < b.id;
        });
        found.erase(std::unique(found.begin(), found.end(), [](const WeatherTypeEntry &a, const WeatherTypeEntry &b) {
            return a.id == b.id;
        }), found.end());
        status = found.empty() ? "No weather types found for this level." :
                 std::to_string(found.size()) + " weather type(s) loaded.";
    }

    {
        std::lock_guard<std::mutex> lock(g_weatherTypesMutex);
        g_weatherTypes = std::move(found);
        g_weatherStatus = status;
        bool selectedExists = false;
        for (const auto &weather: g_weatherTypes) {
            if (weather.id == g_selectedWeatherId) {
                selectedExists = true;
                break;
            }
        }
        if (!selectedExists)
            g_selectedWeatherId = g_weatherTypes.empty() ? 0 : g_weatherTypes.front().id;
    }
    g_weatherScanBusy.store(false, std::memory_order_release);
}

void ApplyLocalWeatherNative(int weatherId, float transitionSeconds) {
    if (weatherId <= 0) return;
    RunOnUnityThreadAsync([weatherId, transitionSeconds]() {
        void *weatherClass = nullptr;
        void *instance = ResolveLevelWeatherSystem(&weatherClass);
        if (!instance || !weatherClass) {
            SetWeatherStatus("LevelWeatherSystem is unavailable.");
            return;
        }
        void *setWeather = FindMethodInHierarchy(weatherClass, OBFUSCATE("SetWeather"), 3);
        if (!setWeather) {
            SetWeatherStatus("SetWeather(int,int,float) was not found.");
            return;
        }

        int previousWeather = 0;
        if (void *field = FindFieldInHierarchy(weatherClass, OBFUSCATE("_currentWeather")))
            _monoFieldGetValue(instance, field, &previousWeather);
        float transition = transitionSeconds;
        if (transition < 0.0f) transition = 0.0f;
        if (transition > 10.0f) transition = 10.0f;
        int targetWeather = weatherId;
        void *args[] = {&targetWeather, &previousWeather, &transition};
        void *exc = nullptr;
        _monoRuntimeInvoke(setWeather, instance, args, &exc);
        if (exc) {
            SetWeatherStatus("Applying local weather raised a managed exception.");
            return;
        }

        if (void *setSnowDelay = FindMethodInHierarchy(weatherClass, OBFUSCATE("SetSnowDelay"), 1)) {
            int id = weatherId;
            void *snowArgs[] = {&id};
            void *snowExc = nullptr;
            _monoRuntimeInvoke(setSnowDelay, instance, snowArgs, &snowExc);
        }
        if (void *playRainbow = FindMethodInHierarchy(weatherClass, OBFUSCATE("TryPlayRainbowFX"), 0)) {
            void *rainbowExc = nullptr;
            _monoRuntimeInvoke(playRainbow, instance, nullptr, &rainbowExc);
        }
        SetWeatherStatus("Local weather and scene effects applied.");
    });
}

void ApplySnowSurfaceAmountNative(float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    RunOnUnityThreadAsync([amount]() {
        void *weatherClass = nullptr;
        void *levelWeather = ResolveLevelWeatherSystem(&weatherClass);
        if (!levelWeather || !weatherClass) {
            SetWeatherStatus("LevelWeatherSystem is unavailable.");
            return;
        }

        void *getManager = FindMethodInHierarchy(weatherClass, OBFUSCATE("get__weatherManager"), 0);
        if (!getManager) {
            SetWeatherStatus("WeatherManager getter was not found.");
            return;
        }
        void *exc = nullptr;
        void *manager = _monoRuntimeInvoke(getManager, levelWeather, nullptr, &exc);
        if (exc || !manager) {
            SetWeatherStatus("WeatherManager is unavailable.");
            return;
        }

        void *managerClass = _monoObjectGetClass(manager);
        void *setSnowTime = managerClass ? FindMethodInHierarchy(
                managerClass, OBFUSCATE("SetSnowTime"), 3) : nullptr;
        if (!setSnowTime) {
            SetWeatherStatus("SetSnowTime was not found.");
            return;
        }

        int snowMode = amount > 0.0f ? 1 : 0;
        float progress = amount;
        float duration = amount > 0.0f ? 1000000000.0f : 0.0f;
        void *args[] = {&snowMode, &progress, &duration};
        exc = nullptr;
        _monoRuntimeInvoke(setSnowTime, manager, args, &exc);
        if (exc) SetWeatherStatus("Applying snow surface raised a managed exception.");
        else SetWeatherStatus(std::string("Snow surface applied: ") +
                              std::to_string(static_cast<int>(amount * 100.0f + 0.5f)) + "%");
    });
}

void RestoreLiveWeatherNative() {
    RunOnUnityThreadAsync([]() {
        void *weatherClass = nullptr;
        void *instance = ResolveLevelWeatherSystem(&weatherClass);
        if (!instance || !weatherClass) {
            SetWeatherStatus("LevelWeatherSystem is unavailable.");
            return;
        }
        void *enterLive = FindMethodInHierarchy(weatherClass, OBFUSCATE("EnterLive"), 1);
        if (!enterLive) {
            SetWeatherStatus("EnterLive(bool) was not found.");
            return;
        }
        uint8_t force = 1;
        void *args[] = {&force};
        void *exc = nullptr;
        _monoRuntimeInvoke(enterLive, instance, args, &exc);
        if (exc) SetWeatherStatus("Restoring live weather raised a managed exception.");
        else SetWeatherStatus("Live weather restored.");
    });
}
