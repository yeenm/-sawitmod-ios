#pragma once

#include <string>
#include "Global.h"   // Supaya NetAuth kenal struct InvokeRequest & global lain
#include "Version.h"
#include "SawitFree.h"   // 免费功能清单（纯 inline，界面/门禁/测试共用）

// =========================================================
//  HASIL OTENTIKASI (v4 - ECDH Dynamic Key Protocol)
// =========================================================
struct AuthResult {
    bool ok       = false;   // server bilang sukses DAN tanda tangan valid
    bool kill     = false;   // server memerintahkan sesi diakhiri
    bool netError = false;   // server tidak terjangkau (JANGAN tutup aplikasi)
    bool tampered = false;   // tanda tangan tidak cocok -> server palsu / MITM
    std::string code;        // LICENSE_EXPIRED, VERSION_UNSUPPORTED, dst
    std::string message;     // pesan untuk ditampilkan ke user
    std::string raw;         // respons mentah, dipakai parsing fitur
    std::string data;        // blob terenkripsi yang sudah terikat signature
    std::string serverPubKey;// ephemeral public key dari server (128 hex)
    std::string requestNonce;// nonce yang dikirim
    std::string requestHwid; // hwid yang dikirim
    std::string decrypted;   // plaintext hasil dekripsi dinamis ECDH
};

// --- Utilitas ---
std::string GenerateNonceNative();
std::string GetHWIDNative();
std::string HttpPostNative(const std::string& urlStr, const std::string& jsonPayload);
std::string HmacSha256HexNative(const std::string& key, const std::string& data);
std::string Sha256HexNativePublic(const std::string& data);

// --- Interpretasi hasil ---
std::string AuthDisplayMessage(const AuthResult& r);
bool        ShouldQuitApp(const AuthResult& r);

// --- Penutup aplikasi ---
void QuitApp(const std::string& message);
void TriggerQuit(const std::string& code, const std::string& message);

// --- Pengecekan ulang berkala ---
void StartLicenseHeartbeat(const std::string& license);
void StopLicenseHeartbeat();

enum class StatusSesi : uint8_t { Keluar, Menghubungkan, Aktif, Tenggang, Dicabut };
bool     SesiAktif();
uint64_t GenerasiSesi();
bool     LisensiPermanen();
std::string TanggalKedaluwarsaLisensi();
bool     ValidasiOtentikasiKetat();
bool     PasangSesi(const std::string& jsonSesi, bool modeAvatar);
// 离线闸（SAWIT_OFFLINE_MODE=1）：安装一个本地会话，不请求服务器、不认卡密。
// 只有离线构建会用到它；正常构建里这个符号同样存在，但装了也会被后续闸门否掉。
bool     LoginOfflineSession(bool modeAvatar = false);
void     AkhiriSesiLokal(StatusSesi status = StatusSesi::Keluar);
bool     PerbaruiSesi(const std::string& license);
void     KeluarSesiServer(const std::string& license);

// --- Fungsi utama ---
AuthResult   DoVerifyRequest(const std::string& license);
void         VerifyLicenseJNI(const char* licenseKey);
InvokeRequest FetchPayloadSync(std::string featId);
void         FetchAndExecute(std::string featId, std::string customParam = "", std::string customInstance = "");

// --- Resolusi op-code <-> wire (rotasi payload) ---
std::string  WireForOp(int op);
int          OpForWire(const std::string& wire);
bool         HasWire(const std::string& wire);

// --- Penerapan katalog (offline dump / cloud) ---
// 把一份 catalog 一次性应用到运行时：op<->wire 映射、菜单 featureList、
// 商店表 (shopNames/shopIds)、广播消息。catalog 必须先过
// LocalCatalog::IsValidCatalog()，dump 快照格式（"wireId"）会被拒。
void         SawitApplyCatalog(const std::string& catalogJson);
