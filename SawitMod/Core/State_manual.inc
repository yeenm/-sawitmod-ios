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

// 界面缩放：默认 150%（原默认 100% 的 1.5 倍）。用户手动应用过的值存在
// NSUserDefaults 的 ui_scale_pct 里，优先于这里的默认值。
inline constexpr int kDefaultUiScalePct = 150;

float LoadSavedUiScale() {
    const int pct = SMDefaults::GetInt("ui_scale_pct", kDefaultUiScalePct);
    float scale = (float)pct / 100.0f;
    if (scale < 0.25f || scale > 2.0f) scale = (float)kDefaultUiScalePct / 100.0f;
    return scale;
}

void SaveUiScale(float value) {
    if (value < 0.25f) value = 0.25f;
    if (value > 2.0f) value = 2.0f;
    SMDefaults::SetInt("ui_scale_pct", (int)(value * 100.0f + 0.5f));
}

// g_unityTasks 的声明带模板参数，生成器（gen_state.py）会跳过，手工定义。
std::vector<std::function<void()>> g_unityTasks;

