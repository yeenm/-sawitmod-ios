// ============================================================================
//  ModUtils.cpp — iOS 移植版（原 Android jni/ModUtils.cpp）
// ----------------------------------------------------------------------------
//  逻辑、函数名、签名、中文注释与 Android 版逐行对应，只换平台层：
//    * Java 侧能力（JavaVM / Context / Intent / 剪贴板） -> App/Platform.h
//    * BoringSSL 动态符号（原版按名字解析）             -> Core/Crypto.h 的 SMCrypto
//    * /proc/self/maps 找 libil2cpp.so 基址             -> Includes/Utils.h 的 dyld 镜像表
//    * Android 私有目录 / 外部存储路径                  -> SMPlatform::DataDir()/DocumentDir()
//    * mprotect -> vm_protect(mach_task_self(), ...)（iOS 要带 VM_PROT_COPY 走写时复制）
//  mono/il2cpp 侧完全不变：_monoXxx 全局指针仍在本文件定义（ModUtils.h 里 extern），
//  由 IL2CPP/MonoCompat.cpp 的 SMMono::InstallPointers() 在 il2cpp 就绪后填充。
// ============================================================================

#include "ModUtils.h"
#include "Global.h"
#include "NetAuth.h"   // LoginOfflineSession（离线闸），以及会话闸门声明
#include "Includes/obfuscate.h"
#include "VirboxTargets.h"
#include "PayloadVM.h"
#include "Includes/Logger.h"
#include "Version.h"       // SAWIT_OFFLINE_MODE（iOS 没有 ndk-build 的 -D 传递）
#include <cmath>
#include <dlfcn.h>
#include <cstring>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <mach/mach.h>      // vm_protect（替换 mprotect）
#include "App/Platform.h"   // SMPlatform::Clipboard/DataDir/DocumentDir/OpenURL
#include "Core/Crypto.h"    // SMCrypto::Ecdh*/HmacSha256Raw/Aes256GcmDecrypt
#include "Includes/Utils.h" // SMImage::FindImageBase（替换 /proc/self/maps）
#include "Starcoolxdl/xdl.h"

// --- Game Definitions ---


// =====================================================================
// DEFINISI VARIABEL MONO/IL2CPP (Wajib agar tidak Undefined Symbol)
// =====================================================================
mono_metadata_get_generic_inst_t _mono_metadata_get_generic_inst = nullptr;
mono_class_inflate_generic_method_t _mono_class_inflate_generic_method = nullptr;
mono_class_vtable_t _monoClassGetVTable = nullptr;
mono_field_static_get_value_t _monoFieldStaticGetValue = nullptr;
mono_field_static_set_value_t _monoFieldStaticSetValue = nullptr;
MonoClassGetFields_t _monoClassGetFields = nullptr;
MonoFieldName_t _monoFieldName = nullptr;
MonoFieldGetOffset_t _monoFieldGetOffset = nullptr;
MonoFieldGetType_t _monoFieldGetType = nullptr;
mono_class_value_size_t _monoClassValueSize = nullptr;
mono_class_get_parent_t _monoClassGetParent = nullptr;
mono_string_new_t _monoStringNew = nullptr;
mono_class_get_name_t _monoClassGetName = nullptr;
MonoMethodSignature_t _monoMethodSignature = nullptr;
MonoSignatureGetParams_t _monoSignatureGetParams = nullptr;
MonoClassFromMonoType_t _monoClassFromMonoType = nullptr;
MonoObjectNew_t _monoObjectNew = nullptr;
MonoDomainGet_t _monoDomainGet = nullptr;
MonoAssemblyOpen_t _monoAssemblyOpen = nullptr;
MonoAssemblyGetImage_t _monoAssemblyGetImage = nullptr;
MonoCompileMethod_t _monoCompileMethod = nullptr;
MonoGetRootDomain_t _monoGetRootDomain = nullptr;
MonoThreadAttach_t _monoThreadAttach = nullptr;
MonoImageLoaded_t _monoImageLoaded = nullptr;
MonoClassFromName_t _monoClassFromName = nullptr;
MonoClassGetMethodFromName_t _monoClassGetMethodFromName = nullptr;
MonoObjectGetClass_t _monoObjectGetClass = nullptr;
MonoClassGetFieldFromName_t _monoClassGetFieldFromName = nullptr;
MonoFieldGetValue_t _monoFieldGetValue = nullptr;
MonoFieldSetValue_t _monoFieldSetValue = nullptr;
MonoFieldGetValueObject_t _monoFieldGetValueObject = nullptr;
MonoRuntimeInvoke_t _monoRuntimeInvoke = nullptr;
MonoObjectUnbox_t _monoObjectUnbox = nullptr;
void *(*_monoClassGetType)(void *klass) = nullptr;
void *(*_monoTypeGetObject)(void *domain, void *type) = nullptr;

// =====================================================================
// FOLDER GAMBAR / INSTRUMENT / PHOTO (iOS)
// ---------------------------------------------------------------------
// Android 版放在外部存储的 app 目录 /files/<Sub>/ 下；iOS 没有外部存储，
// 统一落到应用沙盒的 Documents 下（可用"文件"App 访问，也能被
// iTunes/访达 拖放）。g_availableImages / g_drawingFolderPath /
// g_instrumentFiles / g_instrumentFolderPath / g_photoFiles / g_photoFolderPath
// 的**定义**在 Core/State.cpp（iOS 把全局变量集中在那边），这里只在运行时赋值：
// 静态初始化顺序跨编译单元不确定，所以用 std::call_once 惰性赋值，不能写成
// 文件作用域的 `std::string x = ...` 初始化式。
static std::string SMDocPath(const char* sub) {
    std::string path = SMPlatform::DocumentDir();
    if (!path.empty() && path.back() != '/')
        path += "/";
    path += sub;
    path += "/";
    return path;
}

// 三条目录路径只在第一次用到时填一次（幂等：外部若已赋值就不覆盖）。
static void SMDocPathsEnsure() {
    static std::once_flag once;
    std::call_once(once, []() {
        if (g_drawingFolderPath.empty())
            g_drawingFolderPath = SMDocPath("Drawings");      // Auto Draw
        if (g_instrumentFolderPath.empty())
            g_instrumentFolderPath = SMDocPath("Instrument"); // Music Player 的 .bin
        if (g_photoFolderPath.empty())
            g_photoFolderPath = SMDocPath("Photos");          // Import Picture / Avatar
    });
}

void ScanInstrumentFolder() {
    SMDocPathsEnsure();
    g_instrumentFiles.clear();
    mkdir(g_instrumentFolderPath.c_str(), 0777);
    DIR *dir = opendir(g_instrumentFolderPath.c_str());
    if (dir != nullptr) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr) {
            std::string fileName = ent->d_name;
            if (fileName.length() >= 4) {
                std::string ext = fileName.substr(fileName.length() - 4);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == (const char *) OBFUSCATE(".bin")) {
                    g_instrumentFiles.push_back(fileName);
                }
            }
        }
        closedir(dir);
    }
}

void ScanPhotoFolder() {
    SMDocPathsEnsure();
    g_photoFiles.clear();
    mkdir(g_photoFolderPath.c_str(), 0777);
    DIR *dir = opendir(g_photoFolderPath.c_str());
    if (dir != nullptr) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr) {
            std::string fileName = ent->d_name;
            if (fileName.length() >= 4) {
                std::string ext = fileName.substr(fileName.length() - 4);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == (const char *) OBFUSCATE(".jpg") ||
                    ext == (const char *) OBFUSCATE(".png")) {
                    g_photoFiles.push_back(fileName);
                }
            }
        }
        closedir(dir);
    }
}
std::vector<uint8_t> hexToBytes(const std::string &hex) {
    std::vector<uint8_t> bytes;
    for (unsigned int i = 0; i < hex.length(); i += 2) {
        bytes.push_back((uint8_t) strtol(hex.substr(i, 2).c_str(), nullptr, 16));
    }
    return bytes;
}
std::string GetAndroidClipboard() {
    // iOS 没有 ClipboardManager：直接读 UIPasteboard（App/Platform.mm 的
    // SMPlatform::Clipboard）。函数名保持 Android 版，Menu 里的"粘贴卡密"
    // 调用点不用改。空串语义与 Android 版一致（剪贴板里没有文本）。
    return SMPlatform::Clipboard();
}
void unpad_pkcs7(std::string &s) {
    if (s.empty())
        return;
    unsigned char pad = (unsigned char) s.back();
    if (pad > 0 && pad <= 16 && s.length() >= pad)
        s.erase(s.length() - pad);
}

std::string extractJsonString(const std::string &json, const std::string &key) {
    // Handler fitur lama memanggil helper ini untuk names map. PayloadVM v2
    // menyimpannya sebagai metadata SVMN, sedangkan auth/catalog tetap JSON.
    if (json.size() >= 4 && json.compare(0, 4, "SVMN") == 0)
        return PayloadVM::MetadataValue(json, key);

    std::string searchKey =
            (const char *) OBFUSCATE("\"") + key + (const char *) OBFUSCATE("\":\"");
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        std::string searchNoQuote =
                (const char *) OBFUSCATE("\"") + key + (const char *) OBFUSCATE("\":");
        size_t pos2 = json.find(searchNoQuote);
        if (pos2 == std::string::npos)
            return "";
        pos2 += searchNoQuote.length();
        size_t endPos2 = json.find_first_of((const char *) OBFUSCATE(",}"), pos2);

        return json.substr(pos2, endPos2 - pos2);
    }
    pos += searchKey.length();
    size_t endPos = json.find((const char *) OBFUSCATE("\""), pos);
    return (endPos == std::string::npos) ? "" : json.substr(pos, endPos - pos);
}

// =====================================================================
// ECDH (NIST P-256) + HKDF-SHA256 + AES-256-GCM DYNAMIC SESSION DECRYPTION
// ---------------------------------------------------------------------
// iOS：不再按名字从系统加密库里解析那批 BoringSSL 符号（iOS 上没有 OpenSSL），
// 改用 Core/Crypto.h 的 SMCrypto：
//   EcdhGenerateEphemeral / EcdhComputeShared / EcdhFree -> CryptoApple.mm
//                                                          （Security.framework）
//   HmacSha256Raw / Aes256GcmDecrypt                     -> Crypto.cpp（纯 C++）
// 协议逐字节不变：iv:tag:cipher 的十六进制拆分、iv=12 / tag=16、
// PRK = HMAC(salt = nonce + "|" + hwid, IKM = shared)、
// key = HMAC(PRK, "SawitMod-Session-v4" + 0x01)、AES-256-GCM 无 AAD。
// =====================================================================

void* GenerateEphemeralEcdhKey(std::string& outClientPubKeyHex) {
    outClientPubKeyHex.clear();
    // 生成 P-256 临时密钥对；返回值是不透明句柄（内部是 SecKeyRef，带 +1 引用），
    // 公钥以 128 位十六进制 X||Y 返回，与 Android 版格式一致。
    void* key = SMCrypto::EcdhGenerateEphemeral(outClientPubKeyHex);
    if (!key) {
        outClientPubKeyHex.clear();
        return nullptr;
    }
    return key;
}

void FreeEphemeralEcdhKey(void* ecKey) {
    if (!ecKey) return;
    // 对 nullptr 安全（上面已提前返回）；等价 Android 版的临时私钥释放。
    SMCrypto::EcdhFree(ecKey);
}

std::string DecryptDynamicPayload(const std::string& encryptedData,
                                  void* clientPrivKey,
                                  const std::string& serverPubKeyHex,
                                  const std::string& nonce,
                                  const std::string& hwid) {
    if (encryptedData.empty() || !clientPrivKey || serverPubKeyHex.size() != 128) {
        if (clientPrivKey) FreeEphemeralEcdhKey(clientPrivKey);
        return "";
    }

    size_t p1 = encryptedData.find(':');
    if (p1 == std::string::npos) { FreeEphemeralEcdhKey(clientPrivKey); return ""; }
    size_t p2 = encryptedData.find(':', p1 + 1);
    if (p2 == std::string::npos) { FreeEphemeralEcdhKey(clientPrivKey); return ""; }

    std::vector<uint8_t> iv     = hexToBytes(encryptedData.substr(0, p1));
    std::vector<uint8_t> tag    = hexToBytes(encryptedData.substr(p1 + 1, p2 - p1 - 1));
    std::vector<uint8_t> cipher = hexToBytes(encryptedData.substr(p2 + 1));
    if (iv.size() != 12 || tag.size() != 16 || cipher.empty()) {
        FreeEphemeralEcdhKey(clientPrivKey);
        return "";
    }

    // 服务器公钥：X||Y 各 32 字节（128 hex）。
    std::vector<uint8_t> sPubBytes = hexToBytes(serverPubKeyHex);
    if (sPubBytes.size() != 64) { FreeEphemeralEcdhKey(clientPrivKey); return ""; }

    // P-256 ECDH：共享密钥取 X 坐标（32 字节），与 OpenSSL 的
    // ECDH_compute_key 默认输出一致。临时私钥在这里释放，且只释放一次。
    unsigned char sharedSecret[32] = {0};
    bool sharedOk = SMCrypto::EcdhComputeShared(clientPrivKey, serverPubKeyHex, sharedSecret);
    FreeEphemeralEcdhKey(clientPrivKey);

    if (!sharedOk) {
        memset(sharedSecret, 0, sizeof(sharedSecret));
        return "";
    }

    // Derivasi HKDF-SHA256:
    // PRK = HMAC-SHA256(salt = nonce + "|" + hwid, IKM = sharedSecret)
    std::string salt = nonce + "|" + hwid;
    unsigned char prk[32] = {0};
    bool prkOk = SMCrypto::HmacSha256Raw(salt, sharedSecret, sizeof(sharedSecret), prk);

    // SessionKey = HMAC-SHA256(PRK, info + "\x01")
    std::string info = "SawitMod-Session-v4";
    info.push_back('\x01');
    unsigned char sessionKey[32] = {0};
    bool keyOk = prkOk && SMCrypto::HmacSha256Raw(std::string((const char*)prk, sizeof(prk)),
                                                  (const unsigned char*)info.data(),
                                                  info.size(), sessionKey);

    // Hapus sharedSecret & PRK dari memori
    volatile unsigned char* pSecret = sharedSecret;
    for (size_t i = 0; i < sizeof(sharedSecret); ++i) pSecret[i] = 0;
    volatile unsigned char* pPrk = prk;
    for (size_t i = 0; i < sizeof(prk); ++i) pPrk[i] = 0;

    if (!keyOk) {
        memset(sessionKey, 0, sizeof(sessionKey));
        return "";
    }

    // AES-256-GCM 解密（iv 12 字节、tag 16 字节、无 AAD）；tag 校验失败返回 false。
    std::vector<uint8_t> plain;
    bool ok = SMCrypto::Aes256GcmDecrypt(sessionKey, iv.data(), iv.size(),
                                         cipher.data(), cipher.size(),
                                         tag.data(), tag.size(), plain);

    // Hapus sessionKey dari memori segera setelah dekripsi selesai
    volatile unsigned char* pKey = sessionKey;
    for (size_t i = 0; i < sizeof(sessionKey); ++i) pKey[i] = 0;

    std::string result;
    if (ok) result.assign((const char*)plain.data(), plain.size());

    // plain 只是临时缓冲，拷进 result 后清掉
    if (!plain.empty()) {
        volatile unsigned char* pPlain = plain.data();
        for (size_t i = 0; i < plain.size(); ++i) pPlain[i] = 0;
    }
    return result;
}

extern "C" VIRBOX_TARGET_ATTR std::string Virbox_DecryptPayload(std::string encryptedData) {
    (void)encryptedData;
    return "";
}

std::string DecryptPayload(std::string encryptedData) {
    return Virbox_DecryptPayload(std::move(encryptedData));
}


// Definisikan path absolut yang aman
// iOS：没有 Android 的 app 私有 files 目录，授权文件落到应用可写目录
// （Library/Application Support/SawitMod/license.txt，见 App/Platform.mm）。
// Android 版是宏拼接（包的名字 + /files/license.txt），这里改成函数，
// 不再依赖包名宏（那个宏在 iOS 上没有意义）。
static std::string SMLicensePath() {
    return SMPlatform::DataDir() + "/license.txt";
}

void SaveLicenseLocally(const std::string &key) {
    const std::string licensePath = SMLicensePath();
    std::ofstream file(licensePath);
    if (file.is_open()) {
        file << key;
        file.close();
        LOGI(OBFUSCATE("[LICENSE] Berhasil menyimpan: %s"), key.c_str());
    } else {
        LOGE(OBFUSCATE("[LICENSE] GAGAL menyimpan! Periksa path: %s"), licensePath.c_str());
    }
}

void LoadLicenseLocally() {
    SMDocPathsEnsure();   // 顺带把三个素材目录填好（菜单里有"复制路径"入口）
#if SAWIT_OFFLINE_MODE
    // 卡密验证已关闭（离线闸）：启动即装本地会话，不用输入卡密也不用登录。
    // 仍然会把之前存过的卡密读进输入框，方便对照；菜单只看会话，不看这个串。
    LoginOfflineSession();
    std::ifstream offlineFile(SMLicensePath());
    if (offlineFile.is_open()) {
        std::string savedKey;
        std::getline(offlineFile, savedKey);
        if (!savedKey.empty() && savedKey.length() < 64) {
            strncpy(license_input, savedKey.c_str(), sizeof(license_input) - 1);
        }
        offlineFile.close();
    }
    LOGI(OBFUSCATE("[LICENSE] offline build: card key verification disabled"));
    return;
#endif
    std::ifstream file(SMLicensePath());
    if (file.is_open()) {
        std::string savedKey;
        std::getline(file, savedKey);
        if (!savedKey.empty() && savedKey.length() < 64) {
            strncpy(license_input, savedKey.c_str(), sizeof(license_input) - 1);
            LOGI(OBFUSCATE("[LICENSE] Berhasil memuat: %s"), savedKey.c_str());
        }
        file.close();
    } else {
        LOGI(OBFUSCATE("[LICENSE] File tidak ditemukan, load dilewati."));
    }
}

// ====== MANUAL TELEPORTATION ======
// g_savePoints 的定义在 Core/State.cpp（iOS 把全局变量集中到那边，State.h 里
// 是 extern），这里只保留读写逻辑，不能再重复定义一次。
// 原 Android：路径与 license 同目录（app 私有 files 目录）
// iOS：应用可写目录下的 savepoints.txt。
std::string GetSavePointPath() {
    return SMPlatform::DataDir() + "/savepoints.txt";
}

void SaveSavePoints() {
    std::ofstream file(GetSavePointPath(), std::ios::trunc);
    if (file.is_open()) {
        for (const auto& p : g_savePoints)
            file << p.x << " " << p.y << " " << p.z << "\n";
        file.close();
    }
}

void LoadSavePoints() {
    g_savePoints.clear();
    std::ifstream file(GetSavePointPath());
    if (file.is_open()) {
        Vector3 p;
        while (file >> p.x >> p.y >> p.z)
            g_savePoints.push_back(p);
        file.close();
    }
}

std::vector<uint8_t> parseHex(std::string hex) {
    std::vector<uint8_t> bytes;
    std::stringstream ss(hex);
    std::string item;
    while (std::getline(ss, item, ' ')) {
        if (!item.empty())
            bytes.push_back((uint8_t) std::stoul(item, nullptr, 16));
    }
    return bytes;
}
uintptr_t getExecutableBaseAddress(const char *libName) {
    // iOS 没有 /proc/self/maps：走 dyld 镜像表（Includes/Utils.h 的 SMImage）。
    // 游戏代码在 UnityFramework / 主可执行文件里，先按 targetLibName 找；
    // FindImageBase 拿不到时回落到调用方传进来的名字（旧调用点里还是
    // "libil2cpp.so" 这类 Android 名字，不用逐个改）。都没有返回 0，
    // 调用方会把 0 偏移当"未标定"跳过。
    uintptr_t base = SMImage::FindImageBase(targetLibName);   // "UnityFramework"
    if (base == 0 && libName && *libName)
        base = SMImage::FindImageBase(libName);
    return base;
}

// ============================================================================
//  iOS 追加：镜像基址与路径（ModUtils.h 里声明的两个入口）
// ============================================================================
uintptr_t SMGetGameImageBase() {
    // 等价 Android 的 il2cppBase：Unity 代码所在镜像的头地址。
    return SMImage::FindImageBase(targetLibName);
}

std::string SMAppDataDir() {
    // 等价 Android 的 app 私有 files 目录：License、导出文件都写这里。
    return SMPlatform::DataDir();
}

bool VirtualProtect(void *address, size_t size) {
    if (!address || size == 0)
        return false;
    // iOS 上没有 mprotect(RWX) 这条路：用 vm_protect + VM_PROT_COPY，
    // 让内核给 __TEXT 做写时复制（原始映射仍受代码签名约束，改的是私有副本）。
    size_t pageSize = sysconf(_SC_PAGESIZE);
    uintptr_t start = (uintptr_t) address;
    uintptr_t pageStart = start & ~(pageSize - 1);
    size_t protectSize = (start + size) - pageStart;
    kern_return_t kr = vm_protect(mach_task_self(),
                                  (vm_address_t) pageStart,
                                  (vm_size_t) protectSize,
                                  false,
                                  VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_COPY);
    return kr == KERN_SUCCESS;
}



void *FindMethodInHierarchy(void *klass, const char *name, int paramCount) {
    void *currentKlass = klass;
    while (currentKlass != nullptr) {
        void *method = _monoClassGetMethodFromName(currentKlass, name, paramCount);
        if (method)
            return method;
        currentKlass = _monoClassGetParent(currentKlass);
    }
    return nullptr;
}

uint32_t NativeGetNetId(void *obj) {
    if (!obj || !_monoObjectGetClass)
        return 0;
    void *klass = _monoObjectGetClass(obj);
    std::vector<const char *> idMethods = {OBFUSCATE("get_netId"), OBFUSCATE("GetNetId"),
                                           OBFUSCATE("get_Id"), OBFUSCATE("GetId")};
    for (const char *m: idMethods) {
        void *meth = FindMethodInHierarchy(klass, m, 0);
        if (meth) {
            void *exc = nullptr;
            void *res = _monoRuntimeInvoke(meth, obj, nullptr, &exc);
            if (res && !exc)
                return *(uint32_t *) _monoObjectUnbox(res);
        }
    }

    std::vector<const char *> idFields = {OBFUSCATE("netId"), OBFUSCATE("NetId"), OBFUSCATE("id"),
                                          OBFUSCATE("mNetId"), OBFUSCATE("_netId"),
                                          OBFUSCATE("<netId>k__BackingField")};
    for (const char *f: idFields) {
        void *field = _monoClassGetFieldFromName(klass, f);
        if (field) {
            uint32_t val = 0;
            _monoFieldGetValue(obj, field, &val);
            if (val != 0)
                return val;
        }
    }
    return 0;
}

void *NativeGetObj(void *obj, std::vector<const char *> methodNames, std::vector<const char *> fieldNames) {
    if (!obj || !_monoObjectGetClass)
        return nullptr;
    void *klass = _monoObjectGetClass(obj);
    if (!klass)
        return nullptr;

    for (const char *mName: methodNames) {
        void *method = FindMethodInHierarchy(klass, mName, 0);
        if (method) {
            void *exc = nullptr;
            void *res = _monoRuntimeInvoke(method, obj, nullptr, &exc);
            if (res && !exc)
                return res;
        }
    }

    for (const char *fName: fieldNames) {
        void *field = _monoClassGetFieldFromName(klass, fName);
        if (field) {
            void *resPtr = nullptr;
            _monoFieldGetValue(obj, field, &resPtr);
            if (resPtr)
                return resPtr;
        }
    }
    return nullptr;
}

Vector3 NativeGetPosition(void *obj) {
    if (!obj || !_monoObjectGetClass)
        return {0, 0, 0};
    void *entity = NativeGetObj(obj, {OBFUSCATE("get_entity"), OBFUSCATE("GetEntity")},
                                {OBFUSCATE("entity"), OBFUSCATE("_entity"), OBFUSCATE("m_Entity")});
    void *target = entity ? entity : obj;
    void *klass = _monoObjectGetClass(target);

    void *getPosMeth = FindMethodInHierarchy(klass, OBFUSCATE("get_position"), 0);
    if (getPosMeth) {
        void *exc = nullptr;
        void *posObj = _monoRuntimeInvoke(getPosMeth, target, nullptr, &exc);
        if (posObj && !exc)
            return *(Vector3 *) _monoObjectUnbox(posObj);
    }

    void *mGetPose = FindMethodInHierarchy(klass, OBFUSCATE("GetInteractPose"), 2);
    if (mGetPose) {
        Vector3 pos = {0, 0, 0};
        float dummyRot[4] = {0, 0, 0, 0};
        void *args[2] = {&pos, &dummyRot};
        _monoRuntimeInvoke(mGetPose, target, args, nullptr);
        if (pos.x != 0 || pos.y != 0)
            return pos;
    }

    void *transform = NativeGetObj(target, {OBFUSCATE("get_transform"), OBFUSCATE("get_Transform")},
                                   {OBFUSCATE("transform"), OBFUSCATE("_transform")});
    if (transform) {
        void *tKlass = _monoObjectGetClass(transform);
        void *getPos = FindMethodInHierarchy(tKlass, OBFUSCATE("get_position"), 0);
        if (getPos) {
            void *posObj = _monoRuntimeInvoke(getPos, transform, nullptr, nullptr);
            if (posObj)
                return *(Vector3 *) _monoObjectUnbox(posObj);
        }
    }
    return {0, 0, 0};
}

std::string ReadMonoStringNative(void *stringPtr) {
    if (!stringPtr)
        return OBFUSCATE("NULL");

    int len = *(int *) ((uint8_t *) stringPtr + OFF_MONO_OBJ_HEADER);
    if (len < 0 || len > 1000)
        return OBFUSCATE("Invalid_String");

    char16_t *chars = (char16_t *) ((uint8_t *) stringPtr + 0x14);
    std::string result;
    // Konversi UTF-16 ke UTF-8
    for (int i = 0; i < len; i++) {
        uint16_t c = chars[i];
        if (c < 0x0080) {
            // Karakter ASCII biasa (1 byte)
            result += (char) c;
        } else if (c < 0x0800) {
            // Karakter 2 byte
            result += (char) (0xC0 | (c >> 6));
            result += (char) (0x80 | (c & 0x3F));
        } else if (c >= 0xD800 && c < 0xDC00 && i + 1 < len) {
            // Surrogate pair (Karakter khusus / Emoji 4 byte)
            uint16_t c2 = chars[++i];
            uint32_t codepoint = 0x10000 + (((c & 0x03FF) << 10) | (c2 & 0x03FF));
            result += (char) (0xF0 | (codepoint >> 18));
            result += (char) (0x80 | ((codepoint >> 12) & 0x3F));
            result += (char) (0x80 | ((codepoint >> 6) & 0x3F));
            result += (char) (0x80 | (codepoint & 0x3F));
        } else {
            // Karakter Mandarin dan huruf kompleks lainnya (3 byte)
            result += (char) (0xE0 | (c >> 12));
            result += (char) (0x80 | ((c >> 6) & 0x3F));
            result += (char) (0x80 | (c & 0x3F));
        }
    }
    return result;
}

float GetVector3Distance(Vector3 a, Vector3 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ID render/Unity thread (di-set tiap frame oleh GameApp_Update di Main.cpp).
// Dipakai untuk mendeteksi pemanggilan Sync DARI render thread (self-deadlock).
std::atomic<std::thread::id> g_unityThreadId{};

// State sinkronisasi dialokasikan di HEAP (shared_ptr) supaya lambda antrean
// tidak pernah menyentuh stack pemanggil yang sudah mati bila kita keburu timeout.
//
// ⚠️ 旧版这里只用了两个 atomic（done / abandoned），存在一个"放弃与开跑同时发生"
// 的窗口：
//     pump 读到 abandoned==false → 调用方超时置 abandoned=true → pump 再执行 task
// → task 里的 [&] 捕获全部指向已经返回的调用方栈帧 = use-after-scope，
//   表现为随机 SIGSEGV（PC 跳到一个非映射地址、没有有效 backtrace）。
// 采集这类"每 0.4 秒一次同步 mono 调用 + 频繁传送导致 pump 停顿"的场景最容易踩到。
// 现在用 mutex + condvar 把"是否已开始/是否被放弃"的决定做成原子的：
//   * 任务开始前 pump 必须先在锁内把 started 置 true；已被放弃则直接跳过；
//   * 调用方超时时：没开始 → 置 abandoned（pump 会跳过）；已经开始 → 必须等它跑完，
//     因为此时 task 正在使用调用方的栈，提前返回就会崩。
struct UnitySyncState {
    std::mutex m;
    std::condition_variable cv;
    bool started = false;
    bool done = false;
    bool abandoned = false;
};

void RunOnUnityThreadSync(std::function<void()> task) {
    // (1) Kalau dipanggil DARI render thread sendiri, jalankan langsung.
    //     Mengantre + menunggu di sini = deadlock diri sendiri (freeze seketika).
    if (std::this_thread::get_id() == g_unityThreadId.load()) {
        try { task(); } catch (...) {}
        return;
    }

    auto st = std::make_shared<UnitySyncState>();
    {
        std::lock_guard<std::mutex> lock(g_unityTaskMutex);
        g_unityTasks.push_back([task, st]() mutable {
            {
                std::lock_guard<std::mutex> stateLock(st->m);
                // 调用方已经放弃：跳过，绝不能碰它的 [&] 变量。
                if (st->abandoned) return;
                st->started = true;
            }
            try { task(); } catch (...) {}
            {
                std::lock_guard<std::mutex> stateLock(st->m);
                st->done = true;
            }
            st->cv.notify_all();
        });
    }

    // (2) Timeout pengaman: kalau render thread berhenti memompa antrian
    //     (mis. saat loading screen / ganti scene di 100%), jangan menggantung selamanya.
    //     但一旦任务已经开始跑，就必须等它结束（否则调用方栈先散 = use-after-scope）。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    std::unique_lock<std::mutex> stateLock(st->m);
    while (!st->done) {
        if (st->cv.wait_until(stateLock, deadline) == std::cv_status::no_timeout) continue;
        if (st->done) break;
        if (!st->started) {
            st->abandoned = true;   // 还没开跑，安全放弃
            return;
        }
        // 已经开跑：无限等待，保证 task 使用期间调用方栈帧仍然有效
        st->cv.wait(stateLock, [&st]() { return st->done; });
    }
}

void RunOnUnityThreadAsync(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(g_unityTaskMutex);
    g_unityTasks.push_back(task);
}

void *GetManagerInstanceNative(const char *targetManagerName) {
    void *imgFramework = _monoImageLoaded(OBFUSCATE("XDTBaseService"));
    if (!imgFramework)
        imgFramework = _monoImageLoaded(OBFUSCATE("XDTGame.Framework"));
    if (!imgFramework)
        return nullptr;

    void *klassManagers = _monoClassFromName(imgFramework, OBFUSCATE("XDTGame.Framework"),
                                             OBFUSCATE("Managers"));
    if (!klassManagers)
        return nullptr;

    void *vtable = _monoClassGetVTable(_monoGetRootDomain(), klassManagers);
    if (!vtable)
        return nullptr;

    const char *dictNames[] = {OBFUSCATE("_serviceDic"), OBFUSCATE("_moduleDic"),
                               OBFUSCATE("serviceDic"), OBFUSCATE("moduleDic")};
    for (const char *dictName: dictNames) {
        void *f_dict = _monoClassGetFieldFromName(klassManagers, dictName);
        if (f_dict) {
            void *dictPtr = nullptr;
            _monoFieldStaticGetValue(vtable, f_dict, &dictPtr);

            if (dictPtr) {
                void *dictKlass = _monoObjectGetClass(dictPtr);
                void *f_entries = _monoClassGetFieldFromName(dictKlass, OBFUSCATE("_entries"));

                if (f_entries) {
                    void *entriesArray = nullptr;
                    _monoFieldGetValue(dictPtr, f_entries, &entriesArray);

                    if (entriesArray) {
                        int arrayCap = *(int *) ((uint8_t *) entriesArray + OFF_ARR_COUNT);
                        uint8_t *baseEntryPtr = (uint8_t *) entriesArray + OFF_ARR_DATA;

                        for (int i = 0; i < arrayCap; i++) {
                            int hash = *(int *) (baseEntryPtr + (i * SZ_DICT_ENTRY));
                            if (hash >= 0) {
                                void *wrapperObj = *(void **) (baseEntryPtr + (i * SZ_DICT_ENTRY) + OFF_ENTRY_VALUE);
                                if (wrapperObj) {
                                    // Ekstrak objek manager dari dalam wrapper
                                    void *targetObj = *(void **) ((uint8_t *) wrapperObj + OFF_MONO_OBJ_HEADER);
                                    if (!targetObj)
                                        targetObj = *(void **) wrapperObj; // Fallback

                                    if (targetObj) {
                                        const char *name = _monoClassGetName(
                                                _monoObjectGetClass(targetObj));
                                        if (name && strcmp(name, targetManagerName) == 0) {
                                            return targetObj;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}

void GetPlayerNetIdOnUnityThread() {
    _monoThreadAttach(_monoGetRootDomain());
    void *img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!img) img = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (!img) return;

    void *klass = _monoClassFromName(img, OBFUSCATE("XDTDataAndProtocol"),
                                     OBFUSCATE("PlayerDataCenter"));
    if (!klass) klass = _monoClassFromName(img, "", OBFUSCATE("PlayerDataCenter"));
    if (!klass) return;

    void *method = FindMethodInHierarchy(klass, OBFUSCATE("GetSelfNetPlayerId"), 0);
    if (!method) return;

    void *exc = nullptr;
    void *res = _monoRuntimeInvoke(method, nullptr, nullptr, &exc);
    if (res && !exc) {
        g_gardenPlayerNetId = *(uint32_t *) _monoObjectUnbox(res);
    }
}

void GetPlayerNetIdNative() {
    RunOnUnityThreadSync([]() {
        GetPlayerNetIdOnUnityThread();
    });
}

void SafeBotSleep(float seconds) {
    int cycles = (int) (seconds * 10.0f);
    for (int i = 0; i < cycles; i++) {
        if (!g_isGardenBotRunning)
            break;
        usleep(100000);
    }
}

void *GetConfigManagerInstance() {
    // Jika sudah pernah dapat, langsung kembalikan. Jangan muter-muter nyari lagi!
    if (g_cachedConfigManager != nullptr) {
        return g_cachedConfigManager;
    }

    void *imgFramework = _monoImageLoaded(OBFUSCATE("XDTBaseService"));
    if (!imgFramework)
        imgFramework = _monoImageLoaded(OBFUSCATE("XDTGame.Framework"));
    if (!imgFramework)
        return nullptr;

    void *klassManagers = _monoClassFromName(imgFramework, OBFUSCATE("XDTGame.Framework"),
                                             OBFUSCATE("Managers"));
    if (!klassManagers)
        return nullptr;

    // 1. Panggil GetAllManagers() -> Mengembalikan IEnumerable<IManager>
    void *m_getAll = FindMethodInHierarchy(klassManagers, OBFUSCATE("GetAllManagers"), 0);
    if (!m_getAll)
        return nullptr;

    void *exc = nullptr;
    void *ienumerableObj = _monoRuntimeInvoke(m_getAll, nullptr, nullptr, &exc);
    if (!ienumerableObj || exc)
        return nullptr;

    // 2. Panggil GetEnumerator() -> Mengembalikan IEnumerator<IManager>
    void *enumerableKlass = _monoObjectGetClass(ienumerableObj);

    // Trik nama eksplisit C# di metadata Mono
    void *m_getEnum = FindMethodInHierarchy(enumerableKlass, OBFUSCATE(
            "System.Collections.Generic.IEnumerable<XDTGame.Core.IManager>.GetEnumerator"), 0);
    if (!m_getEnum)
        m_getEnum = FindMethodInHierarchy(enumerableKlass, OBFUSCATE("GetEnumerator"),
                                          0); // Fallback

    void *enumeratorObj = _monoRuntimeInvoke(m_getEnum, ienumerableObj, nullptr, &exc);
    if (!enumeratorObj || exc)
        enumeratorObj = ienumerableObj; // Fallback

    // 3. Persiapkan method MoveNext() dan get_Current()
    void *enumeratorKlass = _monoObjectGetClass(enumeratorObj);
    void *m_moveNext = FindMethodInHierarchy(enumeratorKlass, OBFUSCATE("MoveNext"), 0);
    void *m_current = FindMethodInHierarchy(enumeratorKlass, OBFUSCATE(
            "System.Collections.Generic.IEnumerator<XDTGame.Core.IManager>.get_Current"), 0);
    if (!m_current)
        m_current = FindMethodInHierarchy(enumeratorKlass, OBFUSCATE("get_Current"), 0); // Fallback

    if (!m_moveNext || !m_current) {
        return nullptr;
    }

    // 4. Looping layaknya foreach di C# !
    while (true) {
        void *exc2 = nullptr;

        // Call MoveNext()
        void *moveNextRes = _monoRuntimeInvoke(m_moveNext, enumeratorObj, nullptr, &exc2);
        if (!moveNextRes || exc2)
            break;

        // Unbox hasil boolean dari MoveNext
        bool hasNext = *(bool *) _monoObjectUnbox(moveNextRes);
        if (!hasNext)
            break; // Jika false, iterasi selesai

        // Call get_Current()
        void *currentManager = _monoRuntimeInvoke(m_current, enumeratorObj, nullptr, &exc2);
        if (currentManager) {
            void *mgrKlass = _monoObjectGetClass(currentManager);
            const char *namePtr = _monoClassGetName(mgrKlass);

            if (namePtr) {
                std::string mgrName(namePtr);
                if (mgrName == (const char *) OBFUSCATE("ConfigManager")) {
                    g_cachedConfigManager = currentManager;
                    return g_cachedConfigManager;
                }
            }
        }
    }
    return nullptr;
}

void *GetLevelScriptableConfigInstanceNative() {
    // Jika sudah pernah didapat, jangan buang CPU untuk mencari lagi
    if (g_cachedLevelScriptableConfig != nullptr)
        return g_cachedLevelScriptableConfig;

    void *imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel)
        return nullptr;

    void *klassTarget = _monoClassFromName(imgLevel,
                                           OBFUSCATE("ScriptsRefactory.LevelAndEntity.Utils"),
                                           OBFUSCATE("LevelScriptableConfig"));
    if (!klassTarget) {
        LOGE(OBFUSCATE("[-] Class LevelScriptableConfig tidak ditemukan!"));
        return nullptr;
    }

    // ⚡ TRIK NINJA: Ambil Class Bapaknya!
    void *klassParent = _monoClassGetParent(klassTarget);
    if (!klassParent)
        return nullptr;

    void *domain = _monoGetRootDomain();
    void *vtable = _monoClassGetVTable(domain, klassParent);
    if (!vtable)
        return nullptr;

    // Cari field Instance yang otomatis digenerate C#
    const char *possibleNames[] = {OBFUSCATE("<Instance>k__BackingField"), OBFUSCATE("instance"),
                                   OBFUSCATE("_instance"), OBFUSCATE("Instance")};
    void *targetField = nullptr;

    for (int i = 0; i < 4; i++) {
        targetField = _monoClassGetFieldFromName(klassParent, possibleNames[i]);
        if (targetField)
            break;
    }

    if (targetField) {
        void *targetInst = nullptr;
        _monoFieldStaticGetValue(vtable, targetField, &targetInst); // Sedot via VTable

        if (targetInst) {
            LOGI(OBFUSCATE("[+] JACKPOT! LevelScriptableConfig Ditemukan di RAM: %p"), targetInst);
            g_cachedLevelScriptableConfig = targetInst;
            return targetInst;
        }
    }

    // Fallback: Jika static field gagal ditarik, coba paksa lewat property
    void *getInstMeth = FindMethodInHierarchy(klassParent, OBFUSCATE("get_Instance"), 0);
    if (getInstMeth) {
        void *exc = nullptr;
        void *targetInst = _monoRuntimeInvoke(getInstMeth, nullptr, nullptr, &exc);
        if (targetInst && !exc) {
            LOGI(OBFUSCATE("[+] LevelScriptableConfig Ditemukan via Property: %p"), targetInst);
            g_cachedLevelScriptableConfig = targetInst;
            return targetInst;
        }
    }

    LOGE(OBFUSCATE("[-] Gagal mendapatkan Instance LevelScriptableConfig."));
    return nullptr;
}

void *GetPlayerMotionConfigNative() {
    if (g_cachedPlayerMotionConfig != nullptr)
        return g_cachedPlayerMotionConfig;

    void *levelConfig = GetLevelScriptableConfigInstanceNative();
    if (!levelConfig)
        return nullptr;

    void *klass = _monoObjectGetClass(levelConfig);
    void *motionField = _monoClassGetFieldFromName(klass, OBFUSCATE("playerMotion"));
    if (!motionField)
        return nullptr;

    void *motionConfig = nullptr;
    _monoFieldGetValue(levelConfig, motionField, &motionConfig);

    if (motionConfig) {
        LOGI(OBFUSCATE("[+] Player MotionConfig Ditemukan di RAM: %p"), motionConfig);
        g_cachedPlayerMotionConfig = motionConfig;
    }
    return motionConfig;
}

void ScanImagesInFolder() {
    SMDocPathsEnsure();   // iOS：目录路径在运行时填充（变量定义在 Core/State.cpp）
    g_availableImages.clear();

    // Otomatis bikin folder jika belum ada
    // （iOS 沙盒：Documents/Drawings/ 不存在时由 mkdir 创建）
    mkdir(g_drawingFolderPath.c_str(), 0777);

    DIR *dir = opendir(g_drawingFolderPath.c_str());
    if (dir != nullptr) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr) {
            std::string fileName = ent->d_name;
            if (fileName.length() >= 4) {
                std::string ext = fileName.substr(fileName.length() - 4);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == (const char *) OBFUSCATE(".jpg") ||
                    ext == (const char *) OBFUSCATE(".png")) {
                    g_availableImages.push_back(fileName);
                }
            }
        }
        closedir(dir);
    }
}

void *GetDrawingConfigInstanceNative() {
    void *configMgrInst = GetConfigManagerInstance();
    if (!configMgrInst) {
        LOGE(OBFUSCATE("[-] Gagal mendapatkan ConfigManager Instance."));
        return nullptr;
    }

    void *configMgrKlass = _monoObjectGetClass(configMgrInst);
    void *m_getDrawingConfig = FindMethodInHierarchy(configMgrKlass, OBFUSCATE("get_DrawingConfig"),
                                                     0);
    void *drawingConfigInst = nullptr;
    void *exc = nullptr;

    if (m_getDrawingConfig) {
        drawingConfigInst = _monoRuntimeInvoke(m_getDrawingConfig, configMgrInst, nullptr, &exc);
    } else {
        LOGI(OBFUSCATE("[!] get_DrawingConfig method tidak ditemukan, mencoba via Field..."));
        void *f_drawingConfig = _monoClassGetFieldFromName(configMgrKlass,
                                                           OBFUSCATE("DrawingConfig"));
        if (!f_drawingConfig)
            f_drawingConfig = _monoClassGetFieldFromName(configMgrKlass, OBFUSCATE(
                    "<DrawingConfig>k__BackingField"));

        if (f_drawingConfig) {
            _monoFieldGetValue(configMgrInst, f_drawingConfig, &drawingConfigInst);
        }
    }

    if (!drawingConfigInst || exc) {
        LOGE(OBFUSCATE("[-] Gagal mengekstrak objek DrawingConfig."));
        return nullptr;
    }

    LOGI(OBFUSCATE("[+] JACKPOT! DrawingConfig Ditemukan di: %p"), drawingConfigInst);
    return drawingConfigInst;
}

void OpenSystemFileManagerNative() {
    // iOS 没有 Intent.ACTION_GET_CONTENT：用 shareddocuments:// 把本 App 的
    // Documents 目录交给"文件"App（需 Info.plist 开 UIFileSharingEnabled +
    // LSSupportsOpeningDocumentsInPlace），用户从那里拖放图片/乐器文件。
    // 打不开就静默返回（Android 版同样容错，不抛异常）。
    const std::string docs = SMPlatform::DocumentDir();
    if (docs.empty())
        return;
    SMPlatform::OpenURL(std::string("shareddocuments://") + docs);
}

void *FindFieldInHierarchy(void *klass, const char *name) {
    void *current = klass;
    while (current != nullptr) {
        void *field = _monoClassGetFieldFromName(current, name);
        if (field != nullptr) return field;
        current = _monoClassGetParent(current);
    }
    return nullptr;
}

void *InflateGetComponent(void *img, const char *ns, const char *klassName, void *m_GetComponent) {
    void *targetKlass = _monoClassFromName(img, ns, klassName);
    if (!targetKlass) return nullptr;
    void *targetType = _monoClassGetType(targetKlass);
    void *typeArgs[1] = {targetType};
    void *methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
    MonoGenericContext ctx = {nullptr, methodInst};
    return _mono_class_inflate_generic_method(m_GetComponent, &ctx);
}

void OpenURL(const char *url) {
    // iOS：没有 Uri.parse + ACTION_VIEW，直接交给 UIApplication（App/Platform.mm
    // 里已经在 main queue 上调 openURL:options:completionHandler:）。
    if (!url || !*url)
        return;
    SMPlatform::OpenURL(std::string(url));
}

void SetGameTimeNative(float timeVal) {
    _monoThreadAttach(_monoGetRootDomain());

    // 1. Dapatkan Class WeatherManager
    void *imgLevel = _monoImageLoaded((const char *) OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) {
        return;
    }

    void *klassWeather = _monoClassFromName(imgLevel, (const char *) OBFUSCATE(
                                                    "XDTLevelAndEntity.GameplaySystem.Weather"),
                                            (const char *) OBFUSCATE("WeatherManager"));
    if (!klassWeather) {
        return;
    }

    auto FindMethod = [](void *klass, const char *name, int paramCount) -> void * {
        void *current = klass;
        while (current != nullptr) {
            void *method = _monoClassGetMethodFromName(current, name, paramCount);
            if (method != nullptr) return method;
            current = _monoClassGetParent(current);
        }
        return nullptr;
    };

    void *m_SetTime = FindMethod(klassWeather, (const char *) OBFUSCATE("SetTimeNormalize"), 1);
    if (!m_SetTime) {
        return;
    }

    void *exc = nullptr;
    void *managerInst = nullptr;

    // =========================================================================
    // CARI INSTANCE VIA Managers.GetModule (TANPA Unity Engine)
    // =========================================================================
    void *imgFramework = _monoImageLoaded((const char *) OBFUSCATE("XDTBaseService"));
    if (!imgFramework)
        imgFramework = _monoImageLoaded((const char *) OBFUSCATE("XDTGame.Framework"));

    if (imgFramework) {
        void *klassManagers = _monoClassFromName(imgFramework,
                                                 (const char *) OBFUSCATE("XDTGame.Framework"),
                                                 (const char *) OBFUSCATE("Managers"));

        if (klassManagers) {
            // Ambil method: internal static IModule GetModule(Type type)
            // Membutuhkan 1 parameter (yaitu System.Type)
            void *m_GetModule = FindMethod(klassManagers, (const char *) OBFUSCATE("GetModule"), 1);

            if (m_GetModule) {
                // Bungkus pointer class WeatherManager ke dalam bentuk System.Type agar dikenali C#
                void *typeObject = _monoTypeGetObject(_monoGetRootDomain(),
                                                      _monoClassGetType(klassWeather));

                if (typeObject) {
                    void *argsMod[1] = {typeObject};

                    // Panggil Managers.GetModule(typeof(WeatherManager))
                    managerInst = _monoRuntimeInvoke(m_GetModule, nullptr, argsMod, &exc);

                    if (managerInst && !exc) {
                    }
                }
            }
        }
    }
    if (managerInst) {

        // STEP 1: UBAH CONTROL MODE MENJADI FIXED (Mengunci waktu)
        void *m_SetControlMode = FindMethod(klassWeather,
                                            (const char *) OBFUSCATE("SetControlMode"), 1);
        if (m_SetControlMode) {
            int modeVal = 1; // 1 = Parameter Enum WeatherControlMode (FixedStatic / BuildPreview)
            void *modeArgs[1] = {&modeVal};
            exc = nullptr;
            _monoRuntimeInvoke(m_SetControlMode, managerInst, modeArgs, &exc);
        }

        // STEP 2: TERAPKAN WAKTU YANG DIPILIH
        void *args[1] = {&timeVal};
        exc = nullptr;
        _monoRuntimeInvoke(m_SetTime, managerInst, args, &exc);

    }
}