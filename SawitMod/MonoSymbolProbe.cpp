// ============================================================================
//  MonoSymbolProbe.cpp — 符号自检 / 类名导出（诊断用）
// ----------------------------------------------------------------------------
//  两个解锁功能都要按名字去 Mono 元数据里找类、方法、字段。PC 版的 dump（dump.cs）
//  对不上 Android 包体，而程序集在磁盘上是 XDENCODE 容器、只有进进程才解密，所以
//  "名字到底对不对"只能在运行时回答。
//
//  这个功能把答案落到文件里：
//    * 已加载的全部程序集名（确认 EcsClient / XDTGameUI ... 的真实拼写）
//    * 两个功能依赖的每一个类/方法/字段的解析结果（OK / NULL + 命中镜像）
//    * 关键字命中的类清单（命名空间.类名 + 命中的方法 / 字段）
//
//  输出路径（第一个能写的为准）：
//    <App Documents>/SawitMod_symbols.txt        （iOS）
//    <App DataDir>/SawitMod_symbols.txt
//  Android 原版是 /data/data/<pkg>/files 与 /sdcard/Android/data/<pkg>/files。
//
//  全部只读元数据查询，不改游戏任何状态。
// ============================================================================

#include "App/Platform.h"
#include "ModFeatures.h"
#include "ModUtils.h"
#include "MonoGameApi.h"
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace MonoApi;

namespace {

constexpr int kMonoTableTypeDef = 0x02;
constexpr size_t kMaxReportBytes = 512 * 1024;
constexpr int kMaxClassesPerImage = 20000;

// 关键字：命中的类会被展开（方法/字段也按同一批关键字过滤）。
const char *kProbeKeywords[] = {
        OBFUSCATE("FriendInteract"), OBFUSCATE("FriendDouble"), OBFUSCATE("FriendDuo"),
        OBFUSCATE("ExpressionAction"), OBFUSCATE("Emoji"), OBFUSCATE("Emote"),
        OBFUSCATE("SocialReq"), OBFUSCATE("SocialFace"), OBFUSCATE("SingleShow"),
        OBFUSCATE("PlayerSocial"), OBFUSCATE("TableData"), OBFUSCATE("TableFriend"),
        OBFUSCATE("ProtocolManager"), OBFUSCATE("EventCenter"),
        OBFUSCATE("CharacterProtocol"),
};
constexpr int kProbeKeywordCount = sizeof(kProbeKeywords) / sizeof(kProbeKeywords[0]);

bool MatchesKeyword(const char *name) {
    if (!name || !*name) return false;
    for (int i = 0; i < kProbeKeywordCount; ++i) {
        if (strstr(name, kProbeKeywords[i]) != nullptr) return true;
    }
    return false;
}

struct Report {
    std::string text;

    void Line(const std::string &line) {
        if (text.size() >= kMaxReportBytes) return;
        text += line;
        text += '\n';
    }

    void Section(const std::string &title) {
        Line("");
        Line("==== " + title + " ====");
    }
};

void CollectImageNames(std::vector<std::string> *names) {
    MonoAssemblyForeachFn forEach = AssemblyForeach();
    if (!forEach) return;

    std::set<std::string> unique;
    forEach([](void *assembly, void *user) {
        auto *out = static_cast<std::set<std::string> *>(user);
        if (!out || !_monoAssemblyGetImage) return;
        void *image = _monoAssemblyGetImage(assembly);
        if (!image) return;
        const std::string name = SafeImageName(image);
        if (!name.empty()) out->insert(name);
    }, &unique);

    for (const std::string &name: unique) names->push_back(name);
}

// 需要的符号清单：跑一遍解析，把结果写进报告。
void ProbeRequiredSymbols(Report *report) {
    struct ClassProbe {
        const char *nameSpace;
        const char *className;
    };
    const ClassProbe classes[] = {
            {OBFUSCATE(""), OBFUSCATE("TableData")},
            {OBFUSCATE(""), OBFUSCATE("TableFriendInteract")},
            {OBFUSCATE("ClientSystem.ExpressionAction"),
             OBFUSCATE("ExpressionActionClientService")},
            {OBFUSCATE("XDT.Scene.Shared.Modules.ExpressionAction"),
             OBFUSCATE("ExpressionActionComponent")},
            {OBFUSCATE("XDTDataAndProtocol.ProtocolService.GamePlay.Character"),
             OBFUSCATE("CharacterProtocolManager")},
            {OBFUSCATE("XDTLevelAndEntity.Gameplay.Interaction"), OBFUSCATE("SocialReqTask")},
            {OBFUSCATE("XDTGame.Core"), OBFUSCATE("EventCenter")},
            {OBFUSCATE("XDTDataAndProtocol.Events.Player"), OBFUSCATE("SocialFaceEvent")},
            {OBFUSCATE("XDTDataAndProtocol"), OBFUSCATE("PlayerDataCenter")},
    };

    report->Section("依赖符号解析");
    for (const ClassProbe &probe: classes) {
        std::string imageName;
        void *klass = FindClass(nullptr, 0, probe.nameSpace, probe.className, &imageName, true);
        std::string line = std::string("class ") + probe.nameSpace + "." + probe.className
                           + " => " + (klass ? "OK" : "NULL");
        if (klass && !imageName.empty()) line += "  [image=" + imageName + "]";
        report->Line(line);
        if (!klass) continue;

        // 每个方法报"按签名解析"的结果 + 该名字下所有重载的真实签名：
        // 名字对不上还是签名对不上，看这一行就知道。签名串：i=整数 b=bool s=string
        // v=结构体 o=引用类型。
        struct MethodProbe {
            const char *name;
            const char *expected;   // 空串 = 无参
        };
        const MethodProbe methodProbes[] = {
                {"GetFriendInteract", "ib"},
                {"GetSingleaction", "ib"},
                {"GetExpression", "ib"},
                {"ExpressionActionComponent", ""},
                {"SendSingleAction", "ib"},
                {"SendEmotion", "i"},
                {"DispatchEvent", "?"},
                {"GetSelfNetPlayerId", ""},
                {"OnResp", "?"},
        };
        for (const MethodProbe &methodProbe: methodProbes) {
            void *method = FindMethodBySignature(klass, methodProbe.name, methodProbe.expected);
            report->Line(std::string("    .") + methodProbe.name + "("
                         + methodProbe.expected + ") => " + (method ? "OK" : "NULL")
                         + "   候选: " + DescribeMethodsNamed(klass, methodProbe.name));
        }

        void *fields[] = {FindFieldInHierarchy(klass, OBFUSCATE("gainCondition")),
                          FindFieldInHierarchy(klass, OBFUSCATE("unlockType")),
                          FindFieldInHierarchy(klass, OBFUSCATE("ExpressionActionType")),
                          FindFieldInHierarchy(klass, OBFUSCATE("Id")),
                          FindFieldInHierarchy(klass, OBFUSCATE("socialType")),
                          FindFieldInHierarchy(klass, OBFUSCATE("_isLoop"))};
        const char *fieldNames[] = {"gainCondition", "unlockType", "ExpressionActionType", "Id",
                                    "socialType", "_isLoop"};
        for (int i = 0; i < 6; ++i) {
            report->Line(std::string("    fld ") + fieldNames[i] + " => "
                         + (fields[i] ? "OK" : "NULL"));
        }
    }
}

// 把某个镜像里"关键字命中"的类展开成 命名空间.类名 + 方法 + 字段。
void DumpImage(const std::string &imageName, Report *report) {
    void *image = LoadedImage(imageName.c_str());
    if (!image) return;

    MonoImageTableInfoFn tableInfo = ImageGetTableInfo();
    MonoTableInfoRowsFn rows = TableInfoGetRows();
    MonoClassGetFn classGet = ClassGet();
    if (!tableInfo || !rows || !classGet) return;

    void *typeDefTable = tableInfo(image, kMonoTableTypeDef);
    if (!typeDefTable) return;
    const int rowCount = rows(typeDefTable);
    if (rowCount <= 0) return;

    int matched = 0;
    const int limit = rowCount < kMaxClassesPerImage ? rowCount : kMaxClassesPerImage;
    for (int rid = 1; rid <= limit; ++rid) {
        void *klass = classGet(image, 0x02000000u | static_cast<uint32_t>(rid));
        if (!klass) continue;

        const char *rawName = _monoClassGetName ? _monoClassGetName(klass) : nullptr;
        const std::string fullName = ClassFullName(klass);
        MonoClassGetNamespaceFn nsFn = ClassGetNamespace();
        const char *nameSpace = nsFn ? nsFn(klass) : nullptr;

        const bool classHit = MatchesKeyword(rawName) || MatchesKeyword(nameSpace);
        if (!classHit) continue;

        ++matched;
        report->Line("");
        report->Line("class " + fullName);

        MonoClassGetMethodsFn getMethods = ClassGetMethods();
        MonoMethodGetNameFn methodName = MethodGetName();
        if (getMethods && methodName) {
            void *iter = nullptr;
            void *method = nullptr;
            while ((method = getMethods(klass, &iter)) != nullptr) {
                const char *mName = methodName(method);
                if (!MatchesKeyword(mName)) continue;
                const std::string codes = MethodParamTypeCodes(method);
                report->Line(std::string("    m ") + (mName ? mName : "?") + "("
                             + (codes.empty() ? std::to_string(MethodParamCount(method))
                                              : codes) + ")"
                             + (MethodIsStatic(method) ? " [static]" : " [inst]"));
            }
        }

        if (_monoClassGetFields && _monoFieldName) {
            void *iter = nullptr;
            void *field = nullptr;
            while ((field = _monoClassGetFields(klass, &iter)) != nullptr) {
                const char *fName = _monoFieldName(field);
                report->Line(std::string("    f ") + (fName ? fName : "?"));
            }
        }
    }

    report->Line("");
    report->Line("[image " + imageName + "] typedefs=" + std::to_string(rowCount)
                 + " matched=" + std::to_string(matched));
}

bool WriteReport(const std::string &text, std::string *outPath) {
    // iOS：没有 /sdcard 与 /data/local/tmp；诊断文件写到 App 的 Documents。
    const std::vector<std::string> paths = {
            SMPlatform::DocumentDir() + "/SawitMod_symbols.txt",
            SMPlatform::DataDir() + "/SawitMod_symbols.txt",
    };
    for (const std::string &path: paths) {
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file.is_open()) continue;
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        const bool ok = !file.fail();
        file.close();
        if (!ok) continue;
        if (outPath) *outPath = path;
        return true;
    }
    return false;
}

} // namespace

// 菜单按钮调用（自己转 Unity 线程）。
void RunMonoSymbolProbeNative() {
    if (g_symbolProbeBusy.exchange(true)) return;

    RunOnUnityThreadAsync([]() {
        Report report;
        report.Line("SawitMod Mono symbol probe");
        report.Line("package=" + SMPlatform::BundleId());

        if (!_monoGetRootDomain || !_monoGetRootDomain()) {
            report.Line("Mono 域未就绪。");
        } else {
            if (_monoThreadAttach) _monoThreadAttach(_monoGetRootDomain());

            std::vector<std::string> images;
            CollectImageNames(&images);
            report.Section("已加载程序集 (" + std::to_string(images.size()) + ")");
            for (const std::string &name: images) report.Line("  " + name);

            ProbeRequiredSymbols(&report);

            report.Section("关键字命中的类");
            const char *gameImages[] = {
                    OBFUSCATE("EcsClient"), OBFUSCATE("EcsSystem"),
                    OBFUSCATE("XDTLevelAndEntity"), OBFUSCATE("XDTDataAndProtocol"),
                    OBFUSCATE("XDTGameSystem"), OBFUSCATE("XDTGameUI"),
                    OBFUSCATE("XDTBaseService"), OBFUSCATE("Plugins"),
                    OBFUSCATE("ScriptBridge"), OBFUSCATE("TypeRegister"),
            };
            for (const char *imageName: gameImages) DumpImage(imageName, &report);
        }

        std::string path;
        const bool written = WriteReport(report.text, &path);
        {
            std::lock_guard<std::mutex> lock(g_symbolProbeMutex);
            g_symbolProbeReport = written
                    ? (u8"报告已写入：" + path + u8"（" + std::to_string(report.text.size())
                       + u8" 字节）")
                    : u8"报告写入失败（没有可写路径）。";
        }
        LOGI(OBFUSCATE("[SymbolProbe] %s"), g_symbolProbeReport.c_str());

        // 文件写不进去也不怕：把"依赖符号解析"那一节直接打到 logcat 里。
        {
            size_t begin = report.text.find("==== 依赖符号解析 ====");
            if (begin != std::string::npos) {
                const size_t end = report.text.find("\n\n", begin + 10);
                std::string section = report.text.substr(begin, end == std::string::npos
                        ? std::string::npos : end - begin);
                size_t lineStart = 0;
                int lines = 0;
                while (lineStart < section.size() && lines < 60) {
                    size_t lineEnd = section.find('\n', lineStart);
                    if (lineEnd == std::string::npos) lineEnd = section.size();
                    const std::string line = section.substr(lineStart, lineEnd - lineStart);
                    if (!line.empty()) LOGI(OBFUSCATE("[SymbolProbe] %s"), line.c_str());
                    ++lines;
                    lineStart = lineEnd + 1;
                }
            }
        }
        g_symbolProbeBusy.store(false, std::memory_order_release);
    });
}

std::string GetMonoSymbolProbeReport() {
    std::lock_guard<std::mutex> lock(g_symbolProbeMutex);
    return g_symbolProbeReport;
}

// ---------------------------------------------------------------------------
//  进世界后自动跑一次自检（只读元数据；不挂 hook、不开任何功能）
// ---------------------------------------------------------------------------
//  存在的理由：两个解锁功能的类名/方法名只能在运行时确认，而"点菜单按钮"这一步
//  在回归时最容易卡住。这段只做三件事：枚举程序集 → 解析依赖符号 → 写报告，
//  不碰游戏状态、不装 detour。要关掉：Version.h 的 SAWIT_AUTO_SYMBOL_PROBE=0。
void MaybeRunAutoSymbolProbe() {
    static bool done = false;
    static int frameCounter = 0;
    if (done) return;
    if (++frameCounter < 120) return;       // 注入后约 2 秒再看
    frameCounter = 0;
    if (!IsGameWorldReadyNative()) return;  // 没进世界就下一轮再看
    done = true;
    LOGI(OBFUSCATE("[SymbolProbe] 已进世界，自动跑一次只读符号自检"));
    RunMonoSymbolProbeNative();
}

