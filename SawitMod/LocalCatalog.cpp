#include "LocalCatalog.h"
#include "LocalCatalogData.h"
#include "Global.h"
#include "NetAuth.h"
#include "Version.h"
#include "VirboxTargets.h"
#include "Includes/obfuscate.h"
// iOS：没有 <android/log.h>，日志走 Includes/Logger.h 的垫片；存储路径走
// App/Platform.h（Documents / Application Support / tmp）。
#include "App/Platform.h"
#include "Includes/Logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define LC_TAG "SawitLocalCatalog"
// Android 版是 __android_log_print；iOS 版直接映射到 LOGI/LOGE（默认空语句）。
#define LC_LOG(...) LOGI(__VA_ARGS__)
#define LC_ERR(...) LOGE(__VA_ARGS__)

namespace {

// kEmbeddedPayloads 按 catalog 顺序排过，同一个 wire 的条目是连续的一段。
struct WireSpan {
    int start = 0;
    int count = 0;
};

std::once_flag g_indexOnce;
std::unordered_map<std::string, WireSpan> g_wireIndex;

std::mutex g_rotateMtx;
std::unordered_map<std::string, uint32_t> g_rotate;

std::mutex g_stateMtx;
std::string g_catalogSource = "none";
std::size_t  g_catalogLength = 0;

void BuildWireIndex() {
    for (int i = 0; i < kEmbeddedPayloadCount; ++i) {
        const char* wire = kEmbeddedPayloads[i].wire;
        if (!wire || !*wire) continue;
        auto it = g_wireIndex.find(wire);
        if (it == g_wireIndex.end()) {
            WireSpan span;
            span.start = i;
            span.count = 1;
            g_wireIndex.emplace(std::string(wire), span);
        } else {
            ++it->second.count;
        }
    }
}

std::unordered_map<std::string, WireSpan>& WireIndex() {
    std::call_once(g_indexOnce, BuildWireIndex);
    return g_wireIndex;
}

const char* Safe(const char* s) { return s ? s : ""; }

}  // namespace

// =====================================================================
//  格式闸
// =====================================================================
bool LocalCatalog::LooksLikeDumpSnapshot(const std::string& catalogJson) {
    return catalogJson.find("\"wireId\"") != std::string::npos &&
           catalogJson.find("{\"w\":") == std::string::npos;
}

bool LocalCatalog::IsValidCatalog(const std::string& catalogJson) {
    // 必须是云端原始 catalog：带 "features" 数组，且 feature 用 "w" 作 wire 字段。
    // dump 快照（"wireId"）在这里被拒 —— 放进去 RebuildWireMap 会解析出 0 条映射，
    // 反而把内嵌映射覆盖成空表。
    if (catalogJson.size() < 32) return false;
    if (catalogJson.find("\"features\"") == std::string::npos) return false;
    if (catalogJson.find("{\"w\":") == std::string::npos) return false;
    if (LooksLikeDumpSnapshot(catalogJson)) return false;
    return true;
}

// =====================================================================
//  中文名彻底本地化（菜单固定中文，界面上不留英文）
// =====================================================================
namespace {

// 目录里残留的英文词。长的必须排在前面（NetID 先于 ID）。
struct TermMap {
    const char* from;
    const char* to;
};

const TermMap kNameTerms[] = {
    {"NetID", "\xe7\xbd\x91\xe7\xbb\x9c\xe7\xbc\x96\xe5\x8f\xb7"},   // 网络编号
    {"ANIMAL", "\xe5\x8a\xa8\xe7\x89\xa9"},                          // 动物
    {"EVENT", "\xe6\xb4\xbb\xe5\x8a\xa8"},                           // 活动
    {"NPC", "\xe8\xa7\x92\xe8\x89\xb2"},                             // 角色
    {"ID", "\xe7\xbc\x96\xe5\x8f\xb7"},                              // 编号
};

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    for (std::size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size()))
        s.replace(p, from.size(), to);
}

}  // namespace

void LocalCatalog::LocalizeCatalogName(std::string& s) {
    // 1) 尾部纯英文括注
    for (;;) {
        if (s.size() < 3 || s.back() != ')') break;
        const std::size_t open = s.rfind('(');
        if (open == std::string::npos || open == 0) break;
        const std::string inner = s.substr(open + 1, s.size() - open - 2);
        bool latin = false, nonAscii = false;
        for (std::size_t i = 0; i < inner.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(inner[i]);
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) latin = true;
            if (c >= 0x80) nonAscii = true;   // 括号里有中文 -> 保留
        }
        if (!latin || nonAscii) break;
        s.erase(open);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    }

    // 2) 残留英文词
    for (const TermMap& t : kNameTerms)
        ReplaceAll(s, t.from, t.to);

    // 3) 中文后的半角冒号统一成全角
    for (std::size_t p = s.find(": "); p != std::string::npos; p = s.find(": ", p + 1)) {
        if (p == 0) continue;
        if (static_cast<unsigned char>(s[p - 1]) >= 0x80) {
            s.replace(p, 2, "\xef\xbc\x9a");   // ：
        }
    }
}

// =====================================================================
//  内嵌数据访问
// =====================================================================
const char* LocalCatalog::EmbeddedCatalogJson() { return kEmbeddedCatalogJson; }
std::size_t LocalCatalog::EmbeddedCatalogSize() { return kEmbeddedCatalogJsonLen; }
int LocalCatalog::EmbeddedFeatureCount() { return (int)SAWIT_EMBEDDED_FEATURE_COUNT; }
int LocalCatalog::EmbeddedWireCount() { return (int)SAWIT_EMBEDDED_WIRE_COUNT; }
int LocalCatalog::EmbeddedPayloadTotal() { return (int)kEmbeddedPayloadCount; }
int LocalCatalog::EmbeddedPayloadWireCount() { return (int)kEmbeddedPayloadWireCount; }
int LocalCatalog::EmbeddedMissingPayloadWireCount() {
    return (int)kEmbeddedMissingPayloadWireCount;
}
const char* LocalCatalog::EmbeddedBindVersion() { return SAWIT_EMBEDDED_BIND_VERSION; }
const char* LocalCatalog::EmbeddedBindChannel() { return SAWIT_EMBEDDED_BIND_CHANNEL; }
const char* LocalCatalog::EmbeddedCatalogTimestamp() { return SAWIT_EMBEDDED_CATALOG_TS; }

// =====================================================================
//  装载 catalog
// =====================================================================
bool LocalCatalog::InstallCatalog(const std::string& catalogJson, const char* source) {
    if (!IsValidCatalog(catalogJson)) {
        if (LooksLikeDumpSnapshot(catalogJson)) {
            LC_ERR("catalog rejected (%s): dump snapshot format (\"wireId\"), need cloud \"w\"",
                   Safe(source));
        } else {
            LC_ERR("catalog rejected (%s): format gate failed", Safe(source));
        }
        return false;
    }
    // 解析全部交给 NetAuth.cpp（那边已经有 wire 映射与菜单解析器）。
    SawitApplyCatalog(catalogJson);
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        g_catalogSource = Safe(source);
        g_catalogLength = catalogJson.size();
    }
    LC_LOG("catalog installed from %s (%zu bytes, %zu features)",
           Safe(source), catalogJson.size(), featureList.size());
    return true;
}

bool LocalCatalog::InstallEmbedded() {
#if SAWIT_LOCAL_FEATURES
    const std::string json(kEmbeddedCatalogJson, kEmbeddedCatalogJsonLen);
    return InstallCatalog(json, "embedded");
#else
    return false;
#endif
}

std::string LocalCatalog::CatalogSource() {
    std::lock_guard<std::mutex> lk(g_stateMtx);
    return g_catalogSource;
}

std::size_t LocalCatalog::CatalogLength() {
    std::lock_guard<std::mutex> lk(g_stateMtx);
    return g_catalogLength;
}

// =====================================================================
//  本地 payload 回放
// =====================================================================
bool LocalCatalog::HasPayload(const std::string& wire) {
    if (wire.empty()) return false;
    return WireIndex().find(wire) != WireIndex().end();
}

int LocalCatalog::PayloadCountForWire(const std::string& wire) {
    if (wire.empty()) return 0;
    auto& idx = WireIndex();
    auto it = idx.find(wire);
    return it == idx.end() ? 0 : it->second.count;
}

bool LocalCatalog::TakePayload(const std::string& wire, Entry& out) {
    if (wire.empty()) return false;
    auto& idx = WireIndex();
    auto it = idx.find(wire);
    if (it == idx.end() || it->second.count <= 0) return false;

    const WireSpan& span = it->second;
    uint32_t slot = 0;
    {
        std::lock_guard<std::mutex> lk(g_rotateMtx);
        uint32_t& cursor = g_rotate[wire];
        slot = cursor % (uint32_t)span.count;
        ++cursor;
    }
    const EmbeddedPayloadEntry& e = kEmbeddedPayloads[span.start + (int)slot];
    out.op          = e.op;
    out.wire        = Safe(e.wire);
    out.nonce       = Safe(e.nonce);
    out.hwid        = Safe(e.hwid);
    out.bindVersion = Safe(e.bindVersion);
    out.bindChannel = Safe(e.bindChannel);
    out.envelope    = Safe(e.envelope);
    return !out.envelope.empty();
}

// =====================================================================
//  自检
// =====================================================================
namespace {

struct OpProbe {
    int         op;
    const char* label;
};

const OpProbe kCriticalOps[] = {
    {14, "Open Shop / 商店"},
    {21, "Auto Cook / 烹饪"},
    {8,  "Teleport To Position / 传送"},
    {9,  "Vehicle Swap / 载具替换"},
};

}  // namespace

std::string LocalCatalog::BuildSelfTestReport(const std::string& catalogSource) {
    std::ostringstream o;
    int wireEntries = 0;
    int opEntries = 0;
    {
        std::lock_guard<std::mutex> lk(g_opToWireMtx);
        wireEntries = (int)g_wireToOp.size();
        opEntries = (int)g_opToWire.size();
    }

    const std::time_t now = std::time(nullptr);
    o << "SawitMod SelfTest / LocalCatalog\r\n";
    o << "build           : " << SAWIT_VERSION << "  channel=" << SAWIT_CHANNEL << "\r\n";
    o << "flags           : offline=" << (int)SAWIT_OFFLINE_MODE
      << " localFeatures=" << (int)SAWIT_LOCAL_FEATURES
      << " localPayloadOnly=" << (int)SAWIT_LOCAL_PAYLOAD_ONLY << "\r\n";
    o << "now             : " << (long long)now << "\r\n";
    o << "catalogSource   : " << catalogSource.c_str() << "\r\n";
    o << "catalogTs       : " << SAWIT_EMBEDDED_CATALOG_TS << "\r\n";
    o << "catalogBytes    : " << (unsigned long long)CatalogLength()
      << " (embedded=" << (unsigned long long)kEmbeddedCatalogJsonLen << ")\r\n";
    o << "features        : " << featureList.size()
      << " (embedded=" << (int)SAWIT_EMBEDDED_FEATURE_COUNT << ")\r\n";
    o << "wireEntries     : " << wireEntries
      << " (embedded=" << (int)SAWIT_EMBEDDED_WIRE_COUNT << ")\r\n";
    o << "opEntries       : " << opEntries << "\r\n";
    o << "payloadTotal    : " << kEmbeddedPayloadCount << "\r\n";
    o << "payloadWires    : " << kEmbeddedPayloadWireCount << "\r\n";
    o << "missingWires    : " << kEmbeddedMissingPayloadWireCount << "\r\n";
    o << "bindVersion     : " << SAWIT_EMBEDDED_BIND_VERSION << "\r\n";
    o << "bindChannel     : " << SAWIT_EMBEDDED_BIND_CHANNEL << "\r\n";

    o << "\r\n-- critical ops (菜单主按钮) --\r\n";
    for (const OpProbe& probe : kCriticalOps) {
        const std::string wire = WireForOp(probe.op);
        const int payloads = PayloadCountForWire(wire);
        o << "op=" << probe.op << " " << probe.label
          << " wire=" << (wire.empty() ? std::string("-") : wire)
          << " payloads=" << payloads
          << " lookup=" << (wire.empty() ? "NO_WIRE" : (payloads > 0 ? "OK" : "MISSING"))
          << "\r\n";
    }

    o << "\r\n-- all ops --\r\n";
    // op=0 的功能（通用面板/老板键类）没有 op 索引，直接看内嵌表里有多少条。
    std::unordered_set<std::string> zeroWires;
    for (int i = 0; i < kEmbeddedPayloadCount; ++i) {
        if (kEmbeddedPayloads[i].op == 0) zeroWires.insert(Safe(kEmbeddedPayloads[i].wire));
    }
    int zeroWireTotal = 0;
    {
        std::lock_guard<std::mutex> lk(g_opToWireMtx);
        for (const auto& kv : g_wireToOp) {
            if (kv.second == 0) ++zeroWireTotal;
        }
    }
    o << "op=0 (generic)  wires=" << zeroWireTotal
      << " wiresWithPayload=" << zeroWires.size()
      << " lookup=" << (!zeroWires.empty() ? "OK" : "MISSING") << "\r\n";

    for (int op = 1; op <= (int)OP_AUTO_FISH_BAIT; ++op) {
        const std::string wire = WireForOp(op);
        const int payloads = PayloadCountForWire(wire);
        o << "op=" << op
          << " wire=" << (wire.empty() ? std::string("-") : wire)
          << " payloads=" << payloads
          << " lookup=" << (wire.empty() ? "NO_WIRE" : (payloads > 0 ? "OK" : "MISSING"))
          << "\r\n";
    }

    o << "\r\n-- wires without captured payload --\r\n";
    for (int i = 0; i < kEmbeddedMissingPayloadWireCount; ++i) {
        const char* w = kEmbeddedMissingPayloadWires[i];
        o << (w ? w : "") << " op=" << OpForWire(Safe(w)) << "\r\n";
    }
    return o.str();
}

bool LocalCatalog::WriteSelfTest(const std::string& catalogSource) {
    const std::string report = BuildSelfTestReport(catalogSource);
    // iOS 没有 /storage/emulated/0 与 /data/data/<pkg>：按 Android 版"多个候选路径
    // 依次尝试、能写几个写几个"的逻辑，换成沙盒里三个可写目录。
    //   Android/data/<pkg>/files/...  -> Documents（Finder/iTunes 可取）
    //   /data/data/<pkg>/files/...    -> Application Support（SMPlatform::DataDir）
    //   /data/user/0/<pkg>/files/...  -> tmp
    const std::string candidates[] = {
        SMPlatform::DocumentDir() + "/SawitMod_selftest.txt",
        SMPlatform::DataDir() + "/SawitMod_selftest.txt",
        SMPlatform::LibraryDir() + "/SawitMod_selftest.txt",
    };
    bool wrote = false;
    for (const std::string& path : candidates) {
        std::ofstream f(path, std::ios::out | std::ios::trunc);
        if (!f.is_open()) continue;
        f.write(report.data(), (std::streamsize)report.size());
        f.flush();
        const bool ok = !f.fail();
        f.close();
        if (!ok) continue;
        LC_LOG("selftest written: %s", path.c_str());
        wrote = true;
    }
    if (!wrote) LC_ERR("selftest: no writable path");
    return wrote;
}

void LocalCatalog::SelfTest(const std::string& catalogSource) {
    int wireEntries = 0;
    int opEntries = 0;
    {
        std::lock_guard<std::mutex> lk(g_opToWireMtx);
        wireEntries = (int)g_wireToOp.size();
        opEntries = (int)g_opToWire.size();
    }
    WriteSelfTest(catalogSource);
    LC_LOG("selftest: source=%s features=%d wireEntries=%d opEntries=%d "
           "payload=%d payloadWires=%d missing=%d bind=%s/%s",
           catalogSource.c_str(), (int)featureList.size(), wireEntries, opEntries,
           (int)kEmbeddedPayloadCount, (int)kEmbeddedPayloadWireCount,
           (int)kEmbeddedMissingPayloadWireCount,
           SAWIT_EMBEDDED_BIND_VERSION, SAWIT_EMBEDDED_BIND_CHANNEL);
}
