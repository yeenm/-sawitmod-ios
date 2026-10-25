#include "ModFeatures.h"
#include "ModUtils.h"
#include "IL2CPP/MonoHot.h"
#include "IL2CPP/MonoCompat.h"
#include "App/Platform.h"
#include "Includes/obfuscate.h"
#include "VirboxTargets.h"
#include "Includes/Dobby/dobby.h"
#include "Includes/Logger.h"
#include <map>
#include <mutex>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <random>
#include <fstream>
#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cstdio>
#include <cfloat>
#include <unordered_set>
#include <unordered_map>
#include "NetAuth.h"
#include "PayloadVM.h"
#include "Starcoolxdl/xdl.h"
#include "ByNameModding/Tools.h"

#ifndef SAFE_READ_DEFINED
#define SAFE_READ_DEFINED
template<typename T>
static inline T SafeRead(uintptr_t addr, T defaultValue) {
    return addr ? *reinterpret_cast<T *>(addr) : defaultValue;
}
#endif

static void *AndroidMonoArrayNew(void *domain, void *byteKlass, size_t count);
static int CountBackpackItemTotalByStaticId(int targetStaticId);
static void *GetLocalPlayerInstance();

// ---------------------------------------------------------------------------
//  传送阶段结果（供上层区分"区域没就绪被放弃" / "invoke 失败" / "落地确认超时"）。
//  只存阶段码，不存任何 mono 对象指针 —— 对象用完即弃，绝不跨帧保存。
// ---------------------------------------------------------------------------
enum SMTeleportOutcome : int {
    kSMTpNone = 0,        // 还没走完一次传送流程
    kSMTpPreFail = 1,     // 传送前区域就绪检查超时/未就绪 → 主动放弃
    kSMTpInvokeFail = 2,  // AutoMoveTransfer invoke 抛异常
    kSMTpLandFail = 3,    // 传送后落地确认超时（坐标没到目标 2.0m 内）
    kSMTpOk = 4,          // 就绪 + 落地确认都通过
};
static std::atomic<int> g_tpLastOutcome{kSMTpNone};

struct ESPCachedMethods {
    bool isInitialized = false;
    void *m_GetPlayer = nullptr;
    void *m_GetPlayerMorph = nullptr;   // <== BARU
    void *m_GetPlayerHider = nullptr;   // <== BARU
    void *m_GetNpc = nullptr;
    void *m_GetFish = nullptr;
    void *m_GetInsect = nullptr;
    void *m_GetBird = nullptr;
    void *m_GetRes[6] = {nullptr};
    void *m_GetAnyEntity = nullptr;
    void *m_getPos = nullptr;
    void *m_ToString = nullptr;
    void *dataCenterKlass = nullptr;
    void *m_getCurrentData = nullptr;
    void *cameraKlass = nullptr;
    void *m_get_main = nullptr;
    void *m_WorldToScreenPoint = nullptr;
};
static ESPCachedMethods g_espCache;
static std::map<uint32_t, std::pair<int, std::string>> g_entityTypeCache;
std::vector<std::string> workPaths = {
        OBFUSCATE(
                "GameApp/startup_root(Clone)/XDUIRoot/Bottom/TrackingPanel(Clone)/tracking_bar@w/tracking_cook_danger@list/CommonIconForCookDanger(Clone)/root_visible@go/icon@img@btn"),
        OBFUSCATE(
                "GameApp/startup_root(Clone)/XDUIRoot/Full/CookPanel(Clone)/AniRoot@queueanimation/detail@t/btnBar@go/confirm@swapbtn"),
        OBFUSCATE(
                "GameApp/startup_root(Clone)/XDUIRoot/Bottom/TrackingPanel(Clone)/tracking_bar@w/tracking_common@list/IconsBarWidget(Clone)/root_visible@go/cells@t/cells@list/CommonIconForInteract(Clone)/root_visible@go/icon@img@btn"),
        OBFUSCATE(
                "GameApp/startup_root(Clone)/XDUIRoot/Scene/DialoguePanel(Clone)/AniRoot@go@ani/exit@btn@go")};

const char *g_homeCoords[12] = {
        OBFUSCATE("v3:-112.9986|19.12738|-85.80347"), OBFUSCATE("v3:-126.4445|19.85984|-39.15246"),
        OBFUSCATE("v3:-137.5608|20.63687|3.23426"), OBFUSCATE("v3:-143.7609|21.80173|56.91373"),
        OBFUSCATE("v3:-91.79245|20.11099|130.1303"), OBFUSCATE("v3:-48.95429|20.111|127.2649"),
        OBFUSCATE("v3:12.79531|21.37971|108.0554"), OBFUSCATE("v3:56.12238|20.5537|104.0894"),
        OBFUSCATE("v3:103.9095|22.86296|66.46537"), OBFUSCATE("v3:101.2672|22.885|20.32061"),
        OBFUSCATE("v3:98.78337|21.41634|-38.80313"), OBFUSCATE("v3:89.93763|20.636|-72.31506")};
std::vector<std::string> oreCoords = {OBFUSCATE("v3:130.86|21.51|-6.17"),
                                      OBFUSCATE("v3:142.02|21.83|-37.74"),
                                      OBFUSCATE("v3:110.42|20.84|-63.8"),
                                      OBFUSCATE("v3:-131.39|19.09|-70.01"),
                                      OBFUSCATE("v3:-152.19|20.89|-85.69"),
                                      OBFUSCATE("v3:-168.97|19.84|-33.69"),
                                      OBFUSCATE("v3:-145.3|20.2|-16.3"),
                                      OBFUSCATE("v3:-175.01|20.31|6.77"),
                                      OBFUSCATE("v3:-178.51|21.39|66.8"),
                                      OBFUSCATE("v3:-98.24|20.2|170.98"),
                                      OBFUSCATE("v3:-73.71|20.07|149.82"),
                                      OBFUSCATE("v3:-26.57|19.97|161.18"),
                                      OBFUSCATE("v3:-22.22|20.05|131.14"),
                                      OBFUSCATE("v3:21.55|21.49|146.35"),
                                      OBFUSCATE("v3:35.61|20.93|126.96"),
                                      OBFUSCATE("v3:81.12|20.44|135.52"),
                                      OBFUSCATE("v3:141.95|22.99|78.47"),
                                      OBFUSCATE("v3:115.74|20.82|-106.62"),
                                      OBFUSCATE("v3:121.11|23.18|43.67"),
                                      OBFUSCATE("v3:142.27|23.01|27.56")};
std::vector<std::string> stoneCoords = {OBFUSCATE("v3:-31.36|20.02|115.46"),
                                        OBFUSCATE("v3:-10.6|21.4|107.93"),
                                        OBFUSCATE("v3:16.32|21.34|146.61"),
                                        OBFUSCATE("v3:15.69|21.34|102.43"),
                                        OBFUSCATE("v3:48.21|20.68|96.87"),
                                        OBFUSCATE("v3:66.29|20.48|138.99"),
                                        OBFUSCATE("v3:96.28|23.28|70.82"),
                                        OBFUSCATE("v3:142.14|22.95|65.56"),
                                        OBFUSCATE("v3:97.33|22.64|50.28"),
                                        OBFUSCATE("v3:96.67|22.74|31.21"),
                                        OBFUSCATE("v3:143.01|22.9|21.73"),
                                        OBFUSCATE("v3:97.48|22.75|14.92"),
                                        OBFUSCATE("v3:92.79|21.53|-16.12"),
                                        OBFUSCATE("v3:144.35|21.22|-31.42"),
                                        OBFUSCATE("v3:90.14|21.57|-34.13"),
                                        OBFUSCATE("v3:80.59|20.89|-65.56"),
                                        OBFUSCATE("v3:126.48|21.07|-99.65"),
                                        OBFUSCATE("v3:76.87|20.66|-81.33"),
                                        OBFUSCATE("v3:-95.54|19.86|-79.13"),
                                        OBFUSCATE("v3:-109.09|19.02|-64.32"),
                                        OBFUSCATE("v3:-144.49|19.7|-95.58"),
                                        OBFUSCATE("v3:-119|19.77|-45.61"),
                                        OBFUSCATE("v3:-165.46|19.52|-42.18"),
                                        OBFUSCATE("v3:-125.46|19.85|-18.27"),
                                        OBFUSCATE("v3:-124.2|21.67|-2.32"),
                                        OBFUSCATE("v3:-173.92|20.44|-1.36"),
                                        OBFUSCATE("v3:-127.76|20.71|17.58"),
                                        OBFUSCATE("v3:-130.29|22.19|40.89"),
                                        OBFUSCATE("v3:-176.88|21.33|47.11"),
                                        OBFUSCATE("v3:-133.8|21.35|64.03"),
                                        OBFUSCATE("v3:-96.48|20.7|115.38"),
                                        OBFUSCATE("v3:-84.17|20.22|120.82"),
                                        OBFUSCATE("v3:-110.66|20.03|163.54"),
                                        OBFUSCATE("v3:-51.85|20.18|119.26")};
std::vector<std::string> mandarinCoords = {OBFUSCATE("v3:-36.34|20.18|115.65"),
                                           OBFUSCATE("v3:-56.55|20.18|120.67"),
                                           OBFUSCATE("v3:-65.73|20.92|120.2"),
                                           OBFUSCATE("v3:-66.84|20.37|139.3"),
                                           OBFUSCATE("v3:-32.52|20.18|160.38"),
                                           OBFUSCATE("v3:-94.02|20.18|166.78"),
                                           OBFUSCATE("v3:-81.36|20.73|118.9"),
                                           OBFUSCATE("v3:-95.85|20.59|118.67"),
                                           OBFUSCATE("v3:-114.38|20.37|128.72"),
                                           OBFUSCATE("v3:-131.08|22.35|76.65"),
                                           OBFUSCATE("v3:-131.34|22|69.88"),
                                           OBFUSCATE("v3:-127.13|23.02|40.54"),
                                           OBFUSCATE("v3:-176.81|21.49|38.67"),
                                           OBFUSCATE("v3:-155.47|20.69|30.68"),
                                           OBFUSCATE("v3:-126.61|21.9|23.35"),
                                           OBFUSCATE("v3:-174.26|20.42|9.26"),
                                           OBFUSCATE("v3:-121.27|22.28|-5.92"),
                                           OBFUSCATE("v3:-125.4|21.42|-8.61"),
                                           OBFUSCATE("v3:-123.38|20.23|-22.64"),
                                           OBFUSCATE("v3:-120.61|20.38|-30.04"),
                                           OBFUSCATE("v3:-117.51|19.93|-50.84"),
                                           OBFUSCATE("v3:-134.72|19.21|-66.5"),
                                           OBFUSCATE("v3:-159.54|20.15|-64.13"),
                                           OBFUSCATE("v3:-107.53|19.21|-66.71"),
                                           OBFUSCATE("v3:-94.39|19.51|-89.31"),
                                           OBFUSCATE("v3:-139.07|19.35|-105.9"),
                                           OBFUSCATE("v3:-92.92|19.14|-95.43")};
std::vector<std::string> raspberryCoords = {OBFUSCATE("v3:-168.1|20.1|-44.82"),
                                            OBFUSCATE("v3:-159.86|19.99|-60.52"),
                                            OBFUSCATE("v3:-135.03|18.95|-70.75"),
                                            OBFUSCATE("v3:-198.25|21.78|-71.53"),
                                            OBFUSCATE("v3:-124.88|19.07|-113.42"),
                                            OBFUSCATE("v3:-106.8|19.11|-104.88"),
                                            OBFUSCATE("v3:-153.52|20.7|-15.07"),
                                            OBFUSCATE("v3:-173.6|20.71|-7.49"),
                                            OBFUSCATE("v3:-189.19|20.02|8.1"),
                                            OBFUSCATE("v3:-176.01|20.65|12.34"),
                                            OBFUSCATE("v3:-201.96|19.37|-18.05"),
                                            OBFUSCATE("v3:-159.2|20.92|30.83"),
                                            OBFUSCATE("v3:-178.5|21.6|43.58"),
                                            OBFUSCATE("v3:-193.46|23.25|38.61"),
                                            OBFUSCATE("v3:-177.76|21.35|68.47"),
                                            OBFUSCATE("v3:-163.76|21.49|79.07"),
                                            OBFUSCATE("v3:-160.83|19.05|-103.41")};
std::vector<std::string> blueberryCoords = {OBFUSCATE("v3:-5.86|23.17|99.33"),
                                            OBFUSCATE("v3:-14.15|22.26|105.27"),
                                            OBFUSCATE("v3:-28.76|20.56|113.32"),
                                            OBFUSCATE("v3:-40.26|20.28|115.89"),
                                            OBFUSCATE("v3:-58.97|20.68|117.67"),
                                            OBFUSCATE("v3:-65.91|20.14|123.47"),
                                            OBFUSCATE("v3:-78.43|20.29|121.14"),
                                            OBFUSCATE("v3:-93.45|20.52|118.26"),
                                            OBFUSCATE("v3:21.94|22.14|98.96"),
                                            OBFUSCATE("v3:37.23|20.74|100.19"),
                                            OBFUSCATE("v3:47.98|21.6|94.41"),
                                            OBFUSCATE("v3:64.27|21|94.7"),
                                            OBFUSCATE("v3:76.87|20.5|97.59"),
                                            OBFUSCATE("v3:80.03|20.47|108.47"),
                                            OBFUSCATE("v3:74.95|20.64|137.79"),
                                            OBFUSCATE("v3:45.61|20.67|141.06"),
                                            OBFUSCATE("v3:24.01|21.66|146.53"),
                                            OBFUSCATE("v3:9.77|21.48|147.94"),
                                            OBFUSCATE("v3:-23.05|20.09|158.1"),
                                            OBFUSCATE("v3:-44.83|19.96|164.72"),
                                            OBFUSCATE("v3:97.88|22.73|52.9"),
                                            OBFUSCATE("v3:100.22|22.88|43.81"),
                                            OBFUSCATE("v3:97.27|23.1|67.74"),
                                            OBFUSCATE("v3:121.26|23.08|84.34"),
                                            OBFUSCATE("v3:137.91|23.15|82.24"),
                                            OBFUSCATE("v3:142.78|22.95|63.66"),
                                            OBFUSCATE("v3:123.6|22.89|43.15"),
                                            OBFUSCATE("v3:96.94|22.82|28.91"),
                                            OBFUSCATE("v3:97.84|22.82|18.08"),
                                            OBFUSCATE("v3:97.85|22.83|7.06"),
                                            OBFUSCATE("v3:103.39|22.97|1.14"),
                                            OBFUSCATE("v3:110.07|21.81|-11.87"),
                                            OBFUSCATE("v3:124.84|22.04|-4.28"),
                                            OBFUSCATE("v3:93.88|21.75|-22.81"),
                                            OBFUSCATE("v3:89.69|21.88|-30.47"),
                                            OBFUSCATE("v3:93.98|21.57|-41.11"),
                                            OBFUSCATE("v3:89.38|20.78|-58.92"),
                                            OBFUSCATE("v3:82.51|20.87|-63.06"),
                                            OBFUSCATE("v3:75.68|20.65|-76.41"),
                                            OBFUSCATE("v3:76.12|20.53|-86.39"),
                                            OBFUSCATE("v3:-102.05|19.36|-70.15"),
                                            OBFUSCATE("v3:-96.11|19.69|-77.12"),
                                            OBFUSCATE("v3:-91.35|19.47|-92.45"),
                                            OBFUSCATE("v3:-111.74|19.18|-60.99"),
                                            OBFUSCATE("v3:-117.23|19.84|-54.52"),
                                            OBFUSCATE("v3:-118.64|19.95|-42.71"),
                                            OBFUSCATE("v3:-120.71|20.37|-28.66"),
                                            OBFUSCATE("v3:-123.18|19.91|-26.54"),
                                            OBFUSCATE("v3:-127.92|21.14|-3.41"),
                                            OBFUSCATE("v3:-129.59|20.66|27.85"),
                                            OBFUSCATE("v3:-132.93|21.48|48.36"),
                                            OBFUSCATE("v3:-131.08|21.88|66.78"),
                                            OBFUSCATE("v3:-132.79|21.7|75.47"),
                                            OBFUSCATE("v3:-135.89|23.12|86.09"),
                                            OBFUSCATE("v3:-16.93|20.17|140.75"),
                                            OBFUSCATE("v3:-68.31|20.36|143.23"),
                                            OBFUSCATE("v3:-82.89|20.23|159.33"),
                                            OBFUSCATE("v3:-97.04|20.14|165.01"),
                                            OBFUSCATE("v3:-116.84|20.05|148.01"),
                                            OBFUSCATE("v3:-112.68|20.21|124.94"),
                                            OBFUSCATE("v3:-105.49|20.18|119.81"),
                                            OBFUSCATE("v3:31.73|21.5|124.29"),
                                            OBFUSCATE("v3:141.92|23.06|24.95"),
                                            OBFUSCATE("v3:141.98|22.85|8.89"),
                                            OBFUSCATE("v3:149.34|20.9|-25.04"),
                                            OBFUSCATE("v3:126.56|21.47|-57.81"),
                                            OBFUSCATE("v3:109.28|21.17|-61.6"),
                                            OBFUSCATE("v3:112.94|20.65|-109.74"),
                                            OBFUSCATE("v3:-125.76|21.13|5.11"),
                                            OBFUSCATE("v3:-123.9|21.98|19.82"),
                                            OBFUSCATE("v3:5.07|23.09|97.91"),
                                            OBFUSCATE("v3:127.5|21.23|-84.44")};
std::vector<std::string> appleCoords = {OBFUSCATE("v3:75.23|20.61|-90.56"),
                                        OBFUSCATE("v3:73.31|20.56|-78.31"),
                                        OBFUSCATE("v3:124.84|21.33|-89.82"),
                                        OBFUSCATE("v3:84.79|20.71|-65.84"),
                                        OBFUSCATE("v3:92.11|21.71|-44.37"),
                                        OBFUSCATE("v3:108.99|21.61|-53.08"),
                                        OBFUSCATE("v3:130.79|21.63|-42.09"),
                                        OBFUSCATE("v3:92.63|21.77|-20.51"),
                                        OBFUSCATE("v3:105.58|21.65|-15.25"),
                                        OBFUSCATE("v3:106.65|22.84|0.59"),
                                        OBFUSCATE("v3:94.72|22.51|5.86"),
                                        OBFUSCATE("v3:141.55|22.9|17.7"),
                                        OBFUSCATE("v3:94.16|22.32|28.67"),
                                        OBFUSCATE("v3:112.89|22.93|39.89"),
                                        OBFUSCATE("v3:97.42|22.89|45.89"),
                                        OBFUSCATE("v3:94.29|22.25|55.3"),
                                        OBFUSCATE("v3:141.46|22.95|74.86"),
                                        OBFUSCATE("v3:98.35|23|75.01"),
                                        OBFUSCATE("v3:75.8|20.71|93.52"),
                                        OBFUSCATE("v3:67.58|20.83|94.39"),
                                        OBFUSCATE("v3:63.39|20.57|139.33"),
                                        OBFUSCATE("v3:43.95|20.62|98.4"),
                                        OBFUSCATE("v3:36.55|20.65|116.08"),
                                        OBFUSCATE("v3:29.03|22.64|96.38"),
                                        OBFUSCATE("v3:18.62|21.6|146.54"),
                                        OBFUSCATE("v3:8.81|23.24|97.1"),
                                        OBFUSCATE("v3:-15.95|22.96|102.76")};
std::vector<std::string> oysterCoords = {OBFUSCATE("v3:-139.8|21.3|205.2")};
std::vector<std::string> tallMustardCoords = {OBFUSCATE("v3:-125.213|11.729|290.797")};
std::vector<std::string> blackTruffleCoords = {OBFUSCATE("v3:272.1|12.7|98.2")};
std::vector<std::string> fiddleheadCoords = {OBFUSCATE("v3:229.782|11.404|48.837")};
std::vector<std::string> pennyBunCoords = {OBFUSCATE("v3:176.9|25.9|59.8")};
std::vector<std::string> shitakeCoords = {OBFUSCATE("v3:57.0|18.3|-131.5")};
std::vector<std::string> burdockCoords = {OBFUSCATE("v3:-211.599|29.916|35.416")};
std::vector<std::string> mustardGreensCoords = {OBFUSCATE("v3:-58.984|11.035|-155.413")};
std::vector<std::string> seaAsparagusCoords = {
        OBFUSCATE("v3:-8.8|-41.0|-102.1"), OBFUSCATE("v3:50.3|-39.8|-74.0"),
        OBFUSCATE("v3:34.3|-46.2|-67.7"), OBFUSCATE("v3:-64.0|-36.3|-106.0"),
        OBFUSCATE("v3:-114.9|-36.8|-85.3"), OBFUSCATE("v3:-91.9|-70.4|-17.6"),
        OBFUSCATE("v3:-79.5|-69.8|-67.8"), OBFUSCATE("v3:-58.3|-70.0|-54.2"),
        OBFUSCATE("v3:-29.1|-49.6|-60.2"), OBFUSCATE("v3:-10.7|-49.4|-50.0"),
        OBFUSCATE("v3:-6.6|-49.4|-64.4")};
std::vector<std::string> seaGrapesCoords = {
        OBFUSCATE("v3:63.3|-39.4|-52.9"), OBFUSCATE("v3:75.5|-40.0|-62.8"),
        OBFUSCATE("v3:84.2|-39.5|-47.5"), OBFUSCATE("v3:79.3|-43.1|-25.9"),
        OBFUSCATE("v3:77.1|-45.4|-1.7"), OBFUSCATE("v3:93.5|-46.5|21.0"),
        OBFUSCATE("v3:79.8|-58.5|51.4"), OBFUSCATE("v3:83.4|-57.3|72.8"),
        OBFUSCATE("v3:51.2|-60.7|83.4"), OBFUSCATE("v3:22.4|-70.7|66.2")};
std::vector<std::string> wakameCoords = {
        OBFUSCATE("v3:4.2|-59.5|24.6"), OBFUSCATE("v3:-11.5|-62.8|1.4"),
        OBFUSCATE("v3:4.1|-59.8|-16.9"), OBFUSCATE("v3:22.8|-57.3|-26.7"),
        OBFUSCATE("v3:-50.2|-71.3|12.0"), OBFUSCATE("v3:-47.7|-69.4|30.0"),
        OBFUSCATE("v3:-35.2|-63.6|50.9"), OBFUSCATE("v3:-13.7|-65.6|50.8"),
        OBFUSCATE("v3:34.8|-45.3|-55.6"), OBFUSCATE("v3:45.2|-40.7|-60.7")};

// Premium Wood（优质木材）：iOS 新增资源，坐标来自用户提供的树点文件
// （优质木材坐标.txt，共 40 个点）。走 FIXED 路点：传送 -> 扫附近树 ->
// SendAttackTreeCommand 砍树。
std::vector<std::string> premiumWoodCoords = {
        OBFUSCATE("v3:166.8|24.9|34.3"), OBFUSCATE("v3:164.5|25.4|36.4"), OBFUSCATE("v3:167.8|25.9|38.9"),
        OBFUSCATE("v3:165.7|27.1|43.2"), OBFUSCATE("v3:168.0|27.0|46.2"), OBFUSCATE("v3:166.3|26.9|48.1"),
        OBFUSCATE("v3:161.3|25.9|51.9"), OBFUSCATE("v3:161.3|25.9|53.4"), OBFUSCATE("v3:164.5|26.3|54.3"),
        OBFUSCATE("v3:159.8|25.7|55.0"), OBFUSCATE("v3:156.7|25.5|60.7"), OBFUSCATE("v3:158.3|25.6|69.5"),
        OBFUSCATE("v3:157.6|28.8|88.5"), OBFUSCATE("v3:156.7|28.7|91.3"), OBFUSCATE("v3:163.4|30.1|91.4"),
        OBFUSCATE("v3:144.6|24.4|109.0"), OBFUSCATE("v3:147.1|24.5|109.4"), OBFUSCATE("v3:147.1|24.8|114.9"),
        OBFUSCATE("v3:150.0|25.3|118.1"), OBFUSCATE("v3:166.5|23.1|126.8"), OBFUSCATE("v3:185.7|22.2|131.8"),
        OBFUSCATE("v3:188.8|22.5|142.1"), OBFUSCATE("v3:198.8|22.4|130.7"), OBFUSCATE("v3:194.9|23.3|125.8"),
        OBFUSCATE("v3:202.8|22.4|133.7"), OBFUSCATE("v3:203.0|21.8|139.1"), OBFUSCATE("v3:206.3|22.0|136.4"),
        OBFUSCATE("v3:207.8|22.1|132.2"), OBFUSCATE("v3:197.3|27.1|66.6"), OBFUSCATE("v3:194.5|25.7|58.9"),
        OBFUSCATE("v3:190.2|25.7|56.5"), OBFUSCATE("v3:193.6|25.5|55.9"), OBFUSCATE("v3:192.0|24.8|47.7"),
        OBFUSCATE("v3:184.8|25.2|38.8"), OBFUSCATE("v3:190.1|25.8|30.9"), OBFUSCATE("v3:197.2|23.2|14.1"),
        OBFUSCATE("v3:204.0|23.4|22.6"), OBFUSCATE("v3:207.5|23.0|13.1"), OBFUSCATE("v3:206.9|23.4|34.0"),
        OBFUSCATE("v3:204.4|23.4|36.3")};

std::vector<std::string> *allResourceVectors[] = {
        &oreCoords, &stoneCoords, &mandarinCoords, &raspberryCoords, &blueberryCoords, &appleCoords,
        &oysterCoords, &tallMustardCoords, &blackTruffleCoords, &fiddleheadCoords,
        &pennyBunCoords, &shitakeCoords, &burdockCoords, &mustardGreensCoords,
        &seaAsparagusCoords, &seaGrapesCoords, &wakameCoords, &premiumWoodCoords};

const char *GetCookerNameByStaticId(int staticId) {
    switch (staticId) {
        case 370001:
            return OBFUSCATE("Basic Cooker 1");
            // p_cooker_cooker_1
        case 370002:
            return OBFUSCATE("Basic Cooker 2");
            // p_cooker_cooker_2
        case 370003:
            return OBFUSCATE("Basic Cooker 3");
            // p_cooker_cooker_3
        case 370004:
            return OBFUSCATE("Basic Cooker 4");
            // p_cooker_cooker_4
        case 370005:
            return OBFUSCATE("Basic Cooker 5");
            // p_cooker_cooker_5
        case 370006:
            return OBFUSCATE("Sandshop Cooker");
            // p_cooker_season_sandshop_cooker_1
        case 370007:
            return OBFUSCATE("Oakwoods Cooker 1");
            // p_cooker_mall_oakwoods_cooker_5_1
        case 370008:
            return OBFUSCATE("Oakwoods Cooker 2");
            // p_cooker_mall_oakwoods_cooker_5_2
        case 370009:
            return OBFUSCATE("Oakwoods Cooker 3");
            // p_cooker_mall_oakwoods_cooker_5_3
        case 370010:
            return OBFUSCATE("Halloween Soup Pot");
            // p_cooker_season_halloween_cooker_cooker_1
        case 370011:
            return OBFUSCATE("New Year Ice Cream Van");
            // p_cooker_season_newyear_cooker_cooker_1
        case 370012:
            return OBFUSCATE("European Kitchen Cooker");
            // p_cooker_normal_europeankitchen_cooker_cooker_1_1
        case 370013:
            return OBFUSCATE("Springcamp Cooker");
            // p_cooker_springcamp_cooker_cooker_1
        case 370014:
            return OBFUSCATE("Springcamp Fireplace");
            // p_cooker_springcamp_fireplace_fireplace_1
        case 370015:
            return OBFUSCATE("Dessert Cooker 1");
            // p_cooker_season_dessert_cooker_cooker_1
        case 370016:
            return OBFUSCATE("Dessert Cooker 2");
            // p_cooker_season_dessert_cooker_cooker_2
        case 370017:
            return OBFUSCATE("Dessert Cooker 3");
            // p_cooker_season_dessert_cooker_cooker_3
        case 370018:
            return OBFUSCATE("Season Springcamp Cooker");
            // p_season_springcamp_cooker_1
        case 370019:
            return OBFUSCATE("Patio Cpark Cooker");
            // p_patiofurnitue_cpark_cooker_1
        case 370020:
            return OBFUSCATE("Patio Animal Cooker");
            // p_patiofurnitue_animal_cooker_1
        case 370021:
            return OBFUSCATE("Star Cooker");
            // p_cooker_star_cooker_1
        case 370022:
            return OBFUSCATE("Autumn Cooker");
            // p_cooker_autumn_cooker_1
        case 370023:
            return OBFUSCATE("Mulberry Cage");
            // p_cooker_mulberry_cooker_1
        case 370024:
            return OBFUSCATE("Winter Foilbox 2");
            // p_cooker_winter_cooker_2
        case 370025:
            return OBFUSCATE("Winter Foilbox 1");
            // p_cooker_winter_cooker_1
        case 370026:
            return OBFUSCATE("Treehouse Rabbit Pan 1");
            // p_cooker_treehouse_cooker_1
        case 370027:
            return OBFUSCATE("Treehouse Rabbit Pan 2");
            // p_cooker_treehouse_cooker_2
        case 370028:
            return OBFUSCATE("Treehouse Rabbit Pan 3");
            // p_cooker_treehouse_cooker_3
        case 370029:
            return OBFUSCATE("Movie Cooker");
            // p_cooker_movie_cooker_1
        case 370030:
            return OBFUSCATE("Bricks Cooker");
            // p_cooker_bricks_cooker_1
        default:
            return OBFUSCATE("Unknown ID Type");
    }
}

void ResetBubbleHistory() {
    g_claimedBubbles.clear();
}

void ResetBirdHistory() {
    g_processedBirds.clear();
    g_birdPhotoAttempts.clear();
    g_birdSuccessCount = 0;
    g_birdCurrentTarget = {};
    g_birdRequestPending.store(false, std::memory_order_release);
    g_birdRetryCount = 0;
    g_birdFarmStatus = u8"待机";
}

void SetGlobalTimeScale(float speed) {
    static void *(*resolve_icall)(const char *) = nullptr;
    if (!resolve_icall) {
        void *handle = xdl_open(OBFUSCATE("libil2cpp.so"), 0);
        if (handle)
            resolve_icall = (void *(*)(const char *)) xdl_sym(handle, OBFUSCATE(
                    "il2cpp_resolve_icall"), nullptr);
    }
    if (resolve_icall) {
        void *icallAddr = resolve_icall(OBFUSCATE("UnityEngine.Time::set_timeScale"));
        if (icallAddr) {
            void (*set_timeScale)(float) = (void (*)(float)) icallAddr;
            set_timeScale(speed);
        }
    }
}

uint32_t GetSeedNetId(int staticId) {
    void *img = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!img) img = _monoImageLoaded(OBFUSCATE("XDTGameSystem.dll"));
    if (!img)
        return 0;
    void *klass = _monoClassFromName(img, OBFUSCATE("XDTGameSystem.GameplaySystem.BackPack"),
                                     OBFUSCATE("BackPackSystem"));
    if (!klass)
        return 0;
    auto FindMethod = [&](void *k, const char *n, int p) -> void * {
        while (k) {
            void *m = _monoClassGetMethodFromName(k, n, p);
            if (m)
                return m;
            k = _monoClassGetParent(k);
        }
        return nullptr;
    };

    void *m_getInst = FindMethod(klass, OBFUSCATE("get_Instance"), 0);
    if (!m_getInst)
        return 0;

    void *exc = nullptr;
    void *inst = _monoRuntimeInvoke(m_getInst, nullptr, nullptr, &exc);
    if (!inst || exc)
        return 0;

    void *m_getSeed = FindMethod(klass, OBFUSCATE("GetItemNetId"), 1);
    if (!m_getSeed)
        return 0;
    void *args[1] = {&staticId};
    void *res = _monoRuntimeInvoke(m_getSeed, inst, args, &exc);
    if (res && !exc) {
        uint32_t seedId = *(uint32_t *) _monoObjectUnbox(res);
        if (seedId != 0)
            return seedId;
    }
    return 0;
}

// 把 mono 方法签名压成短串："s<static>a<argc>t<类型码...>"，用于面包屑定位。
// 侧载 iOS 上热更 Mono 程序集是加密的、dump 里没有这些类，只能运行期读签名。
static std::string CompactMethodSig(void *method) {
    if (!method) return std::to_string(0);
    if (!SMMonoHot::IsMonoMethod(method)) return std::string("il2cpp");
    uint32_t flags = SMMonoHot::MethodGetFlags(method, nullptr);
    const bool isStatic = (flags & 0x10u) != 0;   // MONO_METHOD_ATTR_STATIC
    int argc = 0;
    char types[24] = {0};
    void *sig = SMMonoHot::MethodSignature(method);
    if (sig) {
        void *iter = nullptr;
        while (void *type = SMMonoHot::SignatureGetParams(sig, &iter)) {
            const int t = SMMonoHot::TypeGetType(type);
            if (argc < 12) types[argc] = "0123456789abcdef"[(t >> 0) & 0xF];
            ++argc;
            if (argc >= 16) break;
        }
    }
    char buf[80];
    snprintf(buf, sizeof(buf), "s%da%dt%s", isStatic ? 1 : 0, argc, types);
    return std::string(buf);
}


void ScanPotsNative() {
    SMPlatform::SetBreadcrumbLight("scan:pots");
    if (g_gardenPlayerNetId == 0) GetPlayerNetIdOnUnityThread();
    if (g_gardenPlayerNetId == 0) return;

    g_scannedPots.clear();
    g_scannedPlants.clear();

    _monoThreadAttach(_monoGetRootDomain());

    SMPlatform::SetBreadcrumbLight("scan:img");
    void *imgData     = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol.dll"));
    void *imgGameplay = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
    if (!imgGameplay) imgGameplay = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity.dll"));
    if (!imgData || !imgGameplay) return;

    const char *potCandidates[] = { "CropBoxComponent", "CropComponent" };
    void *potKlass = nullptr;
    for (const char* nm : potCandidates) {
        potKlass = _monoClassFromName(imgGameplay,
            (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Homeland"), nm);
        if (potKlass) break;
    }
    if (!potKlass) return;

    SMPlatform::SetBreadcrumbLight("scan:potclass");
    void *potType    = _monoClassGetType(potKlass);
    void *worldKlass = _monoClassFromName(imgData,
        (const char *) OBFUSCATE("XDTLevelAndEntity.Core.World"), (const char *) OBFUSCATE("EntityWorld"));
    if (!worldKlass) worldKlass = _monoClassFromName(imgGameplay,
        (const char *) OBFUSCATE("XDTLevelAndEntity.Core.World"), (const char *) OBFUSCATE("EntityWorld"));
    if (!worldKlass) worldKlass = _monoClassFromName(imgData,
        (const char *) OBFUSCATE("XDTGame.GAS.Client"), (const char *) OBFUSCATE("EntityWorld"));

    SMPlatform::SetBreadcrumbLight("scan:world");
    void *openMethod = worldKlass ? FindMethodInHierarchy(worldKlass, (const char *) OBFUSCATE("GetComponentSystem"), 0) : nullptr;
    if (!openMethod) {
        SMPlatform::SetBreadcrumbLight("scan:no-open");
        return;
    }
    SMPlatform::SetBreadcrumbLight("scan:sig:" + CompactMethodSig(openMethod));

    void *typeArgs[1] = { potType };
    void *mi = _mono_metadata_get_generic_inst(1, typeArgs);
    MonoGenericContext ctx; ctx.class_inst = nullptr; ctx.method_inst = mi;
    void *closed = _mono_class_inflate_generic_method(openMethod, &ctx);
    if (!closed) {
        SMPlatform::SetBreadcrumbLight("scan:no-inflate");
        return;
    }
    SMPlatform::SetBreadcrumbLight("scan:inflated");
    _monoCompileMethod(closed);   // mono 解释器模式：膨胀泛型必须先编译

    SMPlatform::SetBreadcrumbLight("scan:compsys");
    // mono 下如果 GetComponentSystem 是实例方法，传 nullptr 会直接 SIGSEGV；
    // 先读签名/静态标志，实例方法就去拿 EntityWorld 单例，拿不到就跳过。
    bool needWorldInstance = false;
    if (SMMonoHot::IsMonoMethod(closed)) {
        const uint32_t cmf = SMMonoHot::MethodGetFlags(closed, nullptr);
        needWorldInstance = (cmf & 0x10u) == 0;
    }
    SMPlatform::SetBreadcrumbLight(std::string("scan:csig:") + CompactMethodSig(closed) +
                                   (needWorldInstance ? ":i" : ":s"));
    void *worldInst = nullptr;
    if (needWorldInstance) {
        static const char *kInstAccessors[] = {
            "get_Instance", "get_instance", "get_Current", "get_CurrentWorld",
            "get_Default", "get_Main", "GetInstance", "GetWorld"
        };
        for (const char *acc : kInstAccessors) {
            void *m = worldKlass ? FindMethodInHierarchy(worldKlass, acc, 0) : nullptr;
            if (!m) continue;
            void *aexc = nullptr;
            worldInst = _monoRuntimeInvoke(m, nullptr, nullptr, &aexc);
            if (worldInst && !aexc) break;
            worldInst = nullptr;
        }
        if (!worldInst) {
            SMPlatform::SetBreadcrumbLight("scan:no-inst");
            return;
        }
    }
    SMPlatform::SetBreadcrumbLight("scan:compsys");
    void *exc = nullptr;
    void *sysObj = _monoRuntimeInvoke(closed, worldInst, nullptr, &exc);
    if (exc || !sysObj) return;

    void *sysKlass = _monoObjectGetClass(sysObj);
    void *f_components = FindFieldInHierarchy(sysKlass, (const char *) OBFUSCATE("_components"));
    if (!f_components) return;
    void *dictObj = nullptr;
    _monoFieldGetValue(sysObj, f_components, &dictObj);
    if (!dictObj) return;

    void *dictKlass = _monoObjectGetClass(dictObj);
    void *f_entries = FindFieldInHierarchy(dictKlass, (const char *) OBFUSCATE("entries"));
    if (!f_entries) f_entries = FindFieldInHierarchy(dictKlass, (const char *) OBFUSCATE("_entries"));
    if (!f_entries) return;
    void *entriesArr = nullptr;
    _monoFieldGetValue(dictObj, f_entries, &entriesArr);
    if (!entriesArr) return;

    SMPlatform::SetBreadcrumbLight("scan:entries");
    uintptr_t arrAddr = (uintptr_t)entriesArr;
    int length = 0;
    uintptr_t data = 0;
    // 两套运行时的数组/字典布局不一样：
    //   il2cpp: length @ +0x18, data @ +0x20（固定偏移）
    //   mono  : 必须用 mono_array_length / mono_array_addr_with_size
    // 原来一律按 il2cpp 偏移读 mono 数组 → length 变天文数字 → 越界读崩
    // （“扫描灶台”崩溃的根因）。
    // dictObj 是 mono 对象时，ClassifyObject 会给出 mono class；句柄表未必登记，
    // 所以两个条件都试。
    void *monoDictKlass = nullptr;
    const bool monoDict = SMMonoHot::ClassifyObject(dictObj, &monoDictKlass) ||
                          SMMonoHot::IsMonoClass(dictKlass);
    if (monoDict && SMMonoHot::Available()) {
        length = SMMonoHot::ArrayLength(entriesArr);
        void *first = SMMonoHot::ArrayAddrWithSize(entriesArr, 24, 0);
        data = (uintptr_t)first;
        if (length <= 0 || length > 100000 || !first) {
            SMPlatform::SetBreadcrumbLight("scan:badlen");
            return;
        }
    } else {
        length = *(int *)((uint8_t *) entriesArr + OFF_ARR_COUNT);
        data = arrAddr + OFF_ARR_DATA;
        if (length <= 0 || length > 100000) return;
    }

    void *m_getEntity = FindMethodInHierarchy(potKlass, (const char *) OBFUSCATE("get_entity"), 0);
    if (!m_getEntity) m_getEntity = FindMethodInHierarchy(potKlass, (const char *) OBFUSCATE("get_Entity"), 0);

    static bool s_bFalse = false;
    void *compArgs[] = { &s_bFalse };
    void *m_getBuildComp = nullptr;
    void *m_getChild = nullptr;

    SMPlatform::SetBreadcrumbLight("scan:loop");
    int foundPots = 0;
    for (int i = 0; i < length; i++) {
        int hash = *(int *)(data + (i * 24));
        if (hash < 0) continue;

        void *buildComp = *(void **)(data + (i * 24) + 16);
        if (!buildComp) continue;

        exc = nullptr;
        void *entityObj = m_getEntity ? _monoRuntimeInvoke(m_getEntity, buildComp, nullptr, &exc) : nullptr;
        if (!entityObj || exc) continue;

        void *entKlass = _monoObjectGetClass(entityObj);
        void *m_getNetId = FindMethodInHierarchy(entKlass, (const char *) OBFUSCATE("get_netId"), 0);
        exc = nullptr;
        void *netIdRes = m_getNetId ? _monoRuntimeInvoke(m_getNetId, entityObj, nullptr, &exc) : nullptr;
        uint32_t potNetId = netIdRes ? *(uint32_t *) _monoObjectUnbox(netIdRes) : 0;
        if (potNetId == 0) continue;

        Vector3 pos = {0, 0, 0};
        void *m_getLocalPos = FindMethodInHierarchy(entKlass, (const char *) OBFUSCATE("get_localPosition"), 0);
        if (m_getLocalPos) {
            exc = nullptr;
            void *posRes = _monoRuntimeInvoke(m_getLocalPos, entityObj, nullptr, &exc);
            if (posRes) memcpy(&pos, _monoObjectUnbox(posRes), sizeof(Vector3));
        }

        if (!m_getBuildComp) {
            void *m_GetComponent = FindMethodInHierarchy(entKlass, OBFUSCATE("GetComponent"), 1);
            if (m_GetComponent) {
                void *bcKlass = _monoClassFromName(imgGameplay,
                    OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Homeland"), OBFUSCATE("BuildComponent"));
                if (bcKlass) {
                    void *bcType = _monoClassGetType(bcKlass);
                    void *bcTypeArgs[] = { bcType };
                    void *bcInst = _mono_metadata_get_generic_inst(1, bcTypeArgs);
                    MonoGenericContext bcCtx = { nullptr, bcInst };
                    m_getBuildComp = _mono_class_inflate_generic_method(m_GetComponent, &bcCtx);
                }
            }
        }

        uint32_t cId = 0;
        if (m_getBuildComp) {
            exc = nullptr;
            void *buildCompInst = _monoRuntimeInvoke(m_getBuildComp, entityObj, compArgs, &exc);
            if (buildCompInst && !exc) {
                if (!m_getChild) {
                    void *bcClass = _monoObjectGetClass(buildCompInst);
                    m_getChild = FindMethodInHierarchy(bcClass, OBFUSCATE("GetFirstChildrenNetId"), 0);
                }
                if (m_getChild) {
                    exc = nullptr;
                    void *cIdObj = _monoRuntimeInvoke(m_getChild, buildCompInst, nullptr, &exc);
                    if (cIdObj && !exc) cId = *(uint32_t *) _monoObjectUnbox(cIdObj);
                }
            }
        }

        uint64_t correctServerId = ((uint64_t)potNetId & 0xFFFFFFFFULL) | ((uint64_t)2 << 32);

        PotTarget target;
        target.netId = potNetId;
        target.childId = cId;
        target.position = pos;
        target.serverUlong = correctServerId;

        g_scannedPots.push_back(target);
        g_scannedPlants.push_back(potNetId);
        foundPots++;
    }
}

void ExecuteAutoHarvest() {
    if (g_scannedPots.empty())
        return;
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (!img)
        return;

    void *klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Plant"),
                                     OBFUSCATE("CropProtocolManager"));
    if (!klass)
        return;

    void *m_TryGetProgress = FindMethodInHierarchy(klass, OBFUSCATE("TryGetProgress"), 1);
    void *m_PickPlant = FindMethodInHierarchy(klass, OBFUSCATE("PickPlant"), 1);
    if (!m_TryGetProgress || !m_PickPlant)
        return;

    for (auto &pot: g_scannedPots) {
        if (pot.childId == 0 || pot.childId == 999999)
            continue;
        uint32_t childIdVal = pot.childId;
        void *args[1] = {&childIdVal};

        void *exc = nullptr;
        void *progRes = _monoRuntimeInvoke(m_TryGetProgress, nullptr, args, &exc);
        if (progRes && !exc) {
            void *unboxed = _monoObjectUnbox(progRes);
            int rawInt = *(int *) unboxed;
            float progress = *(float *) &rawInt;
            if (progress >= 1.0f) {
                _monoRuntimeInvoke(m_PickPlant, nullptr, args, &exc);
            }
        }
    }
}

void ExecutePlanting() {
    if (g_gardenPlayerNetId == 0)
        return;
    std::vector<PotTarget> emptyPots;
    for (auto &p: g_scannedPots) {
        if (p.childId == 0)
            emptyPots.push_back(p);
    }
    if (emptyPots.empty())
        return;
    uint32_t seedId = 0;
    RunOnUnityThreadSync([&]() { seedId = GetSeedNetId(g_seedStaticId); });
    if (seedId == 0)
        return;
    for (auto &pot: emptyPots) {
        if (!g_isGardenBotRunning.load(std::memory_order_acquire))
            break;
        RunOnUnityThreadSync([&]() {
            void *protocolImg = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
            if (!protocolImg) return;

            void *protocolClass = _monoClassFromName(protocolImg, OBFUSCATE(
                    "XDTDataAndProtocol.ProtocolService.Plant"), OBFUSCATE("CropProtocolManager"));
            if (!protocolClass) return;

            void *method = FindMethodInHierarchy(protocolClass, OBFUSCATE("CropSeeding"), 2);
            if (!method) method = FindMethodInHierarchy(protocolClass, OBFUSCATE("CropSeeding"), 3);
            if (!method) method = FindMethodInHierarchy(protocolClass, OBFUSCATE("CropSeeding"), 1);
            if (!method) method = FindMethodInHierarchy(protocolClass, OBFUSCATE("CropSeeding"), -1);
            if (!method) return;

            void *sig = _monoMethodSignature(method);
            if (!sig) return;

            void *listKlass = nullptr;
            void *iter = nullptr;
            void *pType = nullptr;
            while ((pType = _monoSignatureGetParams(sig, &iter)) != nullptr) {
                void *k = _monoClassFromMonoType(pType);
                if (k && FindMethodInHierarchy(k, OBFUSCATE("Add"), 1)) {
                    listKlass = k;
                    break;
                }
            }
            if (!listKlass) return;

            void *listObj = _monoObjectNew(_monoGetRootDomain(), listKlass);
            if (!listObj) return;

            void *m_ctor = FindMethodInHierarchy(listKlass, OBFUSCATE(".ctor"), 0);
            if (m_ctor) _monoRuntimeInvoke(m_ctor, listObj, nullptr, nullptr);

            void *m_add = FindMethodInHierarchy(listKlass, OBFUSCATE("Add"), 1);
            if (!m_add) return;

            CropPlantPoint pointData;
            pointData.pos = {pot.position.x, pot.position.y + 0.06f, pot.position.z};
            pointData.angle = 0;
            pointData.levelObjectNetId = pot.serverUlong;

            void *addArgs[1] = {&pointData};
            _monoRuntimeInvoke(m_add, listObj, addArgs, nullptr);

            uint32_t sIdVal = seedId;
            void *pArgs[2] = {&sIdVal, listObj};
            _monoRuntimeInvoke(method, nullptr, pArgs, nullptr);
        });
        for (auto &p2: g_scannedPots) {
            if (p2.netId == pot.netId)
                p2.childId = 999999;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void AutoGardenBotThread() {
    struct GardenOperationReset {
        ~GardenOperationReset() {
            g_isGardenBotRunning.store(false, std::memory_order_release);
            int expected = 1;
            g_gardenOperation.compare_exchange_strong(
                expected, 0, std::memory_order_acq_rel);
            g_botStatus = (const char *) OBFUSCATE("IDLE");
        }
    } operationReset;

    g_botStatus = (const char *) OBFUSCATE("BOT STARTED");
    if (g_gardenPlayerNetId == 0)
        GetPlayerNetIdNative();
    if (g_gardenPlayerNetId == 0) {
        g_botStatus = (const char *) OBFUSCATE("ERR: NO PLAYER NETID");
        return;
    }

    while (g_isGardenBotRunning.load(std::memory_order_acquire)) {
        g_botStatus = (const char *) OBFUSCATE("SCANNING POTS...");
        RunOnUnityThreadSync([]() { ScanPotsNative(); });
        if (!g_isGardenBotRunning)
            break;
        SafeBotSleep(1.0f);

        g_botStatus = (const char *) OBFUSCATE("HARVESTING...");
        RunOnUnityThreadSync([]() { ExecuteAutoHarvest(); });
        if (!g_isGardenBotRunning)
            break;
        SafeBotSleep(2.0f);
        g_botStatus = (const char *) OBFUSCATE("UPDATING EMPTY SLOTS...");
        RunOnUnityThreadSync([]() { ScanPotsNative(); });
        if (!g_isGardenBotRunning)
            break;
        SafeBotSleep(1.0f);

        if (g_seedStaticId <= 0) {
            g_botStatus = (const char *) OBFUSCATE("HARVEST ONLY (NO SEED SELECTED)");
        } else {
            g_botStatus = (const char *) OBFUSCATE("CHECKING SEEDS...");
            uint32_t seedId = 0;
            RunOnUnityThreadSync([&]() { seedId = GetSeedNetId(g_seedStaticId); });
            if (seedId == 0) {
                g_botStatus = (const char *) OBFUSCATE("SEED EMPTY / NOT IN BAG");
            } else {
                g_botStatus = (const char *) OBFUSCATE("PLANTING...");
                ExecutePlanting();
                if (!g_isGardenBotRunning)
                    break;
            }
        }

        g_botStatus = (const char *) OBFUSCATE("WAITING (15s)...");
        SafeBotSleep(15.0f);
    }
}

struct WaterTarget {
    uint32_t netId = 0;
    uint32_t childId = 0;
    uint32_t ownerId = 0;
    bool isPlant = false; // false = CropBoxComponent, true = PlantComponent
};

static void* GetComponentDataFromComponent(void* compObj) {
    if (!compObj || !IsValidMonoPointer(compObj)) return nullptr;
    void* compKlass = _monoObjectGetClass(compObj);
    if (!compKlass || !IsValidMonoPointer(compKlass)) return nullptr;

    const char* propNames[] = { "get_ComponentData", "get__componentData", "get_componentData", "GetComponentData" };
    for (const char* pname : propNames) {
        void* m_get = FindMethodInHierarchy(compKlass, pname, 0);
        if (m_get) {
            void* exc = nullptr;
            void* valObj = _monoRuntimeInvoke(m_get, compObj, nullptr, &exc);
            if (valObj && !exc && IsValidMonoPointer(valObj)) return valObj;
        }
    }

    void* f_data = FindFieldInHierarchy(compKlass, "_componentData");
    if (!f_data) f_data = FindFieldInHierarchy(compKlass, "componentData");
    if (f_data) {
        void* valObj = nullptr;
        if (_monoFieldGetValueObject) {
            valObj = _monoFieldGetValueObject(_monoGetRootDomain(), f_data, compObj);
        }
        if (!valObj) {
            _monoFieldGetValue(compObj, f_data, &valObj);
        }
        if (valObj && IsValidMonoPointer(valObj)) return valObj;
    }
    return nullptr;
}

static bool TryReadMonoBoolFieldOrProp(void* obj, const std::vector<std::string>& names, bool& outVal) {
    if (!obj || !IsValidMonoPointer(obj)) return false;
    void* klass = _monoObjectGetClass(obj);
    if (!klass || !IsValidMonoPointer(klass)) return false;
    for (const auto& name : names) {
        void* f = FindFieldInHierarchy(klass, name.c_str());
        if (f) {
            uint8_t b = 0;
            _monoFieldGetValue(obj, f, &b);
            outVal = (b != 0);
            return true;
        }
        std::string getterName = "get_" + name;
        void* m = FindMethodInHierarchy(klass, getterName.c_str(), 0);
        if (m) {
            void* exc = nullptr;
            void* res = _monoRuntimeInvoke(m, obj, nullptr, &exc);
            if (res && !exc && IsValidMonoPointer(res)) {
                void* unboxed = _monoObjectUnbox(res);
                if (unboxed) {
                    outVal = (*(uint8_t*)unboxed != 0);
                    return true;
                }
            }
        }
    }
    return false;
}

static bool TryReadMonoUIntFieldOrProp(void* obj, const std::vector<std::string>& names, uint32_t& outVal) {
    if (!obj || !IsValidMonoPointer(obj)) return false;
    void* klass = _monoObjectGetClass(obj);
    if (!klass || !IsValidMonoPointer(klass)) return false;
    for (const auto& name : names) {
        void* f = FindFieldInHierarchy(klass, name.c_str());
        if (f) {
            uint32_t v = 0;
            _monoFieldGetValue(obj, f, &v);
            if (v != 0) {
                outVal = v;
                return true;
            }
        }
        std::string getterName = "get_" + name;
        void* m = FindMethodInHierarchy(klass, getterName.c_str(), 0);
        if (m) {
            void* exc = nullptr;
            void* res = _monoRuntimeInvoke(m, obj, nullptr, &exc);
            if (res && !exc && IsValidMonoPointer(res)) {
                void* unboxed = _monoObjectUnbox(res);
                if (unboxed && *(uint32_t*)unboxed != 0) {
                    outVal = *(uint32_t*)unboxed;
                    return true;
                }
            }
        }
    }
    return false;
}

void ExecuteWateringAll() {
    struct GardenOperationReset {
        ~GardenOperationReset() {
            int expected = 2;
            g_gardenOperation.compare_exchange_strong(
                expected, 0, std::memory_order_acq_rel);
        }
    } operationReset;

    std::vector<WaterTarget> targetsToWater;
    uint32_t defaultOwnerId = 0;
    void* listKlass = nullptr;
    void* m_ctor = nullptr;
    void* m_add = nullptr;
    void* cropWaterMethod = nullptr;
    void* plantWaterMethod = nullptr;
    int pCountCrop = 0;
    int pCountPlant = 0;

    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());

        if (g_gardenPlayerNetId == 0) GetPlayerNetIdOnUnityThread();

        // Resolve player & field owner netId
        uint32_t fieldOwnerNetId = 0;
        uint32_t playerEntityNetId = 0;
        void* selfPlayer = nullptr;
        void* imgGameplay = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
        if (!imgGameplay) imgGameplay = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
        if (imgGameplay) {
            void* utilKlass = _monoClassFromName(imgGameplay,
                OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
            if (utilKlass) {
                void* m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
                if (m_getSelf) {
                    void* exc = nullptr;
                    selfPlayer = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, &exc);
                }
            }
        }

        if (selfPlayer && IsValidMonoPointer(selfPlayer)) {
            void* selfKlass = _monoObjectGetClass(selfPlayer);
            if (selfKlass) {
                void* m_getFieldOwner = FindMethodInHierarchy(selfKlass, OBFUSCATE("get_inFieldOwnerId"), 0);
                if (m_getFieldOwner) {
                    void* exc = nullptr;
                    void* foObj = _monoRuntimeInvoke(m_getFieldOwner, selfPlayer, nullptr, &exc);
                    if (foObj && !exc) fieldOwnerNetId = *(uint32_t*)_monoObjectUnbox(foObj);
                }
                void* m_getEnt = FindMethodInHierarchy(selfKlass, OBFUSCATE("get_entity"), 0);
                if (m_getEnt) {
                    void* exc = nullptr;
                    void* entObj = _monoRuntimeInvoke(m_getEnt, selfPlayer, nullptr, &exc);
                    if (entObj && !exc && IsValidMonoPointer(entObj)) {
                        void* entKlass = _monoObjectGetClass(entObj);
                        if (entKlass) {
                            void* m_getNet = FindMethodInHierarchy(entKlass, OBFUSCATE("get_netId"), 0);
                            if (m_getNet) {
                                exc = nullptr;
                                void* nObj = _monoRuntimeInvoke(m_getNet, entObj, nullptr, &exc);
                                if (nObj && !exc) playerEntityNetId = *(uint32_t*)_monoObjectUnbox(nObj);
                            }
                        }
                    }
                }
            }
        }

        defaultOwnerId = (fieldOwnerNetId != 0) ? fieldOwnerNetId :
                         ((playerEntityNetId != 0) ? playerEntityNetId : g_gardenPlayerNetId);

        Vector3 playerPos = { 0, 0, 0 };
        bool hasPlayerPos = TryGetLocalPlayerPosition(playerPos);

        // Optional EntityHelper.GetLevelObjectOwner
        void* helperKlass = imgGameplay ? _monoClassFromName(imgGameplay, OBFUSCATE("XDTLevelAndEntity.Utils"), OBFUSCATE("EntityHelper")) : nullptr;
        void* m_getLevelObjectOwner = helperKlass ? FindMethodInHierarchy(helperKlass, OBFUSCATE("GetLevelObjectOwner"), 1) : nullptr;

        // 1. Scan CropBoxComponent / CropComponent (Pots / Farm plots)
        const char* potCandidates[] = { "CropBoxComponent", "CropComponent" };
        void* potKlass = nullptr;
        for (const char* nm : potCandidates) {
            potKlass = _monoClassFromName(imgGameplay,
                OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Homeland"), nm);
            if (potKlass) break;
        }

        // BuildComponent helper for child ID of pots
        void* m_getBuildComp = nullptr;
        void* m_getChild = nullptr;
        static bool s_bFalse = false;
        void* compArgs[] = { &s_bFalse };

        if (potKlass) {
            auto potComps = GetComponentsOfClass(potKlass);
            for (void* comp : potComps) {
                if (!comp || !IsValidMonoPointer(comp)) continue;
                uint32_t potNetId = ReadComponentNetId(comp);
                if (potNetId == 0) continue;

                // Read isWet from CropBoxItemData
                void* dataObj = GetComponentDataFromComponent(comp);
                bool isWet = false;
                if (dataObj) {
                    TryReadMonoBoolFieldOrProp(dataObj, { "isWet", "_isWet", "IsWet" }, isWet);
                }
                if (isWet) {
                    continue; // Sudah basah / tersiram -> lewati
                }

                // Check distance
                Vector3 pos = { 0, 0, 0 };
                void* entObj = GetEntityFromComponent(comp);
                if (entObj && IsValidMonoPointer(entObj)) {
                    pos = NativeGetPosition(entObj);
                }
                if (hasPlayerPos && (pos.x != 0.0f || pos.y != 0.0f || pos.z != 0.0f)) {
                    if (GetVector3Distance(playerPos, pos) > 80.0f) {
                        continue;
                    }
                }

                // Read child ID
                uint32_t cId = 0;
                if (entObj && IsValidMonoPointer(entObj)) {
                    if (!m_getBuildComp && _mono_class_inflate_generic_method && _mono_metadata_get_generic_inst) {
                        void* entKlass = _monoObjectGetClass(entObj);
                        void* m_GetComponent = entKlass ? FindMethodInHierarchy(entKlass, OBFUSCATE("GetComponent"), 1) : nullptr;
                        if (m_GetComponent) {
                            void* bcKlass = _monoClassFromName(imgGameplay,
                                OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Homeland"), OBFUSCATE("BuildComponent"));
                            if (bcKlass && _monoClassGetType) {
                                void* bcType = _monoClassGetType(bcKlass);
                                void* bcTypeArgs[] = { bcType };
                                void* bcInst = _mono_metadata_get_generic_inst(1, bcTypeArgs);
                                MonoGenericContext bcCtx = { nullptr, bcInst };
                                m_getBuildComp = _mono_class_inflate_generic_method(m_GetComponent, &bcCtx);
                            }
                        }
                    }
                    if (m_getBuildComp) {
                        void* exc = nullptr;
                        void* buildCompInst = _monoRuntimeInvoke(m_getBuildComp, entObj, compArgs, &exc);
                        if (buildCompInst && !exc && IsValidMonoPointer(buildCompInst)) {
                            if (!m_getChild) {
                                void* bcClass = _monoObjectGetClass(buildCompInst);
                                m_getChild = FindMethodInHierarchy(bcClass, OBFUSCATE("GetFirstChildrenNetId"), 0);
                            }
                            if (m_getChild) {
                                exc = nullptr;
                                void* cIdObj = _monoRuntimeInvoke(m_getChild, buildCompInst, nullptr, &exc);
                                if (cIdObj && !exc) cId = *(uint32_t*)_monoObjectUnbox(cIdObj);
                            }
                        }
                    }
                }

                // Resolve owner
                uint32_t ownerId = 0;
                if (dataObj) {
                    TryReadMonoUIntFieldOrProp(dataObj, { "ownerId", "_ownerId", "OwnerId", "ownerNetId", "_ownerNetId", "OwnerNetId" }, ownerId);
                }
                if (ownerId == 0 && m_getLevelObjectOwner) {
                    uint64_t sUlong = ((uint64_t)potNetId & 0xFFFFFFFFULL) | ((uint64_t)2 << 32);
                    void* sArgs[] = { &sUlong };
                    void* exc = nullptr;
                    void* oObj = _monoRuntimeInvoke(m_getLevelObjectOwner, nullptr, sArgs, &exc);
                    if (oObj && !exc) ownerId = *(uint32_t*)_monoObjectUnbox(oObj);
                }

                WaterTarget wt;
                wt.netId = potNetId;
                wt.childId = cId;
                wt.ownerId = (ownerId != 0) ? ownerId : defaultOwnerId;
                wt.isPlant = false;
                targetsToWater.push_back(wt);
            }
        }

        // 2. Scan PlantComponent (Ground flowers / plants)
        void* plantKlass = _monoClassFromName(imgGameplay,
            OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Homeland"), OBFUSCATE("PlantComponent"));
        void* furnitureKlass = _monoClassFromName(imgGameplay,
            OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Homeland"), OBFUSCATE("FurnitureBaseComponent"));
        void* furnitureField = plantKlass ? FindFieldInHierarchy(plantKlass, OBFUSCATE("furnitureBase")) : nullptr;
        if (!furnitureField && plantKlass) furnitureField = FindFieldInHierarchy(plantKlass, OBFUSCATE("_furnitureBase"));
        void* m_getOwnerId = furnitureKlass ? FindMethodInHierarchy(furnitureKlass, OBFUSCATE("get_ownerId"), 0) : nullptr;
        if (!m_getOwnerId && furnitureKlass) m_getOwnerId = FindMethodInHierarchy(furnitureKlass, OBFUSCATE("get_OwnerId"), 0);

        if (plantKlass) {
            auto plantComps = GetComponentsOfClass(plantKlass);
            for (void* comp : plantComps) {
                if (!comp || !IsValidMonoPointer(comp)) continue;
                uint32_t plantNetId = ReadComponentNetId(comp);
                if (plantNetId == 0) continue;

                // Read status: masterWater and weatherWater
                void* dataObj = GetComponentDataFromComponent(comp);
                bool masterWater = false;
                bool weatherWater = false;
                if (dataObj) {
                    TryReadMonoBoolFieldOrProp(dataObj, { "masterWater", "_masterWater", "MasterWater" }, masterWater);
                    TryReadMonoBoolFieldOrProp(dataObj, { "weatherWater", "_weatherWater", "WeatherWater" }, weatherWater);
                }

                // If masterWater or weatherWater is true -> already watered! Skip!
                if (masterWater || weatherWater) {
                    continue;
                }

                // Check distance
                Vector3 pos = { 0, 0, 0 };
                void* entObj = GetEntityFromComponent(comp);
                if (entObj && IsValidMonoPointer(entObj)) {
                    pos = NativeGetPosition(entObj);
                }
                if (hasPlayerPos && (pos.x != 0.0f || pos.y != 0.0f || pos.z != 0.0f)) {
                    if (GetVector3Distance(playerPos, pos) > 80.0f) {
                        continue;
                    }
                }

                // Resolve owner
                uint32_t ownerId = 0;
                if (furnitureField && m_getOwnerId) {
                    void* furniture = nullptr;
                    _monoFieldGetValue(comp, furnitureField, &furniture);
                    if (furniture && IsValidMonoPointer(furniture)) {
                        void* exc = nullptr;
                        void* oObj = _monoRuntimeInvoke(m_getOwnerId, furniture, nullptr, &exc);
                        if (oObj && !exc) ownerId = *(uint32_t*)_monoObjectUnbox(oObj);
                    }
                }
                if (ownerId == 0 && dataObj) {
                    TryReadMonoUIntFieldOrProp(dataObj, { "ownerId", "_ownerId", "OwnerId", "ownerNetId", "_ownerNetId", "OwnerNetId" }, ownerId);
                }
                if (ownerId == 0 && m_getLevelObjectOwner) {
                    uint64_t sUlong = ((uint64_t)plantNetId & 0xFFFFFFFFULL) | ((uint64_t)2 << 32);
                    void* sArgs[] = { &sUlong };
                    void* exc = nullptr;
                    void* oObj = _monoRuntimeInvoke(m_getLevelObjectOwner, nullptr, sArgs, &exc);
                    if (oObj && !exc) ownerId = *(uint32_t*)_monoObjectUnbox(oObj);
                }

                WaterTarget wt;
                wt.netId = plantNetId;
                wt.childId = 0;
                wt.ownerId = (ownerId != 0) ? ownerId : defaultOwnerId;
                wt.isPlant = true;
                targetsToWater.push_back(wt);
            }
        }

        // Resolve protocol methods
        void* protocolImg = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        if (!protocolImg) protocolImg = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
        if (!protocolImg) return;

        void* cropProtocolClass = _monoClassFromName(protocolImg,
            OBFUSCATE("XDTDataAndProtocol.ProtocolService.Plant"), OBFUSCATE("CropProtocolManager"));
        void* plantProtocolClass = _monoClassFromName(protocolImg,
            OBFUSCATE("XDTDataAndProtocol.ProtocolService.Plant"), OBFUSCATE("PlantProtocolManager"));

        auto findWaterMethod = [](void* klass) -> void* {
            if (!klass) return nullptr;
            void* m = FindMethodInHierarchy(klass, OBFUSCATE("WaterPlant"), 3);
            if (!m) m = FindMethodInHierarchy(klass, OBFUSCATE("WaterPlant"), 2);
            if (!m) m = FindMethodInHierarchy(klass, OBFUSCATE("WaterPlant"), 1);
            if (!m) m = FindMethodInHierarchy(klass, OBFUSCATE("WaterPlant"), -1);
            return m;
        };

        cropWaterMethod = findWaterMethod(cropProtocolClass);
        plantWaterMethod = findWaterMethod(plantProtocolClass);

        void* targetMethod = cropWaterMethod ? cropWaterMethod : plantWaterMethod;
        if (!targetMethod) return;

        void* sig = _monoMethodSignature(targetMethod);
        if (!sig) return;

        void* iter = nullptr;
        void* pType = nullptr;
        while ((pType = _monoSignatureGetParams(sig, &iter)) != nullptr) {
            void* k = _monoClassFromMonoType(pType);
            if (k && FindMethodInHierarchy(k, OBFUSCATE("Add"), 1)) {
                listKlass = k;
                break;
            }
        }
        if (!listKlass) return;

        m_ctor = FindMethodInHierarchy(listKlass, OBFUSCATE(".ctor"), 0);
        m_add = FindMethodInHierarchy(listKlass, OBFUSCATE("Add"), 1);

        auto getParamCount = [](void* m) -> int {
            if (!m) return 0;
            void* ms = _monoMethodSignature(m);
            if (!ms) return 3;
            int c = 0;
            void* it = nullptr;
            while (_monoSignatureGetParams(ms, &it) != nullptr) c++;
            return (c == 0) ? 3 : c;
        };

        pCountCrop = getParamCount(cropWaterMethod);
        pCountPlant = getParamCount(plantWaterMethod);
    });

    if (targetsToWater.empty()) return;
    if (!listKlass || !m_add || (!cropWaterMethod && !plantWaterMethod)) return;

    int plantMode = 0;
    for (size_t i = 0; i < targetsToWater.size(); ++i) {
        const auto& target = targetsToWater[i];
        uint32_t finalOwnerId = (target.ownerId != 0) ? target.ownerId : defaultOwnerId;

        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());

            auto sendSingle = [&](uint32_t targetId, void* wMethod, int pCount) {
                if (!wMethod || targetId == 0) return;
                void* listObj = _monoObjectNew(_monoGetRootDomain(), listKlass);
                if (!listObj) return;
                void* exc = nullptr;
                if (m_ctor) _monoRuntimeInvoke(m_ctor, listObj, nullptr, &exc);

                void* addArgs[] = { &targetId };
                exc = nullptr;
                _monoRuntimeInvoke(m_add, listObj, addArgs, &exc);

                exc = nullptr;
                if (pCount >= 3) {
                    void* wArgs[] = { &finalOwnerId, listObj, &plantMode };
                    _monoRuntimeInvoke(wMethod, nullptr, wArgs, &exc);
                } else if (pCount == 2) {
                    void* wArgs[] = { &finalOwnerId, listObj };
                    _monoRuntimeInvoke(wMethod, nullptr, wArgs, &exc);
                } else {
                    void* wArgs[] = { listObj };
                    _monoRuntimeInvoke(wMethod, nullptr, wArgs, &exc);
                }
            };

            if (target.isPlant) {
                if (plantWaterMethod) {
                    sendSingle(target.netId, plantWaterMethod, pCountPlant);
                }
            } else {
                if (cropWaterMethod) {
                    if (target.netId != 0) sendSingle(target.netId, cropWaterMethod, pCountCrop);
                    if (target.childId != 0 && target.childId != 999999) sendSingle(target.childId, cropWaterMethod, pCountCrop);
                }
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static void* GetStaticTableDictionary(void* tableDataClass, const char* fieldName) {
    if (!tableDataClass || !fieldName || !*fieldName) return nullptr;
    void* dictionary = nullptr;
    std::string getter = std::string("get_") + fieldName;
    void* method = FindMethodInHierarchy(tableDataClass, getter.c_str(), 0);
    if (method) {
        void* exc = nullptr;
        dictionary = _monoRuntimeInvoke(method, nullptr, nullptr, &exc);
        if (!exc && dictionary) return dictionary;
    }
    void* field = FindFieldInHierarchy(tableDataClass, fieldName);
    if (!field) return nullptr;
    void* vtable = _monoClassGetVTable(_monoGetRootDomain(), tableDataClass);
    if (vtable) _monoFieldStaticGetValue(vtable, field, &dictionary);
    return dictionary;
}

static void* FindIntDictionaryValue(void* dictionary, int wantedKey) {
    if (!dictionary) return nullptr;
    void* dictClass = _monoObjectGetClass(dictionary);
    void* entriesField = FindFieldInHierarchy(dictClass, OBFUSCATE("_entries"));
    if (!entriesField) entriesField = FindFieldInHierarchy(dictClass, OBFUSCATE("entries"));
    if (!entriesField) return nullptr;
    void* entries = nullptr;
    _monoFieldGetValue(dictionary, entriesField, &entries);
    if (!entries) return nullptr;
    const uintptr_t addr = (uintptr_t)entries;
    const int length = SafeRead<int>(addr + OFF_ARR_COUNT, 0);
    if (length <= 0 || length > 200000) return nullptr;
    const uintptr_t data = addr + OFF_ARR_DATA;
    constexpr int stride = 24;
    for (int i = 0; i < length; ++i) {
        const uintptr_t entry = data + (uintptr_t)i * stride;
        if (SafeRead<int>(entry, -1) < 0) continue;
        if (SafeRead<int>(entry + OFF_ENTRY_KEY, 0) != wantedKey) continue;
        return SafeRead<void*>(entry + OFF_ENTRY_VALUE, nullptr);
    }
    return nullptr;
}

static std::string ResolveManagedName(void* object) {
    if (!object) return {};
    void* klass = _monoObjectGetClass(object);
    static const char* names[] = {"get_name", "get_Name", "GetName", "get_displayName", "get_DisplayName"};
    for (const char* name : names) {
        void* method = FindMethodInHierarchy(klass, name, 0);
        if (!method) continue;
        void* exc = nullptr;
        void* result = _monoRuntimeInvoke(method, object, nullptr, &exc);
        if (!exc && result) {
            std::string value = ReadMonoStringNative(result);
            if (!value.empty()) return value;
        }
    }
    return {};
}

bool LoadAllCropseeds() {
    if (g_seedListLoading.exchange(true, std::memory_order_acq_rel)) return false;
    struct ResetLoading { ~ResetLoading() { g_seedListLoading.store(false, std::memory_order_release); } } reset;
    _monoThreadAttach(_monoGetRootDomain());

    void* image = _monoImageLoaded(OBFUSCATE("EcsClient"));
    if (!image) image = _monoImageLoaded(OBFUSCATE("EcsClient.dll"));
    if (!image) { g_seedListLoaded.store(false); return false; }
    void* tableData = _monoClassFromName(image, "", OBFUSCATE("TableData"));
    if (!tableData) tableData = _monoClassFromName(image, OBFUSCATE("XD.GameGerm.ExpParser"), OBFUSCATE("TableData"));
    if (!tableData) { g_seedListLoaded.store(false); return false; }

    void* cropseeds = GetStaticTableDictionary(tableData, OBFUSCATE("TableCropseeds"));
    void* entities = GetStaticTableDictionary(tableData, OBFUSCATE("TableEntitys"));
    if (!entities) entities = GetStaticTableDictionary(tableData, OBFUSCATE("TableEntities"));
    if (!cropseeds || !entities) { g_seedListLoaded.store(false); return false; }

    void* entriesField = FindFieldInHierarchy(_monoObjectGetClass(cropseeds), OBFUSCATE("_entries"));
    if (!entriesField) entriesField = FindFieldInHierarchy(_monoObjectGetClass(cropseeds), OBFUSCATE("entries"));
    void* entries = nullptr;
    if (entriesField) _monoFieldGetValue(cropseeds, entriesField, &entries);
    if (!entries) { g_seedListLoaded.store(false); return false; }

    const uintptr_t addr = (uintptr_t)entries;
    const int length = SafeRead<int>(addr + OFF_ARR_COUNT, 0);
    if (length <= 0 || length > 200000) { g_seedListLoaded.store(false); return false; }
    const uintptr_t data = addr + OFF_ARR_DATA;
    constexpr int stride = 24;
    std::vector<SeedData> loaded;
    loaded.reserve((size_t)length);
    for (int i = 0; i < length; ++i) {
        const uintptr_t entry = data + (uintptr_t)i * stride;
        if (SafeRead<int>(entry, -1) < 0) continue;
        int id = SafeRead<int>(entry + OFF_ENTRY_KEY, 0);
        void* seed = SafeRead<void*>(entry + OFF_ENTRY_VALUE, nullptr);
        if (id <= 0 || !seed) continue;
        void* idField = FindFieldInHierarchy(_monoObjectGetClass(seed), OBFUSCATE("id"));
        if (idField) _monoFieldGetValue(seed, idField, &id);
        if (id <= 0) continue;
        std::string name = ResolveManagedName(FindIntDictionaryValue(entities, id));
        if (name.empty()) name = ResolveManagedName(seed);
        if (!name.empty()) loaded.push_back({name, id});
    }
    std::sort(loaded.begin(), loaded.end(), [](const SeedData& a, const SeedData& b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
    loaded.erase(std::unique(loaded.begin(), loaded.end(), [](const SeedData& a, const SeedData& b) {
        return a.id == b.id;
    }), loaded.end());
    if (loaded.empty()) { g_seedListLoaded.store(false); return false; }
    {
        std::lock_guard<std::mutex> lock(g_seedListMutex);
        g_presetSeeds.swap(loaded);
        if (g_selectedSeedIdx < 0 || g_selectedSeedIdx >= (int)g_presetSeeds.size()) g_selectedSeedIdx = 0;
        g_seedStaticId = g_presetSeeds[(size_t)g_selectedSeedIdx].id;
    }
    g_seedListLoaded.store(true, std::memory_order_release);
    return true;
}

bool LoadAllFlowerSeeds() {
    if (g_flowerSeedListLoading.exchange(true, std::memory_order_acq_rel)) return false;
    struct ResetLoading { ~ResetLoading() { g_flowerSeedListLoading.store(false, std::memory_order_release); } } reset;
    _monoThreadAttach(_monoGetRootDomain());

    void* image = _monoImageLoaded(OBFUSCATE("EcsClient"));
    if (!image) image = _monoImageLoaded(OBFUSCATE("EcsClient.dll"));
    if (!image) { g_flowerSeedListLoaded.store(false); return false; }
    void* tableData = _monoClassFromName(image, "", OBFUSCATE("TableData"));
    if (!tableData) tableData = _monoClassFromName(image, OBFUSCATE("XD.GameGerm.ExpParser"), OBFUSCATE("TableData"));
    if (!tableData) { g_flowerSeedListLoaded.store(false); return false; }

    void* seedbags = GetStaticTableDictionary(tableData, OBFUSCATE("TableSeedbags"));
    void* entities = GetStaticTableDictionary(tableData, OBFUSCATE("TableEntitys"));
    if (!entities) entities = GetStaticTableDictionary(tableData, OBFUSCATE("TableEntities"));
    if (!seedbags || !entities) { g_flowerSeedListLoaded.store(false); return false; }

    void* dictClass = _monoObjectGetClass(seedbags);
    if (!dictClass) { g_flowerSeedListLoaded.store(false); return false; }
    void* entriesField = FindFieldInHierarchy(dictClass, OBFUSCATE("_entries"));
    if (!entriesField) entriesField = FindFieldInHierarchy(dictClass, OBFUSCATE("entries"));
    if (!entriesField) { g_flowerSeedListLoaded.store(false); return false; }

    void* entries = nullptr;
    _monoFieldGetValue(seedbags, entriesField, &entries);
    if (!entries || !IsValidMonoPointer(entries)) { g_flowerSeedListLoaded.store(false); return false; }

    const uintptr_t addr = (uintptr_t)entries;
    const int length = SafeRead<int>(addr + OFF_ARR_COUNT, 0);
    if (length <= 0 || length > 200000) { g_flowerSeedListLoaded.store(false); return false; }

    const uintptr_t data = addr + OFF_ARR_DATA;
    constexpr int stride = 24;
    std::vector<SeedData> loaded;
    loaded.reserve((size_t)length);

    for (int i = 0; i < length; ++i) {
        const uintptr_t entry = data + (uintptr_t)i * stride;
        if (SafeRead<int>(entry, -1) < 0) continue;

        int seedbagId = SafeRead<int>(entry + OFF_ENTRY_KEY, 0);
        void* seedbag = SafeRead<void*>(entry + OFF_ENTRY_VALUE, nullptr);
        if (seedbagId <= 0 || !seedbag || !IsValidMonoPointer(seedbag)) continue;

        void* seedbagClass = _monoObjectGetClass(seedbag);
        int plantId = 0;
        if (seedbagClass) {
            void* idField = FindFieldInHierarchy(seedbagClass, OBFUSCATE("id"));
            if (idField) _monoFieldGetValue(seedbag, idField, &seedbagId);
            void* pIdField = FindFieldInHierarchy(seedbagClass, OBFUSCATE("plantId"));
            if (pIdField) _monoFieldGetValue(seedbag, pIdField, &plantId);
        }
        if (seedbagId <= 0) continue;

        void* entity = FindIntDictionaryValue(entities, seedbagId);
        std::string name = ResolveManagedName(entity);
        if (name.empty()) name = ResolveManagedName(seedbag);
        if (name.empty() && plantId > 0) {
            void* plantEntity = FindIntDictionaryValue(entities, plantId);
            if (plantEntity) name = ResolveManagedName(plantEntity);
        }
        if (name.empty()) continue;

        loaded.push_back({ name, seedbagId });
    }

    std::sort(loaded.begin(), loaded.end(), [](const SeedData& a, const SeedData& b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
    loaded.erase(std::unique(loaded.begin(), loaded.end(), [](const SeedData& a, const SeedData& b) {
        return a.id == b.id;
    }), loaded.end());

    if (loaded.empty()) { g_flowerSeedListLoaded.store(false); return false; }

    {
        std::lock_guard<std::mutex> lock(g_flowerSeedListMutex);
        g_presetFlowerSeeds.swap(loaded);
        if (g_selectedFlowerSeedIdx < 0 || g_selectedFlowerSeedIdx >= (int)g_presetFlowerSeeds.size())
            g_selectedFlowerSeedIdx = 0;
        g_flowerSeedStaticId = g_presetFlowerSeeds[(size_t)g_selectedFlowerSeedIdx].id;
    }
    g_flowerSeedListLoaded.store(true, std::memory_order_release);
    return true;
}

int GetFlowerPlantingCapacity() {
    void* imgProtocol = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!imgProtocol) imgProtocol = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (!imgProtocol) return -1;
    void* hobbyProtoClass = _monoClassFromName(imgProtocol,
        OBFUSCATE("XDTDataAndProtocol.ProtocolService.Hobby"), OBFUSCATE("HobbyProtocolManager"));
    if (!hobbyProtoClass) return -1;
    void* tryGetParam = FindMethodInHierarchy(hobbyProtoClass, OBFUSCATE("TryGetHobbySkillParam"), 2);
    if (!tryGetParam) return -1;

    int skillEnum = 10602; // HobbySkillEnum.PlantingCapacity
    void* floatArray = nullptr;
    void* args[] = { &skillEnum, &floatArray };
    void* exc = nullptr;
    void* resObj = _monoRuntimeInvoke(tryGetParam, nullptr, args, &exc);
    if (resObj && !exc && floatArray && IsValidMonoPointer(floatArray)) {
        bool ok = *(bool*)_monoObjectUnbox(resObj);
        if (ok) {
            uintptr_t arrAddr = (uintptr_t)floatArray;
            int len = SafeRead<int>(arrAddr + OFF_ARR_COUNT, 0);
            if (len > 0) {
                float val = SafeRead<float>(arrAddr + OFF_ARR_DATA, 0.0f);
                return (int)val;
            }
        }
    }
    return -1;
}

bool TryGetHomelandWorldToLocal(const Vector3& playerPos, uint32_t& outFieldNetId, ModMatrix4x4& outW2L) {
    outFieldNetId = 0;
    outW2L = ModMatrix4x4::Identity();

    void* imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
    if (!imgLevel) return false;

    void* entitiesKlass = _monoClassFromName(imgLevel,
        OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("Entities"));
    if (!entitiesKlass) return false;

    void* m_getFieldSys = FindMethodInHierarchy(entitiesKlass, OBFUSCATE("get_fieldSystem"), 0);
    void* exc = nullptr;
    void* fieldSys = m_getFieldSys ? _monoRuntimeInvoke(m_getFieldSys, nullptr, nullptr, &exc) : nullptr;

    // 1. Ambil inFieldNetId dari LocalPlayerComponent
    void* utilKlass = _monoClassFromName(imgLevel,
        OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
    void* m_getSelf = utilKlass ? FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0) : nullptr;
    exc = nullptr;
    void* selfPlayer = m_getSelf ? _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, &exc) : nullptr;

    uint32_t inFieldNetId = 0;
    if (selfPlayer && IsValidMonoPointer(selfPlayer)) {
        void* spKlass = _monoObjectGetClass(selfPlayer);
        if (spKlass) {
            void* m_getFieldNetId = FindMethodInHierarchy(spKlass, OBFUSCATE("get_inFieldNetId"), 0);
            if (m_getFieldNetId) {
                exc = nullptr;
                void* fnObj = _monoRuntimeInvoke(m_getFieldNetId, selfPlayer, nullptr, &exc);
                if (fnObj && !exc) inFieldNetId = *(uint32_t*)_monoObjectUnbox(fnObj);
            }
            if (inFieldNetId == 0) {
                void* f_inFieldNetId = FindFieldInHierarchy(spKlass, OBFUSCATE("_inFieldNetId"));
                if (!f_inFieldNetId) f_inFieldNetId = FindFieldInHierarchy(spKlass, OBFUSCATE("inFieldNetId"));
                if (f_inFieldNetId) _monoFieldGetValue(selfPlayer, f_inFieldNetId, &inFieldNetId);
            }
        }
    }

    void* fieldObj = nullptr;
    if (fieldSys && IsValidMonoPointer(fieldSys)) {
        void* sysKlass = _monoObjectGetClass(fieldSys);
        if (sysKlass) {
            if (inFieldNetId != 0) {
                void* m_getField = FindMethodInHierarchy(sysKlass, OBFUSCATE("GetField"), 1);
                if (m_getField) {
                    void* gArgs[] = { &inFieldNetId };
                    exc = nullptr;
                    fieldObj = _monoRuntimeInvoke(m_getField, fieldSys, gArgs, &exc);
                    if (exc) fieldObj = nullptr;
                }
            }
            // 2. Fallback: GetNearestField(playerPos)
            if (!fieldObj) {
                void* m_getNearest = FindMethodInHierarchy(sysKlass, OBFUSCATE("GetNearestField"), 1);
                if (m_getNearest) {
                    Vector3 pPosCopy = playerPos;
                    void* nArgs[] = { &pPosCopy };
                    exc = nullptr;
                    fieldObj = _monoRuntimeInvoke(m_getNearest, fieldSys, nArgs, &exc);
                    if (exc) fieldObj = nullptr;
                }
            }
        }
    }

    // 3. Fallback: TryGetFieldRootWorldToLocal jika punya inFieldNetId
    if (inFieldNetId != 0 && TryGetFieldRootWorldToLocal(inFieldNetId, outW2L)) {
        outFieldNetId = inFieldNetId;
        return true;
    }

    if (!fieldObj || !IsValidMonoPointer(fieldObj)) return false;

    // Ambil buildWorld atau transform dari fieldObj
    void* fieldKlass = _monoObjectGetClass(fieldObj);
    if (!fieldKlass) return false;
    void* m_getBuildWorld = FindMethodInHierarchy(fieldKlass, OBFUSCATE("get_buildWorld"), 0);
    exc = nullptr;
    void* buildWorld = m_getBuildWorld ? _monoRuntimeInvoke(m_getBuildWorld, fieldObj, nullptr, &exc) : nullptr;
    if (buildWorld && !exc && IsValidMonoPointer(buildWorld)) {
        void* bwKlass = _monoObjectGetClass(buildWorld);
        if (bwKlass) {
            void* m_getW2L = FindMethodInHierarchy(bwKlass, OBFUSCATE("get_worldToLocal"), 0);
            if (m_getW2L) {
                exc = nullptr;
                void* w2lRes = _monoRuntimeInvoke(m_getW2L, buildWorld, nullptr, &exc);
                if (w2lRes && !exc) {
                    outW2L = *(ModMatrix4x4*)_monoObjectUnbox(w2lRes);
                    outFieldNetId = inFieldNetId;
                    return true;
                }
            }
        }
    }

    // Fallback: ViewComponent.get_transform().get_worldToLocalMatrix()
    void* m_getTrans = FindMethodInHierarchy(fieldKlass, OBFUSCATE("get_transform"), 0);
    exc = nullptr;
    void* trObj = m_getTrans ? _monoRuntimeInvoke(m_getTrans, fieldObj, nullptr, &exc) : nullptr;
    if (trObj && !exc && IsValidMonoPointer(trObj)) {
        void* trKlass = _monoObjectGetClass(trObj);
        if (trKlass) {
            void* m_w2l = FindMethodInHierarchy(trKlass, OBFUSCATE("get_worldToLocalMatrix"), 0);
            if (m_w2l) {
                exc = nullptr;
                void* w2lObj = _monoRuntimeInvoke(m_w2l, trObj, nullptr, &exc);
                if (w2lObj && !exc) {
                    outW2L = *(ModMatrix4x4*)_monoObjectUnbox(w2lObj);
                    outFieldNetId = inFieldNetId;
                    return true;
                }
            }
        }
    }

    return false;
}

void ExecuteAutoPlantFlower() {
    if (g_isAutoPlantFlowerBusy.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    struct AutoPlantFlowerGuard {
        ~AutoPlantFlowerGuard() {
            g_isAutoPlantFlowerBusy.store(false, std::memory_order_release);
        }
    } guard;

    g_flowerPlantStatus = u8"准备中…";

    if (g_flowerSeedStaticId <= 0) {
        g_flowerPlantStatus = u8"未选择种子";
        return;
    }

    int sliderCount = g_autoPlantFlowerCount;
    if (sliderCount < 1) sliderCount = 1;
    if (sliderCount > 10) sliderCount = 10;

    Vector3 playerPos = { 0, 0, 0 };
    Vector3 forward = { 0, 0, 1 };
    Vector3 right = { 1, 0, 0 };
    int hobbyCap = -1;
    int placedCount = 0;
    uint32_t fieldNetId = 0;
    ModMatrix4x4 w2l = ModMatrix4x4::Identity();
    bool hasW2L = false;

    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());

        void* player = GetLocalPlayerInstance();
        if (player && IsValidMonoPointer(player)) {
            playerPos = NativeGetPosition(player);
            void* spKlass = _monoObjectGetClass(player);
            if (spKlass) {
                void* m_getEnt = FindMethodInHierarchy(spKlass, OBFUSCATE("get_entity"), 0);
                if (m_getEnt) {
                    void* exc = nullptr;
                    void* entObj = _monoRuntimeInvoke(m_getEnt, player, nullptr, &exc);
                    if (entObj && !exc && IsValidMonoPointer(entObj)) {
                        void* entKlass = _monoObjectGetClass(entObj);
                        if (entKlass) {
                            void* m_getFwd = FindMethodInHierarchy(entKlass, OBFUSCATE("get_forward"), 0);
                            if (m_getFwd) {
                                exc = nullptr;
                                void* fObj = _monoRuntimeInvoke(m_getFwd, entObj, nullptr, &exc);
                                if (fObj && !exc) forward = *(Vector3*)_monoObjectUnbox(fObj);
                            }
                        }
                    }
                }
            }
        }
        forward.y = 0.0f;
        float fLen = sqrtf(forward.x * forward.x + forward.z * forward.z);
        if (fLen > 0.001f) {
            forward.x /= fLen;
            forward.z /= fLen;
            right = { forward.z, 0.0f, -forward.x };
        }

        hasW2L = TryGetHomelandWorldToLocal(playerPos, fieldNetId, w2l);

        // Capacity check
        hobbyCap = GetFlowerPlantingCapacity();

        // Count current placed flowers
        void* imgGameplay = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
        if (!imgGameplay) imgGameplay = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
        if (imgGameplay) {
            void* plantKlass = _monoClassFromName(imgGameplay,
                OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Homeland"), OBFUSCATE("PlantComponent"));
            if (plantKlass) {
                auto plantComps = GetComponentsOfClass(plantKlass);
                placedCount = (int)plantComps.size();
            }
        }
    });

    g_flowerHobbyMaxCapacity = hobbyCap;
    g_flowerCurrentPlaced = placedCount;

    if (playerPos.x == 0.0f && playerPos.y == 0.0f && playerPos.z == 0.0f) {
        g_flowerPlantStatus = u8"未找到玩家坐标";
        return;
    }

    if (!hasW2L) {
        g_flowerPlantStatus = u8"错误：必须在家园/农田";
        return;
    }

    if (hobbyCap > 0 && placedCount >= hobbyCap) {
        g_flowerPlantStatus = u8"已达上限（容量已满）";
        return;
    }

    int countToPlant = sliderCount;
    if (hobbyCap > 0) {
        int remaining = hobbyCap - placedCount;
        if (countToPlant > remaining) countToPlant = remaining;
    }
    if (countToPlant <= 0) {
        g_flowerPlantStatus = u8"已达上限";
        return;
    }

    int plantedTotal = 0;
    for (int i = 0; i < countToPlant; ++i) {
        int col = i % 3;
        int row = i / 3;
        float spacing = 0.75f;
        float sideOffset = (col - 1.0f) * spacing;
        float frontOffset = 1.0f + row * spacing;

        Vector3 worldTarget = {
            playerPos.x + right.x * sideOffset + forward.x * frontOffset,
            playerPos.y,
            playerPos.z + right.z * sideOffset + forward.z * frontOffset
        };

        Vector3 localPlantPos = w2l.MultiplyPoint3x4(worldTarget);

        bool plantOk = false;
        bool seedMissing = false;

        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());

            uint32_t seedNetId = GetSeedNetId(g_flowerSeedStaticId);
            if (seedNetId == 0) {
                seedMissing = true;
                return;
            }

            void* protocolImg = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
            if (!protocolImg) protocolImg = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
            if (!protocolImg) return;

            void* protocolClass = _monoClassFromName(protocolImg,
                OBFUSCATE("XDTDataAndProtocol.ProtocolService.Plant"), OBFUSCATE("PlantProtocolManager"));
            if (!protocolClass) return;

            void* method = FindMethodInHierarchy(protocolClass, OBFUSCATE("GrowPlant"), 3);
            if (!method) method = FindMethodInHierarchy(protocolClass, OBFUSCATE("GrowPlant"), 2);
            if (!method) return;

            void* sig = _monoMethodSignature(method);
            if (!sig) return;

            void* listKlass = nullptr;
            void* iter = nullptr;
            void* pType = nullptr;
            int paramCount = 0;
            while ((pType = _monoSignatureGetParams(sig, &iter)) != nullptr) {
                paramCount++;
                void* k = _monoClassFromMonoType(pType);
                if (k && FindMethodInHierarchy(k, OBFUSCATE("Add"), 1)) {
                    listKlass = k;
                }
            }
            if (!listKlass) return;

            void* listObj = _monoObjectNew(_monoGetRootDomain(), listKlass);
            if (!listObj) return;

            void* m_ctor = FindMethodInHierarchy(listKlass, OBFUSCATE(".ctor"), 0);
            void* exc = nullptr;
            if (m_ctor) _monoRuntimeInvoke(m_ctor, listObj, nullptr, &exc);
            if (exc) return;

            void* m_add = FindMethodInHierarchy(listKlass, OBFUSCATE("Add"), 1);
            if (!m_add) return;

            #pragma pack(push, 1)
            struct FlowerPlantPoint {
                Vector3 pos;
                int32_t angle;
            };
            #pragma pack(pop)

            FlowerPlantPoint pointData;
            pointData.pos = localPlantPos;
            pointData.angle = 0;

            void* addArgs[] = { &pointData };
            exc = nullptr;
            _monoRuntimeInvoke(m_add, listObj, addArgs, &exc);
            if (exc) return;

            uint32_t sIdVal = seedNetId;
            int plantModeVal = 0;
            void* pArgs3[] = { &sIdVal, listObj, &plantModeVal };
            void* pArgs2[] = { &sIdVal, listObj };
            exc = nullptr;
            _monoRuntimeInvoke(method, nullptr, (paramCount >= 3) ? pArgs3 : pArgs2, &exc);
            if (!exc) plantOk = true;
        });

        if (seedMissing) {
            g_flowerPlantStatus = (plantedTotal > 0) ? "SEED EMPTY IN BAG" : "SEED NOT IN BAG";
            break;
        }

        if (plantOk) {
            plantedTotal++;
            g_flowerCurrentPlaced++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (plantedTotal > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "PLANTED %d FLOWER(S)", plantedTotal);
        g_flowerPlantStatus = buf;
    } else if (g_flowerPlantStatus == "PREPARING...") {
        g_flowerPlantStatus = u8"种植失败";
    }
}

void PickAllOwnedMaturePlants(
                              const std::string& dllName,
                              const std::string& protocolNamespace,
                              const std::string& protocolClass,
                              const std::string& payloadRaw) {
    SMPlatform::SetBreadcrumbLight("pickall:enter");
    if (g_pickAllPlantsBusy.exchange(true, std::memory_order_acq_rel)) return;
    struct BusyReset { ~BusyReset() { g_pickAllPlantsBusy.store(false, std::memory_order_release); } } reset;

    try {
        auto PN = [&](const char* key) { return extractJsonString(payloadRaw, key); };
        _monoThreadAttach(_monoGetRootDomain());

        // Panen sayuran pot kebun yang sudah matang
        RunOnUnityThreadSync([]() {
            ScanPotsNative();
            ExecuteAutoHarvest();
        });

        void* imgData = _monoImageLoaded(dllName.c_str());
        if (!imgData && dllName.find(".dll") == std::string::npos)
            imgData = _monoImageLoaded((dllName + ".dll").c_str());
        std::string imgGameplayName = PN("n_imgGameplay");
        void* imgGameplay = _monoImageLoaded(imgGameplayName.c_str());
        if (!imgGameplay && imgGameplayName.find(".dll") == std::string::npos)
            imgGameplay = _monoImageLoaded((imgGameplayName + ".dll").c_str());
        if (!imgData || !imgGameplay) return;

        void* playerClass = _monoClassFromName(imgData, PN("n_nsPlayerData").c_str(), PN("n_clsPlayerData").c_str());
        void* gameplayApi = _monoClassFromName(imgGameplay, PN("n_nsGameplayApi").c_str(), PN("n_clsGameplayApi").c_str());
        void* getSelf = playerClass ? FindMethodInHierarchy(playerClass, PN("n_getSelfId").c_str(), 0) : nullptr;
        void* getOwner = gameplayApi ? FindMethodInHierarchy(gameplayApi, PN("n_getFieldOwner").c_str(), 0) : nullptr;
        if (!getSelf || !getOwner) return;
        void* exc = nullptr;
        void* selfObj = _monoRuntimeInvoke(getSelf, nullptr, nullptr, &exc);
        if (!selfObj || exc) return;
        uint32_t selfId = *(uint32_t*)_monoObjectUnbox(selfObj);
        exc = nullptr;
        void* ownerObj = _monoRuntimeInvoke(getOwner, nullptr, nullptr, &exc);
        if (!ownerObj || exc) return;
        uint32_t fieldOwner = *(uint32_t*)_monoObjectUnbox(ownerObj);
        if (!selfId || fieldOwner != selfId) return;

        void* plantClass = _monoClassFromName(imgGameplay, PN("n_nsHomeland").c_str(), PN("n_clsPlant").c_str());
        void* furnitureClass = _monoClassFromName(imgGameplay, PN("n_nsHomeland").c_str(), PN("n_clsFurniture").c_str());
        void* worldClass = _monoClassFromName(imgData, PN("n_nsWorld").c_str(), PN("n_clsWorld").c_str());
        if (!worldClass) worldClass = _monoClassFromName(imgGameplay, PN("n_nsWorld").c_str(), PN("n_clsWorld").c_str());
        if (!worldClass) worldClass = _monoClassFromName(imgGameplay, OBFUSCATE("XDTLevelAndEntity.Core.World"), PN("n_clsWorld").c_str());
        if (!worldClass) worldClass = _monoClassFromName(imgData, OBFUSCATE("XDTLevelAndEntity.Core.World"), PN("n_clsWorld").c_str());
        if (!plantClass || !furnitureClass || !worldClass) return;
        void* open = FindMethodInHierarchy(worldClass, PN("n_getComponentSystem").c_str(), 0);
        if (!open) return;
        void* typeArgs[] = {_monoClassGetType(plantClass)};
        void* inst = _mono_metadata_get_generic_inst(1, typeArgs);
        MonoGenericContext ctx = {nullptr, inst};
        void* closed = _mono_class_inflate_generic_method(open, &ctx);
        exc = nullptr;
        void* system = _monoRuntimeInvoke(closed, nullptr, nullptr, &exc);
        if (!system || exc) return;
        void* componentsField = FindFieldInHierarchy(_monoObjectGetClass(system), PN("n_components").c_str());
        void* dictionary = nullptr;
        if (componentsField) _monoFieldGetValue(system, componentsField, &dictionary);
        if (!dictionary) return;
        void* entriesField = FindFieldInHierarchy(_monoObjectGetClass(dictionary), PN("n_entries").c_str());
        if (!entriesField) entriesField = FindFieldInHierarchy(_monoObjectGetClass(dictionary), OBFUSCATE("entries"));
        void* entries = nullptr;
        if (entriesField) _monoFieldGetValue(dictionary, entriesField, &entries);
        if (!entries) return;

        void* getEntity = FindMethodInHierarchy(plantClass, PN("n_getEntity").c_str(), 0);
        void* getData = FindMethodInHierarchy(plantClass, PN("n_getComponentData").c_str(), 0);
        void* furnitureField = FindFieldInHierarchy(plantClass, PN("n_furnitureBase").c_str());
        void* getOwnerId = FindMethodInHierarchy(furnitureClass, PN("n_getOwnerId").c_str(), 0);
        void* protocol = _monoClassFromName(imgData, protocolNamespace.c_str(), protocolClass.c_str());
        void* mature = protocol ? FindMethodInHierarchy(protocol, PN("n_tryGetMatureTime").c_str(), 3) : nullptr;
        void* pick = protocol ? FindMethodInHierarchy(protocol, PN("n_pickPlant").c_str(), 1) : nullptr;
        if (!getEntity || !getData || !furnitureField || !getOwnerId || !mature || !pick) return;

        const uintptr_t addr = (uintptr_t)entries;
        const int length = SafeRead<int>(addr + OFF_ARR_COUNT, 0);
        if (length <= 0 || length > 200000) return;
        const uintptr_t data = addr + OFF_ARR_DATA;
        constexpr int stride = 24;
        for (int i = 0; i < length; ++i) {
            const uintptr_t entry = data + (uintptr_t)i * stride;
            if (SafeRead<int>(entry, -1) < 0) continue;
            void* plant = SafeRead<void*>(entry + OFF_ENTRY_VALUE, nullptr);
            if (!plant) continue;
            exc = nullptr;
            void* entity = _monoRuntimeInvoke(getEntity, plant, nullptr, &exc);
            if (!entity || exc) continue;
            void* netMethod = FindMethodInHierarchy(_monoObjectGetClass(entity), PN("n_getNetId").c_str(), 0);
            void* netObj = netMethod ? _monoRuntimeInvoke(netMethod, entity, nullptr, &exc) : nullptr;
            if (!netObj || exc) continue;
            uint32_t netId = *(uint32_t*)_monoObjectUnbox(netObj);
            if (!netId) continue;
            void* furniture = nullptr;
            _monoFieldGetValue(plant, furnitureField, &furniture);
            if (!furniture) continue;
            exc = nullptr;
            void* plantOwnerObj = _monoRuntimeInvoke(getOwnerId, furniture, nullptr, &exc);
            if (!plantOwnerObj || exc || *(uint32_t*)_monoObjectUnbox(plantOwnerObj) != selfId) continue;
            exc = nullptr;
            void* dataObj = _monoRuntimeInvoke(getData, plant, nullptr, &exc);
            if (!dataObj || exc) continue;
            void* dataClass = _monoObjectGetClass(dataObj);
            void* stageField = FindFieldInHierarchy(dataClass, PN("n_stage").c_str());
            void* isPickField = FindFieldInHierarchy(dataClass, PN("n_isPick").c_str());
            uint8_t stage = 0, isPick = 0;
            if (stageField) _monoFieldGetValue(dataObj, stageField, &stage);
            if (isPickField) _monoFieldGetValue(dataObj, isPickField, &isPick);
            if (!stageField || stage != 4 || isPick) continue;
            int64_t matureMs = 0;
            float coldTime = 0.0f;
            void* matureArgs[] = {&netId, &matureMs, &coldTime};
            exc = nullptr;
            void* matureObj = _monoRuntimeInvoke(mature, nullptr, matureArgs, &exc);
            if (!matureObj || exc || !*(uint8_t*)_monoObjectUnbox(matureObj)) continue;
            void* pickArgs[] = {&netId};
            exc = nullptr;
            _monoRuntimeInvoke(pick, nullptr, pickArgs, &exc);
        }

        RunOnUnityThreadSync([]() {
            ScanPotsNative();
            ExecuteAutoHarvest();
        });
    } catch (...) {}
}

static inline std::string CookN(const char* k) {
    std::string v = extractJsonString(g_cookPn, k);
    if (!v.empty()) return v;
    // 离线/服务端 payload 缺字段时的内置回退（名字来自 PC 模组权威调用链）。
    struct CookDefault { const char* key; const char* value; };
    static const CookDefault kDefaults[] = {
        {"n_imgData", "XDTDataAndProtocol"},
        {"n_imgLevel", "XDTLevelAndEntity"},
        {"n_imgLevelDll", "XDTLevelAndEntity.dll"},
        {"n_nsHomeland", "XDTLevelAndEntity.Gameplay.Component.Homeland"},
        {"n_clsCookBuild", "CookBuildComponent"},
        {"n_nsWorld", "XDTLevelAndEntity.Core.World"},
        {"n_clsEntityWorld", "EntityWorld"},
        {"n_getCompSys", "GetComponentSystem"},
        {"n_fComponents", "_components"},
        {"n_entries", "entries"},
        {"n_entriesU", "_entries"},
        {"n_getCompData", "ComponentData"},
        {"n_fStaticId", "staticId"},
        {"n_getCooing", "GetCooingComponent"},
        {"n_getCooing2", "GetCookingComponent"},
        {"n_imgGameSys", "XDTGameSystem"},
        {"n_nsCooking", "XDTGameSystem.GameplaySystem.Cooking"},
        {"n_clsCookingSys", "CookingSystem"},
        {"n_getInstance", "get_Instance"},
        {"n_getInstance2", "get_Instance"},
        {"n_getAllRecipes", "GetAllRecipes"},
        {"n_fItems", "items"},
        {"n_fSize", "size"},
        {"n_nsProtoCook", "XDTDataAndProtocol.ProtocolService.Cooking"},
        {"n_clsProtoMgr", "CookingProtocolManager"},
        {"n_initRecipe", "InitCookingRecipeDetail"},
        {"n_prepCook", "PrepareCooking"},
        {"n_prepCook2", "PrepareCooking"},
        {"n_startCook", "StartCooking"},
        {"n_interact", "InteractWithCooker"},
        {"n_continueCook", "ContinueCooking"},
        {"n_clsPlayerData", "PlayerDataCenter"},
        {"n_getSelfNetId", "GetSelfNetPlayerId"},
    };
    for (const CookDefault &d : kDefaults) {
        if (strcmp(k, d.key) == 0) return d.value;
    }
    return std::string();
}

void FetchCookerNamesFromTable() {
    if (!g_cookerNamesCache.empty())
        return; // Jika sudah terisi, tidak usah sedot lagi

    _monoThreadAttach(_monoGetRootDomain());
    void *imgTarget = _monoImageLoaded(CookN("n_imgEcs").c_str());
    if (!imgTarget)
        imgTarget = _monoImageLoaded(CookN("n_imgAsm").c_str());
    if (!imgTarget)
        return;

    void *klassTableData = _monoClassFromName(imgTarget, "", CookN("n_clsTableData").c_str());
    if (!klassTableData)
        klassTableData = _monoClassFromName(imgTarget, CookN("n_nsExpParser").c_str(),
                                            CookN("n_clsTableData").c_str());
    if (!klassTableData)
        return;
    auto FindMethod = [](void *klass, const char *name, int paramCount) -> void * {
        void *current = klass;
        while (current != nullptr) {
            void *method = _monoClassGetMethodFromName(current, name, paramCount);
            if (method != nullptr)
                return method;
            current = _monoClassGetParent(current);
        }
        return nullptr;
    };
    // 1. GUNAKAN PROPERTY GETTER AGAR LAZY-LOAD GAMENYA BEKERJA
    void *m_getTable = FindMethod(klassTableData, CookN("n_getTableCookers").c_str(), 0);
    if (!m_getTable)
        m_getTable = FindMethod(klassTableData, CookN("n_getTableCookers2").c_str(), 0);

    void *dictObj = nullptr;
    if (m_getTable) {
        void *exc = nullptr;
        dictObj = _monoRuntimeInvoke(m_getTable, nullptr, nullptr, &exc);
    } else {
        // Fallback jika ternyata pakai Static Field murni
        void *f_tableCookers = _monoClassGetFieldFromName(klassTableData,
                                                          CookN("n_fTableCookers").c_str());
        if (f_tableCookers) {
            void *vtable = _monoClassGetVTable(_monoGetRootDomain(), klassTableData);
            _monoFieldStaticGetValue(vtable, f_tableCookers, &dictObj);
        }
    }

    if (!dictObj)
        return;
    // Jika masih null, berarti belum masuk world/gameplay

    // 2. BONGKAR DICTIONARY
    void *dictKlass = _monoObjectGetClass(dictObj);
    void *f_entries = _monoClassGetFieldFromName(dictKlass, CookN("n_entriesU").c_str());
    if (!f_entries)
        f_entries = _monoClassGetFieldFromName(dictKlass, CookN("n_entries").c_str());
    if (!f_entries)
        return;

    void *entriesArr = nullptr;
    _monoFieldGetValue(dictObj, f_entries, &entriesArr);
    if (!entriesArr)
        return;

    int length = *(int *) ((uint8_t *) entriesArr + OFF_ARR_COUNT);
    uint8_t *arrayDataStart = (uint8_t *) entriesArr + OFF_ARR_DATA;
    void *klassTableCooker = _monoClassFromName(imgTarget, "", CookN("n_clsTableCooker").c_str());
    void *m_getName = FindMethod(klassTableCooker, CookN("n_getName").c_str(), 0);
    if (!m_getName)
        m_getName = FindMethod(klassTableCooker, CookN("n_getName2").c_str(), 0);
    // 3. LOOPING & TERJEMAHKAN STRING
    for (int i = 0; i < length; i++) {
        int hashCode = *(int *) (arrayDataStart + (i * SZ_DICT_ENTRY));
        if (hashCode >= 0) {
            int cookerId = *(int *) (arrayDataStart + (i * SZ_DICT_ENTRY) + OFF_ENTRY_KEY);
            // Key
            void *cookerObjPtr = *(void **) (arrayDataStart + (i * SZ_DICT_ENTRY) + OFF_ENTRY_VALUE);
            // Value

            if (cookerObjPtr && m_getName) {
                void *strPtr = _monoRuntimeInvoke(m_getName, cookerObjPtr, nullptr, nullptr);
                if (strPtr) {
                    g_cookerNamesCache[cookerId] = ReadMonoStringNative(strPtr);
                }
            }
        }
    }
}

// ============================================================================
//  灶台扫描（cook:*）—— 首选安卓在役版实测可用的 BuildModule.GetBuildItems 路
// ----------------------------------------------------------------------------
//  路线 BM（首选，安卓真机实测能扫到灶台）：
//    XDTGUI.Module.Build.BuildModule.GetBuildItems(uint playerNetId)   // 静态 1 参
//    → 游戏自己建好的 List（元素是构建组件，带 get_entity()）
//    → 型号：_cookerStaticId → ComponentData.staticId → EntityUtil.GetEntityResId(netId)
//    → 锅位：_cookBurnerMap（Dictionary<ulong, CookingComponent>）的 Key/Value
//    —— 不膨胀泛型、不造 List，天然绕开下面两个坑；只有遇到 ref List<T> 重载时，
//       才用工程既有的三条造 List 路现造一个（签名解包 / corlib 膨胀 / BCL Type.GetType）。
//
//  路线 L（PC 权威兜底）：Entities.GetComponents<CookBuildComponent>(ref List<T>)
//    在 iOS mono 上可能断在"造不出 List<T>"，两个坑：
//    ① `ref List<T>` 签名里的 MonoType 是 BYREF 包装（MONO_TYPE_BYREF=0x10）——
//       mono_class_from_mono_type 对 byref 返回 null；
//    ② mono_object_new 不跑 .ctor，List._items 还是 null，传进游戏会抛异常。
//    所以三条造 List 的路都做可用性校验（get_Count 能跑通才算数）。
//
//  路线 D（最后兜底）：LevelObjectManager._instance._dictionary 遍历 LevelObject，
//    有灶台型号表时按 staticId 直判，否则拿实体上的 CookBuildComponent 探一次。
//    ⚠️ 不再走 EntityWorld.GetComponentSystem（PC 源码零命中，逆向猜出来的旁路）。
//
//  面包屑：cook:bm:N（BM 路成功 N 个）/ cook:found:N:L|D（兜底路成功）/
//          cook:bm-fail:<stage>，stage 语义：
//            1 BuildModule 类未找到          2 GetBuildItems 或单例未找到
//            3 调用抛异常/返回 null          4 List 枚举不出来
//            5 有候选但一个都没判定成灶台（附候选数 /N）   6 列表为空
// ============================================================================

// List`1 类名/命名空间校验：开放泛型和关闭泛型的 Name 都是 "List`1"，
// 所以这个名字只能证明"是个 List"，是否闭合由 listKlass != listOpen 另外判。
static bool SMCookKlassIsList(void *klass) {
    if (!klass) return false;
    const char *name = SMMonoHot::ClassGetName(klass);
    if (!name || strcmp(name, "List`1") != 0) return false;
    const char *ns = SMMonoHot::ClassGetNamespace(klass);
    return !ns || strcmp(ns, "System.Collections.Generic") == 0;
}

// List 实例可用性：get_Count 能跑通且返回值合理（顺带证明 .ctor 跑过、
// klass 真的是 List<T>）。校验不过的实例绝不能传进游戏方法。
static bool SMCookListUsable(void *listObj) {
    if (!listObj) return false;
    void *listKlass = _monoObjectGetClass(listObj);
    if (!listKlass) return false;
    void *m_count = FindMethodInHierarchy(listKlass, (const char *) OBFUSCATE("get_Count"), 0);
    if (!m_count) return false;
    void *exc = nullptr;
    void *boxed = _monoRuntimeInvoke(m_count, listObj, nullptr, &exc);
    if (!boxed || exc) return false;
    const int count = *(int *) _monoObjectUnbox(boxed);
    return count >= 0 && count <= 100000;
}

// new + 初始化：mono_object_new 不跑 .ctor，必须显式跑一次空 .ctor()，
// 否则 List._items 还是 null，传进游戏里 Add 会 NullReference；
// 再补一次 runtime_object_init 兜住不同 mono 版本的 init 语义差异。
static void *SMCookInstantiateList(void *listKlass) {
    if (!SMCookKlassIsList(listKlass)) return nullptr;
    void *obj = SMMonoHot::ObjectNew(listKlass);
    if (!obj) return nullptr;
    void *m_ctor = FindMethodInHierarchy(listKlass, (const char *) OBFUSCATE(".ctor"), 0);
    if (m_ctor) {
        void *exc = nullptr;
        _monoRuntimeInvoke(m_ctor, obj, nullptr, &exc);
    }
    SMMonoHot::RuntimeObjectInit(obj);
    return SMCookListUsable(obj) ? obj : nullptr;
}

// A：从已膨胀方法的签名参数里拿 List<T>（byref 感知）
//    签名里的 List<T> 已经是"用灶台类膨胀过的"关闭泛型，比去 corlib 里自己膨胀可靠。
//    outWhy：1=签名/BYREF 阶段失败，2=类阶段失败，3=new/init 失败，0=成功。
static void *SMCreateListFromMethodSignature(void *closedMethod, int *outWhy) {
    int stage = 1;
    if (!closedMethod || !SMMonoHot::Available()) {
        if (outWhy) *outWhy = 1;
        return nullptr;
    }
    void *sig = SMMonoHot::MethodSignature(closedMethod);
    if (!sig) {
        if (outWhy) *outWhy = 1;
        return nullptr;
    }
    void *iter = nullptr;
    for (int guard = 0; guard < 16; ++guard) {
        void *type = SMMonoHot::SignatureGetParams(sig, &iter);
        if (!type) break;
        // ① 直接取类：非 byref 的 List<T> 参数走这条
        void *klass = SMMonoHot::ClassFromMonoType(type);
        if (!SMCookKlassIsList(klass)) {
            if (SMMonoHot::TypeIsByRef(type)) {
                // ② BYREF 解包：MONO_TYPE_BYREF(0x10) → data.byref → 元素类型
                void *element = SMMonoHot::TypeGetByRefElement(type);
                if (!element) {
                    stage = 1;          // byref 解不开：仍是签名/BYREF 阶段
                    continue;
                }
                klass = SMMonoHot::ClassFromMonoType(element);
            }
        }
        if (!SMCookKlassIsList(klass)) {
            // ③ 底层类型（枚举/包装过的参数）
            void *underlying = SMMonoHot::TypeGetUnderlyingType(type);
            if (underlying) {
                void *underlyingKlass = SMMonoHot::ClassFromMonoType(underlying);
                if (SMCookKlassIsList(underlyingKlass)) klass = underlyingKlass;
            }
        }
        if (!SMCookKlassIsList(klass)) {
            if (stage < 2) stage = 2;   // 读到参数类型了，但拿不到 List 类
            continue;
        }
        stage = 3;                      // 类有了，再失败就是 new/init
        void *obj = SMCookInstantiateList(klass);
        if (obj) {
            if (outWhy) *outWhy = 0;
            return obj;
        }
    }
    if (outWhy) *outWhy = stage;
    return nullptr;
}

// B：corlib 兜底 —— 找开放 List`1，用灶台类膨胀成关闭泛型，再 new + init。
//    ⚠️ 泛型类实例参数必须放 class_inst；拿开放泛型类去 init 会直接 abort。
static void *SMCreateMonoListForType(void *elementKlass, int *outWhy) {
    if (outWhy) *outWhy = 2;
    if (!elementKlass || !SMMonoHot::Available()) return nullptr;
    void *elementType = SMMonoHot::ClassGetType(elementKlass);
    if (!elementType) return nullptr;

    static const char *kCorlibNames[] = {"mscorlib", "mscorlib.dll", "System.Private.CoreLib",
                                         "netstandard", "System", "CoreLib"};
    void *listOpen = nullptr;
    for (const char *corlibName : kCorlibNames) {
        void *corlib = SMMonoHot::ImageLoaded(corlibName);
        if (!corlib) continue;
        listOpen = SMMonoHot::ClassFromName(corlib, "System.Collections.Generic", "List`1");
        if (listOpen) break;
    }
    if (!listOpen) {
        std::string foundImage;
        listOpen = SMMonoHot::FindClassAnywhere("System.Collections.Generic", "List`1",
                                                &foundImage);
    }
    if (!listOpen) return nullptr;
    void *listOpenType = SMMonoHot::ClassGetType(listOpen);
    if (!listOpenType) return nullptr;

    void *typeArgs[1] = {elementType};
    void *genericInst = SMMonoHot::MetadataGetGenericInst(1, typeArgs);
    if (!genericInst) return nullptr;
    MonoGenericContext ctx;
    ctx.class_inst = genericInst;
    ctx.method_inst = nullptr;
    void *listType = SMMonoHot::ClassInflateGenericType(listOpenType, &ctx);
    if (!listType)
        listType = SMMonoHot::ClassInflateGenericTypeChecked(listOpenType, &ctx, nullptr);
    if (!listType) return nullptr;
    void *listKlass = SMMonoHot::ClassFromMonoType(listType);
    if (!listKlass || listKlass == listOpen) return nullptr;   // 开放泛型 → 不许 new
    if (!SMCookKlassIsList(listKlass)) return nullptr;

    if (outWhy) *outWhy = 3;
    void *obj = SMCookInstantiateList(listKlass);
    if (obj && outWhy) *outWhy = 0;
    return obj;
}

// C：BCL 兜底 —— System.Type.GetType("...List`1[[元素类, 所在程序集]], mscorlib")
//    + System.Activator.CreateInstance(Type)。与 PC 模组同款：名字不对只会
//    返回 null（不 abort），造出来的实例 .ctor 一定跑过。
static void *SMCreateListViaBcl(void *elementKlass, int *outWhy) {
    if (outWhy) *outWhy = 2;
    if (!elementKlass || !SMMonoHot::Available()) return nullptr;
    const char *elementName = SMMonoHot::ClassGetName(elementKlass);
    if (!elementName || !*elementName) return nullptr;
    const char *elementNs = SMMonoHot::ClassGetNamespace(elementKlass);
    void *elementImage = SMMonoHot::ClassGetImage(elementKlass);
    const char *imageName = elementImage ? SMMonoHot::ImageGetName(elementImage) : nullptr;

    std::string typeName = "System.Collections.Generic.List`1[[";
    if (elementNs && *elementNs) {
        typeName += elementNs;
        typeName += ".";
    }
    typeName += elementName;
    if (imageName && *imageName) {
        typeName += ", ";
        typeName += imageName;
        // 限定名用程序集短名：mono_image_get_name 一般带 .dll，得去掉。
        const size_t dll = typeName.rfind(".dll");
        if (dll != std::string::npos && dll + 4 == typeName.size()) typeName.erase(dll);
    }
    typeName += "]], mscorlib";

    static const char *kCorlibNames[] = {"mscorlib", "System.Private.CoreLib",
                                         "System.Runtime"};
    void *typeKlass = nullptr;
    void *activatorKlass = nullptr;
    for (const char *corlibName : kCorlibNames) {
        void *corlib = SMMonoHot::ImageLoaded(corlibName);
        if (!corlib) continue;
        if (!typeKlass)
            typeKlass = SMMonoHot::ClassFromName(corlib, "System", "Type");
        if (!activatorKlass)
            activatorKlass = SMMonoHot::ClassFromName(corlib, "System", "Activator");
        if (typeKlass && activatorKlass) break;
    }
    if (!typeKlass || !activatorKlass) return nullptr;
    void *m_getType =
            FindMethodInHierarchy(typeKlass, (const char *) OBFUSCATE("GetType"), 1);
    void *m_createInstance =
            FindMethodInHierarchy(activatorKlass, (const char *) OBFUSCATE("CreateInstance"), 1);
    if (!m_getType || !m_createInstance) return nullptr;

    void *typeNameString = SMMonoHot::StringNew(typeName.c_str());
    if (!typeNameString) return nullptr;
    if (outWhy) *outWhy = 3;
    void *getTypeArgs[1] = {typeNameString};
    void *exc = nullptr;
    void *typeObj = _monoRuntimeInvoke(m_getType, nullptr, getTypeArgs, &exc);
    if (!typeObj || exc) return nullptr;
    void *createArgs[1] = {typeObj};
    exc = nullptr;
    void *listObj = _monoRuntimeInvoke(m_createInstance, nullptr, createArgs, &exc);
    if (!listObj || exc) return nullptr;
    if (!SMCookListUsable(listObj)) return nullptr;
    if (outWhy) *outWhy = 0;
    return listObj;
}

// A → B → C 依次尝试；outWhy 取"最靠后"的失败阶段，方便面包屑定位。
static void *SMCreateCookList(void *closedMethod, void *elementKlass, int *outWhy) {
    int whySignature = 1;
    int whyCorlib = 2;
    int whyBcl = 2;
    void *listObj = SMCreateListFromMethodSignature(closedMethod, &whySignature);
    if (!listObj) listObj = SMCreateMonoListForType(elementKlass, &whyCorlib);
    if (!listObj) listObj = SMCreateListViaBcl(elementKlass, &whyBcl);
    if (outWhy) {
        int why = 1;
        if (whySignature == 3 || whyCorlib == 3 || whyBcl == 3)
            why = 3;
        else if (whySignature == 2 || whyCorlib == 2 || whyBcl == 2)
            why = 2;
        *outWhy = why;
    }
    return listObj;
}

// ---------------------------------------------------------------------------
//  CookBuildComponent → CookerTarget（两条数据源共用，逐行沿用旧实现）
// ---------------------------------------------------------------------------
struct SMCookFieldCache {
    bool resolved = false;
    void *f_entity = nullptr;
    void *f_staticId = nullptr;
    void *f_burnerMap = nullptr;
    void *f_burnerEntity = nullptr;
    void *f_burnerType = nullptr;
    // 组件 → 实体的"方法"通道（BuildModule.GetBuildItems 的元素只有 get_entity()）
    void *m_getEntity = nullptr;
    // 组件 → 型号的"方法"通道（ComponentData 属性返回装箱结构，只能按方法取）
    void *m_getCompData = nullptr;
    void *f_compDataStaticId = nullptr;
};

static void SMCookResolveFields(void *cookKlass, SMCookFieldCache *cache) {
    if (!cache || cache->resolved || !cookKlass) return;
    cache->resolved = true;
    cache->f_entity = FindFieldInHierarchy(cookKlass, (const char *) OBFUSCATE("entity"));
    if (!cache->f_entity)
        cache->f_entity = FindFieldInHierarchy(cookKlass, (const char *) OBFUSCATE("_entity"));
    cache->f_staticId =
            FindFieldInHierarchy(cookKlass, (const char *) OBFUSCATE("_cookerStaticId"));
    if (!cache->f_staticId)
        cache->f_staticId =
                FindFieldInHierarchy(cookKlass, (const char *) OBFUSCATE("cookerStaticId"));
    cache->f_burnerMap =
            FindFieldInHierarchy(cookKlass, (const char *) OBFUSCATE("_cookBurnerMap"));
    if (!cache->f_burnerMap)
        cache->f_burnerMap =
                FindFieldInHierarchy(cookKlass, (const char *) OBFUSCATE("cookBurnerMap"));
    if (!cache->f_entity) {
        cache->m_getEntity =
                FindMethodInHierarchy(cookKlass, (const char *) OBFUSCATE("get_entity"), 0);
        if (!cache->m_getEntity)
            cache->m_getEntity =
                    FindMethodInHierarchy(cookKlass, (const char *) OBFUSCATE("get_Entity"), 0);
    }
}

// 组件 → 型号（BuildModule 路的元素不一定是 CookBuildComponent，没有 _cookerStaticId）：
// ComponentData 只按"方法/属性"取（mono 会把返回的值类型装箱）；绝不当字段读，
// 否则内联结构会被当指针用。
static int SMCookStaticIdFromComponentData(void *comp, SMCookFieldCache *cache) {
    if (!comp || !cache) return 0;
    if (!cache->m_getCompData) {
        void *klass = _monoObjectGetClass(comp);
        if (!klass) return 0;
        cache->m_getCompData =
                FindMethodInHierarchy(klass, (const char *) OBFUSCATE("get_ComponentData"), 0);
        if (!cache->m_getCompData)
            cache->m_getCompData =
                    FindMethodInHierarchy(klass, (const char *) OBFUSCATE("ComponentData"), 0);
        if (!cache->m_getCompData)
            cache->m_getCompData =
                    FindMethodInHierarchy(klass, (const char *) OBFUSCATE("get_componentData"), 0);
    }
    if (!cache->m_getCompData) return 0;
    void *exc = nullptr;
    void *dataObj = _monoRuntimeInvoke(cache->m_getCompData, comp, nullptr, &exc);
    if (!dataObj || exc) return 0;
    if (!cache->f_compDataStaticId) {
        void *dataKlass = _monoObjectGetClass(dataObj);
        if (!dataKlass) return 0;
        cache->f_compDataStaticId =
                FindFieldInHierarchy(dataKlass, (const char *) OBFUSCATE("staticId"));
        if (!cache->f_compDataStaticId)
            cache->f_compDataStaticId =
                    FindFieldInHierarchy(dataKlass, (const char *) OBFUSCATE("staticID"));
        if (!cache->f_compDataStaticId)
            cache->f_compDataStaticId =
                    FindFieldInHierarchy(dataKlass, (const char *) OBFUSCATE("Id"));
    }
    if (!cache->f_compDataStaticId) return 0;
    int staticId = 0;
    _monoFieldGetValue(dataObj, cache->f_compDataStaticId, &staticId);
    return staticId;
}

// 静态判定：MONO_METHOD_ATTR_STATIC=0x10。flags 读不出来（老 bug 下膨胀方法恒 0）时
// 按静态处理 —— 与安卓在役版一致（它一直传 nullptr this）。
static bool SMCookMethodIsStatic(void *method) {
    if (!method) return false;
    const uint32_t flags = SMMonoHot::MethodGetFlags(method, nullptr);
    return flags == 0 || (flags & 0x10u) != 0;
}

// 实体 → 资源型号：EntityUtil.GetEntityResId(uint netId)（静态 1 参，返回 boxed int）
static int SMCookEntityResId(uint32_t netId) {
    if (netId == 0) return 0;
    static void *s_utilKlass = nullptr;
    static bool s_resolved = false;
    if (!s_resolved) {
        s_resolved = true;
        void *img = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
        if (!img) img = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity.dll"));
        if (img)
            s_utilKlass = _monoClassFromName(
                    img, (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
                    (const char *) OBFUSCATE("EntityUtil"));
    }
    if (!s_utilKlass) return 0;
    void *m = FindMethodInHierarchy(s_utilKlass, (const char *) OBFUSCATE("GetEntityResId"), 1);
    if (!m || !SMCookMethodIsStatic(m)) return 0;
    uint32_t arg = netId;
    void *args[1] = {&arg};
    void *exc = nullptr;
    void *res = _monoRuntimeInvoke(m, nullptr, args, &exc);
    if (!res || exc) return 0;
    return *(int *) _monoObjectUnbox(res);
}

// 型号是不是灶台：先查 TableData 拉到的灶台名表，再问 TableData.GetCooker(staticId, false)。
// 返回 1=是 0=不是 -1=判不了（表不可用）。
static int SMCookTableCookerKnown(int staticId) {
    if (staticId <= 0) return 0;
    if (g_cookerNamesCache.find(staticId) != g_cookerNamesCache.end()) return 1;
    static std::unordered_map<int, int> s_known;
    auto hit = s_known.find(staticId);
    if (hit != s_known.end()) return hit->second;
    if (!SMMonoHot::Available()) return -1;

    void *image = _monoImageLoaded((const char *) OBFUSCATE("EcsClient"));
    if (!image) image = _monoImageLoaded((const char *) OBFUSCATE("EcsClient.dll"));
    if (!image) return -1;
    void *tableData = _monoClassFromName(image, (const char *) OBFUSCATE(""),
                                         (const char *) OBFUSCATE("TableData"));
    if (!tableData)
        tableData = _monoClassFromName(image, (const char *) OBFUSCATE("XD.GameGerm.ExpParser"),
                                       (const char *) OBFUSCATE("TableData"));
    if (!tableData) return -1;

    int value = -1;
    void *m = FindMethodInHierarchy(tableData, (const char *) OBFUSCATE("GetCooker"), 2);
    if (m && SMCookMethodIsStatic(m)) {
        int argId = staticId;
        bool needException = false;
        void *args[2] = {&argId, &needException};
        void *exc = nullptr;
        void *row = _monoRuntimeInvoke(m, nullptr, args, &exc);
        if (!exc) value = row ? 1 : 0;
    }
    if (value < 0) {
        m = FindMethodInHierarchy(tableData, (const char *) OBFUSCATE("GetCooker"), 1);
        if (m && SMCookMethodIsStatic(m)) {
            int argId = staticId;
            void *args[1] = {&argId};
            void *exc = nullptr;
            void *row = _monoRuntimeInvoke(m, nullptr, args, &exc);
            if (!exc) value = row ? 1 : 0;
        }
    }
    if (value >= 0) s_known[staticId] = value;
    return value;
}

// 单例：get_Instance() → 静态字段 _instance / Instance
static void *SMCookSingletonInstance(void *klass) {
    if (!klass) return nullptr;
    void *inst = nullptr;
    void *m = FindMethodInHierarchy(klass, (const char *) OBFUSCATE("get_Instance"), 0);
    if (!m) m = FindMethodInHierarchy(klass, (const char *) OBFUSCATE("get_instance"), 0);
    if (m) {
        void *exc = nullptr;
        inst = _monoRuntimeInvoke(m, nullptr, nullptr, &exc);
        if (exc) inst = nullptr;
    }
    if (!inst) {
        void *f = FindFieldInHierarchy(klass, (const char *) OBFUSCATE("_instance"));
        if (!f) f = FindFieldInHierarchy(klass, (const char *) OBFUSCATE("Instance"));
        if (f) {
            void *vtable = _monoClassGetVTable(_monoGetRootDomain(), klass);
            if (vtable) _monoFieldStaticGetValue(vtable, f, &inst);
        }
    }
    return inst;
}

static void SMCookAddTarget(std::vector<CookerTarget> *found, const CookerTarget &target) {
    if (!found) return;
    for (const auto &existing : *found) {
        if (existing.netId == target.netId && existing.burnerId == target.burnerId) return;
    }
    found->push_back(target);
}

// pureFallbackId：字典/列表自带实体 id 时传进来，get_InstanceID 拿不到就用它。
// staticIdOverride：调用方已解好的型号（BuildModule 路的 ComponentData.staticId）。
// 返回是否真的产出了目标。
static bool SMCookPushComponentEx(void *comp, SMCookFieldCache *cache, uint32_t pureFallbackId,
                                  int staticIdOverride, std::vector<CookerTarget> *found) {
    if (!comp || !cache || !found) return false;

    // 组件 → 实体：字段 entity/_entity 优先，其次 get_entity()/get_Entity()
    void *entityObj = nullptr;
    if (cache->f_entity) _monoFieldGetValue(comp, cache->f_entity, &entityObj);
    if (!entityObj && cache->m_getEntity) {
        void *exc0 = nullptr;
        entityObj = _monoRuntimeInvoke(cache->m_getEntity, comp, nullptr, &exc0);
        if (exc0) entityObj = nullptr;
    }
    if (!entityObj) return false;

    void *entKlass = _monoObjectGetClass(entityObj);
    void *m_net =
            entKlass ? FindMethodInHierarchy(entKlass, (const char *) OBFUSCATE("get_netId"), 0)
                     : nullptr;
    void *exc = nullptr;
    void *netObj = m_net ? _monoRuntimeInvoke(m_net, entityObj, nullptr, &exc) : nullptr;
    uint32_t cookerNetId = (netObj && !exc) ? *(uint32_t *) _monoObjectUnbox(netObj) : 0;
    if (cookerNetId == 0) return false;

    // 型号：字段 _cookerStaticId → 调用方解好的 → EntityUtil.GetEntityResId(netId)
    int staticIdVal = 0;
    if (cache->f_staticId) _monoFieldGetValue(comp, cache->f_staticId, &staticIdVal);
    if (staticIdVal <= 0) staticIdVal = staticIdOverride;
    if (staticIdVal <= 0) staticIdVal = SMCookEntityResId(cookerNetId);
    if (staticIdVal <= 0) return false;   // 没有炉子型号 → 不是灶台

    int pureId = 0;
    void *m_iid =
            FindMethodInHierarchy(entKlass, (const char *) OBFUSCATE("get_InstanceID"), 0);
    if (m_iid) {
        exc = nullptr;
        void *res = _monoRuntimeInvoke(m_iid, entityObj, nullptr, &exc);
        if (res && !exc) pureId = *(int *) _monoObjectUnbox(res);
    }
    if (pureId == 0) pureId = (int) pureFallbackId;

    bool haveBurner = false;
    if (cache->f_burnerMap) {
        void *mapObj = nullptr;
        _monoFieldGetValue(comp, cache->f_burnerMap, &mapObj);
        if (mapObj) {
            void *mapKlass = _monoObjectGetClass(mapObj);
            void *f_entries = FindFieldInHierarchy(mapKlass, (const char *) OBFUSCATE("entries"));
            if (!f_entries)
                f_entries = FindFieldInHierarchy(mapKlass, (const char *) OBFUSCATE("_entries"));
            if (f_entries) {
                void *arr = nullptr;
                _monoFieldGetValue(mapObj, f_entries, &arr);
                if (arr) {
                    int len = SMMonoHot::ArrayLength(arr);
                    if (len > 0 && len <= 100000) {
                        uint8_t *base =
                                (uint8_t *) SMMonoHot::ArrayAddrWithSize(arr, 24, 0);
                        if (base) {
                            for (int k = 0; k < len; ++k) {
                                int hashCode = *(int *) (base + k * 24);
                                if (hashCode < 0) continue;
                                uint64_t key = *(uint64_t *) (base + k * 24 + 8);
                                void *cookingComp = *(void **) (base + k * 24 + 16);
                                if (!cookingComp) continue;
                                if (!cache->f_burnerEntity) {
                                    void *cookKlass = _monoObjectGetClass(cookingComp);
                                    cache->f_burnerEntity = FindFieldInHierarchy(
                                            cookKlass, (const char *) OBFUSCATE("entity"));
                                    if (!cache->f_burnerEntity)
                                        cache->f_burnerEntity = FindFieldInHierarchy(
                                                cookKlass, (const char *) OBFUSCATE("_entity"));
                                    cache->f_burnerType = FindFieldInHierarchy(
                                            cookKlass, (const char *) OBFUSCATE("cookerwareType"));
                                    if (!cache->f_burnerType)
                                        cache->f_burnerType = FindFieldInHierarchy(
                                                cookKlass,
                                                (const char *) OBFUSCATE("_cookerwareType"));
                                }
                                int cookTypeVal = 0;
                                if (cache->f_burnerType)
                                    _monoFieldGetValue(cookingComp, cache->f_burnerType,
                                                       &cookTypeVal);
                                void *burnerEntity = nullptr;
                                if (cache->f_burnerEntity)
                                    _monoFieldGetValue(cookingComp, cache->f_burnerEntity,
                                                       &burnerEntity);
                                uint32_t burnerNetId = 0;
                                if (burnerEntity) {
                                    void *burnerKlass = _monoObjectGetClass(burnerEntity);
                                    void *m_bnet = FindMethodInHierarchy(
                                            burnerKlass, (const char *) OBFUSCATE("get_netId"), 0);
                                    if (m_bnet) {
                                        exc = nullptr;
                                        void *res = _monoRuntimeInvoke(m_bnet, burnerEntity,
                                                                       nullptr, &exc);
                                        if (res && !exc)
                                            burnerNetId = *(uint32_t *) _monoObjectUnbox(res);
                                    }
                                }
                                if (burnerNetId == 0) burnerNetId = cookerNetId;
                                uint64_t levelObjectNetId = key;
                                if (levelObjectNetId <= 0xFFFFFFFFULL)
                                    levelObjectNetId |= (1ULL << 32);
                                uint64_t serverId = ((uint64_t) burnerNetId & 0xFFFFFFFFULL) |
                                                    (2ULL << 32);
                                CookerTarget target = {burnerNetId, serverId, levelObjectNetId,
                                                       entityObj, pureId, staticIdVal,
                                                       cookTypeVal};
                                SMCookAddTarget(found, target);
                                haveBurner = true;
                            }
                        }
                    }
                }
            }
        }
    }

    if (!haveBurner) {
        // 没有 _cookBurnerMap 的单锅灶台：按 (2<<32)|pureId 惯例兜底
        uint64_t levelObjectNetId = (1ULL << 32) | (uint64_t) (uint32_t) pureId;
        uint64_t serverId = ((uint64_t) (uint32_t) pureId) | (2ULL << 32);
        CookerTarget target = {cookerNetId, serverId, levelObjectNetId, entityObj, pureId,
                               staticIdVal, 0};
        bool exists = false;
        for (const auto &existing : *found) {
            if (existing.netId == target.netId) {
                exists = true;
                break;
            }
        }
        if (!exists) found->push_back(target);
    }
    return true;
}

// 旧调用点（L/D 路）保持原签名：型号只从 _cookerStaticId 取。
static void SMCookPushComponent(void *comp, SMCookFieldCache *cache, uint32_t pureFallbackId,
                                std::vector<CookerTarget> *found) {
    SMCookPushComponentEx(comp, cache, pureFallbackId, 0, found);
}

// ---------------------------------------------------------------------------
//  BM：XDTGUI.Module.Build.BuildModule.GetBuildItems(uint playerNetId)（静态 1 参）
//      —— 安卓在役版实测唯一能拿到"已摆建筑/家具"清单的入口；元素是构建组件，
//      带 get_entity()，运行时类可能就是 CookBuildComponent（有 _cookerStaticId /
//      _cookBurnerMap）。List 由游戏自己建好，这里只枚举；只有遇到 ref List<T>
//      重载时才用工程既有的三条造 List 路现造一个。
//      返回阶段码（0 = 成功且找到灶台）：
//        1 BuildModule 类未找到             2 GetBuildItems / 单例未找到
//        3 调用抛异常或返回 null            4 List 枚举不出来
//        5 有候选但一个都没判定成灶台（outListed = 候选数）   6 列表为空
// ---------------------------------------------------------------------------
static int SMCookScanViaBuildModule(void *imgGameplay, void *imgData, void *cookKlass,
                                    uint32_t playerNetId, std::vector<CookerTarget> *found,
                                    int *outListed) {
    if (outListed) *outListed = 0;
    if (!found) return 2;
    if (playerNetId == 0) return 3;
    (void) imgGameplay;
    (void) imgData;

    void *klassBM = FindClassInBuildImages("XDTGUI.Module.Build", "BuildModule");
    if (!klassBM) klassBM = FindClassInBuildImages("XDTLevelAndEntity", "BuildModule");
    if (!klassBM) klassBM = FindClassInBuildImages("XDTGameUI.Module.Build", "BuildModule");
    if (!klassBM) return 1;

    void *m_items1 =
            FindMethodInHierarchy(klassBM, (const char *) OBFUSCATE("GetBuildItems"), 1);
    void *m_items2 =
            FindMethodInHierarchy(klassBM, (const char *) OBFUSCATE("GetBuildItems"), 2);
    void *m_items0 =
            FindMethodInHierarchy(klassBM, (const char *) OBFUSCATE("GetBuildItems"), 0);
    if (!m_items1 && !m_items2 && !m_items0) return 2;

    bool needInstance = false;
    // 实例方法必须取到单例才调（绝不用 nullptr 去试探，会 SIGSEGV）；
    // flags 读不出来时按静态处理（与安卓在役版一致）。
    auto invokeItems = [&](void *method, void **args) -> void * {
        if (!method) return nullptr;
        void *obj = nullptr;
        if (!SMCookMethodIsStatic(method)) {
            obj = SMCookSingletonInstance(klassBM);
            if (!obj) {
                needInstance = true;
                return nullptr;
            }
        }
        void *exc = nullptr;
        void *result = _monoRuntimeInvoke(method, obj, args, &exc);
        return exc ? nullptr : result;
    };

    uint32_t netIdArg = playerNetId;
    void *args1[1] = {&netIdArg};
    void *listObj = invokeItems(m_items1, args1);
    if (!listObj && m_items2) {
        // ref/out List<T>：只有这条重载才需要自己造 List（签名解包 → corlib → BCL）
        int why = 1;
        void *listSlot = SMCreateCookList(m_items2, cookKlass, &why);
        if (listSlot) {
            void *args2[2] = {&netIdArg, &listSlot};
            void *obj = nullptr;
            const bool isStatic = SMCookMethodIsStatic(m_items2);
            if (!isStatic) {
                obj = SMCookSingletonInstance(klassBM);
                if (!obj) needInstance = true;
            }
            if (obj || isStatic) {
                void *exc2 = nullptr;
                _monoRuntimeInvoke(m_items2, obj, args2, &exc2);
                if (!exc2) listObj = listSlot;
            }
        }
    }
    if (!listObj && m_items0) listObj = invokeItems(m_items0, nullptr);
    if (!listObj) return needInstance ? 2 : 3;

    void *listKlass = _monoObjectGetClass(listObj);
    if (!listKlass) return 4;
    // 返回类型兜底：List<T>（get_Count/get_Item 或 _items/_size 字段）或托管数组
    const char *listKlassName = SMMonoHot::ClassGetName(listKlass);
    const bool isMonoArray = listKlassName && strstr(listKlassName, "[]") != nullptr;
    void *m_count = isMonoArray
            ? nullptr
            : FindMethodInHierarchy(listKlass, (const char *) OBFUSCATE("get_Count"), 0);
    void *m_item = isMonoArray
            ? nullptr
            : FindMethodInHierarchy(listKlass, (const char *) OBFUSCATE("get_Item"), 1);
    void *f_items = nullptr;
    void *f_size = nullptr;
    if (!isMonoArray && (!m_count || !m_item)) {
        f_items = FindFieldInHierarchy(listKlass, (const char *) OBFUSCATE("_items"));
        if (!f_items) f_items = FindFieldInHierarchy(listKlass, (const char *) OBFUSCATE("items"));
        f_size = FindFieldInHierarchy(listKlass, (const char *) OBFUSCATE("_size"));
        if (!f_size) f_size = FindFieldInHierarchy(listKlass, (const char *) OBFUSCATE("size"));
        if (!f_items || !f_size) return 4;
    }

    int count = 0;
    if (isMonoArray) {
        count = SMMonoHot::ArrayLength(listObj);
    } else if (m_count) {
        void *exc3 = nullptr;
        void *boxed = _monoRuntimeInvoke(m_count, listObj, nullptr, &exc3);
        if (boxed && !exc3) count = *(int *) _monoObjectUnbox(boxed);
    } else {
        _monoFieldGetValue(listObj, f_size, &count);
    }
    if (count <= 0) return 6;
    if (count > 20000) return 4;
    if (outListed) *outListed = count;

    void *itemsArr = nullptr;
    if (isMonoArray) itemsArr = listObj;
    else if (!m_item && f_items) _monoFieldGetValue(listObj, f_items, &itemsArr);

    void *lastKlass = nullptr;
    SMCookFieldCache cache;
    for (int i = 0; i < count; ++i) {
        void *comp = nullptr;
        if (m_item) {
            int index = i;
            void *indexArgs[1] = {&index};
            void *exc4 = nullptr;
            comp = _monoRuntimeInvoke(m_item, listObj, indexArgs, &exc4);
            if (exc4) comp = nullptr;
        } else if (itemsArr) {
            comp = *(void **) SMMonoHot::ArrayAddrWithSize(itemsArr, (int) sizeof(void *),
                                                           (uintptr_t) i);
        }
        if (!comp) continue;
        void *itemKlass = _monoObjectGetClass(comp);
        if (!itemKlass) continue;
        if (itemKlass != lastKlass) {
            lastKlass = itemKlass;
            cache = SMCookFieldCache();
            SMCookResolveFields(itemKlass, &cache);
        }
        int staticIdOverride = SMCookStaticIdFromComponentData(comp, &cache);
        const bool cookMarker = (cache.f_burnerMap != nullptr) || (cache.f_staticId != nullptr);
        if (!cookMarker) {
            // 不是 CookBuildComponent：只认型号表里确实是灶台的，别把墙/地板/家具收进来
            if (SMCookTableCookerKnown(staticIdOverride) != 1) continue;
        }
        SMCookPushComponentEx(comp, &cache, 0, staticIdOverride, found);
        if (found->size() >= 256) break;
    }
    return found->empty() ? 5 : 0;
}

// ---------------------------------------------------------------------------
//  D：LevelObjectManager._instance._dictionary 直接遍历 LevelObject（最后兜底）
//     - 有 cooker 表（g_cookerNamesCache）时按 staticId 直判，不再多花 invoke；
//     - 没表时就地取实体上的 CookBuildComponent 探一次（_cookerStaticId>0 即灶台）。
// ---------------------------------------------------------------------------
static bool SMCookScanViaLevelObjects(void *imgGameplay, void *imgData, void *entitiesCls,
                                      void *cookKlass, std::vector<CookerTarget> *found) {
    if (!found || !cookKlass) return false;
    void *lomKlass = nullptr;
    static const char *kLevelObjectNamespaces[] = {"ScriptsRefactory.LevelAndEntity",
                                                   "XDTLevelAndEntity",
                                                   "XDTLevelAndEntity.Core.Level"};
    for (const char *ns : kLevelObjectNamespaces) {
        if (!lomKlass && imgData)
            lomKlass = _monoClassFromName(imgData, ns,
                                          (const char *) OBFUSCATE("LevelObjectManager"));
        if (!lomKlass && imgGameplay)
            lomKlass = _monoClassFromName(imgGameplay, ns,
                                          (const char *) OBFUSCATE("LevelObjectManager"));
        if (lomKlass) break;
    }
    if (!lomKlass) return false;

    void *lomInst = nullptr;
    void *m_inst = FindMethodInHierarchy(lomKlass, (const char *) OBFUSCATE("get_Instance"), 0);
    if (m_inst) lomInst = _monoRuntimeInvoke(m_inst, nullptr, nullptr, nullptr);
    if (!lomInst) {
        void *f_inst = FindFieldInHierarchy(lomKlass, (const char *) OBFUSCATE("_instance"));
        if (f_inst) {
            void *vtable = _monoClassGetVTable(_monoGetRootDomain(), lomKlass);
            if (vtable) _monoFieldStaticGetValue(vtable, f_inst, &lomInst);
        }
    }
    if (!lomInst) return false;

    void *lomInstKlass = _monoObjectGetClass(lomInst);
    if (!lomInstKlass) return false;
    void *f_dict = FindFieldInHierarchy(lomInstKlass, (const char *) OBFUSCATE("_dictionary"));
    if (!f_dict) return false;
    void *dictObj = nullptr;
    _monoFieldGetValue(lomInst, f_dict, &dictObj);
    if (!dictObj) return false;
    void *dictKlass = _monoObjectGetClass(dictObj);
    if (!dictKlass) return false;
    void *f_entries = FindFieldInHierarchy(dictKlass, (const char *) OBFUSCATE("entries"));
    if (!f_entries) f_entries = FindFieldInHierarchy(dictKlass, (const char *) OBFUSCATE("_entries"));
    if (!f_entries) return false;
    void *entriesArr = nullptr;
    _monoFieldGetValue(dictObj, f_entries, &entriesArr);
    if (!entriesArr) return false;
    const int length = SMMonoHot::ArrayLength(entriesArr);
    uint8_t *data = (uint8_t *) SMMonoHot::ArrayAddrWithSize(entriesArr, SZ_DICT_ENTRY, 0);
    if (length <= 0 || length > 100000 || !data) return false;

    SMCookFieldCache cache;
    SMCookResolveFields(cookKlass, &cache);
    const bool hasCookerTable = !g_cookerNamesCache.empty();

    // 探测用：实体查找 + GetComponent<CookBuildComponent>（只膨胀一次）
    void *m_getAnyEntity =
            entitiesCls ? FindMethodInHierarchy(entitiesCls, (const char *) OBFUSCATE("GetAnyEntity"),
                                                1)
                        : nullptr;
    void *m_getComponent = nullptr;
    int probes = 0;
    const int maxProbes = 800;

    for (int i = 0; i < length && found->size() < 64; ++i) {
        const int hashCode = *(int *) (data + (i * SZ_DICT_ENTRY));
        if (hashCode < 0) continue;
        const uint64_t levelObjectKey = *(uint64_t *) (data + (i * SZ_DICT_ENTRY) + 8);
        void *levelObject = *(void **) (data + (i * SZ_DICT_ENTRY) + 16);
        if (!levelObject) continue;
        void *loKlass = _monoObjectGetClass(levelObject);
        if (!loKlass) continue;

        uint32_t ownerNetId = 0;
        void *m_owner = FindMethodInHierarchy(loKlass, (const char *) OBFUSCATE("get_ownerNetId"), 0);
        if (m_owner) {
            void *res = _monoRuntimeInvoke(m_owner, levelObject, nullptr, nullptr);
            if (res) ownerNetId = *(uint32_t *) _monoObjectUnbox(res);
        }
        int staticId = 0;
        void *m_staticId = FindMethodInHierarchy(loKlass, (const char *) OBFUSCATE("get_staticId"), 0);
        if (m_staticId) {
            void *res = _monoRuntimeInvoke(m_staticId, levelObject, nullptr, nullptr);
            if (res) staticId = *(int *) _monoObjectUnbox(res);
        }

        if (hasCookerTable && staticId > 0 &&
            g_cookerNamesCache.find(staticId) != g_cookerNamesCache.end()) {
            uint64_t levelObjectNetId = levelObjectKey;
            if (levelObjectNetId <= 0xFFFFFFFFULL) levelObjectNetId |= (1ULL << 32);
            if (ownerNetId == 0) ownerNetId = (uint32_t) (levelObjectKey & 0xFFFFFFFFULL);
            uint64_t serverId = ((uint64_t) ownerNetId & 0xFFFFFFFFULL) | (2ULL << 32);
            CookerTarget target;
            target.netId = ownerNetId;
            target.serverId = serverId;
            target.burnerId = levelObjectNetId;
            target.entityPtr = levelObject;
            target.pureId = (int) ownerNetId;
            target.staticId = staticId;
            target.cookType = 0;
            SMCookAddTarget(found, target);
            continue;
        }

        // 没有 cooker 表（或表里没这个 id）：拿实体上的 CookBuildComponent 探一次
        if (hasCookerTable || ownerNetId == 0) continue;
        if (!m_getAnyEntity || probes >= maxProbes) continue;
        ++probes;
        uint32_t argNetId = ownerNetId;
        void *entArgs[1] = {&argNetId};
        void *entity = _monoRuntimeInvoke(m_getAnyEntity, nullptr, entArgs, nullptr);
        if (!entity) continue;
        if (!m_getComponent) {
            void *entKlass = _monoObjectGetClass(entity);
            void *openGet =
                    entKlass ? FindMethodInHierarchy(entKlass, (const char *) OBFUSCATE("GetComponent"), 1)
                             : nullptr;
            void *cookType = openGet ? _monoClassGetType(cookKlass) : nullptr;
            if (cookType) {
                void *typeArgs[1] = {cookType};
                void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
                if (methodInst) {
                    MonoGenericContext ctx;
                    ctx.class_inst = nullptr;
                    ctx.method_inst = methodInst;
                    m_getComponent = _mono_class_inflate_generic_method(openGet, &ctx);
                    if (m_getComponent) _monoCompileMethod(m_getComponent);
                }
            }
            if (!m_getComponent) break;
        }
        bool includeChildren = false;
        void *compArgs[1] = {&includeChildren};
        void *comp = _monoRuntimeInvoke(m_getComponent, entity, compArgs, nullptr);
        if (!comp) continue;
        SMCookPushComponent(comp, &cache, ownerNetId, found);
    }
    return !found->empty();
}

void ScanCookersNative() {
    // 扫描链（优先级从高到低）：
    //   BM  XDTGUI.Module.Build.BuildModule.GetBuildItems(playerNetId)（安卓真机实测可用）
    //   L   Entities.GetComponents<CookBuildComponent>(ref List<T>)（PC 权威，List 可能造不出来）
    //   D   LevelObjectManager._instance._dictionary（最后兜底）
    // 组件 → CookerTarget 统一走 SMCookPushComponentEx：
    //   netId(get_netId) / burnerId((1<<32)|_cookBurnerMap.Key) / staticId / cookType
    // 面包屑：cook:bm:N（BM 路）/ cook:found:N:L|D / cook:bm-fail:<stage>。
    RunOnUnityThreadAsync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        if (!g_cookPn.empty()) FetchCookerNamesFromTable();
        if (g_gardenPlayerNetId == 0) GetPlayerNetIdOnUnityThread();

        void *imgGameplay = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
        if (!imgGameplay) imgGameplay = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity.dll"));
        if (!imgGameplay) { SMPlatform::SetBreadcrumbLight("cook:no-img"); return; }

        void *cookKlass = nullptr;
        {
            static const char *kCookNsCandidates[] = {
                "XDTLevelAndEntity.Gameplay.Component.Homeland",
                "ScriptsRefactory.LevelAndEntity.Gameplay.Component.Homeland",
                "XDTLevelAndEntity.Gameplay.Component.GamePlay",
            };
            for (const char *ns : kCookNsCandidates) {
                cookKlass = _monoClassFromName(imgGameplay, ns,
                    (const char *) OBFUSCATE("CookBuildComponent"));
                if (cookKlass) break;
            }
        }

        void *imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
        if (!imgData) imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol.dll"));

        std::vector<CookerTarget> found;
        const char *source = nullptr;
        int bmStage = 0;
        int bmListed = 0;
        int listWhy = 1;
        bool listBuilt = false;
        int listCount = 0;
        void *entitiesCls = nullptr;

        // ---------------- 路线 BM：BuildModule.GetBuildItems（安卓实测可用） ----------------
        bmStage = SMCookScanViaBuildModule(imgGameplay, imgData, cookKlass,
                                           g_gardenPlayerNetId, &found, &bmListed);
        if (!found.empty()) source = "BM";

        // ---------------- 路线 L：Entities.GetComponents<T>(ref List<T>) ----------------
        if (found.empty() && cookKlass) {
            void *imgs[2] = {imgGameplay, imgData};
            for (int ii = 0; ii < 2 && !entitiesCls; ++ii) {
                if (!imgs[ii]) continue;
                entitiesCls = _monoClassFromName(imgs[ii],
                    (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
                    (const char *) OBFUSCATE("Entities"));
            }
            if (entitiesCls) {
                void *openGet = FindMethodInHierarchy(entitiesCls, (const char *) OBFUSCATE("GetComponents"), 1);
                void *cookType = _monoClassGetType(cookKlass);
                void *closed = nullptr;
                if (openGet && cookType) {
                    void *typeArgs[1] = {cookType};
                    void *genInst = _mono_metadata_get_generic_inst(1, typeArgs);
                    if (genInst) {
                        MonoGenericContext ctx;
                        ctx.class_inst = nullptr;
                        ctx.method_inst = genInst;
                        closed = _mono_class_inflate_generic_method(openGet, &ctx);
                        if (closed) _monoCompileMethod(closed);
                    }
                }
                if (closed) {
                    void *listObj = SMCreateCookList(closed, cookKlass, &listWhy);
                    if (listObj) {
                        listBuilt = true;
                        void *listSlot = listObj;
                        void *getArgs[1] = {&listSlot};
                        void *exc = nullptr;
                        _monoRuntimeInvoke(closed, nullptr, getArgs, &exc);
                        listObj = listSlot;
                        void *listKlass = listObj ? _monoObjectGetClass(listObj) : nullptr;
                        void *m_count = listKlass ? FindMethodInHierarchy(listKlass, (const char *) OBFUSCATE("get_Count"), 0) : nullptr;
                        void *m_item = listKlass ? FindMethodInHierarchy(listKlass, (const char *) OBFUSCATE("get_Item"), 1) : nullptr;
                        if (listObj && !exc && m_count && m_item) {
                            exc = nullptr;
                            void *cntObj = _monoRuntimeInvoke(m_count, listObj, nullptr, &exc);
                            int count = (cntObj && !exc) ? *(int *) _monoObjectUnbox(cntObj) : 0;
                            if (count > 100000) count = 0;
                            listCount = count;
                            SMCookFieldCache cache;
                            SMCookResolveFields(cookKlass, &cache);
                            for (int i = 0; i < count; ++i) {
                                int index = i;
                                void *iArgs[1] = {&index};
                                exc = nullptr;
                                void *comp = _monoRuntimeInvoke(m_item, listObj, iArgs, &exc);
                                if (!comp || exc) continue;
                                SMCookPushComponent(comp, &cache, 0, &found);
                            }
                            if (!found.empty()) source = "L";
                        }
                    }
                }
            }
        }

        // ---------------- 路线 D：LevelObjectManager._instance._dictionary ----------------
        if (found.empty() && cookKlass) {
            if (SMCookScanViaLevelObjects(imgGameplay, imgData, entitiesCls, cookKlass, &found))
                source = "D";
        }

        if (!found.empty()) {
            g_availableCookers = found;
            const std::string cnt = std::to_string((int) found.size());
            if (source && strcmp(source, "BM") == 0)
                SMPlatform::SetBreadcrumbLight("cook:bm:" + cnt);
            else
                SMPlatform::SetBreadcrumbLight("cook:found:" + cnt + ":" + (source ? source : "?"));
            return;
        }

        // 全灭：把 BM 的失败阶段带出来（5 再附候选数），后面附 L 路阶段
        std::string bc = "cook:bm-fail:" + std::to_string(bmStage);
        if (bmStage == 5 && bmListed > 0) bc += "/" + std::to_string(bmListed);
        if (listBuilt) bc += ":L" + std::to_string(listCount);
        else if (cookKlass) bc += ":L" + std::to_string(listWhy);
        SMPlatform::SetBreadcrumbLight(bc);
    });
}

void GetRecipesNative(int targetCookerId) {
    RunOnUnityThreadAsync([targetCookerId]() {
        _monoThreadAttach(_monoGetRootDomain());

        std::vector<RecipeTarget> tempRecipes;
        void *imgGameSys = _monoImageLoaded(CookN("n_imgGameSys").c_str());
        void *klassCookingSys = _monoClassFromName(imgGameSys, CookN("n_nsCooking").c_str(), CookN("n_clsCookingSys").c_str());

        void *m_getInstance = FindMethodInHierarchy(klassCookingSys, CookN("n_getInstance").c_str(), 0);
        if (!m_getInstance)
            m_getInstance = FindMethodInHierarchy(klassCookingSys, CookN("n_getInstance2").c_str(), 0);

        void *exc = nullptr;
        void *cookingSysInst = _monoRuntimeInvoke(m_getInstance,
                                                  nullptr, nullptr, &exc);
        if (!cookingSysInst) return;

        void *m_GetAllRecipes = FindMethodInHierarchy(klassCookingSys, CookN("n_getAllRecipes").c_str(),
                                                      1);

        // Gunakan parameter dari UI
        int cookerId = targetCookerId;
        void *args[1] = {&cookerId};

        void *listObj = _monoRuntimeInvoke(m_GetAllRecipes, cookingSysInst, args, &exc);
        if (!listObj || exc) return;

        void *listKlass = _monoObjectGetClass(listObj);
        void *f_items = _monoClassGetFieldFromName(listKlass, CookN("n_fItems").c_str());
        void *f_size = _monoClassGetFieldFromName(listKlass, CookN("n_fSize").c_str());
        int listSize = 0;
        void *itemsArray = nullptr;
        _monoFieldGetValue(listObj, f_size, &listSize);
        _monoFieldGetValue(listObj, f_items, &itemsArray);
        if (listSize == 0 || !itemsArray) return;

        uint8_t *arrayData = (uint8_t *) itemsArray + OFF_ARR_DATA;
        for (int i = 0; i < listSize; i++) {
            int recipeId = *(int *) (arrayData + (i * SZ_LIST_ENTRY));
            void *namePtr = *(void **) (arrayData + (i * SZ_LIST_ENTRY) + OFF_ENTRY_KEY);
            std::string recipeName = ReadMonoStringNative(namePtr);
            RecipeTarget rt = {recipeId, recipeName};
            tempRecipes.push_back(rt);
        }

        g_availableRecipes = tempRecipes;
    });
}

void ExecChefAction(std::string methodName, uint32_t netId, uint64_t targetId) {
    RunOnUnityThreadAsync([methodName, netId, targetId]() mutable {
        _monoThreadAttach(_monoGetRootDomain());
        void *img = _monoImageLoaded(CookN("n_imgData").c_str());
        void *klass = _monoClassFromName(img,
                                         CookN("n_nsProtoCook").c_str(),
                                         CookN("n_clsProtoMgr").c_str());
        void *method = FindMethodInHierarchy(klass, methodName.c_str(), 2);

        if (method) {
            void *args[2] = {&netId, &targetId};

            void *exc = nullptr;
            _monoRuntimeInvoke(method, nullptr, args, &exc);
        }
    });
}

int GetCookerStatusNative(CookerTarget cooker) {

    int statusResult = 0;
    RunOnUnityThreadSync([&]() {
        auto FindField = [](void *klass, const char *name) -> void * {
            void *current = klass;
            while (current != nullptr) {
                void *field = _monoClassGetFieldFromName(current, name);
                if (field != nullptr) return field;

                current = _monoClassGetParent(current);
            }
            return nullptr;
        };

        auto FindMethod = [](void *klass, const char *name, int paramCount) -> void * {
            void *current = klass;
            while (current != nullptr) {

                void *method = _monoClassGetMethodFromName(current, name, paramCount);
                if (method != nullptr) return method;
                current = _monoClassGetParent(current);
            }
            return nullptr;
        };

        void *imgGameplay = _monoImageLoaded(CookN("n_imgLevel").c_str());

        if (!imgGameplay) imgGameplay = _monoImageLoaded(CookN("n_imgLevelDll").c_str());
        if (!imgGameplay) return;
// ========================================================
        // TAHAP 1: BUKA LACI MASTER ECS
        // ========================================================
        void *cookKlass = _monoClassFromName(imgGameplay, CookN("n_nsHomeland").c_str(), CookN("n_clsCookBuild").c_str());
        if (!cookKlass) return;

        void *cookMonoType = _monoClassGetType(cookKlass);
        void *imgData = _monoImageLoaded(CookN("n_imgData").c_str());
        void *worldKlass = _monoClassFromName(imgData, CookN("n_nsWorld").c_str(),
                                              CookN("n_clsEntityWorld").c_str());
        void *openMethod = FindMethod(worldKlass, CookN("n_getCompSys").c_str(), 0);
        if (!openMethod) return;

        void *typeArgs[1] = {cookMonoType};
        void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
        MonoGenericContext context;
        context.class_inst = nullptr;
        context.method_inst = methodInst;

        void *closedMethod = _mono_class_inflate_generic_method(openMethod, &context);
        if (closedMethod) _monoCompileMethod(closedMethod);   // 解释器模式必须先编译

        void *exc = nullptr;
        void *sysObj = _monoRuntimeInvoke(closedMethod, nullptr, nullptr, &exc);
        if (exc || !sysObj) return;
// ========================================================
        // TAHAP 2: CARI KOMPOR KITA BERDASARKAN pureId (O(1) Memory Parsing)
        // ========================================================
        void *sysKlass = _monoObjectGetClass(sysObj);
        void *f_components = FindField(sysKlass, CookN("n_fComponents").c_str());
        if (!f_components) return;

        void *dictObj = nullptr;
        _monoFieldGetValue(sysObj, f_components, &dictObj);
        if (!dictObj) return;
        void *dictKlass = _monoObjectGetClass(dictObj);
        void *f_entries = FindField(dictKlass, CookN("n_entries").c_str());
        if (!f_entries) f_entries = FindField(dictKlass, CookN("n_entriesU").c_str());
        if (!f_entries) return;
        void *entriesArr = nullptr;
        _monoFieldGetValue(dictObj, f_entries, &entriesArr);
        if (!entriesArr) return;

        int length = *(int *) ((uint8_t *) entriesArr + OFF_ARR_COUNT);
        uint8_t *arrayDataStart = (uint8_t *) entriesArr + OFF_ARR_DATA;
        void *cookBuildInst = nullptr;
        for (int i = 0; i < length; i++) {
            int hashCode = *(int *) (arrayDataStart + (i * SZ_DICT_ENTRY));
            if (hashCode >= 0) {
                // Di dalam ECS, Key Dictionary adalah ID Entity murni (pureId)
                uint32_t keyId = *(uint32_t * )(arrayDataStart + (i * SZ_DICT_ENTRY) + OFF_ENTRY_KEY);
// Jika ID cocok, tembak pointernya!
                if (keyId == (uint32_t) cooker.pureId) {
                    cookBuildInst = *(void **) (arrayDataStart + (i * SZ_DICT_ENTRY) + OFF_ENTRY_VALUE);
                    break;
                }
            }
        }

        if (!cookBuildInst) {
            // Kita hapus LOGE di sini, karena kalau kompor jauh / dihapus player,
            // biarkan saja nilainya 0 (Idle) agar tidak spamming log.
            return;
        }

        // ========================================================
        // TAHAP 3: BACA STATUS TUNGKU API (GetCooingComponent)
        // ========================================================
        void *m_GetCooing = FindMethod(_monoObjectGetClass(cookBuildInst),
                                       CookN("n_getCooing").c_str(), 1);
        if (!m_GetCooing)
            m_GetCooing = FindMethod(_monoObjectGetClass(cookBuildInst),
                                     CookN("n_getCooing2").c_str(), 1);
        if (!m_GetCooing) return;

        void *cooingArgs[1] = {&cooker.burnerId};
        void *cookingCompInst = _monoRuntimeInvoke(m_GetCooing, cookBuildInst, cooingArgs, &exc);

        if (exc || !cookingCompInst) return;

        void *m_get_CompData = FindMethod(_monoObjectGetClass(cookingCompInst),
                                          CookN("n_getCompData").c_str(), 0);
        if (!m_get_CompData) return;

        void *boxedData = _monoRuntimeInvoke(m_get_CompData, cookingCompInst, nullptr, nullptr);
        if (!boxedData) return;

        void *dataKlass = _monoObjectGetClass(boxedData);
        void *iter = nullptr;
        void *fieldPtr = nullptr;
        void *targetField = nullptr;
        while ((fieldPtr = _monoClassGetFields(dataKlass, &iter)) != nullptr) {
            const char *fName = _monoFieldName(fieldPtr);
            if (strcmp(fName, OBFUSCATE("Status")) == 0 ||
                strcmp(fName, OBFUSCATE("status")) == 0) {
                targetField = fieldPtr;
                break;
            }
        }

        if (targetField) {
            int rawOffset = _monoFieldGetOffset(targetField);
            int actualOffset = rawOffset - MONO_FIELD_HEADER;
            uint8_t *rawData = (uint8_t *) _monoObjectUnbox(boxedData);
            statusResult = *(int *) (rawData + actualOffset);
        }
    });
    return statusResult;
}

// ================= BIRD WATCHING (official client flow) =================
// Android sengaja tidak meng-hook InformTakingPhotoResult pada code heap Mono.
// Dobby pada method JIT ini menyebabkan SEGV_ACCERR di mono_runtime_invoke.

static uint64_t BirdNowMs() {
    return (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int g_birdObservedPhotoCount = -1;
static int g_birdTargetPhotoMaxCount = -1;
static int g_birdLocalPhotoAttempts = 0;
static bool g_birdCounterAvailable = false;

static bool ReadBirdPhotoedCount(uint32_t netId, int &photoedCount,
                                int *continuousCount = nullptr) {
    if (!netId || !_monoClassValueSize || !_mono_metadata_get_generic_inst ||
        !_mono_class_inflate_generic_method || !_monoClassGetType) return false;

    static void *s_inflated = nullptr;
    static void *s_stateClass = nullptr;
    static int s_structSize = 0;
    static int s_photoOffset = -1;
    static int s_contOffset = -1;

    if (!s_inflated) {
        void *dataImage = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        void *ecsImage = _monoImageLoaded(OBFUSCATE("EcsClient"));
        if (!dataImage || !ecsImage) return false;
        void *dataCenter = _monoClassFromName(dataImage,
                OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("DataCenter"));
        void *openMethod = dataCenter ? FindMethodInHierarchy(dataCenter,
                OBFUSCATE("TryGetComponentData"), 2) : nullptr;
        s_stateClass = _monoClassFromName(ecsImage,
                OBFUSCATE("XDT.Scene.Shared.Modules.BirdWatching"),
                OBFUSCATE("BirdAIStateComponent"));
        if (!openMethod || !s_stateClass) return false;

        void *typeArgs[1] = {_monoClassGetType(s_stateClass)};
        void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
        MonoGenericContext context{nullptr, methodInst};
        s_inflated = _mono_class_inflate_generic_method(openMethod, &context);
        uint32_t align = 0;
        s_structSize = _monoClassValueSize(s_stateClass, &align);
        void *photoField = _monoClassGetFieldFromName(s_stateClass,
                OBFUSCATE("photoedCount"));
        void *contField = _monoClassGetFieldFromName(s_stateClass,
                OBFUSCATE("continuPhotoedCount"));
        s_photoOffset = photoField ? _monoFieldGetOffset(photoField) - MONO_FIELD_HEADER : -1;
        s_contOffset = contField ? _monoFieldGetOffset(contField) - MONO_FIELD_HEADER : -1;
        if (!s_inflated || s_structSize <= 0 || s_structSize > 2048 ||
            s_photoOffset < 0 || s_photoOffset + 4 > s_structSize) {
            s_inflated = nullptr;
            return false;
        }
    }

    std::vector<uint8_t> buffer((size_t) s_structSize, 0);
    uint32_t id = netId;
    void *args[] = {&id, buffer.data()};
    void *exc = nullptr;
    void *result = _monoRuntimeInvoke(s_inflated, nullptr, args, &exc);
    if (!result || exc || !*(bool *) _monoObjectUnbox(result)) return false;

    photoedCount = *(int *) (buffer.data() + s_photoOffset);
    if (continuousCount && s_contOffset >= 0 && s_contOffset + 4 <= s_structSize)
        *continuousCount = *(int *) (buffer.data() + s_contOffset);
    return photoedCount >= 0;
}

static int ReadBirdPhotoMaxCount(int staticId) {
    if (staticId <= 0) return -1;
    static std::unordered_map<int, int> cache;
    auto cached = cache.find(staticId);
    if (cached != cache.end()) return cached->second;

    void *image = _monoImageLoaded(OBFUSCATE("EcsClient"));
    void *tableData = image ? _monoClassFromName(image, OBFUSCATE(""),
            OBFUSCATE("TableData")) : nullptr;
    if (!tableData && image)
        tableData = _monoClassFromName(image, OBFUSCATE("XD.GameGerm.ExpParser"),
                                       OBFUSCATE("TableData"));
    if (!tableData) return -1;

    void *dict = nullptr;
    void *getter = FindMethodInHierarchy(tableData, OBFUSCATE("get_TableBirds"), 0);
    if (getter) {
        void *exc = nullptr;
        dict = _monoRuntimeInvoke(getter, nullptr, nullptr, &exc);
        if (exc) dict = nullptr;
    }
    if (!dict) {
        void *field = _monoClassGetFieldFromName(tableData, OBFUSCATE("TableBirds"));
        void *domain = _monoGetRootDomain();
        void *vtable = field && domain ? _monoClassGetVTable(domain, tableData) : nullptr;
        if (field && vtable) _monoFieldStaticGetValue(vtable, field, &dict);
    }
    if (!dict) return -1;

    void *dictClass = _monoObjectGetClass(dict);
    void *entriesField = dictClass ? _monoClassGetFieldFromName(dictClass,
            OBFUSCATE("_entries")) : nullptr;
    if (!entriesField && dictClass)
        entriesField = _monoClassGetFieldFromName(dictClass, OBFUSCATE("entries"));
    void *entries = nullptr;
    if (entriesField) _monoFieldGetValue(dict, entriesField, &entries);
    if (!entries) return -1;

    int length = SafeRead<int>((uintptr_t) entries + OFF_ARR_COUNT, 0);
    uintptr_t data = (uintptr_t) entries + OFF_ARR_DATA;
    if (length <= 0 || length > 100000) return -1;
    for (int i = 0; i < length; ++i) {
        uintptr_t slot = data + (uintptr_t) i * SZ_DICT_ENTRY;
        if (SafeRead<int>(slot, -1) < 0) continue;
        int key = SafeRead<int>(slot + OFF_ENTRY_KEY, 0);
        if (key != staticId) continue;
        void *bird = SafeRead<void *>(slot + OFF_ENTRY_VALUE, nullptr);
        void *birdClass = bird ? _monoObjectGetClass(bird) : nullptr;
        void *maxField = birdClass ? _monoClassGetFieldFromName(birdClass,
                OBFUSCATE("photoMaxCount")) : nullptr;
        int maxCount = -1;
        if (maxField) _monoFieldGetValue(bird, maxField, &maxCount);
        if (maxCount > 0) cache[staticId] = maxCount;
        return maxCount;
    }
    return -1;
}

static void ResetBirdTarget(const char *status, uint64_t delayMs) {
    g_birdCurrentTarget = {};
    g_birdRequestPending.store(false, std::memory_order_release);
    g_birdRetryCount = 0;
    g_birdRequestSentAt = 0;
    g_birdObservedPhotoCount = -1;
    g_birdTargetPhotoMaxCount = -1;
    g_birdLocalPhotoAttempts = 0;
    g_birdCounterAvailable = false;
    g_birdNextActionAt = BirdNowMs() + delayMs;
    g_birdFarmStatus = status ? status : "SCANNING";
}

static bool IsBirdPhotoStateValid(int state) {
    return state < 0 || state == 0 || state == 1 || state == 2 || state == 7;
}

static void *FindEntityComponentByName(void *entity, const char *wantedName) {
    if (!entity || !wantedName) return nullptr;
    void *entityClass = _monoObjectGetClass(entity);
    void *getAll = entityClass ? FindMethodInHierarchy(entityClass,
            OBFUSCATE("GetAllComponents"), 0) : nullptr;
    void *list = getAll ? _monoRuntimeInvoke(getAll, entity, nullptr, nullptr) : nullptr;
    if (!list) return nullptr;
    void *listClass = _monoObjectGetClass(list);
    void *getCount = FindMethodInHierarchy(listClass, OBFUSCATE("get_Count"), 0);
    void *getItem = FindMethodInHierarchy(listClass, OBFUSCATE("get_Item"), 1);
    if (!getCount || !getItem) return nullptr;
    void *countObj = _monoRuntimeInvoke(getCount, list, nullptr, nullptr);
    int count = countObj ? *(int *) _monoObjectUnbox(countObj) : 0;
    for (int i = 0; i < count; ++i) {
        void *args[] = {&i};
        void *component = _monoRuntimeInvoke(getItem, list, args, nullptr);
        if (!component) continue;
        void *componentClass = _monoObjectGetClass(component);
        const char *name = componentClass ? _monoClassGetName(componentClass) : nullptr;
        if (name && strcmp(name, wantedName) == 0) return component;
    }
    return nullptr;
}

static int ReadBirdState(void *entity) {
    void *component = FindEntityComponentByName(entity, OBFUSCATE("BirdComponent"));
    if (!component) return -2;
    void *klass = _monoObjectGetClass(component);
    void *field = klass ? _monoClassGetFieldFromName(klass, OBFUSCATE("_state")) : nullptr;
    if (!field) return -2;
    return SafeRead<int>((uintptr_t) component + _monoFieldGetOffset(field), -2);
}

static int ReadBirdPose(void *scannable) {
    if (!scannable) return 0;
    void *klass = _monoObjectGetClass(scannable);
    void *field = klass ? _monoClassGetFieldFromName(klass, OBFUSCATE("_data")) : nullptr;
    if (!field) return 0;
    return SafeRead<int>((uintptr_t) scannable + _monoFieldGetOffset(field), 0);
}

static uint32_t ReadBirdStandNetId(void *entity) {
    void *component = FindEntityComponentByName(entity, OBFUSCATE("PerchBirdComponent"));
    if (!component) return 0;
    void *klass = _monoObjectGetClass(component);
    void *getPerch = klass ? FindMethodInHierarchy(klass, OBFUSCATE("get_perch"), 0) : nullptr;
    void *perch = getPerch ? _monoRuntimeInvoke(getPerch, component, nullptr, nullptr) : nullptr;
    if (!perch) return 0;
    void *standEntity = NativeGetObj(perch, {OBFUSCATE("get_entity")},
                                     {OBFUSCATE("entity"), OBFUSCATE("_entity")});
    return standEntity ? NativeGetNetId(standEntity) : 0;
}

static bool ResolveBirdTarget(BirdTarget &selected) {
    _monoThreadAttach(_monoGetRootDomain());
    if (!g_StolenInteractSystem) {
        void *image = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
        void *klass = image ? _monoClassFromName(image,
                OBFUSCATE("XDTLevelAndEntity.BaseSystem.InteractSystem"),
                OBFUSCATE("InteractSystem")) : nullptr;
        void *getInstance = klass ? FindMethodInHierarchy(klass,
                OBFUSCATE("get_Instance"), 0) : nullptr;
        g_StolenInteractSystem = getInstance
                ? _monoRuntimeInvoke(getInstance, nullptr, nullptr, nullptr) : nullptr;
    }
    if (!g_StolenInteractSystem) {
        g_birdFarmStatus = u8"等待进入世界";
        return false;
    }

    void *player = NativeGetObj(g_StolenInteractSystem,
            {OBFUSCATE("get_player"), OBFUSCATE("GetPlayer")},
            {OBFUSCATE("player"), OBFUSCATE("_player"), OBFUSCATE("Player")});
    if (!player) return false;
    void *handhold = NativeGetObj(g_StolenInteractSystem, {},
            {OBFUSCATE("_handhold"), OBFUSCATE("handhold")});
    if (!handhold) {
        void *equip = NativeGetObj(player,
                {OBFUSCATE("get_equipComponent"), OBFUSCATE("GetEquipComponent")},
                {OBFUSCATE("equipComponent"), OBFUSCATE("_equipComponent")});
        if (equip) handhold = NativeGetObj(equip,
                {OBFUSCATE("get_handhold"), OBFUSCATE("GetHandhold")},
                {OBFUSCATE("handhold"), OBFUSCATE("_handhold"), OBFUSCATE("HoldObj")});
    }
    if (!handhold) {
        g_birdFarmStatus = u8"请先装备观鸟镜";
        return false;
    }
    const char *heldName = _monoClassGetName(_monoObjectGetClass(handhold));
    const std::string held = heldName ? heldName : "";
    if (held.find((const char *) OBFUSCATE("Scanner")) == std::string::npos &&
        held.find((const char *) OBFUSCATE("Bird")) == std::string::npos) {
        g_birdFarmStatus = u8"请先装备观鸟镜";
        return false;
    }

    void *levelImage = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    void *apiClass = levelImage ? _monoClassFromName(levelImage,
            OBFUSCATE("XDTLevelAndEntity.GameplaySystem"), OBFUSCATE("GameplayApi")) : nullptr;
    void *getPhotoMode = apiClass ? FindMethodInHierarchy(apiClass,
            OBFUSCATE("get_photoMode"), 0) : nullptr;
    void *exc = nullptr;
    void *photoMode = getPhotoMode
            ? _monoRuntimeInvoke(getPhotoMode, nullptr, nullptr, &exc) : nullptr;
    if (!photoMode || exc) {
        g_birdFarmStatus = u8"拍照模式未就绪";
        return false;
    }
    void *photoClass = _monoObjectGetClass(photoMode);
    void *updateAll = FindMethodInHierarchy(photoClass, OBFUSCATE("UpdateAllComponent"), 0);
    if (updateAll) _monoRuntimeInvoke(updateAll, photoMode, nullptr, nullptr);
    void *birdList = NativeGetObj(photoMode, {}, {OBFUSCATE("_birdScannables")});
    if (!birdList) return false;
    void *listClass = _monoObjectGetClass(birdList);
    void *getCount = FindMethodInHierarchy(listClass, OBFUSCATE("get_Count"), 0);
    void *getItem = FindMethodInHierarchy(listClass, OBFUSCATE("get_Item"), 1);
    void *countObj = getCount ? _monoRuntimeInvoke(getCount, birdList, nullptr, nullptr) : nullptr;
    int count = countObj ? *(int *) _monoObjectUnbox(countObj) : 0;

    void *utilClass = levelImage ? _monoClassFromName(levelImage,
            OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
            OBFUSCATE("EntityUtil")) : nullptr;
    void *getResId = utilClass ? FindMethodInHierarchy(utilClass,
            OBFUSCATE("GetEntityResId"), 1) : nullptr;
    Vector3 playerPos = NativeGetPosition(player);
    float nearest = FLT_MAX;
    for (int i = 0; i < count; ++i) {
        void *args[] = {&i};
        exc = nullptr;
        void *scannable = getItem ? _monoRuntimeInvoke(getItem, birdList, args, &exc) : nullptr;
        if (!scannable || exc) continue;
        void *entity = NativeGetObj(scannable,
                {OBFUSCATE("get_entity"), OBFUSCATE("get_Entity")},
                {OBFUSCATE("entity"), OBFUSCATE("_entity")});
        if (!entity) continue;
        uint32_t netId = NativeGetNetId(entity);
        if (!netId || g_processedBirds.count(netId)) continue;
        float distance = GetVector3Distance(playerPos, NativeGetPosition(entity));
        if (distance < g_birdMinPhotoDist || distance > g_birdScanRange || distance >= nearest)
            continue;
        if (!IsBirdPhotoStateValid(ReadBirdState(entity))) continue;

        int staticId = 0;
        if (getResId) {
            void *idArgs[] = {&netId};
            exc = nullptr;
            void *result = _monoRuntimeInvoke(getResId, nullptr, idArgs, &exc);
            if (result && !exc) staticId = *(int *) _monoObjectUnbox(result);
        }
        if (staticId <= 0) continue;

        uint64_t instanceId = 0;
        void *entityClass = _monoObjectGetClass(entity);
        void *getInstanceId = FindMethodInHierarchy(entityClass,
                OBFUSCATE("get_InstanceID"), 0);
        void *instanceObj = getInstanceId
                ? _monoRuntimeInvoke(getInstanceId, entity, nullptr, nullptr) : nullptr;
        if (instanceObj) instanceId = *(uint64_t *) _monoObjectUnbox(instanceObj);
        selected = {staticId, netId, ReadBirdStandNetId(entity), instanceId,
                    ReadBirdPose(scannable)};
        nearest = distance;
    }
    g_birdFarmStatus = selected.birdNetId ? "TARGET ACQUIRED" : "SCANNING";
    return selected.birdNetId != 0;
}

static bool SendBirdPhotoRequest(const BirdTarget &target) {
    if (target.staticId <= 0 || target.birdNetId == 0) return false;
    void *image = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    void *klass = image ? _monoClassFromName(image,
            OBFUSCATE("XDTDataAndProtocol.ProtocolService.GamePlay.Bird"),
            OBFUSCATE("BirdProtocolManager")) : nullptr;
    void *method = klass ? FindMethodInHierarchy(klass,
            OBFUSCATE("TakingBirdPhoto"), 4) : nullptr;
    if (!method) return false;

    NativeBirdPhotoDetailInfo detail{};
    detail.ActionStar = 5;
    detail.IsPerfectStar = 1;
    detail.IsCoverStar = 0;
    detail.BirdActionType = target.pose >= 0 && target.pose <= 5 ? target.pose : 0;
    detail.IsUsingZoom = 0;
    detail.BirdStandNetId = target.standNetId;
    int staticId = target.staticId;
    uint8_t success = 1;
    uint32_t netId = target.birdNetId;
    void *args[] = {&staticId, &success, &detail, &netId};
    void *exc = nullptr;
    g_birdRequestPending.store(g_birdCounterAvailable, std::memory_order_release);
    g_birdRequestSentAt = BirdNowMs();
    _monoRuntimeInvoke(method, nullptr, args, &exc);
    if (exc) {
        g_birdRequestPending.store(false, std::memory_order_release);
        return false;
    }
    ++g_birdLocalPhotoAttempts;
    g_birdPhotoAttempts[target.birdNetId] = g_birdLocalPhotoAttempts;
    LOGI("[BirdWatch] sent static=%d net=%u pose=%d stand=%u count=%d/%d attempts=%d counter=%d",
         staticId, netId, detail.BirdActionType, detail.BirdStandNetId,
         g_birdObservedPhotoCount, g_birdTargetPhotoMaxCount,
         g_birdLocalPhotoAttempts, g_birdCounterAvailable ? 1 : 0);
    return true;
}

void ProcessBirdWatchingNative() {
    uint64_t now = BirdNowMs();
    if (!g_enableBirdFarm) {
        if (g_birdRequestPending.load(std::memory_order_acquire) ||
            g_birdCurrentTarget.birdNetId != 0)
            ResetBirdTarget("STANDBY", 0);
        return;
    }
    SMPlatform::SetBreadcrumbLight("bird:on");
    if (g_birdRequestPending.load(std::memory_order_acquire)) {
        int currentCount = -1;
        int continuousCount = -1;
        if (ReadBirdPhotoedCount(g_birdCurrentTarget.birdNetId,
                                 currentCount, &continuousCount) &&
            currentCount > g_birdObservedPhotoCount) {
            g_birdRequestPending.store(false, std::memory_order_release);
            g_birdObservedPhotoCount = currentCount;
            g_birdPhotoAttempts[g_birdCurrentTarget.birdNetId] = currentCount;
            ++g_birdSuccessCount;
            g_birdRetryCount = 0;
            LOGI("[BirdWatch] photo count advanced net=%u count=%d/%d continuous=%d",
                 g_birdCurrentTarget.birdNetId, currentCount,
                 g_birdTargetPhotoMaxCount, continuousCount);

            if (g_birdTargetPhotoMaxCount > 0 &&
                currentCount >= g_birdTargetPhotoMaxCount) {
                g_processedBirds.insert(g_birdCurrentTarget.birdNetId);
                ResetBirdTarget("TARGET LIMIT REACHED",
                                (uint64_t) (g_birdCatchCooldown * 1000.0f));
            } else {
                static char statusBuf[64];
                snprintf(statusBuf, sizeof(statusBuf), "PHOTO (%d/%d)", currentCount, g_birdTargetPhotoMaxCount);
                g_birdFarmStatus = statusBuf;
                g_birdNextActionAt = now +
                        (uint64_t) (g_birdInvokeCooldown * 1000.0f);
            }
            return;
        }

        if (now - g_birdRequestSentAt >= 8000) {
            g_birdRequestPending.store(false, std::memory_order_release);
            ++g_birdRetryCount;
            if (g_birdRetryCount >= 3) {
                g_processedBirds.insert(g_birdCurrentTarget.birdNetId);
                ResetBirdTarget("PHOTO COUNT TIMEOUT", 1500);
            } else {
                g_birdFarmStatus = u8"重试拍照计数";
                g_birdNextActionAt = now + 1000;
            }
        }
        return;
    }

    if (now < g_birdNextActionAt) return;

    // 观鸟需要手持鸟镜（toolId 4）；没有就尝试切换一次。
    {
        static bool s_birdToolTried = false;
        if (!s_birdToolTried) {
            s_birdToolTried = true;
            SMPlatform::SetBreadcrumbLight("bird:tool");
            void *imgTool = _monoImageLoaded((const char *) OBFUSCATE("XDTGameSystem"));
            void *toolCls = imgTool ? _monoClassFromName(imgTool,
                (const char *) OBFUSCATE("XDTGameSystem.GameplaySystem.Tool"),
                (const char *) OBFUSCATE("ToolSystem")) : nullptr;
            void *getInst = toolCls ? FindMethodInHierarchy(toolCls, (const char *) OBFUSCATE("get_Instance"), 0) : nullptr;
            void *inst = getInst ? _monoRuntimeInvoke(getInst, nullptr, nullptr, nullptr) : nullptr;
            void *setHand = toolCls ? FindMethodInHierarchy(toolCls, (const char *) OBFUSCATE("SetHandhold"), 1) : nullptr;
            if (inst && setHand) {
                int toolId = 4;
                void *a[1] = {&toolId};
                void *e = nullptr;
                _monoRuntimeInvoke(setHand, inst, a, &e);
            }
        }
    }

    SMPlatform::SetBreadcrumbLight("bird:resolve");
    if (g_birdCurrentTarget.birdNetId == 0) {
        BirdTarget target{};
        if (!ResolveBirdTarget(target)) {
            g_birdNextActionAt = now + 1000;
            return;
        }

        int photoCount = -1;
        int continuousCount = -1;
        int maxCount = ReadBirdPhotoMaxCount(target.staticId);
        const bool counterReady = ReadBirdPhotoedCount(
                target.birdNetId, photoCount, &continuousCount);

        // TableBird adalah sumber batas utama. Jika tabel belum siap, gunakan
        // batas konservatif 3 agar scanner tidak berhenti permanen.
        if (maxCount <= 0) maxCount = 3;

        auto it = g_birdPhotoAttempts.find(target.birdNetId);
        int existingAttempts = (it != g_birdPhotoAttempts.end()) ? it->second : 0;

        if (counterReady && photoCount >= maxCount) {
            g_processedBirds.insert(target.birdNetId);
            g_birdPhotoAttempts[target.birdNetId] = photoCount;
            g_birdFarmStatus = u8"已达目标上限";
            g_birdNextActionAt = now +
                    (uint64_t) (g_birdCatchCooldown * 1000.0f);
            return;
        }

        if (!counterReady && existingAttempts >= maxCount) {
            g_processedBirds.insert(target.birdNetId);
            g_birdFarmStatus = u8"已达目标上限";
            g_birdNextActionAt = now +
                    (uint64_t) (g_birdCatchCooldown * 1000.0f);
            return;
        }

        g_birdCurrentTarget = target;
        g_birdCounterAvailable = counterReady;
        g_birdObservedPhotoCount = counterReady ? photoCount : existingAttempts;
        g_birdTargetPhotoMaxCount = maxCount;
        g_birdLocalPhotoAttempts = existingAttempts;
        g_birdRetryCount = 0;

        static char statusBuf[64];
        snprintf(statusBuf, sizeof(statusBuf), "PHOTO (%d/%d)", g_birdLocalPhotoAttempts, maxCount);
        g_birdFarmStatus = statusBuf;
        LOGI("[BirdWatch] target net=%u initial count=%d/%d attempts=%d counter=%d",
             target.birdNetId, photoCount, maxCount, existingAttempts,
             counterReady ? 1 : 0);
    }

    SMPlatform::SetBreadcrumbLight("bird:send");
    if (!SendBirdPhotoRequest(g_birdCurrentTarget)) {
        SMPlatform::SetBreadcrumbLight("bird:sendfail");
        ResetBirdTarget("SEND FAILED", 1000);
        return;
    }
    SMPlatform::SetBreadcrumbLight("bird:sent");

    if (!g_birdCounterAvailable) {
        if (g_birdLocalPhotoAttempts >= g_birdTargetPhotoMaxCount) {
            g_processedBirds.insert(g_birdCurrentTarget.birdNetId);
            ResetBirdTarget("TARGET LIMIT REACHED",
                            (uint64_t) (g_birdCatchCooldown * 1000.0f));
        } else {
            static char statusBuf[64];
            snprintf(statusBuf, sizeof(statusBuf), "PHOTO (%d/%d)", g_birdLocalPhotoAttempts, g_birdTargetPhotoMaxCount);
            g_birdFarmStatus = statusBuf;
            g_birdNextActionAt = now +
                    (uint64_t) (g_birdInvokeCooldown * 1000.0f);
        }
    }
}

static uint64_t CookerKey(const CookerTarget &c);
static uint64_t CookingNowMs();

// Memperbarui slot resep secara dinamis di Unity thread dan memverifikasi
// apakah semua slot bahan terisi (mendukung multi-stack dengan NetID berbeda).
// Mengembalikan true jika seluruh bahan resep lengkap dan siap dimasak, false jika bahan benar-benar habis.
static bool RefreshCookingRecipeSlots(int recipeId) {
    if (recipeId <= 0) return false;
    void *img = _monoImageLoaded(CookN("n_imgGameSys").c_str());
    if (!img) return false;
    void *k = _monoClassFromName(img, CookN("n_nsCooking").c_str(), CookN("n_clsCookingSys").c_str());
    if (!k) return false;
    void *gi = FindMethodInHierarchy(k, CookN("n_getInstance2").c_str(), 0);
    if (!gi) gi = FindMethodInHierarchy(k, CookN("n_getInstance").c_str(), 0);
    void *inst = gi ? _monoRuntimeInvoke(gi, nullptr, nullptr, nullptr) : nullptr;
    if (!inst) return false;

    void *init = FindMethodInHierarchy(k, CookN("n_initRecipe").c_str(), 1);
    if (!init) return false;

    uint32_t r = (uint32_t) recipeId;
    void *a[] = {&r};
    void *exc = nullptr;
    void *recipeDetail = _monoRuntimeInvoke(init, inst, a, &exc);
    if (exc || !recipeDetail) return false;

    void *detailKlass = _monoObjectGetClass(recipeDetail);
    void *fSlots = FindFieldInHierarchy(detailKlass, OBFUSCATE("materialSlots"));
    if (!fSlots) return false;

    void *slotsArr = nullptr;
    _monoFieldGetValue(recipeDetail, fSlots, &slotsArr);
    if (!slotsArr) return false;

    int slotCount = *(int *) ((uint8_t *) slotsArr + OFF_ARR_COUNT);
    if (slotCount <= 0) return false;

    uint8_t *slotData = (uint8_t *) slotsArr + OFF_ARR_DATA;
    void *slotKlass = nullptr;
    void *fFilled = nullptr;
    void *fNetId = nullptr;

    for (int i = 0; i < slotCount; ++i) {
        void *slotObj = *(void **) (slotData + (size_t) i * sizeof(void *));
        if (!slotObj) return false;

        if (!slotKlass) {
            slotKlass = _monoObjectGetClass(slotObj);
            fFilled = FindFieldInHierarchy(slotKlass, OBFUSCATE("filled"));
            fNetId = FindFieldInHierarchy(slotKlass, OBFUSCATE("filledMaterialNetId"));
            if (!fFilled || !fNetId) return false;
        }

        uint8_t filled = 0;
        _monoFieldGetValue(slotObj, fFilled, &filled);
        if (!filled) return false;

        uint32_t netId = 0;
        _monoFieldGetValue(slotObj, fNetId, &netId);
        if (netId == 0) return false;
    }

    return true;
}

// Membaca status seluruh kompor dalam satu pekerjaan Unity thread dengan caching refleksi.
// -1 berarti pembacaan gagal dan tidak boleh dianggap Idle.
// Jika dangerPending diberikan oleh Auto Cook, status Danger langsung ditangani
// di task Unity yang sama sehingga aksi tidak memakai snapshot yang sudah basi.
static std::vector<int> GetCookerStatusesBatch(
        const std::vector<CookerTarget> &cookers,
        std::unordered_map<uint64_t, uint64_t> *dangerPending = nullptr,
        bool *dangerObserved = nullptr) {
    std::vector<int> statuses(cookers.size(), -1);
    if (dangerObserved) *dangerObserved = false;
    if (cookers.empty()) return statuses;

    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
        void *imgGameplay = _monoImageLoaded(CookN("n_imgLevel").c_str());
        if (!imgGameplay) imgGameplay = _monoImageLoaded(CookN("n_imgLevelDll").c_str());
        void *imgData = _monoImageLoaded(CookN("n_imgData").c_str());
        if (!imgGameplay || !imgData) return;

        void *cookKlass = _monoClassFromName(imgGameplay, CookN("n_nsHomeland").c_str(), CookN("n_clsCookBuild").c_str());
        void *worldKlass = _monoClassFromName(imgData, CookN("n_nsWorld").c_str(), CookN("n_clsEntityWorld").c_str());
        if (!cookKlass || !worldKlass) return;

        static void *cachedClosedMethod = nullptr;
        static void *cachedFComponents = nullptr;
        static void *cachedFEntries = nullptr;
        if (!cachedClosedMethod) {
            void *cookType = _monoClassGetType(cookKlass);
            void *openMethod = FindMethodInHierarchy(worldKlass, CookN("n_getCompSys").c_str(), 0);
            if (cookType && openMethod) {
                void *typeArgs[] = {cookType};
                void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
                MonoGenericContext context{};
                context.method_inst = methodInst;
                cachedClosedMethod = _mono_class_inflate_generic_method(openMethod, &context);
            }
        }
        if (!cachedClosedMethod) return;

        void *exc = nullptr;
        void *sysObj = _monoRuntimeInvoke(cachedClosedMethod, nullptr, nullptr, &exc);
        if (exc || !sysObj) return;

        if (!cachedFComponents) {
            void *sysKlass = _monoObjectGetClass(sysObj);
            cachedFComponents = FindFieldInHierarchy(sysKlass, CookN("n_fComponents").c_str());
        }
        if (!cachedFComponents) return;
        void *dictObj = nullptr;
        _monoFieldGetValue(sysObj, cachedFComponents, &dictObj);
        if (!dictObj) return;

        if (!cachedFEntries) {
            void *dictKlass = _monoObjectGetClass(dictObj);
            cachedFEntries = FindFieldInHierarchy(dictKlass, CookN("n_entries").c_str());
            if (!cachedFEntries) cachedFEntries = FindFieldInHierarchy(dictKlass, CookN("n_entriesU").c_str());
        }
        if (!cachedFEntries) return;
        void *entriesArr = nullptr;
        _monoFieldGetValue(dictObj, cachedFEntries, &entriesArr);
        if (!entriesArr) return;

        int length = *(int *) ((uint8_t *) entriesArr + OFF_ARR_COUNT);
        uint8_t *data = (uint8_t *) entriesArr + OFF_ARR_DATA;
        std::unordered_map<uint32_t, void *> builds;
        builds.reserve((size_t) (length > 0 ? length : 0));
        for (int i = 0; i < length; ++i) {
            uint8_t *entry = data + ((size_t) i * SZ_DICT_ENTRY);
            if (*(int *) entry < 0) continue;
            uint32_t key = *(uint32_t *) (entry + OFF_ENTRY_KEY);
            void *value = *(void **) (entry + OFF_ENTRY_VALUE);
            if (value) builds[key] = value;
        }

        static void *cachedDangerMethod = nullptr;
        if (dangerPending && !cachedDangerMethod) {
            void *protoKlass = _monoClassFromName(imgData, CookN("n_nsProtoCook").c_str(),
                                                  CookN("n_clsProtoMgr").c_str());
            if (protoKlass) {
                cachedDangerMethod = FindMethodInHierarchy(protoKlass, CookN("n_interact").c_str(), 2);
            }
        }
        void *dangerMethod = cachedDangerMethod;
        const uint64_t tickNow = CookingNowMs();
        const uint64_t dangerRetryMs = 250; // Jauh lebih responsif untuk mencegah masakan hangus

        static void *cachedGetCooking = nullptr;
        static void *cachedGetData = nullptr;
        static int cachedStatusOffset = -1;

        for (size_t i = 0; i < cookers.size(); ++i) {
            auto found = builds.find((uint32_t) cookers[i].pureId);
            if (found == builds.end()) continue;
            void *build = found->second;
            if (!cachedGetCooking) {
                cachedGetCooking = FindMethodInHierarchy(_monoObjectGetClass(build), CookN("n_getCooing").c_str(), 1);
                if (!cachedGetCooking) cachedGetCooking = FindMethodInHierarchy(_monoObjectGetClass(build), CookN("n_getCooing2").c_str(), 1);
            }
            if (!cachedGetCooking) continue;

            uint64_t burner = cookers[i].burnerId;
            void *args[] = {&burner};
            exc = nullptr;
            void *comp = _monoRuntimeInvoke(cachedGetCooking, build, args, &exc);
            if (exc || !comp) continue;

            if (!cachedGetData) {
                cachedGetData = FindMethodInHierarchy(_monoObjectGetClass(comp), CookN("n_getCompData").c_str(), 0);
            }
            if (!cachedGetData) continue;

            exc = nullptr;
            void *boxed = _monoRuntimeInvoke(cachedGetData, comp, nullptr, &exc);
            if (exc || !boxed) continue;

            if (cachedStatusOffset < 0) {
                void *dataKlass = _monoObjectGetClass(boxed);
                void *iter = nullptr, *field = nullptr;
                while ((field = _monoClassGetFields(dataKlass, &iter)) != nullptr) {
                    const char *name = _monoFieldName(field);
                    if (name && (strcmp(name, "Status") == 0 || strcmp(name, "status") == 0)) break;
                }
                if (field) {
                    cachedStatusOffset = _monoFieldGetOffset(field) - MONO_FIELD_HEADER;
                }
            }
            if (cachedStatusOffset < 0) continue;

            uint8_t *raw = (uint8_t *) _monoObjectUnbox(boxed);
            if (raw) statuses[i] = *(int *) (raw + cachedStatusOffset);

            if (!dangerPending) continue;
            uint64_t key = CookerKey(cookers[i]);
            if (statuses[i] != ST_Danger) {
                // Pending baru selesai setelah status aktual keluar dari Danger.
                dangerPending->erase(key);
                continue;
            }

            if (dangerObserved) *dangerObserved = true;
            auto pending = dangerPending->find(key);
            if (pending != dangerPending->end()) {
                // State-Latch: Sudah di-interact pada siklus Danger ini.
                // Jangan kirim interact lagi sampai status berubah keluar dari ST_Danger!
                continue;
            }

            // Status baru saja divalidasi Danger di Unity thread ini. Interact
            // langsung agar tidak menunggu antrean frame berikutnya.
            if (dangerMethod) {
                uint32_t netId = cookers[i].netId;
                uint64_t burner = cookers[i].burnerId;
                void *dangerArgs[] = {&netId, &burner};
                void *dangerExc = nullptr;
                _monoRuntimeInvoke(dangerMethod, nullptr, dangerArgs, &dangerExc);
                if (!dangerExc) (*dangerPending)[key] = tickNow;
            }
        }
    });
    return statuses;
}

struct CookBatchAction {
    std::string method;
    uint32_t netId;
    uint64_t burnerId;
};

static void ExecChefActionsBatch(const std::vector<CookBatchAction> &actions) {
    if (actions.empty()) return;
    RunOnUnityThreadAsync([actions]() {
        _monoThreadAttach(_monoGetRootDomain());
        void *img = _monoImageLoaded(CookN("n_imgData").c_str());
        if (!img) return;
        void *klass = _monoClassFromName(img, CookN("n_nsProtoCook").c_str(), CookN("n_clsProtoMgr").c_str());
        if (!klass) return;
        std::unordered_map<std::string, void *> methods;
        for (const auto &action: actions) {
            void *method = nullptr;
            auto it = methods.find(action.method);
            if (it != methods.end()) method = it->second;
            else {
                method = FindMethodInHierarchy(klass, action.method.c_str(), 2);
                methods[action.method] = method;
            }
            if (!method) continue;
            uint32_t netId = action.netId;
            uint64_t burner = action.burnerId;
            void *args[] = {&netId, &burner};
            _monoRuntimeInvoke(method, nullptr, args, nullptr);
        }
    });
}

static uint64_t CookerKey(const CookerTarget &c) {
    return ((uint64_t) c.netId << 32) ^ c.burnerId;
}

static uint64_t CookingNowMs() {
    return (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AutoCookBatchRoutine(int recipeId) {
    std::unordered_map<uint64_t, uint64_t> cooldown;
    std::unordered_map<uint64_t, uint64_t> dangerPending;
    uint64_t lastRescan = CookingNowMs();
    size_t normalCursor = 0;

    struct ChefBatchState {
        std::atomic<bool> allowNewCooking{true};
        std::atomic<bool> preparePending{false};
    };
    auto sharedState = std::make_shared<ChefBatchState>();

    uint64_t pendingPrepareKey = 0, pendingPrepareSince = 0;
    int drainedStableTicks = 0;
    const std::string interact = CookN("n_interact");
    const std::string startCook = CookN("n_startCook");

    // Inisialisasi awal recipe detail dan periksa ketersediaan bahan di stack pertama
    RunOnUnityThreadSync([recipeId, sharedState]() {
        _monoThreadAttach(_monoGetRootDomain());
        if (!RefreshCookingRecipeSlots(recipeId)) {
            LOGI(OBFUSCATE("[AutoCook] Bahan awal tidak mencukupi untuk resep %d"), recipeId);
            sharedState->allowNewCooking.store(false);
        }
    });

    while (g_isChefActive) {
        std::vector<CookerTarget> all, cookers;
        RunOnUnityThreadSync([&]() { all = g_availableCookers; });
        cookers.reserve(all.size());
        for (const auto &c: all) if (c.staticId == g_selectedCookerStaticId) cookers.push_back(c);
        bool dangerObserved = false;
        std::vector<int> statuses = GetCookerStatusesBatch(cookers, &dangerPending,
                                                           &dangerObserved);
        uint64_t now = CookingNowMs();
        std::vector<CookBatchAction> normalCandidates;
        std::vector<CookerTarget> prepareCandidates;
        bool hasActiveCooking = false, pendingConfirmed = false;

        for (size_t i = 0; i < cookers.size(); ++i) {
            int st = i < statuses.size() ? statuses[i] : -1;
            uint64_t key = CookerKey(cookers[i]);
            if (st >= ST_Preparing && st <= ST_Failed) hasActiveCooking = true;
            if (sharedState->preparePending.load() && key == pendingPrepareKey && st >= ST_Preparing) {
                sharedState->preparePending.store(false);
                pendingConfirmed = true;
            }
            if (cooldown[key] > now) continue;
            if (st == ST_Danger) {
                // Sudah ditangani secara instan saat status aktual dibaca.
                continue;
            } else if (!dangerObserved && (st == ST_Succeed || st == ST_Failed)) {
                normalCandidates.push_back({interact, cookers[i].netId, cookers[i].burnerId});
            } else if (!dangerObserved && st == ST_Preparing) {
                normalCandidates.push_back({startCook, cookers[i].netId, cookers[i].burnerId});
            } else if (!dangerObserved && st == ST_Idle && sharedState->allowNewCooking.load() && !sharedState->preparePending.load()) {
                prepareCandidates.push_back(cookers[i]);
            }
        }

        if (sharedState->preparePending.load() && now - pendingPrepareSince >= 2000) {
            sharedState->preparePending.store(false);
            RunOnUnityThreadAsync([recipeId, sharedState]() {
                _monoThreadAttach(_monoGetRootDomain());
                if (!RefreshCookingRecipeSlots(recipeId)) {
                    LOGI(OBFUSCATE("[AutoCook] Prepare timeout dan seluruh stack bahan habis. Mode drain aktif."));
                    sharedState->allowNewCooking.store(false);
                } else {
                    LOGI(OBFUSCATE("[AutoCook] Prepare timeout tapi bahan masih ada di stack berikutnya; slot dialihkan."));
                }
            });
        } else if (pendingConfirmed) {
            LOGI(OBFUSCATE("[AutoCook] Prepare dikonfirmasi server."));
        }

        // Saat ada Danger, Collect/Start/Prepare ditahan agar seluruh tick fokus
        // menyelamatkan kompor yang memiliki deadline dua detik.
        std::vector<CookBatchAction> normal;
        if (!dangerObserved && !normalCandidates.empty()) {
            normalCursor %= normalCandidates.size();
            bool containsStart = false;
            for (const auto &candidate: normalCandidates) {
                if (candidate.method == startCook) { containsStart = true; break; }
            }
            size_t maxNormal = containsStart ? (size_t) 4 : (size_t) 16;
            size_t count = std::min(maxNormal, normalCandidates.size());
            normal.reserve(count);
            for (size_t n = 0; n < count; ++n) {
                const auto &a = normalCandidates[(normalCursor + n) % normalCandidates.size()];
                normal.push_back(a);
                cooldown[((uint64_t) a.netId << 32) ^ a.burnerId] = now + (a.method == startCook ? 800 : 400);
            }
            normalCursor = (normalCursor + count) % normalCandidates.size();
        }
        ExecChefActionsBatch(normal);

        if (!dangerObserved && sharedState->allowNewCooking.load() && !sharedState->preparePending.load() && !prepareCandidates.empty()) {
            CookerTarget target = prepareCandidates.front();
            pendingPrepareKey = CookerKey(target);
            pendingPrepareSince = now;
            sharedState->preparePending.store(true);
            cooldown[pendingPrepareKey] = now + 1200;

            RunOnUnityThreadAsync([target, recipeId, sharedState]() {
                _monoThreadAttach(_monoGetRootDomain());

                // Selalu refresh slot sebelum prepare agar NetID selalu mengarah ke stack yang aktif dan valid
                if (!RefreshCookingRecipeSlots(recipeId)) {
                    LOGI(OBFUSCATE("[AutoCook] Seluruh stack bahan habis. Masuk ke mode drain."));
                    sharedState->allowNewCooking.store(false);
                    sharedState->preparePending.store(false);
                    return;
                }

                void *img = _monoImageLoaded(CookN("n_imgGameSys").c_str());
                if (!img) { sharedState->preparePending.store(false); return; }
                void *k = _monoClassFromName(img, CookN("n_nsCooking").c_str(), CookN("n_clsCookingSys").c_str());
                if (!k) { sharedState->preparePending.store(false); return; }
                void *gi = FindMethodInHierarchy(k, CookN("n_getInstance2").c_str(), 0);
                if (!gi) gi = FindMethodInHierarchy(k, CookN("n_getInstance").c_str(), 0);
                void *inst = gi ? _monoRuntimeInvoke(gi, nullptr, nullptr, nullptr) : nullptr;
                void *prep = FindMethodInHierarchy(k, CookN("n_prepCook").c_str(), 3);
                if (!inst || !prep) { sharedState->preparePending.store(false); return; }
                uint32_t netId = target.netId;
                uint64_t burner = target.burnerId;
                uint32_t magic = 0;
                void *args[] = {&netId, &burner, &magic};
                _monoRuntimeInvoke(prep, inst, args, nullptr);
            });
        }

        if (!sharedState->allowNewCooking.load() && !sharedState->preparePending.load()) {
            if (!hasActiveCooking) {
                if (++drainedStableTicks >= 5) {
                    LOGI(OBFUSCATE("[AutoCook] Semua kompor selesai; berhenti karena semua stack bahan habis."));
                    g_isChefActive = false;
                    break;
                }
            } else {
                drainedStableTicks = 0;
            }
        } else {
            drainedStableTicks = 0;
        }

        if (now - lastRescan >= 5000) { lastRescan = now; ScanCookersNative(); }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

void AutoCookRoutine(CookerTarget cooker, int recipeId) {
    auto waitStatus = [&](std::function<bool(int)> ok, int timeoutMs) -> int {
        int waited = 0, s = GetCookerStatusNative(cooker);
        while (g_isChefActive && !ok(s) && waited < timeoutMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            waited += 200;
            s = GetCookerStatusNative(cooker);
        }
        return s;
    };

    while (g_isChefActive) {
        int st = GetCookerStatusNative(cooker);

        // Selesai (sukses / gagal) -> panen, tunggu kompor benar-benar Idle
        if (st == ST_Succeed || st == ST_Failed) {
            ExecChefAction(CookN("n_interact").c_str(), cooker.netId, cooker.burnerId);
            waitStatus([](int s) { return s == ST_Idle; }, 5000);
            continue;
        }

        // Danger -> selamatkan (klik), jangan spam: kasih jeda
        if (st == ST_Danger) {
            ExecChefAction(CookN("n_interact").c_str(), cooker.netId, cooker.burnerId);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            continue;
        }

        // Cooking / Relief -> monitor saja, JANGAN kirim apa pun
        if (st == ST_Cooking || st == ST_Relief) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // Preparing -> baru boleh StartCooking (sekali), lalu tunggu masuk Cooking
        if (st == ST_Preparing) {
            ExecChefAction(CookN("n_startCook").c_str(), cooker.netId, cooker.burnerId);
            waitStatus([](int s) { return s >= ST_Cooking; }, 4000);
            continue;
        }

        // Idle -> Init resep + Prepare, LALU tunggu server ubah state
        if (st == ST_Idle) {
            bool hasIngredients = false;
            RunOnUnityThreadSync([&]() {
                _monoThreadAttach(_monoGetRootDomain());
                hasIngredients = RefreshCookingRecipeSlots(recipeId);
            });
            if (!hasIngredients) {
                LOGI(OBFUSCATE("[AutoCook] Bahan habis di seluruh stack."));
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            RunOnUnityThreadSync([=]() {   // Prepare (sync juga, biar urut)
                _monoThreadAttach(_monoGetRootDomain());
                void *img = _monoImageLoaded(CookN("n_imgGameSys").c_str());
                if (!img) return;
                void *k = _monoClassFromName(img, CookN("n_nsCooking").c_str(), CookN("n_clsCookingSys").c_str());
                if (!k) return;
                void *gi = FindMethodInHierarchy(k, CookN("n_getInstance2").c_str(), 0);
                if (!gi) gi = FindMethodInHierarchy(k, CookN("n_getInstance").c_str(), 0);
                void *inst = _monoRuntimeInvoke(gi, nullptr, nullptr, nullptr);
                void *mPrep = FindMethodInHierarchy(k, CookN("n_prepCook").c_str(), 3);
                if (mPrep && inst) {
                    uint32_t netId = cooker.netId;
                    uint64_t burner = cooker.burnerId;
                    uint32_t magic = 0;
                    void *a[] = {&netId, &burner, &magic};
                    _monoRuntimeInvoke(mPrep, inst, a, nullptr);
                }
            });

            waitStatus([](int s) { return s >= ST_Preparing; }, 4000);
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

void FailedCookRoutine() {
    while (g_isFailedCookActive) {
        std::vector<CookerTarget> cookers = g_availableCookers;

        for (const auto &cooker : cookers) {
            if (!g_isFailedCookActive) break;

            int st = GetCookerStatusNative(cooker);

            switch (st) {
                case ST_Failed:
                case ST_Succeed:
                    //ExecChefAction("InteractWithCooker", cooker.netId, cooker.burnerId);
                    //break;
                case ST_Cooking:
                case ST_Relief:
                    ExecChefAction(CookN("n_interact").c_str(), cooker.netId, cooker.burnerId);
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    break;
                case ST_Danger:
                default:
                    break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void HelperCookRoutine() {
    std::unordered_map<uint64_t, uint64_t> cooldown;
    std::unordered_set<uint64_t> dangerHandled;
    uint64_t lastRescan = CookingNowMs();
    size_t completedCursor = 0;
    const std::string interact = CookN("n_interact");

    while (g_isHelperCookActive) {
        std::vector<CookerTarget> cookers = g_availableCookers;
        std::vector<int> statuses = GetCookerStatusesBatch(cookers);
        uint64_t now = CookingNowMs();
        std::vector<CookBatchAction> danger, completed;

        for (size_t i = 0; i < cookers.size(); ++i) {
            int st = i < statuses.size() ? statuses[i] : -1;
            uint64_t key = CookerKey(cookers[i]);
            if (st == ST_Danger) {
                // State-Latch: hanya kirim 1x per siklus Danger.
                if (dangerHandled.find(key) == dangerHandled.end()) {
                    danger.push_back({interact, cookers[i].netId, cookers[i].burnerId});
                    dangerHandled.insert(key);
                }
            } else {
                dangerHandled.erase(key);
                if (cooldown[key] > now) continue;
                if (st == ST_Succeed || st == ST_Failed) {
                    completed.push_back({interact, cookers[i].netId, cookers[i].burnerId});
                }
            }
        }

        // Danger selalu diproses seluruhnya sebelum pengambilan hasil.
        ExecChefActionsBatch(danger);
        std::vector<CookBatchAction> completedBatch;
        if (!completed.empty()) {
            completedCursor %= completed.size();
            size_t count = std::min((size_t) 16, completed.size());
            completedBatch.reserve(count);
            for (size_t n = 0; n < count; ++n) {
                const auto &a = completed[(completedCursor + n) % completed.size()];
                completedBatch.push_back(a);
                cooldown[((uint64_t) a.netId << 32) ^ a.burnerId] = now + 600;
            }
            completedCursor = (completedCursor + count) % completed.size();
        }
        ExecChefActionsBatch(completedBatch);

        if (now - lastRescan >= 5000) { lastRescan = now; ScanCookersNative(); }
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
    }
}

void AntiAfkRoutine() {
    LOGI(OBFUSCATE("[ANTI-AFK] Tameng pelindung AFK diaktifkan!"));
    while (g_isAntiAfkActive) {
        RunOnUnityThreadAsync([]() {
            _monoThreadAttach(_monoGetRootDomain());

            auto FindMethod = [](void *klass, const char *name, int paramCount) -> void * {
                void *current = klass;
                while (current != nullptr) {
                    void *method = _monoClassGetMethodFromName(current, name, paramCount);
                    if (method != nullptr) return method;
                    current = _monoClassGetParent(current);
                }
                return nullptr;
            };

            // 1. Send Player Operate HeartBeat to Server to reset server AFK timer & prevent "..." icon
            void *imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
            if (!imgData) imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
            if (imgData) {
                void *klassProto = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Player"), OBFUSCATE("PlayerProtocolManager"));
                if (klassProto) {
                    void *m_heartbeat = FindMethod(klassProto, OBFUSCATE("PlayerOperateHeartBeat"), 0);
                    if (m_heartbeat) {
                        _monoRuntimeInvoke(m_heartbeat, nullptr, nullptr, nullptr);
                    }
                }
            }

            // 2. Disable client-side AFK mode (GameSettingSystem)
            void *imgGameSys = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
            if (imgGameSys) {
                void *klassSettingSys = _monoClassFromName(imgGameSys, OBFUSCATE(
                        "XDTGameSystem.GameplaySystem.GameSetting"), OBFUSCATE("GameSettingSystem"));
                if (klassSettingSys) {
                    void *m_getInstance = FindMethod(klassSettingSys, OBFUSCATE("get_Instance"), 0);
                    if (!m_getInstance)
                        m_getInstance = FindMethod(klassSettingSys, OBFUSCATE("get_instance"), 0);
                    if (m_getInstance) {
                        void *settingInst = _monoRuntimeInvoke(m_getInstance, nullptr, nullptr, nullptr);
                        if (settingInst) {
                            void *m_setAFK = FindMethod(klassSettingSys, OBFUSCATE("set_EnableAFKMode"), 1);
                            if (m_setAFK) {
                                bool isAfkEnabled = false;
                                void *args[1] = {&isAfkEnabled};
                                _monoRuntimeInvoke(m_setAFK, settingInst, args, nullptr);
                            }
                        }
                    }
                }
            }
        });

        // Jeda waktu sebelum injeksi ulang (tiap 5 detik)
        for (int i = 0; i < 5; i++) {
            if (!g_isAntiAfkActive)
                return;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    LOGI(OBFUSCATE("[ANTI-AFK] Tameng AFK dimatikan."));
}

void ExecuteGodHandCatchNative() {
    _monoThreadAttach(_monoGetRootDomain());
    auto FindMethod = [&](void *k, const char *n, int p) -> void * {
        while (k) {
            void *m = _monoClassGetMethodFromName(k, n, p);
            if (m)
                return m;
            k = _monoClassGetParent(k);
        }
        return nullptr;
    };
    auto FindField = [&](void *k, const char *n) -> void * {
        while (k) {
            void *f = _monoClassGetFieldFromName(k, n);
            if (f)
                return f;
            k = _monoClassGetParent(k);
        }
        return nullptr;
    };

    void *imgSys = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!imgSys)
        return;
    void *toolSysKlass = _monoClassFromName(imgSys, OBFUSCATE("XDTGameSystem.GameplaySystem.Tool"),
                                            OBFUSCATE("ToolSystem"));
    if (!toolSysKlass)
        return;

    void *m_getInst = FindMethod(toolSysKlass, OBFUSCATE("get_Instance"), 0);
    void *m_getCurrentTool = FindMethod(toolSysKlass, OBFUSCATE("GetCurrentTool"), 0);
    void *m_setHandhold = FindMethod(toolSysKlass, OBFUSCATE("SetHandhold"), 1);

    if (!m_getInst || !m_getCurrentTool || !m_setHandhold)
        return;
    void *toolSysInst = _monoRuntimeInvoke(m_getInst, nullptr, nullptr, nullptr);
    if (!toolSysInst)
        return;

    void *currentToolObj = _monoRuntimeInvoke(m_getCurrentTool, toolSysInst, nullptr, nullptr);
    int currentToolId = 0;
    if (currentToolObj) {
        void *toolKlass = _monoObjectGetClass(currentToolObj);
        void *f_id = FindField(toolKlass, OBFUSCATE("Id"));
        if (f_id)
            _monoFieldGetValue(currentToolObj, f_id, &currentToolId);
    }

    if (currentToolId != 5) {
        int targetTool = 5;
        void *args[1] = {&targetTool};
        _monoRuntimeInvoke(m_setHandhold, toolSysInst, args, nullptr);
        return;
    }

    void *imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel)
        return;

    void *insectKlass = _monoClassFromName(imgLevel,
                                           OBFUSCATE("XDTLevelAndEntity.GameplaySystem.Insect"),
                                           OBFUSCATE("LevelInscetManager"));
    if (!insectKlass)
        return;

    void *monoType = _monoClassGetType(insectKlass);
    void *typeObject = _monoTypeGetObject(_monoGetRootDomain(), monoType);

    void *imgFramework = _monoImageLoaded(OBFUSCATE("XDTBaseService"));
    if (!imgFramework)
        imgFramework = _monoImageLoaded(OBFUSCATE("Assembly-CSharp"));
    if (!imgFramework)
        return;
    void *managersKlass = _monoClassFromName(imgFramework, OBFUSCATE("XDTGame.Framework"),
                                             OBFUSCATE("Managers"));
    if (!managersKlass)
        return;

    void *m_GetModule = FindMethod(managersKlass, OBFUSCATE("GetModule"), 1);
    if (!m_GetModule)
        return;
    void *modArgs[1] = {typeObject};
    void *exc = nullptr;
    void *managerInst = _monoRuntimeInvoke(m_GetModule, nullptr, modArgs, &exc);
    if (!managerInst || exc)
        return;

    void *f_insects = FindField(insectKlass, OBFUSCATE("insects"));
    if (!f_insects)
        return;

    void *listObj = nullptr;
    _monoFieldGetValue(managerInst, f_insects, &listObj);
    if (!listObj)
        return;

    void *listKlass = _monoObjectGetClass(listObj);
    void *m_get_Count = FindMethod(listKlass, OBFUSCATE("get_Count"), 0);
    void *m_get_Item = FindMethod(listKlass, OBFUSCATE("get_Item"), 1);
    if (!m_get_Count || !m_get_Item)
        return;

    void *countObj = _monoRuntimeInvoke(m_get_Count, listObj, nullptr, nullptr);
    int count = countObj ? *(int *) _monoObjectUnbox(countObj) : 0;
    if (count == 0)
        return;

    void *protocolImg = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!protocolImg)
        return;

    void *protocolKlass = _monoClassFromName(protocolImg,
                                             OBFUSCATE("XDTDataAndProtocol.ProtocolService.Insect"),
                                             OBFUSCATE("InsectProtocolManager"));
    if (!protocolKlass)
        return;

    void *catchMethod = FindMethod(protocolKlass, OBFUSCATE("CatchingInsect"), 6);
    if (!catchMethod)
        catchMethod = FindMethod(protocolKlass, OBFUSCATE("CatchInsect"), 6);
    if (!catchMethod)
        catchMethod = FindMethod(protocolKlass, OBFUSCATE("CmdCatchInsect"), 6);
    if (!catchMethod)
        return;

    void *sig = _monoMethodSignature(catchMethod);
    void *iter = nullptr;
    void *type0 = _monoSignatureGetParams(sig, &iter);
    void *klassListIds = _monoClassFromMonoType(type0);
    void *ctorIds = FindMethod(klassListIds, OBFUSCATE(".ctor"), 0);
    void *addIds = FindMethod(klassListIds, OBFUSCATE("Add"), 1);

    void *type1 = _monoSignatureGetParams(sig, &iter);
    void *klassListPos = _monoClassFromMonoType(type1);
    void *ctorPos = FindMethod(klassListPos, OBFUSCATE(".ctor"), 0);
    void *addPos = FindMethod(klassListPos, OBFUSCATE("Add"), 1);

    void *listIds = _monoObjectNew(_monoGetRootDomain(), klassListIds);
    _monoRuntimeInvoke(ctorIds, listIds, nullptr, nullptr);

    void *listPos = _monoObjectNew(_monoGetRootDomain(), klassListPos);
    _monoRuntimeInvoke(ctorPos, listPos, nullptr, nullptr);

    int validTargets = 0;
    int maxCatch = (count > g_insectCatchLimit) ? g_insectCatchLimit : count;
    for (int i = 0; i < maxCatch; i++) {
        void *params[1] = {&i};
        void *entityInstance = _monoRuntimeInvoke(m_get_Item, listObj, params, nullptr);

        if (entityInstance) {
            uint32_t netId = NativeGetNetId(entityInstance);
            Vector3 targetPos = NativeGetPosition(entityInstance);

            if (netId != 0) {
                void *argId[1] = {&netId};
                _monoRuntimeInvoke(addIds, listIds, argId, nullptr);

                void *argPos[1] = {&targetPos};
                _monoRuntimeInvoke(addPos, listPos, argPos, nullptr);

                validTargets++;
            }
        }
    }

    if (validTargets > 0) {
        currentToolObj = _monoRuntimeInvoke(m_getCurrentTool, toolSysInst, nullptr, nullptr);
        currentToolId = 0;
        if (currentToolObj) {
            void *toolKlass = _monoObjectGetClass(currentToolObj);
            void *f_id = FindField(toolKlass, OBFUSCATE("Id"));
            if (f_id)
                _monoFieldGetValue(currentToolObj, f_id, &currentToolId);
        }

        if (currentToolId != 5) {
            g_testAutoCatch = false;
            return;
        }

        bool usePressCoolDown = false;
        bool withNoAlert = false;
        bool isSelected = false;
        int bubbleNetId = 0;

        void *args[6] = {listIds, listPos, &usePressCoolDown, &withNoAlert, &isSelected,
                         &bubbleNetId};
        void *pExc = nullptr;

        _monoRuntimeInvoke(catchMethod, nullptr, args, &pExc);
    }
}

void AutoCatchLoopThread() {
    while (g_testAutoCatch) {
        RunOnUnityThreadAsync([]() { ExecuteGodHandCatchNative(); });
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

void AutoFarmSmartThread() {
    // Hardcode Payload agar Free User tetap bisa Auto Farm tanpa perlu konek ke server
    InvokeRequest teleReq = {OBFUSCATE("tele_to_pos"), OBFUSCATE("XDTLevelAndEntity.dll"),
                             OBFUSCATE("XDTLevelAndEntity.Utils"), OBFUSCATE("EntityHelper"),
                             OBFUSCATE(":AutoMoveTransfer(Vector3)"), "", true, ""};
    teleReq.generasiSesi = GenerasiSesi();
    InvokeRequest hitReq = {OBFUSCATE("main_interact"), OBFUSCATE("XDTLevelAndEntity.dll"),
                            OBFUSCATE("XDTLevelAndEntity.Gameplay"), OBFUSCATE("PlayerInteraction"),
                            OBFUSCATE(":ExecuteHandholdButton(bool,int)"), OBFUSCATE("true,0"),
                            true, ""};
    hitReq.generasiSesi = GenerasiSesi();

    std::vector<std::string> masterPlaylist;

    for (int i = 0; i < NUM_RESOURCES; i++) {
        if (g_farmSelections[i]) {
            masterPlaylist.insert(masterPlaylist.end(), allResourceVectors[i]->begin(),
                                  allResourceVectors[i]->end());
        }
    }

    if (masterPlaylist.empty()) {
        g_autoFarmActive = false;
        return;
    }

    int treeIndex = 0;

    while (g_autoFarmActive) {
        {
            std::lock_guard<std::mutex> lock(invokeMutex);
            teleReq.paramInput = masterPlaylist[treeIndex];
            invokeQueue.push_back(teleReq);
        }
        sleep(2);

        for (int i = 0; i < 6; i++) {
            if (!g_autoFarmActive)
                break;

            {
                std::lock_guard<std::mutex> lock(invokeMutex);
                hitReq.paramInput = (const char *) OBFUSCATE("true,0");
                invokeQueue.push_back(hitReq);
            }
            usleep(400000);
        }
        if (g_autoFarmActive) {
            sleep(2);
        }
        treeIndex++;
        if (treeIndex >= masterPlaylist.size()) {
            treeIndex = 0;
        }
    }
}

static bool TryArmFishingInstantCatchNative();

void ApplyFishingHackNative() {
    if (!SesiAktif()) {
        isInstantFishing = false;
        return;
    }
    std::string wire = WireForOp(OP_SETUP_FISHING);
    if (wire.empty()) {
        isInstantFishing = false;
        return;
    }
    RunOnUnityThreadSync([]() {
        if (isInstantFishing) {
            TryArmFishingInstantCatchNative();
        }

        void *configInst = GetLevelScriptableConfigInstanceNative();
        if (!configInst) return;

        void *klass = _monoObjectGetClass(configInst);
        if (!klass) return;

        void *throwField = FindFieldInHierarchy(klass, OBFUSCATE("fishThrowMaxDistance"));
        void *catchField = FindFieldInHierarchy(klass, OBFUSCATE("fishCatchDistance"));

        if (throwField) {
            float throwVal = g_fishThrowDist;
            int offset = _monoFieldGetOffset(throwField);
            float *pField = (float *) ((uintptr_t) configInst + offset);
            *pField = throwVal; // C++ Direct Memory Rewrite
        }

        if (catchField) {
            float catchVal = isInstantFishing ? 100.0f : 0.5f;
            int offset = _monoFieldGetOffset(catchField);
            float *pField = (float *) ((uintptr_t) configInst + offset);
            *pField = catchVal; // C++ Direct Memory Rewrite
        }
    });
}

void ExecuteGlobalSwimToggleNative(bool enableSwim) {
    // Gunakan Async agar UI ImGui tidak membeku saat mengirim paket ke server
    RunOnUnityThreadAsync([enableSwim]() mutable {
        void *domain = _monoGetRootDomain();
        if (!domain) return;
        _monoThreadAttach(domain);

        //LOGI("[SWIM_HACK] === ðŸŠâ€â™‚ï¸ GLOBAL SWIM HACK (%s) ðŸŠâ€â™‚ï¸ ===", enableSwim ? "ON" : "OFF");

        // =======================================================
        // 1. PANGGIL EntityUtil.GetSelfPlayer()
        // =======================================================
        void *imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
        if (!imgLevel) return;

        void *entityUtilKlass = _monoClassFromName(imgLevel, OBFUSCATE(
                "XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
        void *getSelfPlayerMethod = FindMethodInHierarchy(entityUtilKlass,
                                                          OBFUSCATE("GetSelfPlayer"), 0);
        if (!getSelfPlayerMethod) return;

        void *exc = nullptr;
        void *targetLocalPlayer = _monoRuntimeInvoke(getSelfPlayerMethod, nullptr, nullptr, &exc);

        if (!targetLocalPlayer || exc) {
            //LOGE("[SWIM_HACK] Gagal: Karakter belum spawn di map!");
            return;
        }

        // =======================================================
        // 2. DAPATKAN FIELD 'Status' (PlayerSyncStatus)
        // =======================================================
        void *baseActorKlass = _monoClassFromName(imgLevel, OBFUSCATE(
                "XDTLevelAndEntity.Gameplay.Component.Player"), OBFUSCATE("BaseActorComponent"));
        void *statusField = _monoClassGetFieldFromName(baseActorKlass, OBFUSCATE("Status"));
        if (!statusField) return;

        void *statusObj = nullptr;
        // Di C++, _monoFieldGetValue akan mengisi pointer statusObj
        _monoFieldGetValue(targetLocalPlayer, statusField, &statusObj);
        if (!statusObj) {
            //LOGE("[SWIM_HACK] Gagal: PlayerSyncStatus null!");
            return;
        }

        // =======================================================
        // 3. BEDAH JANTUNG STRUCT 'SwimStatus' (C++ IN-PLACE SURGERY)
        // =======================================================
        void *syncStatusKlass = _monoObjectGetClass(statusObj);
        void *swimStatusField = _monoClassGetFieldFromName(syncStatusKlass,
                                                           OBFUSCATE("SwimStatus"));
        if (!swimStatusField) return;

        void *swimKlass = _monoClassFromName(imgLevel, OBFUSCATE(
                "XDTLevelAndEntity.Gameplay.Component.Player"), OBFUSCATE("SwimStatus"));
        void *isSwimField = _monoClassGetFieldFromName(swimKlass, OBFUSCATE("isSwimming"));

        // Cari DirtyMask (Menangani kemungkinan nama dari obfuscation)
        void *dirtyMaskField = _monoClassGetFieldFromName(swimKlass, OBFUSCATE("dirtyMask"));
        if (!dirtyMaskField)
            dirtyMaskField = _monoClassGetFieldFromName(swimKlass, OBFUSCATE("_dirtyMask"));
        if (!dirtyMaskField)
            dirtyMaskField = _monoClassGetFieldFromName(swimKlass, OBFUSCATE("DirtyMask"));

        // [OPTIMASI C++] Kita tidak butuh byte array seperti di C#!
        // Kita hitung offset utama struct SwimStatus di dalam class PlayerSyncStatus
        int structBaseOffset = _monoFieldGetOffset(swimStatusField);

        if (isSwimField) {
            int fieldOffset = _monoFieldGetOffset(isSwimField) - MONO_FIELD_HEADER;
            // Suntik nilai boolean langsung menembus memori!
            *(bool *) ((uint8_t *) statusObj + structBaseOffset + fieldOffset) = enableSwim;
        }

        if (dirtyMaskField) {
            int fieldOffset = _monoFieldGetOffset(dirtyMaskField) - MONO_FIELD_HEADER;
            // Paksa kotor (Set bit 1) langsung di memori RAM!
            *(int *) ((uint8_t *) statusObj + structBaseOffset + fieldOffset) |= 1;
        }
        //LOGI("[SWIM_HACK] Bedah Memori SwimStatus Selesai!");

        // =======================================================
        // 4. MENCURI NET-ID (JALUR VIP)
        // =======================================================
        void *imgProtocol = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        void *pdcKlass = _monoClassFromName(imgProtocol, OBFUSCATE("XDTDataAndProtocol"),
                                            OBFUSCATE("PlayerDataCenter"));
        void *getNetIdMethod = FindMethodInHierarchy(pdcKlass, OBFUSCATE("GetSelfNetPlayerId"), 0);

        void *netIdObj = _monoRuntimeInvoke(getNetIdMethod, nullptr, nullptr, &exc);
        uint32_t netId = 0;
        if (netIdObj && !exc) {
            netId = *(uint32_t *) _monoObjectUnbox(netIdObj);
        }
        //LOGI("[SWIM_HACK] NetID Didapat: %u", netId);

        // =======================================================
        // 5. TEMBAK PAKET ILUSI KE SERVER!
        // =======================================================
        void *sendSyncMethod = FindMethodInHierarchy(syncStatusKlass, OBFUSCATE("SendSyncStatus"),
                                                     2);
        if (sendSyncMethod) {
            void *args[2] = {&netId, statusObj};
            _monoRuntimeInvoke(sendSyncMethod, nullptr, args, &exc);
        }
    });
}

void ExecuteGlobalSkateToggleNative(bool enableSkate) {
    // Gunakan Async agar UI ImGui tidak membeku saat mengirim paket ke server
    RunOnUnityThreadAsync([enableSkate]() mutable {
        void *domain = _monoGetRootDomain();
        if (!domain) return;
        _monoThreadAttach(domain);

        //LOGI("[SKATE_HACK] === ðŸ›¹ GLOBAL SKATE HACK (%s) ðŸ›¹ ===", enableSkate ? "ON" : "OFF");

        // =======================================================
        // 1. PANGGIL EntityUtil.GetSelfPlayer()
        // =======================================================
        void *imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
        if (!imgLevel) return;

        void *entityUtilKlass = _monoClassFromName(imgLevel, OBFUSCATE(
                "XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
        void *getSelfPlayerMethod = FindMethodInHierarchy(entityUtilKlass,
                                                          OBFUSCATE("GetSelfPlayer"), 0);
        if (!getSelfPlayerMethod) return;

        void *exc = nullptr;
        void *targetLocalPlayer = _monoRuntimeInvoke(getSelfPlayerMethod, nullptr, nullptr, &exc);

        if (!targetLocalPlayer || exc) {
            //LOGE("[SKATE_HACK] Gagal: Karakter belum spawn di map!");
            return;
        }

        // =======================================================
        // 2. DAPATKAN FIELD 'Status' (PlayerSyncStatus)
        // =======================================================
        void *baseActorKlass = _monoClassFromName(imgLevel, OBFUSCATE(
                "XDTLevelAndEntity.Gameplay.Component.Player"), OBFUSCATE("BaseActorComponent"));
        void *statusField = _monoClassGetFieldFromName(baseActorKlass, OBFUSCATE("Status"));
        if (!statusField) return;

        void *statusObj = nullptr;
        _monoFieldGetValue(targetLocalPlayer, statusField, &statusObj);
        if (!statusObj) {
            //LOGE("[SKATE_HACK] Gagal: PlayerSyncStatus null!");
            return;
        }

        // =======================================================
        // 3. BEDAH JANTUNG STRUCT 'SkateStatus' (C++ IN-PLACE SURGERY)
        // =======================================================
        void *syncStatusKlass = _monoObjectGetClass(statusObj);
        void *skateStatusField = _monoClassGetFieldFromName(syncStatusKlass,
                                                            OBFUSCATE("SkateStatus"));
        if (!skateStatusField) {
            //LOGE("[SKATE_HACK] Gagal: Field SkateStatus tidak ditemukan di PlayerSyncStatus!");
            return;
        }

        void *skateKlass = _monoClassFromName(imgLevel, OBFUSCATE(
                "XDTLevelAndEntity.Gameplay.Component.Player"), OBFUSCATE("SkateStatus"));
        void *modeField = _monoClassGetFieldFromName(skateKlass, OBFUSCATE("mode"));

        // Cari DirtyMask (Menangani kemungkinan backing field otomatis dari properti)
        void *dirtyMaskField = _monoClassGetFieldFromName(skateKlass, OBFUSCATE("dirtyMask"));
        if (!dirtyMaskField)
            dirtyMaskField = _monoClassGetFieldFromName(skateKlass, OBFUSCATE("_dirtyMask"));
        if (!dirtyMaskField)
            dirtyMaskField = _monoClassGetFieldFromName(skateKlass, OBFUSCATE("DirtyMask"));
        if (!dirtyMaskField)
            dirtyMaskField = _monoClassGetFieldFromName(skateKlass, OBFUSCATE(
                    "<DirtyMask>k__BackingField"));

        // Hitung posisi utama struct SkateStatus di dalam kelas PlayerSyncStatus
        int structBaseOffset = _monoFieldGetOffset(skateStatusField);

        if (modeField) {
            int fieldOffset = _monoFieldGetOffset(modeField) - MONO_FIELD_HEADER;
            // None = 0, Free = 1 (Masuk ke mode Free skateboard)
            int skateModeValue = enableSkate ? 1 : 0;
            *(int *) ((uint8_t *) statusObj + structBaseOffset + fieldOffset) = skateModeValue;
        }

        if (dirtyMaskField) {
            int fieldOffset = _monoFieldGetOffset(dirtyMaskField) - MONO_FIELD_HEADER;
            // Set bit 1 (DirtyMask |= 1) agar server tahu status Mode telah berubah
            *(int *) ((uint8_t *) statusObj + structBaseOffset + fieldOffset) |= 1;
        }
        //LOGI("[SKATE_HACK] Bedah Memori SkateStatus Selesai!");

        // =======================================================
        // 4. MENCURI NET-ID (JALUR VIP)
        // =======================================================
        void *imgProtocol = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        void *pdcKlass = _monoClassFromName(imgProtocol, OBFUSCATE("XDTDataAndProtocol"),
                                            OBFUSCATE("PlayerDataCenter"));
        void *getNetIdMethod = FindMethodInHierarchy(pdcKlass, OBFUSCATE("GetSelfNetPlayerId"), 0);

        void *netIdObj = _monoRuntimeInvoke(getNetIdMethod, nullptr, nullptr, &exc);
        uint32_t netId = 0;
        if (netIdObj && !exc) {
            netId = *(uint32_t *) _monoObjectUnbox(netIdObj);
        }
        //LOGI("[SKATE_HACK] NetID Didapat: %u", netId);

        // =======================================================
        // 5. TEMBAK PAKET SYNCHRONIZATION KE SERVER!
        // =======================================================
        void *sendSyncMethod = FindMethodInHierarchy(syncStatusKlass, OBFUSCATE("SendSyncStatus"),
                                                     2);
        if (sendSyncMethod) {
            void *args[2] = {&netId, statusObj};
            _monoRuntimeInvoke(sendSyncMethod, nullptr, args, &exc);
        }
    });
}

void PatchSkateConfigNative(float targetTime, int targetEnergyCost) {
    RunOnUnityThreadAsync([targetTime, targetEnergyCost]() {
        _monoThreadAttach(_monoGetRootDomain());
        void *configMgr = GetConfigManagerInstance();
        if (!configMgr) {
            return;
        }
        void *mgrKlass = _monoObjectGetClass(configMgr);
        void *getSkateConfigMeth = FindMethodInHierarchy(mgrKlass, OBFUSCATE("get_SkateConfig"), 0);
        if (!getSkateConfigMeth) return;

        void *exc = nullptr;
        void *skateConfigInst = _monoRuntimeInvoke(getSkateConfigMeth, configMgr, nullptr, &exc);
        if (!skateConfigInst || exc) {
            return;
        }

        void *skateKlass = _monoObjectGetClass(skateConfigInst);
        void *exitTimeField = _monoClassGetFieldFromName(skateKlass, OBFUSCATE("ExitTime"));
        if (exitTimeField) {
            int offsetTime = _monoFieldGetOffset(exitTimeField);
            *(float *) ((uint8_t *) skateConfigInst + offsetTime) = targetTime;
        }
        void *ultimateCostField = _monoClassGetFieldFromName(skateKlass,
                                                             OBFUSCATE("UltimateCostEnergy"));
        if (ultimateCostField) {
            int offsetCost = _monoFieldGetOffset(ultimateCostField);
            *(int *) ((uint8_t *) skateConfigInst + offsetCost) = targetEnergyCost;
        }
    });
}

void PatchJumpHeightNative(float height) {
    RunOnUnityThreadAsync([height]() {
        _monoThreadAttach(_monoGetRootDomain());

        void *motionConfig = GetPlayerMotionConfigNative();
        if (!motionConfig) {
            LOGE(OBFUSCATE("[JUMP_HACK] Batal: MotionConfig belum tersedia di RAM."));
            return;
        }

        void *klass = _monoObjectGetClass(motionConfig);
        void *jumpField = _monoClassGetFieldFromName(klass, OBFUSCATE("JumpingHighest"));
        if (!jumpField) {
            LOGE(OBFUSCATE("[JUMP_HACK] Batal: Field JumpingHighest tidak ditemukan."));
            return;
        }

        int offset = _monoFieldGetOffset(jumpField);

        // âš¡ OPERASI BEDAH MEMORI (MENGGANTI ANGKA LANGSUNG DI RAM)
        *(float *) ((uint8_t *) motionConfig + offset) = height;

        LOGI(OBFUSCATE("[JUMP_HACK] ðŸ¦˜ Kapasitas Loncatan Diubah Menjadi: %.1f"), height);
    });
}

void OpenRemoteShopNative() {
    _monoThreadAttach(_monoGetRootDomain());

    void *imgLevel = _monoImageLoaded((const char *) OBFUSCATE("XDTGameUI"));
    if (!imgLevel)
        return;

    void *exc = nullptr;
    void *klassIntent = _monoClassFromName(imgLevel,
                                           (const char *) OBFUSCATE("XDTGame.Framework.UI"),
                                           (const char *) OBFUSCATE("Intent"));
    void *intentObj = nullptr;
    if (klassIntent) {
        void *m_getIntent = FindMethodInHierarchy(klassIntent, (const char *) OBFUSCATE("Get"), 0);
        if (m_getIntent) {
            intentObj = _monoRuntimeInvoke(m_getIntent, nullptr, nullptr, &exc);
        }
    }

    // --- CARA BARU (Game Update): XDTGame.UI.Logic.ItemSellPanelLogic.Open(Intent) ---
    void *klassLogic = _monoClassFromName(imgLevel,
                                          (const char *) OBFUSCATE("XDTGame.UI.Logic"),
                                          (const char *) OBFUSCATE("ItemSellPanelLogic"));
    if (klassLogic) {
        void *m_OpenLogic = FindMethodInHierarchy(klassLogic, (const char *) OBFUSCATE("Open"), 1);
        if (m_OpenLogic) {
            exc = nullptr;
            void *args[1] = {intentObj};
            _monoRuntimeInvoke(m_OpenLogic, nullptr, args, &exc);
            if (!exc) {
                LOGI((const char *) OBFUSCATE("[+] REMOTE SHOP (LOGIC BARU) BERHASIL DIBUKA!"));
                return;
            } else {
                LOGE((const char *) OBFUSCATE("[-] Exception saat ItemSellPanelLogic.Open dipanggil, coba fallback!"));
            }
        }
    }

    // --- FALLBACK: Cara Lama UIManager.OpenView ---
    void *klassUIManager = nullptr;
    const char *possibleDlls[] = {
            (const char *) OBFUSCATE("XDTGameUI"),
            (const char *) OBFUSCATE("Assembly-CSharp")};

    for (int i = 0; i < 2; i++) {
        void *img = _monoImageLoaded(possibleDlls[i]);
        if (img) {
            klassUIManager = _monoClassFromName(img, (const char *) OBFUSCATE("XDTGame.Core"),
                                                (const char *) OBFUSCATE("UIManager"));
            if (klassUIManager)
                break;
        }
    }

    if (!klassUIManager) {
        LOGE((const char *) OBFUSCATE("[-] Class UIManager tidak ditemukan!"));
        return;
    }

    void *m_getInstance = FindMethodInHierarchy(klassUIManager,
                                                (const char *) OBFUSCATE("get_Instance"), 0);
    if (!m_getInstance) {
        LOGE((const char *) OBFUSCATE("[-] Method get_Instance tidak ditemukan di UIManager!"));
        return;
    }

    exc = nullptr;
    void *uiManagerInst = _monoRuntimeInvoke(m_getInstance, nullptr, nullptr, &exc);

    if (!uiManagerInst || exc) {
        LOGE((const char *) OBFUSCATE(
                "[-] Gagal memanggil UIManager.Instance! (Kemungkinan belum inisialisasi)"));
        return;
    }

    void *klassSellPanel = _monoClassFromName(imgLevel,
                                              (const char *) OBFUSCATE("XDTGame.UI.Panel"),
                                              (const char *) OBFUSCATE("ItemSellPanel"));
    if (!klassSellPanel)
        return;

    void *monoType = _monoClassGetType(klassSellPanel);
    void *typeObject = _monoTypeGetObject(_monoGetRootDomain(), monoType);
    if (!typeObject) {
        LOGE((const char *) OBFUSCATE("[-] Gagal membuat System.Type Object!"));
        return;
    }

    void *m_OpenView = FindMethodInHierarchy(klassUIManager, (const char *) OBFUSCATE("OpenView"),
                                             2);

    if (m_OpenView) {
        void *args[2] = {typeObject, intentObj};
        _monoRuntimeInvoke(m_OpenView, uiManagerInst, args, &exc);

        if (!exc) {
            LOGI((const char *) OBFUSCATE("[+] REMOTE SHOP BERHASIL DIBUKA!"));
        } else {
            LOGE((const char *) OBFUSCATE("[-] Exception saat OpenView dipanggil!"));
        }
    }
}

void ExecuteAutoClaimBubblesNative(std::string dllName, std::string nameSpace, std::string className) {
    // ðŸ’Ž ANTI-ERROR: Bersihkan akhiran ".dll" dari payload backend
    if (dllName.length() >= 4 &&
        dllName.substr(dllName.length() - 4) == (const char *) OBFUSCATE(".dll")) {
        dllName = dllName.substr(0, dllName.length() - 4);
    }

    // Jalankan di Main Thread agar ECS System tidak crash
    RunOnUnityThreadSync([dllName, nameSpace, className]() {
        void *domain = _monoGetRootDomain();
        if (domain) _monoThreadAttach(domain);

        // --- RESOLVE CLASS BUBBLE COMPONENT ---
        void *imgGameplay = _monoImageLoaded(dllName.c_str());
        if (!imgGameplay) imgGameplay = _monoImageLoaded((dllName + ".dll").c_str());
        if (!imgGameplay) {
            LOGE("[BUBBLE] image '%s' tidak loaded (cek payload backend)", dllName.c_str());
            return;
        }

        void *bubbleKlass = _monoClassFromName(imgGameplay, nameSpace.c_str(), className.c_str());
        if (!bubbleKlass) {
            bubbleKlass = _monoClassFromName(imgGameplay, (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Bubble"), (const char *) OBFUSCATE("BubbleComponent"));
        }
        if (!bubbleKlass) {
            LOGE("[BUBBLE] class '%s.%s' tidak ketemu di '%s'", nameSpace.c_str(),
                 className.c_str(), dllName.c_str());
            return;
        }

        void *bubbleMonoType = _monoClassGetType(bubbleKlass);

        // --- RESOLVE WORLD & INFLATE GENERIC METHOD ---
        void *imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        if (!imgData) imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
        if (!imgData) {
            return;
        }

        void *worldKlass = _monoClassFromName(imgData, (const char *) OBFUSCATE("XDTGame.GAS.Client"),
                                              (const char *) OBFUSCATE("EntityWorld"));
        if (!worldKlass) {
            worldKlass = _monoClassFromName(imgData, (const char *) OBFUSCATE("XDTLevelAndEntity.Core.World"),
                                                  (const char *) OBFUSCATE("EntityWorld"));
        }
        if (!worldKlass) {
            worldKlass = _monoClassFromName(imgGameplay,
                                            (const char *) OBFUSCATE("XDTLevelAndEntity.Core.World"),
                                            (const char *) OBFUSCATE("EntityWorld"));
        }
        if (!worldKlass) {
            void *imgLvl = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
            if (!imgLvl) imgLvl = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
            if (imgLvl)
                worldKlass = _monoClassFromName(imgLvl,
                                                (const char *) OBFUSCATE("XDTLevelAndEntity.Core.World"),
                                                (const char *) OBFUSCATE("EntityWorld"));
        }
        if (!worldKlass) {
            LOGE("[BUBBLE] EntityWorld TIDAK ketemu (XDTDataAndProtocol / XDTLevelAndEntity)");
            return;
        }

        void *openMethod = FindMethodInHierarchy(worldKlass, (const char *) OBFUSCATE("GetComponentSystem"), 0);
        if (!openMethod) {
            LOGE("[BUBBLE] GetComponentSystem null");
            return;
        }

        void *typeArgs[1] = {bubbleMonoType};
        void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);

        MonoGenericContext context;
        context.class_inst = nullptr;
        context.method_inst = methodInst;

        void *closedMethod = _mono_class_inflate_generic_method(openMethod, &context);
        if (!closedMethod) return;

        void *exc = nullptr;
        void *sysObj = _monoRuntimeInvoke(closedMethod, nullptr, nullptr, &exc);

        if (exc || !sysObj) {
            LOGE("[BUBBLE] GetComponentSystem invoke gagal (exc=%p sysObj=%p)", exc, sysObj);
            return;
        }

        // --- RESOLVE BUBBLE PROTOCOL MANAGER ---
        void *imgProtocol = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        if (!imgProtocol)
            imgProtocol = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
        if (!imgProtocol)
            imgProtocol = _monoImageLoaded(OBFUSCATE("Assembly-CSharp"));

        void *klassProto = _monoClassFromName(imgProtocol, (const char *) OBFUSCATE(
                "XDTDataAndProtocol.ProtocolService.Bubble"), (const char *) OBFUSCATE("BubbleProtocolManager"));
        if (!klassProto && imgGameplay) {
            klassProto = _monoClassFromName(imgGameplay, (const char *) OBFUSCATE(
                    "XDTDataAndProtocol.ProtocolService.Bubble"), (const char *) OBFUSCATE("BubbleProtocolManager"));
        }
        void *m_GetBubbleAward = nullptr;
        if (klassProto) {
            m_GetBubbleAward = FindMethodInHierarchy(klassProto, (const char *) OBFUSCATE("GetBubbleAward"), 1);
        }

        if (!m_GetBubbleAward) {
            LOGE("[BUBBLE] BubbleProtocolManager.GetBubbleAward tidak ketemu");
            return;
        }

        // --- BONGKAR DICTIONARY '_components' ---
        void *sysKlass = _monoObjectGetClass(sysObj);
        void *f_components = _monoClassGetFieldFromName(sysKlass, (const char *) OBFUSCATE("_components"));
        if (!f_components) {
            return;
        }

        void *dictObj = nullptr;
        _monoFieldGetValue(sysObj, f_components, &dictObj);
        if (!dictObj) {
            return;
        }

        void *dictKlass = _monoObjectGetClass(dictObj);
        void *f_entries = _monoClassGetFieldFromName(dictKlass, (const char *) OBFUSCATE("entries"));
        if (!f_entries)
            f_entries = _monoClassGetFieldFromName(dictKlass, (const char *) OBFUSCATE("_entries"));
        if (!f_entries) {
            return;
        }

        void *entriesArr = nullptr;
        _monoFieldGetValue(dictObj, f_entries, &entriesArr);
        if (!entriesArr) {
            return;
        }

        // --- DIRECT MEMORY PARSING (ECS ARRAY) ---
        int length = *(int *) ((uint8_t *) entriesArr + OFF_ARR_COUNT);
        if (length <= 0 || length > 200000) return;

        uint8_t *arrayDataStart = (uint8_t *) entriesArr + OFF_ARR_DATA;
        int elemSize = 24;
        int valueOffset = sizeof(void *) == 8 ? 16 : 12;

        void *m_getEntity = FindMethodInHierarchy(bubbleKlass, (const char *) OBFUSCATE("get_entity"), 0);
        void *m_getDying = FindMethodInHierarchy(bubbleKlass, (const char *) OBFUSCATE("get_dying"), 0);
        void *m_triggerEnable = FindMethodInHierarchy(bubbleKlass, (const char *) OBFUSCATE("TriggerEnable"), 0);

        for (int i = 0; i < length; i++) {
            uint8_t *entryAddr = arrayDataStart + (i * elemSize);
            int hashCode = *(int *) entryAddr;

            if (hashCode >= 0) {
                uint32_t dictKeyId = *(uint32_t *) (entryAddr + 8);
                void *bubbleObj = *(void **) (entryAddr + valueOffset);

                if (bubbleObj) {
                    if (m_getDying) {
                        void *dyingObj = _monoRuntimeInvoke(m_getDying, bubbleObj, nullptr, nullptr);
                        if (dyingObj && *(bool *) _monoObjectUnbox(dyingObj)) {
                            continue;
                        }
                    }

                    if (m_triggerEnable) {
                        void *trigObj = _monoRuntimeInvoke(m_triggerEnable, bubbleObj, nullptr, nullptr);
                        if (trigObj && !*(bool *) _monoObjectUnbox(trigObj)) {
                            continue;
                        }
                    }

                    uint32_t targetNetId = 0;
                    if (m_getEntity) {
                        void *entObj = _monoRuntimeInvoke(m_getEntity, bubbleObj, nullptr, nullptr);
                        if (entObj) {
                            void *entKlass = _monoObjectGetClass(entObj);
                            void *m_getNetId = FindMethodInHierarchy(entKlass, (const char *) OBFUSCATE("get_netId"), 0);
                            if (m_getNetId) {
                                void *nObj = _monoRuntimeInvoke(m_getNetId, entObj, nullptr, nullptr);
                                if (nObj) targetNetId = *(uint32_t *) _monoObjectUnbox(nObj);
                            }
                            if (targetNetId == 0) {
                                targetNetId = NativeGetNetId(entObj);
                            }
                        }
                    }

                    if (targetNetId == 0) targetNetId = dictKeyId;
                    if (targetNetId == 0) continue;

                    // Anti-Banned Filter (Cek HashMap)
                    if (g_claimedBubbles.find(targetNetId) != g_claimedBubbles.end()) {
                        continue; // Sudah pernah diklaim, lewati.
                    }

                    void *invokeArgs[1] = {&targetNetId};
                    void *invokeExc = nullptr;

                    // Tembak Klaim ke Server!
                    _monoRuntimeInvoke(m_GetBubbleAward, nullptr, invokeArgs, &invokeExc);

                    if (!invokeExc) {
                        g_claimedBubbles.insert(targetNetId);
                        LOGI("[AutoBubble] Claimed bubble netId=%u (dictKey=%u)", targetNetId, dictKeyId);
                    }
                }
            }
        }
    });
}

void AutoBubbleLoopThread(std::string dllName, std::string nameSpace, std::string className) {
    while (g_autoCollectBubbleActive) {
        ExecuteAutoClaimBubblesNative(dllName, nameSpace, className);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
}

void PatchGatherConfigNative() {
    RunOnUnityThreadSync([]() {
        void *lvlConfigInst = GetLevelScriptableConfigInstanceNative(); // Fungsi yang sudah kamu miliki di kode
        if (!lvlConfigInst) {
            return;
        }

        void *klass = _monoObjectGetClass(lvlConfigInst);
        void *f_gatherConfig = _monoClassGetFieldFromName(klass, OBFUSCATE("gatherConfig"));
        if (!f_gatherConfig)
            return;

        void *gatherConfigInst = nullptr;
        _monoFieldGetValue(lvlConfigInst, f_gatherConfig, &gatherConfigInst);
        if (!gatherConfigInst)
            return;

        void *gatherKlass = _monoObjectGetClass(gatherConfigInst);

        // Helper untuk memanipulasi Float langsung ke Offset Memori
        auto setFloat = [&](const char *fName, float val) {
            void *field = _monoClassGetFieldFromName(gatherKlass, fName);
            if (field) {
                int offset = _monoFieldGetOffset(field);
                *(float *) ((uint8_t *) gatherConfigInst + offset) = val;
            }
        };

        // EKSEKUSI VACUUM MAGNET
        setFloat(OBFUSCATE("AutoCollectingDistance"), 999.0f);
        setFloat(OBFUSCATE("WaitingForCollectingTime"), 0.0f);
        setFloat(OBFUSCATE("BeCollectedFlyDurationMin"), 0.0f);
        setFloat(OBFUSCATE("BeCollectedFlyDurationMax"), 0.0f);
        setFloat(OBFUSCATE("DropAngle"), 0.0f);

        // LOGI("[+] Vacuum Magnet Sukses: Jarak=999, Speed=0, Delay=0");
    });
}

static bool ParseResourceCoord(const std::string& text, Vector3& out) {
    return sscanf(text.c_str(), "v3:%f|%f|%f", &out.x, &out.y, &out.z) == 3;
}

static bool IsResourceTypeCompatible(int resourceIndex, int resType) {
    // Sama dengan versi PC:
    // 1=Tree, 2=Stone/Ore, 3=Bush, 9=Meteorite, 10=Dynamic Collectable.
    if (resourceIndex == 0 || resourceIndex == 1) return resType == 2;
    // Pada build Android, Mandarin dapat terdaftar sebagai Tree (1) atau
    // Dynamic Collectable (10). Apple tetap harus berupa Tree.
    if (resourceIndex == 2) return resType == 1 || resType == 10;
    if (resourceIndex == 5) return resType == 1;

    // 17 = Premium Wood（优质木材）：iOS 新增，坐标表来自用户提供的树点。
    // 高级树仍走正常砍树协议（resType 1）；少数配置把它报成 Dynamic Collectable(10)。
    if (resourceIndex == 17) return resType == 1 || resType == 10;

    // Raspberry dan Blueberry dapat muncul sebagai Bush biasa (3) maupun
    // Dynamic Collectable (10), tergantung konfigurasi spawn.
    if (resourceIndex == 3 || resourceIndex == 4)
        return resType == 3 || resType == 10;

    // Resource bawah laut tetap dipanen melalui SendPickBushCommand, tetapi
    // jangan menerima kategori yang jelas berupa pohon/batu/meteorite.
    if (resourceIndex >= 14 && resourceIndex <= 16)
        return resType != 1 && resType != 2 && resType != 9;

    if (resourceIndex >= 3 && resourceIndex < 14) return resType == 10;
    return false;
}

static uint32_t InstantFarmGetStrictNetId(void *entityObj) {
    if (!entityObj) return 0;
    void *klass = _monoObjectGetClass(entityObj);
    if (!klass) return 0;

    static const char *methods[] = {"get_netId", "GetNetId"};
    for (const char *name : methods) {
        void *method = FindMethodInHierarchy(klass, name, 0);
        if (!method) continue;
        void *exc = nullptr;
        void *result = _monoRuntimeInvoke(method, entityObj, nullptr, &exc);
        if (!exc && result) {
            void *raw = _monoObjectUnbox(result);
            if (raw) {
                uint32_t value = *(uint32_t *)raw;
                if (value) return value;
            }
        }
    }

    static const char *fields[] = {
        "netId", "NetId", "mNetId", "_netId", "<netId>k__BackingField"
    };
    for (const char *name : fields) {
        void *field = FindFieldInHierarchy(klass, name);
        if (!field) continue;
        uint32_t value = 0;
        _monoFieldGetValue(entityObj, field, &value);
        if (value) return value;
    }
    return 0;
}

static int ClassifyResourceByPosition(const Vector3& pos, int resType, float* outDistance = nullptr) {
    int best = -1;
    float bestDistSq = 3.402823466e+38F;
    for (int resourceIndex = 0; resourceIndex < NUM_RESOURCES; ++resourceIndex) {
        if (!IsResourceTypeCompatible(resourceIndex, resType)) continue;
        for (const std::string& encoded : *allResourceVectors[resourceIndex]) {
            Vector3 known{};
            if (!ParseResourceCoord(encoded, known)) continue;
            const float dx = pos.x - known.x;
            const float dy = (pos.y - known.y) * 0.35f;
            const float dz = pos.z - known.z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq < bestDistSq) { bestDistSq = distSq; best = resourceIndex; }
        }
    }
    if (outDistance) *outDistance = best >= 0 ? std::sqrt(bestDistSq) : 3.402823466e+38F;
    return best >= 0 && bestDistSq <= (1.5f * 1.5f) ? best : -1;
}

static void* FindResourceIdGetter(void* klass) {
    static const char* names[] = {
            "get_resourceConfigId", "get_ResourceConfigId", "get_resourceID",
            "get_resourceId", "get_ResourceId", "get_staticId", "get_StaticId",
            "get_ResStaticId", "get_CollectableId"};
    if (!klass) return nullptr;
    for (const char* name : names) {
        void* method = FindMethodInHierarchy(klass, name, 0);
        if (method) return method;
    }
    return nullptr;
}

static int InvokeResourceIdGetter(void* method, void* instance) {
    if (!method || !instance) return 0;
    void* exc = nullptr;
    void* result = _monoRuntimeInvoke(method, instance, nullptr, &exc);
    if (!result || exc) return 0;
    void* raw = _monoObjectUnbox(result);
    return raw ? *(int*)raw : 0;
}

static std::string ManagedStringAscii(void* stringObj) {
    if (!stringObj) return {};
    int length = *(int*)((uint8_t*)stringObj + 0x10);
    if (length <= 0 || length > 512) return {};
    std::string out;
    out.reserve((size_t)length);
    const uint16_t* chars = (const uint16_t*)((uint8_t*)stringObj + 0x14);
    for (int i = 0; i < length; ++i) {
        uint16_t c = chars[i];
        if (c >= 32 && c <= 126) out.push_back((char)c);
    }
    return out;
}

static std::string ObjectIdentityText(void* obj) {
    if (!obj) return {};
    void* klass = _monoObjectGetClass(obj);
    void* toString = klass ? FindMethodInHierarchy(klass, OBFUSCATE("ToString"), 0) : nullptr;
    if (!toString) return {};
    void* exc = nullptr;
    void* result = _monoRuntimeInvoke(toString, obj, nullptr, &exc);
    return (!exc && result) ? ManagedStringAscii(result) : std::string();
}

static int ClassifyResourceIdentity(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    auto has = [&](const char* value) { return text.find(value) != std::string::npos; };
    if (has("sea asparagus") || has("seaasparagus") || has("sea_asparagus")) return 14;
    if (has("sea grapes") || has("sea grape") || has("seagrapes") || has("sea_grape")) return 15;
    if (has("wakame") || has("undaria")) return 16;
    if (has("mustard greens") || has("mustardgreens") || has("mustard_green") || has("shepherdspurse")) return 13;
    if (has("tall mustard") || has("tallmustard") || has("garlicmustard")) return 7;
    if (has("black truffle") || has("truffle") || has("p_gather_truffle")) return 8;
    if (has("fiddlehead") || has("event_fiddlehead") || has("bracken")) return 9;
    if (has("penny bun") || has("pennybun") || has("penny") || has("boletus")) return 10;
    if (has("shiitake") || has("shitake") || has("p_gather_shiitake")) return 11;
    if (has("burdock") || has("p_gather_burdock")) return 12;
    if (has("oyster") || has("pleurotus")) return 6;
    // 优质木材（高级树）：只认非常具体的词，避免普通树/果树被误判成它。
    // identity 里没有这些词也没关系 —— FIXED 路点会用位置/类型兜底归类。
    if (has("premium wood") || has("premiumwood") || has("premium_wood") ||
        has("rare tree") || has("raretree") || has("rare_tree") ||
        has("fine wood") || has("finewood") || has("fine_wood") ||
        has("quality wood") || has("qualitywood") || has("quality_wood") ||
        has("hardwood") || has("hard wood") || has("hard_wood"))
        return 17;
    if (has("blueberry") || has("p_fruit_blueberry")) return 4;
    if (has("raspberry") || has("p_fruit_raspberry")) return 3;
    if (has("mandarin") || has("orange") || has("p_fruit_citrus")) return 2;
    if (has("apple") || has("p_fruit_apple")) return 5;

    // Periksa key material spesifik sebelum kata umum "stone". Tanpa urutan ini,
    // p_material_stone2 (Ore) salah dikenali sebagai Stone dan ditolak oleh
    // validasi iconConsistent, sehingga waypoint langsung dianggap kosong.
    if (has("p_material_stone2")) return 0;
    if (has("p_material_stone1")) return 1;
    if (has("ore")) return 0;
    if (has("stone") || has("rock")) return 1;
    return -1;
}

static int GetInstantFarmWaypointResourceIndex(const Vector3& waypoint) {
    for (int resourceIndex = 0; resourceIndex < NUM_RESOURCES; ++resourceIndex) {
        for (const std::string& encoded : *allResourceVectors[resourceIndex]) {
            Vector3 point{};
            if (!ParseResourceCoord(encoded, point)) continue;
            if (std::fabs(point.x - waypoint.x) < 0.01f &&
                std::fabs(point.y - waypoint.y) < 0.01f &&
                std::fabs(point.z - waypoint.z) < 0.01f) return resourceIndex;
        }
    }
    return -1;
}

static float InstantFarmHorizontalDistance(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

static const char* GetInstantFarmIconSpriteKey(int resourceIndex) {
    static const char* keys[NUM_RESOURCES] = {
        "p_material_stone2", "p_material_stone1", "p_fruit_citrus",
        "p_fruit_raspberry", "p_fruit_blueberry", "p_fruit_apple",
        "p_gather_pleurotus_00", "p_gather_garlicmustard_00", "p_gather_truffle_00",
        "p_gather_bracken_00", "p_gather_boletus_00", "p_gather_shiitake_00",
        "p_gather_burdock_00", "p_gather_shepherdspurse_00", "", "", "",
        ""   // 17 Premium Wood：不设 iconKey（iconMapped=false → iconConsistent 恒真）
    };
    return resourceIndex >= 0 && resourceIndex < NUM_RESOURCES ? keys[resourceIndex] : "";
}

static void* FindFirstInstantFarmIdGetter(void* klass, const char** foundName) {
    static const char* names[] = {
        "get_resourceConfigId", "get_ResourceConfigId", "get_resourceID", "get_resourceId",
        "get_ResourceId", "get_staticId", "get_StaticId", "get_ResStaticId", "get_CollectableId"
    };
    if (foundName) *foundName = nullptr;
    for (const char* name : names) {
        void* method = klass ? FindMethodInHierarchy(klass, name, 0) : nullptr;
        if (method) { if (foundName) *foundName = name; return method; }
    }
    return nullptr;
}

static int InvokeInstantFarmIdGetter(void* method, void* instance) {
    return InvokeResourceIdGetter(method, instance);
}

static std::string InstantFarmObjectText(void* object) { return ObjectIdentityText(object); }
static int ClassifyInstantFarmIdentityText(const std::string& text) { return ClassifyResourceIdentity(text); }
static int ClassifyInstantFarmResource(const Vector3& pos, int resType, float* distance = nullptr) {
    return ClassifyResourceByPosition(pos, resType, distance);
}
static bool IsInstantFarmTypeCompatible(int index, int type) { return IsResourceTypeCompatible(index, type); }

static float GetInstantFarmFixedRadius(int resourceIndex) {
    // Koordinat teleport adalah posisi karakter, sedangkan pivot entity batu/pohon
    // dapat berada beberapa meter dari titik tersebut. Bush dan resource laut tetap
    // memakai radius sempit agar tidak mengambil resource tetangga.
    switch (resourceIndex) {
        case 0:             return 5.0f; // Ore
        case 1:             return 3.0f; // Stone
        case 2:             return 8.0f; // Mandarin: pivot entity dapat lebih jauh dari titik teleport
        case 5:             return 5.0f; // Apple
        case 17:            return 8.0f; // Premium Wood: 树 pivot 离传送点更远
        case 14:
        case 15:
        case 16:            return 2.0f; // Resource laut
        default:            return 1.5f; // Raspberry, Blueberry
    }
}

static uint64_t InstantFarmNowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// --- 采集循环的传送节流 / 熔断（只由采集 worker 线程访问）---
// iOS 版传送崩在"生效后游戏处理位置变更"，所以采集这种每几秒传一次的循环必须
//   ① 传送成功后 1500ms 内不再传（给流式加载/AOI 留时间）；
//   ② 连续 2 次落地确认失败就停掉本轮传送（把 waypoint 视为已到达），避免反复崩。
static std::atomic<uint64_t> g_farmTpCooldownUntilMs{0};
static int g_farmTpLandFailCount = 0;
static bool g_farmTpDisabled = false;

static bool InstantFarmMoveToSearchPoint(const std::string& dllName, const Vector3& point) {
    (void)dllName;
    SMPlatform::SetBreadcrumbLight("farm:move-pre");

    // 熔断：本轮采集已经连续两次落地失败 → 不再传送，waypoint 当作"已到达"
    // （上层会直接去扫当前区域，不会再触发崩溃链路）。
    if (g_farmTpDisabled) {
        SMPlatform::SetBreadcrumbLight("farm:tp-disabled");
        return true;
    }

    // 冷却：传送成功后 1500ms 内不再传送。分片等待，STOP 仍然立即生效。
    uint64_t now = InstantFarmNowMs();
    const uint64_t cooldownUntil = g_farmTpCooldownUntilMs.load(std::memory_order_relaxed);
    if (cooldownUntil > now) {
        SMPlatform::SetBreadcrumbLight("farm:tp-cooldown");
        while (now < cooldownUntil && g_instantFarmingActive) {
            const uint64_t slice = (cooldownUntil - now < 50) ? (cooldownUntil - now) : 50;
            usleep((useconds_t) slice * 1000u);
            now = InstantFarmNowMs();
        }
        if (!g_instantFarmingActive) return false;
    }

    const bool moved = AutoMoveTransferNative(point);
    const int outcome = g_tpLastOutcome.load(std::memory_order_relaxed);
    if (moved && outcome == kSMTpOk) {
        // 传送成功落地：记冷却时间戳，清零连续失败计数。
        g_farmTpCooldownUntilMs.store(InstantFarmNowMs() + 1500, std::memory_order_relaxed);
        g_farmTpLandFailCount = 0;
        SMPlatform::SetBreadcrumbLight("farm:move-land");
        return true;
    }

    // invoke 真的发生过（不是"区域没就绪被放弃"）也算一次落地失败：先冷却再计数。
    if (outcome == kSMTpLandFail) {
        g_farmTpCooldownUntilMs.store(InstantFarmNowMs() + 1500, std::memory_order_relaxed);
        ++g_farmTpLandFailCount;
        SMPlatform::SetBreadcrumbLight("farm:tp-land-fail");
        LOGE("[InstantFarm] teleport land-confirm gagal (%d kali berturut-turut).",
             g_farmTpLandFailCount);
        if (g_farmTpLandFailCount >= 2) {
            g_farmTpDisabled = true;
            SMPlatform::SetBreadcrumbLight("farm:tp-disabled");
            LOGI("[InstantFarm] teleport dimatikan untuk putaran ini (waypoint dianggap tercapai).");
            return true;
        }
    }
    return false;
}

static bool InstantFarmUsesAreaScanner(int resourceIndex) {
    // Hanya resource darat dengan satu anchor yang memakai scanner area.
    // Ore-Apple dan ketiga resource laut selalu memakai koordinat FIXED.
    return resourceIndex >= 6 && resourceIndex <= 13;
}

static std::vector<InstantFarmWaypoint> BuildResourceSearchPoints() {
    std::vector<InstantFarmWaypoint> points;
    for (int resourceIndex = 0; resourceIndex < NUM_RESOURCES; ++resourceIndex) {
        if (!g_farmSelections[resourceIndex]) continue;
        for (const std::string& encoded : *allResourceVectors[resourceIndex]) {
            InstantFarmWaypoint waypoint;
            waypoint.resourceIndex = resourceIndex;
            waypoint.useAreaScanner = InstantFarmUsesAreaScanner(resourceIndex);
            if (ParseResourceCoord(encoded, waypoint.position)) points.push_back(waypoint);
        }
    }
    return points;
}

void ExecuteInstantFarmThread(
        std::string dllName, const std::string& nameSpace, const std::string& className) {
    SMPlatform::SetBreadcrumbLight("farm:instant");
        const uint64_t workerGeneration = g_instantFarmGeneration.load(std::memory_order_acquire);
        if (!g_instantFarmingActive || workerGeneration == 0) return;

        uint64_t expectedLease = g_instantFarmWorkerLease.load(std::memory_order_acquire);
        while (expectedLease != workerGeneration) {
            if (g_instantFarmGeneration.load(std::memory_order_acquire) != workerGeneration) return;
            if (g_instantFarmWorkerLease.compare_exchange_weak(
                    expectedLease, workerGeneration, std::memory_order_acq_rel)) break;
        }
        if (expectedLease == workerGeneration) return; // Trigger duplikat untuk start yang sama.

        const auto workerActive = [&]() {
            return g_instantFarmingActive &&
                g_instantFarmGeneration.load(std::memory_order_acquire) == workerGeneration;
        };

        if (dllName.length() >= 4 && dllName.substr(dllName.length() - 4) == ".dll") {
            dllName = dllName.substr(0, dllName.length() - 4);
        }
        PatchGatherConfigNative();

        // 新一轮采集：重置传送冷却/熔断状态（上一轮的失败不该永久禁用传送）。
        g_farmTpCooldownUntilMs.store(0, std::memory_order_relaxed);
        g_farmTpLandFailCount = 0;
        g_farmTpDisabled = false;

        std::string selectedNames;
        for (int i = 0; i < NUM_RESOURCES; ++i) {
            if (!g_farmSelections[i]) continue;
            if (!selectedNames.empty()) selectedNames += ",";
            selectedNames += farmNames[i];
        }
        // BuildInstantFarmSearchPoints mempertahankan urutan kategori (Opsi A):
        // seluruh waypoint kategori terpilih pertama, lalu kategori berikutnya.
        const std::vector<InstantFarmWaypoint> searchPoints = BuildResourceSearchPoints();
        size_t searchPointIndex = 0;
        size_t currentWaypointNumber = 0;
        InstantFarmWaypoint currentWaypoint{};
        int consecutiveEmptyScans = 0;
        bool mustMoveToWaypoint = true;

        LOGI("[InstantFarm] ===== WORKER START generation=%llu =====\n",
            (unsigned long long)workerGeneration);
        LOGI("[InstantFarm] Selected='%s' waypoints=%zu.\n",
            selectedNames.c_str(), searchPoints.size());

        // Antrean hanya menyimpan snapshot ID/data sederhana. Pointer Mono tidak
        // disimpan karena entity dapat unload setelah teleport atau cooldown.
        std::vector<InstantFarmTarget> targetQueue;
        size_t targetQueueIndex = 0;
        uint32_t currentTargetNetId = 0;
        int currentTargetAttempts = 0;
        uint64_t currentTargetStartedAt = 0;
        bool currentTargetTeleported = false;
        std::unordered_set<uint32_t> failedTargetsAtWaypoint;

        auto interruptibleWait = [&](int milliseconds) {
            for (int waited = 0; waited < milliseconds && workerActive(); waited += 50) {
                if (g_toolRestoring) return false;
                const int slice = (milliseconds - waited < 50) ? (milliseconds - waited) : 50;
                usleep((useconds_t)slice * 1000u);
            }
            return workerActive() && !g_toolRestoring;
        };
        const auto cooldownMilliseconds = [](const std::atomic<float>& value) {
            float seconds = value.load(std::memory_order_relaxed);
            // 0.30s 下限：采集是"对服务器发指令"，不是本地循环。0.1s 那种节奏
            // 等于连点，服务端开始判无效之后全是无效请求，也更容易被反作弊盯上。
            if (seconds < 0.30f) seconds = 0.30f;
            if (seconds > 5.0f) seconds = 5.0f;
            return (int)(seconds * 1000.0f + 0.5f);
        };

        while (workerActive()) {
            // JEDA saat Auto Restore Tool sedang mengisi durability. Lanjut lagi begitu tool FULL kembali.
            if (g_toolRestoring) { usleep(300 * 1000u); continue; }

            if (searchPoints.empty()) {
                usleep(1000 * 1000u);
                continue;
            }

            // Koordinat preset hanya menjadi titik pembuka streaming. Target panen
            // selalu berasal dari hasil scan entity runtime setelah tiba di area.
            if (mustMoveToWaypoint) {
                if (searchPointIndex >= searchPoints.size()) searchPointIndex = 0;
                currentWaypointNumber = searchPointIndex + 1;
                currentWaypoint = searchPoints[searchPointIndex++];
                LOGI("[InstantFarm] WAYPOINT move %zu/%zu resource='%s' mode=%s pos=(%.2f, %.2f, %.2f).\n",
                    currentWaypointNumber, searchPoints.size(),
                    (currentWaypoint.resourceIndex >= 0 && currentWaypoint.resourceIndex < NUM_RESOURCES)
                        ? farmNames[currentWaypoint.resourceIndex] : "UNKNOWN",
                    currentWaypoint.useAreaScanner ? "AREA_SCAN" : "FIXED",
                    currentWaypoint.position.x, currentWaypoint.position.y, currentWaypoint.position.z);
                if (!InstantFarmMoveToSearchPoint(dllName, currentWaypoint.position)) {
                    LOGI("[InstantFarm] WAYPOINT move FAILED %zu/%zu.\n",
                        currentWaypointNumber, searchPoints.size());
                    usleep(1000 * 1000u);
                    continue;
                }

                targetQueue.clear();
                targetQueueIndex = 0;
                currentTargetNetId = 0;
                currentTargetAttempts = 0;
                currentTargetStartedAt = 0;
                currentTargetTeleported = false;
                failedTargetsAtWaypoint.clear();
                consecutiveEmptyScans = 0;
                mustMoveToWaypoint = false;

                // Tunggu streaming area/component system, tetapi STOP tetap responsif.
                if (!interruptibleWait(2500)) continue;
            }

            std::vector<InstantFarmTarget> routeList;
            std::unordered_set<uint32_t> cooldownNetIds;
            int scanScanned = 0;
            int scanClassified = 0;
            int scanSelectedActive = 0;
            int scanSelectedCooldown = 0;
            int scanUnknown = 0;
            int scanIncompatible = 0;
            int scanNotSelected = 0;
            int scanColdReadFailed = 0;
            int scanDuplicate = 0;

            RunOnUnityThreadSync([&]() {
                _monoThreadAttach(_monoGetRootDomain());
                void* imgGameplay = _monoImageLoaded(dllName.c_str());
                if (!imgGameplay) return;

                void* collectKlass = _monoClassFromName(imgGameplay, nameSpace.c_str(), className.c_str());
                if (!collectKlass) return;
                void* collectMonoType = _monoClassGetType(collectKlass);

                void* imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
                void* worldKlass = imgData ? _monoClassFromName(imgData, OBFUSCATE("XDTGame.GAS.Client"), OBFUSCATE("EntityWorld")) : nullptr;
                if (!worldKlass && imgData) worldKlass = _monoClassFromName(imgData, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("EntityWorld"));
                if (!worldKlass) worldKlass = _monoClassFromName(imgGameplay, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("EntityWorld"));
                if (!worldKlass) return;
                void* openMethod = FindMethodInHierarchy(worldKlass, OBFUSCATE("GetComponentSystem"), 0);
                if (!openMethod) return;

                void* typeArgs[] = { collectMonoType };
                void* methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
                MonoGenericContext ctx = { nullptr, methodInst };
                void* closedMethod = _mono_class_inflate_generic_method(openMethod, &ctx);

                void* exc = nullptr;
                void* sysObj = _monoRuntimeInvoke(closedMethod, nullptr, nullptr, &exc);
                if (!sysObj || exc) return;

                void* f_components = _monoClassGetFieldFromName(_monoObjectGetClass(sysObj), OBFUSCATE("_components"));
                void* dictObj = nullptr;
                _monoFieldGetValue(sysObj, f_components, &dictObj);
                if (!dictObj) return;

                void* dictKlass = _monoObjectGetClass(dictObj);
                void* f_entries = _monoClassGetFieldFromName(dictKlass, OBFUSCATE("entries"));
                if (!f_entries) f_entries = _monoClassGetFieldFromName(dictKlass, OBFUSCATE("_entries"));
                if (!f_entries) return;

                void* entriesArr = nullptr;
                _monoFieldGetValue(dictObj, f_entries, &entriesArr);
                if (!entriesArr) return;

                uintptr_t arrAddr = (uintptr_t)entriesArr;
                int length = *(int*)(arrAddr + 0x18);
                uintptr_t arrayStart = arrAddr + 0x20;
                int elemSize = 24;
                int valueOffset = 16;

                void* m_getResType = FindMethodInHierarchy(collectKlass, OBFUSCATE("get_resType"), 0);
                void* m_getInCold = FindMethodInHierarchy(collectKlass, OBFUSCATE("get_inCold"), 0);
                void* m_getEntity = FindMethodInHierarchy(collectKlass, OBFUSCATE("get_entity"), 0);
                void* entityKlass = imgData ? _monoClassFromName(imgData, OBFUSCATE("XDTGame.GAS.Client"), OBFUSCATE("Entity")) : nullptr;
                if (!entityKlass && imgData) entityKlass = _monoClassFromName(imgData, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("Entity"));
                if (!entityKlass) entityKlass = _monoClassFromName(imgGameplay, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("Entity"));
                void* m_getPos = entityKlass ? FindMethodInHierarchy(entityKlass, OBFUSCATE("get_position"), 0) : nullptr;
                void* m_getNetId = entityKlass ? FindMethodInHierarchy(entityKlass, OBFUSCATE("get_netId"), 0) : nullptr;
                if (!m_getNetId && entityKlass)
                    m_getNetId = FindMethodInHierarchy(entityKlass, OBFUSCATE("GetNetId"), 0);
                const int activeWaypointResourceIndex = currentWaypoint.resourceIndex;
                const bool useAreaScanner = currentWaypoint.useAreaScanner;
                constexpr float kSpawnAreaRadius = 45.0f;
                const float fixedCoordRadius =
                    GetInstantFarmFixedRadius(activeWaypointResourceIndex);

                // Jalur identity seperti helper referensi: entity -> LevelObject -> resourceID/label.
                void* imgLevelIdentity = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
                if (!imgLevelIdentity) imgLevelIdentity = _monoImageLoaded(dllName.c_str());
                void* entityHelperKlass = imgLevelIdentity
                    ? _monoClassFromName(imgLevelIdentity, OBFUSCATE("XDTLevelAndEntity.Utils"), OBFUSCATE("EntityHelper"))
                    : nullptr;
                void* m_getLevelObject = entityHelperKlass
                    ? FindMethodInHierarchy(entityHelperKlass, OBFUSCATE("GetLevelObject"), 1)
                    : nullptr;

                const char* collectIdGetterName = nullptr;
                const char* entityIdGetterName = nullptr;
                void* m_getCollectResourceId = FindFirstInstantFarmIdGetter(collectKlass, &collectIdGetterName);
                void* m_getEntityResourceId = FindFirstInstantFarmIdGetter(entityKlass, &entityIdGetterName);
                static std::mutex inspectLogMutex;
                static std::unordered_set<uint64_t> inspectedEntities;
                static std::unordered_map<int, int> resourceIdToIndex;

                if (!m_getResType || !m_getInCold || !m_getEntity || !m_getPos) {
                    return;
                }

                int scannedCount = 0;
                int classifiedCount = 0;
                int selectedCount = 0;
                std::unordered_set<uint32_t> queuedNetIds;
                std::unordered_map<uint32_t, uint32_t> dictionaryKeyByNetId;
                for (int i = 0; i < length; i++) {
                    int hash = *(int*)(arrayStart + (i * elemSize));
                    if (hash >= 0) {
                        uint32_t entityId = *(uint32_t*)(arrayStart + (i * elemSize) + 8);
                        void* collectObj = *(void**)(arrayStart + (i * elemSize) + valueOffset);

                        if (entityId > 0 && collectObj) {
                            void* resTypeObj = _monoRuntimeInvoke(m_getResType, collectObj, nullptr, nullptr);
                            if (!resTypeObj) continue;
                            int resType = *(int*)_monoObjectUnbox(resTypeObj);

                            ++scannedCount;
                            void* entityObj = _monoRuntimeInvoke(m_getEntity, collectObj, nullptr, nullptr);
                            if (!entityObj) continue;

                            // Key Dictionary bukan selalu NetID protocol. Gunakan hanya
                            // getter get_netId/GetNetId yang semantik-nya pasti network ID;
                            // get_Id sengaja tidak dipakai karena dapat berupa local/config ID.
                            uint32_t actualNetId = 0;
                            if (m_getNetId) {
                                void* netExc = nullptr;
                                void* netObj = _monoRuntimeInvoke(m_getNetId, entityObj, nullptr, &netExc);
                                if (netObj && !netExc)
                                    actualNetId = *(uint32_t*)_monoObjectUnbox(netObj);
                            }
                            const uint32_t commandEntityId = actualNetId != 0 ? actualNetId : entityId;
                            dictionaryKeyByNetId[commandEntityId] = entityId;

                            void* posObj = _monoRuntimeInvoke(m_getPos, entityObj, nullptr, nullptr);
                            if (!posObj) continue;

                            Vector3 pos{};
                            memcpy(&pos, _monoObjectUnbox(posObj), sizeof(Vector3));

                            int resourceId = 0;
                            const char* idSource = "NONE";
                            std::string identityText;
                            const char* identitySource = "NONE";
                            void* levelObject = nullptr;

                            // EntityHelper.GetLevelObject menerima ulong entity id pada build game ini.
                            if (m_getLevelObject) {
                                uint64_t longEntityId = (uint64_t)entityId;
                                void* levelArgs[] = { &longEntityId };
                                void* levelExc = nullptr;
                                levelObject = _monoRuntimeInvoke(m_getLevelObject, nullptr, levelArgs, &levelExc);
                                if (levelExc) levelObject = nullptr;
                            }

                            if (levelObject) {
                                void* levelKlass = _monoObjectGetClass(levelObject);
                                const char* levelGetterName = nullptr;
                                void* levelIdGetter = FindFirstInstantFarmIdGetter(levelKlass, &levelGetterName);
                                resourceId = InvokeInstantFarmIdGetter(levelIdGetter, levelObject);
                                if (resourceId != 0) idSource = levelGetterName ? levelGetterName : "LevelObject";
                                identityText = InstantFarmObjectText(levelObject);
                                if (!identityText.empty()) identitySource = "LevelObject.ToString";
                            }

                            if (resourceId == 0) {
                                resourceId = InvokeInstantFarmIdGetter(m_getCollectResourceId, collectObj);
                                if (resourceId != 0) idSource = collectIdGetterName ? collectIdGetterName : "Collectable";
                            }
                            if (resourceId == 0) {
                                resourceId = InvokeInstantFarmIdGetter(m_getEntityResourceId, entityObj);
                                if (resourceId != 0) idSource = entityIdGetterName ? entityIdGetterName : "Entity";
                            }
                            if (identityText.empty()) {
                                identityText = InstantFarmObjectText(collectObj);
                                if (!identityText.empty()) identitySource = "Collectable.ToString";
                            }

                            float nearestDistance = FLT_MAX;
                            const float waypointDistance = InstantFarmHorizontalDistance(pos, currentWaypoint.position);
                            int resourceIndex = ClassifyInstantFarmIdentityText(identityText);
                            if (resourceIndex >= 0) {
                                identitySource = "managed-label/icon";
                                if (resourceId > 0) {
                                    std::lock_guard<std::mutex> logLock(inspectLogMutex);
                                    auto old = resourceIdToIndex.find(resourceId);
                                    if (old == resourceIdToIndex.end()) {
                                        resourceIdToIndex[resourceId] = resourceIndex;
                                    }
                                    else if (old->second != resourceIndex) {
                                        resourceIndex = -1; // Jangan panen identity yang bertentangan.
                                    }
                                }
                            }
                            else if (resourceId > 0) {
                                std::lock_guard<std::mutex> logLock(inspectLogMutex);
                                auto known = resourceIdToIndex.find(resourceId);
                                if (known != resourceIdToIndex.end()) {
                                    resourceIndex = known->second;
                                    identitySource = "resourceID-cache";
                                }
                            }

                            // Bootstrap presisi memakai anchor lama bila tersedia.
                            if (resourceIndex < 0) {
                                const int positionIndex = ClassifyInstantFarmResource(pos, resType, &nearestDistance);
                                if (positionIndex >= 0) {
                                    resourceIndex = positionIndex;
                                    identitySource = "position-bootstrap";
                                    if (resourceId > 0) {
                                        std::lock_guard<std::mutex> logLock(inspectLogMutex);
                                        auto old = resourceIdToIndex.find(resourceId);
                                        if (old == resourceIdToIndex.end()) resourceIdToIndex[resourceId] = resourceIndex;
                                        else if (old->second != resourceIndex) resourceIndex = -1;
                                    }
                                }
                            }

                            // Hanya Oyster sampai Mustard Greens yang memakai fallback area.
                            if (resourceIndex < 0 && useAreaScanner &&
                                activeWaypointResourceIndex >= 0 &&
                                waypointDistance <= kSpawnAreaRadius &&
                                IsInstantFarmTypeCompatible(activeWaypointResourceIndex, resType)) {
                                resourceIndex = activeWaypointResourceIndex;
                                identitySource = "waypoint-area";
                            }

                            // Untuk waypoint FIXED, gunakan fallback lokal yang tetap terkunci
                            // pada resource aktif. Radius pohon/ore lebih besar karena koordinat
                            // teleport adalah posisi karakter, bukan selalu pivot entity.
                            if (resourceIndex < 0 && !useAreaScanner &&
                                activeWaypointResourceIndex >= 0 &&
                                waypointDistance <= fixedCoordRadius &&
                                IsInstantFarmTypeCompatible(activeWaypointResourceIndex, resType)) {
                                resourceIndex = activeWaypointResourceIndex;
                                identitySource = "waypoint-fixed";
                            }

                            // Satu waypoint hanya boleh memanen resource pemiliknya.
                            const bool belongsToActiveWaypoint =
                                resourceIndex == activeWaypointResourceIndex;
                            const bool insideWaypointRange = useAreaScanner
                                ? waypointDistance <= kSpawnAreaRadius
                                : waypointDistance <= fixedCoordRadius;
                            const bool indexValid = resourceIndex >= 0 &&
                                resourceIndex < NUM_RESOURCES;
                            if (indexValid) ++classifiedCount;
                            else ++scanUnknown;

                            const bool typeCompatible = indexValid &&
                                IsInstantFarmTypeCompatible(resourceIndex, resType);
                            const bool checkboxSelected = indexValid &&
                                g_farmSelections[resourceIndex];
                            const char* classifiedName = indexValid
                                ? farmNames[resourceIndex] : "UNKNOWN";
                            const char* iconKey = indexValid
                                ? GetInstantFarmIconSpriteKey(resourceIndex) : "";
                            // iconKey adalah mapping lokal dari hasil classified, bukan metadata
                            // independen dari entity. Round-trip ini hanya mendeteksi mapping yang
                            // tidak konsisten; identitas utama tetap classifier + resType + area.
                            const bool iconMapped = iconKey && *iconKey;
                            const bool iconConsistent = !iconMapped ||
                                ClassifyInstantFarmIdentityText(iconKey) == resourceIndex;

                            bool coldReadOk = false;
                            bool inCold = false;
                            void* coldExc = nullptr;
                            void* inColdObj = _monoRuntimeInvoke(
                                m_getInCold, collectObj, nullptr, &coldExc);
                            if (inColdObj && !coldExc) {
                                void* coldRaw = _monoObjectUnbox(inColdObj);
                                if (coldRaw) {
                                    inCold = *(uint8_t*)coldRaw != 0;
                                    coldReadOk = true;
                                }
                            }

                            const char* rejectReason = "ACTIVE_QUEUE";
                            if (!indexValid) {
                                rejectReason = "UNKNOWN_CLASSIFICATION";
                            }
                            else if (!iconConsistent) {
                                rejectReason = "ICON_CLASS_MISMATCH";
                                ++scanIncompatible;
                            }
                            else if (!belongsToActiveWaypoint) {
                                rejectReason = "OTHER_WAYPOINT_RESOURCE";
                                ++scanNotSelected;
                            }
                            else if (!insideWaypointRange) {
                                rejectReason = useAreaScanner
                                    ? "OUTSIDE_AREA_RADIUS" : "OUTSIDE_FIXED_RADIUS";
                                ++scanNotSelected;
                            }
                            else if (!typeCompatible) {
                                rejectReason = "INCOMPATIBLE_RESTYPE";
                                ++scanIncompatible;
                            }
                            else if (!checkboxSelected) {
                                rejectReason = "CHECKBOX_NOT_SELECTED";
                                ++scanNotSelected;
                            }
                            else if (!coldReadOk) {
                                rejectReason = "COLD_READ_FAILED";
                                ++scanColdReadFailed;
                            }
                            else if (inCold) {
                                rejectReason = "COOLDOWN";
                            }

                            // Log per-entity hanya untuk kategori yang dipilih agar file tidak
                            // dipenuhi ratusan entity dunia yang tidak relevan.
                            const uint64_t inspectKey = (uint64_t(entityId) << 16) ^ uint64_t(uint32_t(resType));
                            bool shouldLog = false;
                            if (indexValid && checkboxSelected) {
                                std::lock_guard<std::mutex> logLock(inspectLogMutex);
                                shouldLog = inspectedEntities.insert(inspectKey).second;
                            }
                            if (shouldLog) {
                                LOGI(
                                    "[InstantFarm] CANDIDATE netId=%u resource='%s' iconKey='%s' "
                                    "source='%s' resType=%d distance=%.2f inCold=%d decision=%s.\n",
                                    commandEntityId, classifiedName, iconKey, identitySource,
                                    resType, waypointDistance, inCold ? 1 : 0, rejectReason);
                            }

                            if (!indexValid || !iconConsistent || !belongsToActiveWaypoint ||
                                !insideWaypointRange || !typeCompatible || !checkboxSelected || !coldReadOk)
                                continue;
                            if (inCold) {
                                cooldownNetIds.insert(commandEntityId);
                                ++scanSelectedCooldown;
                                continue;
                            }

                            if (!queuedNetIds.insert(commandEntityId).second) {
                                ++scanDuplicate;
                                continue;
                            }

                            InstantFarmTarget target;
                            target.entityId = commandEntityId;
                            target.dictionaryKey = entityId;
                            target.position = pos;
                            target.resType = resType;
                            target.resourceId = resourceId;
                            target.resourceIndex = resourceIndex;
                            routeList.push_back(target);
                            ++selectedCount;
                            ++scanSelectedActive;
                        }
                    }
                }
                static int scanSerial = 0;
                static int lastScanned = -1;
                static int lastClassified = -1;
                static int lastSelected = -1;
                ++scanSerial;
                scanScanned = scannedCount;
                scanClassified = classifiedCount;
                if (scannedCount != lastScanned || classifiedCount != lastClassified ||
                    selectedCount != lastSelected || (scanSerial % 10) == 0) {
                    lastScanned = scannedCount;
                    lastClassified = classifiedCount;
                    lastSelected = selectedCount;
                    LOGI("[InstantFarm] SCAN waypoint=%zu/%zu scanned=%d classified=%d active=%d cooldown=%d "
                        "unknown=%d incompatible=%d notSelected=%d coldReadFailed=%d duplicate=%d queueSnapshot=%zu.\n",
                        currentWaypointNumber, searchPoints.size(), scanScanned, scanClassified,
                        scanSelectedActive, scanSelectedCooldown, scanUnknown, scanIncompatible,
                        scanNotSelected, scanColdReadFailed, scanDuplicate, routeList.size());
                }
                });

            // Scanner hanya memasukkan target terpilih yang get_inCold() == false.
            // Entity cooldown dengan demikian tidak pernah masuk antrean.
            auto findLiveTarget = [&](uint32_t netId) -> const InstantFarmTarget* {
                for (const auto& candidate : routeList)
                    if (candidate.entityId == netId) return &candidate;
                return nullptr;
            };

            // Setiap scan (termasuk scan setelah teleport) digabungkan ke antrean
            // pending. NetID yang sedang diproses, sudah pending, atau gagal pada
            // waypoint ini tidak dimasukkan dua kali.
            std::unordered_set<uint32_t> pendingNetIds;
            if (currentTargetNetId != 0) pendingNetIds.insert(currentTargetNetId);
            for (size_t i = targetQueueIndex; i < targetQueue.size(); ++i)
                pendingNetIds.insert(targetQueue[i].entityId);

            size_t newlyQueued = 0;
            for (const auto& candidate : routeList) {
                if (failedTargetsAtWaypoint.count(candidate.entityId)) continue;
                if (!pendingNetIds.insert(candidate.entityId).second) continue;
                targetQueue.push_back(candidate);
                ++newlyQueued;
            }
            if (newlyQueued > 0) {
                LOGI("[InstantFarm] QUEUE MERGE added=%zu pending=%zu waypoint=%zu/%zu.\n",
                    newlyQueued, targetQueue.size() - targetQueueIndex,
                    currentWaypointNumber, searchPoints.size());
                consecutiveEmptyScans = 0;
            }

            // Verifikasi ulang target aktif dari scan terbaru. Jika tidak ditemukan,
            // target sudah cooldown/hilang dan dianggap selesai.
            if (currentTargetNetId != 0) {
                const InstantFarmTarget* liveTarget = findLiveTarget(currentTargetNetId);
                if (!liveTarget) {
                    const bool becameCooldown = cooldownNetIds.count(currentTargetNetId) != 0;
                    const int nextTargetWaitMs = cooldownMilliseconds(
                        g_instantFarmNextTargetCooldown);
                    LOGI("[InstantFarm] TARGET DONE netId=%u reason=%s attempts=%d; nextTargetWait=%dms.\n",
                        currentTargetNetId, becameCooldown ? "COOLDOWN" : "MISSING",
                        currentTargetAttempts, nextTargetWaitMs);
                    currentTargetNetId = 0;
                    currentTargetAttempts = 0;
                    currentTargetStartedAt = 0;
                    currentTargetTeleported = false;
                    if (!interruptibleWait(nextTargetWaitMs)) continue;
                    consecutiveEmptyScans = 0;
                    continue;
                }

                const uint64_t now = InstantFarmNowMs();
                if (currentTargetAttempts >= 20 ||
                    (currentTargetStartedAt && now - currentTargetStartedAt >= 15000ULL)) {
                    LOGI("[InstantFarm] TARGET TIMEOUT netId=%u attempts=%d elapsed=%llu; skip at this waypoint.\n",
                        currentTargetNetId, currentTargetAttempts,
                        (unsigned long long)(now - currentTargetStartedAt));
                    failedTargetsAtWaypoint.insert(currentTargetNetId);
                    currentTargetNetId = 0;
                    currentTargetAttempts = 0;
                    currentTargetStartedAt = 0;
                    currentTargetTeleported = false;
                    interruptibleWait(cooldownMilliseconds(
                        g_instantFarmNextTargetCooldown));
                    continue;
                }

                if (!currentTargetTeleported) {
                    LOGI("[InstantFarm] TARGET move netId=%u resource='%s' type=%d pos=(%.2f, %.2f, %.2f).\n",
                        liveTarget->entityId,
                        (liveTarget->resourceIndex >= 0 && liveTarget->resourceIndex < NUM_RESOURCES)
                            ? farmNames[liveTarget->resourceIndex] : "UNKNOWN",
                        liveTarget->resType,
                        liveTarget->position.x, liveTarget->position.y, liveTarget->position.z);
                    if (!InstantFarmMoveToSearchPoint(dllName, liveTarget->position)) {
                        LOGI("[InstantFarm] TARGET move FAILED netId=%u.\n", liveTarget->entityId);
                        ++currentTargetAttempts;
                        interruptibleWait(500);
                        continue;
                    }
                    currentTargetTeleported = true;
                    // Entity dan NetID sudah diperoleh dari scan waypoint aktif,
                    // sehingga command dapat dikirim langsung setelah teleport.
                }

                bool commandOk = false;
                RunOnUnityThreadSync([&]() {
                    if (!workerActive() || g_toolRestoring) return;
                    _monoThreadAttach(_monoGetRootDomain());
                    // 缓存：原来每发一条指令都要 monoClassFromName + 3 次
                    // FindMethodInHierarchy，等于把镜像的类表/方法表从头走一遍；
                    // 采集节奏是 0.4 秒一条指令，Unity 线程被这本账占着，任务泵容易
                    // 拖到超时。这里解析一次就够（进程生命周期内类不会换镜像）。
                    static void* cachedProtocolKlass = nullptr;
                    static void* cachedPickBush = nullptr;
                    static void* cachedHitStone = nullptr;
                    static void* cachedAttackTree = nullptr;
                    if (!cachedProtocolKlass || !cachedPickBush || !cachedHitStone ||
                        !cachedAttackTree) {
                        void* imgProtocol = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
                        if (!imgProtocol) imgProtocol = _monoImageLoaded(OBFUSCATE("Assembly-CSharp"));
                        if (!imgProtocol) return;
                        cachedProtocolKlass = _monoClassFromName(
                            imgProtocol, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Resource"),
                            OBFUSCATE("ResourceProtocolManager"));
                        if (!cachedProtocolKlass) return;
                        cachedPickBush = FindMethodInHierarchy(
                            cachedProtocolKlass, OBFUSCATE("SendPickBushCommand"), 1);
                        cachedHitStone = FindMethodInHierarchy(
                            cachedProtocolKlass, OBFUSCATE("SendHitStoneCommand"), 2);
                        cachedAttackTree = FindMethodInHierarchy(
                            cachedProtocolKlass, OBFUSCATE("SendAttackTreeCommand"), 2);
                    }
                    void* protocolKlass = cachedProtocolKlass;
                    void* sendPickBush = cachedPickBush;
                    void* sendHitStone = cachedHitStone;
                    void* sendAttackTree = cachedAttackTree;

                    // Khusus Mandarin: sebagian CollectableComponent memakai key
                    // dictionary sebagai ID command, bukan Entity.get_netId().
                    // NetID utama tetap dicoba dahulu; fallback baru aktif setelah
                    // tiga hit tidak pernah mengubah target menjadi cooldown.
                    const bool mandarinTarget = liveTarget->resourceIndex == 2;
                    // Premium Wood：不管服务端把高级树报成 Tree(1) 还是 Dynamic
                    // Collectable(10)，一律用砍树协议；否则会退化成拾取命令白跑。
                    const bool premiumWoodTarget = liveTarget->resourceIndex == 17;
                    const bool useDictionaryId = mandarinTarget && currentTargetAttempts >= 3 &&
                        liveTarget->dictionaryKey != 0 &&
                        liveTarget->dictionaryKey != liveTarget->entityId;
                    uint32_t netId = useDictionaryId
                        ? liveTarget->dictionaryKey : liveTarget->entityId;
                    uint8_t isCombo = 0;
                    void* commandExc = nullptr;
                    const bool underwater = liveTarget->resourceIndex >= 14 &&
                        liveTarget->resourceIndex <= 16;
                    if (mandarinTarget || premiumWoodTarget) {
                        // Identitas resource lebih dapat dipercaya daripada resType pada
                        // Mandarin Android; beberapa spawn melaporkannya sebagai type 10.
                        if (!sendAttackTree) return;
                        void* args[] = { &netId, &isCombo };
                        _monoRuntimeInvoke(sendAttackTree, nullptr, args, &commandExc);
                    } else if (underwater || liveTarget->resType == 3 || liveTarget->resType == 10) {
                        if (!sendPickBush) return;
                        void* args[] = { &netId };
                        _monoRuntimeInvoke(sendPickBush, nullptr, args, &commandExc);
                    } else if (liveTarget->resType == 1) {
                        if (!sendAttackTree) return;
                        void* args[] = { &netId, &isCombo };
                        _monoRuntimeInvoke(sendAttackTree, nullptr, args, &commandExc);
                    } else if (liveTarget->resType == 2 || liveTarget->resType == 9) {
                        if (!sendHitStone) return;
                        void* args[] = { &netId, &isCombo };
                        _monoRuntimeInvoke(sendHitStone, nullptr, args, &commandExc);
                    } else return;
                    commandOk = commandExc == nullptr;
                });

                ++currentTargetAttempts;
                const InstantFarmTarget* commandTarget = findLiveTarget(currentTargetNetId);
                const bool usedMandarinDictionaryId = commandTarget &&
                    commandTarget->resourceIndex == 2 && currentTargetAttempts > 3 &&
                    commandTarget->dictionaryKey != 0 &&
                    commandTarget->dictionaryKey != commandTarget->entityId;
                const uint32_t sentCommandId = usedMandarinDictionaryId
                    ? commandTarget->dictionaryKey : currentTargetNetId;
                LOGI("[InstantFarm] COMMAND targetNetId=%u sentId=%u idSource=%s attempt=%d ok=%d; verify by rescan.\n",
                    currentTargetNetId, sentCommandId,
                    usedMandarinDictionaryId ? "dictionary-fallback" : "entity-netId",
                    currentTargetAttempts, commandOk ? 1 : 0);
                // Scan berikutnya menjadi bukti keberhasilan sebenarnya. Interval
                // pengiriman command dibaca live dari slider Tab Game.
                // 下限见 cooldownMilliseconds()（0.30s）：低于这个节奏就是在对着
                // 服务器"连点"，服务端一旦开始丢包/判无效，只会徒增无效请求。
                interruptibleWait(cooldownMilliseconds(
                    g_instantFarmPickupCooldown));
                continue;
            }

            // Ambil target berikutnya dari antrean snapshot bila masih aktif.
            while (targetQueueIndex < targetQueue.size()) {
                const InstantFarmTarget& queued = targetQueue[targetQueueIndex++];
                if (failedTargetsAtWaypoint.count(queued.entityId)) continue;
                if (!findLiveTarget(queued.entityId)) continue;
                currentTargetNetId = queued.entityId;
                LOGI("[InstantFarm] QUEUE NEXT netId=%u index=%zu/%zu.\n",
                    currentTargetNetId, targetQueueIndex, targetQueue.size());
                currentTargetAttempts = 0;
                currentTargetStartedAt = InstantFarmNowMs();
                currentTargetTeleported = false;
                break;
            }
            if (currentTargetNetId != 0) continue;

            // Semua entry pending sudah dikonsumsi atau tidak lagi aktif. Buang
            // snapshot lama; scan berikutnya dapat memasukkan NetID baru kembali.
            targetQueue.clear();
            targetQueueIndex = 0;

            // Dua rescan tanpa target aktif/pending sebelum pindah waypoint.
            ++consecutiveEmptyScans;
            LOGI("[InstantFarm] RESCAN EMPTY waypoint=%zu/%zu pass=%d/2 cooldownSeen=%d.\n",
                currentWaypointNumber, searchPoints.size(), consecutiveEmptyScans,
                scanSelectedCooldown);
            if (consecutiveEmptyScans >= 2) {
                LOGI("[InstantFarm] WAYPOINT EXHAUSTED %zu/%zu; moving to next preset coordinate.\n",
                    currentWaypointNumber, searchPoints.size());
                mustMoveToWaypoint = true;
                consecutiveEmptyScans = 0;
                continue;
            }
            interruptibleWait(750);
        }

    }

void ExecuteLegacyInstantFarmThread(std::string dllName, std::string nameSpace, std::string className) {
    const uint64_t workerGeneration = g_legacyInstantFarmGeneration.load(std::memory_order_acquire);
    if (!g_legacyInstantFarmingActive || workerGeneration == 0) return;

    uint64_t expectedLease = g_legacyInstantFarmWorkerLease.load(std::memory_order_acquire);
    while (expectedLease != workerGeneration) {
        if (g_legacyInstantFarmGeneration.load(std::memory_order_acquire) != workerGeneration) return;
        if (g_legacyInstantFarmWorkerLease.compare_exchange_weak(
                expectedLease, workerGeneration, std::memory_order_acq_rel)) break;
    }
    if (expectedLease == workerGeneration) return;

    const auto workerActive = [&]() {
        return g_legacyInstantFarmingActive && SesiAktif() &&
               g_legacyInstantFarmGeneration.load(std::memory_order_acquire) == workerGeneration;
    };
    const auto selected = [](int type) {
        return (type == 1 && g_farmTree) ||
               (type == 2 && g_farmStone) ||
               (type == 3 && g_farmBush) ||
               (type == 9 && g_farmMeteorite) ||
               (type == 10 && g_farmDynamicBush);
    };
    const auto wait = [&](int ms) {
        for (int elapsed = 0; elapsed < ms && workerActive(); elapsed += 50) {
            int slice = (ms - elapsed < 50) ? (ms - elapsed) : 50;
            usleep((useconds_t)slice * 1000u);
        }
        return workerActive();
    };

    if (dllName.length() >= 4 &&
        dllName.substr(dllName.length() - 4) == (const char *) OBFUSCATE(".dll")) {
        dllName = dllName.substr(0, dllName.length() - 4);
    }

    PatchGatherConfigNative();

    while (workerActive()) {
        if (g_toolRestoring.load()) {
            usleep(300 * 1000u);
            continue;
        }

        std::vector<InstantFarmTarget> targets;

        RunOnUnityThreadSync([&]() {
            if (!workerActive()) return;
            _monoThreadAttach(_monoGetRootDomain());

            void *gameplay = _monoImageLoaded(dllName.c_str());
            void *data = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
            if (!gameplay || !data) return;

            void *collectClass = _monoClassFromName(gameplay, nameSpace.c_str(), className.c_str());
            if (!collectClass) return;

            void *worldClass = _monoClassFromName(data, OBFUSCATE("XDTGame.GAS.Client"), OBFUSCATE("EntityWorld"));
            if (!worldClass) worldClass = _monoClassFromName(data, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("EntityWorld"));
            if (!worldClass) worldClass = _monoClassFromName(gameplay, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("EntityWorld"));
            if (!collectClass || !worldClass) return;

            void *collectType = _monoClassGetType(collectClass);
            void *openMethod = FindMethodInHierarchy(worldClass, OBFUSCATE("GetComponentSystem"), 0);
            if (!collectType || !openMethod) return;

            void *typeArgs[] = { collectType };
            void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
            MonoGenericContext context = { nullptr, methodInst };
            void *closedMethod = _mono_class_inflate_generic_method(openMethod, &context);

            void *exception = nullptr;
            void *system = _monoRuntimeInvoke(closedMethod, nullptr, nullptr, &exception);
            if (!system || exception) return;

            void *componentsField = _monoClassGetFieldFromName(_monoObjectGetClass(system), OBFUSCATE("_components"));
            void *dictionary = nullptr;
            if (componentsField) _monoFieldGetValue(system, componentsField, &dictionary);
            if (!dictionary) return;

            void *entriesField = _monoClassGetFieldFromName(_monoObjectGetClass(dictionary), OBFUSCATE("entries"));
            if (!entriesField) entriesField = _monoClassGetFieldFromName(_monoObjectGetClass(dictionary), OBFUSCATE("_entries"));
            if (!entriesField) return;

            void *entries = nullptr;
            if (entriesField) _monoFieldGetValue(dictionary, entriesField, &entries);
            if (!entries) return;

            void *getType = FindMethodInHierarchy(collectClass, OBFUSCATE("get_resType"), 0);
            void *getCold = FindMethodInHierarchy(collectClass, OBFUSCATE("get_inCold"), 0);
            void *getEntity = FindMethodInHierarchy(collectClass, OBFUSCATE("get_entity"), 0);

            void *entityClass = _monoClassFromName(data, OBFUSCATE("XDTGame.GAS.Client"), OBFUSCATE("Entity"));
            if (!entityClass) entityClass = _monoClassFromName(data, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("Entity"));
            if (!entityClass) entityClass = _monoClassFromName(gameplay, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("Entity"));

            void *getPosition = entityClass ? FindMethodInHierarchy(entityClass, OBFUSCATE("get_position"), 0) : nullptr;
            void *getNetId = entityClass ? FindMethodInHierarchy(entityClass, OBFUSCATE("get_netId"), 0) : nullptr;
            if (!getNetId && entityClass) getNetId = FindMethodInHierarchy(entityClass, OBFUSCATE("GetNetId"), 0);

            if (!getType || !getCold || !getEntity || !getPosition) return;

            const uintptr_t address = (uintptr_t)entries;
            const int count = *(int*)(address + 0x18);
            const uintptr_t first = address + 0x20;
            std::unordered_set<uint32_t> uniqueIds;

            for (int i = 0; i < count; ++i) {
                const uintptr_t entry = first + i * 24;
                if (*(int*)entry < 0) continue;
                const uint32_t dictionaryId = *(uint32_t*)(entry + 8);
                void *collect = *(void**)(entry + 16);
                if (!dictionaryId || !collect) continue;

                void *typeObject = _monoRuntimeInvoke(getType, collect, nullptr, nullptr);
                if (!typeObject) continue;
                const int type = *(int*)_monoObjectUnbox(typeObject);
                if (!selected(type)) continue;

                void *coldObject = _monoRuntimeInvoke(getCold, collect, nullptr, nullptr);
                if (!coldObject || *(uint8_t*)_monoObjectUnbox(coldObject)) continue;

                void *entity = _monoRuntimeInvoke(getEntity, collect, nullptr, nullptr);
                if (!entity) continue;

                void *positionObject = _monoRuntimeInvoke(getPosition, entity, nullptr, nullptr);
                if (!positionObject) continue;

                uint32_t netId = 0;
                if (getNetId) {
                    void *netException = nullptr;
                    void *netObject = _monoRuntimeInvoke(getNetId, entity, nullptr, &netException);
                    if (netObject && !netException)
                        netId = *(uint32_t*)_monoObjectUnbox(netObject);
                }
                if (!netId) netId = InstantFarmGetStrictNetId(entity);
                if (!netId) netId = dictionaryId;
                if (!uniqueIds.insert(netId).second) continue;

                InstantFarmTarget target{};
                target.entityId = netId;
                target.dictionaryKey = dictionaryId;
                target.resType = type;
                memcpy(&target.position, _monoObjectUnbox(positionObject), sizeof(Vector3));
                targets.push_back(target);
            }
        });

        if (targets.empty()) {
            wait(2000);
            continue;
        }

        for (const auto &target : targets) {
            if (!workerActive()) break;
            while (workerActive() && g_toolRestoring.load())
                usleep(300 * 1000u);
            if (!workerActive() || !selected(target.resType)) continue;

            if (!InstantFarmMoveToSearchPoint(dllName, target.position)) continue;
            if (!wait(800)) break;

            for (int hit = 0; hit < 3 && workerActive(); ++hit) {
                RunOnUnityThreadSync([&]() {
                    if (!workerActive() || g_toolRestoring.load()) return;
                    _monoThreadAttach(_monoGetRootDomain());

                    void *protocolImage = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
                    if (!protocolImage) protocolImage = _monoImageLoaded(OBFUSCATE("Assembly-CSharp"));
                    if (!protocolImage) return;

                    void *protocolClass = _monoClassFromName(
                        protocolImage, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Resource"),
                        OBFUSCATE("ResourceProtocolManager"));
                    if (!protocolClass) return;

                    uint32_t netId = target.entityId;
                    uint8_t combo = 0;
                    void *invokeException = nullptr;
                    const char *methodName = target.resType == 1 ? "SendAttackTreeCommand" :
                        ((target.resType == 2 || target.resType == 9) ? "SendHitStoneCommand" :
                         "SendPickBushCommand");
                    const int parameterCount = (target.resType == 3 || target.resType == 10) ? 1 : 2;
                    void *method = FindMethodInHierarchy(protocolClass, methodName, parameterCount);
                    if (!method) return;

                    if (parameterCount == 1) {
                        void *args[] = { &netId };
                        _monoRuntimeInvoke(method, nullptr, args, &invokeException);
                    } else {
                        void *args[] = { &netId, &combo };
                        _monoRuntimeInvoke(method, nullptr, args, &invokeException);
                    }
                });
                if (!wait(200)) break;
            }
            if (!wait(500)) break;
        }
        wait(2000);
    }
}

void DisableTestIndexNowNative() {
    // Hanya untuk callback managed yang sudah berjalan pada Unity thread.
    if (!_monoGetRootDomain || !_monoThreadAttach || !_monoImageLoaded ||
        !_monoClassFromName || !_monoStringNew || !_monoRuntimeInvoke) return;
    void *domain = _monoGetRootDomain();
    if (!domain) return;
    _monoThreadAttach(domain);

    void *imgUnityEngine = _monoImageLoaded((const char *) OBFUSCATE("EngineWrapper"));
    if (!imgUnityEngine)
        imgUnityEngine = _monoImageLoaded((const char *) OBFUSCATE("UnityEngine"));
    if (!imgUnityEngine) return;

    void *klassGameObject = _monoClassFromName(imgUnityEngine,
                                               (const char *) OBFUSCATE("UnityEngine"),
                                               (const char *) OBFUSCATE("GameObject"));
    if (!klassGameObject) return;
    void *m_Find = FindMethodInHierarchy(klassGameObject, (const char *) OBFUSCATE("Find"), 1);
    if (!m_Find) return;

    void *monoPathStr = _monoStringNew(domain, (const char *) OBFUSCATE(
            "GameApp/startup_root(Clone)/XDUIRoot/TEST_INDEX"));
    void *argsFind[1] = {monoPathStr};
    void *exc = nullptr;
    void *gameObjectInst = _monoRuntimeInvoke(m_Find, nullptr, argsFind, &exc);
    if (!gameObjectInst || exc) return;

    void *m_SetActive = FindMethodInHierarchy(klassGameObject,
                                              (const char *) OBFUSCATE("SetActive"), 1);
    if (!m_SetActive) return;
    bool isActive = false;
    void *argsActive[1] = {&isActive};
    exc = nullptr;
    _monoRuntimeInvoke(m_SetActive, gameObjectInst, argsActive, &exc);
}

void AutoDisableTestIndexNative() {
    RunOnUnityThreadAsync([]() { DisableTestIndexNowNative(); });
}

void ExecuteNpcCosplayNative(int npcChoice, std::string dllName) {
    if (dllName.length() >= 4 &&
        dllName.substr(dllName.length() - 4) == (const char *) OBFUSCATE(".dll")) {
        dllName = dllName.substr(0, dllName.length() - 4);
    }
    RunOnUnityThreadAsync([npcChoice, dllName]() {
        _monoThreadAttach(_monoGetRootDomain());

        // Siapkan Lemari Pakaian NPC
        std::vector<int> outfit;
        if (npcChoice == 1)
            outfit = {10111000, 12018041, 11006020, 18002090, 16007250, 15001251}; // Ka Ching
        else if (npcChoice == 2) outfit = {10120900, 10020060, 10010109}; // Bob
        else if (npcChoice == 3) outfit = {10110500, 18016000, 16027000}; // Annie
        else if (npcChoice == 4) outfit = {10112900, 18015000, 16013034}; // Bailey J
        else if (npcChoice == 5)
            outfit = {10110800, 18017000, 16031000, 12027000, 11122000}; // Mr Joan
        else if (npcChoice == 6)
            outfit = {10120510, 11003100, 18008000, 16013032, 12028000}; // Alberth
        else if (npcChoice == 7)
            outfit = {10120200, 10020020, 11028000, 13012010, 14010033, 16023001,
                      12011001}; // Andrew
        else if (npcChoice == 8) outfit = {10122099, 18013000, 16030000}; // Vanya
        else if (npcChoice == 9)
            outfit = {10120700, 11025000, 13031000, 14021000, 16013032}; // Potti
        else return;

        // 3. GUNAKAN DLL DARI BACKEND NODE.JS! (TIDAK ADA LAGI STRING HARDCODED DI C++)
        void *imgLevel = _monoImageLoaded(dllName.c_str());
        if (!imgLevel) return;

        // Dapatkan Entity Utama dari Local Player
        void *utilKlass = _monoClassFromName(imgLevel, OBFUSCATE(
                "XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
        void *getSelfEntity = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayerEntity"), 0);
        if (!getSelfEntity) return;

        void *exc = nullptr;
        void *playerEntityObj = _monoRuntimeInvoke(getSelfEntity, nullptr, nullptr, &exc);
        if (!playerEntityObj || exc) return;

        // Inflate Generic Method: GetComponent<AvatarRendererComponent>(bool)
        void *entityKlass = _monoObjectGetClass(playerEntityObj);
        void *avatarRendererKlass = _monoClassFromName(imgLevel,
                                                       OBFUSCATE("XDTLevelAndEntity.EntityView"),
                                                       OBFUSCATE("AvatarRendererComponent"));
        void *getComponentGeneric = FindMethodInHierarchy(entityKlass, OBFUSCATE("GetComponent"),
                                                          1);

        void *typeArgs[1] = {_monoClassGetType(avatarRendererKlass)};
        void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
        MonoGenericContext ctx = {nullptr, methodInst};
        void *closedGetComp = _mono_class_inflate_generic_method(getComponentGeneric, &ctx);

        bool logErr = false;
        void *compArgs[1] = {&logErr};
        void *avatarCompObj = _monoRuntimeInvoke(closedGetComp, playerEntityObj, compArgs, &exc);
        if (!avatarCompObj || exc) return;

        // Baca properti 'get_avatar'
        void *getAvatarMeth = FindMethodInHierarchy(avatarRendererKlass, OBFUSCATE("get_avatar"),
                                                    0);
        void *xdAvatarObj = _monoRuntimeInvoke(getAvatarMeth, avatarCompObj, nullptr, &exc);
        if (!xdAvatarObj || exc) return;

        void *xdAvatarKlass = _monoObjectGetClass(xdAvatarObj);

        // --- FASE 1: LUCUTI SEMUA PAKAIAN LAMA ---
        void *m_GetAllCloths = FindMethodInHierarchy(xdAvatarKlass, OBFUSCATE("GetAllCloths"), 0);
        void *m_UndressPart = FindMethodInHierarchy(xdAvatarKlass, OBFUSCATE("UndressPart"), 1);
        void *m_UndressAllPendant = FindMethodInHierarchy(xdAvatarKlass,
                                                          OBFUSCATE("UndressAllPendant"), 0);

        if (m_GetAllCloths && m_UndressPart) {
            void *listObj = _monoRuntimeInvoke(m_GetAllCloths, xdAvatarObj, nullptr, &exc);
            if (listObj && !exc) {
                void *listKlass = _monoObjectGetClass(listObj);
                void *f_items = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_items"));
                void *f_size = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_size"));

                int size = 0;
                _monoFieldGetValue(listObj, f_size, &size);
                void *itemsArr = nullptr;
                _monoFieldGetValue(listObj, f_items, &itemsArr);

                if (size > 0 && itemsArr) {
                    int headerSize = sizeof(void *) == 8 ? 0x20
                                                         : 0x10; // Deteksi Arsitektur Memory (64/32bit)
                    int *elements = (int *) ((uint8_t *) itemsArr + headerSize);

                    for (int i = 0; i < size; i++) {
                        int clothId = elements[i];
                        void *uArgs[1] = {&clothId};
                        _monoRuntimeInvoke(m_UndressPart, xdAvatarObj, uArgs, nullptr);
                    }
                }
            }
        }

        if (m_UndressAllPendant) {
            _monoRuntimeInvoke(m_UndressAllPendant, xdAvatarObj, nullptr, nullptr);
        }

        // --- FASE 2: PAKAIKAN KOSTUM NPC BARU ---
        void *m_DressPart = FindMethodInHierarchy(xdAvatarKlass, OBFUSCATE("DressPart"), 1);
        if (m_DressPart) {
            for (int clothId: outfit) {
                void *dArgs[1] = {&clothId};
                _monoRuntimeInvoke(m_DressPart, xdAvatarObj, dArgs, nullptr);
            }
        }
    });
}

void *ExtractColorLutTextureNative(void *drawingConfigInst) {
    if (!drawingConfigInst)
        return nullptr;

    void *drawingConfigKlass = _monoObjectGetClass(drawingConfigInst);
    void *f_colorLut = _monoClassGetFieldFromName(drawingConfigKlass, OBFUSCATE("ColorLut"));

    if (!f_colorLut) {
        LOGE(OBFUSCATE("[-] Field 'ColorLut' tidak ditemukan di DrawingConfig."));
        return nullptr;
    }

    void *colorLutTexObj = nullptr;
    _monoFieldGetValue(drawingConfigInst, f_colorLut, &colorLutTexObj);

    if (colorLutTexObj) {
        LOGI(OBFUSCATE("[+] Mantap! Pointer Texture2D ColorLut didapat: %p"), colorLutTexObj);
    } else {
        LOGI(OBFUSCATE("[-] Pointer ColorLut null. Mungkin belum di-load oleh game?"));
    }

    return colorLutTexObj;
}

extern "C" VIRBOX_TARGET_ATTR void Virbox_PatchAntiCheat() {
        _monoThreadAttach(_monoGetRootDomain());

        // 1. èŽ·å– ConfigManager å®žä¾‹
        void *configMgr = GetConfigManagerInstance();
        if (!configMgr) return;

        void *mgrKlass = _monoObjectGetClass(configMgr);

        void *getGameConfigMeth = nullptr;
        void *currentMgr = mgrKlass;
        while (currentMgr != nullptr) {
            getGameConfigMeth = _monoClassGetMethodFromName(currentMgr, (const char *) OBFUSCATE(
                    "get_GameConfig"), 0);
            if (getGameConfigMeth) break;
            currentMgr = _monoClassGetParent(currentMgr);
        }
        if (!getGameConfigMeth) return;

        void *exc = nullptr;
        void *gameConfigInst = _monoRuntimeInvoke(getGameConfigMeth, configMgr, nullptr, &exc);
        if (!gameConfigInst || exc) return;

        void *gameConfigKlass = _monoObjectGetClass(gameConfigInst);

        // 2. æŸ¥æ‰¾ CollectAntiCheating å­—æ®µ
        void *f_antiCheat = nullptr;
        void *currentCfg = gameConfigKlass;
        while (currentCfg != nullptr) {
            f_antiCheat = _monoClassGetFieldFromName(currentCfg, (const char *) OBFUSCATE(
                    "CollectAntiCheating"));
            if (f_antiCheat) break;
            currentCfg = _monoClassGetParent(currentCfg);
        }
        if (!f_antiCheat) return;

        // 3. èŽ·å– CollectAntiCheating çš„å®žä¾‹
        void *antiCheatInst = nullptr;
        _monoFieldGetValue(gameConfigInst, f_antiCheat, &antiCheatInst);
        if (!antiCheatInst) return;

        void *antiCheatKlass = _monoObjectGetClass(antiCheatInst);

        // =========================================================
        // 4. è¾…åŠ©å‡½æ•°ï¼šæ ¹æ®å­—æ®µåç§°å†™å…¥ Float æˆ– Bool
        // =========================================================
        auto SetFloatField = [&](const char *fieldName, float value) {
            void *field = nullptr;
            void *currentKlass = antiCheatKlass;
            while (currentKlass != nullptr) {
                field = _monoClassGetFieldFromName(currentKlass, fieldName);
                if (field) break;
                currentKlass = _monoClassGetParent(currentKlass);
            }
            if (field) {
                int offset = _monoFieldGetOffset(field);
                *(float *) ((uint8_t *) antiCheatInst + offset) = value;
            }
        };

        auto SetBoolField = [&](const char *fieldName, bool value) {
            void *field = nullptr;
            void *currentKlass = antiCheatKlass;
            while (currentKlass != nullptr) {
                field = _monoClassGetFieldFromName(currentKlass, fieldName);
                if (field) break;
                currentKlass = _monoClassGetParent(currentKlass);
            }
            if (field) {
                int offset = _monoFieldGetOffset(field);
                *(bool *) ((uint8_t *) antiCheatInst + offset) = value;
            }
        };

        // =========================================================
        // 5. æš´åŠ›ç¯¡æ”¹æ‰€æœ‰åä½œå¼Šæ ¡éªŒå­—æ®µ (Sapu Bersih Semua Field!)
        // =========================================================

        // é‡‡é›†è·ç¦»æ”¹æˆå…¨å›¾èŒƒå›´
        SetFloatField((const char *) OBFUSCATE("Distance"), 9999.0f);

        // æ— CDè·ç¦»æ”¹æˆå…¨å›¾èŒƒå›´
        SetFloatField((const char *) OBFUSCATE("CloseDist"), 9999.0f);

        // ç¡®ä¿â€œè·ç¦»è¿‘æ—¶æ— CDâ€è¿™ä¸ªå¼€å…³æ˜¯å¼€ç€çš„
        SetBoolField((const char *) OBFUSCATE("IsNoCdInCloseDist"), true);

        // å°†æœ€å¤§åˆæ³•ç§»é€Ÿä¸Šé™æ”¹åˆ°æžå¤§ï¼Œé˜²æ­¢çž¬ç§»/åŠ é€Ÿè¢«æ‹‰å›ž
        SetFloatField((const char *) OBFUSCATE("MaxSpeed"), 9999.0f);

        // é€Ÿåº¦å®¹å·®ä¸Žå†—ä½™å…¨éƒ¨å¼€åˆ°æœ€å¤§
        SetFloatField((const char *) OBFUSCATE("SpeedTolerance"), 9999.0f);
        SetFloatField((const char *) OBFUSCATE("Slack"), 9999.0f);

        // (safety lock) set intercept mode to warn-only
        SetBoolField((const char *) OBFUSCATE("WarnOnly"), true);
        // Contoh pemanggilan
        PatchMethodReturnFalse(
                OBFUSCATE("XDTLevelAndEntity"),
                OBFUSCATE("XDTLevelAndEntity.BaseSystem.InputCheatManager"),
                OBFUSCATE("InputCheatManager"),
                OBFUSCATE("get_Enable"),
                0
        );
        PatchMethodReturnFalse(
                (const char *) OBFUSCATE("XDKWPerf"),
                (const char *) OBFUSCATE("XDKWPerf"),
                (const char *) OBFUSCATE("XDKWPerfManager"),
                (const char *) OBFUSCATE("CheckCondition"),
                0
        );

        // Memaksa InitPlayer selalu false agar data UID Anda tidak pernah dikirim
        PatchMethodReturnFalse(
                (const char *) OBFUSCATE("XDKWPerf"),
                (const char *) OBFUSCATE("XDKWPerf"),
                (const char *) OBFUSCATE("XDKWPerfManager"),
                (const char *) OBFUSCATE("InitPlayer"),
                1
        );

        // Memaksa KwBegin selalu false untuk menghentikan profiler
        PatchMethodReturnFalse(
                (const char *) OBFUSCATE("XDKWPerf"),
                (const char *) OBFUSCATE("XDKWPerf"),
                (const char *) OBFUSCATE("XDKWPerfManager"),
                (const char *) OBFUSCATE("KwBegin"),
                1
        );
        ExecuteDynamic(
                (const char *) OBFUSCATE("EngineWrapper"),
                "",
                (const char *) OBFUSCATE("ThemisSDKManager"),
                (const char *) OBFUSCATE("SetSwitch"),
                (const char *) OBFUSCATE(
                        "false"),                 // Kirim parameter angka 999999 dalam bentuk string
                true                      // isStatic = true
        );
        LOGI((const char *) OBFUSCATE("[+] Bypass Anti-Cheat å®Œç¾ŽæˆåŠŸï¼æ‰€æœ‰å‚æ•°å·²è¢«å¤¸å¤§ã€‚"));
}

void PatchAntiCheatNative() {
    RunOnUnityThreadAsync([]() { Virbox_PatchAntiCheat(); });
}

void ExecuteVisualESPScanner() {
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());

        if (!g_radarEnabled) {
            std::lock_guard<std::mutex> lock(g_radarMutex);
            g_radarList.clear();
            g_entityTypeCache.clear();
            return;
        }

        // --- INIT CACHE (1x) ---
        if (!g_espCache.isInitialized) {
            void *imgData  = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
            void *imgLevel = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
            void *imgBird  = _monoImageLoaded((const char *) OBFUSCATE("Assembly-CSharp"));
            if (!imgData || !imgLevel) return;

            g_espCache.dataCenterKlass = _monoClassFromName(imgData,
                                                            (const char *) OBFUSCATE("XDTDataAndProtocol.ComponentsData"), (const char *) OBFUSCATE("DataCenter"));
            g_espCache.m_getCurrentData = FindMethodInHierarchy(g_espCache.dataCenterKlass,
                                                                (const char *) OBFUSCATE("get_CurrentData"), 0);

            void *entitiesKlass = _monoClassFromName(imgLevel,
                                                     (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), (const char *) OBFUSCATE("Entities"));
            g_espCache.m_GetAnyEntity = FindMethodInHierarchy(entitiesKlass, (const char *) OBFUSCATE("GetAnyEntity"), 1);

            void *entityKlass = _monoClassFromName(imgData,
                                                   (const char *) OBFUSCATE("XDTLevelAndEntity.Core.World"), (const char *) OBFUSCATE("Entity"));
            g_espCache.m_getPos   = FindMethodInHierarchy(entityKlass, (const char *) OBFUSCATE("get_position"), 0);
            g_espCache.m_ToString = FindMethodInHierarchy(entityKlass, (const char *) OBFUSCATE("ToString"), 0);
            void *m_GetComponent  = FindMethodInHierarchy(entityKlass, (const char *) OBFUSCATE("GetComponent"), 1);

            if (m_GetComponent) {
                g_espCache.m_GetPlayer = InflateGetComponent(imgLevel,
                                                             (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"),
                                                             (const char *) OBFUSCATE("BasePlayerComponent"), m_GetComponent);
                g_espCache.m_GetPlayerMorph = InflateGetComponent(imgLevel,
                                                                  (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.HideAndSeek"),
                                                                  (const char *) OBFUSCATE("PlayerMorphComponent"), m_GetComponent);
                g_espCache.m_GetPlayerHider = InflateGetComponent(imgLevel,
                                                                  (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.HideAndSeek"),
                                                                  (const char *) OBFUSCATE("ClientHideAndSeekHiderPlacedComponent"), m_GetComponent);
                // BaseActorComponent terlalu umum dan membuat banyak entity biasa
                // salah terdeteksi sebagai NPC. Gunakan komponen NPC spesifik.
                g_espCache.m_GetNpc = InflateGetComponent(imgLevel,
                                                          (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Npc"),
                                                          (const char *) OBFUSCATE("NpcComponent"), m_GetComponent);
                g_espCache.m_GetFish = InflateGetComponent(imgLevel,
                                                           (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Fish"),
                                                           (const char *) OBFUSCATE("FishComponent"), m_GetComponent);
                g_espCache.m_GetInsect = InflateGetComponent(imgLevel,
                                                             (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Insect"),
                                                             (const char *) OBFUSCATE("InsectComponent"), m_GetComponent);
                g_espCache.m_GetBird = InflateGetComponent(imgBird ? imgBird : imgLevel,
                                                           (const char *) OBFUSCATE("ScriptsRefactory.LevelAndEntity.Entity.Components"),
                                                           (const char *) OBFUSCATE("BirdComponent"), m_GetComponent);

                // Resource ESP: gunakan component gather yang sama dengan scanner
                // resource game, bukan nama generik Entity.ToString().
                g_espCache.m_GetRes[0] = InflateGetComponent(imgLevel,
                        (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Gather"),
                        (const char *) OBFUSCATE("CollectableMeteoriteLogicComponent"), m_GetComponent);
                g_espCache.m_GetRes[1] = InflateGetComponent(imgLevel,
                        (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Gather"),
                        (const char *) OBFUSCATE("CollectableAdvancedStoneComponent"), m_GetComponent);
                g_espCache.m_GetRes[2] = InflateGetComponent(imgLevel,
                        (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Gather"),
                        (const char *) OBFUSCATE("CollectableAdvancedTreeComponent"), m_GetComponent);
                g_espCache.m_GetRes[3] = InflateGetComponent(imgLevel,
                        (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Gather"),
                        (const char *) OBFUSCATE("CollectableBushComponent"), m_GetComponent);
                g_espCache.m_GetRes[4] = InflateGetComponent(imgLevel,
                        (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Gather"),
                        (const char *) OBFUSCATE("CollectableObjectComponent"), m_GetComponent);
                g_espCache.m_GetRes[5] = InflateGetComponent(imgLevel,
                        (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Gather"),
                        (const char *) OBFUSCATE("DynamicCollectableComponent"), m_GetComponent);
            }
            g_espCache.isInitialized = true;
        }

        if (!g_espCache.m_getCurrentData || !g_espCache.m_GetAnyEntity) return;

        void *exc = nullptr;
        void *currentDataObj = _monoRuntimeInvoke(g_espCache.m_getCurrentData, nullptr, nullptr, &exc);
        if (!currentDataObj || exc) {
            std::lock_guard<std::mutex> lock(g_radarMutex);
            g_radarList.clear();
            g_entityTypeCache.clear();
            return;
        }

        void *f_netMap = FindFieldInHierarchy(_monoObjectGetClass(currentDataObj), (const char *) OBFUSCATE("_netEntitiesMap"));
        if (!f_netMap) return;
        void *dictObj = nullptr;
        _monoFieldGetValue(currentDataObj, f_netMap, &dictObj);
        if (!dictObj) return;

        void *f_entries = FindFieldInHierarchy(_monoObjectGetClass(dictObj), (const char *) OBFUSCATE("entries"));
        if (!f_entries) f_entries = FindFieldInHierarchy(_monoObjectGetClass(dictObj), (const char *) OBFUSCATE("_entries"));
        if (!f_entries) return;
        void *entriesArr = nullptr;
        _monoFieldGetValue(dictObj, f_entries, &entriesArr);
        if (!entriesArr) return;

        int length = *(int *) ((uint8_t *) entriesArr + OFF_ARR_COUNT);
        if (length <= 0 || length > 50000) return;
        uint8_t *arrayDataStart = (uint8_t *) entriesArr + OFF_ARR_DATA;

        std::vector<RadarEntity> tempRadarList;
        bool paramTrue = true;                 // GetComponent<T>(bool) â€” sama seperti PC
        void *compArgs[1] = {&paramTrue};
        // GetComponent generik pada ratusan entity dalam satu Unity frame dapat
        // menahan main thread dan membuat frame ImGui tidak sempat dipresentasikan.
        // Kenali entity baru secara bertahap; cache lama tetap dirender normal.
        int newClassificationBudget = 36;
        static int rotatingScanStart = 0;
        if (rotatingScanStart < 0 || rotatingScanStart >= length)
            rotatingScanStart = 0;

        for (int scanOffset = 0; scanOffset < length; ++scanOffset) {
            const int i = (rotatingScanStart + scanOffset) % length;
            uint8_t *entryAddr = arrayDataStart + (i * SZ_DICT_ENTRY);
            if (*(int *) entryAddr < 0) continue;
            uint32_t targetId = *(uint32_t *) (entryAddr + OFF_ENTRY_KEY);
            if (targetId == 0) continue;

            int entityType = -1;
            std::string entityName = "";

            // 1) CEK CACHE
            auto it = g_entityTypeCache.find(targetId);
            if (it != g_entityTypeCache.end()) {
                entityType = it->second.first;
                entityName = it->second.second;
            } else {
                // 2) PINDAI 1x untuk objek baru, dengan budget per siklus agar
                // aktivasi ESP tidak membekukan Unity/main render thread.
                if (newClassificationBudget <= 0) continue;
                --newClassificationBudget;
                void *cArgs[1] = {&targetId};
                exc = nullptr;
                void *tempEntity = _monoRuntimeInvoke(g_espCache.m_GetAnyEntity, nullptr, cArgs, &exc);
                if (tempEntity && !exc) {
                    bool isPlayer = false;
                    exc = nullptr;
                    if (g_espCache.m_GetPlayer && _monoRuntimeInvoke(g_espCache.m_GetPlayer, tempEntity, compArgs, &exc)) isPlayer = true;
                    exc = nullptr;
                    if (!isPlayer && g_espCache.m_GetPlayerMorph && _monoRuntimeInvoke(g_espCache.m_GetPlayerMorph, tempEntity, compArgs, &exc)) isPlayer = true;
                    exc = nullptr;
                    if (!isPlayer && g_espCache.m_GetPlayerHider && _monoRuntimeInvoke(g_espCache.m_GetPlayerHider, tempEntity, compArgs, &exc)) isPlayer = true;

                    if (isPlayer) { entityType = 0; entityName = (const char *) OBFUSCATE("Player"); }

                    exc = nullptr;
                    if (entityType == -1 && g_espCache.m_GetNpc && _monoRuntimeInvoke(g_espCache.m_GetNpc, tempEntity, compArgs, &exc)) { entityType = 4; entityName = (const char *) OBFUSCATE("NPC"); }
                    exc = nullptr;
                    if (entityType == -1 && g_espCache.m_GetBird && _monoRuntimeInvoke(g_espCache.m_GetBird, tempEntity, compArgs, &exc)) { entityType = 2; entityName = (const char *) OBFUSCATE("Bird"); }
                    exc = nullptr;
                    if (entityType == -1 && g_espCache.m_GetFish && _monoRuntimeInvoke(g_espCache.m_GetFish, tempEntity, compArgs, &exc)) { entityType = 3; entityName = (const char *) OBFUSCATE("Fish"); }
                    exc = nullptr;
                    if (entityType == -1 && g_espCache.m_GetInsect && _monoRuntimeInvoke(g_espCache.m_GetInsect, tempEntity, compArgs, &exc)) { entityType = 5; entityName = (const char *) OBFUSCATE("Insect"); }

                    if (entityType == -1 && g_radarShowResources) {
                        for (int r = 0; r < 6; ++r) {
                            if (!g_espCache.m_GetRes[r]) continue;
                            exc = nullptr;
                            void *resourceComp = _monoRuntimeInvoke(
                                    g_espCache.m_GetRes[r], tempEntity, compArgs, &exc);
                            if (resourceComp && !exc) {
                                entityType = 1;
                                entityName = (const char *) OBFUSCATE("Resource");
                                break;
                            }
                        }
                    }

                    // Jangan menimpa kategori dengan Entity.ToString(). Pada build
                    // game tertentu hasilnya hanya "Entity Info", bukan nama pemain
                    // atau jenis entity. Label kategori dibuat konsisten dengan PC.
                    // Jangan cache entity yang belum berhasil dikenali. Komponen dapat
                    // muncul beberapa frame setelah entity dibuat.
                    if (entityType != -1)
                        g_entityTypeCache[targetId] = {entityType, entityName};
                }
            }

            // 3) FILTER MENU
            bool allowShow = false;
            if      (entityType == 0 && g_radarShowPlayers)  allowShow = true;
            else if (entityType == 4 && g_radarShowNpc)      allowShow = true;
            else if (entityType == 2 && g_radarShowBirds)    allowShow = true;
            else if (entityType == 3 && g_radarShowFish)     allowShow = true;
            else if (entityType == 5 && g_radarShowInsects)  allowShow = true;
            else if (entityType == 1 && g_radarShowResources) allowShow = true;

            // 4) AMBIL POSISI LIVE
            if (allowShow) {
                void *cArgs[1] = {&targetId};
                exc = nullptr;
                void *liveEntity = _monoRuntimeInvoke(g_espCache.m_GetAnyEntity, nullptr, cArgs, &exc);
                if (liveEntity && !exc) {
                    exc = nullptr;
                    void *posObj = _monoRuntimeInvoke(g_espCache.m_getPos, liveEntity, nullptr, &exc);
                    if (posObj && !exc) {
                        Vector3 pos = *(Vector3 *) _monoObjectUnbox(posObj);
                        tempRadarList.push_back({targetId, entityName, entityType, pos, {0, 0, 0}, false});
                    }
                }
            }
        }

        rotatingScanStart = (rotatingScanStart + 36) % length;

        // Buang cache NetID yang sudah despawn agar NetID hasil reuse tidak memakai
        // tipe/nama entity lama.
        std::unordered_set<uint32_t> activeIds;
        activeIds.reserve((size_t)length);
        for (int i = 0; i < length; ++i) {
            uint8_t *entryAddr = arrayDataStart + (i * SZ_DICT_ENTRY);
            if (*(int *)entryAddr < 0) continue;
            uint32_t id = *(uint32_t *)(entryAddr + OFF_ENTRY_KEY);
            if (id) activeIds.insert(id);
        }
        for (auto it = g_entityTypeCache.begin(); it != g_entityTypeCache.end();) {
            if (activeIds.find(it->first) == activeIds.end()) it = g_entityTypeCache.erase(it);
            else ++it;
        }

        std::lock_guard<std::mutex> lock(g_radarMutex);
        g_radarList = std::move(tempRadarList);
    });
}

void RadarLoopThread() {
    const uint64_t generation = g_radarGeneration.load(std::memory_order_acquire);
    RunOnUnityThreadSync([generation]() {
        if (g_radarGeneration.load(std::memory_order_acquire) != generation) return;
        g_entityTypeCache.clear();
        std::lock_guard<std::mutex> lock(g_radarMutex);
        g_radarList.clear();
    });
    while (g_radarEnabled &&
           g_radarGeneration.load(std::memory_order_acquire) == generation) {
        ExecuteVisualESPScanner();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    if (g_radarGeneration.load(std::memory_order_acquire) == generation) {
        std::lock_guard<std::mutex> lock(g_radarMutex);
        g_radarList.clear();
        g_entityTypeCache.clear();
    }
}

void ExecuteFastHouseSetupNative() {
    RunOnUnityThreadAsync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        void *exc = nullptr;

        // =========================================================
        // TAHAP 1: RAKIT BLUEPRINT WARNA
        // =========================================================
        void *imgEcsClient = _monoImageLoaded((const char *) OBFUSCATE("EcsClient"));
        if (!imgEcsClient)
            imgEcsClient = _monoImageLoaded((const char *) OBFUSCATE("Assembly-CSharp"));

        void *tableDataKlass = _monoClassFromName(imgEcsClient, "",
                                                  (const char *) OBFUSCATE("TableData"));
        if (!tableDataKlass)
            tableDataKlass = _monoClassFromName(imgEcsClient, (const char *) OBFUSCATE(
                    "XD.GameGerm.ExpParser"), (const char *) OBFUSCATE("TableData"));
        if (!tableDataKlass) return;

        void *m_GetHouseScheme = FindMethodInHierarchy(tableDataKlass, (const char *) OBFUSCATE(
                "GetHomeLandInitHouseColorSchemes"), 2);
        void *m_GetHousePartScheme = FindMethodInHierarchy(tableDataKlass, (const char *) OBFUSCATE(
                "GetHomeLandInitHouseColorPartsSchemes"), 2);
        if (!m_GetHouseScheme || !m_GetHousePartScheme) return;

        int targetHouseId = 1;
        bool needExc = false;
        void *houseArgs[2] = {&targetHouseId, &needExc};

        void *houseSchemeObj = _monoRuntimeInvoke(m_GetHouseScheme, nullptr, houseArgs, &exc);
        if (!houseSchemeObj) return;

        int housePlanId = 0;
        void *f_housePlanId = FindFieldInHierarchy(_monoObjectGetClass(houseSchemeObj),
                                                   (const char *) OBFUSCATE("housePlanId"));
        if (f_housePlanId) _monoFieldGetValue(houseSchemeObj, f_housePlanId, &housePlanId);

        void *partsArrayObj = nullptr;
        void *f_parts = FindFieldInHierarchy(_monoObjectGetClass(houseSchemeObj),
                                             (const char *) OBFUSCATE("parts"));
        if (f_parts) _monoFieldGetValue(houseSchemeObj, f_parts, &partsArrayObj);
        if (!partsArrayObj) return;

        int partsCount = *(int *) ((uint8_t *) partsArrayObj + OFF_ARR_COUNT);
        int *partsArrayPtr = (int *) ((uint8_t *) partsArrayObj + OFF_ARR_DATA);

        void *imgUI = _monoImageLoaded((const char *) OBFUSCATE("XDTGameUI"));
        void *panelKlass = _monoClassFromName(imgUI, (const char *) OBFUSCATE("XDTGame.UI.Panel"),
                                              (const char *) OBFUSCATE("HomeDesignPanel"));
        if (!panelKlass) return;

        void *f_dyeParts = FindFieldInHierarchy(panelKlass, (const char *) OBFUSCATE("dyeParts"));
        if (!f_dyeParts) return;

        void *genericListKlass = _monoClassFromMonoType(_monoFieldGetType(f_dyeParts));
        void *listObj = _monoObjectNew(_monoGetRootDomain(), genericListKlass);
        void *m_ListCtor = FindMethodInHierarchy(genericListKlass,
                                                 (const char *) OBFUSCATE(".ctor"), 0);

        exc = nullptr;
        _monoRuntimeInvoke(m_ListCtor, listObj, nullptr, &exc);
        void *m_ListAdd = FindMethodInHierarchy(genericListKlass, (const char *) OBFUSCATE("Add"),
                                                1);

        struct DyePartStruct {
            int partSchemeId;
            int chosenColorIndex;
        };

        int chosenColorIndex = 0;
        for (int i = 0; i < partsCount; i++) {
            int partId = partsArrayPtr[i];
            void *partArgs[2] = {&targetHouseId, &partId};
            void *partSchemeObj = _monoRuntimeInvoke(m_GetHousePartScheme, nullptr, partArgs, &exc);

            if (partSchemeObj && !exc) {
                int partSchemeId = 0;
                void *f_id = FindFieldInHierarchy(_monoObjectGetClass(partSchemeObj),
                                                  (const char *) OBFUSCATE("id"));
                if (f_id) _monoFieldGetValue(partSchemeObj, f_id, &partSchemeId);

                DyePartStruct pStruct = {partSchemeId, chosenColorIndex};
                void *addArgs[1] = {&pStruct};
                _monoRuntimeInvoke(m_ListAdd, listObj, addArgs, &exc);
            }
        }

        // =========================================================
        // TAHAP 2: EKSEKUSI KOMBOS PROTOKOL HOMELAND
        // =========================================================
        void *imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
        if (!imgData) imgData = _monoImageLoaded((const char *) OBFUSCATE("Assembly-CSharp"));

        void *protocolKlass = _monoClassFromName(imgData, (const char *) OBFUSCATE(
                "XDTDataAndProtocol.ProtocolService.Homeland"), (const char *) OBFUSCATE(
                "HomelandProtocolManager"));
        if (!protocolKlass) {
            // iOS：建房协议在加密的热更 Mono 程序集里，桥接未就绪时这里恒 null。
            // 此时「即时金币农场」只剩开头的 SetGlobalTimeScale(10) 在生效，
            // 表现就是“变成了全局加速”。连续失败 5 次自动关开关并恢复 1.0 倍速。
            static std::atomic<int> fastHouseMisses{0};
            if (++fastHouseMisses >= 5) {
                g_fastHouseSetupEnabled = false;
                SetGlobalTimeScale(1.0f);
            }
            return;
        }

        // 1. SendChooseInitHouseData
        void *m_SendChoose = FindMethodInHierarchy(protocolKlass, (const char *) OBFUSCATE(
                "SendChooseInitHouseData"), 3);
        if (m_SendChoose) {
            int layoutId = 0;
            void *invokeArgs1[3] = {&housePlanId, listObj, &layoutId};
            exc = nullptr;
            _monoRuntimeInvoke(m_SendChoose, nullptr, invokeArgs1, &exc);
        }

        // 2. ModifyHouseName
        void *m_ModifyName = FindMethodInHierarchy(protocolKlass,
                                                   (const char *) OBFUSCATE("ModifyHouseName"), 2);
        if (m_ModifyName) {
            void *monoNameStr = _monoStringNew(_monoGetRootDomain(),
                                               (const char *) OBFUSCATE("Nyawit"));
            int modifyType = 0; // 0 = Init
            void *invokeArgs2[2] = {monoNameStr, &modifyType};
            exc = nullptr;
            _monoRuntimeInvoke(m_ModifyName, nullptr, invokeArgs2, &exc);
        }

        // Tambah counter eksekusi
        g_fastHouseSetupCount++;
    });
}

void FastHouseLoopThread() {
    while (g_fastHouseSetupEnabled) {
        ExecuteFastHouseSetupNative();
        // Delay 0.5 detik (Anda bisa memperkecil jadi 100ms jika server sanggup menampung spam-nya)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void *GetMainCameraNative() {
    if (!g_espCache.m_get_main) {
        void *img = _monoImageLoaded((const char *) OBFUSCATE("EngineWrapper"));
        if (!img) img = _monoImageLoaded((const char *) OBFUSCATE("UnityEngine"));
        if (!img) return nullptr;
        g_espCache.cameraKlass = _monoClassFromName(img, (const char *) OBFUSCATE("UnityEngine"),
                                                    (const char *) OBFUSCATE("Camera"));
        g_espCache.m_get_main = FindMethodInHierarchy(g_espCache.cameraKlass,
                                                      (const char *) OBFUSCATE("get_main"), 0);
        g_espCache.m_WorldToScreenPoint = FindMethodInHierarchy(g_espCache.cameraKlass,
                                                                (const char *) OBFUSCATE(
                                                                        "WorldToScreenPoint"), 1);
    }
    if (!g_espCache.m_get_main) return nullptr;
    void *exc = nullptr;
    return _monoRuntimeInvoke(g_espCache.m_get_main, nullptr, nullptr, &exc);
}

Vector3 WorldToScreenNative(void *camera, Vector3 worldPos) {
    if (!camera || !g_espCache.m_WorldToScreenPoint) return {0, 0, 0};
    void *args[1] = {&worldPos};
    void *exc = nullptr;
    void *res = _monoRuntimeInvoke(g_espCache.m_WorldToScreenPoint, camera, args, &exc);
    if (res && !exc) return *(Vector3 *) _monoObjectUnbox(res);
    return {0, 0, 0};
}

// =====================================================================
// AUTO RESTORE TOOL (port dari versi PC -> Android)
// =====================================================================
static int GetCurrentToolDurabilityNative() {
    void *img = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!img) { LOGE(OBFUSCATE("[RestoreTool] img XDTGameSystem null")); return -1; }

    void *toolSysKlass = _monoClassFromName(img, OBFUSCATE("XDTGameSystem.GameplaySystem.Tool"),
                                            OBFUSCATE("ToolSystem"));
    if (!toolSysKlass) { LOGE(OBFUSCATE("[RestoreTool] ToolSystem class null")); return -1; }

    void *m_getInst = FindMethodInHierarchy(toolSysKlass, OBFUSCATE("get_Instance"), 0);
    if (!m_getInst) m_getInst = FindMethodInHierarchy(toolSysKlass, OBFUSCATE("get_instance"), 0);
    if (!m_getInst) { LOGE(OBFUSCATE("[RestoreTool] get_Instance null")); return -1; }

    void *exc = nullptr;
    void *inst = _monoRuntimeInvoke(m_getInst, nullptr, nullptr, &exc);
    if (!inst || exc) { LOGE(OBFUSCATE("[RestoreTool] ToolSystem inst null/exc")); return -1; }

    void *m_getTool = FindMethodInHierarchy(toolSysKlass, OBFUSCATE("GetCurrentTool"), 0);
    if (!m_getTool) { LOGE(OBFUSCATE("[RestoreTool] GetCurrentTool null")); return -1; }

    void *toolObj = _monoRuntimeInvoke(m_getTool, inst, nullptr, &exc);
    if (!toolObj || exc) { LOGE(OBFUSCATE("[RestoreTool] toolObj null/exc (tidak pegang tool?)")); return -1; }

    void *toolKlass = _monoObjectGetClass(toolObj);
    const char *tkName = _monoClassGetName(toolKlass);

    void *f_dur = _monoClassGetFieldFromName(toolKlass, OBFUSCATE("durability"));
    if (!f_dur) f_dur = _monoClassGetFieldFromName(toolKlass, OBFUSCATE("Durability"));
    if (!f_dur) {
        // fallback property get_durability
        void *m = FindMethodInHierarchy(toolKlass, OBFUSCATE("get_durability"), 0);
        if (!m) m = FindMethodInHierarchy(toolKlass, OBFUSCATE("get_Durability"), 0);
        if (m) {
            void *r = _monoRuntimeInvoke(m, toolObj, nullptr, &exc);
            if (r && !exc) return *(int *) _monoObjectUnbox(r);
        }
        LOGE(OBFUSCATE("[RestoreTool] field 'durability' tidak ada di class '%s'"), tkName ? tkName : "?");
        return -1;
    }

    int dur = 0;
    _monoFieldGetValue(toolObj, f_dur, (void **) &dur);
    LOGI(OBFUSCATE("[RestoreTool] tool class=%s durability=%d"), tkName ? tkName : "?", dur);
    return dur;
}

void RefreshCurrentToolDurability() {
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        g_currentToolDurability = GetCurrentToolDurabilityNative();
    });
}

static uint32_t FindBackpackItemNetId(int targetStaticId) {
    void *sysImg = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!sysImg) return 0;
    void *sysKlass = _monoClassFromName(sysImg, OBFUSCATE("XDTGameSystem.GameplaySystem.BackPack"),
                                        OBFUSCATE("BackPackSystem"));
    if (!sysKlass) return 0;

    void *m_getInst = FindMethodInHierarchy(sysKlass, OBFUSCATE("get_Instance"), 0);
    void *sysInst = m_getInst ? _monoRuntimeInvoke(m_getInst, nullptr, nullptr, nullptr) : nullptr;
    if (!sysInst) return 0;

    void *m_GetAllItem = FindMethodInHierarchy(sysKlass, OBFUSCATE("GetAllItem"), 1);
    if (!m_GetAllItem) return 0;
    int storageType = 1;
    void *argsGetAll[1] = {&storageType};
    void *listObj = _monoRuntimeInvoke(m_GetAllItem, sysInst, argsGetAll, nullptr);
    if (!listObj) return 0;

    void *listKlass = _monoObjectGetClass(listObj);
    void *sizeField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_size"));
    void *itemsField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_items"));
    if (!sizeField || !itemsField) return 0;

    int size = 0;
    _monoFieldGetValue(listObj, sizeField, (void **) &size);
    void *itemsArray = nullptr;
    _monoFieldGetValue(listObj, itemsField, &itemsArray);
    if (!itemsArray || size == 0) return 0;

    void *bpItemKlass = _monoClassFromName(sysImg, OBFUSCATE("XDTGameSystem.UISystem.BackPack"),
                                           OBFUSCATE("BackpackItem"));
    uint32_t align = 0;
    int structSize = _monoClassValueSize(bpItemKlass, &align);
    uintptr_t arrayDataStart = (uintptr_t) itemsArray + OFF_ARR_DATA;

    for (int i = 0; i < size; i++) {
        uintptr_t structAddr = arrayDataStart + (i * structSize);
        uint32_t netId = *(uint32_t *) (structAddr + OFF_BP_NETID);
        int staticId = *(int *) (structAddr + OFF_BP_STATICID);
        if (staticId == targetStaticId && netId != 0) return netId;
    }
    return 0;
}

static int CountBackpackItemTotalByStaticId(int targetStaticId) {
    if (targetStaticId <= 0) return 0;
    _monoThreadAttach(_monoGetRootDomain());
    void *sysImg = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!sysImg) return 0;

    void *sysKlass = _monoClassFromName(sysImg, OBFUSCATE("XDTGameSystem.GameplaySystem.BackPack"),
                                        OBFUSCATE("BackPackSystem"));
    if (!sysKlass) return 0;

    void *m_getInst = FindMethodInHierarchy(sysKlass, OBFUSCATE("get_Instance"), 0);
    void *sysInst = m_getInst ? _monoRuntimeInvoke(m_getInst, nullptr, nullptr, nullptr) : nullptr;
    if (!sysInst) return 0;

    void *m_GetAllItem = FindMethodInHierarchy(sysKlass, OBFUSCATE("GetAllItem"), 1);
    if (!m_GetAllItem) return 0;

    int storageType = 1;
    void *argsGetAll[1] = {&storageType};
    void *listObj = _monoRuntimeInvoke(m_GetAllItem, sysInst, argsGetAll, nullptr);
    if (!listObj) return 0;

    void *listKlass = _monoObjectGetClass(listObj);
    void *sizeField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_size"));
    void *itemsField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_items"));
    if (!sizeField || !itemsField) return 0;

    int size = 0;
    _monoFieldGetValue(listObj, sizeField, (void **) &size);
    void *itemsArray = nullptr;
    _monoFieldGetValue(listObj, itemsField, &itemsArray);
    if (!itemsArray || size <= 0) return 0;

    void *bpItemKlass = _monoClassFromName(sysImg, OBFUSCATE("XDTGameSystem.UISystem.BackPack"),
                                           OBFUSCATE("BackpackItem"));
    if (!bpItemKlass) return 0;

    uint32_t align = 0;
    int structSize = _monoClassValueSize(bpItemKlass, &align);
    if (structSize <= 0) return 0;

    uintptr_t arrayDataStart = (uintptr_t) itemsArray + OFF_ARR_DATA;
    int totalCount = 0;

    for (int i = 0; i < size; i++) {
        uintptr_t structAddr = arrayDataStart + (i * structSize);
        uint32_t netId = *(uint32_t *) (structAddr + OFF_BP_NETID);
        int staticId = *(int *) (structAddr + OFF_BP_STATICID);
        int count = *(int *) (structAddr + OFF_BP_COUNT);
        if (staticId == targetStaticId && netId != 0 && count > 0) {
            totalCount += count;
        }
    }

    return totalCount;
}

static uint64_t LureNowMs() {
    return (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void *LoadMonoImageFlexible(const std::string &name) {
    if (name.empty()) return nullptr;
    void *image = _monoImageLoaded(name.c_str());
    if (image) return image;
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".dll") == 0)
        return _monoImageLoaded(name.substr(0, name.size() - 4).c_str());
    return _monoImageLoaded((name + ".dll").c_str());
}

static uint32_t FindBackpackItemNetIdFromPayload(
        int targetStaticId, int *countOut, const std::string &systemImageName,
        const std::string &backpackSystemNamespace, const std::string &backpackSystemClass,
        const std::string &getInstanceName, const std::string &getAllItemName,
        const std::string &listSizeFieldName, const std::string &listItemsFieldName,
        const std::string &backpackItemNamespace, const std::string &backpackItemClass) {
    if (countOut) *countOut = 0;
    if (targetStaticId <= 0 || systemImageName.empty() || backpackSystemNamespace.empty() ||
        backpackSystemClass.empty() || getInstanceName.empty() || getAllItemName.empty() ||
        listSizeFieldName.empty() || listItemsFieldName.empty() || backpackItemNamespace.empty() ||
        backpackItemClass.empty() || !_monoClassValueSize) return 0;

    void *sysImg = LoadMonoImageFlexible(systemImageName);
    if (!sysImg) return 0;
    void *sysKlass = _monoClassFromName(sysImg, backpackSystemNamespace.c_str(),
                                        backpackSystemClass.c_str());
    if (!sysKlass) return 0;
    void *getInstance = FindMethodInHierarchy(sysKlass, getInstanceName.c_str(), 0);
    void *instance = getInstance ? _monoRuntimeInvoke(getInstance, nullptr, nullptr, nullptr) : nullptr;
    if (!instance) return 0;
    void *getAllItem = FindMethodInHierarchy(sysKlass, getAllItemName.c_str(), 1);
    if (!getAllItem) return 0;

    int storageType = 1;
    void *args[] = {&storageType};
    void *list = _monoRuntimeInvoke(getAllItem, instance, args, nullptr);
    if (!list) return 0;
    void *listClass = _monoObjectGetClass(list);
    void *sizeField = _monoClassGetFieldFromName(listClass, listSizeFieldName.c_str());
    void *itemsField = _monoClassGetFieldFromName(listClass, listItemsFieldName.c_str());
    if (!sizeField || !itemsField) return 0;

    int size = 0;
    void *itemsArray = nullptr;
    _monoFieldGetValue(list, sizeField, (void **) &size);
    _monoFieldGetValue(list, itemsField, &itemsArray);
    if (!itemsArray || size <= 0 || size > 10000) return 0;

    void *itemClass = _monoClassFromName(sysImg, backpackItemNamespace.c_str(),
                                         backpackItemClass.c_str());
    if (!itemClass) return 0;
    uint32_t align = 0;
    const int structSize = _monoClassValueSize(itemClass, &align);
    if (structSize < 0x18 || structSize > 0x400) return 0;
    const uintptr_t arrayData = (uintptr_t) itemsArray + OFF_ARR_DATA;
    for (int i = 0; i < size; ++i) {
        const uintptr_t item = arrayData + (uintptr_t) i * (uintptr_t) structSize;
        const uint32_t netId = *(uint32_t *) (item + OFF_BP_NETID);
        const int staticId = *(int *) (item + OFF_BP_STATICID);
        if (staticId == targetStaticId && netId != 0) {
            if (countOut) *countOut = *(int *) (item + OFF_BP_COUNT);
            return netId;
        }
    }
    return 0;
}

void UseFishingLureBall() {
    if (g_lureBallBusy.exchange(true, std::memory_order_acq_rel)) return;
    const uint64_t now = LureNowMs();
    if (g_lureBallCooldownUntilMs.load(std::memory_order_acquire) > now) {
        g_lureBallBusy.store(false, std::memory_order_release);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_lureBallMutex);
        g_lureBallStatus = u8"正在加载打窝球数据…";
    }

    const std::string lureWire = WireForOp(OP_AUTO_LURE_BALL);
    if (lureWire.empty()) {
        std::lock_guard<std::mutex> lock(g_lureBallMutex);
        g_lureBallStatus = u8"缺少打窝功能映射，请重新登录";
        g_lureBallBusy.store(false, std::memory_order_release);
        return;
    }

    InvokeRequest req = FetchPayloadSync(lureWire);
    const char *payloadError = nullptr;
    if (!SesiAktif()) payloadError = "Lure Ball session expired";
    else if (req.op != OP_AUTO_LURE_BALL) payloadError = "Lure payload request rejected";
    else if (req.methodDesc != (const char *) OBFUSCATE("CUSTOM_AUTO_LURE_BALL"))
        payloadError = "Lure payload descriptor invalid";
    else if (req.payloadRaw.empty()) payloadError = "Lure payload metadata missing";
    if (payloadError) {
        std::lock_guard<std::mutex> lock(g_lureBallMutex);
        g_lureBallStatus = payloadError;
        g_lureBallBusy.store(false, std::memory_order_release);
        return;
    }

    RunOnUnityThreadAsync([req]() {
        auto finish = [](const std::string &status, bool cooldown = false, int count = 0) {
            {
                std::lock_guard<std::mutex> lock(g_lureBallMutex);
                g_lureBallStatus = status;
                g_lureBallCount = count;
            }
            if (cooldown) {
                const int seconds = std::max(10, std::min(120, g_lureBallCooldownSec));
                g_lureBallCooldownUntilMs.store(
                        LureNowMs() + (uint64_t) seconds * 1000ULL, std::memory_order_release);
            }
            g_lureBallBusy.store(false, std::memory_order_release);
        };
        auto pn = [&](const char *key) { return PayloadVM::MetadataValue(req.payloadRaw, key); };

        try {
            if (!SesiAktif() || req.generasiSesi != GenerasiSesi())
                return finish("Lure Ball session expired");
            void *domain = _monoGetRootDomain();
            if (!domain) return finish("Mono domain unavailable");
            _monoThreadAttach(domain);

            int staticId = 0;
            try { staticId = std::stoi(pn("lure_static_id")); }
            catch (...) { return finish("Invalid Lure Ball payload"); }

            const std::string imgSystem = pn("n_img_system");
            const std::string nsBackpackSystem = pn("n_ns_backpack_system");
            const std::string clsBackpackSystem = pn("n_cls_backpack_system");
            const std::string getInstance = pn("n_get_instance");
            const std::string getAllItem = pn("n_get_all_item");
            const std::string listSize = pn("n_list_size");
            const std::string listItems = pn("n_list_items");
            const std::string nsBackpackItem = pn("n_ns_backpack_item");
            const std::string clsBackpackItem = pn("n_cls_backpack_item");
            const std::string imgLevelName = pn("n_img_level");
            const std::string nsFish = pn("n_ns_fish");
            const std::string clsFish = pn("n_cls_fish_helper");
            const std::string canThrowName = pn("n_can_throw");
            const std::string useLureName = pn("n_use_lure");
            if (staticId <= 0 || req.dllName.empty() || req.nameSpace.empty() ||
                req.className.empty() || imgSystem.empty() || nsBackpackSystem.empty() ||
                clsBackpackSystem.empty() || getInstance.empty() || getAllItem.empty() ||
                listSize.empty() || listItems.empty() || nsBackpackItem.empty() ||
                clsBackpackItem.empty() || imgLevelName.empty() || nsFish.empty() ||
                clsFish.empty() || canThrowName.empty() || useLureName.empty())
                return finish("Incomplete Lure Ball payload");

            int itemCount = 0;
            const uint32_t itemNetId = FindBackpackItemNetIdFromPayload(
                    staticId, &itemCount, imgSystem, nsBackpackSystem, clsBackpackSystem,
                    getInstance, getAllItem, listSize, listItems, nsBackpackItem, clsBackpackItem);
            if (itemNetId == 0 || itemCount <= 0) return finish("Lure Ball not found");

            void *levelImage = LoadMonoImageFlexible(imgLevelName);
            void *fishClass = levelImage ? _monoClassFromName(
                    levelImage, nsFish.c_str(), clsFish.c_str()) : nullptr;
            void *canThrow = fishClass ? FindMethodInHierarchy(fishClass, canThrowName.c_str(), 1) : nullptr;
            if (!canThrow) return finish("Lure target check unavailable", false, itemCount);

            Vector3 target{};
            void *targetArgs[] = {&target};
            void *exc = nullptr;
            void *result = _monoRuntimeInvoke(canThrow, nullptr, targetArgs, &exc);
            if (exc || !result) return finish("Unable to validate lure target", false, itemCount);
            void *raw = _monoObjectUnbox(result);
            if (!raw || *(uint8_t *) raw == 0)
                return finish("Face a valid fishing area", false, itemCount);

            void *dataImage = LoadMonoImageFlexible(req.dllName);
            void *protocolClass = dataImage ? _monoClassFromName(
                    dataImage, req.nameSpace.c_str(), req.className.c_str()) : nullptr;
            void *useLure = protocolClass ? FindMethodInHierarchy(
                    protocolClass, useLureName.c_str(), 3) : nullptr;
            if (!useLure) return finish("Lure request unavailable", false, itemCount);

            uint32_t netIdArg = itemNetId;
            float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            void *useArgs[] = {&netIdArg, &target, rotation};
            exc = nullptr;
            _monoRuntimeInvoke(useLure, nullptr, useArgs, &exc);
            if (exc) return finish("Lure Ball request failed", false, itemCount);
            LOGI(OBFUSCATE("[LureBall] payload request netId=%u count=%d target=(%.2f,%.2f,%.2f)"),
                 itemNetId, itemCount, target.x, target.y, target.z);
            finish("Lure Ball request sent", true, itemCount);
        } catch (...) {
            finish("Lure Ball failed");
        }
    });
}

void AutoLureBallLoop(uint32_t generation) {
    LOGI(OBFUSCATE("[LureBall] auto loop started gen=%u"), generation);
    while (g_autoLureBallActive && SesiAktif() &&
           g_lureBallLoopGeneration.load(std::memory_order_acquire) == generation) {
        const uint64_t now = LureNowMs();
        const uint64_t until = g_lureBallCooldownUntilMs.load(std::memory_order_acquire);
        if (!g_lureBallBusy.load(std::memory_order_acquire) && now >= until) {
            UseFishingLureBall();
            usleep(3000000);
        } else {
            usleep(500000);
        }
    }
    if (!SesiAktif()) g_autoLureBallActive = false;
    LOGI(OBFUSCATE("[LureBall] auto loop stopped"));
}

struct BackpackStackInfoAndroid {
    uint32_t netId = 0;
    int count = 0;
};

static int ScanBackpackStacksByStaticIdFromPayload(
        int targetStaticId,
        std::vector<BackpackStackInfoAndroid> &outStacks,
        int *totalCountOut,
        const std::string &systemImageName,
        const std::string &backpackSystemNamespace,
        const std::string &backpackSystemClass,
        const std::string &getInstanceName,
        const std::string &getAllItemName,
        const std::string &listSizeFieldName,
        const std::string &listItemsFieldName,
        const std::string &backpackItemNamespace,
        const std::string &backpackItemClass) {
    outStacks.clear();
    if (totalCountOut) *totalCountOut = 0;
    if (targetStaticId <= 0 || systemImageName.empty() || backpackSystemNamespace.empty() ||
        backpackSystemClass.empty() || getInstanceName.empty() || getAllItemName.empty() ||
        listSizeFieldName.empty() || listItemsFieldName.empty() ||
        backpackItemNamespace.empty() || backpackItemClass.empty() || !_monoClassValueSize)
        return 0;

    void *sysImg = LoadMonoImageFlexible(systemImageName);
    if (!sysImg) return 0;
    void *sysKlass = _monoClassFromName(sysImg, backpackSystemNamespace.c_str(),
                                        backpackSystemClass.c_str());
    if (!sysKlass) return 0;
    void *getInstance = FindMethodInHierarchy(sysKlass, getInstanceName.c_str(), 0);
    void *instance = getInstance ? _monoRuntimeInvoke(getInstance, nullptr, nullptr, nullptr) : nullptr;
    if (!instance) return 0;
    void *getAllItem = FindMethodInHierarchy(sysKlass, getAllItemName.c_str(), 1);
    if (!getAllItem) return 0;

    int storageType = 1;
    void *args[] = {&storageType};
    void *list = _monoRuntimeInvoke(getAllItem, instance, args, nullptr);
    if (!list) return 0;

    void *listClass = _monoObjectGetClass(list);
    void *sizeField = _monoClassGetFieldFromName(listClass, listSizeFieldName.c_str());
    void *itemsField = _monoClassGetFieldFromName(listClass, listItemsFieldName.c_str());
    if (!sizeField || !itemsField) return 0;

    int size = 0;
    void *itemsArray = nullptr;
    _monoFieldGetValue(list, sizeField, (void **) &size);
    _monoFieldGetValue(list, itemsField, &itemsArray);
    if (!itemsArray || size <= 0 || size > 10000) return 0;

    void *itemClass = _monoClassFromName(sysImg, backpackItemNamespace.c_str(),
                                         backpackItemClass.c_str());
    if (!itemClass) return 0;
    uint32_t align = 0;
    const int structSize = _monoClassValueSize(itemClass, &align);
    if (structSize < 0x18 || structSize > 0x400) return 0;
    const uintptr_t arrayData = (uintptr_t) itemsArray + OFF_ARR_DATA;

    int totalCount = 0;
    for (int i = 0; i < size; ++i) {
        const uintptr_t item = arrayData + (uintptr_t) i * (uintptr_t) structSize;
        const uint32_t netId = *(uint32_t *) (item + OFF_BP_NETID);
        const int staticId = *(int *) (item + OFF_BP_STATICID);
        const int count = *(int *) (item + OFF_BP_COUNT);
        if (staticId == targetStaticId && netId != 0 && count > 0) {
            outStacks.push_back({netId, count});
            totalCount += count;
        }
    }
    if (totalCountOut) *totalCountOut = totalCount;
    return (int) outStacks.size();
}

static bool TryGetFishBaitThrowTargetFromPayload(
        Vector3 &outTarget,
        float throwDistance,
        const std::string &levelImageName,
        const std::string &nsEntityUtil,
        const std::string &clsEntityUtil,
        const std::string &getSelfPlayerEntityName,
        const std::string &getSelfPlayerName,
        const std::string &getEntityName,
        const std::string &getPositionName,
        const std::string &getForwardName) {
    void *levelImg = LoadMonoImageFlexible(levelImageName);
    if (!levelImg) return false;
    void *utilKlass = _monoClassFromName(levelImg, nsEntityUtil.c_str(), clsEntityUtil.c_str());
    if (!utilKlass) return false;

    void *entityObj = nullptr;
    if (!getSelfPlayerEntityName.empty()) {
        void *mGetSelfEntity = FindMethodInHierarchy(utilKlass, getSelfPlayerEntityName.c_str(), 0);
        if (mGetSelfEntity) {
            void *exc = nullptr;
            entityObj = _monoRuntimeInvoke(mGetSelfEntity, nullptr, nullptr, &exc);
            if (exc) entityObj = nullptr;
        }
    }
    if (!entityObj && !getSelfPlayerName.empty() && !getEntityName.empty()) {
        void *mGetSelfPlayer = FindMethodInHierarchy(utilKlass, getSelfPlayerName.c_str(), 0);
        if (mGetSelfPlayer) {
            void *exc = nullptr;
            void *playerObj = _monoRuntimeInvoke(mGetSelfPlayer, nullptr, nullptr, &exc);
            if (!exc && playerObj) {
                void *playerKlass = _monoObjectGetClass(playerObj);
                void *mGetEntity = playerKlass ? FindMethodInHierarchy(playerKlass, getEntityName.c_str(), 0) : nullptr;
                if (mGetEntity) {
                    exc = nullptr;
                    entityObj = _monoRuntimeInvoke(mGetEntity, playerObj, nullptr, &exc);
                    if (exc) entityObj = nullptr;
                }
            }
        }
    }
    if (!entityObj) return false;

    void *entityKlass = _monoObjectGetClass(entityObj);
    if (!entityKlass) return false;
    void *mGetPos = FindMethodInHierarchy(entityKlass, getPositionName.c_str(), 0);
    void *mGetFwd = FindMethodInHierarchy(entityKlass, getForwardName.c_str(), 0);
    if (!mGetPos || !mGetFwd) return false;

    void *exc = nullptr;
    void *posBox = _monoRuntimeInvoke(mGetPos, entityObj, nullptr, &exc);
    if (exc || !posBox) return false;
    void *posRaw = _monoObjectUnbox(posBox);
    if (!posRaw) return false;
    Vector3 playerPos = *(Vector3 *) posRaw;

    exc = nullptr;
    void *fwdBox = _monoRuntimeInvoke(mGetFwd, entityObj, nullptr, &exc);
    if (exc || !fwdBox) return false;
    void *fwdRaw = _monoObjectUnbox(fwdBox);
    if (!fwdRaw) return false;
    Vector3 forward = *(Vector3 *) fwdRaw;

    forward.y = 0.0f;
    float sqrMag = forward.x * forward.x + forward.z * forward.z;
    if (sqrMag < 0.0004f) {
        forward.x = 0.0f;
        forward.y = 0.0f;
        forward.z = 1.0f;
    } else {
        float invLen = 1.0f / std::sqrt(sqrMag);
        forward.x *= invLen;
        forward.z *= invLen;
    }

    if (throwDistance <= 0.1f) throwDistance = 4.0f;
    outTarget.x = playerPos.x + forward.x * throwDistance;
    outTarget.y = playerPos.y + forward.y * throwDistance;
    outTarget.z = playerPos.z + forward.z * throwDistance;
    return true;
}

void AutoFishBaitLoop(uint32_t generation) {
    LOGI(OBFUSCATE("[FishBait] auto loop started gen=%u"), generation);
    auto setStatus = [](const std::string &en, const std::string &zh, bool isEmpty, int totalCnt, int stackCnt, int numStacks) {
        std::lock_guard<std::mutex> lock(g_fishBaitMutex);
        g_fishBaitStatus = en;
        g_fishBaitStatusZh = zh;
        g_fishBaitEmpty = isEmpty;
        g_fishBaitCount = totalCnt;
        g_fishBaitCurrentStackCount = stackCnt;
        g_fishBaitStackCount = numStacks;
    };

    setStatus("Loading Fish Bait payload...", u8"\u6b63\u5728\u52a0\u8f7d\u9c7c\u9975\u914d\u7f6e...", false, 0, 0, 0);

    const std::string baitWire = WireForOp(OP_AUTO_FISH_BAIT);
    if (baitWire.empty()) {
        setStatus("Fish bait wire missing - login again", u8"\u767b\u5f55\u5931\u6548\uff0c\u8bf7\u91cd\u65b0\u767b\u5f55", false, 0, 0, 0);
        g_autoFishBaitActive = false;
        g_fishBaitBusy.store(false, std::memory_order_release);
        return;
    }

    InvokeRequest req = FetchPayloadSync(baitWire);
    if (!SesiAktif()) {
        setStatus("Session expired", u8"\u4f1a\u8bdd\u5df2\u8fc7\u671f", false, 0, 0, 0);
        g_autoFishBaitActive = false;
        g_fishBaitBusy.store(false, std::memory_order_release);
        return;
    }
    if (req.op != OP_AUTO_FISH_BAIT || req.methodDesc != (const char *) OBFUSCATE("CUSTOM_AUTO_FISH_BAIT") ||
        req.payloadRaw.empty()) {
        setStatus("Fish Bait payload unavailable", u8"\u9c7c\u9975\u670d\u52a1\u914d\u7f6e\u4e0d\u53ef\u7528", false, 0, 0, 0);
        g_autoFishBaitActive = false;
        g_fishBaitBusy.store(false, std::memory_order_release);
        return;
    }

    auto pn = [&](const char *key) { return PayloadVM::MetadataValue(req.payloadRaw, key); };
    int baitStaticId = 0;
    float throwDistance = 4.0f;
    try { baitStaticId = std::stoi(pn("bait_static_id")); }
    catch (...) { baitStaticId = 0; }
    try {
        std::string distStr = pn("throw_distance");
        if (!distStr.empty()) throwDistance = std::stof(distStr);
    }
    catch (...) { throwDistance = 4.0f; }

    const std::string imgSystem = pn("n_img_system");
    const std::string nsBackpackSystem = pn("n_ns_backpack_system");
    const std::string clsBackpackSystem = pn("n_cls_backpack_system");
    const std::string getInstance = pn("n_get_instance");
    const std::string getAllItem = pn("n_get_all_item");
    const std::string listSize = pn("n_list_size");
    const std::string listItems = pn("n_list_items");
    const std::string nsBackpackItem = pn("n_ns_backpack_item");
    const std::string clsBackpackItem = pn("n_cls_backpack_item");
    const std::string imgLevel = pn("n_img_level");
    const std::string nsEntityUtil = pn("n_ns_entity_util");
    const std::string clsEntityUtil = pn("n_cls_entity_util");
    const std::string getSelfPlayerEntity = pn("n_get_self_player_entity");
    const std::string getSelfPlayer = pn("n_get_self_player");
    const std::string getEntity = pn("n_get_entity");
    const std::string getPosition = pn("n_get_position");
    const std::string getForward = pn("n_get_forward");
    const std::string scatterBaitName = pn("n_scatter_bait");

    if (baitStaticId <= 0 || req.dllName.empty() || req.nameSpace.empty() || req.className.empty() ||
        imgSystem.empty() || nsBackpackSystem.empty() || clsBackpackSystem.empty() ||
        getInstance.empty() || getAllItem.empty() || listSize.empty() || listItems.empty() ||
        nsBackpackItem.empty() || clsBackpackItem.empty() || imgLevel.empty() ||
        nsEntityUtil.empty() || clsEntityUtil.empty() || getPosition.empty() ||
        getForward.empty() || scatterBaitName.empty()) {
        setStatus("Incomplete Fish Bait payload", u8"\u9c7c\u9975\u914d\u7f6e\u6570\u636e\u4e0d\u5b8c\u6574", false, 0, 0, 0);
        g_autoFishBaitActive = false;
        g_fishBaitBusy.store(false, std::memory_order_release);
        return;
    }

    std::unordered_set<uint32_t> exhaustedStacks;

    while (g_autoFishBaitActive && SesiAktif() &&
           g_fishBaitLoopGeneration.load(std::memory_order_acquire) == generation &&
           req.generasiSesi == GenerasiSesi()) {

        std::vector<BackpackStackInfoAndroid> stacks;
        int totalBagCount = 0;

        RunOnUnityThreadSync([&]() {
            void *domain = _monoGetRootDomain();
            if (!domain) return;
            _monoThreadAttach(domain);
            ScanBackpackStacksByStaticIdFromPayload(
                    baitStaticId, stacks, &totalBagCount,
                    imgSystem, nsBackpackSystem, clsBackpackSystem,
                    getInstance, getAllItem, listSize, listItems,
                    nsBackpackItem, clsBackpackItem);
        });

        if (!g_autoFishBaitActive || !SesiAktif() ||
            g_fishBaitLoopGeneration.load(std::memory_order_acquire) != generation) {
            break;
        }

        if (stacks.empty() || totalBagCount <= 0) {
            exhaustedStacks.clear();
            setStatus(
                    "Empty / Not Found in Bag (ID: " + std::to_string(baitStaticId) + ")",
                    u8"\u80cc\u5305\u4e2d\u672a\u627e\u5230\u9c7c\u9975 / \u5df2\u7528\u5b8c (ID: " + std::to_string(baitStaticId) + ")",
                    true, 0, 0, 0);
            for (int w = 0; w < 10; ++w) {
                if (!g_autoFishBaitActive || !SesiAktif() ||
                    g_fishBaitLoopGeneration.load(std::memory_order_acquire) != generation)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }

        BackpackStackInfoAndroid chosenStack{};
        int unexhaustedTotal = 0;
        int unexhaustedStackCount = 0;
        for (const auto &st : stacks) {
            if (exhaustedStacks.find(st.netId) == exhaustedStacks.end()) {
                if (chosenStack.netId == 0) chosenStack = st;
                unexhaustedTotal += st.count;
                unexhaustedStackCount++;
            }
        }

        if (chosenStack.netId == 0) {
            exhaustedStacks.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            continue;
        }

        setStatus(
                "Throwing Fish Bait... Stack: " + std::to_string(chosenStack.count) + " | Total: " + std::to_string(unexhaustedTotal),
                u8"\u6b63\u5728\u6295\u63b7\u9c7c\u9975... \u5f53\u524d\u7ec4: " + std::to_string(chosenStack.count) + u8" | \u603b\u8ba1: " + std::to_string(unexhaustedTotal),
                false, unexhaustedTotal, chosenStack.count, unexhaustedStackCount);

        for (int thrown = 0; thrown < chosenStack.count; ++thrown) {
            if (!g_autoFishBaitActive || !SesiAktif() ||
                g_fishBaitLoopGeneration.load(std::memory_order_acquire) != generation) {
                break;
            }

            bool invokeOk = false;
            bool fatalError = false;
            std::string errEn, errZh;

            RunOnUnityThreadSync([&]() {
                void *domain = _monoGetRootDomain();
                if (!domain) {
                    fatalError = true;
                    errEn = "Mono domain unavailable";
                    errZh = u8"Mono \u8fd0\u884c\u65f6\u4e0d\u53ef\u7528";
                    return;
                }
                _monoThreadAttach(domain);

                Vector3 targetPos{};
                if (!TryGetFishBaitThrowTargetFromPayload(
                        targetPos, throwDistance, imgLevel, nsEntityUtil, clsEntityUtil,
                        getSelfPlayerEntity, getSelfPlayer, getEntity, getPosition, getForward)) {
                    errEn = "Player entity unavailable";
                    errZh = u8"\u672a\u627e\u5230\u73a9\u5bb6\u5b9e\u4f53\u4f4d\u7f6e";
                    return;
                }

                void *dataImage = LoadMonoImageFlexible(req.dllName);
                if (!dataImage) {
                    fatalError = true;
                    errEn = "Fishing protocol image unavailable";
                    errZh = u8"\u9493\u9c7c\u534f\u8bae\u6a21\u5757\u4e0d\u53ef\u7528";
                    return;
                }

                void *protocolClass = _monoClassFromName(
                        dataImage, req.nameSpace.c_str(), req.className.c_str());
                void *scatterMethod = protocolClass
                                      ? FindMethodInHierarchy(protocolClass, scatterBaitName.c_str(), 2)
                                      : nullptr;
                if (!scatterMethod) {
                    fatalError = true;
                    errEn = "CmdScatterBait unavailable";
                    errZh = u8"\u6295\u63b7\u9c7c\u9975\u63a5\u53e3\u4e0d\u53ef\u7528";
                    return;
                }

                uint32_t netIdArg = chosenStack.netId;
                void *args[2] = {&netIdArg, &targetPos};
                void *exc = nullptr;
                _monoRuntimeInvoke(scatterMethod, nullptr, args, &exc);
                if (!exc) {
                    invokeOk = true;
                } else {
                    errEn = "CmdScatterBait exception";
                    errZh = u8"\u6295\u63b7\u9c7c\u9975\u8c03\u7528\u5f02\u5e38";
                }
            });

            if (fatalError) {
                setStatus(errEn, errZh, false, 0, 0, 0);
                g_autoFishBaitActive = false;
                break;
            }

            if (!invokeOk) {
                setStatus(errEn, errZh, false, unexhaustedTotal - thrown, chosenStack.count - thrown, unexhaustedStackCount);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                break;
            }

            const int remStack = std::max(0, chosenStack.count - (thrown + 1));
            const int remTotal = std::max(0, unexhaustedTotal - (thrown + 1));
            setStatus(
                    "Throwing Fish Bait... Stack: " + std::to_string(remStack) + " | Total: " + std::to_string(remTotal),
                    u8"\u6b63\u5728\u6295\u63b7\u9c7c\u9975... \u5f53\u524d\u7ec4: " + std::to_string(remStack) + u8" | \u603b\u8ba1: " + std::to_string(remTotal),
                    false, remTotal, remStack, unexhaustedStackCount);

            const int delayMs = std::max(20, std::min(2000, g_fishBaitIntervalMs));
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }

        exhaustedStacks.insert(chosenStack.netId);
        if (g_autoFishBaitActive && SesiAktif() &&
            g_fishBaitLoopGeneration.load(std::memory_order_acquire) == generation) {
            setStatus(
                    "Stack finished, re-checking bag...",
                    u8"\u5f53\u524d\u7ec4\u5df2\u6295\u5b8c\uff0c\u6b63\u5728\u91cd\u65b0\u68c0\u67e5\u80cc\u5305...",
                    false, std::max(0, unexhaustedTotal - chosenStack.count), 0, std::max(0, unexhaustedStackCount - 1));
        }
    }

    if (!g_autoFishBaitActive) {
        std::lock_guard<std::mutex> lock(g_fishBaitMutex);
        if (!g_fishBaitEmpty) {
            g_fishBaitStatus = u8"已停止";
            g_fishBaitStatusZh = u8"\u5df2\u505c\u6b62";
        }
    }
    g_fishBaitBusy.store(false, std::memory_order_release);
    LOGI(OBFUSCATE("[FishBait] auto loop stopped gen=%u"), generation);
}


static void UseBackpackItemNative(int type, uint32_t netId) {
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) return;
    void *klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.BackPack"),
                                     OBFUSCATE("BackpackProtocolManager"));
    if (!klass) return;
    void *m_use = FindMethodInHierarchy(klass, OBFUSCATE("UseBackpackItem"), 2);
    if (!m_use) return;

    int argType = type;          // EntityType (enum) -> underlying int
    uint32_t argNetId = netId;
    void *args[] = {&argType, &argNetId};
    _monoRuntimeInvoke(m_use, nullptr, args, nullptr); // static
}

static void NotifyThrowToolRestorerNative(uint32_t itemNetId, uint32_t parentNetId = 0) {
    void *imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    void *imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) { LOGE(OBFUSCATE("[RestoreTool] imgData null")); return; }

    // Posisi diri sendiri
    Vector3 pos = {0.0f, 0.0f, 0.0f};
    if (imgLevel) {
        void *utilKlass = _monoClassFromName(imgLevel,
                                             OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
                                             OBFUSCATE("EntityUtil"));
        void *m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
        if (m_getSelf) {
            void *exc = nullptr;
            void *self = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, &exc);
            if (self && !exc) pos = NativeGetPosition(self);
        }
    }

    float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // identity (x, y, z, w)

    void *klass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ProtocolService.ItemDisplay"),
                                     OBFUSCATE("ToolRestorerProtocolManager"));
    if (!klass) { LOGE(OBFUSCATE("[RestoreTool] ToolRestorerProtocolManager null")); return; }

    // static void NotifyThrowToolRestorer(uint netId, Vector3 pos, Quaternion rot, uint parentNetId) -> 4 param
    void *m_notify = FindMethodInHierarchy(klass, OBFUSCATE("NotifyThrowToolRestorer"), 4);
    if (!m_notify) { LOGE(OBFUSCATE("[RestoreTool] NotifyThrowToolRestorer null")); return; }

    uint32_t argNetId = itemNetId;
    uint32_t argParent = parentNetId;
    // value-type (Vector3/Quaternion) dikirim sebagai pointer ke struct-nya
    void *args[] = {&argNetId, &pos, rot, &argParent};
    void *exc = nullptr;
    _monoRuntimeInvoke(m_notify, nullptr, args, &exc); // static

    LOGI(OBFUSCATE("[RestoreTool] ThrowToolRestorer netId=%u pos=(%.1f,%.1f,%.1f) parent=%u exc=%p"),
         itemNetId, pos.x, pos.y, pos.z, parentNetId, exc);
}

void AutoRestoreToolRoutine() {
    LOGI(OBFUSCATE("[RestoreTool] === routine STARTED ==="));
    auto lastInvoke = std::chrono::steady_clock::now();
    bool hasInvoked = false;
    int restorePrevDur = -1;
    int restoreStable = 0;
    g_toolRestoring.store(false);

    while (g_isRestoreToolActive) {
        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());

            int dur = GetCurrentToolDurabilityNative();
            g_currentToolDurability = dur;
            if (dur < 0) return;

            if (dur <= g_restoreToolThreshold) {
                // Farming berhenti sejak threshold terpicu, termasuk selama cooldown.
                g_toolRestoring.store(true);
                restorePrevDur = dur;
                restoreStable = 0;

                auto now = std::chrono::steady_clock::now();
                if (hasInvoked) {
                    long elapsed = (long) std::chrono::duration_cast<std::chrono::seconds>(
                            now - lastInvoke).count();
                    if (elapsed < g_restoreToolCooldownSec) return;
                }

                uint32_t netId = FindBackpackItemNetId(1750);
                if (netId == 0) netId = FindBackpackItemNetId(1751);
                if (netId != 0) {
                    NotifyThrowToolRestorerNative(netId);
                    lastInvoke = std::chrono::steady_clock::now();
                    hasInvoked = true;
                    LOGI(OBFUSCATE("[RestoreTool] invoke (dur=%d, target=%d) -> cooldown %ds"),
                         dur, g_restoreToolThreshold, g_restoreToolCooldownSec);
                } else {
                    LOGE(OBFUSCATE("[RestoreTool] item 1750/1751 tidak ada di backpack"));
                }
            } else if (g_toolRestoring.load()) {
                // Jangan lanjut farming hanya karena durability sudah melewati threshold.
                // Tunggu sampai proses repair benar-benar berhenti menaikkan durability.
                if (dur > restorePrevDur) {
                    restorePrevDur = dur;
                    restoreStable = 0;
                } else if (++restoreStable >= 5) {
                    g_toolRestoring.store(false);
                    LOGI(OBFUSCATE("[RestoreTool] durability stabil (dur=%d) -> farming lanjut"), dur);
                }
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    g_toolRestoring.store(false);
    LOGI(OBFUSCATE("[RestoreTool] === routine STOPPED ==="));
}


// =====================================================================
// AUTO FISHING / AUTO EAT / AUTO SNOW SCULPTURE (ported dari versi PC)
// =====================================================================

// ---- helper porting (pengganti Windows API) ----
static inline void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static inline uint64_t NowMs() {
    return (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

static const int FISH_SHADOW_BATTLE = 4; // inferensi dari enum FishStateName (4 = Battle)
static int g_fishThrowState = 1;

// ---------- Auto Snow & Sand Base Scanning ----------
static bool ScanBasesByStaticIdNative(int wantSid, std::vector<uint32_t> &outTargets) {
    outTargets.clear();
    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
        void *imgData  = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        void *imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
        if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
        if (!imgData || !imgLevel) { LOGE("[BaseScan] img null"); return; }

        void *entitiesKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("Entities"));
        void *m_getAny = entitiesKlass ? FindMethodInHierarchy(entitiesKlass, OBFUSCATE("GetAnyEntity"), 1) : nullptr;
        void *entityKlass = _monoClassFromName(imgData, OBFUSCATE("XDTGame.GAS.Client"), OBFUSCATE("Entity"));
        if (!entityKlass) entityKlass = _monoClassFromName(imgData, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("Entity"));
        if (!entityKlass && imgLevel) entityKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("Entity"));
        void *m_GetComponent = entityKlass ? FindMethodInHierarchy(entityKlass, OBFUSCATE("GetComponent"), 1) : nullptr;
        void *buildKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Homeland"), OBFUSCATE("BuildComponent"));
        if (!buildKlass) buildKlass = _monoClassFromName(imgData, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Homeland"), OBFUSCATE("BuildComponent"));

        void *m_getBuild = nullptr;
        void *m_getStaticId = nullptr;
        if (m_GetComponent && buildKlass) {
            void *typeArgs[1] = { _monoClassGetType(buildKlass) };
            void *mi = _mono_metadata_get_generic_inst(1, typeArgs);
            MonoGenericContext ctx; ctx.class_inst = nullptr; ctx.method_inst = mi;
            m_getBuild = _mono_class_inflate_generic_method(m_GetComponent, &ctx);
            m_getStaticId = FindMethodInHierarchy(buildKlass, OBFUSCATE("get_StaticId"), 0);
            if (!m_getStaticId) m_getStaticId = FindMethodInHierarchy(buildKlass, OBFUSCATE("get_staticId"), 0);
        }

        std::vector<uint32_t> allIds;
        void *dcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("DataCenter"));
        void *m_getCur = dcKlass ? FindMethodInHierarchy(dcKlass, OBFUSCATE("get_CurrentData"), 0) : nullptr;
        if (m_getCur) {
            void *exc0 = nullptr;
            void *cur = _monoRuntimeInvoke(m_getCur, nullptr, nullptr, &exc0);
            if (cur && !exc0) {
                void *sysKlass = _monoObjectGetClass(cur);
                void *f_map = _monoClassGetFieldFromName(sysKlass, OBFUSCATE("_netEntitiesMap"));
                void *dict = nullptr;
                if (f_map) _monoFieldGetValue(cur, f_map, &dict);
                if (dict) {
                    void *dk = _monoObjectGetClass(dict);
                    void *f_e = _monoClassGetFieldFromName(dk, OBFUSCATE("entries"));
                    if (!f_e) f_e = _monoClassGetFieldFromName(dk, OBFUSCATE("_entries"));
                    void *arr = nullptr;
                    if (f_e) _monoFieldGetValue(dict, f_e, &arr);
                    if (arr) {
                        int len = *(int *) ((uint8_t *) arr + OFF_ARR_COUNT);
                        uint8_t *data = (uint8_t *) arr + OFF_ARR_DATA;
                        if (len > 0 && len <= 100000) {
                            for (int i = 0; i < len; i++) {
                                uint8_t *e = data + (size_t) i * SZ_DICT_ENTRY;
                                if (*(int *) e < 0) continue;
                                uint32_t k = *(uint32_t *) (e + OFF_ENTRY_KEY);
                                if (k) allIds.push_back(k);
                            }
                        }
                    }
                }
            }
        }

        void *util = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
        void *m_getResId = util ? FindMethodInHierarchy(util, OBFUSCATE("GetEntityResId"), 1) : nullptr;

        uint8_t logErr = 0;
        for (uint32_t nid : allIds) {
            if (nid == 0) continue;
            int sid = -1;

            if (m_getAny && m_getBuild && m_getStaticId) {
                void *exc = nullptr;
                uint32_t n = nid; void *a[] = { &n };
                void *ent = _monoRuntimeInvoke(m_getAny, nullptr, a, &exc);
                if (ent && !exc) {
                    void *compArgs[] = { &logErr };
                    void *comp = _monoRuntimeInvoke(m_getBuild, ent, compArgs, &exc);
                    if (comp && !exc) {
                        void *sidObj = _monoRuntimeInvoke(m_getStaticId, comp, nullptr, &exc);
                        if (sidObj && !exc) sid = *(int *) _monoObjectUnbox(sidObj);
                    }
                }
            }

            if (sid != wantSid && m_getResId) {
                void *re = nullptr; uint32_t nn = nid; void *ra[] = { &nn };
                void *rr = _monoRuntimeInvoke(m_getResId, nullptr, ra, &re);
                if (rr && !re) { int rid = *(int *) _monoObjectUnbox(rr); if (rid == wantSid || (wantSid == 5200 && rid == 5200)) sid = rid; }
            }

            if (sid == wantSid) {
                outTargets.push_back(nid);
            }
        }
    });
    return !outTargets.empty();
}

static bool ScanSnowBasesNative(std::vector<uint32_t> &outTargets) {
    return ScanBasesByStaticIdNative(5200, outTargets);
}

static bool ScanSandBasesNative(std::vector<uint32_t> &outTargets, int wantSid) {
    return ScanBasesByStaticIdNative(wantSid, outTargets);
}

static int ReadSnowStateNative(uint32_t netId) {
    void *imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) return -1;
    void *dcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("DataCenter"));
    if (!dcKlass) return -1;
    void *m_try = FindMethodInHierarchy(dcKlass, OBFUSCATE("TryGetComponentData"), 2);
    if (!m_try) return -1;
    void *sbcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("SnowBaseComponentData"));
    if (!sbcKlass) return -1;

    void *typeArgs[1] = { _monoClassGetType(sbcKlass) };
    void *mi = _mono_metadata_get_generic_inst(1, typeArgs);
    MonoGenericContext ctx = { nullptr, mi };
    void *m_inflated = _mono_class_inflate_generic_method(m_try, &ctx);
    if (!m_inflated) return -1;

    uint32_t align = 0;
    int size = _monoClassValueSize(sbcKlass, &align);
    if (size <= 0 || size > 512) return -1;
    std::vector<char> buf(size, 0);

    uint32_t argNet = netId;
    void *args[] = { &argNet, buf.data() };
    void *exc = nullptr;
    void *res = _monoRuntimeInvoke(m_inflated, nullptr, args, &exc);
    if (!res || exc) return -1;
    if (!*(bool *) _monoObjectUnbox(res)) return -1;

    void *f_state = _monoClassGetFieldFromName(sbcKlass, OBFUSCATE("state"));
    if (!f_state) return -1;
    int off = _monoFieldGetOffset(f_state) - MONO_FIELD_HEADER;
    if (off < 0 || off + 4 > size) return -1;
    return *(int *) (buf.data() + off);
}

static void SnowSendNative(const char *method, uint32_t netId) {
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) return;
    void *klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Snow"), OBFUSCATE("SnowSculptureProtocolManager"));
    if (!klass) return;
    void *m = FindMethodInHierarchy(klass, method, 1);
    if (!m) return;
    uint32_t n = netId; void *args[] = { &n }; void *exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
}

static void PutSnowBallNative(uint32_t snowBaseNetId, uint32_t snowBallNetId) {
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (!img) return;
    void *klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Snow"), OBFUSCATE("SnowSculptureProtocolManager"));
    if (!klass) return;
    void *m = FindMethodInHierarchy(klass, OBFUSCATE("PutSnowBall"), 2);
    if (!m) return;
    uint32_t bId = snowBaseNetId; uint32_t iId = snowBallNetId;
    void *args[] = { &bId, &iId }; void *exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
    LOGI(OBFUSCATE("[Snow] PutSnowBall base=%u ball=%u exc=%p"), snowBaseNetId, snowBallNetId, exc);
}

static void StartSculptingNative(uint32_t netId) { SnowSendNative(OBFUSCATE("StartSculpting"), netId); }
static void StopSculptingNative(uint32_t netId) { SnowSendNative(OBFUSCATE("StopSculpting"), netId); }
static void GatherSnowSculptureNative(uint32_t netId) { SnowSendNative(OBFUSCATE("GatherSnowSculpture"), netId); }

static void ReportSculptingScoreNative(uint32_t netId, int score) {
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) return;
    void *klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Snow"), OBFUSCATE("SnowSculptureProtocolManager"));
    if (!klass) return;
    void *m = FindMethodInHierarchy(klass, OBFUSCATE("ReportSculptingScore"), 2);
    if (!m) return;
    uint32_t n = netId; int s = score;
    void *args[] = { &n, &s }; void *exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
    LOGI(OBFUSCATE("[Snow] ReportScore netId=%u score=%d exc=%p"), netId, score, exc);
}

static uint32_t GetActiveSnowTargetNetId() {
    uint32_t targetNetId = 0;
    void *imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    void *imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgData || !imgLevel) return 0;

    void *utilKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
    if (!utilKlass) return 0;
    void *m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
    if (!m_getSelf) return 0;

    void *exc = nullptr;
    void *selfPlayer = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, &exc);
    if (!selfPlayer || exc) return 0;

    void *spKlass = _monoObjectGetClass(selfPlayer);
    void *m_getStatus = FindMethodInHierarchy(spKlass, OBFUSCATE("get_Status"), 0);
    if (!m_getStatus) return 0;
    void *statusObj = _monoRuntimeInvoke(m_getStatus, selfPlayer, nullptr, &exc);
    if (!statusObj || exc) return 0;

    void *stKlass = _monoObjectGetClass(statusObj);
    void *m_getSnowSt = FindMethodInHierarchy(stKlass, OBFUSCATE("get_SnowSculptureStatus"), 0);
    if (!m_getSnowSt) return 0;
    void *snowStObj = _monoRuntimeInvoke(m_getSnowSt, statusObj, nullptr, &exc);
    if (!snowStObj || exc) return 0;

    void *sstKlass = _monoObjectGetClass(snowStObj);
    void *m_getTarget = FindMethodInHierarchy(sstKlass, OBFUSCATE("get_TargetNetId"), 0);
    if (!m_getTarget) return 0;
    void *tNetObj = _monoRuntimeInvoke(m_getTarget, snowStObj, nullptr, &exc);
    if (tNetObj && !exc) {
        targetNetId = *(uint32_t *) _monoObjectUnbox(tNetObj);
    }
    return targetNetId;
}


// Auto Sand Sculpture logic ported to AutoSandSculpture.h

void RunAutoSnowSculptureLoop() {
    SMPlatform::SetBreadcrumbLight("snow:loop");
    LOGI(OBFUSCATE("[Snow] === START FULL AUTO SNOW FAST LOOP ==="));

    while (g_isAutoSnowActive) {
        uint32_t activeTarget = 0;
        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());
            activeTarget = GetActiveSnowTargetNetId();
        });

        std::vector<uint32_t> targets;
        if (activeTarget != 0) {
            targets.push_back(activeTarget);
        } else {
            ScanSnowBasesNative(targets);
        }

        if (targets.empty()) {
            LOGI(OBFUSCATE("[Snow] No snow base (staticId 5200) found nearby. Waiting..."));
            SleepMs(600);
            continue;
        }

        for (uint32_t netId : targets) {
            if (!g_isAutoSnowActive) break;
            uint32_t cap = netId;

            // 1) Cek snowball di tas (staticId 5100)
            uint32_t snowBallNetId = 0;
            RunOnUnityThreadSync([&]() {
                _monoThreadAttach(_monoGetRootDomain());
                snowBallNetId = GetSeedNetId(5100);
            });

            if (snowBallNetId == 0) {
                LOGI(OBFUSCATE("[Snow] Snowballs in bag empty (netId=0). Ending loop."));
                g_isAutoSnowActive = false;
                break;
            }

            // 2) Jika belum dalam proses sculpting, isi bola salju & mulai sculpt
            if (activeTarget == 0) {
                RunOnUnityThreadSync([&]() {
                    _monoThreadAttach(_monoGetRootDomain());
                    PutSnowBallNative(cap, snowBallNetId);
                });
                SleepMs(100);

                if (!g_isAutoSnowActive) break;

                RunOnUnityThreadSync([&]() {
                    _monoThreadAttach(_monoGetRootDomain());
                    StartSculptingNative(cap);
                });
                SleepMs(150);
            }

            if (!g_isAutoSnowActive) break;

            // 3) Kirim skor QTE (Default 4 round, score 100/best)
            int score = (g_snowScore > 0) ? g_snowScore : 100;
            for (int k = 1; k <= 4; k++) {
                if (!g_isAutoSnowActive) break;
                RunOnUnityThreadSync([&]() {
                    _monoThreadAttach(_monoGetRootDomain());
                    ReportSculptingScoreNative(cap, score);
                });
                SleepMs(60);
            }

            if (!g_isAutoSnowActive) break;

            // 4) Stop Sculpting (Wajib sebelum Gather)
            SleepMs(80);
            RunOnUnityThreadSync([&]() {
                _monoThreadAttach(_monoGetRootDomain());
                StopSculptingNative(cap);
            });
            SleepMs(120);

            if (!g_isAutoSnowActive) break;

            // 5) Gather Snow Sculpture (Ambil hasil patung salju)
            RunOnUnityThreadSync([&]() {
                _monoThreadAttach(_monoGetRootDomain());
                GatherSnowSculptureNative(cap);
            });
            SleepMs(120);
        }

        SleepMs(100);
    }

    g_isAutoSnowActive = false;
    LOGI(OBFUSCATE("[Snow] === FULL AUTO SNOW FAST LOOP DONE ==="));
}

void RunAutoSnowSculptureOnce() {
    RunAutoSnowSculptureLoop();
}

// ---------- Auto Fishing ----------
static bool GetEntityPosByNetId(uint32_t netId, Vector3 &out) {
    void *imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) return false;
    void *entitiesKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("Entities"));
    void *m_get = FindMethodInHierarchy(entitiesKlass, OBFUSCATE("GetAnyEntity"), 1);
    if (!m_get) return false;
    uint32_t n = netId; void *a[] = { &n }; void *exc = nullptr;
    void *ent = _monoRuntimeInvoke(m_get, nullptr, a, &exc);
    if (!ent || exc) return false;
    void *ek = _monoObjectGetClass(ent);
    void *m_pos = FindMethodInHierarchy(ek, OBFUSCATE("get_position"), 0);
    if (!m_pos) return false;
    void *pObj = _monoRuntimeInvoke(m_pos, ent, nullptr, &exc);
    if (!pObj || exc) return false;
    memcpy(&out, _monoObjectUnbox(pObj), sizeof(Vector3));
    return true;
}
static bool TryGetCompDataBuf(const char *cls, uint32_t netId, std::vector<char> &buf, void *&outKlass) {
    if (!_monoClassValueSize || !_mono_metadata_get_generic_inst || !_mono_class_inflate_generic_method || !_monoClassGetType)
        return false;
    static std::map<std::string, void *> s_inf;
    static std::map<std::string, void *> s_klass;
    static std::map<std::string, int> s_size;
    std::string key = cls;

    void *m_inf; void *tKlass; int size;
    auto it = s_inf.find(key);
    if (it != s_inf.end()) { m_inf = it->second; tKlass = s_klass[key]; size = s_size[key]; }
    else {
        void *imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        if (!imgData) return false;
        void *dcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("DataCenter"));
        void *m_try = dcKlass ? FindMethodInHierarchy(dcKlass, OBFUSCATE("TryGetComponentData"), 2) : nullptr;
        tKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), cls);
        if (!m_try || !tKlass) return false;
        void *typeArgs[1] = { _monoClassGetType(tKlass) };
        void *mi = _mono_metadata_get_generic_inst(1, typeArgs);
        MonoGenericContext ctx = { nullptr, mi };
        m_inf = _mono_class_inflate_generic_method(m_try, &ctx);
        if (!m_inf) return false;
        uint32_t align = 0; size = _monoClassValueSize(tKlass, &align);
        if (size <= 0 || size > 1024) return false;
        s_inf[key] = m_inf; s_klass[key] = tKlass; s_size[key] = size;
    }

    outKlass = tKlass;
    buf.assign(size, 0);
    uint32_t argNet = netId; void *args[] = { &argNet, buf.data() }; void *exc = nullptr;
    void *res = _monoRuntimeInvoke(m_inf, nullptr, args, &exc);
    if (!res || exc) return false;
    return *(bool *) _monoObjectUnbox(res);
}
static int CompFieldOff(void *klass, const char *name) {
    void *f = _monoClassGetFieldFromName(klass, name);
    return f ? (_monoFieldGetOffset(f) - MONO_FIELD_HEADER) : -1;
}
static void UpdateFishShadowPositionNative(uint32_t fishNetId, Vector3 pos) {
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) return;
    void *klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Fishing"), OBFUSCATE("FishingProtocolManager"));
    if (!klass) return;
    void *m = FindMethodInHierarchy(klass, OBFUSCATE("UpdateFishShadowPosition"), 2);
    if (!m) return;
    uint32_t n = fishNetId; Vector3 p = pos;
    void *args[] = { &n, &p }; void *exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
}
static void GetAllNetIds(std::vector<uint32_t> &out) {
    void *imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) return;
    void *dcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("DataCenter"));
    void *m_getCur = dcKlass ? FindMethodInHierarchy(dcKlass, OBFUSCATE("get_CurrentData"), 0) : nullptr;
    if (!m_getCur) return;
    void *exc = nullptr;
    void *cur = _monoRuntimeInvoke(m_getCur, nullptr, nullptr, &exc);
    if (!cur || exc) return;
    void *sysKlass = _monoObjectGetClass(cur);
    void *f_map = _monoClassGetFieldFromName(sysKlass, OBFUSCATE("_netEntitiesMap"));
    if (!f_map) return;
    void *dict = nullptr; _monoFieldGetValue(cur, f_map, &dict);
    if (!dict) return;
    void *dk = _monoObjectGetClass(dict);
    void *f_e = _monoClassGetFieldFromName(dk, OBFUSCATE("entries"));
    if (!f_e) f_e = _monoClassGetFieldFromName(dk, OBFUSCATE("_entries"));
    if (!f_e) return;
    void *arr = nullptr; _monoFieldGetValue(dict, f_e, &arr);
    if (!arr) return;
    uintptr_t a = (uintptr_t) arr;
    int len = SafeRead<int>(a + OFF_ARR_COUNT, 0);
    if (len <= 0 || len > 100000) return;
    uintptr_t d = a + OFF_ARR_DATA;
    for (int i = 0; i < len; i++) {
        uintptr_t e = d + (uintptr_t) i * SZ_DICT_ENTRY;
        if (SafeRead<int>(e, -1) < 0) continue;
        uint32_t k = SafeRead<uint32_t>(e + OFF_ENTRY_KEY, 0);
        if (k) out.push_back(k);
    }
}
static int GetCurrentToolIdNative() {
    _monoThreadAttach(_monoGetRootDomain());
    void *imgSys = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!imgSys) return -1;
    void *toolSysKlass = _monoClassFromName(imgSys, OBFUSCATE("XDTGameSystem.GameplaySystem.Tool"), OBFUSCATE("ToolSystem"));
    if (!toolSysKlass) return -1;
    void *m_getInst = FindMethodInHierarchy(toolSysKlass, OBFUSCATE("get_Instance"), 0);
    void *m_getCurTool = FindMethodInHierarchy(toolSysKlass, OBFUSCATE("GetCurrentTool"), 0);
    if (!m_getInst || !m_getCurTool) return -1;
    void *exc = nullptr;
    void *inst = _monoRuntimeInvoke(m_getInst, nullptr, nullptr, &exc);
    if (!inst || exc) return -1;
    void *tool = _monoRuntimeInvoke(m_getCurTool, inst, nullptr, &exc);
    if (!tool || exc) return 0;
    void *tk = _monoObjectGetClass(tool);
    void *f_id = _monoClassGetFieldFromName(tk, OBFUSCATE("Id"));
    if (!f_id) return -1;
    void *pv = nullptr; _monoFieldGetValue(tool, f_id, &pv);
    return (int) (uintptr_t) pv;
}
static void EquipFishingRodNative(int rodId) {
    _monoThreadAttach(_monoGetRootDomain());
    void *imgSys = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!imgSys) return;
    void *toolSysKlass = _monoClassFromName(imgSys, OBFUSCATE("XDTGameSystem.GameplaySystem.Tool"), OBFUSCATE("ToolSystem"));
    if (!toolSysKlass) return;
    void *m_getInst = FindMethodInHierarchy(toolSysKlass, OBFUSCATE("get_Instance"), 0);
    void *m_setHandhold = FindMethodInHierarchy(toolSysKlass, OBFUSCATE("SetHandhold"), 1);
    if (!m_getInst || !m_setHandhold) return;
    void *exc = nullptr;
    void *inst = _monoRuntimeInvoke(m_getInst, nullptr, nullptr, &exc);
    if (!inst || exc) return;
    int id = rodId; void *args[] = { &id };
    _monoRuntimeInvoke(m_setHandhold, inst, args, &exc);
}
static bool EnsureRodEquipped(int rodId) {
    int cur = GetCurrentToolIdNative();
    if (cur != rodId) { EquipFishingRodNative(rodId); return true; }
    return false;
}
static bool IsAutoRepairBusy() {
    if (!g_isRestoreToolActive) return false;
    int dur = GetCurrentToolDurabilityNative(); // -1 = tidak pegang tool
    if (dur < 0) return false;
    return dur <= g_restoreToolThreshold;
}
static bool SetFishingStateButtonPressedNative(bool pressed);
static bool TryArmFishingInstantCatchNative();

void PullFishingRodNative(bool pressed) {
    _monoThreadAttach(_monoGetRootDomain());
    SetFishingStateButtonPressedNative(pressed);
    void *imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) return;
    void *protoKlass = _monoClassFromName(imgData, (const char *) OBFUSCATE("XDTDataAndProtocol.ProtocolService.Fishing"), (const char *) OBFUSCATE("FishingProtocolManager"));
    if (!protoKlass) return;
    void *m_pull = FindMethodInHierarchy(protoKlass, (const char *) OBFUSCATE("FishingRodPull"), 1);
    if (!m_pull) return;
    bool p = pressed;
    void *args[] = { &p };
    void *exc = nullptr;
    _monoRuntimeInvoke(m_pull, nullptr, args, &exc);
}

void CancelFishingNative() {
    _monoThreadAttach(_monoGetRootDomain());
    void *imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
    if (imgData) {
        void *protoKlass = _monoClassFromName(imgData, (const char *) OBFUSCATE("XDTDataAndProtocol.ProtocolService.Fishing"), (const char *) OBFUSCATE("FishingProtocolManager"));
        if (protoKlass) {
            void *m_cancel = FindMethodInHierarchy(protoKlass, (const char *) OBFUSCATE("CancelFishing"), 0);
            if (m_cancel) {
                void *exc = nullptr;
                _monoRuntimeInvoke(m_cancel, nullptr, nullptr, &exc);
            }
        }
    }
    void *imgLevel = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
    if (imgLevel) {
        void *utilKlass = _monoClassFromName(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.GameplaySystem"), (const char *) OBFUSCATE("FishingUtil"));
        if (utilKlass) {
            void *m_hookUp = FindMethodInHierarchy(utilKlass, (const char *) OBFUSCATE("SendCameraEventHookUpEmpty"), 0);
            if (m_hookUp) {
                void *exc = nullptr;
                _monoRuntimeInvoke(m_hookUp, nullptr, nullptr, &exc);
            }
        }
        void *klass = _monoClassFromName(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.GameplaySystem"), (const char *) OBFUSCATE("GameplayApi"));
        if (klass) {
            void *m = FindMethodInHierarchy(klass, (const char *) OBFUSCATE("ExitFishing"), 0);
            if (m) {
                void *exc = nullptr;
                _monoRuntimeInvoke(m, nullptr, nullptr, &exc);
            }
        }
    }
}

static bool CastRodNative() {
    _monoThreadAttach(_monoGetRootDomain());
    void *imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) return false;
    void *protoKlass = _monoClassFromName(imgData,
        (const char *) OBFUSCATE("XDTDataAndProtocol.ProtocolService.Fishing"), (const char *) OBFUSCATE("FishingProtocolManager"));
    if (!protoKlass) return false;
    void *m_cast = FindMethodInHierarchy(protoKlass, (const char *) OBFUSCATE("CastRod"), 0);
    if (!m_cast) return false;
    void *exc = nullptr;
    _monoRuntimeInvoke(m_cast, nullptr, nullptr, &exc);
    return (exc == nullptr);
}

static bool NotifyFloatInWaterNative(uint32_t floatNetId, Vector3 buoyPos, Vector3 dir, float successLen, float failLen) {
    _monoThreadAttach(_monoGetRootDomain());
    void *imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) return false;
    void *protoKlass = _monoClassFromName(imgData,
        (const char *) OBFUSCATE("XDTDataAndProtocol.ProtocolService.Fishing"), (const char *) OBFUSCATE("FishingProtocolManager"));
    if (!protoKlass) return false;
    void *m_notify = FindMethodInHierarchy(protoKlass, (const char *) OBFUSCATE("NotifyFloatInWater"), 5);
    if (!m_notify) return false;
    uint32_t fnId = floatNetId;
    Vector3 bp = buoyPos;
    Vector3 d = dir;
    float sLen = successLen;
    float fLen = failLen;
    void *args[] = { &fnId, &bp, &d, &sLen, &fLen };
    void *exc = nullptr;
    _monoRuntimeInvoke(m_notify, nullptr, args, &exc);
    return (exc == nullptr);
}

void *GetSelfMoveComponentNative() {
    if (!SesiAktif()) return nullptr;
    void *img = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
    if (!img) return nullptr;
    void *entityUtil = _monoClassFromName(img,
        (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
        (const char *) OBFUSCATE("EntityUtil"));
    void *getSelf = entityUtil ? FindMethodInHierarchy(
        entityUtil, (const char *) OBFUSCATE("GetSelfPlayer"), 0) : nullptr;
    void *exc = nullptr;
    void *player = getSelf ? _monoRuntimeInvoke(getSelf, nullptr, nullptr, &exc) : nullptr;
    if (!player || exc) return nullptr;
    void *playerClass = _monoObjectGetClass(player);
    void *getMove = playerClass ? FindMethodInHierarchy(
        playerClass, (const char *) OBFUSCATE("get_moveComponent"), 0) : nullptr;
    exc = nullptr;
    void *movement = getMove ? _monoRuntimeInvoke(getMove, player, nullptr, &exc) : nullptr;
    return (!exc) ? movement : nullptr;
}

void SetAutoFishHoverNative(bool enable) {
    if (!enable && !g_autoFishHoverGravitySaved) {
        g_autoFishHoverAnchorValid = false;
        return;
    }

    if (isInstantFishing && !g_autoFishingActive && SesiAktif()) {
        static uint64_t s_lastManualInstantArm = 0;
        const uint64_t now = (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - s_lastManualInstantArm >= 120) {
            s_lastManualInstantArm = now;
            TryArmFishingInstantCatchNative();
        }
    }
    void *movement = GetSelfMoveComponentNative();
    if (!movement) return;
    void *movementClass = _monoObjectGetClass(movement);
    if (!movementClass) return;

    void *getGravity = FindMethodInHierarchy(
        movementClass, (const char *) OBFUSCATE("get_Gravity"), 0);
    void *setGravity = FindMethodInHierarchy(
        movementClass, (const char *) OBFUSCATE("set_Gravity"), 1);
    if (!setGravity) return;

    if (!enable) {
        if (g_autoFishHoverGravitySaved) {
            float gravity = g_autoFishHoverOriginalGravity;
            void *args[] = { &gravity };
            _monoRuntimeInvoke(setGravity, movement, args, nullptr);
            g_autoFishHoverGravitySaved = false;
            LOGI((const char *) OBFUSCATE("[AutoFish] hover OFF; gravity restored %.2f"), gravity);
        }
        g_autoFishHoverAnchorValid = false;
        return;
    }

    if (!g_autoFishHoverGravitySaved) {
        void *exc = nullptr;
        void *boxed = getGravity
            ? _monoRuntimeInvoke(getGravity, movement, nullptr, &exc) : nullptr;
        if (boxed && !exc) {
            void *raw = _monoObjectUnbox(boxed);
            if (raw) g_autoFishHoverOriginalGravity = *(float *) raw;
        }
        g_autoFishHoverGravitySaved = true;
        LOGI((const char *) OBFUSCATE("[AutoFish] hover ON; saved gravity %.2f"),
            g_autoFishHoverOriginalGravity);
    }

    float zeroGravity = 0.0f;
    void *gravityArgs[] = { &zeroGravity };
    _monoRuntimeInvoke(setGravity, movement, gravityArgs, nullptr);

    auto clearVerticalVelocity = [&](const char *getterName, const char *setterName) {
        void *getter = FindMethodInHierarchy(movementClass, getterName, 0);
        void *setter = FindMethodInHierarchy(movementClass, setterName, 1);
        if (!getter || !setter) return;
        void *exc = nullptr;
        void *boxed = _monoRuntimeInvoke(getter, movement, nullptr, &exc);
        if (!boxed || exc) return;
        void *raw = _monoObjectUnbox(boxed);
        if (!raw) return;
        Vector3 velocity = *(Vector3 *) raw;
        velocity.y = 0.0f;
        void *args[] = { &velocity };
        _monoRuntimeInvoke(setter, movement, args, nullptr);
    };
    clearVerticalVelocity((const char *) OBFUSCATE("get_Velocity"), (const char *) OBFUSCATE("set_Velocity"));
    clearVerticalVelocity((const char *) OBFUSCATE("get_RealVelocity"), (const char *) OBFUSCATE("set_RealVelocity"));

    void *controllerField = FindFieldInHierarchy(
        movementClass, (const char *) OBFUSCATE("_controller"));
    void *controller = nullptr;
    if (controllerField)
        _monoFieldGetValue(movement, controllerField, &controller);
    if (!controller) return;
    void *controllerClass = _monoObjectGetClass(controller);
    void *getTransform = controllerClass ? FindMethodInHierarchy(
        controllerClass, (const char *) OBFUSCATE("get_transform"), 0) : nullptr;
    void *move = controllerClass ? FindMethodInHierarchy(
        controllerClass, (const char *) OBFUSCATE("Move"), 1) : nullptr;
    if (!getTransform || !move) return;
    void *exc = nullptr;
    void *transform = _monoRuntimeInvoke(getTransform, controller, nullptr, &exc);
    if (!transform || exc) return;
    void *transformClass = _monoObjectGetClass(transform);
    void *getPosition = transformClass ? FindMethodInHierarchy(
        transformClass, (const char *) OBFUSCATE("get_position"), 0) : nullptr;
    if (!getPosition) return;
    exc = nullptr;
    void *boxedPosition = _monoRuntimeInvoke(
        getPosition, transform, nullptr, &exc);
    if (!boxedPosition || exc) return;
    void *rawPosition = _monoObjectUnbox(boxedPosition);
    if (!rawPosition) return;
    const Vector3 position = *(Vector3 *) rawPosition;
    if (!g_autoFishHoverAnchorValid) {
        g_autoFishHoverAnchorY = position.y;
        g_autoFishHoverAnchorValid = true;
        LOGI((const char *) OBFUSCATE("[AutoFish] hover Y locked at %.2f"), position.y);
    }
    const float dy = g_autoFishHoverAnchorY - position.y;
    if (fabsf(dy) > 0.0001f) {
        Vector3 correction = { 0.0f, dy, 0.0f };
        void *moveArgs[] = { &correction };
        _monoRuntimeInvoke(move, controller, moveArgs, nullptr);
    }
}
// =========================================================
// PATCH TELEPORT ABNORMAL LOG & ANTI-CRASH / ANTI-BLACK-SCREEN
// =========================================================
void EnsureTeleportAntiCrashPatched() {
#if defined(__APPLE__)
    // 侧载 iOS：__TEXT 受代码签名保护、mono 方法体是解释器入口，写方法体 RET 只会
    // 把游戏方法写坏（实测「传送采集」崩在 tp:auto 的补丁阶段）。传送链路已改走
    // 游戏原生 AutoMoveTransfer，不再需要这些补丁 —— 这里直接返回，一行代码页都不碰。
    return;
#endif
    static std::atomic<bool> s_patched{ false };
    if (s_patched.load(std::memory_order_relaxed)) return;

    bool allOk = true;

    // Patch 1: BasePlayerComponent.LogAbnormalTransferIfNeeded -> RET
    // Mencegah game memanggil MathUtility.LogInvalid / DebugSystem.LogError yang memicu crash
    bool ok1 = PatchMethodReturnNop(
        (const char *) OBFUSCATE("XDTLevelAndEntity"),
        (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"),
        (const char *) OBFUSCATE("BasePlayerComponent"),
        (const char *) OBFUSCATE("LogAbnormalTransferIfNeeded"),
        6
    );
    if (ok1) {
        LOGI("[TeleportFix] LogAbnormalTransferIfNeeded di-patch RET.");
    } else {
        allOk = false;
    }

    // Patch 2: ActorMovementComponent.CheckHasWalkableBelow -> return true
    // Method ini dipakai di BasePlayerComponent.Transfer saat checkCollision=true (Transfer2Entity).
    // Dengan patch ini, Transfer2Entity tidak lagi memblokir teleport ke area yang chunk belum di-load.
    bool ok2 = PatchMethodReturnTrue(
        (const char *) OBFUSCATE("XDTLevelAndEntity"),
        (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component"),
        (const char *) OBFUSCATE("ActorMovementComponent"),
        (const char *) OBFUSCATE("CheckHasWalkableBelow"),
        2  // (Vector3 position, out Vector3 walkablePos)
    );
    if (ok2) {
        LOGI("[TeleportFix] CheckHasWalkableBelow di-patch ReturnTrue.");
    } else {
        allOk = false;
    }

    // Patch 3: TeleportModule.OnSwitchSceneEvent -> RET
    // Muara perintah hilangnya UI dan Karakter saat teleport
    bool ok3 = PatchMethodReturnNop(
        (const char *) OBFUSCATE("XDTLevelAndEntity"),
        (const char *) OBFUSCATE("XDTGUI.Module.Scene"),
        (const char *) OBFUSCATE("TeleportModule"),
        (const char *) OBFUSCATE("OnSwitchSceneEvent"),
        1  // param: SwitchSceneEvent
    );
    if (ok3) {
        LOGI("[TeleportFix] TeleportModule.OnSwitchSceneEvent di-patch RET (UI & Karakter kebal).");
    } else {
        allOk = false;
    }

    // Patch 4: UIEventBridge.OnSwitchSceneOpenRequested -> RET
    // Mencegah game memanggil SwitchScenePanel.Open dan UIManager.CloseAllPanelsWithoutType (yang menutup semua UI)
    bool ok4 = PatchMethodReturnNop(
        (const char *) OBFUSCATE("XDTGameUI"),
        (const char *) OBFUSCATE("XDTGame.UI"),
        (const char *) OBFUSCATE("UIEventBridge"),
        (const char *) OBFUSCATE("OnSwitchSceneOpenRequested"),
        1  // param: SwitchSceneOpenRequestedEvent
    );
    if (ok4) {
        LOGI("[TeleportFix] UIEventBridge.OnSwitchSceneOpenRequested di-patch RET (cegah UI hilang).");
    } else {
        allOk = false;
    }

    // Patch 5: UIEventBridge.OnTeleportModuleShow -> RET
    // Mencegah game memanggil SwitchScenePanel.Open saat event TeleportModuleShow dikirim
    bool ok5 = PatchMethodReturnNop(
        (const char *) OBFUSCATE("XDTGameUI"),
        (const char *) OBFUSCATE("XDTGame.UI"),
        (const char *) OBFUSCATE("UIEventBridge"),
        (const char *) OBFUSCATE("OnTeleportModuleShow"),
        1  // param: TeleportModuleShowEvent
    );
    if (ok5) {
        LOGI("[TeleportFix] UIEventBridge.OnTeleportModuleShow di-patch RET (cegah SwitchScenePanel).");
    } else {
        allOk = false;
    }

    // Patch 6: UIEventBridge.OnLoadingOpenRequested -> RET
    // Mencegah game memanggil LoadingPanel.Open dan UIManager.CloseAllPanelsWithoutType
    bool ok6 = PatchMethodReturnNop(
        (const char *) OBFUSCATE("XDTGameUI"),
        (const char *) OBFUSCATE("XDTGame.UI"),
        (const char *) OBFUSCATE("UIEventBridge"),
        (const char *) OBFUSCATE("OnLoadingOpenRequested"),
        1  // param: LoadingOpenRequestedEvent
    );
    if (ok6) {
        LOGI("[TeleportFix] UIEventBridge.OnLoadingOpenRequested di-patch RET (cegah LoadingPanel & CloseAllPanels).");
    } else {
        allOk = false;
    }

    // Patch 7: SwitchScenePanel.Open -> return 0 / null
    // Memastikan panel loading/fade tidak dapat dibuka sama sekali dari jalur manapun
    bool ok7 = PatchMethodReturnInt(
        (const char *) OBFUSCATE("XDTGameUI"),
        (const char *) OBFUSCATE("XDTGame.UI.Panel"),
        (const char *) OBFUSCATE("SwitchScenePanel"),
        (const char *) OBFUSCATE("Open"),
        2, // param: AnimType, SwitchSceneType
        0  // return null pointer
    );
    if (ok7) {
        LOGI("[TeleportFix] SwitchScenePanel.Open di-patch Return 0 (cegah fade panel).");
    } else {
        allOk = false;
    }

    // Patch 8: ActionClipSwitchSceneOn.OnBehaveStart -> RET
    // Mencegah animator pemain memicu TransmitStart (yang mendissolve/menyembunyikan mesh model karakter)
    bool ok8 = PatchMethodReturnNop(
        (const char *) OBFUSCATE("XDTLevelAndEntity"),
        (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Action"),
        (const char *) OBFUSCATE("ActionClipSwitchSceneOn"),
        (const char *) OBFUSCATE("OnBehaveStart"),
        1  // param: float time
    );
    if (ok8) {
        LOGI("[TeleportFix] ActionClipSwitchSceneOn.OnBehaveStart di-patch RET (karakter tidak hilang).");
    } else {
        allOk = false;
    }

    // Patch 9: UIEventBridge.OnFullScreenCloseRequested -> RET
    // Mencegah game memanggil UIManager.CloseAllFullScreen() saat teleportasi/scene switch
    bool ok9 = PatchMethodReturnNop(
        (const char *) OBFUSCATE("XDTGameUI"),
        (const char *) OBFUSCATE("XDTGame.UI"),
        (const char *) OBFUSCATE("UIEventBridge"),
        (const char *) OBFUSCATE("OnFullScreenCloseRequested"),
        1  // param: FullScreenCloseRequestedEvent
    );
    if (ok9) {
        LOGI("[TeleportFix] UIEventBridge.OnFullScreenCloseRequested di-patch RET (cegah CloseAllFullScreen).");
    } else {
        allOk = false;
    }

    if (allOk) {
        s_patched.store(true, std::memory_order_relaxed);
        LOGI("[TeleportFix] Semua patch teleport aktif (LogAbnormal + CheckWalkable + NoFadeAnim + NoSwitchScene + AntiHideUI + AntiHideChar + AntiCloseFullScreen).");
    }
}


// ===========================================================================
//  传送（iOS 安全版）—— 与 PC 版 HeartopiaComplete.Teleport.cs 的权威路径对齐
// ---------------------------------------------------------------------------
//  PC 版唯一的传送原语（也是游戏自己用的那一个）：
//      EntityHelper.AutoMoveTransfer(Vector3)
//  游戏侧实现（XDTLevelAndEntity.Utils.EntityHelper，反编译核对）：
//      EntityManager.Instance.selfPlayer
//          .GetComponent<LocalPlayerComponent>()
//          .Transfer(pos, entity.rotation.eulerAngles, 0, checkCollision:false)
//  静态、单参、内部全是托管代码，相机与位置同步由游戏自己处理。
//
//  iOS 版此前「所有传送一用就闪退」的四类根因，本段全部拆掉：
//   ① 线程（主因）：云端载荷（HOME / PLAYER / 传送目录）在 worker 线程里派发，
//      FeatureDispatch 旧代码直接调 AutoMoveTransferDirect —— 在非 Unity 线程
//      里碰游戏对象 = 随机 SIGSEGV。现在统一走 AutoMoveTransferNative 的
//      RunOnUnityThreadSync 封送（FeatureDispatch 已同步改回 Native 入口）。
//   ② 相机吸附块：传送后改写 XDTCameraManager / FollowCameraController 的插值
//      状态与 isFirstFrame，实测游戏自己的相机更新在下一帧就崩（tp:cam7）。
//      PC 版传送不做这一步 —— 整块删除。
//   ③ 反崩溃 inline patch（往游戏方法体首字节写 RET）：侧载 iOS 的 __TEXT 受
//      代码签名保护、mono 方法体是解释器入口，写下去只会把游戏方法写坏。
//      传送链路不再调用 EnsureTeleportAntiCrashPatched；FeatureDispatch 里的
//      补丁函数在 Apple 上统一 return false。
//   ④ 传送前后的 mono 反射探针（GetSelfPlayer / get_position / 字典遍历轮询）：
//      对象未稳定时探测本身就会崩（旧版 tp:pre 即此）。现在只做一次
//      「零 invoke 的签名自检」，是否成功以 invoke 有没有抛异常为准。
//
//  线程纪律：所有 *_Direct 函数只允许在 Unity 线程执行（由 Native 入口封送）。
// ===========================================================================

// 帧泵（= iOS 主线程 / Unity 线程）是否已经在跑。RunOnUnityThreadSync 的队列
// 由它消费；没跑起来就排队 = 调用方白等 20 秒，所以传送前先用纯 C++ 信号查一次。
static bool SMTpPumpAlive() {
    return g_unityThreadId.load(std::memory_order_relaxed) != std::thread::id();
}

// 传送前闸门：纯 C++、不碰 mono、不 sleep、不排队。
// 只要求帧泵在跑（能把 invoke 封送到 Unity 线程）。游戏侧 AutoMoveTransfer 自己
// 会判 EntityManager / selfPlayer 是否就绪（null 直接 return），所以这里不再做
// 「区块加载好没」的反射探测 —— 那正是旧版 tp:pre 的崩溃点。
static bool SMTpGate(const char **reasonOut) {
    if (reasonOut) *reasonOut = "?";
    if (!SMTpPumpAlive()) {
        if (reasonOut) *reasonOut = "no-pump";
        return false;
    }
    if (reasonOut) *reasonOut = "ok";
    return true;
}

// 闸门包装：打面包屑（通过 tp:gate / 拒绝 tp:skip:<reason>），不做任何等待。
// pos / timeoutMs 只为兼容既有调用点保留。
static bool SMTpWaitAreaReady(Vector3 pos, int timeoutMs) {
    (void) pos;
    (void) timeoutMs;
    const char *reason = "?";
    if (SMTpGate(&reason)) {
        SMPlatform::SetBreadcrumbLight("tp:gate");
        return true;
    }
    SMPlatform::SetBreadcrumbLight(std::string("tp:skip:") + reason);
    LOGE("[TeleportFix] gate teleport belum lolos (%s); teleport dibatalkan.", reason);
    return false;
}

// 方法是否 mono 静态方法（MONO_METHOD_ATTR_STATIC = 0x10）。
// 实例方法传 nullptr this 会 SIGSEGV；AOT 方法不走本链路。
static bool SMTpMethodIsStatic(void *method) {
    if (!method || !SMMonoHot::IsMonoMethod(method)) return false;
    const uint32_t flags = SMMonoHot::MethodGetFlags(method, nullptr);
    return (flags & 0x10u) != 0;
}

// 校验 EntityHelper.AutoMoveTransfer(Vector3)：
//   mono 静态方法、返回 void、恰好 1 个参数、参数是 UnityEngine.Vector3
//   （12 字节值类型，MONO_TYPE_VALUETYPE = 0x11）。
// 读不到任何一项都返回 false —— 这是防「iOS 版本方法表变化 → 把 12 字节 Vector3
// 当别的参数盲调 → SIGSEGV」的唯一保险（宁可不传送，也不盲调）。
static bool SMTpCheckAutoMoveSignature(void *method, std::string *sigOut) {
    if (sigOut) *sigOut = "non-mono";
    if (!SMTpMethodIsStatic(method)) return false;
    void *sig = SMMonoHot::MethodSignature(method);
    if (!sig) {
        if (sigOut) *sigOut = "no-sig";
        return false;
    }
    void *retType = SMMonoHot::SignatureGetReturnType(sig);
    const int retCode = retType ? SMMonoHot::TypeGetType(retType) : -1;
    if (retCode != 0x01) {   // MONO_TYPE_VOID
        if (sigOut) *sigOut = "ret";
        return false;
    }
    void *iter = nullptr;
    void *param0 = SMMonoHot::SignatureGetParams(sig, &iter);
    if (!param0) {
        if (sigOut) *sigOut = "argc0";
        return false;
    }
    if (SMMonoHot::SignatureGetParams(sig, &iter) != nullptr) {
        if (sigOut) *sigOut = "argc2";
        return false;
    }
    const int typeCode = SMMonoHot::TypeGetType(param0);
    char buf[48];
    snprintf(buf, sizeof(buf), "s1a1t%02x", (typeCode < 0 || typeCode > 0xff) ? 0xff : typeCode);
    if (sigOut) *sigOut = buf;
    if (typeCode != 0x11) return false;   // MONO_TYPE_VALUETYPE（struct 值类型）
    void *paramClass = SMMonoHot::ClassFromMonoType(param0);
    if (!paramClass) {
        if (sigOut) *sigOut = "ptype";
        return false;
    }
    const char *clsName = SMMonoHot::ClassGetName(paramClass);
    const char *nsName = SMMonoHot::ClassGetNamespace(paramClass);
    if (!clsName || strcmp(clsName, "Vector3") != 0) {
        if (sigOut) *sigOut = "ptype-name";
        return false;
    }
    if (nsName && *nsName && strcmp(nsName, "UnityEngine") != 0) {
        if (sigOut) *sigOut = "ptype-ns";
        return false;
    }
    return true;
}

// 校验 EntityHelper.Transfer2Entity(uint)：mono 静态、返回 void、1 个整数参数（I4/U4）。
static bool SMTpCheckEntityTransferSignature(void *method, std::string *sigOut) {
    if (sigOut) *sigOut = "non-mono";
    if (!SMTpMethodIsStatic(method)) return false;
    void *sig = SMMonoHot::MethodSignature(method);
    if (!sig) {
        if (sigOut) *sigOut = "no-sig";
        return false;
    }
    void *retType = SMMonoHot::SignatureGetReturnType(sig);
    if (!retType || SMMonoHot::TypeGetType(retType) != 0x01) {
        if (sigOut) *sigOut = "ret";
        return false;
    }
    void *iter = nullptr;
    void *param0 = SMMonoHot::SignatureGetParams(sig, &iter);
    if (!param0) {
        if (sigOut) *sigOut = "argc0";
        return false;
    }
    if (SMMonoHot::SignatureGetParams(sig, &iter) != nullptr) {
        if (sigOut) *sigOut = "argc2";
        return false;
    }
    const int typeCode = SMMonoHot::TypeGetType(param0);
    char buf[48];
    snprintf(buf, sizeof(buf), "s1a1t%02x", (typeCode < 0 || typeCode > 0xff) ? 0xff : typeCode);
    if (sigOut) *sigOut = buf;
    return typeCode == 0x08 || typeCode == 0x09;   // MONO_TYPE_I4 / MONO_TYPE_U4
}

// --- 游戏原生瞬移入口：EntityHelper.AutoMoveTransfer(Vector3) ---
// 只允许在 Unity 线程调用（AutoMoveTransferNative / Transfer2EntityNative 负责封送）。
// 成功判据 = invoke 返回且没有托管异常；不做相机吸附、不做落地反射探针（见文件头）。
bool AutoMoveTransferDirect(Vector3 pos) {
    SMPlatform::SetBreadcrumbLight("tp:auto");
    g_tpLastOutcome.store(kSMTpNone, std::memory_order_relaxed);
    LOGI("[TeleportFix] AutoMoveTransferDirect pos=(%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);

    SMPlatform::SetBreadcrumbLight("tp:att");
    if (_monoGetRootDomain && _monoThreadAttach) {
        void *domain = _monoGetRootDomain();
        if (domain) _monoThreadAttach(domain);
    }
    SMPlatform::SetBreadcrumbLight("tp:att-done");

    // 明确不做 inline patch：EnsureTeleportAntiCrashPatched 在 Apple 上已是 no-op。
    SMPlatform::SetBreadcrumbLight("tp:patch-off");

    void *img = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
    if (!img) img = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity.dll"));
    if (!img) {
        SMPlatform::SetBreadcrumbLight("tp:no-img");
        LOGE("[TeleportFix] AutoMoveTransferDirect GAGAL: image XDTLevelAndEntity tidak ditemukan.");
        return false;
    }

    SMPlatform::SetBreadcrumbLight("tp:helper");
    void *helperClass = _monoClassFromName(img,
            (const char *) OBFUSCATE("XDTLevelAndEntity.Utils"),
            (const char *) OBFUSCATE("EntityHelper"));
    if (!helperClass) {
        SMPlatform::SetBreadcrumbLight("tp:no-helper");
        LOGE("[TeleportFix] AutoMoveTransferDirect GAGAL: EntityHelper class tidak ditemukan.");
        return false;
    }

    SMPlatform::SetBreadcrumbLight("tp:method");
    void *autoMoveMethod = FindMethodInHierarchy(helperClass,
            (const char *) OBFUSCATE("AutoMoveTransfer"), 1);
    if (!autoMoveMethod) {
        SMPlatform::SetBreadcrumbLight("tp:no-method");
        g_tpLastOutcome.store(kSMTpInvokeFail, std::memory_order_relaxed);
        LOGE("[TeleportFix] AutoMoveTransferDirect GAGAL: AutoMoveTransfer(1) tidak ditemukan.");
        return false;
    }

    // 零 invoke 的签名自检：不是 mono 静态单参 Vector3 就放弃，绝不盲调。
    SMPlatform::SetBreadcrumbLight("tp:sig");
    std::string sig;
    if (!SMTpCheckAutoMoveSignature(autoMoveMethod, &sig)) {
        SMPlatform::SetBreadcrumbLight("tp:sig-bad:" + sig);
        g_tpLastOutcome.store(kSMTpInvokeFail, std::memory_order_relaxed);
        LOGE("[TeleportFix] AutoMoveTransferDirect GAGAL: signature tidak cocok (%s).", sig.c_str());
        return false;
    }
    SMPlatform::SetBreadcrumbLight("tp:sig:" + sig);

    Vector3 targetPos = pos;
    void *args[] = { &targetPos };
    SMPlatform::SetBreadcrumbLight("tp:invoke");
    void *exc = nullptr;
    _monoRuntimeInvoke(autoMoveMethod, nullptr, args, &exc);
    SMPlatform::SetBreadcrumbLight("tp:invoke-done");

    if (exc) {
        SMPlatform::SetBreadcrumbLight("tp:fail:exc");
        g_tpLastOutcome.store(kSMTpInvokeFail, std::memory_order_relaxed);
        LOGE("[TeleportFix] Exception saat AutoMoveTransfer.");
        return false;
    }

    g_tpLastOutcome.store(kSMTpOk, std::memory_order_relaxed);
    SMPlatform::SetBreadcrumbLight("tp:ok");
    LOGI("[TeleportFix] AutoMoveTransfer ok -> (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
    return true;
}

// --- 实体传送：先取目标实体坐标走 AutoMoveTransfer（与 PC 版一致），
//     取不到坐标才退 EntityHelper.Transfer2Entity(netId)（游戏自己的封装）。 ---
void Transfer2EntityDirect(uint32_t entityNetId) {
    SMPlatform::SetBreadcrumbLight("tp:entity");
    if (entityNetId == 0) return;
    LOGI("[TeleportFix] Transfer2EntityDirect netId=%u", entityNetId);

    if (_monoGetRootDomain && _monoThreadAttach) {
        void *domain = _monoGetRootDomain();
        if (domain) _monoThreadAttach(domain);
    }

    void *img = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
    if (!img) img = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity.dll"));
    if (!img) {
        SMPlatform::SetBreadcrumbLight("tp:entity-no-img");
        LOGE("[TeleportFix] Transfer2EntityDirect: XDTLevelAndEntity belum diload.");
        return;
    }

    // 1) 目标实体坐标（读不到就走游戏自带的 Transfer2Entity）
    void *entitiesKlass = _monoClassFromName(img,
            (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"),
            (const char *) OBFUSCATE("Entities"));
    void *m_getAny = entitiesKlass
            ? FindMethodInHierarchy(entitiesKlass, (const char *) OBFUSCATE("GetAnyEntity"), 1)
            : nullptr;
    if (m_getAny && SMTpMethodIsStatic(m_getAny)) {
        uint32_t netId = entityNetId;
        void *args[] = { &netId };
        void *exc = nullptr;
        void *targetEntity = _monoRuntimeInvoke(m_getAny, nullptr, args, &exc);
        if (targetEntity && !exc) {
            void *entKlass = _monoObjectGetClass(targetEntity);
            void *m_getPos = entKlass
                    ? FindMethodInHierarchy(entKlass, (const char *) OBFUSCATE("get_position"), 0)
                    : nullptr;
            if (m_getPos) {
                void *posExc = nullptr;
                void *boxedPos = _monoRuntimeInvoke(m_getPos, targetEntity, nullptr, &posExc);
                void *raw = (boxedPos && !posExc) ? _monoObjectUnbox(boxedPos) : nullptr;
                if (raw) {
                    Vector3 targetPos{0.0f, 0.0f, 0.0f};
                    memcpy(&targetPos, raw, sizeof(float) * 3);
                    if (std::isfinite(targetPos.x) && std::isfinite(targetPos.y) &&
                        std::isfinite(targetPos.z)) {
                        LOGI("[TeleportFix] entity pos=(%.2f, %.2f, %.2f)",
                             targetPos.x, targetPos.y, targetPos.z);
                        AutoMoveTransferDirect(targetPos);
                        return;
                    }
                }
            }
        }
    } else if (m_getAny) {
        SMPlatform::SetBreadcrumbLight("tp:entity-any-skip");
    }

    // 2) 兜底：EntityHelper.Transfer2Entity(netId)（游戏实现 = GetAnyEntity + TransferAndLookAt）
    SMPlatform::SetBreadcrumbLight("tp:entity-fallback");
    void *helperClass = _monoClassFromName(img,
            (const char *) OBFUSCATE("XDTLevelAndEntity.Utils"),
            (const char *) OBFUSCATE("EntityHelper"));
    if (!helperClass) {
        SMPlatform::SetBreadcrumbLight("tp:entity-no-helper");
        return;
    }
    void *m_transfer = FindMethodInHierarchy(helperClass,
            (const char *) OBFUSCATE("Transfer2Entity"), 1);
    if (!m_transfer) {
        SMPlatform::SetBreadcrumbLight("tp:entity-no-method");
        LOGE("[TeleportFix] EntityHelper.Transfer2Entity(1) tidak ditemukan.");
        return;
    }
    std::string sig;
    if (!SMTpCheckEntityTransferSignature(m_transfer, &sig)) {
        SMPlatform::SetBreadcrumbLight("tp:entity-sig-bad:" + sig);
        LOGE("[TeleportFix] Transfer2Entity signature tidak cocok (%s).", sig.c_str());
        return;
    }
    uint32_t netId = entityNetId;
    void *args[] = { &netId };
    void *exc = nullptr;
    SMPlatform::SetBreadcrumbLight("tp:entity-invoke");
    _monoRuntimeInvoke(m_transfer, nullptr, args, &exc);
    if (exc) {
        SMPlatform::SetBreadcrumbLight("tp:entity-exc");
        LOGE("[TeleportFix] Exception saat invoke EntityHelper.Transfer2Entity.");
        return;
    }
    SMPlatform::SetBreadcrumbLight("tp:entity-ok");
    LOGI("[TeleportFix] EntityHelper.Transfer2Entity ok (netId=%u).", entityNetId);
}

// --- 对外入口：负责闸门 + 封送到 Unity 线程。所有传送 UI/循环都必须走这两个。 ---
bool AutoMoveTransferNative(Vector3 pos) {
    if (!SMTpWaitAreaReady(pos, 2000)) {
        g_tpLastOutcome.store(kSMTpPreFail, std::memory_order_relaxed);
        return false;
    }
    SMPlatform::SetBreadcrumbLight("tp:marshal");
    bool ok = false;
    RunOnUnityThreadSync([pos, &ok]() { ok = AutoMoveTransferDirect(pos); });
    if (!ok && g_tpLastOutcome.load(std::memory_order_relaxed) == kSMTpNone)
        g_tpLastOutcome.store(kSMTpPreFail, std::memory_order_relaxed);
    return ok;
}

void ManualTeleportNative(Vector3 pos) {
    AutoMoveTransferNative(pos);
}

void Transfer2EntityNative(uint32_t entityNetId) {
    if (entityNetId == 0) return;
    if (!SMTpWaitAreaReady(Vector3{0.0f, 0.0f, 0.0f}, 2000)) return;
    SMPlatform::SetBreadcrumbLight("tp:entity-marshal");
    RunOnUnityThreadSync([entityNetId]() { Transfer2EntityDirect(entityNetId); });
}

Vector3 GetSelfPositionNative() {
    _monoThreadAttach(_monoGetRootDomain());
    Vector3 pos = { 0.0f, 0.0f, 0.0f };
    void *img = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!img) return pos;
    void *utilKlass = _monoClassFromName(img, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
    if (!utilKlass) return pos;
    void *m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
    if (!m_getSelf) return pos;
    void *exc = nullptr;
    void *self = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, &exc);
    if (self && !exc) pos = NativeGetPosition(self);
    return pos;
}
static void CastFishingNative(int throwState, Vector3 tarPos) {
    _monoThreadAttach(_monoGetRootDomain());
    void *img = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!img) return;
    void *klass = _monoClassFromName(img, OBFUSCATE("XDTLevelAndEntity.GameplaySystem"), OBFUSCATE("GameplayApi"));
    if (!klass) return;
    void *m = FindMethodInHierarchy(klass, OBFUSCATE("EnterFishing"), 2);
    if (!m) return;
    int st = throwState; Vector3 p = tarPos;
    void *args[] = { &st, &p }; void *exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
}
static uint32_t GetSelfNetIdInline() {
    void *imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) return 0;
    void *kd = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol"), OBFUSCATE("PlayerDataCenter"));
    void *m = kd ? FindMethodInHierarchy(kd, OBFUSCATE("GetSelfNetPlayerId"), 0) : nullptr;
    if (!m) return 0;
    void *e = nullptr; void *r = _monoRuntimeInvoke(m, nullptr, nullptr, &e);
    if (r && !e) return *(uint32_t *) _monoObjectUnbox(r);
    return 0;
}

static uint64_t NowMsSC() {
    return (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ==================== AUTO CLEAN SEA POLLUTION ====================
static void *GetSeaCleanKlass() {
    static void *s_klass = nullptr;
    if (s_klass) return s_klass;
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) return nullptr;
    s_klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.SeaClean"), OBFUSCATE("SeaCleanProtocolManager"));
    return s_klass;
}
static void *SeaCleanMethod(const char *name, int paramCount) {
    void *k = GetSeaCleanKlass();
    return k ? FindMethodInHierarchy(k, name, paramCount) : nullptr;
}
static void SeaReqStartClean(uint32_t netId) {
    void *m = SeaCleanMethod(OBFUSCATE("ReqStartClean"), 1); if (!m) return;
    uint32_t n = netId; void *a[] = {&n}; void *e = nullptr;
    _monoRuntimeInvoke(m, nullptr, a, &e);
}
static void SeaReqStopClean(uint32_t netId) {
    void *m = SeaCleanMethod(OBFUSCATE("ReqStopClean"), 1); if (!m) return;
    uint32_t n = netId; void *a[] = {&n}; void *e = nullptr;
    _monoRuntimeInvoke(m, nullptr, a, &e);
}
static void SeaReqStartCleanPublic(uint32_t netId, bool qtePassed) {
    void *m = SeaCleanMethod(OBFUSCATE("ReqStartCleanPublicPollutant"), 2); if (!m) return;
    uint32_t n = netId; bool q = qtePassed; void *a[] = {&n, &q}; void *e = nullptr;
    _monoRuntimeInvoke(m, nullptr, a, &e);
}
static void SeaReqStopCleanPublic(uint32_t netId) {
    void *m = SeaCleanMethod(OBFUSCATE("ReqStopCleanPublicPollutant"), 1); if (!m) return;
    uint32_t n = netId; void *a[] = {&n}; void *e = nullptr;
    _monoRuntimeInvoke(m, nullptr, a, &e);
}
static void SeaReqExclusiveSync(uint32_t netId, int typeId, float hp, bool qtePassed, bool resetQteProb) {
    void *m = SeaCleanMethod(OBFUSCATE("ReqExclusiveSyncState"), 5); if (!m) return;
    uint32_t n = netId; int t = typeId; float h = hp; bool q = qtePassed; bool r = resetQteProb;
    void *a[] = {&n, &t, &h, &q, &r}; void *e = nullptr;
    _monoRuntimeInvoke(m, nullptr, a, &e);
}
static void SeaReqBodyCleanByBath() {
    void *m = SeaCleanMethod(OBFUSCATE("ReqBodyPollutionCleanByBath"), 0); if (!m) return;
    void *e = nullptr;
    _monoRuntimeInvoke(m, nullptr, nullptr, &e);
}
struct SeaPollutantView {
    bool valid = false;
    int typeId = 0;
    float currentHp = 0.0f;
    uint32_t hostNetId = 0;
    bool isShared = false;
    int subAreaPoint = 0;
    bool isHidden = false;
    float maxHp = 0.0f;
};
static bool ReadSeaPollutant(uint32_t netId, SeaPollutantView &v) {
    std::vector<char> buf; void *k = nullptr;
    if (!TryGetCompDataBuf(OBFUSCATE("SeaCleanMonsterComponentData"), netId, buf, k)) return false;
    int oType = CompFieldOff(k, OBFUSCATE("typeId"));
    int oHp = CompFieldOff(k, OBFUSCATE("currentHp"));
    int oHost = CompFieldOff(k, OBFUSCATE("hostNetId"));
    int oShr = CompFieldOff(k, OBFUSCATE("isShared"));
    int oSub = CompFieldOff(k, OBFUSCATE("subAreaPoint"));
    int oHid = CompFieldOff(k, OBFUSCATE("isHidden"));
    int oMax = CompFieldOff(k, OBFUSCATE("maxHp"));
    if (oType >= 0) v.typeId = *(int *) (buf.data() + oType);
    if (oHp >= 0) v.currentHp = *(float *) (buf.data() + oHp);
    if (oHost >= 0) v.hostNetId = *(uint32_t *) (buf.data() + oHost);
    if (oShr >= 0) v.isShared = *(bool *) (buf.data() + oShr);
    if (oSub >= 0) v.subAreaPoint = *(int *) (buf.data() + oSub);
    if (oHid >= 0) v.isHidden = *(bool *) (buf.data() + oHid);
    if (oMax >= 0) v.maxHp = *(float *) (buf.data() + oMax);
    v.valid = true;
    return true;
}
void AutoCleanSeaLoop() {
    LOGI(OBFUSCATE("[SeaClean] STARTED"));
    std::vector<uint32_t> cleaning;
    uint64_t lastBody = 0, lastLog = 0;

    while (g_autoCleanActive) {
        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());
            uint64_t now = NowMsSC();

            uint32_t selfId = GetSelfNetIdInline();
            Vector3 selfPos{}; bool haveSelf = (selfId && GetEntityPosByNetId(selfId, selfPos));

            std::vector<uint32_t> ids; GetAllNetIds(ids);
            std::vector<uint32_t> inRange;
            int hit = 0;

            for (uint32_t nid : ids) {
                SeaPollutantView v;
                if (!ReadSeaPollutant(nid, v)) continue;
                if (v.isHidden || v.currentHp <= 0.0f) continue;

                if (haveSelf) {
                    Vector3 pp{};
                    if (!GetEntityPosByNetId(nid, pp)) continue;
                    if (GetVector3Distance(selfPos, pp) > g_autoCleanRadius) continue;
                }
                inRange.push_back(nid);
                hit++;

                if (g_autoCleanMode == 0) {
                    if (!v.isShared) SeaReqExclusiveSync(nid, v.typeId, 0.0f, true, false);
                    else SeaReqStartCleanPublic(nid, true);
                } else {
                    bool already = false;
                    for (uint32_t c : cleaning) if (c == nid) { already = true; break; }
                    if (!already) {
                        if (!v.isShared) SeaReqStartClean(nid);
                        else SeaReqStartCleanPublic(nid, false);
                        cleaning.push_back(nid);
                    }
                }
            }

            if (g_autoCleanMode == 1) {
                for (size_t i = 0; i < cleaning.size();) {
                    uint32_t nid = cleaning[i];
                    bool stillIn = false;
                    for (uint32_t r : inRange) if (r == nid) { stillIn = true; break; }
                    if (!stillIn) {
                        SeaReqStopClean(nid);
                        SeaReqStopCleanPublic(nid);
                        cleaning.erase(cleaning.begin() + i);
                    } else i++;
                }
            }

            if (g_autoCleanBody && now - lastBody > 5000) {
                SeaReqBodyCleanByBath();
                lastBody = now;
            }

            if (now - lastLog > 3000) {
                LOGI("[SeaClean] mode=%s radius=%.0f target=%d", g_autoCleanMode == 0 ? "INSTANT" : "LEGIT", g_autoCleanRadius, hit);
                lastLog = now;
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(g_autoCleanIntervalMs));
    }

    if (!cleaning.empty()) {
        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());
            for (uint32_t nid : cleaning) { SeaReqStopClean(nid); SeaReqStopCleanPublic(nid); }
        });
    }
    LOGI(OBFUSCATE("[SeaClean] STOPPED"));
}

// ==================== AUTO OPEN BOXES ====================
void AutoOpenBoxes() {
    void *rootDomain = _monoGetRootDomain();
    if (rootDomain) _monoThreadAttach(rootDomain);
    void *protocolImg = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!protocolImg) return;

    void *protocolKlass = _monoClassFromName(protocolImg, OBFUSCATE("XDTDataAndProtocol.ProtocolService.BackPack"), OBFUSCATE("BackpackProtocolManager"));
    void *m_OpenChest = FindMethodInHierarchy(protocolKlass, OBFUSCATE("OpenChest"), 2);
    if (!m_OpenChest) { LOGE(OBFUSCATE("[Box] Method OpenChest tidak ditemukan!")); return; }

    void *sysImg = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    void *sysKlass = _monoClassFromName(sysImg, OBFUSCATE("XDTGameSystem.GameplaySystem.BackPack"), OBFUSCATE("BackPackSystem"));

    void *m_getInst = FindMethodInHierarchy(sysKlass, OBFUSCATE("get_Instance"), 0);
    void *sysInst = m_getInst ? _monoRuntimeInvoke(m_getInst, nullptr, nullptr, nullptr) : nullptr;

    void *m_GetAllItem = FindMethodInHierarchy(sysKlass, OBFUSCATE("GetAllItem"), 1);
    if (!m_GetAllItem) { LOGE(OBFUSCATE("[Box] Method GetAllItem tidak ditemukan!")); return; }
    int storageType = 1;
    void *argsGetAll[1] = {&storageType};
    void *listObj = _monoRuntimeInvoke(m_GetAllItem, sysInst, argsGetAll, nullptr);
    if (!listObj) { LOGE(OBFUSCATE("[Box] listObj NULL")); return; }

    void *listKlass = _monoObjectGetClass(listObj);
    void *sizeField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_size"));
    void *itemsField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_items"));

    int size = 0;
    if (sizeField) _monoFieldGetValue(listObj, sizeField, (void **) &size);

    void *itemsArray = nullptr;
    if (itemsField) _monoFieldGetValue(listObj, itemsField, &itemsArray);

    if (!itemsArray || size == 0) { LOGI("[Box] Tidak ada item di backpack (size=%d)", size); return; }

    void *bpItemKlass = _monoClassFromName(sysImg, OBFUSCATE("XDTGameSystem.UISystem.BackPack"), OBFUSCATE("BackpackItem"));
    uint32_t align = 0;
    int structSize = bpItemKlass ? _monoClassValueSize(bpItemKlass, &align) : 0;
    if (structSize <= 0) { LOGE(OBFUSCATE("[Box] structSize invalid")); return; }

    uintptr_t arrayDataStart = (uintptr_t) itemsArray + 0x20;
    int totalOpened = 0;
    LOGI("[Box] Mulai Proses Auto Open Box... (size=%d)", size);

    for (int i = 0; i < size; i++) {
        uintptr_t structAddr = arrayDataStart + (uintptr_t) i * structSize;
        uint32_t netId = *(uint32_t *) (structAddr + 0x00);
        int staticId = *(int *) (structAddr + 0x04);
        int count = *(int *) (structAddr + 0x14);

        if (staticId == 13301 || staticId == 13302 || staticId == 13303) {
            LOGI("[Box] #%d StaticID=%d Isi=%d", i, staticId, count);
            for (int c = 0; c < count; c++) {
                int selectIndex = 0;
                void *argsOpen[2] = {&netId, &selectIndex};
                _monoRuntimeInvoke(m_OpenChest, nullptr, argsOpen, nullptr);
                totalOpened++;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
    LOGI("[Box] === Selesai! Berhasil membuka %d Box! ===", totalOpened);
}

// ===== Auto Clear Task (Quest) =====
static void *GetTaskProtocolKlass() {
    static void *s_klass = nullptr;
    if (s_klass) return s_klass;
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) return nullptr;
    s_klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Task"), OBFUSCATE("TaskProtocolManager"));
    return s_klass;
}
static void *TaskMethod(const char *name, int paramCount) {
    void *k = GetTaskProtocolKlass();
    return k ? FindMethodInHierarchy(k, name, paramCount) : nullptr;
}
static int TaskGetCurTrackTaskId() {
    void *m = TaskMethod(OBFUSCATE("GetCurTrackTaskId"), 0); if (!m) return 0;
    void *e = nullptr;
    void *res = _monoRuntimeInvoke(m, nullptr, nullptr, &e);
    return (e || !res) ? 0 : *(int *) _monoObjectUnbox(res);
}
static int TaskGetState(int taskStaticId) {
    void *m = TaskMethod(OBFUSCATE("GetTaskState"), 1); if (!m) return 0;
    int id = taskStaticId; void *a[] = {&id}; void *e = nullptr;
    void *res = _monoRuntimeInvoke(m, nullptr, a, &e);
    return (e || !res) ? 0 : *(int *) _monoObjectUnbox(res);
}
static void TaskInvoke1(const char *name, int taskStaticId) {
    void *m = TaskMethod(name, 1); if (!m) return;
    int id = taskStaticId; void *a[] = {&id}; void *e = nullptr;
    _monoRuntimeInvoke(m, nullptr, a, &e);
}

static uint32_t TaskGetCurrentTrackedNetId() {
    void *img = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!img) return 0;
    void *klass = _monoClassFromName(img,
        OBFUSCATE("XDTGameSystem.GameplaySystem.Quest"), OBFUSCATE("TaskSystem"));
    if (!klass) return 0;
    void *mInstance = FindMethodInHierarchy(klass, OBFUSCATE("get_Instance"), 0);
    void *mTracked = FindMethodInHierarchy(klass, OBFUSCATE("GetCurrentTrackedTaskNetId"), 0);
    if (!mInstance || !mTracked) return 0;
    void *exc = nullptr;
    void *instance = _monoRuntimeInvoke(mInstance, nullptr, nullptr, &exc);
    if (exc || !instance) return 0;
    exc = nullptr;
    void *result = _monoRuntimeInvoke(mTracked, instance, nullptr, &exc);
    return (exc || !result) ? 0u : *(uint32_t *)_monoObjectUnbox(result);
}

static bool QuestValidPosition(const Vector3 &p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
           std::fabs(p.x) < 100000.0f && std::fabs(p.y) < 100000.0f &&
           std::fabs(p.z) < 100000.0f;
}

static bool TaskTryGetHudWaypoint(int taskStaticId, uint32_t taskNetId,
                                  const Vector3 &playerPos, Vector3 &outPos) {
    if (!_monoFieldGetValueObject) return false;
    void *img = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!img) return false;
    void *klass = _monoClassFromName(img,
        OBFUSCATE("XDTGameSystem.GameplaySystem.Navigation"), OBFUSCATE("TrackingSystem"));
    if (!klass) return false;
    void *mInstance = FindMethodInHierarchy(klass, OBFUSCATE("get_Instance"), 0);
    void *mItems = FindMethodInHierarchy(klass, OBFUSCATE("GetTrackingItems"), 0);
    if (!mInstance || !mItems) return false;

    void *exc = nullptr;
    void *instance = _monoRuntimeInvoke(mInstance, nullptr, nullptr, &exc);
    if (exc || !instance) return false;
    exc = nullptr;
    void *list = _monoRuntimeInvoke(mItems, instance, nullptr, &exc);
    if (exc || !list) return false;
    void *listKlass = _monoObjectGetClass(list);
    void *mCount = listKlass ? FindMethodInHierarchy(listKlass, OBFUSCATE("get_Count"), 0) : nullptr;
    void *mItem = listKlass ? FindMethodInHierarchy(listKlass, OBFUSCATE("get_Item"), 1) : nullptr;
    if (!mCount || !mItem) return false;
    void *countObj = _monoRuntimeInvoke(mCount, list, nullptr, &exc);
    if (exc || !countObj) return false;
    int count = *(int *)_monoObjectUnbox(countObj);
    if (count <= 0 || count > 1024) return false;

    bool found = false;
    float best = FLT_MAX;
    void *domain = _monoGetRootDomain();
    for (int i = 0; i < count; ++i) {
        int index = i; void *args[] = {&index}; exc = nullptr;
        void *item = _monoRuntimeInvoke(mItem, list, args, &exc);
        if (exc || !item) continue;
        void *itemKlass = _monoObjectGetClass(item);
        void *fData = itemKlass ? FindFieldInHierarchy(itemKlass, OBFUSCATE("TrackData")) : nullptr;
        if (!fData) continue;
        void *data = _monoFieldGetValueObject(domain, fData, item);
        if (!data) continue;
        void *dataKlass = _monoObjectGetClass(data);
        void *fPos = dataKlass ? FindFieldInHierarchy(dataKlass, OBFUSCATE("Position")) : nullptr;
        void *fTask = dataKlass ? FindFieldInHierarchy(dataKlass, OBFUSCATE("TaskTableId")) : nullptr;
        void *fTarget = dataKlass ? FindFieldInHierarchy(dataKlass, OBFUSCATE("TargetNetId")) : nullptr;
        if (!fPos || !fTask || !fTarget) continue;
        Vector3 pos{}; int tableId = 0; uint32_t targetNetId = 0;
        _monoFieldGetValue(data, fPos, &pos);
        _monoFieldGetValue(data, fTask, &tableId);
        _monoFieldGetValue(data, fTarget, &targetNetId);
        if (tableId != taskStaticId && (taskNetId == 0 || targetNetId != taskNetId)) continue;
        if (!QuestValidPosition(pos)) continue;
        float dx = pos.x - playerPos.x, dy = pos.y - playerPos.y, dz = pos.z - playerPos.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq < best) { best = distSq; outPos = pos; found = true; }
    }
    return found;
}

static void *QuestGetActivePanel(const char *panelName) {
    void *img = _monoImageLoaded(OBFUSCATE("XDTGameUI"));
    if (!img) return nullptr;
    void *uiKlass = _monoClassFromName(img, OBFUSCATE("XDTGame.Core"), OBFUSCATE("UIManager"));
    void *panelKlass = _monoClassFromName(img, OBFUSCATE("XDTGame.UI.Panel"), panelName);
    if (!uiKlass || !panelKlass) return nullptr;
    void *mInstance = FindMethodInHierarchy(uiKlass, OBFUSCATE("get_Instance"), 0);
    void *mGetView = FindMethodInHierarchy(uiKlass, OBFUSCATE("GetView"), 0);
    if (!mInstance || !mGetView) return nullptr;
    void *exc = nullptr;
    void *ui = _monoRuntimeInvoke(mInstance, nullptr, nullptr, &exc);
    if (exc || !ui) return nullptr;
    void *typeArgs[] = {_monoClassGetType(panelKlass)};
    void *inst = _mono_metadata_get_generic_inst(1, typeArgs);
    MonoGenericContext ctx = {nullptr, inst};
    void *closed = _mono_class_inflate_generic_method(mGetView, &ctx);
    if (!closed) return nullptr;
    exc = nullptr;
    void *panel = _monoRuntimeInvoke(closed, ui, nullptr, &exc);
    return exc ? nullptr : panel;
}

static std::atomic<int64_t> g_questUiTransitionUntil{0};
static int64_t QuestNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static bool QuestTryAdvanceDialogue() {
    static int64_t lastInvoke = 0;
    int64_t now = QuestNowMs();
    if (now - lastInvoke < 500) return false;
    void *panel = QuestGetActivePanel(OBFUSCATE("DialoguePanel"));
    if (!panel) return false;
    void *method = FindMethodInHierarchy(_monoObjectGetClass(panel), OBFUSCATE("_OnTapShortcut"), 0);
    if (!method) return false;
    lastInvoke = now;
    g_questUiTransitionUntil.store(now + 2500);
    void *exc = nullptr;
    _monoRuntimeInvoke(method, panel, nullptr, &exc);
    return exc == nullptr;
}

static bool QuestTrySkipCutscene() {
    static void *attemptedVideo = nullptr;
    static void *attemptedSkip = nullptr;
    static int64_t noPanelSince = 0;
    void *video = QuestGetActivePanel(OBFUSCATE("UIVideoPanel"));
    void *skip = QuestGetActivePanel(OBFUSCATE("SkipPanel"));
    int64_t now = QuestNowMs();
    if (!video && !skip) {
        if (!noPanelSince) noPanelSince = now;
        if (now - noPanelSince > 1000) { attemptedVideo = nullptr; attemptedSkip = nullptr; }
        return false;
    }
    noPanelSince = 0;
    if (video && video != attemptedVideo) {
        attemptedVideo = video;
        void *method = FindMethodInHierarchy(_monoObjectGetClass(video), OBFUSCATE("Skip"), 0);
        if (method) {
            g_questUiTransitionUntil.store(now + 5000);
            void *exc = nullptr; _monoRuntimeInvoke(method, video, nullptr, &exc);
            if (!exc) return true;
        }
    }
    if (!skip || skip == attemptedSkip) return false;
    attemptedSkip = skip;
    void *fData = FindFieldInHierarchy(_monoObjectGetClass(skip), OBFUSCATE("_panelData"));
    if (!fData) return false;
    void *data = nullptr; _monoFieldGetValue(skip, fData, &data);
    if (!data) return false;
    void *fCallback = FindFieldInHierarchy(_monoObjectGetClass(data), OBFUSCATE("skipCallback"));
    void *callback = nullptr;
    if (fCallback) _monoFieldGetValue(data, fCallback, &callback);
    if (!callback) return false;
    void *invoke = FindMethodInHierarchy(_monoObjectGetClass(callback), OBFUSCATE("Invoke"), 0);
    if (!invoke) return false;
    g_questUiTransitionUntil.store(now + 5000);
    void *exc = nullptr; _monoRuntimeInvoke(invoke, callback, nullptr, &exc);
    return exc == nullptr;
}

void AutoQuestUiLoop() {
    static std::atomic<bool> running{false};
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) return;
    while (g_autoQuestDialogue || g_autoQuestCutsceneSkip) {
        RunOnUnityThreadSync([]() {
            _monoThreadAttach(_monoGetRootDomain());
            bool skipped = g_autoQuestCutsceneSkip && QuestTrySkipCutscene();
            if (!skipped && g_autoQuestDialogue) QuestTryAdvanceDialogue();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    running.store(false);
}

void AutoClearTaskLoop() {
    int64_t lastTeleport = 0;
    Vector3 lastCandidate{};
    int lastTaskId = 0;
    int stableReads = 0;
    while (g_autoClearTaskActive) {
        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());
            if (!g_autoQuestTeleport) {
                g_questFollowStatus.store((int)QuestFollowStatus::Off, std::memory_order_relaxed);
            }
            int id = TaskGetCurTrackTaskId();
            if (!id) {
                if (g_autoQuestTeleport)
                    g_questFollowStatus.store((int)QuestFollowStatus::WaitingForTask, std::memory_order_relaxed);
                return;
            }
            int state = TaskGetState(id);
            if (state == 2) { TaskInvoke1(OBFUSCATE("ClientAcceptTask"), id); return; }
            if (state == 4) { TaskInvoke1(OBFUSCATE("ClientSubmitTask"), id); return; }
            if (!g_autoQuestTeleport) return;
            if (state != 3 || g_instantFarmingActive || g_toolRestoring.load()) {
                g_questFollowStatus.store((int)QuestFollowStatus::WaitingForAcceptedState, std::memory_order_relaxed);
                return;
            }

            int64_t now = QuestNowMs();
            if (now - lastTeleport < 3000 || now < g_questUiTransitionUntil.load()) return;
            Vector3 playerPos = GetSelfPositionNative();
            if (!QuestValidPosition(playerPos) ||
                (playerPos.x == 0.0f && playerPos.y == 0.0f && playerPos.z == 0.0f)) {
                g_questFollowStatus.store((int)QuestFollowStatus::PlayerPositionFailed, std::memory_order_relaxed);
                return;
            }
            Vector3 waypoint{};
            if (!TaskTryGetHudWaypoint(id, TaskGetCurrentTrackedNetId(), playerPos, waypoint)) {
                g_questFollowStatus.store((int)QuestFollowStatus::NoMatchingHudWaypoint, std::memory_order_relaxed);
                return;
            }

            float cx = waypoint.x - lastCandidate.x;
            float cy = waypoint.y - lastCandidate.y;
            float cz = waypoint.z - lastCandidate.z;
            bool same = lastTaskId == id && (cx * cx + cy * cy + cz * cz) <= 1.0f;
            if (same) ++stableReads;
            else { lastTaskId = id; lastCandidate = waypoint; stableReads = 1; }
            if (stableReads < 2) {
                g_questFollowStatus.store((int)QuestFollowStatus::WaitingForStableWaypoint, std::memory_order_relaxed);
                return;
            }

            float dx = waypoint.x - playerPos.x;
            float dy = waypoint.y - playerPos.y;
            float dz = waypoint.z - playerPos.z;
            float distanceSq = dx * dx + dy * dy + dz * dz;
            float threshold = std::max(2.0f, g_autoQuestTeleportDistance);
            if (!std::isfinite(distanceSq)) {
                g_questFollowStatus.store((int)QuestFollowStatus::NoMatchingHudWaypoint, std::memory_order_relaxed);
                return;
            }
            if (distanceSq <= threshold * threshold) {
                g_questFollowStatus.store((int)QuestFollowStatus::Teleported, std::memory_order_relaxed);
                return;
            }
            const bool moved = AutoMoveTransferNative(waypoint);
            g_questFollowStatus.store((int)(moved ? QuestFollowStatus::Teleported
                                                  : QuestFollowStatus::TeleportFailed),
                                      std::memory_order_relaxed);
            if (moved) lastTeleport = now;
        });
        int gap = std::max(100, g_autoClearTaskIntervalMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(gap));
    }
    g_questFollowStatus.store((int)(g_autoQuestTeleport ? QuestFollowStatus::WorkerStopped
                                                        : QuestFollowStatus::Off),
                              std::memory_order_relaxed);
}

// ===================== PET =====================
static void *ReadListSizeItems(void *listObj, int &sizeOut) {
    sizeOut = 0;
    if (!listObj) return nullptr;
    void *lk = _monoObjectGetClass(listObj);
    if (!lk) return nullptr;
    void *fSize = _monoClassGetFieldFromName(lk, OBFUSCATE("_size"));
    void *fItems = _monoClassGetFieldFromName(lk, OBFUSCATE("_items"));
    if (!fSize || !fItems) return nullptr;
    _monoFieldGetValue(listObj, fSize, (void **) &sizeOut);
    void *arr = nullptr;
    _monoFieldGetValue(listObj, fItems, &arr);
    return arr;
}

static int FindPetCompOff(void *klass, void *petCompKlass) {
    if (!klass) return -1;
    void *iter = nullptr; void *f = nullptr;
    while ((f = _monoClassGetFields(klass, &iter)) != nullptr) {
        void *t = _monoFieldGetType(f);
        void *fk = t ? _monoClassFromMonoType(t) : nullptr;
        if (petCompKlass && fk == petCompKlass)
            return _monoFieldGetOffset(f) - 16;
    }
    int o = CompFieldOff(klass, OBFUSCATE("petComponentData")); if (o >= 0) return o;
    o = CompFieldOff(klass, OBFUSCATE("PetComponentData"));     if (o >= 0) return o;
    o = CompFieldOff(klass, OBFUSCATE("_petComponentData"));    if (o >= 0) return o;
    return -1;
}

static void ScanMyPets(std::vector<MyPet> &out) {
    out.clear();
    void *imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    void *petCompKlass = imgData ? _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("PetComponentData")) : nullptr;

    int offMine  = petCompKlass ? CompFieldOff(petCompKlass, OBFUSCATE("isMine"))     : -1;
    int offOwner = petCompKlass ? CompFieldOff(petCompKlass, OBFUSCATE("ownerNetId")) : -1;
    int offBreed = petCompKlass ? CompFieldOff(petCompKlass, OBFUSCATE("breedName"))  : -1;

    uint32_t self = GetSelfNetIdInline();

    static int s_offDog = -2, s_offMeow = -2;

    std::vector<uint32_t> ids; GetAllNetIds(ids);
    for (uint32_t nid : ids) {
        if (nid == 0) continue;
        std::vector<char> buf; void *k = nullptr;
        bool isDog = false, isPet = false; int offPet = -1;

        if (TryGetCompDataBuf(OBFUSCATE("DogComponentData"), nid, buf, k)) {
            isDog = true; isPet = true;
            if (s_offDog == -2) s_offDog = FindPetCompOff(k, petCompKlass);
            offPet = s_offDog;
        } else if (TryGetCompDataBuf(OBFUSCATE("MeowComponentData"), nid, buf, k)) {
            isDog = false; isPet = true;
            if (s_offMeow == -2) s_offMeow = FindPetCompOff(k, petCompKlass);
            offPet = s_offMeow;
        }
        if (!isPet || offPet < 0) continue;

        bool mine = false;
        if (offMine >= 0 && (size_t) (offPet + offMine + 1) <= buf.size())
            mine = (*(uint8_t *) (buf.data() + offPet + offMine) != 0);
        if (offOwner >= 0 && (size_t) (offPet + offOwner + 4) <= buf.size()) {
            uint32_t owner = *(uint32_t *) (buf.data() + offPet + offOwner);
            if (owner != 0 && self != 0) mine = (owner == self);
        }
        if (!mine) continue;

        MyPet mp{}; mp.netId = nid; mp.isDog = isDog;
        if (offBreed >= 0 && (size_t) (offPet + offBreed + (int) sizeof(void *)) <= buf.size()) {
            void *sp = *(void **) (buf.data() + offPet + offBreed);
            if (sp) mp.name = ReadMonoStringNative(sp);
        }
        out.push_back(mp);
    }
}

void ScanNearbyPets() {
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        std::vector<MyPet> pets; ScanMyPets(pets);
        g_myPets.swap(pets);
        if (g_selectedPetIndex >= (int) g_myPets.size()) g_selectedPetIndex = 0;
        LOGI("[Pet] scan selesai, pet milik kita = %d", (int) g_myPets.size());
    });
}

void DumpMyPets() {
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());

        void *imgSys = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
        void *petSysKlass = imgSys ? _monoClassFromName(imgSys, OBFUSCATE("XDTGameSystem.GameplaySystem.Pet"), OBFUSCATE("PetSystem")) : nullptr;
        LOGI("[PetDiag] petSysKlass=%p", petSysKlass);
        if (!petSysKlass) return;

        void *m_getInst = FindMethodInHierarchy(petSysKlass, OBFUSCATE("get_Instance"), 0);
        void *petSysInst = m_getInst ? _monoRuntimeInvoke(m_getInst, nullptr, nullptr, nullptr) : nullptr;
        LOGI("[PetDiag] petSysInst=%p", petSysInst);
        if (!petSysInst) return;

        void *fNet = _monoClassGetFieldFromName(petSysKlass, OBFUSCATE("_netIds"));
        void *fDat = _monoClassGetFieldFromName(petSysKlass, OBFUSCATE("_petComponentDatas"));
        void *netList = nullptr, *datList = nullptr;
        if (fNet) _monoFieldGetValue(petSysInst, fNet, &netList);
        if (fDat) _monoFieldGetValue(petSysInst, fDat, &datList);
        LOGI("[PetDiag] netList=%p datList=%p", netList, datList);

        int nCount = 0, dCount = 0;
        void *netArr = ReadListSizeItems(netList, nCount);
        void *datArr = ReadListSizeItems(datList, dCount);
        LOGI("[PetDiag] _netIds.size=%d _petComponentDatas.size=%d (netArr=%p datArr=%p)", nCount, dCount, netArr, datArr);

        void *imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
        void *petCompKlass = imgData ? _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ComponentsData"), OBFUSCATE("PetComponentData")) : nullptr;
        LOGI("[PetDiag] PetComponentData klass=%p", petCompKlass);
        if (petCompKlass) {
            uint32_t align = 0; int ss = _monoClassValueSize(petCompKlass, &align);
            int oMine = CompFieldOff(petCompKlass, OBFUSCATE("isMine"));
            int oOwn  = CompFieldOff(petCompKlass, OBFUSCATE("ownerNetId"));
            int oOwned = CompFieldOff(petCompKlass, OBFUSCATE("isOwned"));
            int oBreed = CompFieldOff(petCompKlass, OBFUSCATE("breedName"));
            LOGI("[PetDiag] structSize=%d off isMine=%d ownerNetId=%d isOwned=%d breedName=%d", ss, oMine, oOwn, oOwned, oBreed);

            if (datArr && ss > 0) {
                uintptr_t netData = netArr ? (uintptr_t) netArr + 0x20 : 0;
                uintptr_t datData = (uintptr_t) datArr + 0x20;
                int show = dCount < 10 ? dCount : 10;
                for (int i = 0; i < show; i++) {
                    uintptr_t elem = datData + (uintptr_t) i * (uintptr_t) ss;
                    uint32_t nid = netData ? *(uint32_t *) (netData + (uintptr_t) i * 4) : 0;
                    int mineB = (oMine >= 0) ? *(uint8_t *) (elem + oMine) : -1;
                    int ownedB = (oOwned >= 0) ? *(uint8_t *) (elem + oOwned) : -1;
                    uint32_t own = (oOwn >= 0) ? *(uint32_t *) (elem + oOwn) : 0;
                    LOGI("[PetDiag] elem#%d nid=%u isMine=%d isOwned=%d ownerNetId=%u", i, nid, mineB, ownedB, own);
                }
            }
        }

        std::vector<uint32_t> ids; GetAllNetIds(ids);
        int dog = 0, meow = 0;
        for (uint32_t nid : ids) {
            std::vector<char> b; void *k = nullptr;
            if (TryGetCompDataBuf(OBFUSCATE("DogComponentData"), nid, b, k)) dog++;
            else if (TryGetCompDataBuf(OBFUSCATE("MeowComponentData"), nid, b, k)) meow++;
        }
        LOGI("[PetDiag] via component-scan: totalEntities=%d Dog=%d Meow=%d", (int) ids.size(), dog, meow);
    });
}

static void *GetPetProtoKlass() {
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) return nullptr;
    return _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Pet"), OBFUSCATE("PetProtocolManager"));
}

static void PetProto1(const char *name, uint32_t netId) {
    void *k = GetPetProtoKlass();
    if (!k) { LOGE("[Pet] PetProto1(%s) batal: PetProtocolManager klass tidak ditemukan", name); return; }
    void *m = FindMethodInHierarchy(k, name, 1);
    if (!m) { LOGE("[Pet] PetProto1(%s) batal: method tidak ditemukan (1 param)", name); return; }
    uint32_t n = netId; void *args[] = {&n}; void *exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
    if (exc) LOGE("[Pet] PetProto1(%s) melempar exception (netId=%u)", name, netId);
    else LOGI("[Pet] PetProto1(%s) terkirim OK (netId=%u)", name, netId);
}

static void PetBeginFeed(uint32_t petNetId, uint32_t foodNetId, uint32_t qty, uint32_t toolNetId) {
    void *k = GetPetProtoKlass();
    if (!k) { LOGE(OBFUSCATE("[Pet] feed batal: PetProtocolManager klass tidak ditemukan")); return; }
    void *m = FindMethodInHierarchy(k, OBFUSCATE("BeginFeed"), 3);
    if (!m) { LOGE(OBFUSCATE("[Pet] feed batal: method BeginFeed(3 param) tidak ditemukan")); return; }

    if (qty < 1) qty = 1; if (qty > 200) qty = 200;

    void *domain = _monoGetRootDomain();

    void *sig = _monoMethodSignature(m);
    void *iter = nullptr;
    void *tPet = _monoSignatureGetParams(sig, &iter);
    void *uintKlass = tPet ? _monoClassFromMonoType(tPet) : nullptr;
    if (!uintKlass) {
        void *corlib = _monoImageLoaded(OBFUSCATE("mscorlib"));
        if (!corlib) corlib = _monoImageLoaded(OBFUSCATE("System.Private.CoreLib"));
        uintKlass = corlib ? _monoClassFromName(corlib, OBFUSCATE("System"), OBFUSCATE("UInt32")) : nullptr;
    }
    if (!uintKlass) { LOGE(OBFUSCATE("[Pet] feed batal: klass uint tidak ditemukan")); return; }

    static void *(*s_monoArrayNew)(void *, void *, uintptr_t) = nullptr;
    if (!s_monoArrayNew) {
        // Pakai flag 0 dulu (sama seperti pemanggilan xdl_open yang sudah
        // terbukti berhasil saat resolve fungsi mono_* lain di startup),
        // baru coba RTLD_NOW/XDL_TRY_FORCE_LOAD sebagai fallback.
        void *hMonoLib = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), 0);
        if (!hMonoLib) hMonoLib = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), RTLD_NOW);
        if (!hMonoLib) hMonoLib = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), XDL_TRY_FORCE_LOAD);
        if (!hMonoLib) { LOGE(OBFUSCATE("[Pet] feed batal: libmonosgen-2.0.so tidak ditemukan via xdl_open")); return; }

        s_monoArrayNew = (void *(*)(void *, void *, uintptr_t)) xdl_sym(hMonoLib, (const char *) OBFUSCATE("mono_array_new"), (size_t *) nullptr);
        if (!s_monoArrayNew) {
            // Beberapa build hanya expose simbol ini lewat full symbol table.
            void *hMonoLib2 = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), XDL_TRY_FORCE_LOAD);
            if (hMonoLib2)
                s_monoArrayNew = (void *(*)(void *, void *, uintptr_t)) xdl_sym(hMonoLib2, (const char *) OBFUSCATE("mono_array_new"), (size_t *) nullptr);
        }
        if (!s_monoArrayNew) {
            LOGE(OBFUSCATE("[Pet] feed batal: mono_array_new tidak ditemukan lewat xdl_sym (library ketemu tapi simbol tidak ada)"));
            return;
        }
        LOGI(OBFUSCATE("[Pet] mono_array_new berhasil di-resolve"));
    }
    if (!s_monoArrayNew) { LOGE(OBFUSCATE("[Pet] feed batal: mono_array_new tidak ditemukan")); return; }

    void *foodArr = s_monoArrayNew(domain, uintKlass, (uintptr_t) qty);
    if (!foodArr) { LOGE(OBFUSCATE("[Pet] feed batal: gagal alokasi array makanan")); return; }

    uint32_t *vec = (uint32_t *) ((char *) foodArr + 4 * sizeof(void *));
    for (uint32_t i = 0; i < qty; i++) vec[i] = foodNetId;

    uint32_t n = petNetId, t = toolNetId;
    void *args[] = {&n, foodArr, &t}; void *exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc);
    if (exc) LOGE("[Pet] BeginFeed melempar exception (netId=%u food=%u qty=%u)", petNetId, foodNetId, qty);
    else LOGI("[Pet] BeginFeed terkirim OK (netId=%u food=%u qty=%u)", petNetId, foodNetId, qty);
}

static uint32_t ResolvePetFoodNetId(bool isDog) {
    static const int dogFoodStaticId = 96000;
    static const int catFoodStaticId = 4021;
    static const int universalFoodStaticId = 823000;
    auto find = [](int sid) -> uint32_t { return sid > 0 ? FindBackpackItemNetId(sid) : 0; };
    uint32_t n = 0; int triedSid = 0;
    switch (g_petFoodMode) {
        case 1: triedSid = universalFoodStaticId; n = find(triedSid); break;
        case 2: triedSid = dogFoodStaticId; n = find(triedSid); break;
        case 3: triedSid = catFoodStaticId; n = find(triedSid); break;
        default: triedSid = isDog ? dogFoodStaticId : catFoodStaticId; n = find(triedSid); break;
    }
    if (n == 0) {
        LOGE("[Pet] food staticId=%d tidak ada di backpack, coba universal (staticId=%d)", triedSid, universalFoodStaticId);
        n = find(universalFoodStaticId);
    }
    if (n == 0) LOGE(OBFUSCATE("[Pet] food batal: staticId dog=96000/cat=4021/universal=823000 semua TIDAK ADA di backpack -> jalankan 'Dump Food (debug)' untuk cek staticId asli"));
    else LOGI("[Pet] food ditemukan netId=%u", n);
    return n;
}

static bool GetSelectedPet(MyPet &out) {
    if (g_selectedPetIndex < 0 || g_selectedPetIndex >= (int) g_myPets.size()) return false;
    out = g_myPets[g_selectedPetIndex];
    return out.netId != 0;
}

void DumpPetFoodCandidates() {
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        void *img = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
        if (!img) { LOGE(OBFUSCATE("[PetFoodDiag] image XDTGameSystem null")); return; }
        void *klass = _monoClassFromName(img, OBFUSCATE("XDTGameSystem.GameplaySystem.BackPack"), OBFUSCATE("BackPackSystem"));
        if (!klass) { LOGE(OBFUSCATE("[PetFoodDiag] BackPackSystem klass null")); return; }
        void *m_getInst = FindMethodInHierarchy(klass, OBFUSCATE("get_Instance"), 0);
        void *inst = m_getInst ? _monoRuntimeInvoke(m_getInst, nullptr, nullptr, nullptr) : nullptr;
        if (!inst) { LOGE(OBFUSCATE("[PetFoodDiag] BackPackSystem instance null")); return; }

        void *m_GetAllItem = FindMethodInHierarchy(klass, OBFUSCATE("GetAllItem"), 1);
        if (!m_GetAllItem) { LOGE(OBFUSCATE("[PetFoodDiag] GetAllItem method null")); return; }
        int storageType = 1;
        void *args[1] = {&storageType};
        void *exc = nullptr;
        void *listObj = _monoRuntimeInvoke(m_GetAllItem, inst, args, &exc);
        if (!listObj || exc) { LOGE(OBFUSCATE("[PetFoodDiag] GetAllItem gagal")); return; }

        void *listKlass = _monoObjectGetClass(listObj);
        void *sizeField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_size"));
        void *itemsField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_items"));
        if (!sizeField || !itemsField) { LOGE(OBFUSCATE("[PetFoodDiag] list fields null")); return; }
        int size = 0;
        _monoFieldGetValue(listObj, sizeField, (void **) &size);
        void *itemsArray = nullptr;
        _monoFieldGetValue(listObj, itemsField, &itemsArray);
        LOGI("[PetFoodDiag] total backpack item slot = %d", size);
        if (!itemsArray || size == 0) return;

        void *bpItemKlass = _monoClassFromName(img, OBFUSCATE("XDTGameSystem.UISystem.BackPack"), OBFUSCATE("BackpackItem"));
        if (!bpItemKlass) { LOGE(OBFUSCATE("[PetFoodDiag] BackpackItem klass null")); return; }
        uint32_t align = 0;
        int structSize = _monoClassValueSize(bpItemKlass, &align);
        void *fEntity = _monoClassGetFieldFromName(bpItemKlass, OBFUSCATE("entityType"));
        int offEntity = fEntity ? (_monoFieldGetOffset(fEntity) - MONO_FIELD_HEADER) : -1;
        void *m_getName = FindMethodInHierarchy(bpItemKlass, OBFUSCATE("get_Name"), 0);

        uintptr_t arrayDataStart = (uintptr_t) itemsArray + OFF_ARR_DATA;
        int shown = 0;
        for (int i = 0; i < size; i++) {
            uintptr_t structAddr = arrayDataStart + (i * structSize);
            uint32_t netId = *(uint32_t *) (structAddr + OFF_BP_NETID);
            int staticId = *(int *) (structAddr + OFF_BP_STATICID);
            int count = *(int *) (structAddr + OFF_BP_COUNT);
            int et = (offEntity >= 0) ? *(int *) (structAddr + offEntity) : -1;
            if (netId == 0 || count <= 0) continue;

            std::string name = "?";
            if (m_getName) {
                void *e2 = nullptr;
                void *strObj = _monoRuntimeInvoke(m_getName, (void *) structAddr, nullptr, &e2);
                if (strObj && !e2) name = ReadMonoStringNative(strObj);
            }
            LOGI("[PetFoodDiag] slot#%d staticId=%d count=%d entityType=%d name='%s' netId=%u", i, staticId, count, et, name.c_str(), netId);
            shown++;
        }
        LOGI("[PetFoodDiag] total item ditampilkan = %d (dari %d slot)", shown, size);
    });
}

static void DoFeedPet(const MyPet &p) {
    uint32_t food = ResolvePetFoodNetId(p.isDog);
    if (food == 0) { LOGE(OBFUSCATE("[Pet] feed batal: makanan habis")); return; }
    int qty = g_petFeedQty;
    if (qty < 1) qty = 1; if (qty > 200) qty = 200;
    PetProto1(OBFUSCATE("PrepareFeed"), p.netId);
    PetBeginFeed(p.netId, food, (uint32_t) qty, 0);
}

void FeedSelectedPetOnce() {
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        MyPet p; if (!GetSelectedPet(p)) { LOGE(OBFUSCATE("[Pet] belum ada pet terpilih")); return; }
        DoFeedPet(p);
    });
}

void InteractSelectedPetOnce() {
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        MyPet p; if (!GetSelectedPet(p)) { LOGE(OBFUSCATE("[Pet] belum ada pet terpilih")); return; }
        PetProto1(OBFUSCATE("Pet"), p.netId);
    });
}

void BathePetOnce() {
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        MyPet p; if (!GetSelectedPet(p)) { LOGE(OBFUSCATE("[Pet] tidak ada pet terpilih")); return; }
        PetProto1(OBFUSCATE("PetBathinghBegin"), p.netId);
        PetProto1(OBFUSCATE("PetBathingRoundStart"), p.netId);
    });
}
// ===================== END PET =====================

bool TakeUpRodNative(uint32_t rodNetId) {
    void *domain = _monoGetRootDomain();
    if (domain) _monoThreadAttach(domain);
    void *imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) return false;
    void *protoKlass = _monoClassFromName(imgData,
        (const char *) OBFUSCATE("XDTDataAndProtocol.ProtocolService.Fishing"),
        (const char *) OBFUSCATE("FishingProtocolManager"));
    if (!protoKlass) return false;
    void *m_takeUp = FindMethodInHierarchy(protoKlass, (const char *) OBFUSCATE("TakeUpRod"), 1);
    if (!m_takeUp) return false;
    uint32_t rId = rodNetId;
    void *args[] = { &rId };
    void *exc = nullptr;
    _monoRuntimeInvoke(m_takeUp, nullptr, args, &exc);
    return (exc == nullptr);
}

struct SearchClassContext {
    const char *ns;
    const char *name;
    void *result;
};

static void SearchAssemblyCallback(void *assembly, void *user_data) {
    SearchClassContext *ctx = (SearchClassContext *) user_data;
    if (ctx->result) return;
    if (!_monoAssemblyGetImage) return;
    void *img = _monoAssemblyGetImage(assembly);
    if (!img) return;
    void *klass = _monoClassFromName(img, ctx->ns, ctx->name);
    if (klass) ctx->result = klass;
}

static void *FindMonoClassAnywhere(const char *ns, const char *name) {
    const char *likelyImages[] = {
        "EngineWrapper", "EngineWrapper.dll",
        "UnityEngine.AnimationModule", "UnityEngine.AnimationModule.dll",
        "UnityEngine.CoreModule", "UnityEngine.CoreModule.dll",
        "UnityEngine", "UnityEngine.dll",
        "XDTLevelAndEntity", "XDTLevelAndEntity.dll",
        "XDTDataAndProtocol", "XDTDataAndProtocol.dll",
        "XDT.Scene.Shared", "XDT.Scene.Shared.dll",
        "EcsClient", "EcsClient.dll",
        "Client", "Client.dll", "Assembly-CSharp", "Assembly-CSharp.dll"
    };
    for (const char *imgName : likelyImages) {
        void *img = _monoImageLoaded(imgName);
        if (img) {
            void *klass = _monoClassFromName(img, ns, name);
            if (klass) return klass;
        }
    }
    // iOS：mono_assembly_foreach 由 MonoCompat 垫片提供（内部走 il2cpp 的
    // assembly 列表），不需要 dlsym libmonosgen-2.0.so。函数指针只取一次。
    using ForeachFn = void (*)(void (*)(void *, void *), void *);
    static ForeachFn s_monoAssemblyForeach = (ForeachFn)SMMono::Lookup("mono_assembly_foreach");
    if (s_monoAssemblyForeach) {
        SearchClassContext ctx = { ns, name, nullptr };
        s_monoAssemblyForeach(SearchAssemblyCallback, &ctx);
        if (ctx.result) return ctx.result;
    }
    return nullptr;
}

static void *GetLocalPlayerInstance();

static int UnityStringToHash(const char *name) {
    if (!name || !name[0]) return 0;
    void *domain = _monoGetRootDomain();
    if (domain) _monoThreadAttach(domain);

    static void *m_strToHash = nullptr;
    if (!m_strToHash) {
        void *klass = FindMonoClassAnywhere((const char *) OBFUSCATE("UnityEngine"), (const char *) OBFUSCATE("Animator"));
        if (klass) {
            m_strToHash = FindMethodInHierarchy(klass, (const char *) OBFUSCATE("StringToHash"), 1);
        }
        if (!m_strToHash) {
            void *player = GetLocalPlayerInstance();
            if (player) {
                void *playerKlass = _monoObjectGetClass(player);
                void *m_getAnim = playerKlass ? FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("get_animationComponent"), 0) : nullptr;
                if (!m_getAnim && playerKlass) m_getAnim = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("GetAnimationComponent"), 0);
                void *exc = nullptr;
                void *animComp = m_getAnim ? _monoRuntimeInvoke(m_getAnim, player, nullptr, &exc) : nullptr;
                if (!animComp || exc) {
                    void *f_anim = _monoClassGetFieldFromName(playerKlass, (const char *) OBFUSCATE("animationComponent"));
                    if (!f_anim) f_anim = _monoClassGetFieldFromName(playerKlass, (const char *) OBFUSCATE("_animationComponent"));
                    if (f_anim) _monoFieldGetValue(player, f_anim, &animComp);
                }
                if (animComp) {
                    void *animKlass = _monoObjectGetClass(animComp);
                    void *m_getAnimator = animKlass ? FindMethodInHierarchy(animKlass, (const char *) OBFUSCATE("get_animator"), 0) : nullptr;
                    void *animator = m_getAnimator ? _monoRuntimeInvoke(m_getAnimator, animComp, nullptr, &exc) : nullptr;
                    if (!animator) {
                        void *f_anim = _monoClassGetFieldFromName(animKlass, (const char *) OBFUSCATE("_animator"));
                        if (!f_anim) f_anim = _monoClassGetFieldFromName(animKlass, (const char *) OBFUSCATE("animator"));
                        if (f_anim) _monoFieldGetValue(animComp, f_anim, &animator);
                    }
                    if (animator) {
                        void *animClass = _monoObjectGetClass(animator);
                        if (animClass) {
                            m_strToHash = FindMethodInHierarchy(animClass, (const char *) OBFUSCATE("StringToHash"), 1);
                        }
                    }
                }
            }
        }
    }
    if (m_strToHash && domain) {
        void *monoStr = _monoStringNew(domain, name);
        if (monoStr) {
            void *args[] = { monoStr };
            void *exc = nullptr;
            void *res = _monoRuntimeInvoke(m_strToHash, nullptr, args, &exc);
            if (res && !exc) {
                int hVal = *(int *) _monoObjectUnbox(res);
                LOGI((const char *) OBFUSCATE("[StringToHash] '%s' -> %d"), name, hVal);
                return hVal;
            }
        }
    }
    return 0;
}

static void *GetLocalPlayerInstance() {
    void *imgLevel = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) imgLevel = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity.dll"));
    if (!imgLevel) return nullptr;

    void *interactKlass = _monoClassFromName(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem"), (const char *) OBFUSCATE("InteractSystem"));
    if (interactKlass) {
        void *m_getInst = FindMethodInHierarchy(interactKlass, (const char *) OBFUSCATE("get_Instance"), 0);
        if (m_getInst) {
            void *exc = nullptr;
            void *inst = _monoRuntimeInvoke(m_getInst, nullptr, nullptr, &exc);
            if (inst && !exc) {
                void *m_getPlayer = FindMethodInHierarchy(interactKlass, (const char *) OBFUSCATE("get_player"), 0);
                if (m_getPlayer) {
                    void *player = _monoRuntimeInvoke(m_getPlayer, inst, nullptr, &exc);
                    if (player && !exc) return player;
                }
            }
        }
    }

    void *entityUtilKlass = _monoClassFromName(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), (const char *) OBFUSCATE("EntityUtil"));
    if (entityUtilKlass) {
        void *m_self = FindMethodInHierarchy(entityUtilKlass, (const char *) OBFUSCATE("GetSelfPlayer"), 0);
        if (m_self) {
            void *exc = nullptr;
            void *player = _monoRuntimeInvoke(m_self, nullptr, nullptr, &exc);
            if (player && !exc) return player;
        }
    }
    return nullptr;
}

static bool FacePlayerTowardTargetNative(Vector3 targetPos) {
    void *domain = _monoGetRootDomain();
    if (domain) _monoThreadAttach(domain);
    void *imgLevel = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) return false;

    void *entityUtilKlass = _monoClassFromName(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), (const char *) OBFUSCATE("EntityUtil"));
    if (!entityUtilKlass) return false;
    void *m_self = FindMethodInHierarchy(entityUtilKlass, (const char *) OBFUSCATE("GetSelfPlayer"), 0);
    if (!m_self) return false;
    void *exc = nullptr;
    void *player = _monoRuntimeInvoke(m_self, nullptr, nullptr, &exc);
    if (!player || exc) return false;

    Vector3 playerPos = NativeGetPosition(player);
    float dx = targetPos.x - playerPos.x;
    float dz = targetPos.z - playerPos.z;
    float sqrDist = dx * dx + dz * dz;
    if (sqrDist < 0.04f) return false;

    float len = sqrtf(sqrDist);
    float normX = dx / len;
    float normZ = dz / len;

    float yawRad = atan2f(normX, normZ);
    float yawDeg = yawRad * (180.0f / 3.14159265358979323846f);
    Vector3 eulerAngles = { 0.0f, yawDeg, 0.0f };

    float halfYaw = yawRad * 0.5f;
    ModQuaternion faceRot;
    faceRot.x = 0.0f;
    faceRot.y = sinf(halfYaw);
    faceRot.z = 0.0f;
    faceRot.w = cosf(halfYaw);

    void *playerKlass = _monoObjectGetClass(player);

    void *m_transfer = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("Transfer"), 4);
    if (!m_transfer) m_transfer = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("Transfer"), 2);
    if (m_transfer) {
        uint32_t sceneId = 0;
        bool force = false;
        void *args4[] = { &playerPos, &eulerAngles, &sceneId, &force };
        void *args2[] = { &playerPos, &eulerAngles };
        void *excTransfer = nullptr;
        _monoRuntimeInvoke(m_transfer, player, (FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("Transfer"), 4) ? args4 : args2), &excTransfer);
    }

    void *m_getMove = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("get_moveComponent"), 0);
    if (!m_getMove) m_getMove = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("GetMoveComponent"), 0);
    if (m_getMove) {
        void *excMove = nullptr;
        void *moveObj = _monoRuntimeInvoke(m_getMove, player, nullptr, &excMove);
        if (moveObj && !excMove) {
            void *moveKlass = _monoObjectGetClass(moveObj);
            void *m_worldFaceTo = moveKlass ? FindMethodInHierarchy(moveKlass, (const char *) OBFUSCATE("WorldFaceTo"), 1) : nullptr;
            if (m_worldFaceTo) {
                void *argsFace[] = { &faceRot };
                void *excFace = nullptr;
                _monoRuntimeInvoke(m_worldFaceTo, moveObj, argsFace, &excFace);
            }
            void *f_forward = moveKlass ? _monoClassGetFieldFromName(moveKlass, (const char *) OBFUSCATE("_Forward")) : nullptr;
            if (!f_forward && moveKlass) f_forward = _monoClassGetFieldFromName(moveKlass, (const char *) OBFUSCATE("Forward"));
            if (f_forward) {
                struct ModVector2 { float x; float y; } v2 = { normX, normZ };
                _monoFieldSetValue(moveObj, f_forward, &v2);
            }
        }
    }
    return true;
}

static bool ForceFishingRodThrowTargetNative(Vector3 targetPos) {
    void *domain = _monoGetRootDomain();
    if (domain) _monoThreadAttach(domain);

    void *player = GetLocalPlayerInstance();
    if (!player) return false;

    void *playerKlass = _monoObjectGetClass(player);
    if (!playerKlass) return false;
    void *m_getEquip = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("get_equipComponent"), 0);
    if (!m_getEquip) m_getEquip = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("GetEquipComponent"), 0);
    if (!m_getEquip) return false;

    void *exc = nullptr;
    void *equipComp = _monoRuntimeInvoke(m_getEquip, player, nullptr, &exc);
    if (!equipComp || exc) return false;

    void *equipKlass = _monoObjectGetClass(equipComp);
    if (!equipKlass) return false;
    void *m_getHandhold = FindMethodInHierarchy(equipKlass, (const char *) OBFUSCATE("get_handhold"), 0);
    if (!m_getHandhold) m_getHandhold = FindMethodInHierarchy(equipKlass, (const char *) OBFUSCATE("GetHandhold"), 0);
    if (!m_getHandhold) return false;

    void *handhold = _monoRuntimeInvoke(m_getHandhold, equipComp, nullptr, &exc);
    if (!handhold || exc) return false;

    void *handholdKlass = _monoObjectGetClass(handhold);
    if (!handholdKlass) return false;

    void *f_throwTar = _monoClassGetFieldFromName(handholdKlass, (const char *) OBFUSCATE("_throwTarPos"));
    if (!f_throwTar) f_throwTar = _monoClassGetFieldFromName(handholdKlass, (const char *) OBFUSCATE("throwTarPos"));
    if (f_throwTar) {
        Vector3 tp = targetPos;
        _monoFieldSetValue(handhold, f_throwTar, &tp);
    }

    void *f_canThrow = _monoClassGetFieldFromName(handholdKlass, (const char *) OBFUSCATE("CanThrow"));
    if (!f_canThrow) f_canThrow = _monoClassGetFieldFromName(handholdKlass, (const char *) OBFUSCATE("canThrow"));
    if (f_canThrow) {
        bool canThrow = true;
        _monoFieldSetValue(handhold, f_canThrow, &canThrow);
    }

    return true;
}

static void SetPlayerAnimSpeedNative(float speed) {
    void *domain = _monoGetRootDomain();
    if (domain) _monoThreadAttach(domain);

    void *player = GetLocalPlayerInstance();
    if (!player) return;

    void *playerKlass = _monoObjectGetClass(player);
    if (!playerKlass) return;
    void *m_getAnim = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("get_animationComponent"), 0);
    if (!m_getAnim) m_getAnim = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("GetAnimationComponent"), 0);
    void *exc = nullptr;
    void *animComp = m_getAnim ? _monoRuntimeInvoke(m_getAnim, player, nullptr, &exc) : nullptr;
    if (!animComp || exc) {
        void *f_anim = _monoClassGetFieldFromName(playerKlass, (const char *) OBFUSCATE("animationComponent"));
        if (!f_anim) f_anim = _monoClassGetFieldFromName(playerKlass, (const char *) OBFUSCATE("_animationComponent"));
        if (f_anim) _monoFieldGetValue(player, f_anim, &animComp);
    }
    if (!animComp) return;

    void *animKlass = _monoObjectGetClass(animComp);
    if (!animKlass) return;

    void *m_getAnimator = FindMethodInHierarchy(animKlass, (const char *) OBFUSCATE("get_animator"), 0);
    void *animator = nullptr;
    if (m_getAnimator) {
        exc = nullptr;
        animator = _monoRuntimeInvoke(m_getAnimator, animComp, nullptr, &exc);
    } else {
        void *f_anim = _monoClassGetFieldFromName(animKlass, (const char *) OBFUSCATE("_animator"));
        if (!f_anim) f_anim = _monoClassGetFieldFromName(animKlass, (const char *) OBFUSCATE("animator"));
        if (f_anim) _monoFieldGetValue(animComp, f_anim, &animator);
    }

    if (animator) {
        void *animClass = _monoObjectGetClass(animator);
        void *m_setSpeed = FindMethodInHierarchy(animClass, (const char *) OBFUSCATE("set_speed"), 1);
        if (m_setSpeed) {
            float spd = speed;
            void *args[] = { &spd };
            exc = nullptr;
            _monoRuntimeInvoke(m_setSpeed, animator, args, &exc);
        }
    }
}

static bool SkipCastAnimNative(bool allowCrossfade, bool &outInSpinningRod) {
    outInSpinningRod = false;
    void *domain = _monoGetRootDomain();
    if (domain) _monoThreadAttach(domain);

    static int hashSpinningRod = 0;
    static int hashFishing = 0;
    if (hashSpinningRod == 0) {
        hashSpinningRod = UnityStringToHash("SpinningRod");
        hashFishing = UnityStringToHash("Fishing");
    }
    if (hashSpinningRod == 0 || hashFishing == 0) return false;

    void *player = GetLocalPlayerInstance();
    if (!player) return false;

    void *playerKlass = _monoObjectGetClass(player);
    if (!playerKlass) return false;
    void *m_getAnim = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("get_animationComponent"), 0);
    if (!m_getAnim) m_getAnim = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("GetAnimationComponent"), 0);
    void *exc = nullptr;
    void *animComp = m_getAnim ? _monoRuntimeInvoke(m_getAnim, player, nullptr, &exc) : nullptr;
    if (!animComp || exc) {
        void *f_anim = _monoClassGetFieldFromName(playerKlass, (const char *) OBFUSCATE("animationComponent"));
        if (!f_anim) f_anim = _monoClassGetFieldFromName(playerKlass, (const char *) OBFUSCATE("_animationComponent"));
        if (f_anim) _monoFieldGetValue(player, f_anim, &animComp);
    }
    if (!animComp) return false;

    void *animKlass = _monoObjectGetClass(animComp);
    if (!animKlass) return false;

    void *m_isAnimState = FindMethodInHierarchy(animKlass, (const char *) OBFUSCATE("IsAnimState"), 1);
    if (m_isAnimState) {
        int hSpin = hashSpinningRod;
        void *argsCheck[] = { &hSpin };
        exc = nullptr;
        void *resCheck = _monoRuntimeInvoke(m_isAnimState, animComp, argsCheck, &exc);
        if (resCheck && !exc) {
            outInSpinningRod = (*(uint8_t *) _monoObjectUnbox(resCheck) != 0);
        }
    }

    if (allowCrossfade && outInSpinningRod) {
        void *m_crossFade = FindMethodInHierarchy(animKlass, (const char *) OBFUSCATE("CrossFade"), 3);
        if (m_crossFade) {
            int hFish = hashFishing;
            float dur = 0.0f;
            int layer = -1;
            void *argsFade[] = { &hFish, &dur, &layer };
            exc = nullptr;
            _monoRuntimeInvoke(m_crossFade, animComp, argsFade, &exc);
            if (!exc) {
                LOGI((const char *) OBFUSCATE("[SkipCast] AnimationComponent CrossFade(Fishing, 0.0f, -1) SUCCESS!"));
                return true;
            }
        }

        // Fallback: CrossFade directly on UnityEngine.Animator
        void *m_getAnimator = FindMethodInHierarchy(animKlass, (const char *) OBFUSCATE("get_animator"), 0);
        exc = nullptr;
        void *animator = m_getAnimator ? _monoRuntimeInvoke(m_getAnimator, animComp, nullptr, &exc) : nullptr;
        if (!animator) {
            void *f_anim = _monoClassGetFieldFromName(animKlass, (const char *) OBFUSCATE("_animator"));
            if (!f_anim) f_anim = _monoClassGetFieldFromName(animKlass, (const char *) OBFUSCATE("animator"));
            if (f_anim) _monoFieldGetValue(animComp, f_anim, &animator);
        }
        if (animator && !exc) {
            void *animClass = _monoObjectGetClass(animator);
            void *m_animCrossFade = animClass ? FindMethodInHierarchy(animClass, (const char *) OBFUSCATE("CrossFade"), 3) : nullptr;
            if (!m_animCrossFade && animClass) m_animCrossFade = FindMethodInHierarchy(animClass, (const char *) OBFUSCATE("CrossFade"), 2);
            if (m_animCrossFade) {
                int hFish = hashFishing;
                float dur = 0.0f;
                int layer = -1;
                void *argsFade3[] = { &hFish, &dur, &layer };
                void *argsFade2[] = { &hFish, &dur };
                void *excAnim = nullptr;
                _monoRuntimeInvoke(m_animCrossFade, animator, (FindMethodInHierarchy(animClass, (const char *) OBFUSCATE("CrossFade"), 3) ? argsFade3 : argsFade2), &excAnim);
                if (!excAnim) {
                    LOGI((const char *) OBFUSCATE("[SkipCast] Animator CrossFade(Fishing, 0.0f, -1) SUCCESS!"));
                    return true;
                }
            }
            void *m_animPlay = animClass ? FindMethodInHierarchy(animClass, (const char *) OBFUSCATE("Play"), 3) : nullptr;
            if (!m_animPlay && animClass) m_animPlay = FindMethodInHierarchy(animClass, (const char *) OBFUSCATE("Play"), 1);
            if (m_animPlay) {
                int hFish = hashFishing;
                int layer = -1;
                float normalizedTime = 0.0f;
                void *argsPlay3[] = { &hFish, &layer, &normalizedTime };
                void *argsPlay1[] = { &hFish };
                void *excPlay = nullptr;
                _monoRuntimeInvoke(m_animPlay, animator, (FindMethodInHierarchy(animClass, (const char *) OBFUSCATE("Play"), 3) ? argsPlay3 : argsPlay1), &excPlay);
                if (!excPlay) {
                    LOGI((const char *) OBFUSCATE("[SkipCast] Animator Play(Fishing) SUCCESS!"));
                    return true;
                }
            }
        }
    }
    return false;
}

static float ReadRodDurabilityNative() {
    void *imgLevel = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) return 1.0f;
    void *entityUtilKlass = _monoClassFromName(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), (const char *) OBFUSCATE("EntityUtil"));
    if (!entityUtilKlass) return 1.0f;
    void *m_self = FindMethodInHierarchy(entityUtilKlass, (const char *) OBFUSCATE("GetSelfPlayer"), 0);
    if (!m_self) return 1.0f;
    void *exc = nullptr;
    void *player = _monoRuntimeInvoke(m_self, nullptr, nullptr, &exc);
    if (!player || exc) return 1.0f;
    void *playerKlass = _monoObjectGetClass(player);
    if (!playerKlass) return 1.0f;

    void *f_ag = _monoClassGetFieldFromName(playerKlass, (const char *) OBFUSCATE("actionGraph"));
    if (!f_ag) f_ag = _monoClassGetFieldFromName(playerKlass, (const char *) OBFUSCATE("_actionGraph"));
    void *agObj = nullptr;
    if (f_ag) _monoFieldGetValue(player, f_ag, &agObj);
    else {
        void *m_ag = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("get_actionGraph"), 0);
        if (m_ag) agObj = _monoRuntimeInvoke(m_ag, player, nullptr, &exc);
    }
    if (!agObj) return 1.0f;

    void *agKlass = _monoObjectGetClass(agObj);
    if (!agKlass) return 1.0f;
    void *f_cpg = _monoClassGetFieldFromName(agKlass, (const char *) OBFUSCATE("currentPlayGraph"));
    if (!f_cpg) f_cpg = _monoClassGetFieldFromName(agKlass, (const char *) OBFUSCATE("_currentPlayGraph"));
    void *cpgObj = nullptr;
    if (f_cpg) _monoFieldGetValue(agObj, f_cpg, &cpgObj);
    if (!cpgObj) return 1.0f;

    void *cpgKlass = _monoObjectGetClass(cpgObj);
    if (!cpgKlass) return 1.0f;
    void *f_mc = _monoClassGetFieldFromName(cpgKlass, (const char *) OBFUSCATE("motionClip"));
    if (!f_mc) f_mc = _monoClassGetFieldFromName(cpgKlass, (const char *) OBFUSCATE("_motionClip"));
    void *mcObj = nullptr;
    if (f_mc) _monoFieldGetValue(cpgObj, f_mc, &mcObj);
    if (!mcObj) return 1.0f;

    void *mcKlass = _monoObjectGetClass(mcObj);
    if (!mcKlass) return 1.0f;
    void *f_dur = _monoClassGetFieldFromName(mcKlass, (const char *) OBFUSCATE("_rodDurability"));
    if (!f_dur) f_dur = _monoClassGetFieldFromName(mcKlass, (const char *) OBFUSCATE("rodDurability"));
    if (!f_dur) return 1.0f;

    float dur = 1.0f;
    _monoFieldGetValue(mcObj, f_dur, (void **) &dur);
    if (!std::isfinite(dur) || dur < 0.0f || dur > 1.05f) return 1.0f;
    if (dur <= 0.001f) {
        void *f_pull = _monoClassGetFieldFromName(mcKlass, (const char *) OBFUSCATE("_pullStrength"));
        if (!f_pull) f_pull = _monoClassGetFieldFromName(mcKlass, (const char *) OBFUSCATE("pullStrength"));
        float pullStr = 0.0f;
        if (f_pull) _monoFieldGetValue(mcObj, f_pull, (void **) &pullStr);
        if (pullStr <= 0.05f) return 1.0f;
    }
    return dur;
}

static void *GetSelfSyncStatusObj(void *&imgLevelOut) {
    void *imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    imgLevelOut = imgLevel;
    if (!imgLevel) return nullptr;
    void *entityUtilKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
    void *m_self = FindMethodInHierarchy(entityUtilKlass, OBFUSCATE("GetSelfPlayer"), 0);
    if (!m_self) return nullptr;
    void *exc = nullptr;
    void *player = _monoRuntimeInvoke(m_self, nullptr, nullptr, &exc);
    if (!player || exc) return nullptr;
    void *baseActorKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"), OBFUSCATE("BaseActorComponent"));
    void *statusField = _monoClassGetFieldFromName(baseActorKlass, OBFUSCATE("Status"));
    if (!statusField) return nullptr;
    void *statusObj = nullptr; _monoFieldGetValue(player, statusField, &statusObj);
    return statusObj;
}
static int ReadMyFsmState() {
    void *imgLevel = nullptr;
    void *statusObj = GetSelfSyncStatusObj(imgLevel);
    if (!statusObj) return -999;
    void *syncKlass = _monoObjectGetClass(statusObj);
    void *fsmField = _monoClassGetFieldFromName(syncKlass, OBFUSCATE("FsmStatus"));
    if (!fsmField) return -998;
    void *fsmKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"), OBFUSCATE("FsmStatus"));
    void *stateField = _monoClassGetFieldFromName(fsmKlass, OBFUSCATE("state"));
    if (!stateField) return -997;
    int fsmBase = _monoFieldGetOffset(fsmField);
    int off = _monoFieldGetOffset(stateField) - MONO_FIELD_HEADER;
    return *(int32_t *) ((uint8_t *) statusObj + fsmBase + off);
}
struct FishingStatusView {
    bool valid = false;
    int inFishingState = -1;
    uint32_t rodNetId = 0;
    int fishState = -999;
    int pressed = -1;
    float pullStrength = 0.0f;
    uint32_t baitStaticId = 0;
    uint32_t baitNetId = 0;
    Vector3 tarPos{};
    Vector3 floatPos{};
};
static FishingStatusView ReadFishingStatus() {
    FishingStatusView v;
    void *imgLevel = nullptr;
    void *statusObj = GetSelfSyncStatusObj(imgLevel);
    if (!statusObj || !imgLevel) return v;
    void *syncKlass = _monoObjectGetClass(statusObj);
    void *fishField = _monoClassGetFieldFromName(syncKlass, (const char *) OBFUSCATE("FishingStatus"));
    if (!fishField) return v;
    uint8_t *base = (uint8_t *) statusObj + _monoFieldGetOffset(fishField);
    void *fk = _monoClassFromName(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"), (const char *) OBFUSCATE("FishingStatus"));
    if (!fk) return v;
    auto off = [&](const char *n) -> int {
        void *f = _monoClassGetFieldFromName(fk, n);
        return f ? (_monoFieldGetOffset(f) - MONO_FIELD_HEADER) : -1;
    };
    int o;
    if ((o = off((const char *) OBFUSCATE("inFishingState"))) >= 0) v.inFishingState = *(uint8_t *) (base + o);
    if ((o = off((const char *) OBFUSCATE("fishRodNetId"))) >= 0) v.rodNetId = *(uint32_t *) (base + o);
    if ((o = off((const char *) OBFUSCATE("fishState"))) >= 0) v.fishState = *(int32_t *) (base + o);
    if ((o = off((const char *) OBFUSCATE("pressed"))) >= 0) v.pressed = *(uint8_t *) (base + o);
    if ((o = off((const char *) OBFUSCATE("tarPos"))) >= 0) memcpy(&v.tarPos, base + o, sizeof(Vector3));
    if ((o = off((const char *) OBFUSCATE("floatPos"))) >= 0) memcpy(&v.floatPos, base + o, sizeof(Vector3));
    if ((o = off((const char *) OBFUSCATE("pullStrength"))) >= 0) v.pullStrength = *(float *) (base + o);
    if ((o = off((const char *) OBFUSCATE("baitingFishStaticId"))) >= 0) v.baitStaticId = *(uint32_t *) (base + o);
    if ((o = off((const char *) OBFUSCATE("baitingFishNetId"))) >= 0) v.baitNetId = *(uint32_t *) (base + o);
    v.valid = true;
    return v;
}
static uint32_t ReadMyFloat(uint32_t selfNetId, Vector3 &floatPos) {
    std::vector<char> fbuf; void *fk = nullptr;
    if (!TryGetCompDataBuf((const char *) OBFUSCATE("PlayerFloatData"), selfNetId, fbuf, fk)) return 0;
    int offF = CompFieldOff(fk, (const char *) OBFUSCATE("floatNetId"));
    int offB = CompFieldOff(fk, (const char *) OBFUSCATE("basePosition"));
    uint32_t f = (offF >= 0) ? *(uint32_t *) (fbuf.data() + offF) : 0;
    if (offB >= 0) memcpy(&floatPos, fbuf.data() + offB, sizeof(Vector3));
    return f;
}

static bool SetFishingStateButtonPressedNative(bool pressed) {
    void *domain = _monoGetRootDomain();
    if (domain) _monoThreadAttach(domain);

    FishingStatusView fs = ReadFishingStatus();
    if (!fs.valid || fs.inFishingState == 0) return false;
    // PlayerStateFishing.TickWaiting cancels fishing if _mainBtnPressed is true while fishState != 2 (Battle)
    if (pressed && fs.fishState != 2) return false;

    bool updated = false;
    void *player = GetLocalPlayerInstance();
    if (player) {
        void *playerKlass = _monoObjectGetClass(player);
        void *m_getCurState = playerKlass ? FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("GetCurrentState"), 0) : nullptr;
        if (m_getCurState) {
            void *exc = nullptr;
            void *curState = _monoRuntimeInvoke(m_getCurState, player, nullptr, &exc);
            if (curState && !exc) {
                void *stateKlass = _monoObjectGetClass(curState);
                const char *stateName = stateKlass ? _monoClassGetName(stateKlass) : nullptr;
                if (stateName && strstr(stateName, "PlayerStateFishing") != nullptr) {
                    void *m_setBtn = FindMethodInHierarchy(stateKlass, (const char *) OBFUSCATE("SetStateButtonPressed"), 1);
                    if (!m_setBtn) m_setBtn = FindMethodInHierarchy(stateKlass, (const char *) OBFUSCATE("OnMainInteraction"), 1);
                    if (m_setBtn) {
                        bool p = pressed;
                        void *args[] = { &p };
                        void *excBtn = nullptr;
                        _monoRuntimeInvoke(m_setBtn, curState, args, &excBtn);
                        if (!excBtn) updated = true;
                    }
                }
            }
        }
    }

    void *imgLevel = nullptr;
    void *statusObj = GetSelfSyncStatusObj(imgLevel);
    if (statusObj && imgLevel) {
        void *syncKlass = _monoObjectGetClass(statusObj);
        void *fishField = syncKlass ? _monoClassGetFieldFromName(syncKlass, (const char *) OBFUSCATE("FishingStatus")) : nullptr;
        void *fk = _monoClassFromName(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"), (const char *) OBFUSCATE("FishingStatus"));
        if (fishField && fk) {
            void *fPressed = _monoClassGetFieldFromName(fk, (const char *) OBFUSCATE("pressed"));
            if (fPressed) {
                int fo = _monoFieldGetOffset(fPressed);
                int off = fo >= MONO_FIELD_HEADER ? (fo - MONO_FIELD_HEADER) : fo;
                uint8_t *base = (uint8_t *) statusObj + _monoFieldGetOffset(fishField);
                *(uint8_t *) (base + off) = pressed ? 1 : 0;
                updated = true;
            }
        }
    }
    return updated;
}

static bool TryGetFishingBuoyPositionNative(Vector3 &outBuoyPos, void **outFloatComp = nullptr) {
    outBuoyPos = { 0.0f, 0.0f, 0.0f };
    if (outFloatComp) *outFloatComp = nullptr;
    void *player = GetLocalPlayerInstance();
    if (!player) return false;

    void *playerKlass = _monoObjectGetClass(player);
    if (!playerKlass) return false;
    void *m_getEquip = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("get_equipComponent"), 0);
    if (!m_getEquip) m_getEquip = FindMethodInHierarchy(playerKlass, (const char *) OBFUSCATE("GetEquipComponent"), 0);
    if (!m_getEquip) return false;

    void *exc = nullptr;
    void *equipComp = _monoRuntimeInvoke(m_getEquip, player, nullptr, &exc);
    if (!equipComp || exc) return false;

    void *equipKlass = _monoObjectGetClass(equipComp);
    if (!equipKlass) return false;
    void *m_getHandhold = FindMethodInHierarchy(equipKlass, (const char *) OBFUSCATE("get_handhold"), 0);
    if (!m_getHandhold) m_getHandhold = FindMethodInHierarchy(equipKlass, (const char *) OBFUSCATE("GetHandhold"), 0);
    if (!m_getHandhold) return false;

    void *handhold = _monoRuntimeInvoke(m_getHandhold, equipComp, nullptr, &exc);
    if (!handhold || exc) return false;

    void *handholdKlass = _monoObjectGetClass(handhold);
    if (!handholdKlass) return false;
    const char *hhName = _monoClassGetName(handholdKlass);
    if (!hhName || strstr(hhName, "FishingRod") == nullptr) return false;

    if (outFloatComp) {
        void *m_getFloat = FindMethodInHierarchy(handholdKlass, (const char *) OBFUSCATE("GetFloat"), 0);
        if (m_getFloat) {
            void *excF = nullptr;
            void *fc = _monoRuntimeInvoke(m_getFloat, handhold, nullptr, &excF);
            if (fc && !excF) *outFloatComp = fc;
        }
        if (!*outFloatComp) {
            void *f_fc = _monoClassGetFieldFromName(handholdKlass, (const char *) OBFUSCATE("floatComponent"));
            if (f_fc) _monoFieldGetValue(handhold, f_fc, outFloatComp);
        }
    }

    void *m_getFloatPos = FindMethodInHierarchy(handholdKlass, (const char *) OBFUSCATE("GetFloatPosition"), 0);
    if (!m_getFloatPos) return false;

    void *posObj = _monoRuntimeInvoke(m_getFloatPos, handhold, nullptr, &exc);
    if (!posObj || exc) return false;
    void *raw = _monoObjectUnbox(posObj);
    if (!raw) return false;

    Vector3 pos = *(Vector3 *) raw;
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) return false;
    if (fabsf(pos.x) < 0.001f && fabsf(pos.y) < 0.001f && fabsf(pos.z) < 0.001f) return false;
    outBuoyPos = pos;
    return true;
}

static bool TrySendBuoyUpdateReliableNative(uint32_t floatNetId, Vector3 buoyPos, Vector3 dir, float successLen, float failLen) {
    void *domain = _monoGetRootDomain();
    if (!domain) return false;
    _monoThreadAttach(domain);

    bool sentAny = false;

    // Route 1: FishingProtocolManager.UpdateFloatPosition (natively sends UpdateRodBuoyPositionNetworkCommand on ChannelType.Reliable)
    void *imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol.dll"));
    if (imgData) {
        void *protoKlass = _monoClassFromName(imgData,
            (const char *) OBFUSCATE("XDTDataAndProtocol.ProtocolService.Fishing"),
            (const char *) OBFUSCATE("FishingProtocolManager"));
        if (protoKlass) {
            void *m_update = FindMethodInHierarchy(protoKlass, (const char *) OBFUSCATE("UpdateFloatPosition"), 5);
            if (m_update) {
                uint32_t fnId = floatNetId;
                Vector3 bp = buoyPos;
                Vector3 d = dir;
                float sLen = successLen;
                float fLen = failLen;
                void *args[] = { &fnId, &bp, &d, &sLen, &fLen };
                void *exc = nullptr;
                _monoRuntimeInvoke(m_update, nullptr, args, &exc);
                if (!exc) sentAny = true;
            }
        }
    }

    // Route 2: Inflated WebRequestUtility.SendCommand<UpdateRodBuoyPositionNetworkCommand> with unboxed valuetype pointer
    static void *s_sendReliableMethod = nullptr;
    static void *s_cmdKlass = nullptr;
    static void *s_fBuoyNetId = nullptr;
    static void *s_fBuoyPos = nullptr;
    static void *s_fDirection = nullptr;
    static void *s_fSuccessLen = nullptr;
    static void *s_fFailLen = nullptr;

    if (!s_sendReliableMethod) {
        void *webKlass = imgData ? _monoClassFromName(imgData,
            (const char *) OBFUSCATE("XDTDataAndProtocol.ProtocolService"),
            (const char *) OBFUSCATE("WebRequestUtility")) : nullptr;
        if (!webKlass && imgData) {
            webKlass = _monoClassFromName(imgData,
                (const char *) OBFUSCATE("XDTDataAndProtocol"),
                (const char *) OBFUSCATE("WebRequestUtility"));
        }
        if (!webKlass) webKlass = FindMonoClassAnywhere((const char *) OBFUSCATE("XDTDataAndProtocol.ProtocolService"), (const char *) OBFUSCATE("WebRequestUtility"));

        s_cmdKlass = FindMonoClassAnywhere((const char *) OBFUSCATE("XDT.Scene.Shared.GamePlay.Fishing"), (const char *) OBFUSCATE("UpdateRodBuoyPositionNetworkCommand"));
        if (!s_cmdKlass) {
            s_cmdKlass = FindMonoClassAnywhere((const char *) OBFUSCATE("XDTDataAndProtocol.Network.Command.Fish"), (const char *) OBFUSCATE("UpdateRodBuoyPositionNetworkCommand"));
        }

        if (webKlass && s_cmdKlass && _monoClassGetType &&
            _mono_metadata_get_generic_inst && _mono_class_inflate_generic_method) {
            void *openSend = FindMethodInHierarchy(webKlass, (const char *) OBFUSCATE("SendCommand"), 3);
            void *cmdType = _monoClassGetType(s_cmdKlass);
            if (openSend && cmdType) {
                void *typeArgs[] = { cmdType };
                void *ginst = _mono_metadata_get_generic_inst(1, typeArgs);
                if (ginst) {
                    MonoGenericContext ctx = { nullptr, ginst };
                    void *inflated = _mono_class_inflate_generic_method(openSend, &ctx);
                    if (inflated) {
                        if (_monoCompileMethod) _monoCompileMethod(inflated);
                        s_fBuoyNetId = FindFieldInHierarchy(s_cmdKlass, (const char *) OBFUSCATE("BuoyNetId"));
                        s_fBuoyPos = FindFieldInHierarchy(s_cmdKlass, (const char *) OBFUSCATE("BuoyPos"));
                        s_fDirection = FindFieldInHierarchy(s_cmdKlass, (const char *) OBFUSCATE("Direction"));
                        s_fSuccessLen = FindFieldInHierarchy(s_cmdKlass, (const char *) OBFUSCATE("SuccessLength"));
                        s_fFailLen = FindFieldInHierarchy(s_cmdKlass, (const char *) OBFUSCATE("FailureLength"));
                        if (s_fBuoyNetId && s_fBuoyPos && s_fDirection && s_fSuccessLen && s_fFailLen) {
                            s_sendReliableMethod = inflated;
                            LOGI((const char *) OBFUSCATE("[InstantFish] Inflated WebRequestUtility.SendCommand<UpdateRodBuoyPositionNetworkCommand> (Reliable)"));
                        }
                    }
                }
            }
        }
    }

    if (!sentAny && s_sendReliableMethod && s_cmdKlass && _monoObjectNew && _monoFieldSetValue && _monoObjectUnbox) {
        void *cmdObj = _monoObjectNew(domain, s_cmdKlass);
        if (cmdObj) {
            uint32_t fnId = floatNetId;
            Vector3 bp = buoyPos;
            Vector3 d = dir;
            float sLen = successLen;
            float fLen = failLen;
            _monoFieldSetValue(cmdObj, s_fBuoyNetId, &fnId);
            _monoFieldSetValue(cmdObj, s_fBuoyPos, &bp);
            _monoFieldSetValue(cmdObj, s_fDirection, &d);
            _monoFieldSetValue(cmdObj, s_fSuccessLen, &sLen);
            _monoFieldSetValue(cmdObj, s_fFailLen, &fLen);

            void *unboxedCmd = _monoObjectUnbox(cmdObj);
            if (unboxedCmd) {
                uint8_t needAuthed = 1;
                int channelReliable = 1; // ChannelType.Reliable
                void *args[] = { unboxedCmd, &needAuthed, &channelReliable };
                void *exc = nullptr;
                void *res = _monoRuntimeInvoke(s_sendReliableMethod, nullptr, args, &exc);
                if (!exc && res) {
                    void *unboxedRes = _monoObjectUnbox(res);
                    if (unboxedRes && *(int32_t *) unboxedRes >= 0) sentAny = true;
                }
            }
        }
    }

    return sentAny;
}

static bool TryArmFishingInstantCatchNative() {
    if (!isInstantFishing || !SesiAktif()) return false;

    uint32_t selfNetId = GetSelfNetIdInline();
    if (selfNetId == 0) return false;

    Vector3 basePos{};
    uint32_t floatNetId = ReadMyFloat(selfNetId, basePos);
    if (floatNetId == 0) return false;

    Vector3 buoyPos{};
    void *floatComp = nullptr;
    if (!TryGetFishingBuoyPositionNative(buoyPos, &floatComp)) {
        if (!GetEntityPosByNetId(floatNetId, buoyPos) ||
            (fabsf(buoyPos.x) < 0.001f && fabsf(buoyPos.y) < 0.001f && fabsf(buoyPos.z) < 0.001f)) {
            if (fabsf(basePos.x) > 0.001f || fabsf(basePos.y) > 0.001f || fabsf(basePos.z) > 0.001f) {
                buoyPos = basePos;
            } else {
                return false;
            }
        }
    }

    Vector3 playerPos{};
    if (!GetEntityPosByNetId(selfNetId, playerPos) ||
        (fabsf(playerPos.x) < 0.001f && fabsf(playerPos.y) < 0.001f && fabsf(playerPos.z) < 0.001f)) {
        void *player = GetLocalPlayerInstance();
        if (player) playerPos = NativeGetPosition(player);
    }

    Vector3 dir = { playerPos.x - buoyPos.x, playerPos.y - buoyPos.y, playerPos.z - buoyPos.z };
    float sqrLen = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
    if (!std::isfinite(sqrLen) || sqrLen < 0.0004f) {
        dir = { 0.0f, 0.0f, 1.0f };
    }

    if (floatComp) {
        void *fcKlass = _monoObjectGetClass(floatComp);
        if (fcKlass) {
            void *m_setData = FindMethodInHierarchy(fcKlass, (const char *) OBFUSCATE("SetFishingData"), 4);
            if (m_setData) {
                Vector3 bp = buoyPos;
                Vector3 d = dir;
                float sLen = 0.01f;
                float fLen = 30.0f;
                void *argsData[] = { &bp, &d, &sLen, &fLen };
                void *excData = nullptr;
                _monoRuntimeInvoke(m_setData, floatComp, argsData, &excData);
            }
        }
    }

    return TrySendBuoyUpdateReliableNative(floatNetId, buoyPos, dir, 0.01f, 30.0f);
}

static uint32_t ReadFishShadowByNetId(uint32_t fishNetId, bool &found) {
    std::vector<char> cb; void *ck = nullptr;
    if (!TryGetCompDataBuf((const char *) OBFUSCATE("FishComponentData"), fishNetId, cb, ck)) { found = false; return 99; }
    found = true;
    int oS = CompFieldOff(ck, (const char *) OBFUSCATE("shadowState"));
    return oS >= 0 ? *(uint32_t *) (cb.data() + oS) : 99;
}
static Vector3 ComputeStandPos(Vector3 fishPos, Vector3 selfPos, float gap) {
    float dx = selfPos.x - fishPos.x, dz = selfPos.z - fishPos.z;
    float len = sqrtf(dx * dx + dz * dz);
    Vector3 out = fishPos;
    if (len > 0.001f) { out.x = fishPos.x + (dx / len) * gap; out.z = fishPos.z + (dz / len) * gap; }
    else { out.x = fishPos.x + gap; }
    out.y = selfPos.y;
    return out;
}
static uint32_t ScanNearestFish(Vector3 ref, float maxDist, float &outDist, Vector3 &outPos, uint32_t ignoreNetId = 0) {
    std::vector<uint32_t> ids; GetAllNetIds(ids);
    uint32_t best = 0; float bestD = 1e9f; Vector3 bestPos{};
    for (uint32_t nid : ids) {
        if (ignoreNetId != 0 && nid == ignoreNetId) continue;
        std::vector<char> cb; void *ck = nullptr;
        if (!TryGetCompDataBuf((const char *) OBFUSCATE("FishComponentData"), nid, cb, ck)) continue;
        int oS = CompFieldOff(ck, (const char *) OBFUSCATE("shadowState")); uint32_t s = oS >= 0 ? *(uint32_t *) (cb.data() + oS) : 99;
        if (s >= 4) continue; // Busy (Battle=4, Escape=5, Succeed=6, RunAway=7)

        int oFloat = CompFieldOff(ck, (const char *) OBFUSCATE("floatNetId"));
        uint32_t fNet = (oFloat >= 0 && (size_t) oFloat + sizeof(uint32_t) <= cb.size()) ? *(uint32_t *) (cb.data() + oFloat) : 0;
        if (fNet != 0) continue; // Occupied by a buoy

        int oPlayer = CompFieldOff(ck, (const char *) OBFUSCATE("playerNetId"));
        uint32_t pNet = (oPlayer >= 0 && (size_t) oPlayer + sizeof(uint32_t) <= cb.size()) ? *(uint32_t *) (cb.data() + oPlayer) : 0;
        if (pNet != 0) continue; // Occupied by another player

        Vector3 fp{};
        if (!GetEntityPosByNetId(nid, fp)) {
            // Visible fish shadows can exist before their live Entity transform is ready.
            // Fall back to the synchronized targetPos from FishComponentData.
            int oTarget = CompFieldOff(ck, (const char *) OBFUSCATE("targetPos"));
            if (oTarget < 0 || (size_t) oTarget + sizeof(Vector3) > cb.size()) continue;
            memcpy(&fp, cb.data() + oTarget, sizeof(Vector3));
            if (!std::isfinite(fp.x) || !std::isfinite(fp.y) || !std::isfinite(fp.z)) continue;
        }

        // Lead moving fish (IdleMove)
        if (s == 1) {
            int oTarget = CompFieldOff(ck, (const char *) OBFUSCATE("targetPos"));
            if (oTarget >= 0 && (size_t) oTarget + sizeof(Vector3) <= cb.size()) {
                Vector3 mTarget{};
                memcpy(&mTarget, cb.data() + oTarget, sizeof(Vector3));
                if (std::isfinite(mTarget.x) && std::isfinite(mTarget.z)) {
                    float dx = mTarget.x - fp.x, dz = mTarget.z - fp.z;
                    float dsq = dx * dx + dz * dz;
                    if (dsq > 0.09f) {
                        float dist = sqrtf(dsq);
                        float lead = (std::min)(1.5f, dist * 0.4f);
                        fp.x += (dx / dist) * lead;
                        fp.z += (dz / dist) * lead;
                    }
                }
            }
        }

        float d = GetVector3Distance(ref, fp);
        if (d < bestD) { bestD = d; best = nid; bestPos = fp; }
    }
    outDist = bestD; outPos = bestPos;
    return (best != 0 && bestD <= maxDist) ? best : 0;
}
void AutoFishingLoop(uint32_t generation) {
    LOGI((const char *) OBFUSCATE("[AutoFish] STARTED (v18 - Instant Catch & Battle Fix)"));
    g_autoFishStatus = u8"正在向服务器验证…";
    g_autoFishSessionCatches = 0;

    // Strict Server Check: Must obtain verified payload from ServerC
    if (!SesiAktif()) {
        LOGI((const char *) OBFUSCATE("[AutoFish] Server authorization FAILED: offline -> abort immediately"));
        g_autoFishStatus = u8"服务器载荷不可用（离线）";
        g_autoFishingActive = false;
        return;
    }
    std::string wire = WireForOp(OP_AUTO_FISHING);
    if (wire.empty()) {
        LOGI((const char *) OBFUSCATE("[AutoFish] Server authorization FAILED: wire empty -> abort immediately"));
        g_autoFishStatus = u8"服务器载荷不可用（离线）";
        g_autoFishingActive = false;
        return;
    }
    InvokeRequest req = FetchPayloadSync(wire);
    if (req.op != OP_AUTO_FISHING || req.methodDesc != "CUSTOM_AUTO_FISHING" ||
        req.payloadRaw.empty() || !SesiAktif()) {
        LOGI((const char *) OBFUSCATE("[AutoFish] Server authorization FAILED or invalid -> abort immediately"));
        g_autoFishStatus = u8"服务器载荷不可用（离线）";
        g_autoFishingActive = false;
        return;
    }

    g_autoFishStatus = u8"启动中";
    int phase = 0, lastPhase = -1;
    bool equippedNow = false, wasFishing = false, responded = false, cancelOnReset = false;
    bool exitAfterCatch = false;
    bool rodPulled = false;
    bool tensionHoldingRelease = false;
    bool castCrossfaded = false;
    int spinSeenCount = 0;
    uint32_t cachedFish = 0, targetFish = 0, lastCaughtFish = 0;
    uint64_t lastCaughtTime = 0;
    Vector3 targetFishPos{};
    uint64_t phaseStart = 0, lastNoFishLog = 0, lastCastAttempt = 0, lastPullTime = 0, lastInstantArmTime = 0;
    uint64_t lastStateDiag = 0;
    int lastDiagPhase = -1, lastDiagFsm = -10000, lastDiagInFishing = -10000;
    int lastDiagFishState = -10000;
    uint32_t lastDiagFloat = UINT32_MAX;

    while (g_autoFishingActive &&
           g_autoFishingGeneration.load(std::memory_order_acquire) == generation) {
        if (!SesiAktif()) {
            LOGI((const char *) OBFUSCATE("[AutoFish] Session expired during loop -> stopping"));
            g_autoFishingActive = false;
            break;
        }
        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());

            const uint64_t equipDelayMs = (uint64_t) g_autoFishEquipDelayTicks * 200;
            const uint64_t biteTimeoutMs = (uint64_t) g_autoFishBiteTimeoutTicks * 200;   // 8s default
            const uint64_t cooldownMs = (uint64_t) g_autoFishCatchGraceTicks * 200;
            const uint64_t respStuckMs = biteTimeoutMs + 12000;

            uint64_t now = NowMs();
            uint32_t selfNetId = GetSelfNetIdInline();
            if (selfNetId == 0) return;
            Vector3 fpos{}; uint32_t myFloat = ReadMyFloat(selfNetId, fpos);

            const bool hoverEnabled = g_autoFishHoverBetweenCasts &&
                g_autoFishTeleport;
            SetAutoFishHoverNative(hoverEnabled);

            if (isInstantFishing && (phase == 2 || phase == 3) && (now - lastInstantArmTime >= 80)) {
                if (TryArmFishingInstantCatchNative()) {
                    lastInstantArmTime = now;
                }
            }

            // --- INTEGRASI AUTO REPAIR: kalau repair sedang jalan, paksa ke fase PAUSE ---
            bool repairBusy = IsAutoRepairBusy();
            if (repairBusy && phase != 5) phase = 5;

            bool entered = (phase != lastPhase);
            if (entered) {
                phaseStart = now;
                lastPhase = phase;
                if (phase == 2) {
                    castCrossfaded = false;
                    spinSeenCount = 0;
                }
            }
            uint64_t elapsed = now - phaseStart;

            const int diagFsm = ReadMyFsmState();
            const FishingStatusView diagFish = ReadFishingStatus();
            const int diagInFishing = diagFish.valid ? diagFish.inFishingState : -1;
            const int diagFishState = diagFish.valid ? diagFish.fishState : -1;
            bool diagTargetFound = false;
            const uint32_t diagFishId = (diagFish.valid && diagFish.baitNetId != 0) ? diagFish.baitNetId : targetFish;
            const uint32_t diagTargetSt = diagFishId != 0 ? ReadFishShadowByNetId(diagFishId, diagTargetFound) : 99;
            const bool diagChanged = phase != lastDiagPhase || diagFsm != lastDiagFsm ||
                diagInFishing != lastDiagInFishing || diagFishState != lastDiagFishState ||
                myFloat != lastDiagFloat;
            if (diagChanged || now - lastStateDiag >= 500) {
                LOGI((const char *) OBFUSCATE("[AutoFishDiag] phase=%d entered=%d elapsed=%llu fsm=%d "
                    "inFishing=%d fishState=%d float=%u baitNet=%u target=%u st=%u "
                    "hover=%d anchor=%d y=%.2f"),
                    phase, entered ? 1 : 0, (unsigned long long) elapsed, diagFsm,
                    diagInFishing, diagFishState, myFloat, diagFish.baitNetId, targetFish, diagTargetSt,
                    hoverEnabled ? 1 : 0, g_autoFishHoverAnchorValid ? 1 : 0,
                    g_autoFishHoverAnchorY);
                lastStateDiag = now;
                lastDiagPhase = phase;
                lastDiagFsm = diagFsm;
                lastDiagInFishing = diagInFishing;
                lastDiagFishState = diagFishState;
                lastDiagFloat = myFloat;
            }

            switch (phase) {
            case 0: { // EQUIP ROD
                g_autoFishStatus = u8"装备鱼竿";
                if (entered) equippedNow = EnsureRodEquipped(g_autoFishRodId);
                if (!equippedNow || elapsed >= equipDelayMs) phase = 1;
                break;
            }
            case 1: { // SCAN & TARGET LOCK
                g_autoFishStatus = u8"扫描目标";
                cachedFish = 0; targetFish = 0; wasFishing = false; responded = false; exitAfterCatch = false; rodPulled = false; tensionHoldingRelease = false;
                Vector3 selfPos{};
                if (!GetEntityPosByNetId(selfNetId, selfPos)) {
                    void *imgLevel = nullptr;
                    void *statusObj = GetSelfSyncStatusObj(imgLevel);
                    (void) statusObj;
                    if (imgLevel) {
                        void *entityUtilKlass = _monoClassFromName(imgLevel,
                            (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), (const char *) OBFUSCATE("EntityUtil"));
                        void *getSelf = entityUtilKlass ? FindMethodInHierarchy(
                            entityUtilKlass, (const char *) OBFUSCATE("GetSelfPlayer"), 0) : nullptr;
                        void *exc = nullptr;
                        void *self = getSelf ? _monoRuntimeInvoke(getSelf, nullptr, nullptr, &exc) : nullptr;
                        if (self && !exc) selfPos = NativeGetPosition(self);
                    }
                }
                uint32_t ignoreId = (lastCaughtFish != 0 && (now - lastCaughtTime < 1500)) ? lastCaughtFish : 0;
                float bestD = 0; Vector3 bestPos{};
                uint32_t best = ScanNearestFish(selfPos, g_autoFishRadius, bestD, bestPos, ignoreId);
                if (best == 0 && ignoreId != 0) {
                    best = ScanNearestFish(selfPos, g_autoFishRadius, bestD, bestPos, 0);
                }
                if (best != 0) {
                    targetFish = best;
                    targetFishPos = bestPos;
                    char tgtBuf[64];
                    snprintf(tgtBuf, sizeof(tgtBuf), "NetId %u (%.1fm)", best, bestD);
                    g_autoFishTargetStatus = tgtBuf;

                    if (g_autoFishTeleport) {
                        g_autoFishStatus = u8"传送中";
                        Vector3 stand = ComputeStandPos(bestPos, selfPos, g_autoFishGap);
                        if (g_autoFishHoverBetweenCasts)
                            g_autoFishHoverAnchorValid = false;
                        AutoMoveTransferNative(stand);
                        if (g_autoFishHoverBetweenCasts)
                            SetAutoFishHoverNative(true);
                        phase = 6;
                        LOGI((const char *) OBFUSCATE("[AutoFish] teleport ikan=%u target=(%.2f,%.2f,%.2f) stand=(%.2f,%.2f,%.2f)"),
                            best, bestPos.x, bestPos.y, bestPos.z, stand.x, stand.y, stand.z);
                    }
                    else {
                        g_autoFishStatus = u8"抛竿中";
                        FacePlayerTowardTargetNative(targetFishPos);
                        ForceFishingRodThrowTargetNative(targetFishPos);
                        CastFishingNative(1, targetFishPos);
                        lastCastAttempt = now;
                        LOGI((const char *) OBFUSCATE("[AutoFish] Fast throw to ikan=%u (%.1fm)"), best, bestD);
                        phase = 2;
                    }
                }
                else {
                    g_autoFishTargetStatus = u8"范围内无目标";
                    if (now - lastNoFishLog > 3000) { LOGI((const char *) OBFUSCATE("[AutoFish] tak ada ikan dlm radius %.0f"), g_autoFishRadius); lastNoFishLog = now; }
                    phase = 0;
                }
                break;
            }
            case 2: { // CASTING / ACCELERATED ANIMATION SKIP
                g_autoFishStatus = u8"抛竿中";
                if (g_autoFishSkipCastAnim && !castCrossfaded) {
                    bool inSpin = false;
                    if (SkipCastAnimNative(spinSeenCount >= 1, inSpin)) {
                        castCrossfaded = true;
                        LOGI((const char *) OBFUSCATE("[AutoFish] Fast Cast CrossFade success -> direct Fishing state! elapsed=%llu ms"), (unsigned long long) elapsed);
                    } else if (inSpin) {
                        spinSeenCount++;
                    }
                }
                const int castFsm = ReadMyFsmState();
                FishingStatusView castState = ReadFishingStatus();
                const bool castAccepted = (castState.valid && castState.inFishingState != 0) || castFsm == 24;
                if (castAccepted) {
                    if (isInstantFishing) {
                        if (TryArmFishingInstantCatchNative()) {
                            lastInstantArmTime = now;
                        }
                    }
                    g_autoFishStatus = u8"等待咬钩";
                    LOGI((const char *) OBFUSCATE("[AutoFishDiag] CAST_ACCEPTED target=%u fsm=%d inFishing=%d after=%llu ms"),
                        targetFish, castFsm, castState.valid ? castState.inFishingState : -1, (unsigned long long) elapsed);
                    phase = 3;
                }
                else if (now - lastCastAttempt >= 1200) {
                    FacePlayerTowardTargetNative(targetFishPos);
                    ForceFishingRodThrowTargetNative(targetFishPos);
                    CastFishingNative(1, targetFishPos);
                    lastCastAttempt = now;
                }
                else if (elapsed > 4000) {
                    phase = 6;
                }
                break;
            }
            case 3: { // FISHING: WAITING FOR BITE & BATTLE
                FishingStatusView fs = ReadFishingStatus();
                if (fpos.x == 0.0f && fpos.y == 0.0f && fpos.z == 0.0f) {
                    if (fs.valid && (fs.floatPos.x != 0.0f || fs.floatPos.z != 0.0f)) {
                        fpos = fs.floatPos;
                    } else if (fs.valid && (fs.tarPos.x != 0.0f || fs.tarPos.z != 0.0f)) {
                        fpos = fs.tarPos;
                    } else if (targetFishPos.x != 0.0f || targetFishPos.z != 0.0f) {
                        fpos = targetFishPos;
                    }
                }

                if (myFloat == 0) {
                    if (fs.valid && fs.inFishingState != 0) {
                        myFloat = fs.rodNetId ? fs.rodNetId : 1;
                    } else {
                        cancelOnReset = false; phase = 4; break;
                    }
                }

                // Always prioritize the fish currently biting/targeting our buoy (fs.baitNetId)
                bool found = false; uint32_t st = 99;
                if (fs.valid && fs.baitNetId != 0) {
                    cachedFish = fs.baitNetId;
                } else if (cachedFish == 0) {
                    cachedFish = targetFish;
                }
                if (cachedFish != 0) {
                    st = ReadFishShadowByNetId(cachedFish, found);
                    if (!found && (!fs.valid || fs.baitNetId == 0)) cachedFish = 0;
                }
                if (cachedFish == 0) {
                    float d = 0; Vector3 p{}; uint32_t b = ScanNearestFish(fpos, 9999.0f, d, p);
                    if (b != 0) { cachedFish = b; st = ReadFishShadowByNetId(b, found); }
                }
                int fsm = ReadMyFsmState();
                if (fsm == 24) wasFishing = true;

                if (entered) {
                    responded = false;
                    rodPulled = false;
                    tensionHoldingRelease = false;
                    g_autoFishStatus = u8"等待咬钩";
                }

                // CrossFade skip animation jika aktif
                if (g_autoFishSkipCastAnim) {
                    bool inSpin = false;
                    SkipCastAnimNative(true, inSpin);
                }

                // Strike / Bite check:
                // FishShadowAiState: 4 = Battle, 5 = Escape (hooked fish pulling away before/between player pulls), 6 = Succeed
                // FishingSubState:   2 = Battle, 5 = FishingSucc
                bool respNow = (st == 4 || st == 5 || st == 6)
                    || (fs.valid && (fs.fishState == 2 || fs.fishState == 5));

                if (respNow && !responded) {
                    responded = true;
                    tensionHoldingRelease = false;
                    lastPullTime = 0;
                    if (isInstantFishing) {
                        TryArmFishingInstantCatchNative();
                        lastInstantArmTime = now;
                    }
                    LOGI((const char *) OBFUSCATE("[AutoFish] IKAN STRIKE / BITE! (st=%u fss=%d bait=%u)"), st, fs.fishState, fs.baitNetId);
                }

                if (responded) {
                    // Caught! (FishShadowAiState.Succeed == 6 or FishingSubState.FishingSucc == 5)
                    if (st == 6 || (fs.valid && fs.fishState == 5)) {
                        PullFishingRodNative(false);
                        rodPulled = false;
                        tensionHoldingRelease = false;
                        lastCaughtFish = (fs.valid && fs.baitNetId != 0) ? fs.baitNetId : (cachedFish != 0 ? cachedFish : targetFish);
                        lastCaughtTime = now;
                        g_autoFishSessionCatches++;
                        char catchBuf[64];
                        snprintf(catchBuf, sizeof(catchBuf), "Caught Fish #%d", g_autoFishSessionCatches);
                        g_autoFishStatus = catchBuf;
                        LOGI((const char *) OBFUSCATE("[AutoFish] TERTANGKAP (st=%u fss=%d, total=%d)"), st, fs.fishState, g_autoFishSessionCatches);

                        // Clean up fishing state on catch
                        TakeUpRodNative(0);
                        CancelFishingNative();
                        if (g_autoFishSkipCatchAnim) {
                            LOGI((const char *) OBFUSCATE("[AutoFish] Skip catch animation: TakeUpRod & Cancel sent"));
                        }

                        cancelOnReset = false;
                        exitAfterCatch = true;
                        phase = 4;
                        break;
                    }

                    // Battle phase: BOTH st==4 (Battle) and st==5 (Escape) are active hooked states when fishState==2!
                    if (st == 4 || st == 5 || (fs.valid && fs.fishState == 2)) {
                        g_autoFishStatus = u8"遛鱼中";
                        float tension = ReadRodDurabilityNative();
                        bool wantPull = true;
                        if (tension <= 0.15f) {
                            tensionHoldingRelease = true;
                            wantPull = false;
                        }
                        else if (tensionHoldingRelease && tension < 0.35f) {
                            wantPull = false;
                        }
                        else {
                            tensionHoldingRelease = false;
                            wantPull = true;
                        }

                        bool stateMismatch = fs.valid && (fs.fishState == 2) && ((fs.pressed != 0) != wantPull);
                        bool periodicReassert = wantPull && (now - lastPullTime >= 120);
                        bool immediateFirstPull = wantPull && !rodPulled;
                        if (immediateFirstPull || ((wantPull != rodPulled || stateMismatch || periodicReassert) && (now - lastPullTime >= 50))) {
                            if (isInstantFishing && wantPull) {
                                TryArmFishingInstantCatchNative();
                                lastInstantArmTime = now;
                            }
                            PullFishingRodNative(wantPull);
                            rodPulled = wantPull;
                            lastPullTime = now;
                        }
                    }

                    // Actual Escaped / Failed states:
                    // FishingSubState: 3 = FishingFail, 4 = BattleFailSlack
                    // FishShadowAiState: 7 = RunAway (only when not in active Battle/Succ)
                    bool battleFailed = fs.valid && (fs.fishState == 3 || fs.fishState == 4);
                    bool fishRanAway = (st == 7) && (!fs.valid || (fs.fishState != 2 && fs.fishState != 5));
                    if (battleFailed || fishRanAway) {
                        PullFishingRodNative(false);
                        rodPulled = false;
                        tensionHoldingRelease = false;
                        g_autoFishStatus = u8"鱼脱钩了";
                        LOGI((const char *) OBFUSCATE("[AutoFish] ikan pergi/gagal (st=%u fss=%d) -> recast"), st, fs.fishState);
                        cancelOnReset = true;
                        phase = 4;
                        break;
                    }

                    if (wasFishing && fsm == 1 && (!fs.valid || fs.inFishingState == 0)) {
                        PullFishingRodNative(false);
                        rodPulled = false;
                        tensionHoldingRelease = false;
                        cancelOnReset = false;
                        phase = 4;
                        break;
                    }

                    if (elapsed > respStuckMs) {
                        PullFishingRodNative(false);
                        rodPulled = false;
                        tensionHoldingRelease = false;
                        LOGI((const char *) OBFUSCATE("[AutoFish] respon macet -> cancel paksa"));
                        cancelOnReset = true;
                        phase = 4;
                        break;
                    }
                }
                else {
                    // Also check if fish ran away or fishing failed before strike
                    if ((fs.valid && (fs.fishState == 3 || fs.fishState == 4)) || (st == 7 && (fs.valid && fs.baitNetId == cachedFish))) {
                        LOGI((const char *) OBFUSCATE("[AutoFish] gagal sebelum strike (st=%u fss=%d) -> recast"), st, fs.fishState);
                        cancelOnReset = true;
                        phase = 4;
                        break;
                    }
                    if (cachedFish != 0 && st < 2 && (fpos.x != 0.0f || fpos.z != 0.0f)) {
                        UpdateFishShadowPositionNative(cachedFish, fpos);
                    }
                    uint64_t currentTimeout = (st == 2 || st == 3 || (fs.valid && fs.baitNetId != 0)) ? respStuckMs : biteTimeoutMs;
                    if (elapsed > currentTimeout) {
                        LOGI((const char *) OBFUSCATE("[AutoFish] tak ada respon -> angkat (st=%u fss=%d)"), st, fs.fishState);
                        cancelOnReset = true;
                        phase = 4;
                        break;
                    }
                }
                break;
            }
            case 4: { // EXIT & RECAST
                if (entered) {
                    if (cancelOnReset) {
                        TakeUpRodNative(0);
                        CancelFishingNative();
                    }
                }

                FishingStatusView fsReset = ReadFishingStatus();
                int fsmReset = ReadMyFsmState();
                bool stateCleared = (!fsReset.valid || fsReset.inFishingState == 0) && (fsmReset == 1 || elapsed >= 250);

                if (exitAfterCatch) {
                    g_autoFishStatus = u8"重新抛竿";
                    // Send exit & reset
                    TakeUpRodNative(0);
                    CancelFishingNative();

                    // Fast recast delay: 30ms if skip animation is enabled, otherwise 100ms
                    const uint64_t recastDelay = (g_autoFishSkipCastAnim || g_autoFishSkipCatchAnim) ? 30 : 100;
                    if (elapsed >= recastDelay && (stateCleared || elapsed >= 400)) {
                        exitAfterCatch = false;
                        cachedFish = 0; targetFish = 0; wasFishing = false; responded = false;
                        cancelOnReset = false; rodPulled = false; phase = 1;
                        LOGI((const char *) OBFUSCATE("[AutoFish] INSTANT RECAST after catch (elapsed=%llu ms)"), (unsigned long long) elapsed);
                    }
                }
                else {
                    const uint64_t failDelay = (std::min)(cooldownMs, (uint64_t) 400);
                    if (elapsed >= failDelay && (stateCleared || elapsed >= 1000)) {
                        TakeUpRodNative(0);
                        CancelFishingNative();
                        cachedFish = 0; targetFish = 0; wasFishing = false; responded = false; cancelOnReset = false; rodPulled = false; phase = 0;
                    }
                }
                break;
            }
            case 6: { // WAIT SETTLE AFTER TELEPORT
                g_autoFishStatus = u8"调整站位";
                bool targetFound = false;
                const uint32_t targetState = targetFish != 0
                    ? ReadFishShadowByNetId(targetFish, targetFound) : 99;
                if (!targetFound || targetState >= (uint32_t) FISH_SHADOW_BATTLE) {
                    LOGI((const char *) OBFUSCATE("[AutoFishDiag] WAIT_ABORT target=%u found=%d targetState=%u"),
                        targetFish, targetFound ? 1 : 0, targetState);
                    phase = 1;
                    break;
                }

                const uint64_t settleDelay = (g_autoFishSkipCastAnim || g_autoFishSkipCatchAnim) ? 50 : 250;
                if (elapsed >= settleDelay) {
                    g_autoFishStatus = u8"抛竿中";
                    FacePlayerTowardTargetNative(targetFishPos);
                    ForceFishingRodThrowTargetNative(targetFishPos);
                    CastFishingNative(1, targetFishPos);
                    lastCastAttempt = now;
                    phase = 2;
                    LOGI((const char *) OBFUSCATE("[AutoFish] Teleport settle done -> Fast throw to target"));
                }
                break;
            }
            case 5: { // PAUSE: Auto Repair sedang jalan
                g_autoFishStatus = u8"已暂停（自动修理）";
                if (entered) {
                    if (myFloat != 0) CancelFishingNative();
                    LOGI((const char *) OBFUSCATE("[AutoFish] Auto Repair aktif -> fishing DIJEDA"));
                }
                if (!repairBusy) {
                    LOGI((const char *) OBFUSCATE("[AutoFish] repair selesai -> lanjut fishing"));
                    cachedFish = 0; targetFish = 0; wasFishing = false; responded = false; cancelOnReset = false; rodPulled = false; phase = 0;
                }
                break;
            }
            }
        });
        SleepMs((phase == 2 || phase == 6) ? 30 : ((phase == 3 || phase == 4) ? 20 : 50));
    }
    // Pulihkan physics player ketika Auto Fishing/hover dimatikan.
    RunOnUnityThreadSync([]() {
        _monoThreadAttach(_monoGetRootDomain());
        SetPlayerAnimSpeedNative(1.0f);
        if (!g_autoFishingActive ||
            !g_autoFishHoverBetweenCasts ||
            !g_autoFishTeleport)
            SetAutoFishHoverNative(false);
    });
    g_autoFishStatus = u8"已停止";
    LOGI((const char *) OBFUSCATE("[AutoFish] STOPPED"));
}

// ---------- Auto Eat ----------
static void *GetPlayerServiceInstance() {
    void *img = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (img) {
        void *klass = _monoClassFromName(img, OBFUSCATE("XDTGameSystem.PlayerService"), OBFUSCATE("PlayerServiceSystem"));
        if (klass) {
            void *m = FindMethodInHierarchy(klass, OBFUSCATE("get_Instance"), 0);
            if (!m) m = FindMethodInHierarchy(klass, OBFUSCATE("get_instance"), 0);
            if (m) {
                void *exc = nullptr;
                void *inst = _monoRuntimeInvoke(m, nullptr, nullptr, &exc);
                if (inst && !exc) return inst;
            }
        }
    }
    void *mgr = GetManagerInstanceNative(OBFUSCATE("PlayerServiceSystem"));
    if (!mgr) mgr = GetManagerInstanceNative(OBFUSCATE("PlayerService"));
    if (!mgr) LOGE(OBFUSCATE("[AutoEat] PlayerServiceSystem instance TIDAK ketemu"));
    return mgr;
}
static int GetStaminaNative() {
    void *inst = GetPlayerServiceInstance();
    if (!inst) return -1;
    void *klass = _monoObjectGetClass(inst);
    void *m = FindMethodInHierarchy(klass, OBFUSCATE("GetStamina"), 0);
    if (!m) { LOGE(OBFUSCATE("[AutoEat] GetStamina null")); return -1; }
    void *exc = nullptr;
    void *r = _monoRuntimeInvoke(m, inst, nullptr, &exc);
    if (r && !exc) return *(int *) _monoObjectUnbox(r);
    return -1;
}
static int IsStaminaFullNative() {
    void *inst = GetPlayerServiceInstance();
    if (!inst) return -1;
    void *klass = _monoObjectGetClass(inst);
    void *m = FindMethodInHierarchy(klass, OBFUSCATE("IsStaminaFull"), 0);
    if (!m) { LOGE(OBFUSCATE("[AutoEat] IsStaminaFull null")); return -1; }
    void *exc = nullptr;
    void *r = _monoRuntimeInvoke(m, inst, nullptr, &exc);
    if (r && !exc) return (*(uint8_t *) _monoObjectUnbox(r)) ? 1 : 0;
    return -1;
}
static void EatFoodNative(uint32_t foodNetId) {
    _monoThreadAttach(_monoGetRootDomain());
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) return;
    void *klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol.ProtocolService.GamePlay.Character"), OBFUSCATE("CharacterProtocolManager"));
    if (!klass) { LOGE(OBFUSCATE("[AutoEat] CharacterProtocolManager null")); return; }
    void *m = FindMethodInHierarchy(klass, OBFUSCATE("EatFood"), 1);
    if (!m) { LOGE(OBFUSCATE("[AutoEat] EatFood null")); return; }
    uint32_t nid = foodNetId; void *args[] = { &nid }; void *exc = nullptr;
    _monoRuntimeInvoke(m, nullptr, args, &exc); // static
}
void ScanFoodItems() {
    _monoThreadAttach(_monoGetRootDomain());
    void *img = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!img) return;
    void *klass = _monoClassFromName(img, OBFUSCATE("XDTGameSystem.GameplaySystem.BackPack"), OBFUSCATE("BackPackSystem"));
    if (!klass) return;
    void *m_getInst = FindMethodInHierarchy(klass, OBFUSCATE("get_Instance"), 0);
    if (!m_getInst) return;
    void *exc = nullptr;
    void *inst = _monoRuntimeInvoke(m_getInst, nullptr, nullptr, &exc);
    if (!inst || exc) return;

    void *m_GetAllItem = FindMethodInHierarchy(klass, OBFUSCATE("GetAllItem"), 1);
    if (!m_GetAllItem) return;
    int storageType = 1; // EStorageType.Backpack
    void *args[1] = { &storageType };
    void *listObj = _monoRuntimeInvoke(m_GetAllItem, inst, args, &exc);
    if (!listObj || exc) return;

    void *listKlass = _monoObjectGetClass(listObj);
    void *sizeField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_size"));
    void *itemsField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_items"));
    if (!sizeField || !itemsField) return;
    int size = 0;
    _monoFieldGetValue(listObj, sizeField, (void **) &size);
    void *itemsArray = nullptr;
    _monoFieldGetValue(listObj, itemsField, &itemsArray);
    if (!itemsArray || size == 0) {
        std::lock_guard<std::mutex> lk(g_foodListMutex);
        g_foodList.clear();
        return;
    }

    void *bpItemKlass = _monoClassFromName(img, OBFUSCATE("XDTGameSystem.UISystem.BackPack"), OBFUSCATE("BackpackItem"));
    if (!bpItemKlass) return;
    uint32_t align = 0;
    int structSize = _monoClassValueSize(bpItemKlass, &align);

    void *fEntity = _monoClassGetFieldFromName(bpItemKlass, OBFUSCATE("entityType"));
    int offEntity = fEntity ? (_monoFieldGetOffset(fEntity) - MONO_FIELD_HEADER) : -1;
    void *m_getName = FindMethodInHierarchy(bpItemKlass, OBFUSCATE("get_Name"), 0);

    uintptr_t arrayDataStart = (uintptr_t) itemsArray + OFF_ARR_DATA;

    std::vector<FoodItem> found;
    for (int i = 0; i < size; i++) {
        uintptr_t structAddr = arrayDataStart + (i * structSize);
        uint32_t netId = *(uint32_t *) (structAddr + OFF_BP_NETID);
        int staticId = *(int *) (structAddr + OFF_BP_STATICID);
        int count = *(int *) (structAddr + OFF_BP_COUNT);
        int et = (offEntity >= 0) ? *(int *) (structAddr + offEntity) : -1;

        if (et != 45) continue; // hanya food
        if (netId == 0 || count <= 0) continue;

        std::string name = "Food";
        if (m_getName) {
            void *e2 = nullptr;
            void *strObj = _monoRuntimeInvoke(m_getName, (void *) structAddr, nullptr, &e2);
            if (strObj && !e2) name = ReadMonoStringNative(strObj);
        }
        FoodItem fi;
        fi.netId = netId; fi.staticId = staticId; fi.count = count; fi.name = name;
        found.push_back(fi);
    }

    {
        std::lock_guard<std::mutex> lk(g_foodListMutex);
        g_foodList = found;
        if (g_selectedFoodIdx >= (int) found.size()) g_selectedFoodIdx = 0;
    }
    LOGI(OBFUSCATE("[AutoEat] scan food selesai: %d jenis makanan"), (int) found.size());
}
void AutoEatLoop() {
    LOGI(OBFUSCATE("[AutoEat] STARTED"));
    bool eating = false;
    uint64_t lastLog = 0;

    while (g_autoEatActive) {
        RunOnUnityThreadSync([&]() {
            _monoThreadAttach(_monoGetRootDomain());

            int stamina = GetStaminaNative();
            if (stamina < 0) return; // gagal baca -> coba lagi nanti

            if (!eating) {
                if (stamina <= g_eatStaminaThreshold) {
                    eating = true;
                    LOGI(OBFUSCATE("[AutoEat] stamina %d <= %d -> MULAI makan"), stamina, g_eatStaminaThreshold);
                } else {
                    return; // belum lapar
                }
            }

            if (IsStaminaFullNative() == 1) {
                LOGI(OBFUSCATE("[AutoEat] stamina PENUH -> berhenti makan"));
                eating = false;
                return;
            }

            ScanFoodItems();
            uint32_t netId = 0; std::string fname;
            {
                std::lock_guard<std::mutex> lk(g_foodListMutex);
                auto &fl = g_foodList;
                if (!fl.empty()) {
                    int idx = g_selectedFoodIdx;
                    if (idx < 0 || idx >= (int) fl.size() || fl[idx].count <= 0) {
                        int alt = -1;
                        for (int i = 0; i < (int) fl.size(); i++) { if (fl[i].count > 0) { alt = i; break; } }
                        if (alt >= 0) {
                            g_selectedFoodIdx = alt;
                            idx = alt;
                            LOGI(OBFUSCATE("[AutoEat] makanan habis -> beralih ke '%s'"), fl[alt].name.c_str());
                        } else {
                            idx = -1;
                        }
                    }
                    if (idx >= 0 && idx < (int) fl.size() && fl[idx].count > 0) {
                        netId = fl[idx].netId; fname = fl[idx].name;
                    }
                }
            }

            if (netId == 0) {
                if (NowMs() - lastLog > 3000) {
                    LOGI(OBFUSCATE("[AutoEat] tidak ada makanan tersisa -> stop"));
                    lastLog = NowMs();
                }
                eating = false; // tak ada makanan, hentikan siklus makan
                return;
            }

            EatFoodNative(netId);
            LOGI(OBFUSCATE("[AutoEat] makan '%s' netId=%u (stamina=%d)"), fname.c_str(), netId, stamina);
        });
        SleepMs(800); // jeda antar makan biar tidak spam packet
    }
    LOGI(OBFUSCATE("[AutoEat] STOPPED"));
}


// ---------- Transfer2Entity (teleport ke entity/pemain) kini didefinisikan bersama AutoMoveTransferNative ----------

// ---------- GetHomelandClientService ----------
void *GetHomelandClientService() {
    _monoThreadAttach(_monoGetRootDomain());
    static void *s_getInflated = nullptr;
    if (!s_getInflated) {
        void *imgData = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
        if (!imgData) return nullptr;
        void *ecsKlass = _monoClassFromName(imgData, (const char *) OBFUSCATE("XDTDataAndProtocol.ProtocolService"), (const char *) OBFUSCATE("EcsService"));
        if (!ecsKlass) return nullptr;
        void *m_Get = FindMethodInHierarchy(ecsKlass, (const char *) OBFUSCATE("Get"), 1);
        if (!m_Get) return nullptr;
        std::vector<std::string> imgCandidates = {
            (const char *) OBFUSCATE("EcsSystem"), (const char *) OBFUSCATE("XDTDataAndProtocol"),
            (const char *) OBFUSCATE("XDTLevelAndEntity"), (const char *) OBFUSCATE("XDTBaseService"),
            (const char *) OBFUSCATE("XDTClientSystem"), (const char *) OBFUSCATE("Assembly-CSharp")
        };
        void *hcsKlass = nullptr;
        for (auto &in : imgCandidates) {
            void *img = _monoImageLoaded(in.c_str());
            if (!img) continue;
            hcsKlass = _monoClassFromName(img, (const char *) OBFUSCATE("ClientSystem.Homeland"), (const char *) OBFUSCATE("HomelandClientService"));
            if (hcsKlass) break;
        }
        if (!hcsKlass) return nullptr;
        void *typeArgs[1] = { _monoClassGetType(hcsKlass) };
        void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
        MonoGenericContext ctx = { nullptr, methodInst };
        s_getInflated = _mono_class_inflate_generic_method(m_Get, &ctx);
        if (!s_getInflated) return nullptr;
    }
    bool isLogError = false;
    void *args[1] = { &isLogError };
    void *exc = nullptr;
    void *inst = _monoRuntimeInvoke(s_getInflated, nullptr, args, &exc);
    if (exc) return nullptr;
    return inst;
}

// ---------- ExperimentForceSenderSwimNative (paksa 1 pemain berenang) ----------
void ExperimentForceSenderSwimNative(uint32_t targetNetId, bool enableSwim) {
    RunOnUnityThreadAsync([targetNetId, enableSwim]() {
        void *domain = _monoGetRootDomain();
        if (!domain) return;
        _monoThreadAttach(domain);
        if (targetNetId == 0) { LOGI(OBFUSCATE("[SwimExp] targetNetId 0, batal")); return; }

        void *imgLevel = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
        void *imgProtocol = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
        if (!imgLevel || !imgProtocol) { LOGI(OBFUSCATE("[SwimExp] image null")); return; }
        std::string NS = (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player");
        void *exc = nullptr;

        void *entitiesKlass = _monoClassFromName(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), (const char *) OBFUSCATE("Entities"));
        void *m_GetAnyEntity = FindMethodInHierarchy(entitiesKlass, (const char *) OBFUSCATE("GetAnyEntity"), 1);
        if (!m_GetAnyEntity) { LOGI(OBFUSCATE("[SwimExp] GetAnyEntity null")); return; }
        uint32_t argId = targetNetId; void *eArgs[1] = { &argId };
        void *entity = _monoRuntimeInvoke(m_GetAnyEntity, nullptr, eArgs, &exc);
        if (!entity || exc) { LOGI(OBFUSCATE("[SwimExp] entity null")); return; }

        void *entityKlass = _monoClassFromName(imgProtocol, (const char *) OBFUSCATE("XDTLevelAndEntity.Core.World"), (const char *) OBFUSCATE("Entity"));
        void *m_GetComponent = FindMethodInHierarchy(entityKlass, (const char *) OBFUSCATE("GetComponent"), 1);
        void *playerCompKlass = _monoClassFromName(imgLevel, NS.c_str(), (const char *) OBFUSCATE("BasePlayerComponent"));
        void *typeArgs[1] = { _monoClassGetType(playerCompKlass) };
        void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
        MonoGenericContext ctx = { nullptr, methodInst };
        void *m_GetPlayer = m_GetComponent ? _mono_class_inflate_generic_method(m_GetComponent, &ctx) : nullptr;
        bool paramTrue = true; void *compArgs[1] = { &paramTrue };
        void *targetPlayer = m_GetPlayer ? _monoRuntimeInvoke(m_GetPlayer, entity, compArgs, &exc) : nullptr;
        if (!targetPlayer || exc) { LOGI(OBFUSCATE("[SwimExp] targetPlayer null")); return; }

        void *m_getSync = FindMethodInHierarchy(entitiesKlass, (const char *) OBFUSCATE("get_SyncSystem"), 0);
        void *syncSystem = m_getSync ? _monoRuntimeInvoke(m_getSync, nullptr, nullptr, &exc) : nullptr;
        if (!syncSystem) { LOGI(OBFUSCATE("[SwimExp] SyncSystem null")); return; }
        void *syncKlass = _monoObjectGetClass(syncSystem);

        void *f_recv = FindFieldInHierarchy(syncKlass, (const char *) OBFUSCATE("_receivers"));
        void *recvDict = nullptr;
        if (f_recv) _monoFieldGetValue(syncSystem, f_recv, &recvDict);
        void *agent = nullptr;
        if (recvDict) {
            void *dictKlass = _monoObjectGetClass(recvDict);
            void *m_getItem = FindMethodInHierarchy(dictKlass, (const char *) OBFUSCATE("get_Item"), 1);
            if (m_getItem) {
                uint32_t k = targetNetId; void *iArgs[1] = { &k }; exc = nullptr;
                void *netReceiver = _monoRuntimeInvoke(m_getItem, recvDict, iArgs, &exc);
                if (netReceiver && !exc) {
                    void *nrK = _monoObjectGetClass(netReceiver);
                    void *f_agent = FindFieldInHierarchy(nrK, (const char *) OBFUSCATE("agent"));
                    if (f_agent) _monoFieldGetValue(netReceiver, f_agent, &agent);
                }
            }
        }
        if (!agent) { LOGI(OBFUSCATE("[SwimExp] agent null")); return; }

        void *m_regist = FindMethodInHierarchy(syncKlass, (const char *) OBFUSCATE("Regist"), 2);
        if (!m_regist) { LOGI(OBFUSCATE("[SwimExp] Regist null")); return; }
        bool pSender = true; void *rArgs[2] = { agent, &pSender }; exc = nullptr;
        _monoRuntimeInvoke(m_regist, syncSystem, rArgs, &exc);

        void *baseActorKlass = _monoClassFromName(imgLevel, NS.c_str(), (const char *) OBFUSCATE("BaseActorComponent"));
        void *statusField = _monoClassGetFieldFromName(baseActorKlass, (const char *) OBFUSCATE("Status"));
        void *statusObj = nullptr;
        if (statusField) _monoFieldGetValue(targetPlayer, statusField, &statusObj);
        if (!statusObj) { LOGI(OBFUSCATE("[SwimExp] statusObj null")); return; }
        void *swimStatusField = _monoClassGetFieldFromName(_monoObjectGetClass(statusObj), (const char *) OBFUSCATE("SwimStatus"));
        void *swimKlass = _monoClassFromName(imgLevel, NS.c_str(), (const char *) OBFUSCATE("SwimStatus"));
        void *isSwimField = _monoClassGetFieldFromName(swimKlass, (const char *) OBFUSCATE("isSwimming"));
        if (swimStatusField && isSwimField) {
            int b = _monoFieldGetOffset(swimStatusField);
            int o = _monoFieldGetOffset(isSwimField) - MONO_FIELD_HEADER;
            *((uint8_t *) statusObj + b + o) = enableSwim ? 1 : 0;
        }

        void *swimFieldKlass = _monoClassFromName(imgLevel, NS.c_str(), (const char *) OBFUSCATE("SwimStatus_Field_0"));
        if (!swimFieldKlass) swimFieldKlass = _monoClassFromName(imgProtocol, NS.c_str(), (const char *) OBFUSCATE("SwimStatus_Field_0"));
        void *m_onSend = swimFieldKlass ? FindMethodInHierarchy(swimFieldKlass, (const char *) OBFUSCATE("OnSend"), 2) : nullptr;
        if (m_onSend) {
            uint32_t k = targetNetId; void *args[2] = { &k, statusObj }; exc = nullptr;
            _monoRuntimeInvoke(m_onSend, nullptr, args, &exc);
        } else { LOGI(OBFUSCATE("[SwimExp] SwimStatus_Field_0/OnSend null")); }

        void *m_unreg = FindMethodInHierarchy(syncKlass, (const char *) OBFUSCATE("Unregist"), 1);
        if (m_unreg) {
            uint32_t k = targetNetId; void *uArgs[1] = { &k };
            _monoRuntimeInvoke(m_unreg, syncSystem, uArgs, nullptr);
        }
        LOGI(OBFUSCATE("[SwimExp] done."));
    });
}
// ---- nama pemain via HomelandClientService.GetPlayerName(uint) ----
std::string GetPlayerNameByNetId(uint32_t playerNetId) {
    if (playerNetId == 0) return "";
    void *inst = GetHomelandClientService();      // sudah ada di ModFeatures.cpp
    if (!inst) return "";
    void *klass = _monoObjectGetClass(inst);
    if (!klass) return "";
    void *m_getName = FindMethodInHierarchy(klass, (const char *) OBFUSCATE("GetPlayerName"), 1);
    if (!m_getName) return "";
    uint32_t argNetId = playerNetId;
    void *args[1] = {&argNetId};
    void *exc = nullptr;
    void *res = _monoRuntimeInvoke(m_getName, inst, args, &exc);
    if (!res || exc) return "";
    std::string name = ReadMonoStringNative(res);
    if (name == (const char *) OBFUSCATE("NULL") || name == (const char *) OBFUSCATE("Invalid_String")) return "";
    return name;
}

// ---- SCAN NEARBY PLAYER (dedicated, hanya player) ----
void ExecutePlayerScanner() {
    RunOnUnityThreadAsync([]() {
        _monoThreadAttach(_monoGetRootDomain());

        void *imgData  = _monoImageLoaded((const char *) OBFUSCATE("XDTDataAndProtocol"));
        void *imgLevel = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
        if (!imgData || !imgLevel) return;

        void *dataCenterKlass  = _monoClassFromName(imgData, (const char *) OBFUSCATE("XDTDataAndProtocol.ComponentsData"), (const char *) OBFUSCATE("DataCenter"));
        void *m_getCurrentData = FindMethodInHierarchy(dataCenterKlass, (const char *) OBFUSCATE("get_CurrentData"), 0);

        void *entitiesKlass  = _monoClassFromName(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), (const char *) OBFUSCATE("Entities"));
        void *m_GetAnyEntity = FindMethodInHierarchy(entitiesKlass, (const char *) OBFUSCATE("GetAnyEntity"), 1);

        void *entityKlass    = _monoClassFromName(imgData, (const char *) OBFUSCATE("XDTLevelAndEntity.Core.World"), (const char *) OBFUSCATE("Entity"));
        void *m_getPos       = FindMethodInHierarchy(entityKlass, (const char *) OBFUSCATE("get_position"), 0);
        void *m_GetComponent = FindMethodInHierarchy(entityKlass, (const char *) OBFUSCATE("GetComponent"), 1);
        if (!m_getCurrentData || !m_GetAnyEntity || !m_GetComponent) return;

        void *m_GetPlayer      = InflateGetComponent(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.Player"), (const char *) OBFUSCATE("BasePlayerComponent"), m_GetComponent);
        void *m_GetPlayerMorph = InflateGetComponent(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.HideAndSeek"), (const char *) OBFUSCATE("PlayerMorphComponent"), m_GetComponent);
        void *m_GetPlayerHider = InflateGetComponent(imgLevel, (const char *) OBFUSCATE("XDTLevelAndEntity.Gameplay.Component.HideAndSeek"), (const char *) OBFUSCATE("ClientHideAndSeekHiderPlacedComponent"), m_GetComponent);

        void *exc = nullptr;
        void *currentDataObj = _monoRuntimeInvoke(m_getCurrentData, nullptr, nullptr, &exc);
        if (!currentDataObj || exc) return;

        void *f_netMap = FindFieldInHierarchy(_monoObjectGetClass(currentDataObj), (const char *) OBFUSCATE("_netEntitiesMap"));
        if (!f_netMap) return;
        void *dictObj = nullptr;
        _monoFieldGetValue(currentDataObj, f_netMap, &dictObj);
        if (!dictObj) return;

        void *f_entries = FindFieldInHierarchy(_monoObjectGetClass(dictObj), (const char *) OBFUSCATE("entries"));
        if (!f_entries) f_entries = FindFieldInHierarchy(_monoObjectGetClass(dictObj), (const char *) OBFUSCATE("_entries"));
        if (!f_entries) return;
        void *entriesArr = nullptr;
        _monoFieldGetValue(dictObj, f_entries, &entriesArr);
        if (!entriesArr) return;

        int length = *(int *) ((uint8_t *) entriesArr + OFF_ARR_COUNT);
        if (length <= 0 || length > 50000) return;
        uint8_t *arrayDataStart = (uint8_t *) entriesArr + OFF_ARR_DATA;

        std::vector<RadarEntity> tempList;
        bool paramTrue = true;
        void *compArgs[1] = {&paramTrue};

        for (int i = 0; i < length; i++) {
            uint8_t *entryAddr = arrayDataStart + (i * SZ_DICT_ENTRY);
            if (*(int *) entryAddr < 0) continue;
            uint32_t targetId = *(uint32_t *) (entryAddr + OFF_ENTRY_KEY);
            if (targetId == 0) continue;

            void *cArgs[1] = {&targetId};
            exc = nullptr;
            void *entity = _monoRuntimeInvoke(m_GetAnyEntity, nullptr, cArgs, &exc);
            if (!entity || exc) continue;

            void *playerComp = nullptr;
            exc = nullptr;
            if (m_GetPlayer) playerComp = _monoRuntimeInvoke(m_GetPlayer, entity, compArgs, &exc);
            if (!playerComp) { exc = nullptr; if (m_GetPlayerMorph) playerComp = _monoRuntimeInvoke(m_GetPlayerMorph, entity, compArgs, &exc); }
            if (!playerComp) { exc = nullptr; if (m_GetPlayerHider) playerComp = _monoRuntimeInvoke(m_GetPlayerHider, entity, compArgs, &exc); }
            if (!playerComp) continue;               // bukan player -> skip

            std::string playerName = GetPlayerNameByNetId(targetId);
            if (playerName.empty()) playerName = (const char *) OBFUSCATE("Player");

            exc = nullptr;
            void *posObj = _monoRuntimeInvoke(m_getPos, entity, nullptr, &exc);
            if (!posObj || exc) continue;
            Vector3 pos = *(Vector3 *) _monoObjectUnbox(posObj);

            tempList.push_back({targetId, playerName, 0, pos, {0, 0, 0}, false});
        }

        std::lock_guard<std::mutex> lock(g_radarMutex);
        g_radarList = tempList;
    });
}


// =====================================================================
// AUTO PUZZLE (Jigsaw) â€” port dari versi PC (logika identik)
// Adaptasi: MonoNative::mono_* -> _mono*, ModdingUtils::* -> global helper,
//           _OBF -> OBFUSCATE, Sleep(ms) -> usleep(ms*1000), DebugLog -> LOGI/LOGE.
// DumpPuzzleSystemLayout (diagnostik) sengaja tidak diport (pakai IsBadReadPtr khas Windows).
// =====================================================================

// Kirim 1 keping ke papan: Lock (opsional) -> Bingo.
// Insight: SetJigsawPuzzlePieceBingo(uint pieceNetId) TIDAK butuh posisi,
// server yang auto-snap. Jadi cukup kirim netId keping.
// Pola persis meniru ExecChefAction (invoke ProtocolManager statis).
void PuzzleBingoPiece(uint32_t pieceNetId, bool lockFirst) {
    // PENTING: JANGAN pernah sleep di dalam lambda RunOnUnityThread* karena lambda itu
    // dieksekusi di MAIN THREAD game (render). Sleep di situ membekukan seluruh game.
    // Jeda dilakukan di THREAD BOT (pemanggil), bukan di main thread.
    if (lockFirst) {
        RunOnUnityThreadAsync([=]() {
            _monoThreadAttach(_monoGetRootDomain());
            auto PN = [](const char* k) -> std::string { return extractJsonString(g_puzzlePn, k); };
            if (g_puzzlePn.empty()) return;
            void *img = _monoImageLoaded(PN("n_imgData").c_str());
            if (!img) { LOGE("[Puzzle] LOCK GAGAL: img XDTDataAndProtocol NULL"); return; }
            void *klass = _monoClassFromName(img,
                                             PN("n_nsProtoPuzzle").c_str(),
                                             PN("n_clsProtoMgr").c_str());
            if (!klass) { LOGE("[Puzzle] LOCK GAGAL: klass JigsawPuzzleProtocolManager NULL"); return; }
            uint32_t valNetId = pieceNetId;
            // LockJigsawPuzzlePiece(uint pieceNetId, Action callback = null) -> 2 param
            void *mLock = FindMethodInHierarchy(klass, PN("n_lockPiece").c_str(), 2);
            if (mLock) {
                void *lockArgs[] = {&valNetId, nullptr};
                _monoRuntimeInvoke(mLock, nullptr, lockArgs, nullptr);
                LOGI("[Puzzle] Lock invoked netId=%u", pieceNetId);
            } else {
                LOGE("[Puzzle] method LockJigsawPuzzlePiece NULL (argc=2?)");
            }
        });
        usleep(250000); // di THREAD BOT, tidak membekukan game
    }

    RunOnUnityThreadAsync([=]() {
        _monoThreadAttach(_monoGetRootDomain());
            auto PN = [](const char* k) -> std::string { return extractJsonString(g_puzzlePn, k); };
            if (g_puzzlePn.empty()) return;
        void *img = _monoImageLoaded(PN("n_imgData").c_str());
        if (!img) { LOGE("[Puzzle] BINGO GAGAL: img XDTDataAndProtocol NULL"); return; }
        void *klass = _monoClassFromName(img,
                                         PN("n_nsProtoPuzzle").c_str(),
                                         PN("n_clsProtoMgr").c_str());
        if (!klass) { LOGE("[Puzzle] BINGO GAGAL: klass JigsawPuzzleProtocolManager NULL"); return; }
        uint32_t valNetId = pieceNetId;
        // SetJigsawPuzzlePieceBingo(uint pieceNetId) -> 1 param
        void *mBingo = FindMethodInHierarchy(klass, PN("n_setBingo").c_str(), 1);
        if (mBingo) {
            void *args[] = {&valNetId};
            _monoRuntimeInvoke(mBingo, nullptr, args, nullptr);
            LOGI("[Puzzle] Bingo invoked netId=%u", pieceNetId);
        } else {
            LOGE("[Puzzle] method SetJigsawPuzzlePieceBingo NULL (argc=1?)");
        }
    });
}

// Enumerasi netId keping yang masih di TAS (bag).
// Sumber kebenaran: JigsawPuzzleSystem._pieceDic (Dictionary<uint, JigsawPuzzlePieceItemData>).
// JigsawPuzzleSystem : DataModule<JigsawPuzzleSystem> -> singleton Instance ada di static class induk.
std::vector<uint32_t> ScanPuzzleBagPiecesNative() {
    std::vector<uint32_t> result;
    RunOnUnityThreadSync([&]() {
        _monoThreadAttach(_monoGetRootDomain());
            auto PN = [](const char* k) -> std::string { return extractJsonString(g_puzzlePn, k); };
            if (g_puzzlePn.empty()) return;
        LOGI("[PuzzleScan] step 1: mulai");

        void *img = _monoImageLoaded(PN("n_imgGameSys").c_str());
        if (!img) img = _monoImageLoaded(PN("n_imgGameSysDll").c_str());
        if (!img) { LOGE("[PuzzleScan] GAGAL step 2: image XDTGameSystem NULL (assembly beda?)"); return; }
        LOGI("[PuzzleScan] step 2 OK: img=%p", img);

        void *sysKlass = _monoClassFromName(img,
                                            PN("n_nsPuzzle").c_str(),
                                            PN("n_clsPuzzleSys").c_str());
        if (!sysKlass) { LOGE("[PuzzleScan] GAGAL step 3: class JigsawPuzzleSystem NULL (cek ns/assembly)"); return; }
        LOGI("[PuzzleScan] step 3 OK: sysKlass=%p", sysKlass);

        // --- Ambil singleton Instance (pola DataModule<T>) ---
        void *domain = _monoGetRootDomain();
        void *instance = nullptr;
        // std::string agar byte hasil OBFUSCATE tersalin (tidak dangling saat dipakai di loop).
        std::string instNames[] = {OBFUSCATE("<Instance>k__BackingField"), OBFUSCATE("instance"),
                                   OBFUSCATE("_instance"), OBFUSCATE("Instance"), OBFUSCATE("s_instance")};
        void *klassParent = _monoClassGetParent(sysKlass); // DataModule<JigsawPuzzleSystem>
        if (klassParent) {
            void *vtP = _monoClassGetVTable(domain, klassParent);
            for (int n = 0; n < 5 && !instance; n++) {
                void *fld = _monoClassGetFieldFromName(klassParent, instNames[n].c_str());
                if (fld && vtP) _monoFieldStaticGetValue(vtP, fld, &instance);
            }
            if (!instance) {
                void *getInst = FindMethodInHierarchy(klassParent, PN("n_getInstance").c_str(), 0);
                if (getInst) {
                    void *e0 = nullptr;
                    void *r0 = _monoRuntimeInvoke(getInst, nullptr, nullptr, &e0);
                    if (r0 && !e0) instance = r0;
                }
            }
        }
        if (!instance) {
            void *vtS = _monoClassGetVTable(domain, sysKlass);
            for (int n = 0; n < 5 && !instance; n++) {
                void *fld = _monoClassGetFieldFromName(sysKlass, instNames[n].c_str());
                if (fld && vtS) _monoFieldStaticGetValue(vtS, fld, &instance);
            }
        }
        if (!instance) { LOGE("[PuzzleScan] GAGAL step 4: Instance JigsawPuzzleSystem NULL (masuk minigame dulu)"); return; }
        LOGI("[PuzzleScan] step 4 OK: instance=%p", instance);

        // --- Ambil field _pieceDic ---
        void *f_pieceDic = _monoClassGetFieldFromName(sysKlass, PN("n_fPieceDic").c_str());
        if (!f_pieceDic) { LOGE("[PuzzleScan] GAGAL step 5: field _pieceDic NULL"); return; }
        void *dictObj = nullptr;
        _monoFieldGetValue(instance, f_pieceDic, &dictObj);
        if (!dictObj) { LOGE("[PuzzleScan] GAGAL step 6: _pieceDic NULL"); return; }
        void *dictKlass = _monoObjectGetClass(dictObj);

        // --- Hitung jumlah entry ---
        void *excD = nullptr;
        int dcount = 0;
        void *m_getCount = FindMethodInHierarchy(dictKlass, PN("n_getCount").c_str(), 0);
        if (m_getCount) {
            void *co = _monoRuntimeInvoke(m_getCount, dictObj, nullptr, &excD);
            if (co && !excD) dcount = *(int *) _monoObjectUnbox(co);
        }
        LOGI("[PuzzleScan] step 7 OK: _pieceDic count=%d", dcount);
        if (dcount <= 0) { LOGE("[PuzzleScan] GAGAL step 8: _pieceDic kosong (belum ada keping tersinkron)"); return; }

        // --- Enumerasi dictionary via GetEnumerator/MoveNext/get_Current ---
        excD = nullptr;
        void *m_getEnum = FindMethodInHierarchy(dictKlass, PN("n_getEnum").c_str(), 0);
        if (!m_getEnum) { LOGE("[PuzzleScan] GAGAL step 9: GetEnumerator NULL"); return; }
        void *enumObj = _monoRuntimeInvoke(m_getEnum, dictObj, nullptr, &excD);
        if (!enumObj || excD) { LOGE("[PuzzleScan] GAGAL step 10: enumObj NULL"); return; }
        void *enumKlass = _monoObjectGetClass(enumObj);
        // PENTING: Dictionary.GetEnumerator() balikin STRUCT (value type) yang di-box.
        // MoveNext/get_Current adalah instance method value-type, jadi 'this' HARUS pointer ke
        // data yang SUDAH di-unbox, bukan objek box-nya.
        void *enumData = _monoObjectUnbox(enumObj);
        if (!enumData) enumData = enumObj;
        void *m_moveNext = FindMethodInHierarchy(enumKlass, PN("n_moveNext").c_str(), 0);
        void *m_current = FindMethodInHierarchy(enumKlass, PN("n_getCurrent").c_str(), 0);
        if (!m_moveNext || !m_current) { LOGE("[PuzzleScan] GAGAL step 11: MoveNext/get_Current NULL"); return; }

        // GetPieceItemData(uint) -> boxed struct, untuk baca flag isInBag pada offset pasti.
        void *m_getPiece = FindMethodInHierarchy(sysKlass, PN("n_getPieceData").c_str(), 1);
        LOGI("[PuzzleScan] step 12 OK: mulai iterasi, GetPieceItemData=%p", m_getPiece);

        int scanned = 0, inBag = 0, guard = 0;
        while (guard++ < 100000) {
            void *ex2 = nullptr;
            void *mn = _monoRuntimeInvoke(m_moveNext, enumData, nullptr, &ex2);
            if (!mn || ex2) break;
            if (*(uint8_t *) _monoObjectUnbox(mn) == 0) break;
            void *kvp = _monoRuntimeInvoke(m_current, enumData, nullptr, &ex2);
            if (!kvp || ex2) continue;
            scanned++;
            // KeyValuePair<uint, struct> di-unbox: key(uint) di offset 0 = pieceNetId.
            uint32_t pieceNetId = *(uint32_t *) _monoObjectUnbox(kvp);
            if (pieceNetId == 0) continue;
            // Layout struct: pieceNetId@0, jigsawPuzzleBoardNetId@4, isTemp@8, isInBag@9, isInDraft@10.
            bool isBag = false;
            if (m_getPiece) {
                void *pex = nullptr;
                void *arg[] = {&pieceNetId};
                void *boxed = _monoRuntimeInvoke(m_getPiece, instance, arg, &pex);
                if (boxed && !pex) {
                    uint8_t *d = (uint8_t *) _monoObjectUnbox(boxed);
                    if (d) isBag = (d[9] != 0);
                }
            }
            if (isBag) { result.push_back(pieceNetId); inBag++; }
        }
        LOGI("[PuzzleScan] iterasi: total dicek=%d, di TAS(isInBag)=%d", scanned, inBag);
        LOGI("[PuzzleScan] SELESAI: ketemu %d keping di tas", (int) result.size());
    });
    return result;
}

// Sleep yang menghormati toggle puzzle (mirror SafeBotSleep).
void SafePuzzleSleep(float seconds) {
    int cycles = (int) (seconds * 10.0f);
    for (int i = 0; i < cycles; i++) {
        if (!g_isPuzzleBotRunning) break;
        usleep(100000);
    }
}

// Master loop: scan tas -> Lock+Bingo tiap keping -> ulang sampai tas kosong.
void AutoPuzzleBotThread() {
    g_botStatus = (const char *) OBFUSCATE("PUZZLE: STARTED");
    LOGI("[PuzzleBot] === START ===");
    int emptyStreak = 0;

    while (g_isPuzzleBotRunning) {
        g_botStatus = (const char *) OBFUSCATE("PUZZLE: SCAN BAG...");
        LOGI("[PuzzleBot] scanning...");
        std::vector<uint32_t> bag = ScanPuzzleBagPiecesNative();
        if (!g_isPuzzleBotRunning) break;

        if (bag.empty()) {
            emptyStreak++;
            LOGI("[PuzzleBot] bag kosong (streak=%d/3)", emptyStreak);
            g_botStatus = std::string((const char *) OBFUSCATE("PUZZLE: BAG KOSONG (")) +
                          std::to_string(emptyStreak) + (const char *) OBFUSCATE("/3)");
            if (emptyStreak >= 3) {
                LOGI("[PuzzleBot] STOP: 3x scan kosong beruntun.");
                g_botStatus = (const char *) OBFUSCATE("PUZZLE: SELESAI/KOSONG");
                g_isPuzzleBotRunning = false;
                break;
            }
            SafePuzzleSleep(1.0f);
            continue;
        }
        emptyStreak = 0;

        LOGI("[PuzzleBot] pasang %d keping", (int) bag.size());
        g_botStatus = std::string((const char *) OBFUSCATE("PUZZLE: PASANG (")) +
                      std::to_string(bag.size()) + (const char *) OBFUSCATE(")");
        for (uint32_t pieceNetId : bag) {
            if (!g_isPuzzleBotRunning) break;
            PuzzleBingoPiece(pieceNetId, true);
            SafePuzzleSleep(0.35f);
        }
        SafePuzzleSleep(0.8f);
    }
    LOGI("[PuzzleBot] === STOP ===");
    g_botStatus = (const char *) OBFUSCATE("IDLE");
}

// =====================================================================
//  PORT DARI PC: MUSIC PLAYER + IMPORT PICTURE + IMPORT AS AVATAR
//  Metode & alur dipertahankan sama seperti versi PC (ModFeatures.h).
//  Perbedaan hanya wrapper Mono (Win32 MonoNative::mono_* -> Android _mono*)
//  dan mono_array_new di-resolve dinamis lewat xdl (pola yang sama dgn
//  ExecuteInjectJpgToCanvasNative / Pet feed).
// =====================================================================

// Helper: resolve mono_array_new secara dinamis (sama pola dgn Auto Draw).
static void *AndroidMonoArrayNew(void *domain, void *byteKlass, size_t count) {
    static void *(*p)(void *, void *, uintptr_t) = nullptr;
    if (!p) {
        void *h = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), 0);
        if (!h) h = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), RTLD_NOW);
        if (!h) h = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), XDL_TRY_FORCE_LOAD);
        if (!h) return nullptr;
        p = (void *(*)(void *, void *, uintptr_t)) xdl_sym(h, (const char *) OBFUSCATE("mono_array_new"), (size_t *) nullptr);
        if (!p) {
            void *h2 = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), XDL_TRY_FORCE_LOAD);
            if (h2)
                p = (void *(*)(void *, void *, uintptr_t)) xdl_sym(h2, (const char *) OBFUSCATE("mono_array_new"), (size_t *) nullptr);
        }
    }
    if (!p) return nullptr;
    return p(domain, byteKlass, count);
}

// Offset data pertama array Mono (64-bit = 0x20, 32-bit = 0x10).
static inline size_t MonoArrayDataOffset() { return sizeof(void *) == 8 ? 0x20 : 0x10; }

// Baca seluruh isi file ke buffer byte.
static bool ReadWholeFile(const std::string &path, std::vector<uint8_t> &out) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) return false;
    std::streamoff sz = ifs.tellg();
    if (sz <= 0) return false;
    ifs.seekg(0, std::ios::beg);
    out.resize((size_t) sz);
    return (bool) ifs.read((char *) out.data(), sz);
}

// ------------------------------------------------------------------
//  MUSIC PLAYER: putar file .bin instrument lewat AudioPlaybackComponent.
// ------------------------------------------------------------------
void PlayMusicBinNative(std::string binPath, bool loop) {
    g_musicPn = FetchPayloadSync(WireForOp(OP_MUSIC_PLAYER)).payloadRaw;
    if (g_musicPn.empty()) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }

    std::vector<uint8_t> fileBytes;
    if (!ReadWholeFile(binPath, fileBytes) || fileBytes.size() < 6) {
        g_musicStatus = (const char *) OBFUSCATE("Failed");
        return;
    }
    // Validasi header .bin (sama seperti PC).
    if (*(int32_t *) fileBytes.data() != 1380270916 || *(int16_t *) (fileBytes.data() + 4) != 1) {
        g_musicStatus = (const char *) OBFUSCATE("Failed");
        return;
    }

    g_musicStatus = (const char *) OBFUSCATE("Process...");
    RunOnUnityThreadAsync([fileBytes, loop]() {
        void *domain = _monoGetRootDomain();
        if (!domain) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }
        _monoThreadAttach(domain);
        auto PN = [](const char *k) -> std::string { return extractJsonString(g_musicPn, k); };

        void *imgData = _monoImageLoaded(PN("n_imgData").c_str());
        void *imgGameplay = _monoImageLoaded(PN("n_imgLevel").c_str());
        if (!imgGameplay) imgGameplay = _monoImageLoaded(PN("n_imgLevelDll").c_str());
        if (!imgGameplay) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *pbKlass = _monoClassFromName(imgGameplay, PN("n_nsHomeland").c_str(), PN("n_clsAudioPlayback").c_str());
        if (!pbKlass) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }

        // EntityWorld.GetComponentSystem<AudioPlaybackComponent>()
        void *pbType = _monoClassGetType(pbKlass);
        void *worldKlass = _monoClassFromName(imgData, PN("n_nsWorld").c_str(), PN("n_clsEntityWorld").c_str());
        if (!worldKlass) worldKlass = _monoClassFromName(imgGameplay, PN("n_nsWorld").c_str(), PN("n_clsEntityWorld").c_str());
        void *openMethod = worldKlass ? FindMethodInHierarchy(worldKlass, PN("n_getCompSys").c_str(), 0) : nullptr;
        if (!openMethod) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *typeArgs[] = { pbType };
        void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
        MonoGenericContext ctx = { nullptr, methodInst };
        void *closedMethod = _mono_class_inflate_generic_method(openMethod, &ctx);
        if (!closedMethod) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *exc = nullptr;
        void *sysObj = _monoRuntimeInvoke(closedMethod, nullptr, nullptr, &exc);
        if (exc || !sysObj) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *sysKlass = _monoObjectGetClass(sysObj);
        void *f_components = FindFieldInHierarchy(sysKlass, PN("n_fComponents").c_str());
        if (!f_components) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *dictObj = nullptr;
        _monoFieldGetValue(sysObj, f_components, &dictObj);
        if (!dictObj) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *dictKlass = _monoObjectGetClass(dictObj);
        void *f_entries = FindFieldInHierarchy(dictKlass, PN("n_entries").c_str());
        if (!f_entries) f_entries = FindFieldInHierarchy(dictKlass, PN("n_entriesU").c_str());
        if (!f_entries) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *entriesArr = nullptr;
        _monoFieldGetValue(dictObj, f_entries, &entriesArr);
        if (!entriesArr) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }
        uintptr_t arrAddr = (uintptr_t) entriesArr;
        int length = *(int *) (arrAddr + 0x18);
        uintptr_t data = arrAddr + 0x20;
        g_musicStatus = (const char *) OBFUSCATE("Process...");

        void *mscorlib = _monoImageLoaded(PN("n_imgMscorlib").c_str());
        if (!mscorlib) mscorlib = _monoImageLoaded(PN("n_imgCoreLib").c_str());
        void *byteKlass = mscorlib ? _monoClassFromName(mscorlib, PN("n_sysNs").c_str(), PN("n_byte").c_str()) : nullptr;
        if (!byteKlass) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *m_play = FindMethodInHierarchy(pbKlass, PN("n_startPlayback").c_str(), 4);
        if (!m_play) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *m_stopReset = FindMethodInHierarchy(pbKlass, PN("n_stopPlayback").c_str(), 0);
        void *m_getEntity = FindMethodInHierarchy(pbKlass, PN("n_getEntity").c_str(), 0);
        if (!m_getEntity) m_getEntity = FindMethodInHierarchy(pbKlass, PN("n_getEntity2").c_str(), 0);
        void *f_isPlaying = FindFieldInHierarchy(pbKlass, PN("n_fIsPlaying").c_str());
        void *f_entityPos = FindFieldInHierarchy(pbKlass, PN("n_fEntityPos").c_str());

        int played = 0;
        size_t arrOff = MonoArrayDataOffset();
        for (int i = 0; i < length && played == 0; i++) {
            int hash = *(int *) (data + (i * 24));
            if (hash < 0) continue;
            void *pbComp = *(void **) (data + (i * 24) + 16);
            if (!pbComp) continue;
            g_musicStatus = (const char *) OBFUSCATE("Process...");

            // Reset hanya jika sedang playing (hindari StopPlayback saat belum pernah main).
            if (m_stopReset && f_isPlaying) {
                bool alreadyPlaying = false;
                _monoFieldGetValue(pbComp, f_isPlaying, (void *) &alreadyPlaying);
                if (alreadyPlaying) { exc = nullptr; _monoRuntimeInvoke(m_stopReset, pbComp, nullptr, &exc); }
            }

            // Posisi playback = posisi entity gramofon.
            Vector3 pos = { 0, 0, 0 };
            if (f_entityPos) _monoFieldGetValue(pbComp, f_entityPos, (void *) &pos);
            if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f && m_getEntity) {
                exc = nullptr;
                void *entityObj = _monoRuntimeInvoke(m_getEntity, pbComp, nullptr, &exc);
                if (entityObj && !exc) {
                    void *entKlass = _monoObjectGetClass(entityObj);
                    void *m_getPos = FindMethodInHierarchy(entKlass, PN("n_getLocalPos").c_str(), 0);
                    if (!m_getPos) m_getPos = FindMethodInHierarchy(entKlass, PN("n_getPos").c_str(), 0);
                    if (m_getPos) {
                        exc = nullptr;
                        void *posRes = _monoRuntimeInvoke(m_getPos, entityObj, nullptr, &exc);
                        if (posRes && !exc) memcpy(&pos, _monoObjectUnbox(posRes), sizeof(Vector3));
                    }
                }
            }

            // byte[] fresh per komponen, isi = file .bin.
            void *monoArr = AndroidMonoArrayNew(domain, byteKlass, fileBytes.size());
            if (!monoArr) continue;
            memcpy((uint8_t *) ((uintptr_t) monoArr + arrOff), fileBytes.data(), fileBytes.size());

            bool bLoop = loop;
            int repeat = -1;
            void *playArgs[] = { &pos, monoArr, &bLoop, &repeat };
            exc = nullptr;
            _monoRuntimeInvoke(m_play, pbComp, playArgs, &exc);
            if (!exc) played++;
        }

        g_musicStatus = played > 0 ? (const char *) OBFUSCATE("Success")
                                   : (const char *) OBFUSCATE("Failed");
    });
}

// Hentikan semua playback gramofon.
void StopMusicPlaybackNative() {
    g_musicPn = FetchPayloadSync(WireForOp(OP_MUSIC_PLAYER)).payloadRaw;
    if (g_musicPn.empty()) { g_musicStatus = (const char *) OBFUSCATE("Failed"); return; }
    RunOnUnityThreadAsync([]() {
        void *domain = _monoGetRootDomain();
        if (!domain) return;
        _monoThreadAttach(domain);
        auto PN = [](const char *k) -> std::string { return extractJsonString(g_musicPn, k); };
        void *imgData = _monoImageLoaded(PN("n_imgData").c_str());
        void *imgGameplay = _monoImageLoaded(PN("n_imgLevel").c_str());
        if (!imgGameplay) imgGameplay = _monoImageLoaded(PN("n_imgLevelDll").c_str());
        if (!imgGameplay) return;
        void *pbKlass = _monoClassFromName(imgGameplay, PN("n_nsHomeland").c_str(), PN("n_clsAudioPlayback").c_str());
        if (!pbKlass) return;
        void *pbType = _monoClassGetType(pbKlass);
        void *worldKlass = _monoClassFromName(imgData, PN("n_nsWorld").c_str(), PN("n_clsEntityWorld").c_str());
        if (!worldKlass) worldKlass = _monoClassFromName(imgGameplay, PN("n_nsWorld").c_str(), PN("n_clsEntityWorld").c_str());
        void *openMethod = worldKlass ? FindMethodInHierarchy(worldKlass, PN("n_getCompSys").c_str(), 0) : nullptr;
        if (!openMethod) return;
        void *typeArgs[] = { pbType };
        void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
        MonoGenericContext ctx = { nullptr, methodInst };
        void *closedMethod = _mono_class_inflate_generic_method(openMethod, &ctx);
        if (!closedMethod) return;
        void *exc = nullptr;
        void *sysObj = _monoRuntimeInvoke(closedMethod, nullptr, nullptr, &exc);
        if (exc || !sysObj) return;
        void *sysKlass = _monoObjectGetClass(sysObj);
        void *f_components = FindFieldInHierarchy(sysKlass, PN("n_fComponents").c_str());
        if (!f_components) return;
        void *dictObj = nullptr;
        _monoFieldGetValue(sysObj, f_components, &dictObj);
        if (!dictObj) return;
        void *dictKlass = _monoObjectGetClass(dictObj);
        void *f_entries = FindFieldInHierarchy(dictKlass, PN("n_entries").c_str());
        if (!f_entries) f_entries = FindFieldInHierarchy(dictKlass, PN("n_entriesU").c_str());
        if (!f_entries) return;
        void *entriesArr = nullptr;
        _monoFieldGetValue(dictObj, f_entries, &entriesArr);
        if (!entriesArr) return;
        uintptr_t arrAddr = (uintptr_t) entriesArr;
        int length = *(int *) (arrAddr + 0x18);
        uintptr_t data = arrAddr + 0x20;
        void *m_stop = FindMethodInHierarchy(pbKlass, PN("n_stopPlayback").c_str(), 0);
        if (!m_stop) return;
        for (int i = 0; i < length; i++) {
            int hash = *(int *) (data + (i * 24));
            if (hash < 0) continue;
            void *pbComp = *(void **) (data + (i * 24) + 16);
            if (!pbComp) continue;
            exc = nullptr;
            _monoRuntimeInvoke(m_stop, pbComp, nullptr, &exc);
        }
        g_musicStatus = (const char *) OBFUSCATE("Success");
    });
}

// ------------------------------------------------------------------
//  IMPORT PICTURE: masukkan gambar ke album foto lokal game.
// ------------------------------------------------------------------
void ImportPhotoToGameNative(std::string imagePath) {
    g_picturePn = FetchPayloadSync(WireForOp(OP_IMPORT_PICTURE)).payloadRaw;
    if (g_picturePn.empty()) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
    std::vector<uint8_t> imgBytes;
    if (!ReadWholeFile(imagePath, imgBytes)) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

    g_photoStatus = (const char *) OBFUSCATE("Process...");
    RunOnUnityThreadAsync([imgBytes]() {
        void *domain = _monoGetRootDomain();
        if (!domain) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        _monoThreadAttach(domain);
        auto PN = [](const char *k) -> std::string { return extractJsonString(g_picturePn, k); };

        void *imgCore = _monoImageLoaded(PN("n_imgEngine").c_str());
        if (!imgCore) imgCore = _monoImageLoaded(PN("n_ueNs").c_str());
        if (!imgCore) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *imgConv = _monoImageLoaded(PN("n_imgImageConv").c_str());
        if (!imgConv) imgConv = imgCore;

        void *tex2dKlass = _monoClassFromName(imgCore, PN("n_ueNs").c_str(), PN("n_texture2d").c_str());
        if (!tex2dKlass) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *texObj = _monoObjectNew(domain, tex2dKlass);
        if (!texObj) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *m_ctor = FindMethodInHierarchy(tex2dKlass, PN("n_ctor").c_str(), 2);
        if (!m_ctor) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        int cw = 2, ch = 2; void *ctorArgs[] = { &cw, &ch }; void *exc = nullptr;
        _monoRuntimeInvoke(m_ctor, texObj, ctorArgs, &exc);
        if (exc) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *mscorlib = _monoImageLoaded(PN("n_imgMscorlib").c_str());
        if (!mscorlib) mscorlib = _monoImageLoaded(PN("n_imgCoreLib").c_str());
        void *byteKlass = mscorlib ? _monoClassFromName(mscorlib, PN("n_sysNs").c_str(), PN("n_byte").c_str()) : nullptr;
        if (!byteKlass) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *monoArr = AndroidMonoArrayNew(domain, byteKlass, imgBytes.size());
        if (!monoArr) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        memcpy((uint8_t *) ((uintptr_t) monoArr + MonoArrayDataOffset()), imgBytes.data(), imgBytes.size());

        void *convKlass = _monoClassFromName(imgConv, PN("n_ueNs").c_str(), PN("n_imageConversion").c_str());
        if (!convKlass && imgConv != imgCore) convKlass = _monoClassFromName(imgCore, PN("n_ueNs").c_str(), PN("n_imageConversion").c_str());
        if (!convKlass) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *m_loadImage = FindMethodInHierarchy(convKlass, PN("n_loadImage").c_str(), 2);
        if (!m_loadImage) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        exc = nullptr;
        void *liArgs[] = { texObj, monoArr };
        void *liRet = _monoRuntimeInvoke(m_loadImage, nullptr, liArgs, &exc);
        if (exc) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        if (liRet && *(uint8_t *) _monoObjectUnbox(liRet) == 0) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        g_photoStatus = (const char *) OBFUSCATE("Process...");

        // LocalTextureCacheUtility.SaveLocalTextureResize3(Texture2D, bool, string) -> localId
        void *imgBase = _monoImageLoaded(PN("n_imgBaseService").c_str());
        if (!imgBase) imgBase = _monoImageLoaded(PN("n_imgBaseServiceDll").c_str());
        if (!imgBase) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *cacheKlass = _monoClassFromName(imgBase, PN("n_nsTexture").c_str(), PN("n_clsLocalTexCache").c_str());
        if (!cacheKlass) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *m_save = FindMethodInHierarchy(cacheKlass, PN("n_saveLocalTex").c_str(), 3);
        if (!m_save) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        bool saveRaw = true; void *saveArgs[] = { texObj, &saveRaw, nullptr };
        exc = nullptr;
        void *idRet = _monoRuntimeInvoke(m_save, nullptr, saveArgs, &exc);
        if (exc || !idRet) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        g_photoStatus = (const char *) OBFUSCATE("Process...");

        // PhotoSystem singleton (pola DataModule<PhotoSystem>) -> GenerateInfoByPhoto()
        void *imgGameSys = _monoImageLoaded(PN("n_imgLevel").c_str());
        if (!imgGameSys) imgGameSys = _monoImageLoaded(PN("n_imgLevelDll").c_str());
        if (!imgGameSys) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *psKlass = _monoClassFromName(imgGameSys, PN("n_nsPhoto").c_str(), PN("n_clsPhotoSystem").c_str());
        if (!psKlass) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *psInst = nullptr;
        const char *instNames[] = { "<Instance>k__BackingField", "instance", "_instance", "Instance", "s_instance" };
        void *psParent = _monoClassGetParent(psKlass);
        if (psParent) {
            void *vtP = _monoClassGetVTable(domain, psParent);
            for (int n = 0; n < 5 && !psInst; n++) {
                void *fld = _monoClassGetFieldFromName(psParent, instNames[n]);
                if (fld && vtP) _monoFieldStaticGetValue(vtP, fld, &psInst);
            }
            if (!psInst) {
                void *getInst = FindMethodInHierarchy(psParent, PN("n_getInstance").c_str(), 0);
                if (getInst) { void *e0 = nullptr; void *r0 = _monoRuntimeInvoke(getInst, nullptr, nullptr, &e0); if (r0 && !e0) psInst = r0; }
            }
        }
        if (!psInst) {
            void *vtS = _monoClassGetVTable(domain, psKlass);
            for (int n = 0; n < 5 && !psInst; n++) {
                void *fld = _monoClassGetFieldFromName(psKlass, instNames[n]);
                if (fld && vtS) _monoFieldStaticGetValue(vtS, fld, &psInst);
            }
        }
        if (!psInst) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *m_genInfo = FindMethodInHierarchy(psKlass, PN("n_genInfoByPhoto").c_str(), 0);
        if (m_genInfo) { exc = nullptr; _monoRuntimeInvoke(m_genInfo, psInst, nullptr, &exc); }

        const char *refreshNames[] = { "RefreshClientPhotos", "InitPhotos", "RefreshPhotos", "Refresh" };
        for (int r = 0; r < 4; r++) {
            void *m_ref = FindMethodInHierarchy(psKlass, refreshNames[r], 0);
            if (m_ref) { void *er = nullptr; _monoRuntimeInvoke(m_ref, psInst, nullptr, &er); }
        }

        g_photoStatus = (const char *) OBFUSCATE("Success");
    });
}

// ------------------------------------------------------------------
//  IMPORT AS AVATAR: upload gambar ke server lalu jadikan avatar aktif.
// ------------------------------------------------------------------
void ImportPhotoAsAvatarNative(std::string imagePath) {
    g_avatarPn = FetchPayloadSync(WireForOp(OP_IMPORT_AVATAR)).payloadRaw;
    if (g_avatarPn.empty()) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
    std::vector<uint8_t> imgBytes;
    if (!ReadWholeFile(imagePath, imgBytes)) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

    auto PN = [](const char *k) -> std::string { return extractJsonString(g_avatarPn, k); };
    g_photoStatus = (const char *) OBFUSCATE("Process...");

    std::string objectId;
    bool ok = false;

    // Langkah A: Texture2D + LoadImage + UploadTexture -> objectId (SYNC).
    RunOnUnityThreadSync([&]() {
        void *domain = _monoGetRootDomain();
        if (!domain) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        _monoThreadAttach(domain);

        void *imgCore = _monoImageLoaded(PN("n_imgEngine").c_str());
        if (!imgCore) imgCore = _monoImageLoaded(PN("n_ueNs").c_str());
        if (!imgCore) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *imgConv = _monoImageLoaded(PN("n_imgImageConv").c_str());
        if (!imgConv) imgConv = imgCore;

        void *tex2dKlass = _monoClassFromName(imgCore, PN("n_ueNs").c_str(), PN("n_texture2d").c_str());
        if (!tex2dKlass) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *texObj = _monoObjectNew(domain, tex2dKlass);
        if (!texObj) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *m_ctor = FindMethodInHierarchy(tex2dKlass, PN("n_ctor").c_str(), 2);
        if (!m_ctor) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        int cw = 2, ch = 2; void *ctorArgs[] = { &cw, &ch }; void *exc = nullptr;
        _monoRuntimeInvoke(m_ctor, texObj, ctorArgs, &exc);
        if (exc) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *mscorlib = _monoImageLoaded(PN("n_imgMscorlib").c_str());
        if (!mscorlib) mscorlib = _monoImageLoaded(PN("n_imgCoreLib").c_str());
        void *byteKlass = mscorlib ? _monoClassFromName(mscorlib, PN("n_sysNs").c_str(), PN("n_byte").c_str()) : nullptr;
        if (!byteKlass) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *monoArr = AndroidMonoArrayNew(domain, byteKlass, imgBytes.size());
        if (!monoArr) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        memcpy((uint8_t *) ((uintptr_t) monoArr + MonoArrayDataOffset()), imgBytes.data(), imgBytes.size());

        void *convKlass = _monoClassFromName(imgConv, PN("n_ueNs").c_str(), PN("n_imageConversion").c_str());
        if (!convKlass && imgConv != imgCore) convKlass = _monoClassFromName(imgCore, PN("n_ueNs").c_str(), PN("n_imageConversion").c_str());
        if (!convKlass) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *m_loadImage = FindMethodInHierarchy(convKlass, PN("n_loadImage").c_str(), 2);
        if (!m_loadImage) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        exc = nullptr;
        void *liArgs[] = { texObj, monoArr };
        void *liRet = _monoRuntimeInvoke(m_loadImage, nullptr, liArgs, &exc);
        if (exc) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        if (liRet && *(uint8_t *) _monoObjectUnbox(liRet) == 0) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        // UploadSystem singleton (ns XDTGUI.Module.Upload).
        void *usKlass = nullptr;
        void *imgU1 = _monoImageLoaded(PN("n_imgLevel").c_str());
        if (imgU1) usKlass = _monoClassFromName(imgU1, PN("n_nsUpload").c_str(), PN("n_clsUploadSystem").c_str());
        if (!usKlass) { void *imgU2 = _monoImageLoaded(PN("n_imgGameUI").c_str()); if (imgU2) usKlass = _monoClassFromName(imgU2, PN("n_nsUpload").c_str(), PN("n_clsUploadSystem").c_str()); }
        if (!usKlass) { void *imgU3 = _monoImageLoaded(PN("n_imgGameSys").c_str()); if (imgU3) usKlass = _monoClassFromName(imgU3, PN("n_nsUpload").c_str(), PN("n_clsUploadSystem").c_str()); }
        if (!usKlass) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *usInst = nullptr;
        const char *instNames[] = { "<Instance>k__BackingField", "instance", "_instance", "Instance", "s_instance" };
        void *usParent = _monoClassGetParent(usKlass);
        if (usParent) {
            void *vtP = _monoClassGetVTable(domain, usParent);
            for (int n = 0; n < 5 && !usInst; n++) {
                void *fld = _monoClassGetFieldFromName(usParent, instNames[n]);
                if (fld && vtP) _monoFieldStaticGetValue(vtP, fld, &usInst);
            }
            if (!usInst) {
                void *getInst = FindMethodInHierarchy(usParent, PN("n_getInstance").c_str(), 0);
                if (getInst) { void *e0 = nullptr; void *r0 = _monoRuntimeInvoke(getInst, nullptr, nullptr, &e0); if (r0 && !e0) usInst = r0; }
            }
        }
        if (!usInst) {
            void *vtS = _monoClassGetVTable(domain, usKlass);
            for (int n = 0; n < 5 && !usInst; n++) {
                void *fld = _monoClassGetFieldFromName(usKlass, instNames[n]);
                if (fld && vtS) _monoFieldStaticGetValue(vtS, fld, &usInst);
            }
        }
        if (!usInst) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        // string UploadTexture(Texture2D, UploadTextureType, Action<string>, Action<string>,
        //                      bool, string, Vector2Int, string) -> 8 param.
        void *m_upload = FindMethodInHierarchy(usKlass, PN("n_uploadTexture").c_str(), 8);
        if (!m_upload) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        int typeVal = 5;       // UploadTextureType.Other
        bool isAudit = false;
        struct { int x; int y; } resize = { 0, 0 };
        void *upArgs[8] = { texObj, &typeVal, nullptr, nullptr, &isAudit, nullptr, &resize, nullptr };
        exc = nullptr;
        void *idRet = _monoRuntimeInvoke(m_upload, usInst, upArgs, &exc);
        if (exc || !idRet) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        objectId = ReadMonoStringNative(idRet);
        if (objectId.empty()) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        ok = true;
    });

    if (!ok || objectId.empty()) return;

    g_photoStatus = (const char *) OBFUSCATE("Wait 6 sec...");
    // Upload OBS diproses UploadSystem.Update() di thread Unity tiap frame.
    // Sleep di thread ini TIDAK memblok thread Unity.
    sleep(6);

    // Langkah B: daftarkan objectId sebagai avatar (koleksi + set aktif).
    RunOnUnityThreadSync([&]() {
        void *domain = _monoGetRootDomain();
        _monoThreadAttach(domain);

        void *imgData = _monoImageLoaded(PN("n_imgData").c_str());
        if (!imgData) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *ppmKlass = _monoClassFromName(imgData, PN("n_nsProtoPlayer").c_str(), PN("n_clsPlayerProtoMgr").c_str());
        if (!ppmKlass) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        void *mStr = _monoStringNew(domain, objectId.c_str());

        void *m_add = FindMethodInHierarchy(ppmKlass, PN("n_editAltAvatar").c_str(), 2);
        if (m_add) {
            bool isDelete = false; void *aArgs[] = { &isDelete, mStr }; void *e1 = nullptr;
            _monoRuntimeInvoke(m_add, nullptr, aArgs, &e1);
        }

        void *m_change = FindMethodInHierarchy(ppmKlass, PN("n_changeAvatarUrl").c_str(), 1);
        if (!m_change) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }
        void *cArgs[] = { mStr }; void *e2 = nullptr;
        _monoRuntimeInvoke(m_change, nullptr, cArgs, &e2);
        if (e2) { g_photoStatus = (const char *) OBFUSCATE("Failed"); return; }

        g_photoStatus = (const char *) OBFUSCATE("Success");
    });
}
