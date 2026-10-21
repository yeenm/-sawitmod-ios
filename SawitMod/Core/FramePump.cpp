// ============================================================================
//  FramePump.cpp — Unity 帧回调（从 Android 版 Main.cpp 第 1890~2449 行移植）
// ----------------------------------------------------------------------------
//  包含三块：
//    * RunUnityFramePump()：每帧泵（任务队列、功能 OnUpdate、雷达扫描…）
//    * GameApp_Update()：被 DobbyHook 挂到 GameApp.Update 的替身
//    * RenderESPOverlay()：雷达 ESP 叠加层（在菜单之后、ImGui::Render 之前画）
//
//  Android 版由 Main.cpp 的 hack_thread 装 hook；iOS 版由 App/Bootstrap.mm
//  在 il2cpp 就绪后装（地址来自 Core/Offsets-iOS.h 的 OFF_GAMEAPP_UPDATE）。
// ============================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ModFeatures.h"
#include "NetAuth.h"
#include "Weiyan.h"
#include "LocalCatalog.h"
#include "PayloadVM.h"
#include "ModUtils.h"
#include "Core/State.h"
#include "ClientControlAgent.h"
#include "Global.h"
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "StartupTrace.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

using namespace std::chrono;

// 这两个函数定义在 Core/FeatureDispatch.cpp（原 Main.cpp 1232/1808 行）。
void ExecuteTableSwappingNative(std::string dllName, std::string nameSpace, std::string className,
                                int myCarId, int targetCarId);
void SwitchMapNative(int roomId, std::string dllName, std::string nameSpace, std::string className,
                     std::string methodName);


// 由 Bootstrap 安装 hook 时写入的真函数指针。
void (*SMOldGameAppUpdate)(void *) = nullptr;
void SMRunUnityFramePump() {
    if (!g_unityFramePumpReady.load(std::memory_order_acquire)) return;

    static std::atomic<bool> firstActiveFrame{true};
    const bool traceFrame = firstActiveFrame.exchange(false, std::memory_order_acq_rel);
    if (traceFrame) STARTUP_TRACE("F01 first Time.get_deltaTime frame entered");

    {
        // Catat Unity thread supaya RunOnUnityThreadSync dapat mendeteksi self-call.
        g_unityThreadId.store(std::this_thread::get_id());
        // Catat id render thread supaya RunOnUnityThreadSync bisa deteksi self-call.
        g_unityThreadId.store(std::this_thread::get_id());
        if (traceFrame) STARTUP_TRACE("F02 unity thread registered");
        // Agent dan hook lifecycle sudah disiapkan setelah Mono siap. Callback
        // update hanya memompa refresh/command ketika method ini memang aktif.
        ClientControlAgent::PumpUnityCommands();
        if (traceFrame) STARTUP_TRACE("F03 client-control pump returned");
        std::vector<std::function<void()>> tasksCopy;
        g_unityTaskMutex.lock();
        tasksCopy = g_unityTasks;
        g_unityTasks.clear();
        g_unityTaskMutex.unlock();
        if (traceFrame) STARTUP_TRACE("F04 unity task queue copied");
        static int testIndexCheckDelay = 0;
        testIndexCheckDelay++;
        if (testIndexCheckDelay >= 60) {
            testIndexCheckDelay = 0;
            DisableTestIndexNowNative();
        }
        if (traceFrame) STARTUP_TRACE("F05 periodic task scheduling complete");
        for (auto &task: tasksCopy) {
            // Jangan biarkan 1 task yang melempar exception menghentikan pump
            // atau menggantung RunOnUnityThreadSync (penyebab deadlock di loading 100%).
            try { task(); } catch (...) {}
        }
        if (traceFrame) STARTUP_TRACE("F06 unity tasks complete");
        if (g_cameraModActive) {
            try {
                ApplyCameraSettingsNative();
            } catch (...) {}
        }
        const bool autoFishHoverEnabled = g_autoFishingActive &&
            g_autoFishTeleport &&
            g_autoFishHoverBetweenCasts &&
            SesiAktif();
        if (autoFishHoverEnabled || g_autoFishHoverGravitySaved) {
            SetAutoFishHoverNative(autoFishHoverEnabled);
        }
        if (g_autoCookingActive && g_autoCookPayloadReady) {
            g_autoCookFrameDelay++;

            if (g_autoCookFrameDelay >= 10) {
                g_autoCookFrameDelay = 0;
                bool clickedAnythingThisFrame = false;

                for (const std::string &path: workPaths) {
                    void *panelObj = ExecuteDynamic(ac_dll, ac_ns_go, ac_cl_go, ac_m_find, path,
                                                    true, "");
                    if (panelObj != nullptr) {
                        char hexBuffer[32];
                        snprintf(hexBuffer, sizeof(hexBuffer), OBFUSCATE("%llx"),
                                 (unsigned long long) panelObj);

                        void *btnComp = ExecuteDynamic(ac_dll, ac_ns_go, ac_cl_go, ac_m_getcomp,
                                                       ac_cl_btn, false, std::string(hexBuffer));
                        if (btnComp != nullptr) {
                            char btnHex[32];
                            snprintf(btnHex, sizeof(btnHex), OBFUSCATE("%llx"),
                                     (unsigned long long) btnComp);

                            ExecuteDynamic(ac_dll, ac_ns_ui, ac_cl_sel, ac_m_int, ac_p_true, false,
                                           std::string(btnHex));

                            void *onClickEvent = ExecuteDynamic(ac_dll, ac_ns_ui, ac_cl_btn,
                                                                ac_m_onclick, "", false,
                                                                std::string(btnHex));
                            if (onClickEvent != nullptr) {
                                char clickHex[32];
                                snprintf(clickHex, sizeof(clickHex), OBFUSCATE("%llx"),
                                         (unsigned long long) onClickEvent);

                                ExecuteDynamic(ac_dll, ac_ns_evt, ac_cl_evt, ac_m_invoke, "", false,
                                               std::string(clickHex));
                                clickedAnythingThisFrame = true;
                            }
                        }
                    }
                }

                static int stuckCounter = 0;
                if (clickedAnythingThisFrame) {
                    stuckCounter = 0;
                } else {
                    void *cookPanelObj = ExecuteDynamic(ac_dll, ac_ns_go, ac_cl_go, ac_m_find,
                                                        OBFUSCATE(
                                                                "GameApp/startup_root(Clone)/XDUIRoot/Full/CookPanel(Clone)"),
                                                        true, "");
                    if (cookPanelObj != nullptr) {
                        stuckCounter++;
                        if (stuckCounter >= 30) {
                            std::vector<std::string> closeBtnPaths = {
                                    OBFUSCATE(
                                            "GameApp/startup_root(Clone)/XDUIRoot/Full/CookPanel(Clone)/back@w"),
                                    OBFUSCATE(
                                            "GameApp/startup_root(Clone)/XDUIRoot/Full/CookPanel(Clone)/back@btn"),
                                    OBFUSCATE(
                                            "GameApp/startup_root(Clone)/XDUIRoot/Full/CookPanel(Clone)/close@btn"),
                                    OBFUSCATE(
                                            "GameApp/startup_root(Clone)/XDUIRoot/Full/CookPanel(Clone)/closeBtn@w"),
                                    OBFUSCATE(
                                            "GameApp/startup_root(Clone)/XDUIRoot/Full/CookPanel(Clone)/BackBtn"),
                                    OBFUSCATE(
                                            "GameApp/startup_root(Clone)/XDUIRoot/Full/CookPanel(Clone)/back_btn")};

                            for (const std::string &cPath: closeBtnPaths) {
                                void *cPanelObj = ExecuteDynamic(ac_dll, ac_ns_go, ac_cl_go,
                                                                 ac_m_find, cPath, true, "");
                                if (cPanelObj != nullptr) {
                                    char hBuf[32];
                                    snprintf(hBuf, sizeof(hBuf), OBFUSCATE("%llx"),
                                             (unsigned long long) cPanelObj);
                                    void *cBtnComp = ExecuteDynamic(ac_dll, ac_ns_go, ac_cl_go,
                                                                    ac_m_getcomp, ac_cl_btn, false,
                                                                    std::string(hBuf));

                                    if (cBtnComp != nullptr) {
                                        char bHex[32];
                                        snprintf(bHex, sizeof(bHex), OBFUSCATE("%llx"),
                                                 (unsigned long long) cBtnComp);
                                        ExecuteDynamic(ac_dll, ac_ns_ui, ac_cl_sel, ac_m_int,
                                                       ac_p_true, false, std::string(bHex));
                                        void *cOnClick = ExecuteDynamic(ac_dll, ac_ns_ui, ac_cl_btn,
                                                                        ac_m_onclick, "", false,
                                                                        std::string(bHex));
                                        if (cOnClick != nullptr) {
                                            char cHex[32];
                                            snprintf(cHex, sizeof(cHex), OBFUSCATE("%llx"),
                                                     (unsigned long long) cOnClick);
                                            ExecuteDynamic(ac_dll, ac_ns_evt, ac_cl_evt,
                                                           ac_m_invoke, "", false,
                                                           std::string(cHex));
                                            stuckCounter = 0;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        stuckCounter = 0;
                    }
                }
            }
        }
        if (traceFrame) STARTUP_TRACE("F07 auto-cooking block complete");
        // Bird diproses setelah GameApp_Update asli selesai agar tidak melakukan
        // Clear/GetComponents pada collection PhotoMode di tengah update game.
        if (traceFrame) STARTUP_TRACE("F08 bird-watching deferred until post-update");
        if (g_radarEnabled) {
            void *camera = GetMainCameraNative();
            if (camera) {
                static void *m_get_width = nullptr;
                static void *m_get_height = nullptr;
                if (!m_get_width || !m_get_height) {
                    void *imgUnityEngine = _monoImageLoaded(OBFUSCATE("EngineWrapper"));
                    if (!imgUnityEngine) imgUnityEngine = _monoImageLoaded(OBFUSCATE("UnityEngine"));
                    void *klassScreen = imgUnityEngine ? _monoClassFromName(
                            imgUnityEngine, OBFUSCATE("UnityEngine"), OBFUSCATE("Screen")) : nullptr;
                    if (klassScreen) {
                        m_get_width = FindMethodInHierarchy(klassScreen, OBFUSCATE("get_width"), 0);
                        m_get_height = FindMethodInHierarchy(klassScreen, OBFUSCATE("get_height"), 0);
                    }
                }

                float sW = (float)glWidth;
                float sH = (float)glHeight;
                void *exc = nullptr;
                if (m_get_width) {
                    void *v = _monoRuntimeInvoke(m_get_width, nullptr, nullptr, &exc);
                    if (!exc && v) sW = (float)*(int *)_monoObjectUnbox(v);
                }
                exc = nullptr;
                if (m_get_height) {
                    void *v = _monoRuntimeInvoke(m_get_height, nullptr, nullptr, &exc);
                    if (!exc && v) sH = (float)*(int *)_monoObjectUnbox(v);
                }
                if (sW <= 0.0f) sW = (float)glWidth;
                if (sH <= 0.0f) sH = (float)glHeight;
                const float scaleX = sW > 0.0f ? (float)glWidth / sW : 1.0f;
                const float scaleY = sH > 0.0f ? (float)glHeight / sH : 1.0f;

                // Jangan tahan mutex selama pemanggilan managed WorldToScreenPoint.
                // Render EGL juga membaca daftar ini; lock panjang membuat seluruh
                // menu ImGui terlihat hilang/berkedip ketika ESP aktif.
                std::vector<RadarEntity> radarSnapshot;
                {
                    std::lock_guard<std::mutex> lock(g_radarMutex);
                    radarSnapshot = g_radarList;
                }

                struct ScreenResult {
                    uint32_t netId;
                    Vector3 pos;
                    bool onScreen;
                };
                std::vector<ScreenResult> results;
                results.reserve(radarSnapshot.size());
                for (const auto &ent: radarSnapshot) {
                    Vector3 sPos = WorldToScreenNative(camera, ent.worldPos);
                    ScreenResult result{ent.netId, {0, 0, 0}, false};
                    if (std::isfinite(sPos.x) && std::isfinite(sPos.y) &&
                        std::isfinite(sPos.z) && sPos.z > 0.01f) {
                        const float x = sPos.x * scaleX;
                        const float y = (sH - sPos.y) * scaleY;
                        result.pos = {x, y, sPos.z};
                        result.onScreen = x >= 0.0f && x <= (float)glWidth &&
                                          y >= 0.0f && y <= (float)glHeight;
                    }
                    results.push_back(result);
                }

                {
                    std::lock_guard<std::mutex> lock(g_radarMutex);
                    std::unordered_map<uint32_t, ScreenResult> byId;
                    byId.reserve(results.size());
                    for (const auto &r: results) byId[r.netId] = r;
                    for (auto &ent: g_radarList) {
                        auto it = byId.find(ent.netId);
                        if (it == byId.end()) {
                            ent.isOnScreen = false;
                            continue;
                        }
                        ent.screenPos = it->second.pos;
                        ent.isOnScreen = it->second.onScreen;
                    }
                }
            }
        }

        if (traceFrame) STARTUP_TRACE("F09 radar block complete");

        // Execute API Queue
        std::vector<InvokeRequest> pendingRequests;
        {
            std::lock_guard<std::mutex> lock(invokeMutex);
            if (!invokeQueue.empty()) {
                pendingRequests = invokeQueue;
                invokeQueue.clear();
            }
        }

        if (traceFrame) STARTUP_TRACE("F10 invoke queue copied");
        for (auto &req: pendingRequests) {
            if (!ValidasiOtentikasiKetat() || req.generasiSesi == 0 || req.generasiSesi != GenerasiSesi())
                continue;

            if (!req.vmStream.empty()) {
                PayloadVM::Context vmContext;
                vmContext.nonce = req.vmNonce;
                vmContext.hwid = req.vmHwid;
                vmContext.wireId = req.vmWireId;
                // 内嵌 dump 回放时绑定串用的是抓包那一刻的 version/channel，
                // 与当前 build 的 SAWIT_VERSION 可能不同（例如 5.6.0 vs 5.6.0-offline）。
                vmContext.version = req.vmBindVersion.empty() ? std::string(SAWIT_VERSION)
                                                              : req.vmBindVersion;
                vmContext.channel = req.vmBindChannel.empty() ? std::string(SAWIT_CHANNEL)
                                                              : req.vmBindChannel;
                vmContext.expectedOp = req.op;
                if (!PayloadVM::InterpretProtected(req.vmStream, vmContext, req)) {
                    PayloadVM::WipeString(req.vmStream);
                    PayloadVM::WipeString(req.vmNonce);
                    PayloadVM::WipeString(req.vmHwid);
                    PayloadVM::WipeString(req.vmWireId);
                    PayloadVM::WipeString(req.vmParamOverride);
                    PayloadVM::WipeString(req.vmInstanceOverride);
                    PayloadVM::WipeString(req.vmBindVersion);
                    PayloadVM::WipeString(req.vmBindChannel);
                    auth_status_msg = u8"载荷校验失败（VM2）";
                    auth_status_color = ImColor(172, 46, 38);
                    continue;
                }
                if (!req.vmParamOverride.empty()) req.paramInput = req.vmParamOverride;
                req.instancePtrHexStr = req.vmInstanceOverride;
                PayloadVM::WipeString(req.vmStream);
                PayloadVM::WipeString(req.vmNonce);
                PayloadVM::WipeString(req.vmHwid);
                PayloadVM::WipeString(req.vmWireId);
                PayloadVM::WipeString(req.vmParamOverride);
                PayloadVM::WipeString(req.vmInstanceOverride);
                PayloadVM::WipeString(req.vmBindVersion);
                PayloadVM::WipeString(req.vmBindChannel);
            }

            if (req.op == OP_SET_TIME) {
                float timeVal = 0.5f;
                try {
                    timeVal = std::stof(req.paramInput); // Konversi string kembali ke float
                } catch (...) {}

                SetGameTimeNative(timeVal);
                continue;
            }
            if (req.op == OP_CAMERA_CONTROLS) {
                continue;
            }
            if (req.op == OP_BIRD_WATCHING &&
                req.methodDesc == (const char *) OBFUSCATE("CUSTOM_BIRD_WATCHING")) {
                g_birdCurrentTarget = {};
                g_birdRequestPending.store(false, std::memory_order_release);
                g_birdRetryCount = 0;
                g_birdNextActionAt = 0;
                g_enableBirdFarm = true;
                g_birdFarmStatus = u8"扫描中";
                continue;
            }
            if (req.op == OP_SWITCH_MAP) {
                LOGI((const char *) OBFUSCATE("[SWITCH-MAP] Trigger GameApp_Update..."));
                int roomId = 0;
                try { roomId = std::stoi(req.paramInput); } catch (...) {}

                if (roomId > 0) {
                    SwitchMapNative(roomId, req.dllName, req.nameSpace, req.className,
                                    req.methodDesc);
                }
                continue;
            }
            if (req.methodDesc == (const char *) OBFUSCATE("CUSTOM_INSTANT_FARM")) {
                std::thread([req]() {
                    ExecuteInstantFarmThread(req.dllName, req.nameSpace, req.className);
                }).detach();
                continue;
            }
            if (req.op == OP_PICK_ALL_PLANT &&
                req.methodDesc == (const char *) OBFUSCATE("CUSTOM_PICK_ALL_PLANT")) {
                std::thread([req]() {
                    PickAllOwnedMaturePlants(req.dllName, req.nameSpace, req.className, req.payloadRaw);
                }).detach();
                continue;
            }
            if (req.op == OP_GACHA_SELECTOR &&
                req.methodDesc == (const char *) OBFUSCATE("CUSTOM_GACHA_SELECTOR")) {
                if (req.paramInput == (const char *) OBFUSCATE("scan")) {
                    ScanGachaPoolsNative();
                } else if (req.paramInput.rfind((const char *) OBFUSCATE("rewards:"), 0) == 0) {
                    int poolId = 0;
                    try { poolId = std::stoi(req.paramInput.substr(8)); } catch (...) {}
                    if (poolId > 0) OpenGachaRewardsNative(poolId, 0);
                }
                continue;
            }
            if (req.op == OP_WEATHER_CHANGER &&
                req.methodDesc == (const char *) OBFUSCATE("CUSTOM_WEATHER_CHANGER")) {
                if (req.paramInput == (const char *) OBFUSCATE("scan")) {
                    ScanWeatherTypesNative();
                } else if (req.paramInput == (const char *) OBFUSCATE("restore")) {
                    RestoreLiveWeatherNative();
                } else if (req.paramInput.rfind((const char *) OBFUSCATE("apply:"), 0) == 0) {
                    const std::string args = req.paramInput.substr(6);
                    const size_t split = args.find(':');
                    int weatherId = 0;
                    float transition = 0.0f;
                    try {
                        weatherId = std::stoi(args.substr(0, split));
                        if (split != std::string::npos)
                            transition = std::stof(args.substr(split + 1));
                    } catch (...) {}
                    if (weatherId > 0) ApplyLocalWeatherNative(weatherId, transition);
                } else if (req.paramInput.rfind((const char *) OBFUSCATE("surface:"), 0) == 0) {
                    float amount = 0.0f;
                    try { amount = std::stof(req.paramInput.substr(8)); } catch (...) {}
                    if (amount < 0.0f) amount = 0.0f;
                    if (amount > 1.0f) amount = 1.0f;
                    ApplySnowSurfaceAmountNative(amount);
                }
                continue;
            }
            if (req.op == OP_AUTO_SAND || req.methodDesc == (const char *) OBFUSCATE("CUSTOM_AUTO_SAND")) {
                static std::atomic<bool> s_sandThreadRunning{false};
                if (!s_sandThreadRunning.exchange(true)) {
                    g_isAutoSandActive = true;
                    std::thread([]() {
                        RunAutoSandSculptureLoop();
                        g_isAutoSandActive = false;
                        s_sandThreadRunning = false;
                    }).detach();
                }
                continue;
            }
            if (req.op == OP_AUTO_SNOW || req.methodDesc == (const char *) OBFUSCATE("CUSTOM_AUTO_SNOW")) {
                if (!g_isAutoSnowActive) {
                    g_isAutoSnowActive = true;
                    std::thread([]() {
                        RunAutoSnowSculptureLoop();
                        g_isAutoSnowActive = false;
                    }).detach();
                }
                continue;
            }
            if (req.op == OP_AUTO_PUMPKIN || req.methodDesc == (const char *) OBFUSCATE("CUSTOM_AUTO_PUMPKIN")) {
                if (req.payloadRaw.empty() || !SesiAktif()) {
                    g_pumpkinPn.clear();
                    g_isAutoPumpkinActive = false;
                    continue;
                }
                g_pumpkinPn = req.payloadRaw;
                g_isAutoPumpkinActive = true;
                continue;
            }
            if (req.op == OP_AUTO_FISHING || req.methodDesc == (const char *) OBFUSCATE("CUSTOM_AUTO_FISHING")) {
                if (!g_autoFishingActive) {
                    g_autoFishingActive = true;
                    const uint32_t gen = g_autoFishingGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
                    std::thread(AutoFishingLoop, gen).detach();
                }
                continue;
            }
            if (req.op == OP_AUTO_FISH_BAIT || req.methodDesc == (const char *) OBFUSCATE("CUSTOM_AUTO_FISH_BAIT")) {
                if (req.payloadRaw.empty() || !SesiAktif()) {
                    g_autoFishBaitActive = false;
                    continue;
                }
                if (!g_autoFishBaitActive) {
                    g_autoFishBaitActive = true;
                    const uint32_t gen = g_fishBaitLoopGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
                    std::thread(AutoFishBaitLoop, gen).detach();
                }
                continue;
            }
            if (req.featureId == (const char *) OBFUSCATE("MACRO_TELE_HOME")) {
                void *ret = ExecuteDynamic(req.dllName, req.nameSpace, req.className,
                                           req.methodDesc, "", req.isStatic);
                uint32_t homeId = UnboxToUint(ret);
                if (homeId != 0) {
                    std::string param = std::to_string(homeId);
                    std::thread([param]() { FetchAndExecute(WireForOp(OP_TELE_TO_HOME), param); })
                            .detach();
                }
                continue;
            }
            if (req.methodDesc == (const char *) OBFUSCATE("CUSTOM_REMOTE_SHOP")) {
                OpenRemoteShopNative();
                continue;
            }
            if (req.methodDesc == (const char *) OBFUSCATE("CUSTOM_VEHICLE_SWAP")) {
                size_t delimPos = req.paramInput.find('|');
                if (delimPos != std::string::npos) {
                    int myCarId = std::stoi(req.paramInput.substr(0, delimPos));
                    int targetCarId = std::stoi(req.paramInput.substr(delimPos + 1));
                    ExecuteTableSwappingNative(req.dllName, req.nameSpace, req.className, myCarId,
                                               targetCarId);
                }
                continue; // Bebaskan dari antrean ExecuteDynamic
            }
            if (req.featureId == (const char *) OBFUSCATE("MACRO_SCAN_HOMES")) {
                std::string oDll, oNs, oCl, oMeth;
                std::stringstream ssData(req.paramInput);
                std::getline(ssData, oDll, '|');
                std::getline(ssData, oNs, '|');
                std::getline(ssData, oCl, '|');
                std::getline(ssData, oMeth, '|');

                for (int i = 0; i < 12; i++) {
                    void *hPtr = ExecuteDynamic(req.dllName, req.nameSpace, req.className,
                                                req.methodDesc, g_homeCoords[i], true);
                    if (hPtr != nullptr) {
                        char hexBuffer[32];
                        snprintf(hexBuffer, sizeof(hexBuffer), OBFUSCATE("%llx"),
                                 (unsigned long long) hPtr);

                        void *ownerRet = ExecuteDynamic(oDll, oNs, oCl, oMeth, "", false,
                                                        std::string(hexBuffer));
                        uint32_t ownerId = UnboxToUint(ownerRet);
                        if (ownerId != 0) {
                            g_ownerNetIds[i] = ownerId;
                        }
                    }
                }
                continue;
            }
            if (req.methodDesc == (const char *) OBFUSCATE("CUSTOM_NPC_COSPLAY")) {
                int npcChoice = 0;
                try {
                    npcChoice = std::stoi(req.paramInput);
                }
                catch (...) {
                }

                if (npcChoice >= 1 && npcChoice <= 9) {
                    ExecuteNpcCosplayNative(npcChoice, req.dllName);
                }
                continue; // Bebaskan dari antrean ExecuteDynamic
            }

            void *ret = ExecuteDynamic(req.dllName, req.nameSpace, req.className, req.methodDesc,
                                       req.paramInput, req.isStatic, req.instancePtrHexStr);

            if (req.op == OP_GET_SELF_PLAYER && ret != nullptr) {
                char hexBuffer[32];
                snprintf(hexBuffer, sizeof(hexBuffer), OBFUSCATE("%llx"), (unsigned long long) ret);
                g_cachedPlayerPtrHex = std::string(hexBuffer);
                g_speedActive = true;
            }
        }

        if (traceFrame) STARTUP_TRACE("F11 invoke queue complete");
        if (g_speedHackNeedsUpdate) {
            SetGlobalTimeScale(g_targetSpeed);
            g_speedHackNeedsUpdate = false;
        }
        if (traceFrame) STARTUP_TRACE("F12 mod frame logic complete");
    }
}

void SMGameAppUpdate(void *instance) {
    const bool pumpReady = instance && g_unityFramePumpReady.load(std::memory_order_acquire);
    if (pumpReady) SMRunUnityFramePump();
    if (SMOldGameAppUpdate) SMOldGameAppUpdate(instance);
    if (pumpReady) {
        ProcessBirdWatchingNative();
        ProcessPumpkinCarvingOnUpdate();
        // 表情解锁：hook 只记录，真正的本地补演在 Unity 线程上做（PC 版同款分工）。
        ProcessEmoteUnlockOnUpdate();
        // 双人互动：开着的时候定期重贴（换世界会换一份等级字典）。
        ProcessFriendInteractUnlockOnUpdate();
        // 只读符号自检：进世界后自动跑一次（不挂 hook、不开功能）。
        MaybeRunAutoSymbolProbe();
    }
}

// Tambahkan di Main.cpp (dekat DrawMenu)
void RenderESPOverlay() {
    if (!g_radarEnabled) return;
    ImDrawList *drawList = ImGui::GetBackgroundDrawList();
    std::lock_guard<std::mutex> lock(g_radarMutex);
    for (const auto &ent : g_radarList) {
        if (!ent.isOnScreen) continue;              // sudah difilter z>0 di GameApp_Update
        ImU32 color = IM_COL32(255, 255, 255, 255);
        if      (ent.type == 0) color = IM_COL32(255,  50,  50, 255); // Player merah
        else if (ent.type == 1) color = IM_COL32( 50, 255,  50, 255); // Resource hijau
        else if (ent.type == 2) color = IM_COL32( 50, 255, 255, 255); // Bird cyan
        else if (ent.type == 3) color = IM_COL32( 50, 150, 255, 255); // Fish biru
        else if (ent.type == 4) color = IM_COL32(255, 255,  50, 255); // NPC kuning
        else if (ent.type == 5) color = IM_COL32(255, 150,  50, 255); // Insect oranye

        ImVec2 sp(ent.screenPos.x, ent.screenPos.y);
        float distance = ent.screenPos.z;           // .z = jarak kamera (meter)

        std::string name = ent.name.empty() ? std::string("Player") : ent.name;
        char buf[128];
        snprintf(buf, sizeof(buf), "%s [%.0fm]", name.c_str(), distance);

        drawList->AddCircleFilled(sp, 3.5f, color);
        drawList->AddCircle(sp, 3.5f, IM_COL32(0, 0, 0, 255), 0, 1.5f);

        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImVec2 tp(sp.x - ts.x / 2.0f, sp.y + 6.0f);
        drawList->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 200), buf);
        drawList->AddText(tp, color, buf);
    }
}

