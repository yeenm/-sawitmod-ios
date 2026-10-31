#pragma once
// ============================================================================
//  State.h — 全局状态与结构（Android 版 Global.h 的 iOS 移植）
// ----------------------------------------------------------------------------
//  与 Android 版保持同名同类型的 extern，这样从 Main.cpp / ModFeatures.cpp
//  搬过来的界面和功能代码不用改符号。
//
//  平台差异只在这几处：
//    * 不再有 JavaVM/jobject（jvm/globalContext 删除，改 App/Platform.h）
//    * 偏移表换成 Offsets-iOS.h（iOS 的 RVA 必须重新标定）
//    * 菜单/缩放状态（Android 版定义在 Main.cpp）挪到这里声明
// ============================================================================

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Vector3.h"
#include "KittyMemory/MemoryPatch.h"
#include "imgui/imgui.h"   // ImVec4（auth_status_color / 主题色）

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <functional>
#include <deque>
#include <cstdint>
#include <atomic>
#include "Core/Offsets-iOS.h"

#include "Vector3.h"
#include "KittyMemory/MemoryPatch.h"
#include "imgui/imgui.h" // dibutuhkan untuk ImVec4 (auth_status_color)

// =====================================================================
// STRUCT & ENUM (dipindah dari Main.cpp, dibutuhkan lintas file)
// =====================================================================

// --- Rotating payload op-code (STABIL lintas rotasi; canonical id disembunyikan) ---
enum FeatureOp {
    OP_GENERIC = 0,
    OP_GET_SELF_PLAYER = 1,
    OP_SET_MOVE_SPEED = 2,
    OP_GET_MY_HOME_ID = 3,
    OP_TELE_TO_HOME = 4,
    OP_GET_FIELD_BY_POS = 5,
    OP_GET_OWNER_NETID = 6,
    OP_GET_FIELD_ID_BY_PLAYER = 7,
    OP_TELE_TO_POS = 8,
    OP_VEHICLE_SWAP = 9,
    OP_AUTO_COLLECT_BUBBLE = 10,
    OP_INSTANT_FARM_TRIGGER = 11,
    OP_NPC_COSPLAY = 12,
    OP_SWITCH_MAP = 13,
    OP_OPEN_SHOP = 14,
    OP_EDIT_NAME = 15,
    OP_UNLOCK_PLOT_AREA = 16,
    OP_SET_TIME = 17,
    OP_MAIN_INTERACT = 18,
    OP_SETUP_FISHING = 19,
    OP_AUTO_DRAW = 20,
    OP_AUTO_COOK = 21,
    OP_COOK_HELPER = 22,
    OP_JIGSAW_PUZZLE = 23,
    OP_MUSIC_PLAYER = 24,
    OP_IMPORT_PICTURE = 25,
    OP_IMPORT_AVATAR = 26,
    OP_PICK_ALL_PLANT = 27,
    OP_GACHA_SELECTOR = 28,
    OP_WEATHER_CHANGER = 29,
    OP_BIRD_WATCHING = 30,
    OP_AUTO_LURE_BALL = 31,
    OP_CAMERA_CONTROLS = 32,
    OP_OBJECT_SCALE = 33,
    OP_AUTO_SAND = 34,
    OP_AUTO_SNOW = 35,
    OP_AUTO_FISHING = 36,
    OP_AUTO_PUMPKIN = 37,
    OP_AUTO_FISH_BAIT = 38
};

struct InvokeRequest {
    std::string featureId;
    std::string dllName;
    std::string nameSpace;
    std::string className;
    std::string methodDesc;
    std::string paramInput;
    bool isStatic;
    std::string instancePtrHexStr;
    int op = 0;   // OP_* penanda identitas fitur (0 = generic)
    std::string payloadRaw;   // Metadata biner SVMN untuk handler fitur khusus.
    uint64_t generasiSesi = 0;

    // Request async mempertahankan envelope SVM2 yang operand-nya masih
    // tertransformasi hingga tiba di Unity thread. Descriptor eksekusi baru
    // dibentuk sesaat sebelum dispatch dan kemudian buffer ini dibersihkan.
    std::string vmStream;
    std::string vmNonce;
    std::string vmHwid;
    std::string vmWireId;
    std::string vmParamOverride;
    std::string vmInstanceOverride;
    // 绑定串 SHA256(nonce|hwid|wire|version|channel|op) 里的 version/channel。
    // 云端信封填 SAWIT_VERSION/SAWIT_CHANNEL；内嵌 dump 信封填抓包时那一组。
    // 空 => Main.cpp 回落到 SAWIT_VERSION/SAWIT_CHANNEL。
    std::string vmBindVersion;
    std::string vmBindChannel;
};

struct CheatFeature {
    std::string id;      // menyimpan WIRE id (opaque, rotasi). Canonical TIDAK ada di client.
    std::string name;
    std::string nameZh;
    int catalog;
    int op = 0;
    bool isPatch = false;
};

struct RadarEntity {
    uint32_t netId;
    std::string name;
    int type; // 0=Player, 1=Resource, 2=Bird, 3=Fish, 4=NPC, 5=Insect
    Vector3 worldPos;
    Vector3 screenPos;
    bool isOnScreen;
};

#pragma pack(push, 1)
struct CropPlantPoint {
    Vector3 pos;
    int angle;
    uint64_t levelObjectNetId;
};
struct NativeBirdPhotoDetailInfo {
    int ActionStar; // Offset 0
    uint8_t IsPerfectStar;
    // Offset 4
    uint8_t IsCoverStar; // Offset 5
    uint8_t _pad1[2];
    // Offset 6, 7
    int BirdActionType; // Offset 8
    uint8_t IsUsingZoom;
    // Offset 12
    uint8_t _pad2[3]; // Offset 13, 14, 15
    uint32_t BirdStandNetId;
    // Offset 16
};
#pragma pack(pop)

struct PotTarget {
    uint32_t netId;
    uint32_t childId;
    Vector3 position;
    uint64_t serverUlong;
};
struct SeedData {
    std::string name;
    int id;
};

struct CookerTarget {
    uint32_t netId;
    uint64_t serverId;
    uint64_t burnerId;
    void *entityPtr;
    int pureId;
    int staticId;
    int cookType;
};

struct RecipeTarget {
    int staticId;
    std::string name;
};

struct MyPet {
    uint32_t netId;
    bool isDog;
    std::string name;
};

struct BirdTarget {
    int staticId = 0;
    uint32_t birdNetId = 0;
    uint32_t standNetId = 0;
    uint64_t instanceId = 0;
    int pose = 0;
};

enum CookSt {
    ST_Idle = 0, ST_Preparing = 1, ST_Cooking = 2, ST_Danger = 3,
    ST_Relief = 4, ST_Succeed = 5, ST_Failed = 6
};

struct InstantFarmWaypoint {
    Vector3 position{};
    int resourceIndex = -1;
    bool useAreaScanner = false;
};

struct InstantFarmTarget {
    uint32_t entityId = 0;
    uint32_t dictionaryKey = 0;
    Vector3 position{};
    int resType = 0;
    int resourceId = 0;
    int resourceIndex = -1;
};

struct GachaPoolEntry {
    int id = 0;
    int poolType = 0;
    int classId = 0;
    int sortId = 0;
    std::string name;
};

struct WeatherTypeEntry {
    int id = 0;
    int weatherClass = 0;
    bool isFunctionWeather = false;
    bool buildModePreview = false;
    std::string nameDay;
    std::string nameNight;
};

struct PlayerActionEntry {
    int id = 0;
    bool isLoop = false;
    int rarity = 0;
    std::string name;
    std::string resource;
};

struct BroadcastMessage {
    int64_t id = 0;
    std::string title;
    std::string contentEn;
    std::string contentZh;
    std::string createdAt;
    std::string updatedAt;
};

enum CookingStatus {
    Idle = 0,
    Preparing = 1,
    Cooking = 2,
    Danger = 3,
    Relief = 4,
    Succeed = 5,
    Failed = 6
};

// =====================================================================
// VARIABEL GLOBAL (extern) — definisi asli TETAP di Main.cpp
// =====================================================================

// --- JNI Core ---

// --- Auth & License ---
extern char license_input[64];
extern bool is_loading;
extern std::string auth_status_msg;
extern ImVec4 auth_status_color;
extern std::string licenseFilePath;

// --- Manual Teleport save points ---
extern std::vector<Vector3> g_savePoints;

// --- Data Fetch API / Invoke Queue ---
extern std::vector<InvokeRequest> invokeQueue;
extern std::mutex invokeMutex;
extern std::vector<CheatFeature> featureList;
// --- Rotating payload op<->wire map (refresh tiap login / UNKNOWN_FEATURE) ---
extern std::unordered_map<int, std::string> g_opToWire;   // op -> wire (khusus special op != 0)
extern std::unordered_map<std::string, int> g_wireToOp;   // wire -> op
extern std::mutex g_opToWireMtx;
extern std::unordered_map<std::string, bool> g_activePatches;
extern std::vector<std::string> shopNames;
extern std::vector<std::string> shopIds;
// catalog 里还带一份中文店名（shopNamesZh）；菜单固定中文，优先用它，
// shopNames + TranslateShopName 只作为兜底。
extern std::vector<std::string> shopNamesZh;
extern std::vector<GachaPoolEntry> g_gachaPools;
extern std::mutex g_gachaPoolsMutex;
extern int g_selectedGachaPoolId;
extern std::atomic<bool> g_gachaPoolScanBusy;
extern std::vector<WeatherTypeEntry> g_weatherTypes;
extern std::mutex g_weatherTypesMutex;
extern int g_selectedWeatherId;
extern float g_weatherTransitionSeconds;
extern float g_snowSurfaceAmount;
extern std::atomic<bool> g_weatherScanBusy;
extern std::string g_weatherStatus;
extern std::vector<PlayerActionEntry> g_playerActions;
extern std::mutex g_playerActionsMutex;
extern int g_selectedPlayerActionId;
extern bool g_playerActionForceLoop;
extern std::atomic<bool> g_playerActionBusy;
extern std::string g_playerActionStatus;
extern std::vector<BroadcastMessage> g_broadcastMessages;
extern std::mutex g_broadcastMutex;
extern int64_t g_lastSeenBroadcastId;

// --- Fast House ---
extern bool g_fastHouseSetupEnabled;
extern int g_fastHouseSetupCount;

// --- Auto Restore Tool ---
extern int g_currentToolDurability;
extern bool g_isRestoreToolActive;
extern int g_restoreToolThreshold;
extern int g_restoreToolCooldownSec;
extern std::atomic<bool> g_toolRestoring; // true selama proses repair belum benar-benar selesai

// --- Auto Snow Sculpture ---
extern bool g_isAutoSnowActive;
extern int g_snowScore;
extern int g_snowScoreCount;

// --- Auto Pumpkin Carving ---
extern bool g_isAutoPumpkinActive;
extern bool g_autoPumpkinPlace;
extern bool g_autoPumpkinCollect;
extern int  g_pumpkinDoneCount;
extern std::string g_pumpkinStatus;
extern std::string g_pumpkinPn;
extern int  g_pumpkinScore;
extern int  g_pumpkinScoreCount;
extern bool g_isAutoSandActive;
extern int g_sandBaseStaticId;
extern int g_sandProductId;
extern bool g_sandPreferRare;      // 优先稀有/未收集模型（PC sandPreferRareUncollected）
extern int g_sandLastPickProduct;  // 上次选中的 productId（UI 显示）
extern int g_sandLastPickModel;    // 上次选中的模型 staticId（UI 显示）
extern int g_sandSuccessCount;
extern int g_sandPerfectCount;

// --- Sea Clean + Auto Bath ---
extern bool g_autoCleanActive;
extern int g_autoCleanMode;
extern float g_autoCleanRadius;
extern bool g_autoCleanBody;
extern int g_autoCleanIntervalMs;

// --- Auto Clear Task (Quest) ---
enum class QuestFollowStatus : int {
    Off = 0,
    WorkerStopped,
    WaitingForTask,
    WaitingForAcceptedState,
    PlayerPositionFailed,
    NoMatchingHudWaypoint,
    WaitingForStableWaypoint,
    TeleportFailed,
    Teleported
};
extern bool g_autoClearTaskActive;
extern int g_autoClearTaskIntervalMs;
extern bool g_autoQuestTeleport;
extern float g_autoQuestTeleportDistance;
extern bool g_autoQuestDialogue;
extern bool g_autoQuestCutsceneSkip;
extern std::atomic<int> g_questFollowStatus;

// --- Auto Fishing ---
extern bool g_autoFishingActive;
extern std::atomic<uint32_t> g_autoFishingGeneration;
extern int g_autoFishRodId;
extern float g_autoFishRadius;
extern bool g_autoFishTeleport;
extern float g_autoFishGap;
extern int g_autoFishCatchGraceTicks;
extern int g_autoFishBiteTimeoutTicks;
extern int g_autoFishEquipDelayTicks;
extern bool g_autoFishHoverBetweenCasts;
extern bool g_autoFishHoverGravitySaved;
extern float g_autoFishHoverOriginalGravity;
extern bool g_autoFishHoverAnchorValid;
extern float g_autoFishHoverAnchorY;
extern bool g_autoFishSkipCastAnim;
extern bool g_autoFishSkipCatchAnim;
extern int g_autoFishSessionCatches;
extern std::string g_autoFishStatus;
extern std::string g_autoFishTargetStatus;

// --- Auto Eat ---
struct FoodItem {
    uint32_t netId = 0;
    int staticId = 0;
    int count = 0;
    std::string name;
};
extern std::vector<FoodItem> g_foodList;
extern std::mutex g_foodListMutex;
extern bool g_autoEatActive;
extern int g_eatStaminaThreshold;
extern int g_selectedFoodIdx;

// --- Radar ---
extern bool g_radarEnabled;
extern bool g_radarShowPlayers;
extern bool g_radarShowResources;
extern bool g_radarShowBirds;
extern bool g_radarShowFish;
extern bool g_radarShowNpc;
extern bool g_radarShowInsects;
extern std::atomic<uint64_t> g_radarGeneration;
extern std::vector<RadarEntity> g_radarList;
extern std::mutex g_radarMutex;

// --- Translation Map (UI) ---
extern std::unordered_map<std::string, const char *> g_TranslationMap;

// --- Language / i18n (0 = 中文, 1 = English) ---
enum AppLanguage { LANG_CN = 0, LANG_EN = 1 };
extern int g_language;
inline const char *Tr(const char *en, const char *cn) { return g_language == LANG_EN ? en : cn; }

// --- UI & Rendering ---
extern int glWidth, glHeight;

// --- Bubble ---
extern bool g_autoCollectBubbleActive;
extern std::unordered_set<uint32_t> g_claimedBubbles;

// --- Vehicle Swap ---
extern bool g_vehicleSwapSuccess;
extern std::chrono::steady_clock::time_point g_vehicleSwapTime;

// --- Anti AFK / Anti Cheat ---
extern bool g_isAntiAfkActive;
extern bool g_isAntiCheatActive;

// --- Speed Hack ---
extern bool g_speedToggle;
extern bool g_speedActive;
extern int g_tempSliderInt;
extern std::string g_cachedPlayerPtrHex;
extern bool g_globalSpeedToggle;
extern int g_globalSpeedValue;
extern bool g_speedHackNeedsUpdate;
extern float g_targetSpeed;

// --- Jump Hack ---
extern void *g_cachedLevelScriptableConfig;
extern void *g_cachedPlayerMotionConfig;
extern bool g_jumpHackActive;
extern float g_jumpHeightSlider;

// --- Auto Garden ---
extern std::atomic<bool> g_isGardenBotRunning;
extern std::atomic<int> g_gardenOperation;
extern bool g_isPuzzleBotRunning; // Auto Puzzle (Jigsaw)
extern std::string g_puzzlePn;  // payload identifier JSON utk Auto Puzzle (dari server)
extern std::string g_cookPn;  // payload identifier JSON utk fitur Cook (dari server)
extern std::string g_drawPn;  // payload identifier JSON utk Auto Draw (dari server)

// --- Music Player (port dari PC) ---
extern std::string g_musicPn;      // payload identifier JSON utk Music Player (dari server)
extern std::string g_musicStatus;
extern int g_musicSelectedIdx;
extern bool g_musicLoop;
extern std::vector<std::string> g_instrumentFiles;
extern std::string g_instrumentFolderPath;

// --- Import Picture / Import As Avatar (port dari PC) ---
extern std::string g_picturePn;    // payload identifier JSON utk Import Picture
extern std::string g_avatarPn;     // payload identifier JSON utk Import As Avatar
extern std::string g_photoStatus;
extern int g_photoSelectedIdx;
extern std::vector<std::string> g_photoFiles;
extern std::string g_photoFolderPath;

// --- Avatar login mode (tab AVATAR vs VIP) ---
extern bool g_avatarMode;          // true jika user login lewat tab AVATAR

extern std::string g_botStatus;
extern uint32_t g_gardenPlayerNetId;
extern std::vector<PotTarget> g_scannedPots;
extern std::vector<uint32_t> g_scannedPlants;
extern int g_selectedSeedIdx;
extern int g_seedStaticId;
extern std::vector<SeedData> g_presetSeeds;
extern std::mutex g_seedListMutex;
extern std::atomic<bool> g_seedListLoading;
extern std::atomic<bool> g_seedListLoaded;
extern std::atomic<bool> g_pickAllPlantsBusy;

// --- Auto Plant Flower ---
extern std::atomic<bool> g_flowerSeedListLoading;
extern std::atomic<bool> g_flowerSeedListLoaded;
extern std::mutex g_flowerSeedListMutex;
extern std::vector<SeedData> g_presetFlowerSeeds;
extern int g_selectedFlowerSeedIdx;
extern int g_flowerSeedStaticId;
extern int g_autoPlantFlowerCount;
extern int g_flowerHobbyMaxCapacity;
extern int g_flowerCurrentPlaced;
extern std::atomic<bool> g_isAutoPlantFlowerBusy;
extern std::string g_flowerPlantStatus;

// --- Auto Cook ---
extern std::vector<CookerTarget> g_availableCookers;
extern std::vector<RecipeTarget> g_availableRecipes;
extern bool g_isChefActive;
extern int g_selectedRecipeIdx;
extern uint32_t g_playerNetId;
extern bool g_autoCookingActive;
extern bool g_autoCookPayloadReady;
extern int g_autoCookFrameDelay;

// --- Pet ---
extern std::vector<MyPet> g_myPets;
extern int g_selectedPetIndex;
extern int g_petFoodMode;
extern int g_petFeedQty;
extern std::unordered_map<int, std::string> g_cookerNamesCache;
extern int g_selectedCookerStaticId;
extern bool g_isFailedCookActive;
extern bool g_isHelperCookActive;

// --- Swim / Skate ---
extern bool g_swimHackActive;
extern bool g_skateHackActive;

// --- Bird Farm ---
extern bool g_enableBirdFarm;
extern float g_birdCatchCooldown;
extern float g_birdScanRange;
extern std::unordered_set<uint32_t> g_processedBirds;
extern std::unordered_map<uint32_t, int> g_birdPhotoAttempts;
extern float g_birdMinPhotoDist;
extern float g_birdInvokeCooldown;
extern BirdTarget g_birdCurrentTarget;
extern std::atomic<bool> g_birdRequestPending;
extern std::atomic<uint64_t> g_birdResultSequence;
extern std::atomic<int> g_birdLastError;
extern uint64_t g_birdHandledResultSequence;
extern uint64_t g_birdRequestSentAt;
extern uint64_t g_birdNextActionAt;
extern int g_birdSuccessCount;
extern int g_birdRetryCount;
// 拍鸟：拍到上限的鸟自动用 BirdEscape/RemoveBird 吓走（对齐 PC 版 autoScareMaxPhotoEnabled）
extern bool g_birdAutoScareMaxed;
extern int g_birdScaredCount;   // 本会话吓走成功次数
extern std::string g_birdFarmStatus;
extern int g_insectCatchLimit;
extern bool g_testAutoCatch;
extern void *g_StolenInteractSystem;

// --- Fishing ---
extern bool isInstantFishing;
extern float g_fishThrowDist;

// --- Auto Lure Ball (payload VM2 op 31) ---
extern bool g_autoLureBallActive;
extern int g_lureBallCooldownSec;
extern std::atomic<uint64_t> g_lureBallCooldownUntilMs;
extern std::atomic<bool> g_lureBallBusy;
extern std::atomic<uint32_t> g_lureBallLoopGeneration;
extern std::mutex g_lureBallMutex;
extern std::string g_lureBallStatus;
extern int g_lureBallCount;

// --- Auto Throw Fish Bait (payload VM2 op 38) ---
extern bool g_autoFishBaitActive;
extern int g_fishBaitIntervalMs;
extern std::atomic<bool> g_fishBaitBusy;
extern std::atomic<uint32_t> g_fishBaitLoopGeneration;
extern std::mutex g_fishBaitMutex;
extern std::string g_fishBaitStatus;
extern std::string g_fishBaitStatusZh;
extern bool g_fishBaitEmpty;
extern int g_fishBaitCount;
extern int g_fishBaitCurrentStackCount;
extern int g_fishBaitStackCount;

// --- Instant Farm ---
inline constexpr int NUM_RESOURCES = 18;
// 优质木材在 farmNames/allResourceVectors 里的下标（第 18 项，iOS 新增）
inline constexpr int kPremiumWoodIndex = 17;
extern bool g_autoFarmActive;
extern bool g_farmSelections[NUM_RESOURCES];
extern const char *farmNames[NUM_RESOURCES];
extern const char *farmNamesZh[NUM_RESOURCES];
extern bool g_farmTree;
extern bool g_farmStone;
extern bool g_farmBush;
extern bool g_farmMeteorite;
extern bool g_farmDynamicBush;
extern bool g_instantFarmingActive;
extern std::atomic<uint64_t> g_instantFarmGeneration;
extern std::atomic<uint64_t> g_instantFarmWorkerLease;
extern std::atomic<float> g_instantFarmPickupCooldown;
extern std::atomic<float> g_instantFarmNextTargetCooldown;
// 优质木材：一轮固定坐标全部走完后自动停止（避免立刻把 40 个点再传送一遍）。
// 默认 true；菜单「游戏 -> 种植 -> 瞬间资源采集」可关。
extern bool g_premiumWoodOnePass;
extern bool g_legacyInstantFarmingActive;
extern std::atomic<uint64_t> g_legacyInstantFarmGeneration;
extern std::atomic<uint64_t> g_legacyInstantFarmWorkerLease;
extern bool g_bypassOverlapEnabled;
extern bool g_noSurfaceLimit;
extern bool g_noRangeHeightLimit;
extern bool g_bypassOverlap;
extern bool g_freePlaceRotate;
extern MemoryPatch patch_OverlapBox1;
extern MemoryPatch patch_OverlapBox2;
extern MemoryPatch patch_OverlapSphere1;
extern MemoryPatch patch_OverlapSphere2;

// --- Unity Main-Thread Task Queue ---
extern std::mutex g_unityTaskMutex;
extern std::vector<std::function<void()>> g_unityTasks;

// --- Config Manager Cache ---
extern void *g_cachedConfigManager;

// --- Home / Vehicle Swap Data ---
extern uint32_t g_ownerNetIds[12];
extern const char *g_homeCoords[12];
extern const char *originalVehicleNames[2];
extern int originalVehicleIds[2];
extern int selectedOriginalIdx;
extern const char *targetVehicleNames[78];
extern int targetVehicleIds[78];
extern int selectedTargetIdx;

extern std::vector<std::string> g_availableImages;
extern std::string g_drawingFolderPath;
extern std::vector<std::string> workPaths;

// --- Unlock All Emotes（PC 版 EmoteUnlockFeature 移植）---
extern bool g_emoteUnlockEnabled;          // 菜单开关（UI 线程写，Unity 线程读）
extern std::mutex g_emoteUnlockMutex;      // 保护 g_emoteUnlockStatus 文本
extern bool g_emoteUnlockProbeLoop;        // "试演"按钮用的循环标记
extern int g_emoteUnlockProbeActionId;     // "试演"按钮用的动作 id

// --- Unlock All Two-Person Interactions（PC 版 FriendInteractUnlockFeature 移植）---
extern bool g_friendInteractUnlockEnabled;
// 额外的"等级双人动作"开关：把 TableFriendInteract.friendshipLv 拍成 0，
// 让等级桶里的行也变成可点。⚠️ 越级的那几个服务器仍会回 601。
extern bool g_friendInteractLevelUnlockEnabled;
extern std::mutex g_friendInteractUnlockMutex;

// --- Mono 符号自检（诊断导出，见 MonoSymbolProbe.cpp）---
extern std::atomic<bool> g_symbolProbeBusy;
extern std::mutex g_symbolProbeMutex;
extern std::string g_symbolProbeReport;

// --- Camera Controls ---
extern bool g_cameraModActive;
extern bool g_cameraFovActive;
extern float g_cameraFov;
extern bool g_cameraDistActive;
extern float g_cameraDist;
extern bool g_cameraOffsetActive;
extern float g_cameraOffsetX;
extern float g_cameraOffsetY;
extern float g_cameraOffsetZ;
extern bool g_cameraRollActive;
extern float g_cameraRoll;
extern bool g_cameraNoClipActive;

// --- Auto Color Chat ---
extern bool g_autoColorChatActive;
extern int g_chatColorMode;
extern char g_chatCustomHex[16];

// =====================================================================
// FUNGSI HELPER (implementasi pindah ke ModUtils.cpp / NetAuth.cpp)
// =====================================================================
extern std::string extractJsonString(const std::string& json, const std::string& key);
extern std::string DecryptPayload(std::string encryptedData);
extern void SaveLicenseLocally(const std::string& license);
extern void ResetBubbleHistory();
extern void ResetBirdHistory();

// ============================================================================
//  iOS 菜单/渲染状态（Android 版写在 Main.cpp，这里集中声明）
// ============================================================================
struct MenuBounds {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool active = false;
};

extern MenuBounds g_MenuRect;      // 完整菜单窗口（ImGui 像素坐标）
extern MenuBounds g_MinRect;       // 最小化条（点它恢复菜单）
extern std::mutex g_MenuBoundsMutex;
extern bool g_MenuMinimized;       // 只显示小方条
extern std::atomic<float> g_RequestedUiScale;  // 设置页改缩放时写入
extern std::atomic<float> g_RealScreenWidth;   // 屏幕像素宽（iOS: renderbuffer 宽）
extern std::atomic<float> g_RealScreenHeight;

float LoadSavedUiScale();
void SaveUiScale(float value);

// 平台把"当前是否在游戏前台"告诉 UI（iOS 上窗口始终存在，这里按 didEnterBackground
// 事件维护，避免后台还跑 display link）。
extern std::atomic<bool> g_AppInForeground;

// 世界就绪标志：iOS 版由 il2cpp 探测结果置位（见 IL2CPP/MonoCompat.cpp）。
extern std::atomic<bool> g_GameWorldReady;

// FramePump 是否可用（GameApp.Update 挂钩成功后才启动）。
extern std::atomic<bool> g_unityFramePumpReady;

// --- Auto Cook 面板反射用的程序集/类/方法名（Android 版定义在 Main.cpp 第 412 行）---
extern std::string ac_dll;
extern std::string ac_ns_go;
extern std::string ac_cl_go;
extern std::string ac_m_find;
extern std::string ac_m_getcomp;
extern std::string ac_cl_btn;
extern std::string ac_ns_ui;
extern std::string ac_m_onclick;
extern std::string ac_ns_evt;
extern std::string ac_cl_evt;
extern std::string ac_m_invoke;
extern std::string ac_cl_sel;
extern std::string ac_m_int;
extern std::string ac_p_true;
extern int g_selectedImageIdx;     // Auto Draw：当前选中的图片下标

extern std::string g_drawStatus;   // 自动绘画状态文本（定义在 FeatureDispatch.cpp）

// 上次闪退前执行到的步骤（启动时从 NSUserDefaults 读出，显示在卡密页）
extern std::string g_prevBreadcrumb;

// 上次崩溃日志的头部（启动时从 Documents/mod_menu_crash_*.txt 读出）
extern std::string g_prevCrashHead;
