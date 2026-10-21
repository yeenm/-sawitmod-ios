#pragma once

#include <cstddef>
#include <string>

// =====================================================================
//  LocalCatalog — 离线功能目录 + 云端 dump 回放
// =====================================================================
// 背景（见 SawitMod_离线化诊断报告）：
//   1. 关闭卡密验证后，菜单能开，但 JSON 下发的功能（商店/烹饪/传送/载具替换…）
//      全部点不动 —— 因为离线构建从来没拿到过功能目录，WireForOp(OP_*) 返回空。
//   2. 设备上那份 dump 快照 catalog.json 的 feature 字段名是 "wireId"，而
//      Virbox_RebuildWireMap / Virbox_MenuCatalogParse 只认云端原始格式的 "w"。
//      误把快照喂进去会解析出 0 条映射，映射表被清空 → HasWire 全失败。
//
// 这个模块把抓包下来的云端下发内容内嵌进 so：
//   * 云端原始 catalog（含 "w"）→ 重建 op<->wire 映射、菜单列表、商店表、广播；
//   * 194 条 SVM2 信封 + 它们抓包时的 nonce/hwid/version/channel → 本地回放，
//     绑定串 SHA256(nonce|hwid|wire|version|channel|op) 才校验得过。
// 全程不请求服务器，SAWIT_OFFLINE_MODE=1 的构建里这就是功能数据的唯一来源。
namespace LocalCatalog {

    // catalog 格式闸 ----------------------------------------------------
    // 只认云端原始 catalog：必须含 "features" 且含 "{\"w\":"。
    bool IsValidCatalog(const std::string& catalogJson);
    // dump 快照格式（"wireId"）——识别出来只为把它挡掉并记日志。
    bool LooksLikeDumpSnapshot(const std::string& catalogJson);

    // 把目录里下发的中文名彻底本地化：
    //   1) 去掉尾部的纯英文括注："打开商店 (Open Shop)" -> "打开商店"
    //   2) 词表替换残留的英文词：NetID->网络编号 / ID->编号 / NPC->角色 /
    //      ANIMAL->动物 / EVENT->活动
    //   3) 中文后紧跟的 ": " 换成全角 "："
    // 括号内含中文（如 "界面 (UI) 设置"）或本来就以非 ')' 结尾时不动。
    void LocalizeCatalogName(std::string& s);

    // ---------------- 内嵌数据 ----------------
    const char* EmbeddedCatalogJson();
    std::size_t EmbeddedCatalogSize();
    int         EmbeddedFeatureCount();
    int         EmbeddedWireCount();
    int         EmbeddedPayloadTotal();
    int         EmbeddedPayloadWireCount();
    int         EmbeddedMissingPayloadWireCount();
    const char* EmbeddedBindVersion();
    const char* EmbeddedBindChannel();
    const char* EmbeddedCatalogTimestamp();

    // ---------------- 装载 ----------------
    // 装载内嵌 catalog（重建映射 + 菜单列表 + 商店表 + 广播消息）。
    bool InstallEmbedded();
    // 装载任意来源的 catalog，先过格式闸。source 只用于日志与自检。
    bool InstallCatalog(const std::string& catalogJson, const char* source);

    // 返回副本：这两个值可能在登录时被改写，返回引用会留下悬垂风险。
    std::string CatalogSource();
    std::size_t CatalogLength();

    // ---------------- 本地 payload 回放 ----------------
    struct Entry {
        int         op = 0;
        std::string wire;
        std::string nonce;
        std::string hwid;
        std::string bindVersion;
        std::string bindChannel;
        std::string envelope;
    };

    bool HasPayload(const std::string& wire);
    int  PayloadCountForWire(const std::string& wire);
    // 同一 wire 有多条信封时按轮转索引依次取，避免反复使用同一个 nonce。
    bool TakePayload(const std::string& wire, Entry& out);

    // ---------------- 自检 ----------------
    std::string BuildSelfTestReport(const std::string& catalogSource);
    // 依次尝试 Documents、Application Support（DataDir）、tmp 写出。
    bool WriteSelfTest(const std::string& catalogSource);
    void SelfTest(const std::string& catalogSource);
}
