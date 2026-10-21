#pragma once
// ============================================================================
//  Offsets-iOS.h — iOS 版偏移表（RVA 相对 UnityFramework / 主可执行文件）
// ----------------------------------------------------------------------------
//  ⚠️ 这些 RVA 不能从 Android 版抄：libil2cpp.so 与 iOS 的 UnityFramework
//  是两份不同的编译产物，函数顺序、内联、链接布局都不同。
//
//  这里的 0x0 是"未标定"哨兵：Main/iOS 在挂钩前会检查地址是否落在镜像范围
//  内，0 偏移会被跳过并打日志，不会把代码写坏。
//
//  标定步骤（详见 docs/移植说明.md）：
//    1. 从 iOS 包里取出 UnityFramework（或主二进制），IDA/Ghidra 打开；
//    2. 按注释里的类/方法名在 global-metadata / 符号表里定位函数；
//    3. 记录相对 __TEXT 的偏移，填回下面每一项；
//    4. 同时导出 il2cpp 的 global-metadata.dat 用于 ByName 查类（可选）。
//
//  ABI 布局常量（对象头、List/Dictionary/Array 布局）在 il2cpp 上跨平台一致，
//  保留 Android 版的值即可，但如果 iOS 版本换了 Unity 大版本仍需复核。
// ============================================================================
#include <cstdint>
// =====================================================================
// OFFSET TERPUSAT - ubah di sini saat game update (tanpa cari di kode)
// =====================================================================
#define GAME_PACKAGE_NAME "com.xd.xdt"
// --- IL2CPP / Mono runtime ABI (biasanya TETAP antar update) ---
static const uintptr_t OFF_MONO_OBJ_HEADER = 0x10; // header object sebelum data (mono_object_unbox manual, panjang string)
static const int       MONO_FIELD_HEADER   = 16;   // hasil mono_field_get_offset dikurangi ini => offset di dalam value-type

// --- Layout Dictionary<TKey,TValue> / Array ---
static const uintptr_t OFF_ARR_COUNT   = 0x18; // panjang entries/array
static const uintptr_t OFF_ARR_DATA    = 0x20; // awal data entries/array
static const int       SZ_DICT_ENTRY   = 24;   // ukuran 1 entry Dictionary
static const uintptr_t OFF_ENTRY_KEY   = 8;    // offset key di dalam entry
static const uintptr_t OFF_ENTRY_VALUE = 16;   // offset value(ref) di dalam entry

// --- Layout List<T> (entry recipe) ---
static const uintptr_t SZ_LIST_ENTRY   = 0x18; // ukuran 1 entry list recipe

// --- Struct BackpackItem (raw offset, CEK saat game update) ---
static const uintptr_t OFF_BP_NETID    = 0x00;
static const uintptr_t OFF_BP_STATICID = 0x04;
static const uintptr_t OFF_BP_COUNT    = 0x14;

// =====================================================================
// OFFSET POINTER GAME (RVA @ libil2cpp.so) - WAJIB DICEK ULANG / DICARI
// LAGI SETIAP GAME UPDATE (beda dengan offset ABI mono di atas).
// Cara pakai: getExecutableBaseAddress("libil2cpp.so") + OFF_xxx
// =====================================================================

// --- Hook fungsi utama ---
static const uintptr_t OFF_GAMEAPP_UPDATE = 0x0; // GameApp.Update (dipakai DobbyHook)

static const uintptr_t OFF_THEMIS_P1  = 0x0;
static const uintptr_t OFF_THEMIS_P2  = 0x0;
static const uintptr_t OFF_THEMIS_P3  = 0x0;
static const uintptr_t OFF_THEMIS_P4  = 0x0;
static const uintptr_t OFF_THEMIS_P5  = 0x0;
static const uintptr_t OFF_THEMIS_P6  = 0x0;
static const uintptr_t OFF_THEMIS_P7  = 0x0;
static const uintptr_t OFF_THEMIS_P8  = 0x0;
static const uintptr_t OFF_THEMIS_P9  = 0x0;
static const uintptr_t OFF_THEMIS_P10 = 0x0;
static const uintptr_t OFF_THEMIS_P11 = 0x0;
static const uintptr_t OFF_THEMIS_P12 = 0x0;
static const uintptr_t OFF_THEMIS_P13 = 0x0;
static const uintptr_t OFF_THEMIS_P14 = 0x0;
static const uintptr_t OFF_THEMIS_P15 = 0x0;
static const uintptr_t OFF_THEMIS_P16 = 0x0;
static const uintptr_t OFF_THEMIS_P17 = 0x0;
static const uintptr_t OFF_THEMIS_P18 = 0x0;
static const uintptr_t OFF_THEMIS_P19 = 0x0; // return null (bool/string)
static const uintptr_t OFF_THEMIS_P20 = 0x0; // return null (bool/string)
static const uintptr_t OFF_THEMIS_P21 = 0x0; // return null (bool/string)
static const uintptr_t OFF_THEMIS_P22 = 0x0; // return null (bool/string)
static const uintptr_t OFF_THEMIS_P23 = 0x0; // return null (bool/string)
static const uintptr_t OFF_THEMIS_P24 = 0x0; // return null (bool/string)
static const uintptr_t OFF_THEMIS_P25 = 0x0; // return null (bool/string)

// --- Bypass XDPerf (telemetry/performance profiler) ---
static const uintptr_t OFF_XDPERF_INIT_ANDROID_JAVA_OBJECT = 0x0;
static const uintptr_t OFF_XDPERF_INIT                     = 0x0;
static const uintptr_t OFF_XDPERF_REPORT_UID                = 0x0;
static const uintptr_t OFF_XDPERF_REPORT_APP_INFO            = 0x0;
static const uintptr_t OFF_XDPERF_GET_DEVICE_PROFILE_PATH    = 0x0;
static const uintptr_t OFF_XDPERF_GET_CPU_TEMP               = 0x0;
static const uintptr_t OFF_XDPERF_GET_BATTERY_TEMP           = 0x0;
static const uintptr_t OFF_XDPERF_POST_POSITION               = 0x0;
static const uintptr_t OFF_XDPERF_POST_POSITION_OVERRIDE       = 0x0;

// --- Patch OverlapBox / OverlapSphere (dipakai fitur no-clip/build dll) ---
static const uintptr_t OFF_OVERLAP_BOX1    = 0x0;
static const uintptr_t OFF_OVERLAP_BOX2    = 0x0;
static const uintptr_t OFF_OVERLAP_SPHERE1 = 0x0;
static const uintptr_t OFF_OVERLAP_SPHERE2 = 0x0;


// --- Patch TSDK ---
static const uintptr_t OFF_SDK_P1 = 0x0; // public Void .ctor(Object object, IntPtr method)
static const uintptr_t OFF_SDK_P2 = 0x0; // public virtual Void Invoke(String condition, String stackTrace, LogType type)
static const uintptr_t OFF_SDK_P3 = 0x0; // public virtual IAsyncResult BeginInvoke(String condition, String stackTrace, LogType type, AsyncCallback callback, Object object)
static const uintptr_t OFF_SDK_P4 = 0x0; // public virtual Void EndInvoke(IAsyncResult result)
static const uintptr_t OFF_SDK_P5 = 0x0; // public static Void InitAndroidWithID(String appid)
static const uintptr_t OFF_SDK_P6 = 0x0; // public static Void ConfigDebugMode(Boolean enable)
static const uintptr_t OFF_SDK_P7 = 0x0; // public static Void U3d_ConfigAutoQuitApplication(Boolean autoQuit)
static const uintptr_t OFF_SDK_P8 = 0x0; // public static Void U3d_ConfigAutoReportLogLevel(LogSeverity level)
static const uintptr_t OFF_SDK_P9 = 0x0; // public static Void U3d_RegisterLogCallback(LogCallbackDelegate handler)
static const uintptr_t OFF_SDK_P10 = 0x0; // public static Void SetGamePlayer(String playerId)
static const uintptr_t OFF_SDK_P11 = 0x0; // public static Void SetGameScene(String sceneId)
static const uintptr_t OFF_SDK_P12 = 0x0; // public static Void ReportException(String name, String message, String stackTrace)
static const uintptr_t OFF_SDK_P13 = 0x0; // public static Void ReportLogError(String name, String message, String stackTrace)
static const uintptr_t OFF_SDK_P14 = 0x0; // public static Void ReportCustomException(String name, String reason, String message, Boolean isQuitApp)
static const uintptr_t OFF_SDK_P15 = 0x0; // public static Void AddCustomField(String str_key, String str_value)
static const uintptr_t OFF_SDK_P16 = 0x0; // public static Void SetCallback(TapAndroidCallBackImp cb)
static const uintptr_t OFF_SDK_P17 = 0x0; // public static Void SetEngine(Boolean isCustom, Boolean isOpenGl)
static const uintptr_t OFF_SDK_P18 = 0x0; // public static Void ReportCustomExceptionEx(String name, String reason, String message, Boolean isQuitApp, String extra_message, Int32 extra_len)
static const uintptr_t OFF_SDK_P19 = 0x0; // public static String GetHeartbeat(Int32 index, Int64 random)
static const uintptr_t OFF_SDK_P20 = 0x0; // public static String GetOneidData()
static const uintptr_t OFF_SDK_P21 = 0x0; // public static String GetOneID()
static const uintptr_t OFF_SDK_P22 = 0x0; // public static Void SetUseExtendCallback(Boolean b)
static const uintptr_t OFF_SDK_P23 = 0x0; // public static Void TMInit()
static const uintptr_t OFF_SDK_P24 = 0x0; // public static Void TMCR(String sceneId, Boolean on_off)
static const uintptr_t OFF_SDK_P25 = 0x0; // public static Void InputData(Int32 type, Single force, Single x, Single y, Int32 index, UInt32 source)
static const uintptr_t OFF_SDK_P26 = 0x0; // private static Void add__LogCallbackEventHandler(LogCallbackDelegate value)
static const uintptr_t OFF_SDK_P27 = 0x0; // private static Void remove__LogCallbackEventHandler(LogCallbackDelegate value)
static const uintptr_t OFF_SDK_P28 = 0x0; // public static String get_PluginVersion()
static const uintptr_t OFF_SDK_P29 = 0x0; // public static Boolean get_IsInitialized()
static const uintptr_t OFF_SDK_P30 = 0x0; // public static Boolean get_AutoQuitApplicationAfterReport()
static const uintptr_t OFF_SDK_P31 = 0x0; // private static Void _RegisterExceptionHandler()
static const uintptr_t OFF_SDK_P32 = 0x0; // public static Void LocalDebugLog(String format, Object[] args)
static const uintptr_t OFF_SDK_P33 = 0x0; // private static Void _OnLogCallbackHandler(String condition, String stackTrace, LogType type)
static const uintptr_t OFF_SDK_P34 = 0x0; // private static Void _OnUncaughtExceptionHandler(Object sender, UnhandledExceptionEventArgs args)
static const uintptr_t OFF_SDK_P35 = 0x0; // private static Void _HandleException(Exception e, String message, Boolean uncaught)
static const uintptr_t OFF_SDK_P36 = 0x0; // private static Void _HandleException(LogSeverity logLevel, String name, String message, String stackTrace, Boolean uncaught)
static const uintptr_t OFF_SDK_P37 = 0x0; // private static Void _reportException(String name, String reason, String stackTrace, Boolean uncaught)
static const uintptr_t OFF_SDK_P38 = 0x0; // private static Void .cctor()
static const uintptr_t OFF_SDK_P39 = 0x0; // public static TapAndroidAgent Get()
static const uintptr_t OFF_SDK_P40 = 0x0; // public virtual Void InitAndroidAgentWithID(String appID)
static const uintptr_t OFF_SDK_P41 = 0x0; // public virtual Void SetGamePlayer(String gamePlayer)
static const uintptr_t OFF_SDK_P42 = 0x0; // public virtual Void SetGameCurrentScene(String sceneId)
static const uintptr_t OFF_SDK_P43 = 0x0; // public virtual Void EnableDebugMode(Boolean enable)
static const uintptr_t OFF_SDK_P44 = 0x0; // public virtual Void ReportException(String name, String reason, String stackTrace, Boolean isQuitApp)
static const uintptr_t OFF_SDK_P45 = 0x0; // public virtual Void ReportCustomException(String name, String reason, String message, Boolean isQuitApp)
static const uintptr_t OFF_SDK_P46 = 0x0; // public virtual Void AddCustomField(String str_key, String str_value)
static const uintptr_t OFF_SDK_P47 = 0x0; // public virtual Void SetCallback(TapAndroidCallBackImp cb)
static const uintptr_t OFF_SDK_P48 = 0x0; // public virtual Void SetEngine(Boolean isCustom, Boolean isOpenGl)
static const uintptr_t OFF_SDK_P49 = 0x0; // public virtual Void ReportCustomExceptionEx(String name, String reason, String message, Boolean isQuitApp, String extra_message, Int32 extra_len)
static const uintptr_t OFF_SDK_P50 = 0x0; // public virtual String GetHeartbeat(Int32 index, Int64 random)
static const uintptr_t OFF_SDK_P51 = 0x0; // public virtual String GetOneidData()
static const uintptr_t OFF_SDK_P52 = 0x0; // public virtual Void SetUseExtendCallback(Boolean b)
static const uintptr_t OFF_SDK_P53 = 0x0; // public virtual Void TMInit()
static const uintptr_t OFF_SDK_P54 = 0x0; // public virtual Void TMCR(String sceneId, Boolean on_off)
static const uintptr_t OFF_SDK_P55 = 0x0; // public virtual Void InputData(Int32 type, Single force, Single x, Single y, Int32 index, UInt32 source)
static const uintptr_t OFF_SDK_P56 = 0x0; // public virtual String GetOneID()
static const uintptr_t OFF_SDK_P57 = 0x0; // public Void .ctor()
static const uintptr_t OFF_SDK_P58 = 0x0; // public Void .ctor(Object object, IntPtr method)
static const uintptr_t OFF_SDK_P59 = 0x0; // public virtual Void Invoke(String str_key, String str_value)
static const uintptr_t OFF_SDK_P60 = 0x0; // public virtual IAsyncResult BeginInvoke(String str_key, String str_value, AsyncCallback callback, Object object)
static const uintptr_t OFF_SDK_P61 = 0x0; // public virtual Void EndInvoke(IAsyncResult result)
static const uintptr_t OFF_SDK_P62 = 0x0; // public Void .ctor(Object object, IntPtr method)
static const uintptr_t OFF_SDK_P63 = 0x0; // public virtual Void Invoke(String name, String reason, String stackTrace, Int32 type, Boolean isQuitApp)
static const uintptr_t OFF_SDK_P64 = 0x0; // public virtual IAsyncResult BeginInvoke(String name, String reason, String stackTrace, Int32 type, Boolean isQuitApp, AsyncCallback callback, Object object)
static const uintptr_t OFF_SDK_P65 = 0x0; // public virtual Void EndInvoke(IAsyncResult result)
static const uintptr_t OFF_SDK_P66 = 0x0; // public Void .ctor(Object object, IntPtr method)
static const uintptr_t OFF_SDK_P67 = 0x0; // public virtual Void Invoke(String strEvent)
static const uintptr_t OFF_SDK_P68 = 0x0; // public virtual IAsyncResult BeginInvoke(String strEvent, AsyncCallback callback, Object object)
static const uintptr_t OFF_SDK_P69 = 0x0; // public virtual Void EndInvoke(IAsyncResult result)
static const uintptr_t OFF_SDK_P70 = 0x0; // public Void .ctor(Object object, IntPtr method)
static const uintptr_t OFF_SDK_P71 = 0x0; // public virtual Void Invoke(Boolean isEnable)
static const uintptr_t OFF_SDK_P72 = 0x0; // public virtual IAsyncResult BeginInvoke(Boolean isEnable, AsyncCallback callback, Object object)
static const uintptr_t OFF_SDK_P73 = 0x0; // public virtual Void EndInvoke(IAsyncResult result)
static const uintptr_t OFF_SDK_P74 = 0x0; // public Void .ctor(Object object, IntPtr method)
static const uintptr_t OFF_SDK_P75 = 0x0; // public virtual Void Invoke(Int32 type, Single force, Single x, Single y, Int32 index, UInt32 source)
static const uintptr_t OFF_SDK_P76 = 0x0; // public virtual IAsyncResult BeginInvoke(Int32 type, Single force, Single x, Single y, Int32 index, UInt32 source, AsyncCallback callback, Object object)
static const uintptr_t OFF_SDK_P77 = 0x0; // public virtual Void EndInvoke(IAsyncResult result)
static const uintptr_t OFF_SDK_P78 = 0x0; // private static AndroidJavaClass get_Android()
static const uintptr_t OFF_SDK_P79 = 0x0; // private static Void setAbi()
static const uintptr_t OFF_SDK_P80 = 0x0; // private static Void realInit(String appID)
static const uintptr_t OFF_SDK_P81 = 0x0; // public override Void InitAndroidAgentWithID(String appID)
static const uintptr_t OFF_SDK_P82 = 0x0; // public override Void ReportException(String name, String reason, String stackTrace, Boolean isQuitApp)
static const uintptr_t OFF_SDK_P83 = 0x0; // public override Void ReportCustomException(String name, String reason, String message, Boolean isQuitApp)
static const uintptr_t OFF_SDK_P84 = 0x0; // public override Void SetGamePlayer(String userInfo)
static const uintptr_t OFF_SDK_P85 = 0x0; // public override Void SetGameCurrentScene(String sceneId)
static const uintptr_t OFF_SDK_P86 = 0x0; // public override Void EnableDebugMode(Boolean enable)
static const uintptr_t OFF_SDK_P87 = 0x0; // public override Void AddCustomField(String str_key, String str_value)
static const uintptr_t OFF_SDK_P88 = 0x0; // public override Void SetCallback(TapAndroidCallBackImp cb)
static const uintptr_t OFF_SDK_P89 = 0x0; // public override Void SetEngine(Boolean isCustom, Boolean isOpenGl)
static const uintptr_t OFF_SDK_P90 = 0x0; // public override Void ReportCustomExceptionEx(String name, String reason, String message, Boolean isQuitApp, String extra_message, Int32 extra_len)
static const uintptr_t OFF_SDK_P91 = 0x0; // public override String GetHeartbeat(Int32 index, Int64 random)
static const uintptr_t OFF_SDK_P92 = 0x0; // public override String GetOneidData()
static const uintptr_t OFF_SDK_P93 = 0x0; // public override Void SetUseExtendCallback(Boolean b)
static const uintptr_t OFF_SDK_P94 = 0x0; // public override Void TMInit()
static const uintptr_t OFF_SDK_P95 = 0x0; // public override Void TMCR(String sceneId, Boolean on_off)
static const uintptr_t OFF_SDK_P96 = 0x0; // public override Void InputData(Int32 type, Single force, Single x, Single y, Int32 index, UInt32 source)
static const uintptr_t OFF_SDK_P97 = 0x0; // public override String GetOneID()
static const uintptr_t OFF_SDK_P98 = 0x0; // public Void .ctor()
static const uintptr_t OFF_SDK_P99 = 0x0; // private static Void .cctor()
static const uintptr_t OFF_SDK_P100 = 0x0; // public Void .ctor(TapAndroidCallBack cb)
static const uintptr_t OFF_SDK_P101 = 0x0; // public Void onHandleAndroidState(Int32 state, String message)
static const uintptr_t OFF_SDK_P102 = 0x0; // public String getExtraMessage()
static const uintptr_t OFF_SDK_P103 = 0x0; // public String getExceptionMessage()
static const uintptr_t OFF_SDK_P104 = 0x0; // public String getExtraMessageEx(String message)
static const uintptr_t OFF_SDK_P105 = 0x0; // public String getOOMMessage(Single v)
static const uintptr_t OFF_SDK_P106 = 0x0; // public String getStrongKillMessage()
static const uintptr_t OFF_SDK_P107 = 0x0; // private static Void .cctor()

#define PATCH_1 "CollectAntiCheatingFormatter"
#define PATCH_2 "MovementAntiCheatingFormatter"
