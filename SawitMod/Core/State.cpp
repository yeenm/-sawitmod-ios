// ============================================================================
//  State.cpp — 全局状态定义（由 State.h 的 extern 逐条落地）
// ----------------------------------------------------------------------------
//  ⚠️ 这个文件由 scripts/gen_state.py 生成：新增全局变量请改 State.h 再跑脚本。
//     需要初值的表/手写定义在 State_manual.inc。
// ============================================================================

#include "Core/State.h"

#include "App/Defaults.h"
#include "Includes/obfuscate.h"

char license_input[64];
bool is_loading;
std::string auth_status_msg;
ImVec4 auth_status_color;
std::string licenseFilePath;
std::vector<Vector3> g_savePoints;
std::vector<InvokeRequest> invokeQueue;
std::mutex invokeMutex;
std::vector<CheatFeature> featureList;
std::unordered_map<int, std::string> g_opToWire;
std::unordered_map<std::string, int> g_wireToOp;
std::mutex g_opToWireMtx;
std::unordered_map<std::string, bool> g_activePatches;
std::vector<std::string> shopNames;
std::vector<std::string> shopIds;
std::vector<std::string> shopNamesZh;
std::vector<GachaPoolEntry> g_gachaPools;
std::mutex g_gachaPoolsMutex;
int g_selectedGachaPoolId;
std::atomic<bool> g_gachaPoolScanBusy;
std::vector<WeatherTypeEntry> g_weatherTypes;
std::mutex g_weatherTypesMutex;
int g_selectedWeatherId;
float g_weatherTransitionSeconds;
float g_snowSurfaceAmount;
std::atomic<bool> g_weatherScanBusy;
std::string g_weatherStatus;
std::vector<PlayerActionEntry> g_playerActions;
std::mutex g_playerActionsMutex;
int g_selectedPlayerActionId;
bool g_playerActionForceLoop;
std::atomic<bool> g_playerActionBusy;
std::string g_playerActionStatus;
std::vector<BroadcastMessage> g_broadcastMessages;
std::mutex g_broadcastMutex;
int64_t g_lastSeenBroadcastId;
bool g_fastHouseSetupEnabled;
int g_fastHouseSetupCount;
int g_currentToolDurability;
bool g_isRestoreToolActive;
int g_restoreToolThreshold;
int g_restoreToolCooldownSec;
std::atomic<bool> g_toolRestoring;
bool g_isAutoSnowActive;
int g_snowScore;
int g_snowScoreCount;
bool g_isAutoPumpkinActive;
bool g_autoPumpkinPlace;
bool g_autoPumpkinCollect;
int g_pumpkinDoneCount;
std::string g_pumpkinStatus;
std::string g_pumpkinPn;
int g_pumpkinScore;
int g_pumpkinScoreCount;
bool g_isAutoSandActive;
int g_sandBaseStaticId;
int g_sandProductId;
int g_sandSuccessCount;
int g_sandPerfectCount;
bool g_autoCleanActive;
int g_autoCleanMode;
float g_autoCleanRadius;
bool g_autoCleanBody;
int g_autoCleanIntervalMs;
bool g_autoClearTaskActive;
int g_autoClearTaskIntervalMs;
bool g_autoQuestTeleport;
float g_autoQuestTeleportDistance;
bool g_autoQuestDialogue;
bool g_autoQuestCutsceneSkip;
std::atomic<int> g_questFollowStatus;
bool g_autoFishingActive;
std::atomic<uint32_t> g_autoFishingGeneration;
int g_autoFishRodId;
float g_autoFishRadius;
bool g_autoFishTeleport;
float g_autoFishGap;
int g_autoFishCatchGraceTicks;
int g_autoFishBiteTimeoutTicks;
int g_autoFishEquipDelayTicks;
bool g_autoFishHoverBetweenCasts;
bool g_autoFishHoverGravitySaved;
float g_autoFishHoverOriginalGravity;
bool g_autoFishHoverAnchorValid;
float g_autoFishHoverAnchorY;
bool g_autoFishSkipCastAnim;
bool g_autoFishSkipCatchAnim;
int g_autoFishSessionCatches;
std::string g_autoFishStatus;
std::string g_autoFishTargetStatus;
std::vector<FoodItem> g_foodList;
std::mutex g_foodListMutex;
bool g_autoEatActive;
int g_eatStaminaThreshold;
int g_selectedFoodIdx;
bool g_radarEnabled;
bool g_radarShowPlayers;
bool g_radarShowResources;
bool g_radarShowBirds;
bool g_radarShowFish;
bool g_radarShowNpc;
bool g_radarShowInsects;
std::atomic<uint64_t> g_radarGeneration;
std::vector<RadarEntity> g_radarList;
std::mutex g_radarMutex;
std::unordered_map<std::string, const char *> g_TranslationMap;
int g_language;
int glWidth, glHeight;
bool g_autoCollectBubbleActive;
std::unordered_set<uint32_t> g_claimedBubbles;
bool g_vehicleSwapSuccess;
std::chrono::steady_clock::time_point g_vehicleSwapTime;
bool g_isAntiAfkActive;
bool g_isAntiCheatActive;
bool g_speedToggle;
bool g_speedActive;
int g_tempSliderInt;
std::string g_cachedPlayerPtrHex;
bool g_globalSpeedToggle;
int g_globalSpeedValue;
bool g_speedHackNeedsUpdate;
float g_targetSpeed;
bool g_jumpHackActive;
float g_jumpHeightSlider;
std::atomic<bool> g_isGardenBotRunning;
std::atomic<int> g_gardenOperation;
bool g_isPuzzleBotRunning;
std::string g_puzzlePn;
std::string g_cookPn;
std::string g_drawPn;
std::string g_musicPn;
std::string g_musicStatus;
int g_musicSelectedIdx;
bool g_musicLoop;
std::vector<std::string> g_instrumentFiles;
std::string g_instrumentFolderPath;
std::string g_picturePn;
std::string g_avatarPn;
std::string g_photoStatus;
int g_photoSelectedIdx;
std::vector<std::string> g_photoFiles;
std::string g_photoFolderPath;
bool g_avatarMode;
std::string g_botStatus;
uint32_t g_gardenPlayerNetId;
std::vector<PotTarget> g_scannedPots;
std::vector<uint32_t> g_scannedPlants;
int g_selectedSeedIdx;
int g_seedStaticId;
std::vector<SeedData> g_presetSeeds;
std::mutex g_seedListMutex;
std::atomic<bool> g_seedListLoading;
std::atomic<bool> g_seedListLoaded;
std::atomic<bool> g_pickAllPlantsBusy;
std::atomic<bool> g_flowerSeedListLoading;
std::atomic<bool> g_flowerSeedListLoaded;
std::mutex g_flowerSeedListMutex;
std::vector<SeedData> g_presetFlowerSeeds;
int g_selectedFlowerSeedIdx;
int g_flowerSeedStaticId;
int g_autoPlantFlowerCount;
int g_flowerHobbyMaxCapacity;
int g_flowerCurrentPlaced;
std::atomic<bool> g_isAutoPlantFlowerBusy;
std::string g_flowerPlantStatus;
std::vector<CookerTarget> g_availableCookers;
std::vector<RecipeTarget> g_availableRecipes;
bool g_isChefActive;
int g_selectedRecipeIdx;
uint32_t g_playerNetId;
bool g_autoCookingActive;
bool g_autoCookPayloadReady;
int g_autoCookFrameDelay;
std::vector<MyPet> g_myPets;
int g_selectedPetIndex;
int g_petFoodMode;
int g_petFeedQty;
std::unordered_map<int, std::string> g_cookerNamesCache;
int g_selectedCookerStaticId;
bool g_isFailedCookActive;
bool g_isHelperCookActive;
bool g_swimHackActive;
bool g_skateHackActive;
bool g_enableBirdFarm;
float g_birdCatchCooldown;
float g_birdScanRange;
std::unordered_set<uint32_t> g_processedBirds;
std::unordered_map<uint32_t, int> g_birdPhotoAttempts;
float g_birdMinPhotoDist;
float g_birdInvokeCooldown;
BirdTarget g_birdCurrentTarget;
std::atomic<bool> g_birdRequestPending;
std::atomic<uint64_t> g_birdResultSequence;
std::atomic<int> g_birdLastError;
uint64_t g_birdHandledResultSequence;
uint64_t g_birdRequestSentAt;
uint64_t g_birdNextActionAt;
int g_birdSuccessCount;
int g_birdRetryCount;
std::string g_birdFarmStatus;
int g_insectCatchLimit;
bool g_testAutoCatch;
bool isInstantFishing;
float g_fishThrowDist;
bool g_autoLureBallActive;
int g_lureBallCooldownSec;
std::atomic<uint64_t> g_lureBallCooldownUntilMs;
std::atomic<bool> g_lureBallBusy;
std::atomic<uint32_t> g_lureBallLoopGeneration;
std::mutex g_lureBallMutex;
std::string g_lureBallStatus;
int g_lureBallCount;
bool g_autoFishBaitActive;
int g_fishBaitIntervalMs;
std::atomic<bool> g_fishBaitBusy;
std::atomic<uint32_t> g_fishBaitLoopGeneration;
std::mutex g_fishBaitMutex;
std::string g_fishBaitStatus;
std::string g_fishBaitStatusZh;
bool g_fishBaitEmpty;
int g_fishBaitCount;
int g_fishBaitCurrentStackCount;
int g_fishBaitStackCount;
bool g_autoFarmActive;
bool g_farmSelections[NUM_RESOURCES];
bool g_farmTree;
bool g_farmStone;
bool g_farmBush;
bool g_farmMeteorite;
bool g_farmDynamicBush;
bool g_instantFarmingActive;
std::atomic<uint64_t> g_instantFarmGeneration;
std::atomic<uint64_t> g_instantFarmWorkerLease;
std::atomic<float> g_instantFarmPickupCooldown;
std::atomic<float> g_instantFarmNextTargetCooldown;
bool g_legacyInstantFarmingActive;
std::atomic<uint64_t> g_legacyInstantFarmGeneration;
std::atomic<uint64_t> g_legacyInstantFarmWorkerLease;
bool g_bypassOverlapEnabled;
bool g_noSurfaceLimit;
bool g_noRangeHeightLimit;
bool g_bypassOverlap;
bool g_freePlaceRotate;
MemoryPatch patch_OverlapBox1;
MemoryPatch patch_OverlapBox2;
MemoryPatch patch_OverlapSphere1;
MemoryPatch patch_OverlapSphere2;
std::mutex g_unityTaskMutex;
std::vector<std::string> g_availableImages;
std::string g_drawingFolderPath;
bool g_emoteUnlockEnabled;
std::mutex g_emoteUnlockMutex;
bool g_emoteUnlockProbeLoop;
int g_emoteUnlockProbeActionId;
bool g_friendInteractUnlockEnabled;
bool g_friendInteractLevelUnlockEnabled;
std::mutex g_friendInteractUnlockMutex;
std::atomic<bool> g_symbolProbeBusy;
std::mutex g_symbolProbeMutex;
std::string g_symbolProbeReport;
bool g_cameraModActive;
bool g_cameraFovActive;
float g_cameraFov;
bool g_cameraDistActive;
float g_cameraDist;
bool g_cameraOffsetActive;
float g_cameraOffsetX;
float g_cameraOffsetY;
float g_cameraOffsetZ;
bool g_cameraRollActive;
float g_cameraRoll;
bool g_cameraNoClipActive;
bool g_autoColorChatActive;
int g_chatColorMode;
char g_chatCustomHex[16];
std::string ac_dll;
std::string ac_ns_go;
std::string ac_cl_go;
std::string ac_m_find;
std::string ac_m_getcomp;
std::string ac_cl_btn;
std::string ac_ns_ui;
std::string ac_m_onclick;
std::string ac_ns_evt;
std::string ac_cl_evt;
std::string ac_m_invoke;
std::string ac_cl_sel;
std::string ac_m_int;
std::string ac_p_true;

// ============================================================================
//  State_manual.inc — State.cpp 的手写尾部（生成器原样拼接）
// ----------------------------------------------------------------------------
//  这里放"必须带初值"的表和 iOS 侧新增的菜单/渲染状态；其余全局变量由
//  scripts/gen_state.py 从 State.h 自动生成。
// ============================================================================

bool libLoaded = false;

// Instant Farm 的 17 种资源：顺序必须与 NUM_RESOURCES / g_farmSelections 一致。
const char *farmNames[NUM_RESOURCES] = {
    OBFUSCATE("Ore"), OBFUSCATE("Stone"), OBFUSCATE("Mandarin"), OBFUSCATE("Raspberry"),
    OBFUSCATE("Blueberry"), OBFUSCATE("Apple"),
    OBFUSCATE("Oyster"), OBFUSCATE("Tall Mustard"), OBFUSCATE("Black Truffle"),
    OBFUSCATE("Fiddlehead"), OBFUSCATE("Penny Bun"), OBFUSCATE("Shitake"),
    OBFUSCATE("Burdock"), OBFUSCATE("Mustard Greens"), OBFUSCATE("Sea Asparagus"),
    OBFUSCATE("Sea Grapes"), OBFUSCATE("Wakame")};

const char *farmNamesZh[NUM_RESOURCES] = {
    u8"矿石", u8"石头", u8"柑橘", u8"树莓", u8"蓝莓", u8"苹果",
    u8"平菇", u8"高荠", u8"黑松露", u8"蕨菜", u8"牛肝菌", u8"香菇",
    u8"口蘑", u8"荠菜", u8"海芦笋", u8"海葡萄", u8"海带"};

// 12 个家园传送点：g_homeCoords 由 ModFeatures.cpp 定义（那里是完整坐标表），
// 这里只保留 ownerNetId 数组。
uint32_t g_ownerNetIds[12] = {0};

const char *originalVehicleNames[2] = {OBFUSCATE("White Car"), OBFUSCATE("Green Bike")};
int originalVehicleIds[2] = {81002, 81104};
int selectedOriginalIdx = 0;

const char *targetVehicleNames[78] = {
    OBFUSCATE("Car 1"), OBFUSCATE("Car 2"), OBFUSCATE("Car 3"), OBFUSCATE("Car 4"),
    OBFUSCATE("Car 5"), OBFUSCATE("Car 6"), OBFUSCATE("Car 7"), OBFUSCATE("Car 8"),
    OBFUSCATE("Car 9"), OBFUSCATE("Car 10"), OBFUSCATE("Car 11"), OBFUSCATE("Car 12"),
    OBFUSCATE("Car 13"), OBFUSCATE("Car 14"), OBFUSCATE("Car 15"), OBFUSCATE("Car 16"),
    OBFUSCATE("Car 17"), OBFUSCATE("Car 18"), OBFUSCATE("Car 19"), OBFUSCATE("Car 20"),
    OBFUSCATE("Car 21"), OBFUSCATE("Car 22"), OBFUSCATE("Car 23"), OBFUSCATE("Car 24"),
    OBFUSCATE("Car 25"), OBFUSCATE("Car 26"), OBFUSCATE("Car 27"), OBFUSCATE("Car 28"),
    OBFUSCATE("Car 29"), OBFUSCATE("Car 30"), OBFUSCATE("Car 31"), OBFUSCATE("Car 32"),
    OBFUSCATE("Car 33"), OBFUSCATE("Car 34"), OBFUSCATE("Car 35"), OBFUSCATE("Car 36"),
    OBFUSCATE("Car 37"), OBFUSCATE("Car 38"), OBFUSCATE("Car 39"), OBFUSCATE("Car 40"),
    OBFUSCATE("Car 41"), OBFUSCATE("Car 42"), OBFUSCATE("Car 43"), OBFUSCATE("Car 44"),
    OBFUSCATE("Car 45"), OBFUSCATE("Car 46"), OBFUSCATE("Car 47"), OBFUSCATE("Car 48"),
    OBFUSCATE("Car 49"), OBFUSCATE("Car 50"), OBFUSCATE("Car 51"), OBFUSCATE("Car 52"),
    OBFUSCATE("Car 53"), OBFUSCATE("Car 54"), OBFUSCATE("Car 55"), OBFUSCATE("Car 56"),
    OBFUSCATE("Car 57"), OBFUSCATE("Car 58"), OBFUSCATE("Car 59"), OBFUSCATE("Car 60"),
    OBFUSCATE("Car 61"), OBFUSCATE("Car 62"), OBFUSCATE("Car 63"), OBFUSCATE("Car 64"),
    OBFUSCATE("Car 65"), OBFUSCATE("Car 66"),
    OBFUSCATE("Car 67"), OBFUSCATE("My Little Pony"), OBFUSCATE("Little Dog"),
    OBFUSCATE("Sultan Bike"), OBFUSCATE("DragonBoat"), OBFUSCATE("Car 68"),
    OBFUSCATE("Car 69"), OBFUSCATE("Car 70"), OBFUSCATE("Car 71"),
    OBFUSCATE("Car 72"), OBFUSCATE("Car 73"), OBFUSCATE("Car 74"),
};

int targetVehicleIds[78] = {
    81063, 81064, 81065, 81070, 81071, 81072, 81101, 81102, 81103, 81104,
    81105, 81106, 81107, 81108, 81110, 81111, 81112, 81113, 81114, 81115,
    81116, 81201, 81202, 81203, 81204, 81205, 81207, 81208, 81209, 81210,
    81211, 81212, 81213, 81214, 81215, 81216, 81217, 81218, 81219, 81220,
    81221, 81222, 81223, 81224, 81001, 81002, 81003, 81004, 81005, 81006,
    81007, 81008, 81051, 81052, 81053, 81054, 81055, 81056, 81057, 81058,
    81059, 81060, 81061, 81062, 81067, 81068, 81069, 81066, 81109, 81117,
    81206, 81009, 81073, 81074, 81075, 81076, 81118, 81225,
};
int selectedTargetIdx = 0;

// ============================================================================
//  iOS 菜单/渲染状态（Android 版在 Main.cpp 里是 static，这里需要跨文件可见）
// ============================================================================
MenuBounds g_MenuRect{};
MenuBounds g_MinRect{};
std::mutex g_MenuBoundsMutex;
bool g_MenuMinimized = false;
std::atomic<float> g_RequestedUiScale{-1.0f};
std::atomic<float> g_RealScreenWidth{0.0f};
std::atomic<float> g_RealScreenHeight{0.0f};
std::atomic<bool> g_AppInForeground{true};
std::atomic<bool> g_GameWorldReady{false};
std::atomic<bool> g_unityFramePumpReady{false};

void *g_cachedConfigManager = nullptr;
void *g_cachedLevelScriptableConfig = nullptr;
void *g_cachedPlayerMotionConfig = nullptr;
void *g_StolenInteractSystem = nullptr;

float LoadSavedUiScale() {
    const int pct = SMDefaults::GetInt("ui_scale_pct", 100);
    float scale = (float)pct / 100.0f;
    if (scale < 0.25f || scale > 2.0f) scale = 1.0f;
    return scale;
}

void SaveUiScale(float value) {
    if (value < 0.25f) value = 0.25f;
    if (value > 2.0f) value = 2.0f;
    SMDefaults::SetInt("ui_scale_pct", (int)(value * 100.0f + 0.5f));
}

// g_unityTasks 的声明带模板参数，生成器（gen_state.py）会跳过，手工定义。
std::vector<std::function<void()>> g_unityTasks;
