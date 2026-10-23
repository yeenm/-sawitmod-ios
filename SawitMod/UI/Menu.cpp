// ============================================================================
//  Menu.cpp — 菜单界面（从 Android 版 Main.cpp 的 DrawMenu 原样移植）
// ----------------------------------------------------------------------------
//  这段代码是原工程 Main.cpp 第 2451~5296 行，一行未改地搬过来：
//    * 只依赖 ImGui + ActiveUI(CyloneUI) + Core/State.h 的全局状态
//    * 没有 JNI / EGL / ANativeWindow 依赖（Android 版也没有）
//  因此移植成本极低，也保证了 iOS 与 Android 的界面、文案、功能开关完全一致。
//
//  平台相关只有两处：
//    * OpenURL -> SMPlatform::OpenURL（见 CyloneOpenUrl）
//    * 字体/DPI 初始化在 UI/MenuGlue.cpp（需要 GL 上下文）
// ============================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AndroidBrandConfig.h"
#include "App/Alert.h"
#include "Core/AntiBan.h"
#include "Core/FramePump.h"
#include "App/OverlayWindow.h"
#include "App/Platform.h"
#include "ModFeatures.h"
#include "ModUtils.h"
#include "Core/State.h"
#include "Global.h"
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "LocalCatalog.h"
#include "NetAuth.h"
#include "PayloadVM.h"
#include "Version.h"

#include "Core/ShopTranslate.h"

// 这些实现位于 Core/FeatureDispatch.cpp（原 Main.cpp 的中间段）：
void ExecuteInjectJpgToCanvasNative(std::string imagePath);
bool EnsureChatHookInstalled();
void SetDrawStatusText(const std::string &text);
#include "Weiyan.h"

// CyloneUI 的 LoadTexture 直接用 GL；iOS 用 GLES3（与 imgui_impl_opengl3 一致）。
#if defined(IMGUI_IMPL_OPENGL_ES3)
#  if defined(__APPLE__)
#    include <OpenGLES/ES3/gl.h>
#  else
#    include <GLES3/gl3.h>
#  endif
#endif
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/custom/ActiveUI.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

using namespace std::chrono;

// 原 Main.cpp 里的静态辅助：打开外部链接（Android 走 JNI Intent，iOS 走 UIApplication）
static bool CyloneOpenUrlAndroid(const char *url) { return true; }
static bool CyloneOpenUrl(const char *url) {
    if (url && *url)
        OpenURL(url);
    return true;
}

void DrawMenu() {
    ImGuiIO &io = ImGui::GetIO();
    if (g_MenuMinimized) {
        bool restore = false;
        ImVec2 miniMin{}, miniMax{};
        ActiveUI::DrawMinimized(&restore, &miniMin, &miniMax);
        if (restore)
            g_MenuMinimized = false;
        {
            std::lock_guard<std::mutex> lock(g_MenuBoundsMutex);
            g_MenuRect.active = false;
            g_MinRect = {miniMin.x, miniMin.y,
                         miniMax.x - miniMin.x, miniMax.y - miniMin.y,
                         !restore};
        }
        return;
    }

    static int login_tab = 2;
    static int product_tab = 1;
    static int game_sub = 0;
    // 界面固定中文：中英切换按钮已去掉，is_english 恒为 false。
    g_language = LANG_CN;
    bool is_english = false;
    bool request_min = false;

    // 卡密验证闸门：没通过之前「功能」「公告」两页不出现在导航里，
    // 功能页内容也换成卡密提示，FetchAndExecute 同样被挡住（NetAuth 里还有一道）。
    const bool cardVerified = Weiyan::IsVerified();
    ActiveUI::features_unlocked = cardVerified;
    {
        static bool bcUnlock = false;
        if (cardVerified && !bcUnlock) {
            bcUnlock = true;
            SMPlatform::SetBreadcrumb("ui:unlock");
        }
    }

    // 标题栏右侧显示版本号（Version.h 的 SAWIT_UI_VERSION，仅界面用）。
    ActiveUI::FrameInfo frame = ActiveUI::BeginChrome(
            ANDROID_BRAND_NAME, SAWIT_UI_VERSION, &request_min, &is_english, CyloneOpenUrl);
    if (request_min)
        g_MenuMinimized = true;
    {
        std::lock_guard<std::mutex> lock(g_MenuBoundsMutex);
        g_MenuRect = {frame.win_min.x, frame.win_min.y,
                      frame.win_max.x - frame.win_min.x,
                      frame.win_max.y - frame.win_min.y, true};
        g_MinRect.active = false;
    }

    // 「云下发」状态机（每次启动只告知一次）：
    //   未登录  -> 提示"请登录接收服务器云下发"
    //   登录成功 -> 进"接收云下发文件中…"（约 3.5 秒，带百分比）
    //   接收完成 -> 弹一次"云下发接收成功"，文案转"云下发已就绪"
    //   本次启动已经收到过 -> 一律只显示"云下发已就绪"，不再提示、不再弹窗
    static std::chrono::steady_clock::time_point cloudStart{};
    static bool cloudRunning = false;
    static bool cloudDone = false;      // 本次启动是否已接收完成
    static bool cloudToastShown = false;
    constexpr float kCloudSeconds = 3.5f;

    if (cloudDone) {
        cloudRunning = false;           // 本次启动已完成：后续登录不再走流程
    } else if (!cardVerified) {
        cloudRunning = false;
    } else if (!cloudRunning) {
        cloudRunning = true;
        cloudStart = std::chrono::steady_clock::now();
    }

    float cloudProgress = 0.0f;
    if (cloudRunning) {
        const float elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - cloudStart).count();
        cloudProgress = elapsed / kCloudSeconds;
        if (cloudProgress >= 1.0f) {
            cloudProgress = 1.0f;
            cloudRunning = false;
            cloudDone = true;
        }
    }
    if (cloudDone && !cloudToastShown) {
        cloudToastShown = true;
        SMPlatform::SetBreadcrumb("ui:cloud-toast");
        ActiveUI::ShowToast(u8"云下发接收成功", 3.2f);
    }

    // 导航顺序 主页 / 功能 / 公告 / 设置：「功能」页现在免费功能也在这里，
    // 所以不再因为没卡密而隐藏或把 tab 拉回主页。

    // ---- 启动即装内嵌会话 + 功能目录（只做一次）----
    // 以前这一步只在卡密验证成功后才做，于是没卡密时 SesiAktif()/HasWire() 都是假，
    // 免费功能点了没反应。会话本身不带授权判断，授权闸门在 Virbox_Fetch* 里。
#if SAWIT_OFFLINE_MODE
    static std::once_flag localSessionOnce;
    std::call_once(localSessionOnce, []() { LoginOfflineSession(false); });
#endif

    // Rail kategori Product berada tepat di kanan rail Home/Product/Updates.
    // Fungsi ini juga menggeser content_min agar konten langsung mulai dari atas.
    const bool sesiOtentik = ValidasiOtentikasiKetat();
    const bool productUsesInnerScroll =
            ActiveUI::tab == 1 && cardVerified && sesiOtentik && !g_avatarMode;
    if (productUsesInnerScroll)
        ActiveUI::DrawProductCategoryRail(&product_tab, is_english, frame);

    // Halaman Product (termasuk GAME > WEATHER) mempunyai child scroll sendiri.
    // Nonaktifkan scroll parent agar drag sentuh hanya ditangani satu window.
    ActiveUI::BeginContent(frame, !productUsesInnerScroll);

    // ---- 免费功能（不需要卡密）：采集类 ----
    // 驱动方式必须和付费页一致，这里踩过两次坑：
    //   1) op=11 不是走 VM 派发，它有原生 worker；
    //   2) 瞬间采集有**两套**系统，真正带"蓝莓"的是新的那套：
    //        g_farmSelections[4]（farmNames[] 第 5 项 = Blueberry）
    //        + g_instantFarmingActive + g_instantFarmGeneration
    //        + ExecuteInstantFarmThread()（读 g_farmSelections 决定采什么）
    //      旧的那套是 g_farmBush/g_farmDynamicBush + ExecuteLegacyInstantFarmThread。
    //   3) op=10 自动收集气泡走 AutoBubbleLoopThread（条件 g_autoCollectBubbleActive）。
    static constexpr int kBlueberryIndex = 4;   // farmNames[]: Ore/Stone/Mandarin/Raspberry/Blueberry
    static std::atomic<bool> g_berryAuto{false};    // 蓝莓自动采集
    static std::atomic<bool> g_bubbleAuto{false};   // 自动收集气泡

    auto startBerryAuto = []() {
        g_farmSelections[kBlueberryIndex] = true;   // 蓝莓
        g_instantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
        g_instantFarmingActive = true;
        std::thread([]() {
            if (!SesiAktif()) {
                g_instantFarmingActive = false;
                return;
            }
            InvokeRequest req = FetchPayloadSync(WireForOp(OP_INSTANT_FARM_TRIGGER));
            if (req.dllName.empty() || req.nameSpace.empty() || req.className.empty()) {
                g_instantFarmingActive = false;
                return;
            }
            ExecuteInstantFarmThread(req.dllName, req.nameSpace, req.className);
        }).detach();
    };
    auto stopBerryAuto = []() {
        g_instantFarmingActive = false;
        g_instantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
        // 旧系统一起收掉，避免两个 worker 同时跑
        g_legacyInstantFarmingActive = false;
        g_legacyInstantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
    };
    auto startBubbleAuto = []() {
        g_autoCollectBubbleActive = true;
        ResetBubbleHistory();
        std::thread([]() {
            if (!SesiAktif()) {
                g_autoCollectBubbleActive = false;
                return;
            }
            InvokeRequest req = FetchPayloadSync(WireForOp(OP_AUTO_COLLECT_BUBBLE));
            if (req.dllName.empty() || req.nameSpace.empty() || req.className.empty()) {
                g_autoCollectBubbleActive = false;
                return;
            }
            AutoBubbleLoopThread(req.dllName, req.nameSpace, req.className);
        }).detach();
    };
    auto stopBubbleAuto = []() { g_autoCollectBubbleActive = false; };

    // 免费功能区块：只在未登录（会员功能没解锁）时画；卡密通过后不再显示
    auto DrawFreeFeatures = [&]() {
        ImGui::PushStyleColor(ImGuiCol_Text, ActiveUI::kAccent);
        ImGui::TextUnformatted(u8"免费功能（无需卡密）");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        const ImVec4 onColor(0.16f, 0.52f, 0.28f, 1.0f);
        const bool berry = g_berryAuto.load();
        ImGui::PushStyleColor(ImGuiCol_Button, berry ? onColor : ActiveUI::kButton);
        if (ImGui::Button(berry ? u8"蓝莓自动采集：开启中##berry"
                                : u8"蓝莓自动采集：已关闭##berry",
                          ImVec2(-1.0f, ActiveUI::S(42)))) {
            const bool next = !berry;
            g_berryAuto.store(next);
            if (next) startBerryAuto(); else stopBerryAuto();
            ActiveUI::ShowToast(next ? u8"蓝莓自动采集已开启" : u8"蓝莓自动采集已关闭", 1.6f);
        }
        ImGui::PopStyleColor();
        if (ImGui::Button(u8"立即采集一次##berry_once", ImVec2(-1.0f, ActiveUI::S(34)))) {
            startBerryAuto();
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::seconds(4));
                if (!g_berryAuto.load()) {   // 开关没开的话，采完这一轮就停
                    g_instantFarmingActive = false;
                    g_instantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
                }
            }).detach();
            ActiveUI::ShowToast(u8"已开始一次采集", 1.4f);
        }

        const bool bubble = g_bubbleAuto.load();
        ImGui::PushStyleColor(ImGuiCol_Button, bubble ? onColor : ActiveUI::kButton);
        if (ImGui::Button(bubble ? u8"自动收集气泡：开启中##bubble"
                                 : u8"自动收集气泡：已关闭##bubble",
                          ImVec2(-1.0f, ActiveUI::S(38)))) {
            const bool next = !bubble;
            g_bubbleAuto.store(next);
            if (next) startBubbleAuto(); else stopBubbleAuto();
            ActiveUI::ShowToast(next ? u8"自动收集气泡已开启" : u8"自动收集气泡已关闭", 1.6f);
        }
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", u8"这两项采集功能不需要卡密。");
        ImGui::TextDisabled("%s", u8"蓝莓自动采集＝持续采集蓝莓；自动收集气泡每 1.5 秒收一次。");
    };

    if (ActiveUI::tab == 0) {
        const bool en = (g_language == LANG_EN);
        // 新版首页：标题 + 信息表 + 操作区。旧版的霓虹动画大标题已去掉。
        ImGui::PushStyleColor(ImGuiCol_Text, ActiveUI::kAccent);
        ImGui::TextUnformatted(ANDROID_BRAND_NAME);
        ImGui::PopStyleColor();
        // 版本号 + 云下发状态
        //   本次启动已收到过 / 未登录过 -> 不重复告知
        if (cloudDone) {
            ImGui::TextDisabled("%s   %s", SAWIT_UI_VERSION, u8"云下发已就绪");
        } else if (!cardVerified) {
            ImGui::TextDisabled("%s   %s", SAWIT_UI_VERSION, u8"请登录接收服务器云下发");
        } else {
            const int dots = 1 + (int)(ImGui::GetTime() * 2.0) % 3;
            ImGui::TextDisabled("%s   %s%.*s  %d%%", SAWIT_UI_VERSION,
                                u8"接收云下发文件中", dots, "...",
                                (int)(cloudProgress * 100.0f));
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float label_w = ImGui::GetContentRegionAvail().x * 0.42f;
        auto InfoRow = [&](const char* label, const char* value, const ImVec4& color) {
            ImGui::TextUnformatted(label);
            ImGui::SameLine(label_w);
            ImGui::TextColored(color, "%s", value);
        };

        // 卡密验证是唯一的门槛：通过后才显示功能页，并通过后在这里显示授权信息。
        static std::atomic<bool> verifyBusy{false};
        if (cardVerified) {
            {
                static bool bcCardInfo = false;
                if (!bcCardInfo) { bcCardInfo = true; SMPlatform::SetBreadcrumb("ui:cardinfo"); }
            }
            const ImVec4 okColor(0.35f, 0.78f, 0.45f, 1.0f);
            InfoRow(Tr("Card key", u8"卡密状态"), Tr("Verified", u8"验证通过"), okColor);
            const std::string km = Weiyan::CurrentCard();
            if (!km.empty()) {
                std::string masked = km;
                if (masked.size() > 6)
                    masked = masked.substr(0, 3) + std::string(masked.size() - 6, '*') +
                             masked.substr(masked.size() - 3);
                InfoRow(Tr("Card", u8"当前卡密"), masked.c_str(), ActiveUI::kMuted);
            }
            // 有效期一律取卡密面板返回的那一份（原来这两项读的是旧会话，离线构建里恒为"永久"）
            const std::string expiryText = Weiyan::CardExpiryText();
            const int remainCount = Weiyan::CardRemaining();
            if (!expiryText.empty()) {
                InfoRow(remainCount >= 0 ? Tr("Remaining", u8"剩余次数")
                                         : Tr("Valid until", u8"有效期"),
                        expiryText.c_str(), ActiveUI::kWarn);
            } else {
                InfoRow(Tr("Valid until", u8"有效期"), Tr("Not returned", u8"面板未返回"),
                        ActiveUI::kMuted);
            }
            ImGui::Spacing();
            ImGui::TextWrapped("%s", Weiyan::StatusMessage().c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ActiveUI::kDanger);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.26f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ActiveUI::kOnAccent);
            if (ImGui::Button(Tr("LOGOUT", u8"退出卡密"), ImVec2(-1.0f, ActiveUI::S(40)))) {
                g_instantFarmingActive = false;
                g_instantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
                g_legacyInstantFarmingActive = false;
                g_legacyInstantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
                g_autoFarmActive = false;
                g_autoClearTaskActive = false;
                g_autoQuestTeleport = false;
                g_autoQuestDialogue = false;
                g_autoQuestCutsceneSkip = false;
                g_radarEnabled = false;
                g_radarGeneration.fetch_add(1, std::memory_order_acq_rel);
                { std::lock_guard<std::mutex> lock(g_radarMutex); g_radarList.clear(); }
                { std::lock_guard<std::mutex> lock(invokeMutex); invokeQueue.clear(); }
                std::thread([]() { StopLicenseHeartbeat(); KeluarSesiServer(license_input); }).detach();
                Weiyan::Logout();
                // 免费功能的开关状态跟着会话一起复位（循环线程的条件已经失效）
                g_berryAuto.store(false);
                g_bubbleAuto.store(false);
                g_avatarMode = false;
                ActiveUI::tab_stored = 0;
            }
            ImGui::PopStyleColor();
            // 上面 push 了 Button/ButtonHovered/Text 三个颜色，必须配平，
            // 否则 ImGui 样式栈每帧泄漏 2 个，长时间运行后界面会异常。
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("%s", u8"输入卡密并验证通过后才会显示全部功能");
            ImGui::Spacing();

            ImVec2 field_min = ImGui::GetCursorScreenPos();
            ActiveUI::KeyField(u8"在此输入卡密",
                             license_input, (int) sizeof(license_input),
                             field_min, ImVec2(ImGui::GetContentRegionAvail().x, ActiveUI::S(38)));
            // iOS 上没有 ImGui 的文本输入后端，键盘敲不进这个框。
            // 点一下输入框就弹系统 UIAlertController，用真键盘输入/粘贴。
            if (ImGui::IsItemClicked()) {
                std::string currentKey = license_input;
                SMAlert::ShowInput(u8"输入卡密", u8"输入后点 OK 回填到菜单",
                                   currentKey, [](const std::string &value) {
                    memset(license_input, 0, sizeof(license_input));
                    strncpy(license_input, value.c_str(), sizeof(license_input) - 1);
                    license_input[sizeof(license_input) - 1] = 0;
                });
            }
            ImGui::Dummy(ActiveUI::S2(0, ActiveUI::S(46))); // tinggi field + gap

            const float half = (ImGui::GetContentRegionAvail().x - ActiveUI::S(6)) * 0.5f;
            ImVec2 sign_min = ImGui::GetCursorScreenPos();
            if (ActiveUI::SignInButton(u8"验证卡密", verifyBusy.load(),
                                     sign_min, ImVec2(half, ActiveUI::S(38)))) {
                SMPlatform::SetBreadcrumb("verify:click");
                if (!verifyBusy.exchange(true)) {
                    std::string key = license_input;
                                        std::thread([key]() {
                        SMPlatform::RunWithAutoreleasePool([key]() {
                            SMPlatform::SetBreadcrumb("verify:thread");
                            // 卡密为空时先试上次保存的卡密，和原版输入 y 的行为一致
                            std::string use = key;
                            if (use.empty()) use = Weiyan::LoadSavedCard();
                            if (use.empty()) {
                                Weiyan::SetStatus(u8"请输入卡密");
                                verifyBusy.store(false);
                                return;
                            }
                            if (use != key) {
                                strncpy(license_input, use.c_str(), sizeof(license_input) - 1);
                                license_input[sizeof(license_input) - 1] = 0;
                            }
                            SMPlatform::SetBreadcrumb("verify:start");
                            try {
                                Weiyan::Result r = Weiyan::VerifyCard(use);
                                SMPlatform::SetBreadcrumb(r.ok ? "verify:ok" : "verify:fail");
                                if (r.ok) {
                                    // 卡密通过后把内部会话也装好，功能才能走本地 payload 回放
                                    SMPlatform::SetBreadcrumb("offline:start");
                                    LoginOfflineSession(false);
                                    SMPlatform::SetBreadcrumb("offline:done");
                                    SaveLicenseLocally(use);
                                    SMPlatform::SetBreadcrumb("done");
                                }
                            } catch (...) {
                                Weiyan::SetStatus(u8"验证异常，请重试");
                            }
                            verifyBusy.store(false);
                        });
                    }).detach();
                }
            }
            if (ActiveUI::GhostButton("##paste_key", "Q", u8"粘贴卡密",
                                    sign_min + ImVec2(half + ActiveUI::S(6), 0),
                                    ImVec2(half, ActiveUI::S(38)))) {
                std::string clipboardText = GetAndroidClipboard();
                if (!clipboardText.empty()) {
                    memset(license_input, 0, sizeof(license_input));
                    strncpy(license_input, clipboardText.c_str(), sizeof(license_input) - 1);
                }
            }
            ImGui::Dummy(ActiveUI::S2(0, ActiveUI::S(46))); // tinggi tombol + gap
            ImGui::TextColored(verifyBusy.load() ? ActiveUI::kWarn
                                                 : ActiveUI::kErr,
                               "%s", verifyBusy.load() ? u8"正在验证卡密…"
                                                       : Weiyan::StatusMessage().c_str());
            if (!g_prevBreadcrumb.empty() && g_prevBreadcrumb != "done" &&
                g_prevBreadcrumb != "verify:fail" && g_prevBreadcrumb != "boot") {
                ImGui::TextColored(ActiveUI::kWarn, u8"上次停住: %s", g_prevBreadcrumb.c_str());
            }
            {
                std::string dbg = SMOrientationDebug();
                dbg += " prev=";
                dbg += g_prevBreadcrumb.empty() ? std::string("-") : g_prevBreadcrumb;
                if (!g_prevCrashHead.empty()) {
                    dbg += " crash=";
                    dbg += g_prevCrashHead.substr(0, 160);
                }
                ImGui::TextDisabled("%s", dbg.c_str());
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ActiveUI::kTextInactive);
        ImGui::TextUnformatted(en ? ANDROID_BRAND_FOOTER_EN : ANDROID_BRAND_FOOTER_CN);
        ImGui::PopStyleColor();
    } else if (ActiveUI::tab == 2) {
        // ================= 服务器公告（登录前后都能看） =================
        static std::atomic<bool> noticeBusy{false};
        static std::mutex noticeMutex;
        static std::string noticeText;
        static bool noticeFetched = false;

        // 进这一页就自动拉一次；公告接口不需要卡密，所以未验证也照常拉。
        if (!noticeFetched && !noticeBusy.exchange(true)) {
            noticeFetched = true;
            std::thread([]() {
                std::string text = Weiyan::FetchNotice();
                {
                    std::lock_guard<std::mutex> lk(noticeMutex);
                    noticeText = text;
                }
                noticeBusy.store(false);
            }).detach();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ActiveUI::kAccent);
        ImGui::TextUnformatted(u8"服务器公告");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        const float half = (ImGui::GetContentRegionAvail().x - ActiveUI::S(6)) * 0.5f;
        if (ImGui::Button(u8"刷新公告##notice_refresh", ImVec2(half, ActiveUI::S(34)))) {
            if (!noticeBusy.exchange(true)) {
                std::thread([]() {
                    std::string text = Weiyan::FetchNotice();
                    {
                        std::lock_guard<std::mutex> lk(noticeMutex);
                        noticeText = text;
                    }
                    noticeBusy.store(false);
                }).detach();
            }
        }
        ImGui::SameLine(0.0f, ActiveUI::S(6));
        if (ImGui::Button(u8"复制公告##notice_copy", ImVec2(half, ActiveUI::S(34)))) {
            std::string copy;
            {
                std::lock_guard<std::mutex> lk(noticeMutex);
                copy = noticeText;
            }
            ImGui::SetClipboardText(copy.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (noticeBusy.load()) {
            ImGui::TextDisabled("%s", u8"正在获取公告…");
        } else {
            std::string shown;
            {
                std::lock_guard<std::mutex> lk(noticeMutex);
                shown = noticeText;
            }
            if (shown.empty()) {
                ImGui::TextDisabled("%s", u8"暂无公告（可点刷新重试）");
            } else {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(shown.c_str());
                ImGui::PopTextWrapPos();
            }
        }

        // 公告页底部放交流群入口（原「信息」页的内容并到这里）
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Tr(ANDROID_GROUP_TEXT_EN, ANDROID_GROUP_TEXT_CN));
        ImGui::Spacing();
        if (ImGui::Button(Tr(ANDROID_JOIN_GROUP_LABEL_EN, ANDROID_JOIN_GROUP_LABEL_CN),
                          ImVec2(-1.0f, ActiveUI::S(38))))
            OpenURL(ANDROID_GROUP_URL);
    } else if (ActiveUI::tab == 3) {
        // ================= 设置（界面缩放 + 卡密接口自查） =================
        static float pendingScale = 1.0f;
        static bool settingsInitialized = false;
        if (!settingsInitialized) {
            pendingScale = ActiveUI::user_scale;
            settingsInitialized = true;
        }

        ActiveUI::Section("E", Tr("INTERFACE SETTINGS", u8"界面设置"));
        ImGui::TextWrapped("%s", Tr(
                "Adjust menu size for your screen and touch preference.",
                u8"根据屏幕和触控习惯调整菜单大小。"));
        ImGui::Dummy(ActiveUI::S2(0, 8));

        ImGui::Text("%s: %.0f%%", Tr("UI scale", u8"界面缩放"), pendingScale * 100.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##ui_scale", &pendingScale, 0.25f, 2.00f, "%.2fx",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled("%s", Tr(
                "Range: 25%%-200%%. Recommended: 85%%-100%%.",
                u8"范围：25%%-200%%。建议：85%%-100%%。"));
        ImGui::Dummy(ActiveUI::S2(0, 10));

        if (ImGui::Button(Tr("APPLY UI SIZE", u8"应用界面大小"),
                          ImVec2(-1.0f, ActiveUI::S(44.f)))) {
            const float requested = ImClamp(pendingScale, 0.25f, 2.00f);
            SaveUiScale(requested);
            g_RequestedUiScale.store(requested, std::memory_order_release);
        }

        if (ImGui::Button(Tr("RESET TO DEFAULT", u8"恢复默认"),
                          ImVec2(-1.0f, ActiveUI::S(40.f)))) {
            pendingScale = 1.0f;
            SaveUiScale(1.0f);
            g_RequestedUiScale.store(1.0f, std::memory_order_release);
        }

        ImGui::Dummy(ActiveUI::S2(0, 8));
        ImGui::TextDisabled("%s %.2f | %s %.2f", Tr("Device", u8"设备"),
                            ActiveUI::device_dpi, Tr("Current", u8"当前"), ActiveUI::dpi);

    } else if (!cardVerified) {
        // 功能页（未验证）：免费功能始终可用，会员功能给提示
        DrawFreeFeatures();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ActiveUI::kAccent);
        ImGui::TextUnformatted(u8"会员功能未解锁");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", u8"请先在「主页」输入卡密并验证通过，验证成功后这里会显示全部功能。");
        ImGui::Spacing();
        ImGui::TextDisabled("%s", Weiyan::StatusMessage().c_str());
        ImGui::Spacing();
        if (ImGui::Button(u8"去输入卡密", ImVec2(-1.0f, ActiveUI::S(38)))) {
            ActiveUI::tab_stored = 0;
        }
    } else if (g_avatarMode && sesiOtentik) {
        // ==========================================
        // MENU AVATAR (setelah login lewat tab AVATAR)
        //   1. Import / Add Picture
        //   2. Import As Avatar
        //   + tombol Logout
        // ==========================================
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 15));
        ImGui::PushStyleColor(ImGuiCol_Button, ActiveUI::kDanger);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.26f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ActiveUI::kOnAccent);
        if (ImGui::Button(Tr("Logout", u8"登出"), ImVec2(-1, 60))) {
            g_instantFarmingActive = false;
            g_instantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
            g_legacyInstantFarmingActive = false;
            g_legacyInstantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
            g_autoFarmActive = false;
            g_autoClearTaskActive = false;
            g_autoQuestTeleport = false;
            g_autoQuestDialogue = false;
            g_autoQuestCutsceneSkip = false;
            g_radarEnabled = false;
            g_radarGeneration.fetch_add(1, std::memory_order_acq_rel);
            { std::lock_guard<std::mutex> lock(g_radarMutex); g_radarList.clear(); }
            { std::lock_guard<std::mutex> lock(invokeMutex); invokeQueue.clear(); }
            std::thread([]() { StopLicenseHeartbeat(); KeluarSesiServer(license_input); }).detach();
            g_avatarMode = false;
            login_tab = 1;
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::BeginChild("##avatar_scroll", ImVec2(0, 0), true);
        ActiveUI::Section("G", Tr("AVATAR", u8"头像 (AVATAR)"));
        ImGui::TextColored(LisensiPermanen()
                ? ActiveUI::kOk
                : ActiveUI::kWarn,
            "%s", LisensiPermanen()
                ? Tr("License: Permanent (unlimited use)", u8"许可证：永久（无限使用）")
                : Tr("License: One use per feature", u8"许可证：每个功能仅限一次"));
        ImGui::Spacing();

        // Folder gambar: /Android/data/<pkg>/files/Photos/
        if (ImGui::Button(Tr("Refresh Photo Folder", u8"刷新图片文件夹"), ImVec2(-1, 55)))
            ScanPhotoFolder();
        ImGui::Spacing();
        if (g_photoFiles.empty()) {
            ImGui::TextColored(ActiveUI::kWarn,
                Tr("No image in /files/Photos/. Put .jpg/.png then Refresh.",
                   u8"/files/Photos/ 没有图片。放入 .jpg/.png 后点击刷新。"));
        } else {
            if (g_photoSelectedIdx >= (int) g_photoFiles.size()) g_photoSelectedIdx = 0;
            if (ImGui::BeginCombo(Tr("Image", u8"图片"), g_photoFiles[g_photoSelectedIdx].c_str())) {
                for (int i = 0; i < (int) g_photoFiles.size(); i++) {
                    bool isSel = (g_photoSelectedIdx == i);
                    if (ImGui::Selectable(g_photoFiles[i].c_str(), isSel)) g_photoSelectedIdx = i;
                    if (isSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Spacing();

        // 1. Import / Add Picture
        if (ImGui::Button(Tr("1. Import / Add Picture", u8"1. 导入/添加图片"), ImVec2(-1, 60))) {
            if (!g_photoFiles.empty()) {
                std::string full = g_photoFolderPath + g_photoFiles[g_photoSelectedIdx];
                std::thread(ImportPhotoToGameNative, full).detach();
            }
        }
        ImGui::Spacing();

        // 2. Import As Avatar
        if (ImGui::Button(Tr("2. Import As Avatar", u8"2. 导入为头像"), ImVec2(-1, 60))) {
            if (!g_photoFiles.empty()) {
                std::string full = g_photoFolderPath + g_photoFiles[g_photoSelectedIdx];
                std::thread(ImportPhotoAsAvatarNative, full).detach();
            }
        }
        ImGui::Spacing();
        ImGui::TextColored(ActiveUI::kInfo, Tr("Status: ", u8"状态："));
        ImGui::SameLine();
        ImGui::Text("%s", g_photoStatus.c_str());

        ImGui::EndChild();
    } else if (sesiOtentik && !g_avatarMode) {
        // 会员功能页：卡密已通过，不再显示免费功能，直接给完整列表
        int& tab = product_tab;
        ImVec2 body_avail = ImGui::GetContentRegionAvail();
        char scrollAreaId[48];
        if (tab == 2)
            snprintf(scrollAreaId, sizeof(scrollAreaId), "ScrollArea_Game_%d", game_sub);
        else
            snprintf(scrollAreaId, sizeof(scrollAreaId), "ScrollArea_Tab_%d", tab);
        ImGui::BeginChild(scrollAreaId, body_avail, true);

        // Navigasi sub-tab Game berada di atas konten. Grid menghitung jumlah
        // kolom dari lebar yang tersedia dan otomatis membungkus ke baris baru.
        if (tab == 2)
            ActiveUI::DrawGameSubGrid(&game_sub, is_english);

        // Setiap sub-tab memiliki posisi scroll sendiri dan selalu mulai dari atas.
        // Ini mencegah offset scroll besar dari halaman panjang terbawa ke Weather.
        static int previousGameSub = -1;
        if (tab == 2 && previousGameSub != game_sub) {
            ImGui::SetScrollY(0.0f);
            previousGameSub = game_sub;
        }

        if (tab == 2) {
            // Weather dirender pada blok terpisah di bawah. Jangan membuat group
            // kosong untuk sub-tab ini karena bounding item group dapat ikut
            // memperbesar rentang scroll child tanpa konten yang nyata.
            const bool hasGroupedGameContent = (game_sub >= 0 && game_sub <= 4);
            if (hasGroupedGameContent) {
                ImGui::BeginGroup();
                ImGui::Spacing();
            }

            if (game_sub == 0) {
            ActiveUI::Section("G", Tr("Pet (Mine)", u8"宠物（我的）"));
            if (ImGui::Button(Tr("Scan Nearby Pets", u8"扫描附近宠物")))
                std::thread([]() { ScanNearbyPets(); }).detach();
            ImGui::SameLine(); ImGui::Text("(%d)", (int) g_myPets.size());

            {
                std::vector<std::string> petLabels;
                for (size_t i = 0; i < g_myPets.size(); i++) {
                    auto &mp = g_myPets[i];
                    std::string nm = mp.name.empty() ? (mp.isDog ? "Dog" : "Cat") : mp.name;
                    petLabels.push_back(std::to_string(i + 1) + ". " + nm + (mp.isDog ? " [Dog]" : " [Cat]"));
                }
                std::vector<const char *> petItems;
                for (auto &s : petLabels) petItems.push_back(s.c_str());
                if (!petItems.empty())
                    ImGui::Combo(Tr("Select Pet", u8"选择宠物"), &g_selectedPetIndex, petItems.data(), (int) petItems.size());
                else
                    ImGui::TextDisabled(Tr("No pets. Press Scan.", u8"暂无宠物，请扫描"));
            }

            const char *petFoodModes[] = {
                Tr("Auto (by type)", u8"自动（按类型）"),
                Tr("Universal", u8"通用"),
                Tr("Dog Only", u8"仅狗粮"),
                Tr("Cat Only", u8"仅猫粮")
            };
            ImGui::Combo(Tr("Food Type", u8"食物类型"), &g_petFoodMode, petFoodModes, 4);

            ImGui::SliderInt(Tr("Feed Qty", u8"喂食数量"), &g_petFeedQty, 1, 200);
            if (g_petFeedQty < 1)   g_petFeedQty = 1;
            if (g_petFeedQty > 200) g_petFeedQty = 200;

            if (ImGui::Button(Tr("Feed", u8"喂食"), ImVec2(190, 60)))
                std::thread([]() { FeedSelectedPetOnce(); }).detach();
            ImGui::SameLine();
            if (ImGui::Button(Tr("Interact", u8"互动"), ImVec2(190, 60)))
                std::thread([]() { InteractSelectedPetOnce(); }).detach();
            ImGui::SameLine();
            if (ImGui::Button(Tr("Bathe", u8"洗澡"), ImVec2(190, 60)))
                std::thread([]() { BathePetOnce(); }).detach();
            }

            if (game_sub == 4) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ActiveUI::Section("J", Tr("Auto Puzzle", u8"自动拼图"));
            ImGui::Separator();
            if (ImGui::Checkbox(Tr("Auto Puzzle", u8"自动拼图"), &g_isPuzzleBotRunning)) {
                if (g_isPuzzleBotRunning) {
                    std::thread([]() {
                        g_puzzlePn = FetchPayloadSync(WireForOp(OP_JIGSAW_PUZZLE)).payloadRaw;
                        if (g_puzzlePn.empty()) { g_isPuzzleBotRunning = false; g_botStatus = (const char*)OBFUSCATE("PUZZLE: PAYLOAD KOSONG (server?)"); return; }
                        AutoPuzzleBotThread();
                    }).detach();
                }
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(g_botStatus.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextDisabled("%s", SMAntiBanStatus().c_str());
            {
                // 侧载 iOS 没有 hook，帧泵由主线程每帧轮询驱动；加载阶段跑会卡死，
                // 所以默认关闭，让玩家进到世界以后手动打开。
                bool pumpArmed = g_framePumpArmed;
                if (ImGui::Checkbox(u8"启用每帧功能（默认 90 秒后自动开启）##pump", &pumpArmed)) {
                    g_framePumpArmed = pumpArmed;
                }
                bool antiBanArmed = SMAntiBanArmed();
                if (ImGui::Checkbox(u8"启用防封拦截（默认 110 秒后自动开启）##antiban", &antiBanArmed)) {
                    if (antiBanArmed) SMAntiBanArm();
                }
            }
        if (ImGui::Checkbox(OBFUSCATE(u8"反作弊模式 - ANTI-CHEAT"), &g_isAntiCheatActive)) {
                if (g_isAntiCheatActive) {
                    // iOS 版：托管层 Themis/上报拦截（幂等），原生 RVA 路子只作兜底。
                    SMAntiBanInstall();
                    std::thread(PatchAntiCheatNative).detach();
                }
            }
            ImGui::Spacing();
            if (ImGui::Checkbox(Tr("Anti-AFK Mode", u8"防挂机模式"), &g_isAntiAfkActive)) {
                if (g_isAntiAfkActive) {
                    std::thread(AntiAfkRoutine).detach();
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            }

            if (game_sub == 3) {
            ImGui::Spacing();
            ActiveUI::Section("E", Tr("Player Move Speed", u8"玩家移动速度"));
            if (ImGui::Checkbox(Tr("Enable Speed", u8"启用加速"), &g_speedToggle)) {
                if (g_speedToggle) {
                    std::thread([]() { FetchAndExecute(WireForOp(OP_GET_SELF_PLAYER)); })
                            .detach();
                } else {
                    if (!g_cachedPlayerPtrHex.empty()) {
                        std::string inst = g_cachedPlayerPtrHex;
                        std::thread([inst]() {
                            FetchAndExecute(WireForOp(OP_SET_MOVE_SPEED), OBFUSCATE("1f"), inst);
                        })
                                .detach();
                    }
                    g_speedActive = false;
                    g_cachedPlayerPtrHex = "";
                    g_tempSliderInt = 1;
                }
            }

            if (g_speedToggle) {
                ImGui::Spacing();
                ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.5f);
                ImGui::SliderInt(OBFUSCATE("##speedslider"), &g_tempSliderInt, 1, 10);
                ImGui::SameLine();
                ImGui::Dummy(ImVec2(10.0f, 0.0f));
                ImGui::SameLine();

                if (g_speedActive) {
                    if (ImGui::Button(Tr("Set", u8"设置"), ImVec2(150, 0))) {
                        std::string prm =
                                std::to_string(g_tempSliderInt) + (const char *) OBFUSCATE("f");
                        std::string inst = g_cachedPlayerPtrHex;
                        std::thread([prm, inst]() {
                            FetchAndExecute(WireForOp(OP_SET_MOVE_SPEED), prm, inst);
                        })
                                .detach();
                    }
                } else {
                    ImGui::Button(Tr("Wait..", u8"等待.."), ImVec2(150, 0));
                }
            }
            ActiveUI::Section("N", Tr("Player Jump", u8"玩家跳跃"));
            if (ImGui::Checkbox(Tr("Enable High Jump", u8"启用高跳"), &g_jumpHackActive)) {
                if (g_jumpHackActive) {
                    PatchJumpHeightNative(g_jumpHeightSlider);
                } else {
                    PatchJumpHeightNative(1.3f);
                }
            }
            if (g_jumpHackActive) {
                ImGui::Indent(10.0f);
                ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.5f);
                if (ImGui::SliderFloat(OBFUSCATE("##jumpheight"), &g_jumpHeightSlider, 1.3f, 40.0f,
                                       Tr("Height: %.1f", u8"高度: %.1f"))) {
                    PatchJumpHeightNative(g_jumpHeightSlider);
                }
                ImGui::Unindent(10.0f);
            }

            ActiveUI::Section("C", Tr("Global Speedhack", u8"全局时间加速"));
            if (ImGui::Checkbox(Tr("Enable Global Speed", u8"启用全局加速"), &g_globalSpeedToggle)) {
                if (!g_globalSpeedToggle) {
                    g_targetSpeed = 1.0f;
                    g_speedHackNeedsUpdate = true;
                    g_globalSpeedValue = 1;
                }
            }
            if (g_globalSpeedToggle) {
                ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.5f);
                ImGui::SliderInt(OBFUSCATE("##globalspeed"), &g_globalSpeedValue, 1, 10);
                ImGui::SameLine();
                ImGui::Dummy(ImVec2(10.0f, 0.0f));
                ImGui::SameLine();
                if (ImGui::Button(Tr("Set", u8"设置"), ImVec2(180, 0))) {
                    g_targetSpeed = (float) g_globalSpeedValue;
                    g_speedHackNeedsUpdate = true;
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            }

            if (game_sub == 2) {
            ImGui::Spacing();
            ActiveUI::Section("O", Tr("Instant Resource Farming", u8"瞬间资源采集"));
            ImGui::Text(Tr("Select resources to collect instantly:", u8"选择要瞬间采集的资源:"));
            if (ImGui::BeginListBox(OBFUSCATE("##instant_resource_list"), ImVec2(-1, 360))) {
                // 选中态：深绿底 + 暖白字 + "✓"前缀。
                // 原来选中用的是主题的 Header/Hover（焦糖 #9E6830 叠在米色上，字又是深灰），
                // 看起来就是一块"深褐色"，在米色面板里既不明显也不好读。
                static const ImVec4 kFarmSelBg = ImVec4(0.118f, 0.463f, 0.235f, 1.00f);
                static const ImVec4 kFarmSelHover = ImVec4(0.157f, 0.553f, 0.294f, 1.00f);
                static const ImVec4 kFarmSelActive = ImVec4(0.086f, 0.376f, 0.192f, 1.00f);
                static const ImVec4 kFarmSelText = ImVec4(1.000f, 0.988f, 0.965f, 1.00f);
                for (int i = 0; i < NUM_RESOURCES; i++) {
                    const bool selected = g_farmSelections[i];
                    char rowLabel[192];
                    snprintf(rowLabel, sizeof(rowLabel), "%s  %s",
                             selected ? u8"✓" : u8"·", Tr(farmNames[i], farmNamesZh[i]));
                    if (selected) {
                        ImGui::PushStyleColor(ImGuiCol_Header, kFarmSelBg);
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kFarmSelHover);
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, kFarmSelActive);
                        ImGui::PushStyleColor(ImGuiCol_Text, kFarmSelText);
                    }
                    if (ImGui::Selectable(rowLabel, selected,
                                          ImGuiSelectableFlags_DontClosePopups, ImVec2(0, 50))) {
                        g_farmSelections[i] = !g_farmSelections[i];
                    }
                    if (selected) ImGui::PopStyleColor(4);
                }
                ImGui::EndListBox();
            }
            ImGui::Spacing();

            float pickupCooldown = g_instantFarmPickupCooldown.load(std::memory_order_relaxed);
            if (ImGui::SliderFloat(
                    Tr("Pickup Cooldown", u8"采集冷却"),
                    &pickupCooldown, 0.3f, 5.0f, "%.2f s")) {
                g_instantFarmPickupCooldown.store(pickupCooldown, std::memory_order_relaxed);
            }
            float nextTargetCooldown = g_instantFarmNextTargetCooldown.load(std::memory_order_relaxed);
            if (ImGui::SliderFloat(
                    Tr("Next Target Cooldown", u8"下一目标冷却"),
                    &nextTargetCooldown, 0.1f, 5.0f, "%.1f s")) {
                g_instantFarmNextTargetCooldown.store(nextTargetCooldown, std::memory_order_relaxed);
            }
            ImGui::Spacing();

            if (g_instantFarmingActive) {
                if (ImGui::Button(Tr("STOP INSTANT RESOURCE FARM", u8"停止瞬间资源采集"), ImVec2(-1, 70))) {
                    g_instantFarmingActive = false;
                    g_instantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
                }
            } else if (ImGui::Button(Tr("START INSTANT RESOURCE FARM", u8"开始瞬间资源采集"), ImVec2(-1, 70))) {
                bool selected = false;
                for (int i = 0; i < NUM_RESOURCES; ++i) selected = selected || g_farmSelections[i];
                if (selected && SesiAktif()) {
                    g_instantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
                    g_instantFarmingActive = true;
                    std::thread([]() {
                        if (!SesiAktif()) {
                            g_instantFarmingActive = false;
                            return;
                        }
                        InvokeRequest req = FetchPayloadSync(WireForOp(OP_INSTANT_FARM_TRIGGER));
                        if (req.dllName.empty() || req.nameSpace.empty() || req.className.empty()) {
                            g_instantFarmingActive = false;
                            return;
                        }
                        ExecuteInstantFarmThread(req.dllName, req.nameSpace, req.className);
                    }).detach();
                }
            }
            ImGui::TextColored(ActiveUI::kMuted, Tr("Scans the selected resource identity and moves to another spawn point when empty.", u8"识别所选资源；当前区域为空时自动前往下一个刷新点。"));
            ImGui::Spacing();
            ImGui::Separator();
            }

            if (game_sub == 0) {
            ImGui::Spacing();
            ActiveUI::Section("Q", Tr("Master Chef Mode", u8"厨神模式"));
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button(Tr("Scan Nearby Stoves", u8"扫描附近炉子"), ImVec2(-1, 70))) {
                g_selectedCookerStaticId = -1;
                g_availableRecipes.clear();
                g_selectedRecipeIdx = -1;
                std::thread([]() {
                    g_cookPn = FetchPayloadSync(WireForOp(OP_AUTO_COOK)).payloadRaw;
                    if (g_cookPn.empty()) return;
                    ScanCookersNative();
                }).detach();
            }
            ImGui::Spacing();

            std::vector<int> uniqueCookerTypes;
            for (auto &c: g_availableCookers) {
                if (std::find(uniqueCookerTypes.begin(), uniqueCookerTypes.end(), c.staticId) ==
                    uniqueCookerTypes.end()) {
                    uniqueCookerTypes.push_back(c.staticId);
                }
            }

            if (!uniqueCookerTypes.empty()) {
                ImGui::TextColored(ActiveUI::kWarn, Tr("Select active stove type:", u8"选择活跃的炉子类型:"));

                std::string currentComboLabel = Tr("Select stove model...", u8"选择炉子型号...");
                if (g_selectedCookerStaticId != -1) {
                    for (auto &c: g_availableCookers) {
                        if (c.staticId == g_selectedCookerStaticId) {
                            currentComboLabel = GetCookerNameByStaticId(c.staticId);
                            break;
                        }
                    }
                }

                if (ImGui::BeginCombo(OBFUSCATE("##stoveCombo"), currentComboLabel.c_str())) {
                    for (int typeId: uniqueCookerTypes) {
                        std::string displayName = Tr("Unknown Model", u8"未知型号");

                        for (auto &c: g_availableCookers) {
                            if (c.staticId == typeId) {
                                displayName = GetCookerNameByStaticId(c.staticId);
                                break;
                            }
                        }

                        bool isSelected = (g_selectedCookerStaticId == typeId);
                        if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                            g_selectedCookerStaticId = typeId;
                            g_availableRecipes.clear();
                            g_selectedRecipeIdx = -1;
                            GetRecipesNative(g_selectedCookerStaticId);
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextColored(ActiveUI::kMuted,
                                   Tr("No stoves found. Tap 'Scan Nearby Stoves' first.", u8"未扫描到炉子。请先点击 '扫描附近炉子'。"));
            }

            ImGui::Spacing();
            ImGui::Text(Tr("Total stove entities: %zu | Available recipes: %zu", u8"总炉子实体: %zu | 可用食谱: %zu"), g_availableCookers.size(),
                        g_availableRecipes.size());
            ImGui::Spacing();

            if (!g_availableRecipes.empty()) {
                ImGui::TextColored(ActiveUI::kWarn, Tr("Select recipe to cook:", u8"选择要烹饪的食谱:"));
                ImGui::BeginChild("##recipe_box", ImVec2(0, 300), true);

                for (int i = 0; i < g_availableRecipes.size(); i++) {
                    bool isSelected = (g_selectedRecipeIdx == i);
                    if (ImGui::Selectable(g_availableRecipes[i].name.c_str(), isSelected, 0,
                                          ImVec2(0, 60))) {
                        g_selectedRecipeIdx = i;
                    }
                }
                ImGui::EndChild();
            } else {
                if (g_selectedCookerStaticId != -1) {
                    ImGui::TextColored(ActiveUI::kMuted, Tr("Loading/No recipes found for this model...", u8"加载中/未找到此型号的食谱..."));
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Checkbox(Tr("Start Auto Cook", u8"开始自动烹饪"), &g_isChefActive)) {
                if (g_isChefActive) {
                    if (g_availableCookers.empty() || g_selectedRecipeIdx == -1 ||
                        g_selectedCookerStaticId == -1) {
                        g_isChefActive = false;
                    } else {
                        int recipeId = g_availableRecipes[g_selectedRecipeIdx].staticId;
                        // AutoCook dan Helper tidak boleh mengirim aksi bersamaan.
                        g_isHelperCookActive = false;
                        std::thread([recipeId]() {
                            g_cookPn = FetchPayloadSync(WireForOp(OP_AUTO_COOK)).payloadRaw;
                            if (g_cookPn.empty()) {
                                g_isChefActive = false;
                                return;
                            }
                            ScanCookersNative();
                            AutoCookBatchRoutine(recipeId);
                        }).detach();
                    }
                }
            }

            ImGui::Spacing();
            if (ImGui::Checkbox(Tr("START/STOP FAILED COOKING", u8"取消/开始失败烹饪"), &g_isFailedCookActive)) {
                if (g_isFailedCookActive) {
                    std::thread([]() {
                        g_cookPn = FetchPayloadSync(WireForOp(OP_AUTO_COOK)).payloadRaw;
                        if (g_cookPn.empty()) { g_isFailedCookActive = false; return; }
                        ScanCookersNative();
                        FailedCookRoutine();
                    }).detach();
                }
            }

            ImGui::Spacing();
            if (ImGui::Checkbox(Tr("START/STOP HELPER COOKING", u8"帮手烹饪"), &g_isHelperCookActive)) {
                if (g_isHelperCookActive) {
                    // Helper mengambil alih pemantauan agar tidak terjadi aksi ganda.
                    g_isChefActive = false;
                    std::thread([]() {
                        g_cookPn = FetchPayloadSync(WireForOp(OP_COOK_HELPER)).payloadRaw;
                        if (g_cookPn.empty()) { g_isHelperCookActive = false; return; }
                        ScanCookersNative();
                        HelperCookRoutine();
                    }).detach();
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            }

            if (game_sub == 1) {
            ImGui::Spacing();
            ActiveUI::Section("P", Tr("FISHING", u8"钓鱼"));
            ImGui::Separator();
            if (ImGui::Checkbox(Tr("Instant Fishing (Auto Catch)", u8"立即钓鱼 (自动捕获)"), &isInstantFishing)) {
                if (isInstantFishing) {
                    if (!SesiAktif() || WireForOp(OP_SETUP_FISHING).empty()) {
                        isInstantFishing = false;
                    } else {
                        std::thread([](){ ApplyFishingHackNative(); }).detach();
                    }
                } else {
                    std::thread([](){ ApplyFishingHackNative(); }).detach();
                }
            }

            ImGui::TextDisabled(Tr("Throw Max Distance: %.1fm", u8"投掷最大距离: %.1f米"), g_fishThrowDist);
            if (ImGui::SliderFloat("##fishDist", &g_fishThrowDist, 1.0f, 20.0f)) {
                std::thread([](){ ApplyFishingHackNative(); }).detach();
            }
            // ================= AUTO FISHING (VIP) =================
            ImGui::Spacing();
            ActiveUI::Section("P", Tr("AUTO FISHING", u8"自动钓鱼"));
            ImGui::Separator();
            if (ImGui::Checkbox(Tr("Auto Fishing", u8"自动钓鱼"), &g_autoFishingActive)) {
                if (g_autoFishingActive) {
                    if (!SesiAktif() || WireForOp(OP_AUTO_FISHING).empty()) {
                        g_autoFishingActive = false;
                        g_autoFishStatus = u8"离线 / 未授权";
                    } else {
                        const uint32_t generation = g_autoFishingGeneration.fetch_add(
                                1, std::memory_order_acq_rel) + 1;
                        std::thread(AutoFishingLoop, generation).detach();
                    }
                }
            }
            ImGui::SliderFloat(Tr("Radius Scan", u8"扫描半径"), &g_autoFishRadius, 5.0f, 100.0f, "%.0f");
            ImGui::SliderFloat(Tr("Fish Distance (m)", u8"与鱼的距离 (米)"), &g_autoFishGap, 1.0f, 20.0f, "%.0f");
            ImGui::SliderInt(Tr("Timeout (tick)", u8"咬钩超时 (tick)"), &g_autoFishBiteTimeoutTicks, 15, 150);
            ImGui::Checkbox(Tr("Teleport To Fish", u8"传送到鱼"), &g_autoFishTeleport);
            ImGui::Checkbox(Tr("Hover Between Casts", u8"投竿间保持悬浮"),
                &g_autoFishHoverBetweenCasts);
            ImGui::TextDisabled("%s", Tr(
                "Keeps vertical movement frozen while Auto Fishing is active.",
                u8"自动钓鱼期间锁定垂直移动。"));
            ImGui::Checkbox(Tr("Fast Cast Animation (10x + CrossFade)", u8"快速投竿动画"), &g_autoFishSkipCastAnim);
            ImGui::Checkbox(Tr("Skip Catch Animation (TakeUpRod)", u8"跳过捕获动画"), &g_autoFishSkipCatchAnim);
            if (g_autoFishingActive) {
                ImGui::TextColored(ActiveUI::kOk, u8"状态: %s", g_autoFishStatus.c_str());
                ImGui::TextColored(ActiveUI::kInfo, u8"目标: %s", g_autoFishTargetStatus.c_str());
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), u8"本次钓获: %d", g_autoFishSessionCatches);
            }

            ImGui::Spacing();
            ActiveUI::Section("L", Tr("FISHING LURE BALL", u8"鱼类诱饵球"));
            ImGui::Separator();
            ImGui::SliderInt(Tr("Lure Cooldown (seconds)", u8"诱饵冷却（秒）"),
                             &g_lureBallCooldownSec, 10, 120);

            const uint64_t lureNow = (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            const uint64_t lureUntil = g_lureBallCooldownUntilMs.load(std::memory_order_acquire);
            const int lureRemaining = lureUntil > lureNow
                                      ? (int) ((lureUntil - lureNow + 999ULL) / 1000ULL) : 0;
            const bool lureBusy = g_lureBallBusy.load(std::memory_order_acquire);
            const bool wasAutoLure = g_autoLureBallActive;
            if (ImGui::Checkbox(Tr("Auto Throw Lure Ball", u8"自动投掷诱饵球"),
                                &g_autoLureBallActive)) {
                if (g_autoLureBallActive && !wasAutoLure) {
                    g_lureBallCooldownUntilMs.store(0, std::memory_order_release);
                    const uint32_t generation = g_lureBallLoopGeneration.fetch_add(
                            1, std::memory_order_acq_rel) + 1;
                    std::thread(AutoLureBallLoop, generation).detach();
                } else if (!g_autoLureBallActive) {
                    g_lureBallLoopGeneration.fetch_add(1, std::memory_order_acq_rel);
                    std::lock_guard<std::mutex> lock(g_lureBallMutex);
                    g_lureBallStatus = u8"自动打窝球已停止";
                }
            }
            ImGui::TextDisabled("%s", lureBusy
                                      ? Tr("Checking and throwing...", u8"正在检查并投掷...")
                                      : (g_autoLureBallActive
                                         ? Tr("Automatic repeat is active.", u8"自动重复已启用。")
                                         : Tr("Automatic repeat is off.", u8"自动重复已关闭。")));
            {
                std::lock_guard<std::mutex> lock(g_lureBallMutex);
                ImGui::TextDisabled("%s %s", Tr("Status:", u8"状态："), g_lureBallStatus.c_str());
                ImGui::TextDisabled("%s %d", Tr("Last known amount:", u8"上次已知数量："),
                                    g_lureBallCount);
            }
            if (lureRemaining > 0)
                ImGui::TextColored(ImVec4(1.00f, 0.72f, 0.20f, 1.00f), "%s %d s",
                                   Tr("Cooldown remaining:", u8"剩余冷却："), lureRemaining);
            ImGui::TextDisabled("%s", Tr(
                    "Uses a real Lure Ball from your Backpack. Face a valid fishing area before throwing.",
                    u8"使用背包中真实的诱饵球。投掷前请面向有效钓鱼区域。"));

            ImGui::Spacing();
            ImGui::Separator();
            ActiveUI::Section("B", Tr("AUTO THROW FISH BAIT", u8"自动投掷鱼饵"));
            ImGui::SliderInt(Tr("Throw Speed (ms)", u8"投掷速度（毫秒）"),
                             &g_fishBaitIntervalMs, 20, 2000);

            const bool wasAutoFishBait = g_autoFishBaitActive;
            if (ImGui::Checkbox(Tr("Auto Throw Fish Bait", u8"自动投掷鱼饵"),
                                &g_autoFishBaitActive)) {
                if (g_autoFishBaitActive && !wasAutoFishBait) {
                    {
                        std::lock_guard<std::mutex> lock(g_fishBaitMutex);
                        g_fishBaitEmpty = false;
                        g_fishBaitStatus = u8"正在检查背包中的鱼饵…";
                        g_fishBaitStatusZh = u8"正在检查背包中的鱼饵...";
                    }
                    const uint32_t baitGen = g_fishBaitLoopGeneration.fetch_add(
                            1, std::memory_order_acq_rel) + 1;
                    std::thread(AutoFishBaitLoop, baitGen).detach();
                } else if (!g_autoFishBaitActive) {
                    g_fishBaitLoopGeneration.fetch_add(1, std::memory_order_acq_rel);
                    std::lock_guard<std::mutex> lock(g_fishBaitMutex);
                    g_fishBaitStatus = u8"自动抛鱼饵已停止";
                    g_fishBaitStatusZh = u8"自动投掷鱼饵已停止";
                }
            }

            {
                std::lock_guard<std::mutex> lock(g_fishBaitMutex);
                if (g_fishBaitEmpty) {
                    ImGui::TextColored(ImVec4(1.00f, 0.35f, 0.35f, 1.00f), "%s",
                                       Tr("[!] Empty / Not Found: Fish Bait (ID 20511) is not in your bag!",
                                          u8"[!] 未找到 / 已用完：背包中没有鱼饵 (ID 20511)！"));
                }
                ImGui::TextDisabled("%s %s", Tr("Status:", u8"状态："),
                                    Tr(g_fishBaitStatus.c_str(), g_fishBaitStatusZh.c_str()));
                ImGui::TextDisabled(
                        Tr("Current Stack: %d | Total in Bag: %d (%d stacks)",
                           u8"当前组数量: %d | 背包总数: %d (%d 组)"),
                        g_fishBaitCurrentStackCount,
                        g_fishBaitCount,
                        g_fishBaitStackCount);
            }
            ImGui::TextDisabled("%s", Tr(
                    "Throws Fish Bait (ID 20511) continuously to spawn fish instantly. Automatically switches stacks (max 200/stack).",
                    u8"连续投掷鱼饵 (ID 20511) 立即刷出鱼。当前组用完后自动重新检查背包切换下一组（每组最多200个）。"));

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ActiveUI::Section("A", Tr("Bug Catching", u8"捕虫"));
            if (ImGui::Checkbox(Tr("Auto Catch Bugs", u8"自动捕虫"), &g_testAutoCatch)) {
                if (g_testAutoCatch) {
                    std::thread(AutoCatchLoopThread).detach();
                }
            }
            if (g_testAutoCatch) {
                ImGui::Indent(10.0f);
                ImGui::PushItemWidth(-120.0f);
                ImGui::SliderInt(Tr("Max Catch", u8"最大捕捉量"), &g_insectCatchLimit, 1, 10);
                ImGui::PopItemWidth();
                ImGui::Unindent(10.0f);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ActiveUI::Section("R", Tr("Bird Scanner", u8"鸟类扫描仪"));
            ImGui::Text(Tr("Bird Scanner Farm", u8"鸟类扫描农场"));
            std::string birdStatus = g_enableBirdFarm
                                     ? Tr("[>] ACTIVE", u8"[>] 运行中")
                                     : Tr("[.] STANDBY", u8"[.] 待机");
            bool prevBirdFarm = g_enableBirdFarm;
            if (ImGui::Checkbox(birdStatus.c_str(), &g_enableBirdFarm)) {
                g_birdCurrentTarget = {};
                g_birdRequestPending.store(false, std::memory_order_release);
                g_birdRetryCount = 0;
                g_birdNextActionAt = 0;
                if (g_enableBirdFarm && !prevBirdFarm) {
                    g_enableBirdFarm = false;
                    g_birdFarmStatus = u8"正在授权";
                    std::thread([] {
                        InvokeRequest req = FetchPayloadSync(WireForOp(OP_BIRD_WATCHING));
                        if (req.dllName.empty() || req.methodDesc.empty()) {
                            g_birdFarmStatus = u8"授权失败";
                            return;
                        }
                        std::lock_guard<std::mutex> lock(invokeMutex);
                        invokeQueue.push_back(std::move(req));
                    }).detach();
                } else {
                    g_birdFarmStatus = u8"待机";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Reset History", u8"重置历史"))) {
                ResetBirdHistory();
            }
            ImGui::TextDisabled(u8"状态: %s | 成功: %d | 完成: %d",
                                g_birdFarmStatus.c_str(), g_birdSuccessCount,
                                (int) g_processedBirds.size());
            ImGui::TextDisabled(Tr("Rescan Cooldown: %.1fs", u8"重新扫描冷却: %.1f秒"), g_birdCatchCooldown);
            ImGui::SliderFloat("##birdCool", &g_birdCatchCooldown, 0.2f, 10.0f);
            ImGui::TextDisabled(Tr("Range: %.0fm", u8"范围: %.0f米"), g_birdScanRange);
            ImGui::SliderFloat("##birdRange", &g_birdScanRange, 5.0f, 100.0f);
            ImGui::TextDisabled(Tr("Photo Cooldown: %.1fs", u8"拍照冷却: %.1f秒"), g_birdInvokeCooldown);
            ImGui::SliderFloat("##birdDelay", &g_birdInvokeCooldown, 0.1f, 10.0f);

            ImGui::Spacing();
            ImGui::Separator();
            }

            if (game_sub == 2) {
            ImGui::Spacing();
            ActiveUI::Section("O", Tr("Auto Plant Bot", u8"自动种植机器人"));
            ImGui::Spacing();
            const bool seedLoading = g_seedListLoading.load(std::memory_order_acquire);
            if (seedLoading) ImGui::BeginDisabled();
            if (ImGui::Button(seedLoading
                    ? Tr("Loading Seeds...", u8"正在加载种子...")
                    : Tr("Get All Seeds", u8"获取全部种子"), ImVec2(-1, 55))) {
                RunOnUnityThreadAsync([]() { LoadAllCropseeds(); });
            }
            if (seedLoading) ImGui::EndDisabled();

            {
                std::lock_guard<std::mutex> lock(g_seedListMutex);
                const bool hasSeeds = !g_presetSeeds.empty();
                if (hasSeeds && (g_selectedSeedIdx < 0 || g_selectedSeedIdx >= (int)g_presetSeeds.size()))
                    g_selectedSeedIdx = 0;
                const char* preview = hasSeeds ? g_presetSeeds[(size_t)g_selectedSeedIdx].name.c_str()
                                               : Tr("Click Get All Seeds first", u8"请先点击获取全部种子");
                if (!hasSeeds) ImGui::BeginDisabled();
                if (ImGui::BeginCombo(Tr("Seed", u8"种子"), preview)) {
                    for (int i = 0; i < (int)g_presetSeeds.size(); ++i) {
                        const bool selected = i == g_selectedSeedIdx;
                        if (ImGui::Selectable(g_presetSeeds[(size_t)i].name.c_str(), selected)) {
                            g_selectedSeedIdx = i;
                            g_seedStaticId = g_presetSeeds[(size_t)i].id;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if (!hasSeeds) ImGui::EndDisabled();
            }

            std::string selectedSeedName = Tr("Not selected", u8"未选择");
            {
                std::lock_guard<std::mutex> lock(g_seedListMutex);
                if (!g_presetSeeds.empty() && g_selectedSeedIdx >= 0 && g_selectedSeedIdx < (int)g_presetSeeds.size())
                    selectedSeedName = g_presetSeeds[(size_t)g_selectedSeedIdx].name;
            }
            ImGui::Text(Tr("Target seed: %s", u8"目标种子: %s"), selectedSeedName.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const int gardenOperation = g_gardenOperation.load(std::memory_order_acquire);
            const bool gardenBotRunning = g_isGardenBotRunning.load(std::memory_order_acquire);
            const bool canStartGardenBot = gardenBotRunning || (gardenOperation == 0);
            if (!canStartGardenBot) ImGui::BeginDisabled();
            if (ImGui::Button(gardenBotRunning
                ? Tr("Stop Auto Plant", u8"停止自动种植")
                : Tr("Start Auto Plant", u8"开始自动种植"), ImVec2(-1, 55))) {
                if (gardenBotRunning) {
                    g_isGardenBotRunning.store(false, std::memory_order_release);
                } else {
                    int expected = 0;
                    if (g_gardenOperation.compare_exchange_strong(
                        expected, 1, std::memory_order_acq_rel)) {
                        g_isGardenBotRunning.store(true, std::memory_order_release);
                        std::thread(AutoGardenBotThread).detach();
                    }
                }
            }
            if (!canStartGardenBot) ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Text(Tr("Bot status: %s", u8"机器人状态: %s"), g_botStatus.c_str());
            ImGui::Text(Tr("Pots Detected: %d", u8"检测到的花盆: %d"), (int)g_scannedPots.size());
            if (!g_seedListLoaded.load(std::memory_order_acquire)) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), Tr("Tip: Click [Get All Seeds] to enable auto-planting.", u8"提示：点击[获取全部种子]启用自动播种。"));
            } else if (g_seedStaticId <= 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), Tr("Tip: Select a seed to auto-plant (currently Harvest Only).", u8"提示：请选择种子（当前仅自动收获）。"));
            }
            ImGui::Spacing();

            const bool waterAllBusy = gardenOperation != 0;
            if (waterAllBusy) ImGui::BeginDisabled();
            if (ImGui::Button(Tr("[~] WATER ALL (CROPS & PLANTS)", u8"[~] 浇灌所有作物和植物"), ImVec2(-1, 55))) {
                int expected = 0;
                if (g_gardenOperation.compare_exchange_strong(
                    expected, 2, std::memory_order_acq_rel)) {
                    std::thread(ExecuteWateringAll).detach();
                }
            }
            if (waterAllBusy) ImGui::EndDisabled();
            ImGui::Spacing();

            const bool pickBusy = g_pickAllPlantsBusy.load(std::memory_order_acquire);
            if (pickBusy) ImGui::BeginDisabled();
            if (ImGui::Button(pickBusy
                    ? Tr("Picking Plants...", u8"正在采摘植物...")
                    : Tr("Pick All Plant", u8"采摘所有植物"), ImVec2(-1, 55))) {
                std::thread([]() { FetchAndExecute(WireForOp(OP_PICK_ALL_PLANT)); }).detach();
            }
            if (pickBusy) ImGui::EndDisabled();
            ImGui::TextDisabled("%s", Tr("Picks mature plants from your own field only.",
                                         u8"仅采摘自己田地中已成熟的植物。"));
            ImGui::Spacing();
            ImGui::Separator();

            // HEADER: AUTO PLANT FLOWER (INSTANT)
            ActiveUI::Section("F", Tr("AUTO PLANT FLOWER (INSTANT)", u8"自动种花 (瞬间)"));
            ImGui::Spacing();

            const bool flowerSeedLoading = g_flowerSeedListLoading.load(std::memory_order_acquire);
            if (flowerSeedLoading) ImGui::BeginDisabled();
            if (ImGui::Button(flowerSeedLoading
                ? Tr("Loading Flower Seeds...", u8"正在加载花种...")
                : Tr("Get All Flower Seeds", u8"获取所有花种"), ImVec2(-1, 55))) {
                RunOnUnityThreadAsync([]() {
                    LoadAllFlowerSeeds();
                });
            }
            if (flowerSeedLoading) ImGui::EndDisabled();

            {
                std::lock_guard<std::mutex> lock(g_flowerSeedListMutex);
                const bool hasSeeds = !g_presetFlowerSeeds.empty();
                if (hasSeeds && (g_selectedFlowerSeedIdx < 0 ||
                    g_selectedFlowerSeedIdx >= (int)g_presetFlowerSeeds.size()))
                    g_selectedFlowerSeedIdx = 0;

                const char* preview = hasSeeds
                    ? g_presetFlowerSeeds[(size_t)g_selectedFlowerSeedIdx].name.c_str()
                    : Tr("Click Get All Flower Seeds first", u8"请先点击获取所有花种");

                if (!hasSeeds) ImGui::BeginDisabled();
                if (ImGui::BeginCombo(Tr("Flower Seed", u8"花种"), preview)) {
                    for (int i = 0; i < (int)g_presetFlowerSeeds.size(); ++i) {
                        const bool selected = i == g_selectedFlowerSeedIdx;
                        if (ImGui::Selectable(g_presetFlowerSeeds[(size_t)i].name.c_str(), selected)) {
                            g_selectedFlowerSeedIdx = i;
                            g_flowerSeedStaticId = g_presetFlowerSeeds[(size_t)i].id;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if (!hasSeeds) ImGui::EndDisabled();
            }

            ImGui::SliderInt(Tr("Plant Count", u8"种植数量"), &g_autoPlantFlowerCount, 1, 10);

            if (g_flowerHobbyMaxCapacity > 0) {
                ImGui::Text(Tr("Hobby Capacity: %d / %d flowers", u8"爱好容量: %d / %d"),
                    g_flowerCurrentPlaced, g_flowerHobbyMaxCapacity);
            }

            const bool flowerPlantBusy = g_isAutoPlantFlowerBusy.load(std::memory_order_acquire);
            if (flowerPlantBusy) ImGui::BeginDisabled();
            if (ImGui::Button(flowerPlantBusy
                ? Tr("Planting Flowers...", u8"正在种花...")
                : Tr("AUTO PLANT FLOWER", u8"自动种花"), ImVec2(-1, 55))) {
                std::thread(ExecuteAutoPlantFlower).detach();
            }
            if (flowerPlantBusy) ImGui::EndDisabled();

            ImGui::Text(Tr("Status: %s", u8"状态: %s"), g_flowerPlantStatus.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            }

            if (game_sub == 4) {
            ImGui::Spacing();
            ActiveUI::Section("C", Tr("Patch Mode", u8"补丁模式"));
            ImGui::Spacing();

            for (auto &feat: featureList) {
                if (feat.isPatch) {
                    bool isActive = g_activePatches[feat.id];
                    ImGui::BeginDisabled(isActive);

                    std::string translatedName =
                            (g_language == LANG_CN && !feat.nameZh.empty()) ? feat.nameZh : feat.name;

                    std::string btnText = isActive ? (translatedName + Tr(" [ACTIVE]", u8" [已激活]"))
                                                   : translatedName;

                    if (ImGui::Button(btnText.c_str(), ImVec2(-1, ActiveUI::S(52)))) {
                        LOGE("[PATCH-UI] klik feat=%s", feat.id.c_str());   // <-- tambah
                        g_activePatches[feat.id] = true;
                        std::thread([feat]() {
                            LOGE("[PATCH-UI] masuk FetchAndExecute %s", feat.id.c_str()); // <-- tambah
                            FetchAndExecute(feat.id);
                            LOGE("[PATCH-UI] selesai FetchAndExecute %s", feat.id.c_str()); // <-- tambah
                        }).detach();
                    }

                    ImGui::EndDisabled();
                    ImGui::Spacing();
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            }

            if (game_sub == 3) {
            ImGui::Spacing();
            ActiveUI::Section("C", Tr("Others", u8"其他"));
            ImGui::Spacing();

            ActiveUI::Section("P", Tr("Swim Mode", u8"游泳模式"));
            if (ImGui::Checkbox(Tr("Enable Swim Mode", u8"启用游泳模式"), &g_swimHackActive)) {
                ExecuteGlobalSwimToggleNative(g_swimHackActive);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ActiveUI::Section("E", Tr("Skating Mode", u8"滑冰模式"));
            if (ImGui::Checkbox(Tr("Enable Skating Mode", u8"启用滑冰模式"), &g_skateHackActive)) {
                PatchSkateConfigNative(90000.0f, 1);
                ExecuteGlobalSkateToggleNative(g_skateHackActive);
            }
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Separator();
            // ================= AUTO RESTORE TOOL (VIP) =================
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.72f, 1.0f),
                               Tr("[*] AUTO RESTORE TOOL", u8"[*] 自动修复工具 (AUTO RESTORE TOOL)"));
            ImGui::Separator();

            ImGui::SliderInt(Tr("Durability Target", u8"耐久目标 (Durability Target)"),
                             &g_restoreToolThreshold, 0, 2000);
            ImGui::SliderInt(Tr("Restore Cooldown s", u8"修复冷却 秒 (Restore Cooldown s)"),
                             &g_restoreToolCooldownSec, 0, 60);
            ImGui::SameLine();
            ImGui::Text(Tr("Now: %d", u8"当前: %d (Now)"), g_currentToolDurability);
            if (ImGui::Button(Tr("Refresh Durability", u8"刷新耐久 (Refresh Durability)"))) {
                std::thread(RefreshCurrentToolDurability).detach();
            }

            if (ImGui::Checkbox(Tr("Enable Auto Restore Tool", u8"启用自动修复 (Enable Auto Restore Tool)"),
                                &g_isRestoreToolActive)) {
                if (g_isRestoreToolActive) {
                    std::thread(AutoRestoreToolRoutine).detach();
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            std::string gachaCapabilityId;
            for (const auto &feat: featureList) {
                if (feat.op == OP_GACHA_SELECTOR) {
                    gachaCapabilityId = feat.id;
                    break;
                }
            }

            ActiveUI::Section("G", Tr("Gacha Banner Selector", u8"扭蛋卡池选择器"));
            if (gachaCapabilityId.empty()) {
                ImGui::TextDisabled("%s", Tr("Feature is unavailable from server.", u8"服务器未提供此功能。"));
            } else {
                if (ImGui::Button(Tr("Scan Gacha Pools", u8"扫描扭蛋卡池"), ImVec2(-1, 55))) {
                    std::string capId = gachaCapabilityId;
                    std::thread([capId]() { FetchAndExecute(capId, "scan"); }).detach();
                }

                std::vector<GachaPoolEntry> pools;
                int selectedPoolId = 0;
                {
                    std::lock_guard<std::mutex> lock(g_gachaPoolsMutex);
                    pools = g_gachaPools;
                    selectedPoolId = g_selectedGachaPoolId;
                }

                const GachaPoolEntry *selectedPool = nullptr;
                for (const auto &pool: pools) {
                    if (pool.id == selectedPoolId) {
                        selectedPool = &pool;
                        break;
                    }
                }
                const std::string preview = selectedPool
                    ? selectedPool->name + "  [ID " + std::to_string(selectedPool->id) + "]"
                    : Tr("Select a pool", u8"选择卡池");

                if (ImGui::BeginCombo(Tr("Gacha Pool", u8"扭蛋卡池"), preview.c_str())) {
                    for (const auto &pool: pools) {
                        const bool selected = pool.id == selectedPoolId;
                        const std::string label = pool.name + "  [ID " + std::to_string(pool.id) +
                                                  ", Type " + std::to_string(pool.poolType) + "]";
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            std::lock_guard<std::mutex> lock(g_gachaPoolsMutex);
                            g_selectedGachaPoolId = pool.id;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::BeginDisabled(selectedPoolId <= 0);
                if (ImGui::Button(Tr("Open Reward List", u8"打开奖励列表"), ImVec2(-1, 55))) {
                    std::string capId = gachaCapabilityId;
                    std::string action = "rewards:" + std::to_string(selectedPoolId);
                    std::thread([capId, action]() { FetchAndExecute(capId, action); }).detach();
                }
                ImGui::EndDisabled();

                if (g_gachaPoolScanBusy.load(std::memory_order_acquire))
                    ImGui::TextDisabled("%s", Tr("Scanning gacha pools...", u8"正在扫描扭蛋卡池..."));
                else
                    ImGui::TextDisabled("%s %zu", Tr("Loaded pools:", u8"已加载卡池："), pools.size());
                ImGui::TextDisabled("%s", Tr(
                    "Select a pool to preview its reward list.",
                    u8"选择卡池以预览奖励列表。"));
            }
            ImGui::Spacing();
            ImGui::Separator();
            }

            if (game_sub == 0) {
            // ================= AUTO EAT FOOD =================
            ActiveUI::Section("Q", Tr("AUTO EAT FOOD", u8"自动进食"));
            ImGui::Separator();
            if (ImGui::Button(Tr("Scan Food", u8"扫描食物 (Scan Food)"))) {
                std::thread(ScanFoodItems).detach();
            }
            {
                std::lock_guard<std::mutex> lk(g_foodListMutex);
                std::string label = "-";
                if (!g_foodList.empty() && g_selectedFoodIdx >= 0 &&
                    g_selectedFoodIdx < (int)g_foodList.size()) {
                    auto &f = g_foodList[g_selectedFoodIdx];
                    label = f.name + " x" + std::to_string(f.count);
                }
                if (ImGui::BeginCombo(u8"##FoodCombo", label.c_str())) {
                    for (int i = 0; i < (int)g_foodList.size(); i++) {
                        auto &f = g_foodList[i];
                        std::string visible = f.name + " x" + std::to_string(f.count);
                        std::string idLabel = visible + "##food" + std::to_string(i);
                        bool sel = (g_selectedFoodIdx == i);
                        if (ImGui::Selectable(idLabel.c_str(), sel)) g_selectedFoodIdx = i;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::SliderInt(Tr("Eat when stamina <=", u8"体力低于则进食 (Eat when stamina <=)"), &g_eatStaminaThreshold, 0, 200);
            if (ImGui::Checkbox(Tr("Enable Auto Eat", u8"启用自动进食 (Enable Auto Eat)"), &g_autoEatActive)) {
                if (g_autoEatActive) std::thread(AutoEatLoop).detach();
            }
            ImGui::Spacing();

            // ================= AUTO SNOW SCULPTURE =================
            ActiveUI::Section("O", Tr("AUTO SNOW SCULPTURE", u8"自动雪雕"));
            ImGui::Separator();
            if (ImGui::Checkbox(Tr("Auto Snow Sculpture", u8"自动雪雕 (Auto Snow Sculpture)"), &g_isAutoSnowActive)) {
                if (g_isAutoSnowActive) {
                    std::thread([]() {
                        std::string wire = WireForOp(OP_AUTO_SNOW);
                        if (!wire.empty() && SesiAktif()) {
                            FetchAndExecute(wire);
                        }
                        g_isAutoSnowActive = false;
                    }).detach();
                }
            }
            ImGui::Spacing();
            ImGui::Separator();

            // ================= AUTO SAND SCULPTURE =================
            ActiveUI::Section("O", Tr("AUTO SAND SCULPTURE", u8"自动沙雕"));
            ImGui::Separator();
            if (ImGui::Checkbox(Tr("Auto Sand Sculpture", u8"自动沙雕 (Auto Sand Sculpture)"), &g_isAutoSandActive)) {
                if (g_isAutoSandActive) {
                    if (!SesiAktif()) {
                        g_isAutoSandActive = false;
                    } else {
                        std::thread([]() {
                            std::string wire = WireForOp(OP_AUTO_SAND);
                            if (!wire.empty()) {
                                FetchAndExecute(wire);
                            } else {
                                RunAutoSandSculptureLoop();
                                g_isAutoSandActive = false;
                            }
                        }).detach();
                    }
                }
            }
            ImGui::Spacing();

            // ================= AUTO PUMPKIN CARVING =================
            ActiveUI::Section("P", Tr("AUTO PUMPKIN CARVING", u8"自动南瓜雕刻"));
            ImGui::Separator();
            if (ImGui::Checkbox(Tr("Auto Pumpkin Carving", u8"自动南瓜雕刻 (Auto Pumpkin Carving)"), &g_isAutoPumpkinActive)) {
                if (g_isAutoPumpkinActive) {
                    if (!SesiAktif() || WireForOp(OP_AUTO_PUMPKIN).empty()) {
                        g_isAutoPumpkinActive = false;
                        g_pumpkinStatus = u8"服务器载荷不可用（离线）";
                    } else {
                        g_autoPumpkinPlace = true;
                        g_autoPumpkinCollect = true;
                        g_pumpkinStatus = u8"正在向服务器验证…";
                        std::thread([]() {
                            RunAutoPumpkinCarvingLoop();
                        }).detach();
                    }
                } else {
                    g_pumpkinPn.clear();
                }
            }
            if (g_isAutoPumpkinActive) {
                ImGui::SameLine();
                ImGui::TextColored(ActiveUI::kOk, u8"[已激活]");
            }
            ImGui::Checkbox(Tr("Auto Place (Meja & Labu dari Tas)", u8"自动放置"), &g_autoPumpkinPlace);
            ImGui::SameLine();
            ImGui::Checkbox(Tr("Auto Collect (Ambil Hasil)", u8"自动拾取"), &g_autoPumpkinCollect);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), u8"状态: %s | 已完成: %d", g_pumpkinStatus.c_str(), g_pumpkinDoneCount);
            ImGui::Spacing();
            ImGui::Separator();

            // ================= SEA CLEAN + AUTO BATH =================
            ActiveUI::Section("P", Tr("SEA CLEAN + AUTO BATH", u8"清理海洋 + 自动洗澡"));
            ImGui::Separator();
            if (ImGui::Checkbox(Tr("Enable Auto Clean", u8"启用自动清理"), &g_autoCleanActive)) {
                if (g_autoCleanActive) std::thread(AutoCleanSeaLoop).detach();
            }
            ImGui::Text(Tr("Mode", u8"模式"));
            ImGui::RadioButton(Tr("Instant", u8"瞬间"), &g_autoCleanMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton(Tr("Legit", u8"正常"), &g_autoCleanMode, 1);
            ImGui::SliderFloat(Tr("Clean Radius", u8"清理半径"), &g_autoCleanRadius, 5.0f, 200.0f, "%.0f m");
            ImGui::Checkbox(Tr("Auto Bath (Body Pollution)", u8"自动清洁身体污染"), &g_autoCleanBody);
            ImGui::Spacing();
            ImGui::Separator();
            }

            if (game_sub == 4) {
            // ================= AUTO OPEN BOXES =================
            ActiveUI::Section("J", Tr("AUTO OPEN BOXES", u8"自动开宝箱"));
            ImGui::Separator();
            if (ImGui::Button(Tr("Auto Open All Boxes", u8"自动开启所有宝箱"), ImVec2(-1, 70))) {
                std::thread([]() { AutoOpenBoxes(); }).detach();
            }
            ImGui::Spacing();
            ImGui::Separator();

            // ================= AUTO QUEST CLEAR =================
            ActiveUI::Section("N", Tr("AUTO QUEST CLEAR", u8"自动完成追踪任务"));
            ImGui::Separator();
            if (ImGui::Checkbox(Tr("Auto Clear Tracked Task", u8"自动完成追踪任务"), &g_autoClearTaskActive)) {
                if (g_autoClearTaskActive) {
                    if (g_autoQuestTeleport)
                        g_questFollowStatus.store((int)QuestFollowStatus::WaitingForTask, std::memory_order_relaxed);
                    std::thread(AutoClearTaskLoop).detach();
                } else {
                    g_questFollowStatus.store((int)(g_autoQuestTeleport
                        ? QuestFollowStatus::WorkerStopped : QuestFollowStatus::Off),
                        std::memory_order_relaxed);
                }
            }
            ImGui::SliderInt(Tr("Interval (ms)", u8"间隔 (ms)"), &g_autoClearTaskIntervalMs, 200, 2000);
            if (ImGui::Checkbox(Tr("Follow Tracked Quest Waypoint", u8"跟随当前任务导航点"), &g_autoQuestTeleport)) {
                g_questFollowStatus.store((int)(g_autoQuestTeleport
                    ? (g_autoClearTaskActive ? QuestFollowStatus::WaitingForTask
                                             : QuestFollowStatus::WorkerStopped)
                    : QuestFollowStatus::Off), std::memory_order_relaxed);
            }
            {
                const QuestFollowStatus followStatus = (QuestFollowStatus)
                    g_questFollowStatus.load(std::memory_order_relaxed);
                const char* followText = Tr("WORKER STOPPED", u8"工作线程已停止");
                ImVec4 followColor(1.0f, 0.72f, 0.20f, 1.0f);
                switch (followStatus) {
                    case QuestFollowStatus::Off:
                        followText = Tr("OFF", u8"关闭");
                        followColor = ActiveUI::kMuted; break;
                    case QuestFollowStatus::WorkerStopped:
                        followText = Tr("WORKER STOPPED", u8"工作线程已停止"); break;
                    case QuestFollowStatus::WaitingForTask:
                        followText = Tr("WAITING FOR TASK", u8"等待追踪任务"); break;
                    case QuestFollowStatus::WaitingForAcceptedState:
                        followText = Tr("WAITING FOR ACCEPTED STATE", u8"等待任务接受状态"); break;
                    case QuestFollowStatus::PlayerPositionFailed:
                        followText = Tr("PLAYER POSITION FAILED", u8"读取玩家位置失败");
                        followColor = ImVec4(1.0f, 0.30f, 0.25f, 1.0f); break;
                    case QuestFollowStatus::NoMatchingHudWaypoint:
                        followText = Tr("NO MATCHING HUD WAYPOINT", u8"没有匹配的 HUD 导航点"); break;
                    case QuestFollowStatus::WaitingForStableWaypoint:
                        followText = Tr("WAITING FOR STABLE WAYPOINT", u8"等待导航点稳定"); break;
                    case QuestFollowStatus::TeleportFailed:
                        followText = Tr("TELEPORT FAILED", u8"传送失败");
                        followColor = ImVec4(1.0f, 0.30f, 0.25f, 1.0f); break;
                    case QuestFollowStatus::Teleported:
                        followText = Tr("TELEPORTED", u8"已传送");
                        followColor = ActiveUI::kOk; break;
                }
                ImGui::SameLine();
                ImGui::TextColored(followColor, "[%s]", followText);
            }
            if (g_autoQuestTeleport) {
                ImGui::SliderFloat(Tr("Reposition Distance", u8"重新定位距离"),
                                   &g_autoQuestTeleportDistance, 5.0f, 50.0f, "%.0f m");
                ImGui::TextDisabled(Tr("Uses the tracked waypoint shown by the quest HUD.",
                                       u8"使用任务 HUD 显示的追踪导航点。"));
            }
            if (ImGui::Checkbox(Tr("Auto Next NPC Dialogue", u8"自动继续 NPC 对话"),
                                &g_autoQuestDialogue)) {
                if (g_autoQuestDialogue) std::thread(AutoQuestUiLoop).detach();
            }
            if (ImGui::Checkbox(Tr("Auto Skip Quest Cutscene", u8"自动跳过任务过场"),
                                &g_autoQuestCutsceneSkip)) {
                if (g_autoQuestCutsceneSkip) std::thread(AutoQuestUiLoop).detach();
            }
            ImGui::TextDisabled(Tr("Dialogue choices remain manual.", u8"对话选项仍需手动选择。"));
            ImGui::Spacing();
            ImGui::Separator();

            ImGui::TextColored(ActiveUI::kOk, OBFUSCATE("------"));
            }

            if (hasGroupedGameContent) {
                ImGui::EndGroup();
                ImGui::Separator();
            }
        }

        if (tab == 3) {
            // === PLAYER MACROS + SCAN ===
            ActiveUI::Section("G", Tr("PLAYER MACROS", u8"玩家宏"));
            ImGui::Separator();
            ImGui::Spacing();

            for (auto &feat: featureList) {
                if (feat.catalog != 3) continue;
                if (feat.op == OP_GET_MY_HOME_ID ||
                    feat.op == OP_GET_FIELD_BY_POS ||
                    feat.op == OP_GET_OWNER_NETID ||
                    feat.op == OP_GET_FIELD_ID_BY_PLAYER ||
                    feat.op == OP_TELE_TO_POS)
                    continue;

                const char *displayName =
                        (g_language == LANG_CN && !feat.nameZh.empty()) ? feat.nameZh.c_str() : feat.name.c_str();

                if (ImGui::Button(displayName, ImVec2(-1, 0))) {
                    if (feat.op == OP_TELE_TO_HOME) {
                        std::thread([]() {
                            InvokeRequest req = FetchPayloadSync(WireForOp(OP_GET_MY_HOME_ID));
                            req.featureId = (const char *) OBFUSCATE("MACRO_TELE_HOME");
                            std::lock_guard<std::mutex> lock(invokeMutex);
                            invokeQueue.push_back(req);
                        }).detach();
                    } else {
                        std::thread([feat]() { FetchAndExecute(feat.id); }).detach();
                    }
                }
                ImGui::Spacing();
            }

            if (ImGui::Button(Tr("[*] SCAN NEARBY PLAYERS", u8"[*] 扫描附近玩家 (SCAN NEARBY PLAYERS)"), ImVec2(-1, 0))) {
                std::thread([]() { ExecutePlayerScanner(); }).detach();
            }

            ImGui::Spacing();
            ImGui::Columns(2, "playerLayout", true);

            // --- KOLOM 1: HOME ---
            ActiveUI::Section("H", Tr("HOME", u8"家座"));
            ImGui::Separator();
            for (int i = 0; i < 12; i++) {
                std::string homeTxt = std::string(Tr("HOME ", u8"家 (HOME) ")) + std::to_string(i + 1);
                if (ImGui::Button(homeTxt.c_str(), ImVec2(-1, 0))) {
                    std::string param = g_homeCoords[i];
                    std::thread([param]() { FetchAndExecute(WireForOp(OP_TELE_TO_POS), param); }).detach();
                }
                if (g_ownerNetIds[i] != 0) {
                    std::string plyTxt = std::string(Tr("PLAYER ", u8"玩家 (PLAYER) ")) + std::to_string(i + 1);
                    if (ImGui::Button(plyTxt.c_str(), ImVec2(-1, 0))) {
                        std::string param = std::to_string(g_ownerNetIds[i]);
                        std::thread([param]() { FetchAndExecute(WireForOp(OP_TELE_TO_HOME), param); }).detach();
                    }
                }
            }

            ImGui::NextColumn();

            // --- KOLOM 2: SCANNED PLAYERS ---
            ActiveUI::Section("G", Tr("SCANNED PLAYERS", u8"扫描到的玩家"));
            ImGui::Separator();
            {
                std::lock_guard<std::mutex> lock(g_radarMutex);
                static std::unordered_map<uint32_t, bool> s_swimState;
                bool anyPlayer = false;
                int idx = 0;
                for (const auto &ent: g_radarList) {
                    if (ent.type != 0) continue;
                    anyPlayer = true;
                    uint32_t netId = ent.netId;

                    std::string label = ent.name.empty() ? std::string("Player") : ent.name;
                    ImGui::Text("%s", label.c_str());

                    std::string tpId = std::string(Tr("TP##", u8"传送 (TP)##")) + std::to_string(idx);
                    if (ImGui::Button(tpId.c_str(), ImVec2(0, 0))) {
                        std::thread([netId]() { Transfer2EntityNative(netId); }).detach();
                    }
                    ImGui::SameLine();

                    bool sw = s_swimState[netId];
                    std::string swId = (sw ? std::string(Tr("Swim:ON##", u8"游泳:开 (Swim:ON)##"))
                                           : std::string(Tr("Swim:OFF##", u8"游泳:关 (Swim:OFF)##"))) + std::to_string(idx);
                    if (ImGui::Button(swId.c_str(), ImVec2(0, 0))) {
                        sw = !sw;
                        s_swimState[netId] = sw;
                        ExperimentForceSenderSwimNative(netId, sw);
                    }
                    ImGui::Separator();
                    idx++;
                }
                if (!anyPlayer) {
                    ImGui::TextDisabled(Tr("No players. Tap SCAN.", u8"未扫描到玩家，请点击扫描。"));
                }
            }
            ImGui::Columns(1);
        }

        if (tab == 5) {
            ImGui::Spacing();
            ActiveUI::Section("I", Tr("Instant Auto Farm", u8"瞬间自动采集"));
            ImGui::Text(Tr("Select resources to farm:", u8"选择要采集的资源:"));

            ImGui::Checkbox(Tr("Trees", u8"树木"), &g_farmTree);
            ImGui::SameLine();
            ImGui::Checkbox(Tr("Stone", u8"石头"), &g_farmStone);
            ImGui::SameLine();
            ImGui::Checkbox(Tr("Bush", u8"灌木"), &g_farmBush);
            ImGui::Checkbox(Tr("Meteorite", u8"陨石"), &g_farmMeteorite);
            ImGui::SameLine();
            ImGui::Checkbox(Tr("Dynamic Bush", u8"动态灌木"), &g_farmDynamicBush);
            ImGui::Spacing();

            if (g_legacyInstantFarmingActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ActiveUI::kDanger);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.26f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ActiveUI::kOnAccent);
                if (ImGui::Button(Tr("Stop Farming", u8"停止采集"), ImVec2(-1, 70))) {
                    g_legacyInstantFarmingActive = false;
                    g_legacyInstantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
                }
                ImGui::PopStyleColor();
            } else if (ImGui::Button(Tr("Start Instant Farm", u8"开始瞬间采集"), ImVec2(-1, 70))) {
                if ((g_farmTree || g_farmStone || g_farmBush || g_farmMeteorite ||
                     g_farmDynamicBush) && SesiAktif()) {
                    g_legacyInstantFarmGeneration.fetch_add(1, std::memory_order_acq_rel);
                    g_legacyInstantFarmingActive = true;
                    std::thread([]() {
                        if (!SesiAktif()) {
                            g_legacyInstantFarmingActive = false;
                            return;
                        }
                        InvokeRequest req = FetchPayloadSync(WireForOp(OP_INSTANT_FARM_TRIGGER));
                        if (req.dllName.empty() || req.nameSpace.empty() || req.className.empty()) {
                            g_legacyInstantFarmingActive = false;
                            return;
                        }
                        ExecuteLegacyInstantFarmThread(req.dllName, req.nameSpace, req.className);
                    }).detach();
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ActiveUI::Section("E", Tr("Vehicle Swap", u8"载具替换"));
            ImGui::TextColored(ActiveUI::kOk, Tr("Note: Use only on loading screen", u8"注意：仅在加载界面使用"));
            ImGui::Spacing();

            ImGui::Combo(Tr("Original Vehicle", u8"原载具"), &selectedOriginalIdx, originalVehicleNames,
                         IM_ARRAYSIZE(originalVehicleNames));
            ImGui::Spacing();

            ImGui::Combo(Tr("Target Vehicle", u8"目标载具"), &selectedTargetIdx, targetVehicleNames,
                         IM_ARRAYSIZE(targetVehicleNames));
            ImGui::Spacing();

            if (ImGui::Button(Tr("Execute Swap", u8"执行替换"), ImVec2(-1, 70))) {
                std::string param = std::to_string(originalVehicleIds[selectedOriginalIdx]) +
                                    (const char *) OBFUSCATE("|") +
                                    std::to_string(targetVehicleIds[selectedTargetIdx]);
                std::thread([param]() { FetchAndExecute(WireForOp(OP_VEHICLE_SWAP), param); })
                        .detach();

                g_vehicleSwapSuccess = false;
            }

            if (g_vehicleSwapSuccess) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(
                        now - g_vehicleSwapTime).count() < 4) {
                    ImGui::Spacing();
                    ImGui::TextColored(ActiveUI::kOk, Tr("[+] Swap success! Mount/dismount to refresh.", u8"[+] 替换成功！请上下载具刷新显示。"));
                } else {
                    g_vehicleSwapSuccess = false;
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ActiveUI::Section("A", Tr("Transform NPC", u8"变身 NPC"));
            ImGui::TextColored(ActiveUI::kOk, Tr("Change your model to an NPC!", u8"将你的模型更改为 NPC！"));
            ImGui::Spacing();

            const char *npcNames[] = {
                    Tr("Select NPC...", u8"选择 NPC..."),
                    Tr("Ka Ching", u8"卡清 (Ka Ching)"),
                    Tr("Bob", u8"波叔 (Bob)"),
                    Tr("Annie", u8"安妮 (Annie)"),
                    Tr("Bailey J", u8"贝莉 (Bailey J)"),
                    Tr("Mr Joan", u8"琼女士 (Mr Joan)"),
                    Tr("Alberth Jr", u8"小阿尔伯特 (Alberth Jr)"),
                    Tr("Andrew", u8"安德鲁 (Andrew)"),
                    Tr("Vanya", u8"万尼亚 (Vanya)"),
                    Tr("Potti", u8"帕蒂 (Potti)")};
            static int selectedNpcIdx = 0;

            ImGui::Combo(Tr("NPC Model", u8"NPC 模型"), &selectedNpcIdx, npcNames, IM_ARRAYSIZE(npcNames));
            ImGui::Spacing();

            if (ImGui::Button(Tr("TRANSFORM", u8"执行变身 (TRANSFORM)"), ImVec2(-1, ActiveUI::S(52)))) {
                if (selectedNpcIdx > 0) {
                    // Konversi int pilihan ke string dan kirim sebagai parameter payload
                    std::string param = std::to_string(selectedNpcIdx);
                    // Untuk ID Payload ("npc_cosplay") tetap gunakan string standar agar bisa dibaca server
                    std::thread([param]() { FetchAndExecute(WireForOp(OP_NPC_COSPLAY), param); })
                            .detach();
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ActiveUI::Section("R", Tr("AUTO DRAW", u8"自动绘画"));
            ImGui::Spacing();

            if (ImGui::Button(Tr("Copy folder path to clipboard", u8"复制文件夹路径到剪贴板"), ImVec2(-1, 50))) {
                ImGui::SetClipboardText(g_drawingFolderPath.c_str());
            }
            // FIX: dulu hardcode "com.xd.xdt" (salah paket) -> user taruh gambar di folder yang keliru.
            // Sekarang ikut g_drawingFolderPath yang memakai GAME_PACKAGE_NAME.
            ImGui::TextColored(ActiveUI::kMuted, Tr("Path: %s", u8"路径位置: %s"),
                               g_drawingFolderPath.c_str());
            ImGui::TextColored(ActiveUI::kMuted,
                               Tr("Paste the path in MT Manager/Zarchiver and place .jpg files", u8"请在 MT管理器/Zarchiver 中粘贴路径并放置 .jpg 文件"));
            ImGui::Spacing();

            if (ImGui::Button(Tr("Scan Folder", u8"扫描文件夹"), ImVec2(-1, 50))) {
                ScanImagesInFolder();
                g_selectedImageIdx = 0;
            }
            ImGui::Spacing();

            if (!g_availableImages.empty()) {
                if (ImGui::BeginCombo(Tr("Select Image", u8"选择图片"),
                                      g_availableImages[g_selectedImageIdx].c_str())) {
                    for (int i = 0; i < g_availableImages.size(); i++) {
                        bool isSelected = (g_selectedImageIdx == i);
                        if (ImGui::Selectable(g_availableImages[i].c_str(), isSelected)) {
                            g_selectedImageIdx = i;
                        }
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::Spacing();

                // PC 同款：走 DrawBoardProtoManager.DrawingOperation 的服务端指令，
                // 不依赖 op=20 的服务器 payload（离线 dump 里没有）。
                if (ImGui::Button(Tr("Draw To Board (server op, PC-style)",
                                     u8"画到画板（服务端指令·PC同款）"),
                                  ImVec2(-1, 70))) {
                    std::string fullPath =
                            g_drawingFolderPath + g_availableImages[g_selectedImageIdx];
                    // g_drawPn 不再必需：AutoDraw.cpp 自己按名字找类/方法。
                    StartAutoDrawImageNative(fullPath);
                }
                ImGui::TextDisabled("%s", Tr(
                        "Stand at the easel with the drawing UI open. Uses DrawBoardProtoManager.DrawingOperation (same path the PC mod uses).",
                        u8"站在画板前、把绘画界面打开再按（走 PC 版同款 DrawingOperation）。"));

                if (ImGui::Button(Tr("Start Drawing (legacy local)", u8"开始绘画（旧·本地点绘）"),
                                  ImVec2(-1, 60))) {
                    std::string fullPath =
                            g_drawingFolderPath + g_availableImages[g_selectedImageIdx];
                    std::thread([fullPath]() {
                        g_drawPn = FetchPayloadSync(WireForOp(OP_AUTO_DRAW)).payloadRaw;
                        // 不再因为 payload 为空就静默 return：离线 dump 里本来就没有
                        // op=20 的信封，这时用内置默认名继续跑（见 DrawNameDefault）。
                        if (g_drawPn.empty())
                            LOGI(OBFUSCATE("[DRAW] op=20 payload kosong -> pakai nama default"));
                        ExecuteInjectJpgToCanvasNative(fullPath);
                    }).detach();
                }
                ImGui::TextColored(ActiveUI::kInfo, "%s", Tr("Status: ", u8"状态："));
                ImGui::SameLine();
                ImGui::TextWrapped("%s", g_drawStatus.c_str());
                ImGui::TextDisabled("%s", Tr(
                        "Offline build has no op=20 payload: built-in names are used (UnityEngine side is fixed, game side is searched by name).",
                        u8"离线构建没有 op=20 的 payload：改用内置默认名（UnityEngine 部分是固定名，游戏侧按名字现找）。"));
            } else {
                ImGui::TextColored(ActiveUI::kErr, Tr("No images found. Tap 'Scan Folder' first.", u8"未找到图片。请先点击 '扫描文件夹'。"));
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            // ==========================================
            // MUSIC PLAYER (port dari PC) - Misc tab
            // ==========================================
            ActiveUI::Section("M", Tr("Music Player", u8"音乐播放器"));
            ImGui::Spacing();

            if (ImGui::Button(Tr("Copy Instrument folder path", u8"复制乐器文件夹路径"), ImVec2(-1, 50)))
                ImGui::SetClipboardText(g_instrumentFolderPath.c_str());
            ImGui::TextColored(ActiveUI::kMuted, Tr("Path: %s", u8"路径位置: %s"), g_instrumentFolderPath.c_str());
            ImGui::TextColored(ActiveUI::kMuted,
                               Tr("Place .bin instrument files there, then Scan.", u8"把 .bin 乐器文件放到该文件夹，然后扫描。"));
            ImGui::Spacing();

            if (ImGui::Button(Tr("Scan Instrument Folder", u8"扫描乐器文件夹"), ImVec2(-1, 50))) {
                ScanInstrumentFolder();
                g_musicSelectedIdx = 0;
            }
            ImGui::Spacing();

            if (!g_instrumentFiles.empty()) {
                if (g_musicSelectedIdx >= (int) g_instrumentFiles.size()) g_musicSelectedIdx = 0;
                if (ImGui::BeginCombo(Tr("Select .bin", u8"选择 .bin"), g_instrumentFiles[g_musicSelectedIdx].c_str())) {
                    for (int i = 0; i < (int) g_instrumentFiles.size(); i++) {
                        bool isSel = (g_musicSelectedIdx == i);
                        if (ImGui::Selectable(g_instrumentFiles[i].c_str(), isSel)) g_musicSelectedIdx = i;
                        if (isSel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::Spacing();
                ImGui::Checkbox(Tr("Loop", u8"循环播放"), &g_musicLoop);
                ImGui::Spacing();
                if (ImGui::Button(Tr("Play Music", u8"播放音乐"), ImVec2(-1, 70))) {
                    std::string full = g_instrumentFolderPath + g_instrumentFiles[g_musicSelectedIdx];
                    bool loop = g_musicLoop;
                    std::thread([full, loop]() { PlayMusicBinNative(full, loop); }).detach();
                }
                ImGui::Spacing();
                if (ImGui::Button(Tr("Stop Music", u8"停止音乐"), ImVec2(-1, 55))) {
                    std::thread([]() { StopMusicPlaybackNative(); }).detach();
                }
            } else {
                ImGui::TextColored(ActiveUI::kErr, Tr("No .bin found. Tap 'Scan Instrument Folder' first.", u8"未找到 .bin。请先点击 '扫描乐器文件夹'。"));
            }
            ImGui::Spacing();
            ImGui::TextColored(ActiveUI::kInfo, Tr("Status: ", u8"状态："));
            ImGui::SameLine();
            ImGui::Text("%s", g_musicStatus.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ActiveUI::Section("O", Tr("Auto Collect Bubble", u8"自动收集气泡"));
            ImGui::Spacing();
            if (ImGui::Checkbox(Tr("Enable Auto Collect", u8"开启自动收集"), &g_autoCollectBubbleActive)) {
                if (g_autoCollectBubbleActive) {
                    ResetBubbleHistory();

                    std::thread([]() {
                        if (!SesiAktif()) {
                            g_autoCollectBubbleActive = false;
                            return;
                        }
                        InvokeRequest req = FetchPayloadSync(WireForOp(OP_AUTO_COLLECT_BUBBLE));
                        if (req.dllName.empty() || req.nameSpace.empty() || req.className.empty()) {
                            g_autoCollectBubbleActive = false;
                            return;
                        }
                        AutoBubbleLoopThread(req.dllName, req.nameSpace, req.className);
                    })
                            .detach();
                }
            }

            ImGui::SameLine();
            ImGui::TextColored(ActiveUI::kMuted, Tr("Bubbles collected: %zu", u8"本次已收集气泡: %zu"),
                               g_claimedBubbles.size());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

// --- Visual ESP (Radar) ---
            ImGui::TextColored(ActiveUI::kInfo, Tr("--- Visual ESP (Buggy) ---", u8"--- 视觉ESP（错误） ---"));
            ImGui::Checkbox(Tr("Players", u8"玩家"), &g_radarShowPlayers);
            ImGui::SameLine();
            ImGui::Checkbox(Tr("Resources", u8"资源"), &g_radarShowResources);
            ImGui::SameLine();
            ImGui::Checkbox(Tr("Birds", u8"鸟类"), &g_radarShowBirds);
            ImGui::Checkbox(Tr("Fish", u8"鱼"), &g_radarShowFish);
            ImGui::SameLine();
            ImGui::Checkbox(u8"角色", &g_radarShowNpc);
            ImGui::SameLine();
            ImGui::Checkbox(Tr("Insects", u8"昆虫"), &g_radarShowInsects);
            ImGui::Spacing();
            if (ImGui::Button(g_radarEnabled ? Tr("STOP ESP", u8"停止 ESP")
                                             : Tr("START ESP", u8"启动 ESP"),
                              ImVec2(-1, 62))) {
                g_radarEnabled = !g_radarEnabled;
                g_radarGeneration.fetch_add(1, std::memory_order_acq_rel);
                if (g_radarEnabled) {
                    std::thread(RadarLoopThread).detach();
                } else {
                    std::lock_guard<std::mutex> lock(g_radarMutex);
                    g_radarList.clear();
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

// --- Switch Map ---
            ImGui::TextColored(ActiveUI::kInfo, Tr("--- Switch Map ---", u8"--- 切换地图 ---"));
            ImGui::Spacing();
            const char *mapNames[] = {
                    Tr("Select map...", u8"选择地图..."),
                    Tr("Star Town", u8"星镇"),
                    Tr("Music Room", u8"音乐室"),
                    Tr("Ocean World", u8"海洋世界"),
                    Tr("Build Competition", u8"建立竞争")
            };
            int mapIds[] = {0, 1, 2, 3, 4, 5, 100};
            static int selectedMapIdx = 0;

            ImGui::Combo(Tr("Map Destination", u8"地图目的地"), &selectedMapIdx, mapNames,
                         IM_ARRAYSIZE(mapNames));
            ImGui::Spacing();

            if (ImGui::Button(Tr("Teleport to Map", u8"传送至地图"), ImVec2(-1, 70))) {
                if (selectedMapIdx > 0) {
                    std::string param = std::to_string(mapIds[selectedMapIdx]);

                    std::thread([param]() {
                        FetchAndExecute(WireForOp(OP_SWITCH_MAP), param);
                    }).detach();
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            static float g_timeValue = 0.5f;

// --- Set Time (Day/Night) ---
            ImGui::TextColored(ActiveUI::kInfo, Tr("--- Set Time (Day/Night) ---", u8"--- 设置时间（白天/夜晚）---"));
            ImGui::SliderFloat(Tr("Time", u8"时间"), &g_timeValue, 0.1f, 1.0f, "%.2f");
            ImGui::SameLine();

            if (ImGui::Button(Tr("Set", u8"放"), ImVec2(90, 0))) {
                std::string param = std::to_string(g_timeValue);
                std::thread([param]() {
                    InvokeRequest req;
                    req.featureId = std::string((const char *)OBFUSCATE("set_time"));
                    req.op = OP_SET_TIME;
                    req.paramInput = param;
                    req.generasiSesi = GenerasiSesi();

                    std::lock_guard<std::mutex> lock(invokeMutex);
                    invokeQueue.push_back(req);
                }).detach();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // --- Instant Coin Farm ---
            ImGui::TextColored(ActiveUI::kInfo, Tr("--- Instant Coin Farm ---", u8"--- 即时金币农场 ---"));

            if (ImGui::Checkbox(Tr("Enable Instant Coin Farm", u8"启用即时金币农场"),
                                &g_fastHouseSetupEnabled)) {
                if (g_fastHouseSetupEnabled) {
                    g_fastHouseSetupCount = 0;
                    SetGlobalTimeScale(10.0f);
                    std::thread(FastHouseLoopThread).detach();
                } else {
                    SetGlobalTimeScale(1.0f);
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(ActiveUI::kWarn, Tr("[ Executed: %d ]", u8"[ 执行: %d ]"),
                               g_fastHouseSetupCount);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        if (tab == 2 && game_sub == 5) {
            ImGui::Spacing();
            const std::string weatherCapabilityId = WireForOp(OP_WEATHER_CHANGER);
            if (weatherCapabilityId.empty()) {
                ImGui::TextDisabled("%s", Tr("Feature is unavailable from server.", u8"服务器未提供此功能。"));
            }

            // Letakkan Snow Surface di bagian teratas agar selalu terlihat pada SawitUI,
            // termasuk ketika tinggi menu kecil atau posisi scroll dari sub-tab lama tersimpan.
            ActiveUI::Section("S", Tr("Snow Surface Amount", u8"积雪覆盖量"));
            ImGui::TextUnformatted(Tr("Surface Amount", u8"积雪覆盖量"));
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##weather_snow_surface_amount", &g_snowSurfaceAmount,
                               0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            ImGui::BeginDisabled(weatherCapabilityId.empty());
            const float snowButtonGap = ImGui::GetStyle().ItemSpacing.x;
            const float snowButtonWidth =
                    (ImGui::GetContentRegionAvail().x - snowButtonGap) * 0.5f;
            if (ImGui::Button(Tr("APPLY SNOW", u8"应用积雪"), ImVec2(snowButtonWidth, 52))) {
                const std::string capId = weatherCapabilityId;
                const std::string action = "surface:" +
                                           std::to_string(g_snowSurfaceAmount / 100.0f);
                std::thread([capId, action]() { FetchAndExecute(capId, action); }).detach();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("RESET SNOW", u8"重置积雪"), ImVec2(snowButtonWidth, 52))) {
                g_snowSurfaceAmount = 0.0f;
                const std::string capId = weatherCapabilityId;
                std::thread([capId]() { FetchAndExecute(capId, "surface:0"); }).detach();
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ActiveUI::Section("W", Tr("Local Weather Changer", u8"本地天气切换"));

            ImGui::BeginDisabled(weatherCapabilityId.empty());
            if (ImGui::Button(Tr("Scan Weather Types", u8"扫描天气类型"), ImVec2(-1, 52))) {
                const std::string capId = weatherCapabilityId;
                std::thread([capId]() { FetchAndExecute(capId, "scan"); }).detach();
            }
            ImGui::EndDisabled();

            std::vector<WeatherTypeEntry> weatherSnapshot;
            int selectedWeatherId = 0;
            std::string weatherStatus;
            {
                std::lock_guard<std::mutex> lock(g_weatherTypesMutex);
                weatherSnapshot = g_weatherTypes;
                selectedWeatherId = g_selectedWeatherId;
                weatherStatus = g_weatherStatus;
            }

            const WeatherTypeEntry *selectedWeather = nullptr;
            for (const auto &weather: weatherSnapshot) {
                if (weather.id == selectedWeatherId) {
                    selectedWeather = &weather;
                    break;
                }
            }
            const std::string weatherPreview = selectedWeather
                    ? selectedWeather->nameDay + "  [ID " + std::to_string(selectedWeather->id) + "]"
                    : Tr("Select weather", u8"选择天气");

            if (ImGui::BeginCombo(Tr("Weather", u8"天气"), weatherPreview.c_str())) {
                for (const auto &weather: weatherSnapshot) {
                    const bool selected = weather.id == selectedWeatherId;
                    std::string label = weather.nameDay;
                    if (weather.nameNight != weather.nameDay)
                        label += " / " + weather.nameNight;
                    label += "  [ID " + std::to_string(weather.id) + "]";
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        std::lock_guard<std::mutex> lock(g_weatherTypesMutex);
                        g_selectedWeatherId = weather.id;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SliderFloat(Tr("Transition (seconds)", u8"过渡时间（秒）"),
                               &g_weatherTransitionSeconds, 0.0f, 10.0f, "%.1f s");
            ImGui::BeginDisabled(weatherCapabilityId.empty() || selectedWeatherId <= 0);
            if (ImGui::Button(Tr("Apply Local Weather", u8"应用本地天气"), ImVec2(-1, 55))) {
                const std::string capId = weatherCapabilityId;
                const std::string action = "apply:" + std::to_string(selectedWeatherId) +
                                           ":" + std::to_string(g_weatherTransitionSeconds);
                std::thread([capId, action]() { FetchAndExecute(capId, action); }).detach();
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::BeginDisabled(weatherCapabilityId.empty());
            if (ImGui::Button(Tr("Restore Live Weather", u8"恢复实时天气"), ImVec2(-1, 52))) {
                const std::string capId = weatherCapabilityId;
                std::thread([capId]() { FetchAndExecute(capId, "restore"); }).detach();
            }
            ImGui::EndDisabled();

            if (g_weatherScanBusy.load(std::memory_order_acquire))
                ImGui::TextDisabled("%s", Tr("Scanning weather types...", u8"正在扫描天气类型..."));
            else
                ImGui::TextWrapped("%s", weatherStatus.c_str());
            ImGui::TextDisabled("%s", Tr(
                    "Visual/local only; gameplay conditions still follow server weather.",
                    u8"仅影响本地视觉；游戏条件仍遵循服务器天气。"));
            ImGui::Spacing();
        }

        if (tab == 2 && game_sub == 6) {
            ImGui::Spacing();
            // ================================================================
            //  解锁全部表情动作（PC 版 EmoteUnlockFeature 移植）
            // ================================================================
            ActiveUI::Section("E", Tr("Unlock All Emotes", u8"解锁全部表情动作"));
            ImGui::TextDisabled("%s", Tr(
                    "Panel shows every emote; locked ones are played locally on click.",
                    u8"面板列出全部表情；未获得的点击时由本机补演。"));

            // 启动/停止按钮：和其它功能一致的一键开关（PC 版只有开关，这里给按钮）。
            if (ImGui::Button(g_emoteUnlockEnabled
                                      ? Tr("STOP EMOTE UNLOCK", u8"停止表情全解锁")
                                      : Tr("START EMOTE UNLOCK", u8"启动表情全解锁"),
                              ImVec2(-1, 62))) {
                g_emoteUnlockEnabled = !g_emoteUnlockEnabled;
                // 打开：立刻建列表 + 挂 hook（没进世界会提示"等进世界"，不会硬挂）。
                // 关闭：下一帧 ProcessEmoteUnlockOnUpdate 会把 getter 放回原版。
                if (g_emoteUnlockEnabled) ReinstallEmoteUnlockHooksNative();
            }
            ImGui::TextDisabled("%s", g_emoteUnlockEnabled
                                      ? Tr("Running — panel list replaced + local playback armed.",
                                           u8"运行中 —— 面板列表已替换，本地补演已武装。")
                                      : Tr("Stopped.", u8"已停止。"));
            if (ImGui::Button(Tr("Reinstall Hooks", u8"重新安装 Hook"), ImVec2(-1, 48)))
                ReinstallEmoteUnlockHooksNative();

            ImGui::TextDisabled(u8"试演 ID（直接跑本地那一半）");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("##emote_probe_id", &g_emoteUnlockProbeActionId, 1, 10);
            ImGui::Checkbox(Tr("Loop##emote_probe", u8"循环##emote_probe"),
                            &g_emoteUnlockProbeLoop);
            ImGui::BeginDisabled(g_emoteUnlockProbeActionId <= 0);
            if (ImGui::Button(Tr("Play Locally", u8"本地试演"), ImVec2(-1, 48)))
                PlayEmoteUnlockLocalNowNative(g_emoteUnlockProbeActionId,
                                              g_emoteUnlockProbeLoop);
            ImGui::EndDisabled();

            ImGui::TextColored(ActiveUI::kInfo, "%s", Tr("Status: ", u8"状态："));
            ImGui::SameLine();
            ImGui::TextWrapped("%s", GetEmoteUnlockStatus().c_str());
            ImGui::TextDisabled("%s", Tr(
                    "Body emotes replicate to other players; face bubbles are local only.",
                    u8"身体动作会同步给别人；表情气泡仅本机可见。"));

            // ================================================================
            //  解锁所有双人互动（PC 版 FriendInteractUnlockFeature 移植）
            // ================================================================
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ActiveUI::Section("D", Tr("Unlock Two-Person Interactions", u8"解锁所有双人互动"));
            ImGui::TextDisabled("%s", Tr(
                    "Lists event-locked duo poses. Friendship level is never touched.",
                    u8"只把活动锁的双人动作列出来；好友等级一律不改。"));

            if (ImGui::Button(g_friendInteractUnlockEnabled
                                      ? Tr("STOP DUO UNLOCK", u8"停止双人互动解锁")
                                      : Tr("START DUO UNLOCK", u8"启动双人互动解锁"),
                              ImVec2(-1, 62))) {
                g_friendInteractUnlockEnabled = !g_friendInteractUnlockEnabled;
                if (g_friendInteractUnlockEnabled) ApplyFriendInteractUnlockNative();
            }
            ImGui::TextDisabled("%s", g_friendInteractUnlockEnabled
                                      ? Tr("Running — re-applied every ~10 s.",
                                           u8"运行中 —— 每约 10 秒自动重贴一次。")
                                      : Tr("Stopped (already listed rows stay listed until the level table reloads).",
                                           u8"已停止（已经列出来的行会保持，直到等级表重新加载）。"));

            // 等级双人动作：把 friendshipLv 拍成 0，等级桶里的行也变成可点。
            if (ImGui::Button(g_friendInteractLevelUnlockEnabled
                                      ? Tr("STOP LEVEL DUO UNLOCK", u8"停止等级双人动作解锁")
                                      : Tr("START LEVEL DUO UNLOCK", u8"启动等级双人动作解锁"),
                              ImVec2(-1, 62))) {
                g_friendInteractLevelUnlockEnabled = !g_friendInteractLevelUnlockEnabled;
                if (g_friendInteractLevelUnlockEnabled) ApplyFriendInteractUnlockNative();
            }
            ImGui::TextDisabled("%s", Tr(
                    "Flattens friendshipLv so level-gated duo poses become clickable. Out-of-level ones are still refused server-side (601).",
                    u8"把 friendshipLv 拍平，等级锁的双人动作变成可点；真正越级的服务器仍回 601。"));

            ImGui::TextColored(ActiveUI::kInfo, "%s", Tr("Status: ", u8"状态："));
            ImGui::SameLine();
            ImGui::TextWrapped("%s", GetFriendInteractUnlockStatus().c_str());
            ImGui::TextDisabled("%s", Tr(
                    "Item transfers and seasonal activities stay locked; duo poses still need the partner.",
                    u8"物品转移与季节活动保持锁定；双人动作仍需对方配合。"));

            // ================================================================
            //  Mono 符号自检（名字对不上时先跑这个）
            // ================================================================
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ActiveUI::Section("P", Tr("Symbol Probe", u8"符号自检"));
            ImGui::TextDisabled("%s", Tr(
                    "Dumps loaded assemblies and every class/method this feature needs.",
                    u8"导出已加载程序集，以及本功能依赖的每个类/方法/字段的解析结果。"));
            ImGui::BeginDisabled(g_symbolProbeBusy.load(std::memory_order_acquire));
            if (ImGui::Button(Tr("Run Probe", u8"运行自检"), ImVec2(-1, 48)))
                RunMonoSymbolProbeNative();
            ImGui::EndDisabled();
            ImGui::TextWrapped("%s", GetMonoSymbolProbeReport().c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ActiveUI::Section("A", Tr("Local Player Action", u8"本地玩家动作"));
            ImGui::TextDisabled("%s", Tr(
                    "Plays the selected action only on this client; no network request is sent.",
                    u8"仅在本地客户端播放所选动作；不发送网络请求。"));

            const bool actionBusy = g_playerActionBusy.load(std::memory_order_acquire);
            ImGui::BeginDisabled(actionBusy);
            if (ImGui::Button(Tr("Scan Action Table", u8"扫描动作表"), ImVec2(-1, 52)))
                std::thread([]() { ScanPlayerActionsNative(); }).detach();
            ImGui::EndDisabled();

            std::vector<PlayerActionEntry> actionSnapshot;
            int selectedActionId = 0;
            std::string actionStatus;
            {
                std::lock_guard<std::mutex> lock(g_playerActionsMutex);
                actionSnapshot = g_playerActions;
                selectedActionId = g_selectedPlayerActionId;
                actionStatus = g_playerActionStatus;
            }

            static char actionFilter[96] = {};
            ImGui::InputTextWithHint("##player_action_filter",
                    Tr("Search name, resource, or ID...", u8"搜索名称、资源或ID..."),
                    actionFilter, IM_ARRAYSIZE(actionFilter));

            const PlayerActionEntry *selectedAction = nullptr;
            for (const auto &action: actionSnapshot) {
                if (action.id == selectedActionId) {
                    selectedAction = &action;
                    break;
                }
            }
            const std::string actionPreview = selectedAction
                    ? selectedAction->name + "  [ID " + std::to_string(selectedAction->id) + "]"
                    : Tr("Select an action", u8"选择动作");

            if (ImGui::BeginCombo(Tr("Action", u8"动作"), actionPreview.c_str())) {
                std::string filter = actionFilter;
                std::transform(filter.begin(), filter.end(), filter.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                for (const auto &action: actionSnapshot) {
                    const std::string label = action.name + "  [ID " +
                                              std::to_string(action.id) + "]";
                    std::string searchable = label + " " + action.resource;
                    std::transform(searchable.begin(), searchable.end(), searchable.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (!filter.empty() && searchable.find(filter) == std::string::npos) continue;
                    const bool selected = action.id == selectedActionId;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        std::lock_guard<std::mutex> lock(g_playerActionsMutex);
                        g_selectedPlayerActionId = action.id;
                        selectedActionId = action.id;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (selectedAction) {
                ImGui::TextDisabled(u8"编号: %d | %s: %s | %s: %d", selectedAction->id,
                        Tr("Loop", u8"循环"), selectedAction->isLoop ? "Yes" : "No",
                        Tr("Rarity", u8"稀有度"), selectedAction->rarity);
                if (!selectedAction->resource.empty())
                    ImGui::TextWrapped("%s: %s", Tr("Resource", u8"资源"),
                                       selectedAction->resource.c_str());
            }

            ImGui::Checkbox(Tr("Force Loop", u8"强制循环"), &g_playerActionForceLoop);
            ImGui::BeginDisabled(actionBusy || selectedActionId <= 0);
            if (ImGui::Button(Tr("Play Local Action", u8"播放本地动作"), ImVec2(-1, 55)))
                PlayPlayerActionNative(selectedActionId, g_playerActionForceLoop);
            ImGui::EndDisabled();
            ImGui::BeginDisabled(actionBusy);
            if (ImGui::Button(Tr("Stop Action", u8"停止动作"), ImVec2(-1, 52)))
                StopPlayerActionNative();
            ImGui::EndDisabled();

            ImGui::Spacing();
            if (actionBusy)
                ImGui::TextDisabled("%s", Tr("Processing action...", u8"正在处理动作..."));
            else
                ImGui::TextWrapped("%s", actionStatus.c_str());
            ImGui::TextDisabled("%s", Tr(
                    "Only you can see it. The animation asset and a compatible player state are still required.",
                    u8"仅自己可见。仍需要动画资源和兼容的玩家状态。"));
            ImGui::Spacing();
        }

        if (tab == 2 && game_sub == 7) {
            ImGui::Spacing();
            const std::string camCapabilityId = WireForOp(OP_CAMERA_CONTROLS);
            ActiveUI::Section("V", Tr("Camera Settings", u8"视角设置"));
            if (camCapabilityId.empty()) {
                ImGui::TextDisabled("%s", Tr("Feature is unavailable from server.", u8"服务器未提供此功能。"));
            }
            ImGui::BeginDisabled(camCapabilityId.empty());

            ImGui::Checkbox(Tr("Enable Camera Customization", u8"启用视角自定义"), &g_cameraModActive);
            const std::string camStatus = GetCameraStatusString();
            ImGui::TextDisabled("%s: %s", Tr("Status", u8"状态"), camStatus.c_str());
            ImGui::Spacing();

            // 1. FIELD OF VIEW (FOV)
            ActiveUI::Section("1", Tr("FIELD OF VIEW (FOV)", u8"视野范围 (FOV)"));
            ImGui::Checkbox(Tr("Enable Custom FOV", u8"启用自定义FOV"), &g_cameraFovActive);
            ImGui::BeginDisabled(!g_cameraFovActive || !g_cameraModActive);
            ImGui::SliderFloat("##camFov", &g_cameraFov, 30.0f, 120.0f, u8"%.1f 度");
            ImGui::SameLine();
            if (ImGui::Button(Tr("Reset##fov", u8"重置##fov"))) {
                g_cameraFov = 60.0f;
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();

            // 2. CAMERA DISTANCE MULTIPLIER
            ActiveUI::Section("2", Tr("CAMERA DISTANCE", u8"相机距离"));
            ImGui::Checkbox(Tr("Enable Custom Distance", u8"启用自定义距离"), &g_cameraDistActive);
            ImGui::BeginDisabled(!g_cameraDistActive || !g_cameraModActive);
            ImGui::SliderFloat("##camDist", &g_cameraDist, 0.2f, 10.0f, "%.2fx");
            ImGui::SameLine();
            if (ImGui::Button(Tr("Reset##dist", u8"重置##dist"))) {
                g_cameraDist = 1.0f;
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();

            // 3. CAMERA FOCUS OFFSET (SHOULDER / OTS)
            ActiveUI::Section("3", Tr("CAMERA OFFSET (SHOULDER / OTS)", u8"相机偏移 (肩部/高度)"));
            ImGui::Checkbox(Tr("Enable Custom Offset", u8"启用自定义偏移"), &g_cameraOffsetActive);
            ImGui::BeginDisabled(!g_cameraOffsetActive || !g_cameraModActive);
            ImGui::SliderFloat(Tr("Offset X (Left/Right)", u8"X轴偏移 (左右)"), &g_cameraOffsetX, -5.0f, 5.0f, "%.2fm");
            ImGui::SliderFloat(Tr("Offset Y (Height)", u8"Y轴偏移 (高度)"), &g_cameraOffsetY, -5.0f, 5.0f, "%.2fm");
            ImGui::SliderFloat(Tr("Offset Z (Forward/Back)", u8"Z轴偏移 (前后)"), &g_cameraOffsetZ, -5.0f, 5.0f, "%.2fm");
            if (ImGui::Button(Tr("Reset Offset", u8"重置偏移"))) {
                g_cameraOffsetX = 0.0f;
                g_cameraOffsetY = 0.0f;
                g_cameraOffsetZ = 0.0f;
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();

            // 4. CAMERA ROLL ANGLE
            ActiveUI::Section("4", Tr("CAMERA ROLL ANGLE", u8"相机倾斜角度"));
            ImGui::Checkbox(Tr("Enable Roll Angle", u8"启用倾斜角度"), &g_cameraRollActive);
            ImGui::BeginDisabled(!g_cameraRollActive || !g_cameraModActive);
            ImGui::SliderFloat("##camRoll", &g_cameraRoll, -45.0f, 45.0f, u8"%.1f 度");
            ImGui::SameLine();
            if (ImGui::Button(Tr("Reset##roll", u8"重置##roll"))) {
                g_cameraRoll = 0.0f;
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();

            // 5. NO CLIP / DISABLE CAMERA COLLISION
            ActiveUI::Section("5", Tr("CAMERA COLLISION", u8"相机碰撞"));
            ImGui::BeginDisabled(!g_cameraModActive);
            ImGui::Checkbox(Tr("Disable Camera Collision (No Clip)", u8"禁用相机碰撞 (穿墙视线)"), &g_cameraNoClipActive);
            ImGui::TextDisabled("%s", Tr(
                "Prevents camera from auto-zooming when obstructed by walls, roofs, or trees.",
                u8"防止相机被墙壁、屋顶或树木遮挡时自动拉近放大。"));
            ImGui::EndDisabled();

            ImGui::EndDisabled();
            ImGui::Spacing();
        }

        if (tab == 2 && game_sub == 8) {
            ImGui::Spacing();
            ActiveUI::Section("C", Tr("AUTO COLOR CHAT", u8"自动彩色聊天"));
            ImGui::Separator();

            if (ImGui::Checkbox(Tr("Auto Color Chat (In-game Chat)", u8"自动彩字聊天（游戏中正常聊天生效）"), &g_autoColorChatActive)) {
                if (g_autoColorChatActive) {
                    std::thread([]() { EnsureChatHookInstalled(); }).detach();
                }
            }
            ImGui::TextDisabled("%s", Tr(
                "Automatically formats every in-game chat message with bypass tags so text displays in color for all players.",
                u8"自动为发送的聊天消息添加绕过标签，使所有玩家都能看到彩色文字。"));

            ImGui::Spacing();
            ImGui::Separator();
            ActiveUI::Section("1", Tr("COLOR SELECTION", u8"颜色选择"));

            const char* colorOptions[] = {
                Tr("Random Color", u8"随机颜色 (Random)"),
                Tr("Red (#FF0000)", u8"红色 (#FF0000)"),
                Tr("Pink (#FFC0CB)", u8"粉色 (#FFC0CB)"),
                Tr("Blue (#0088FF)", u8"蓝色 (#0088FF)"),
                Tr("Green (#00CC44)", u8"绿色 (#00CC44)"),
                Tr("Yellow (#FFDD00)", u8"黄色 (#FFDD00)"),
                Tr("Purple (#BB44FF)", u8"紫色 (#BB44FF)"),
                Tr("Orange (#FF8800)", u8"橙色 (#FF8800)"),
                Tr("White (#FFFFFF)", u8"白色 (#FFFFFF)"),
                Tr("Custom Hex", u8"自定义颜色 (Custom Hex)")
            };
            ImGui::Combo(Tr("Color Mode", u8"颜色模式"), &g_chatColorMode, colorOptions, IM_ARRAYSIZE(colorOptions));

            if (g_chatColorMode == 9) {
                ImGui::InputText(Tr("Hex Code", u8"Hex颜色代码"), g_chatCustomHex, sizeof(g_chatCustomHex));
            }
            ImGui::Spacing();
        }

        if (tab == 2 && game_sub == 9) {
            ImGui::Spacing();
            ActiveUI::Section("B", Tr("BUILD FEATURES", u8"建筑功能"));
            ImGui::Separator();

            if (ImGui::Checkbox(Tr("No surface limit", u8"无表面限制"), &g_noSurfaceLimit)) {
                if (g_noSurfaceLimit) {
                    EnsureBuildHooksInstalled();
                }
            }
            ImGui::TextDisabled("%s", Tr("Allows placing objects on any surface without placement restrictions.", u8"允许在任何表面上放置物品，无放置限制。"));
            ImGui::Spacing();

            if (ImGui::Checkbox(Tr("No range/height limit", u8"无范围/高度限制"), &g_noRangeHeightLimit)) {
                if (g_noRangeHeightLimit) {
                    EnsureBuildHooksInstalled();
                }
            }
            ImGui::TextDisabled("%s", Tr("Bypasses boundary and height limits when building.", u8"绕过建造时的边界和高度限制。"));
            ImGui::Spacing();

            if (ImGui::Checkbox(Tr("Bypass Overlap", u8"绕过重叠限制"), &g_bypassOverlap)) {
                g_bypassOverlapEnabled = g_bypassOverlap;
                if (g_bypassOverlap) {
                    EnsureBuildHooksInstalled();
                }
            }
            ImGui::TextDisabled("%s", Tr("Allows intersecting or overlapping objects with other structures and slabs.", u8"允许物品与其他结构或板块重叠放置。"));
            ImGui::Spacing();

            if (ImGui::Checkbox(Tr("Free place & rotate for all items", u8"所有物品自由放置与旋转"), &g_freePlaceRotate)) {
                SetFreePlaceRotateUnlock(g_freePlaceRotate);
            }
            ImGui::TextDisabled("%s", Tr("Clears build blacklist, allowing free placement and 360 rotation on all items.", u8"清除建造黑名单，允许所有物品自由放置并进行360度旋转。"));
            ImGui::Spacing();
        }

        const bool iface_cols = (tab == 1);
        const bool renderCatalogFeatures = !(tab == 2 && (game_sub == 5 || game_sub == 6 || game_sub == 7 || game_sub == 8 || game_sub == 9));
        if (iface_cols)
            ImGui::Columns(2, "ifaceCols", false);
        for (auto &feat: featureList) {
            if (renderCatalogFeatures && feat.catalog == tab) {
                if (feat.op == OP_GET_SELF_PLAYER ||
                    feat.op == OP_SET_MOVE_SPEED ||
                    feat.op == OP_EDIT_NAME ||
                    feat.op == OP_UNLOCK_PLOT_AREA ||
                    feat.op == OP_GET_MY_HOME_ID ||
                    feat.op == OP_GET_FIELD_BY_POS ||
                    feat.op == OP_GET_OWNER_NETID ||
                    feat.op == OP_GET_FIELD_ID_BY_PLAYER ||
                    feat.op == OP_TELE_TO_POS ||
                    feat.op == OP_TELE_TO_HOME ||
                    feat.isPatch ||
                    feat.op == OP_VEHICLE_SWAP ||
                    feat.op == OP_AUTO_COLLECT_BUBBLE ||
                    feat.op == OP_INSTANT_FARM_TRIGGER ||
                    feat.op == OP_BIRD_WATCHING) {
                    continue;
                }

                const char *displayName =
                        (g_language == LANG_CN && !feat.nameZh.empty()) ? feat.nameZh.c_str() : feat.name.c_str();

                if (feat.op == OP_OPEN_SHOP && !shopNames.empty()) {
                    if (iface_cols)
                        ImGui::Columns(1);
                    ActiveUI::Section("J", Tr("Shop List", u8"商店列表"));
                    ImGui::Columns(2, "shopCols", false);
                    for (int i = 0; i < shopNames.size(); i++) {
                        // 固定中文：优先用 catalog 下发的中文店名，其次查翻译表，最后才回落原名。
                        std::string sName;
                        if (g_language == LANG_CN) {
                            if (i < (int) shopNamesZh.size() && !shopNamesZh[i].empty())
                                sName = shopNamesZh[i];
                            else
                                sName = TranslateShopName(shopNames[i]);
                        } else {
                            sName = shopNames[i];
                        }
                        std::string shopBtnName = std::string(Tr("Open: ", u8"打开: ")) + sName;
                        if (ImGui::Button(shopBtnName.c_str(), ImVec2(-1, ActiveUI::S(52)))) {
                            std::string p = shopIds[i] + (const char *) OBFUSCATE(";0");
                            std::thread([feat, p]() { FetchAndExecute(feat.id, p); })
                                    .detach();
                        }
                        ImGui::Spacing();
                        ImGui::NextColumn();
                    }
                    ImGui::Columns(1);
                    ImGui::Separator();
                    if (iface_cols)
                        ImGui::Columns(2, "ifaceCols", false);
                } else {
                    if (ImGui::Button(displayName, ImVec2(-1, ActiveUI::S(52)))) {
                        std::thread([feat]() { FetchAndExecute(feat.id); })
                                .detach();
                    }
                    ImGui::Spacing();
                    if (iface_cols)
                        ImGui::NextColumn();
                }
            }
        }
        if (iface_cols)
            ImGui::Columns(1);

        if (tab == 6) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.72f, 1.0f),
                Tr("[*]  MANUAL TELEPORTATION", u8"[*]  手动传送"));
            ImGui::Separator();
            ImGui::Spacing();

            static bool spLoaded = false;
            if (!spLoaded) { LoadSavePoints(); spLoaded = true; }

            if (ImGui::Button(Tr("[+] Save Point (Save Coordinates)",
                u8"[+] 保存当前坐标"), ImVec2(-1, 0))) {
                RunOnUnityThreadAsync([]() {
                    Vector3 cur = GetSelfPositionNative();
                    if (cur.x != 0.0f || cur.y != 0.0f || cur.z != 0.0f) {
                        g_savePoints.push_back(cur);
                        SaveSavePoints();
                    }
                });
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text(Tr("Saved Points:", u8"已保存点："));
            ImGui::Spacing();

            if (g_savePoints.empty()) {
                ImGui::TextDisabled(Tr("There are no save points yet.",
                    u8"暂无保存点。"));
            }

            int deleteIdx = -1;
            for (size_t i = 0; i < g_savePoints.size(); i++) {
                const Vector3& p = g_savePoints[i];
                std::string label = std::string(Tr("Save Point ", u8"保存点 ")) + std::to_string(i + 1);

                float fullW = ImGui::GetContentRegionAvail().x;

                if (ImGui::Button((label + "##sp" + std::to_string(i)).c_str(),
                    ImVec2(fullW * 0.78f, 0))) {
                    Vector3 dest = p;
                    RunOnUnityThreadAsync([dest]() {
                        ManualTeleportNative(dest);
                    });
                }

                ImGui::SameLine();
                if (ImGui::Button(("X##del" + std::to_string(i)).c_str(),
                    ImVec2(fullW * 0.20f, 0))) {
                    deleteIdx = (int)i;
                }

                ImGui::TextDisabled("   (%.1f, %.1f, %.1f)", p.x, p.y, p.z);
            }

            if (deleteIdx >= 0) {
                g_savePoints.erase(g_savePoints.begin() + deleteIdx);
                SaveSavePoints();
            }
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            const float currentScrollY = ImGui::GetScrollY();
            const float maxScrollY = ImGui::GetScrollMaxY();
            const float requestedScrollY = currentScrollY - ImGui::GetIO().MouseDelta.y;
            ImGui::SetScrollY(ImClamp(requestedScrollY, 0.0f, maxScrollY));
        }

        ImGui::EndChild();
    } else {
        ActiveUI::Section("O", Tr("FREE FEATURES", u8"免费功能"));
        ImGui::Spacing();

        // 2. Open General Shop
        ImGui::Text(Tr("2. Shop:", u8"2. 商店 (Shop):"));
        if (ImGui::Button(Tr("Open General Shop", u8"打开普通商店 (Open General Shop)"), ImVec2(-1, ActiveUI::S(52)))) {
            InvokeRequest req = {WireForOp(OP_OPEN_SHOP), OBFUSCATE("XDTGameUI.dll"),
                                 OBFUSCATE("XDTGame.UI.Panel"),
                                 OBFUSCATE("ShopPanel"), OBFUSCATE(":OpenShopPanel(int,int)"),
                                 OBFUSCATE("2;0"), true, ""};
            req.op = OP_OPEN_SHOP;
            req.generasiSesi = GenerasiSesi();
            std::lock_guard<std::mutex> lock(invokeMutex);
            invokeQueue.push_back(req);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3. Global Speedhack (x2)
        ImGui::Text(Tr("3. Global Speedhack x2:", u8"3. 全局时间加速 (Global Speedhack x2):"));
        if (ImGui::Checkbox(Tr("Enable x2 Speed", u8"启用二倍速 (Enable x2 Speed)"), &g_globalSpeedToggle)) {
            g_targetSpeed = g_globalSpeedToggle ? 2.0f : 1.0f; // Cuma mentok x2
            g_speedHackNeedsUpdate = true;
        }
    }

    ActiveUI::EndContent(!productUsesInnerScroll);
    {
        static bool bcDrawEnd = false;
        if (!bcDrawEnd) { bcDrawEnd = true; SMPlatform::SetBreadcrumb("ui:draw-end"); }
    }
    ActiveUI::EndChrome(frame, &is_english);
    // 固定中文：不再根据 is_english 切回英文。
    g_language = LANG_CN;
}
