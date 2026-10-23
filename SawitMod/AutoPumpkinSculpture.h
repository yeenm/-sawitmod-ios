#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <chrono>
#include <dlfcn.h>
#include "Global.h"
#include "ModUtils.h"
#include "App/Platform.h"
#include "NetAuth.h"
#include "obfuscate.h"
#include "Logger.h"
#include "Starcoolxdl/xdl.h"
// The RVA table comes in through Global.h (Offsets-cn.h). Including Offsets.h here as well pulled
// in the Global build's table on top of it, so it is deliberately not included twice.

#ifndef SAFE_READ_DEFINED
#define SAFE_READ_DEFINED
template<typename T>
static inline T SafeRead(uintptr_t addr, T defaultValue) {
    return addr ? *reinterpret_cast<T *>(addr) : defaultValue;
}
#endif

inline bool IsValidMonoPointer(void* ptr) {
    if (!ptr) return false;
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < 0x10000) return false;
    if ((addr & 7) != 0) return false;
    return true;
}

inline void* SafeMonoObjectGetClass(void* obj) {
    if (!IsValidMonoPointer(obj)) return nullptr;
    if (!_monoObjectGetClass) return nullptr;
    void* klass = _monoObjectGetClass(obj);
    if (!IsValidMonoPointer(klass)) return nullptr;
    return klass;
}

#ifndef MOD_QUATERNION_DEFINED
#define MOD_QUATERNION_DEFINED
struct ModQuaternion {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};
#endif

#ifndef MOD_MATRIX4X4_DEFINED
#define MOD_MATRIX4X4_DEFINED
struct ModMatrix4x4 {
    float m[16];

    static ModMatrix4x4 Identity() {
        ModMatrix4x4 mat{};
        mat.m[0] = 1.0f;
        mat.m[5] = 1.0f;
        mat.m[10] = 1.0f;
        mat.m[15] = 1.0f;
        return mat;
    }

    static ModMatrix4x4 FromTRS(const Vector3 &pos, const ModQuaternion &q, const Vector3 &scale = {1.0f, 1.0f, 1.0f}) {
        ModMatrix4x4 mat{};
        float num = q.x * 2.0f;
        float num2 = q.y * 2.0f;
        float num3 = q.z * 2.0f;
        float num4 = q.x * num;
        float num5 = q.y * num2;
        float num6 = q.z * num3;
        float num7 = q.x * num2;
        float num8 = q.x * num3;
        float num9 = q.y * num3;
        float num10 = q.w * num;
        float num11 = q.w * num2;
        float num12 = q.w * num3;

        mat.m[0] = (1.0f - (num5 + num6)) * scale.x;
        mat.m[1] = (num7 + num12) * scale.x;
        mat.m[2] = (num8 - num11) * scale.x;
        mat.m[3] = 0.0f;

        mat.m[4] = (num7 - num12) * scale.y;
        mat.m[5] = (1.0f - (num4 + num6)) * scale.y;
        mat.m[6] = (num9 + num10) * scale.y;
        mat.m[7] = 0.0f;

        mat.m[8] = (num8 + num11) * scale.z;
        mat.m[9] = (num9 - num10) * scale.z;
        mat.m[10] = (1.0f - (num4 + num5)) * scale.z;
        mat.m[11] = 0.0f;

        mat.m[12] = pos.x;
        mat.m[13] = pos.y;
        mat.m[14] = pos.z;
        mat.m[15] = 1.0f;
        return mat;
    }

    bool Inverse(ModMatrix4x4 &out) const {
        float inv[16], det;
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

        det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (std::abs(det) < 1e-6f) return false;
        det = 1.0f / det;
        for (int i = 0; i < 16; i++) out.m[i] = inv[i] * det;
        return true;
    }

    ModMatrix4x4 Inverse() const {
        ModMatrix4x4 out = Identity();
        Inverse(out);
        return out;
    }

    Vector3 MultiplyPoint3x4(const Vector3 &p) const {
        Vector3 res;
        res.x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
        res.y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        res.z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
        return res;
    }
};
#endif

inline ModQuaternion NativeGetRotation(void *obj) {
    if (!IsValidMonoPointer(obj)) return {0, 0, 0, 1};
    void *klass = SafeMonoObjectGetClass(obj);
    if (!klass) return {0, 0, 0, 1};
    void *m_getRot = FindMethodInHierarchy(klass, OBFUSCATE("get_rotation"), 0);
    if (!m_getRot) m_getRot = FindMethodInHierarchy(klass, OBFUSCATE("get_Rotation"), 0);
    if (m_getRot) {
        void *exc = nullptr;
        void *rObj = _monoRuntimeInvoke(m_getRot, obj, nullptr, &exc);
        if (rObj && !exc) {
            return *(ModQuaternion *) _monoObjectUnbox(rObj);
        }
    }
    return {0, 0, 0, 1};
}

inline bool TryGetFieldRootWorldToLocal(uint32_t rootNetId, ModMatrix4x4 &worldToLocal) {
    worldToLocal = ModMatrix4x4::Identity();
    if (rootNetId == 0) return false;
    void *imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
    if (!imgLevel) return false;

    void *entitiesKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("Entities"));
    void *m_getAny = entitiesKlass ? FindMethodInHierarchy(entitiesKlass, OBFUSCATE("GetAnyEntity"), 1) : nullptr;
    if (!m_getAny) {
        void *utilKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
        if (utilKlass) m_getAny = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetEntity"), 1);
        if (!m_getAny && utilKlass) m_getAny = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetAnyEntity"), 1);
    }
    if (!m_getAny) return false;

    uint32_t n = rootNetId;
    void *args[] = { &n };
    void *exc = nullptr;
    void *ent = _monoRuntimeInvoke(m_getAny, nullptr, args, &exc);
    if (!ent || exc || !IsValidMonoPointer(ent)) return false;

    void *entKlass = SafeMonoObjectGetClass(ent);
    if (entKlass) {
        void *m_l2w = FindMethodInHierarchy(entKlass, OBFUSCATE("get_localToWorldMatrix"), 0);
        if (m_l2w) {
            void *r = _monoRuntimeInvoke(m_l2w, ent, nullptr, &exc);
            if (r && !exc) {
                void *unboxed = _monoObjectUnbox(r);
                if (unboxed) {
                    ModMatrix4x4 l2w = *(ModMatrix4x4 *) unboxed;
                    return l2w.Inverse(worldToLocal);
                }
            }
        }
    }

    Vector3 rootPos = NativeGetPosition(ent);
    ModQuaternion rootRot = NativeGetRotation(ent);
    ModMatrix4x4 l2w = ModMatrix4x4::FromTRS(rootPos, rootRot, {1.0f, 1.0f, 1.0f});
    return l2w.Inverse(worldToLocal);
}

// =========================================================================
// =========================================================================
// =========================================================================
// AUTO PUMPKIN CARVING (100% IDENTICAL IMPLEMENTATION TO SAWITMOD PC + SERVER-C ONLY)
// =========================================================================

enum class PumpkinApiState {
    FindRough,
    Start,
    WaitResult
};

struct PumpkinMonoCache {
    bool resolved = false;
    void* protocolKlass = nullptr;
    void* startMethod = nullptr;
    void* finishMethod = nullptr;
    void* poseDeleteMethod = nullptr;

    void* viewKlass = nullptr;         // Homeland.PumpkinComponent
    void* buildKlass = nullptr;        // Homeland.BuildComponent
    void* finishViewKlass = nullptr;   // Homeland.PumpkinFinishComponent
    void* baseViewKlass = nullptr;     // Homeland.PumpkinBaseComponent

    void* buildOwnerMethod = nullptr;  // get_OwnerId
    void* buildParentMethod = nullptr; // GetFirstParentNetId

    void* tableKlass = nullptr;
    void* dictRough = nullptr;         // TablePumpkinroughs Dictionary
    void* dictBase = nullptr;          // TablePumpkinplanters Dictionary
    void* getRoughMethod = nullptr;    // GetPumpkinrough
    void* getBaseMethod = nullptr;     // GetPumpkinbase
};

static PumpkinMonoCache g_pumpkinCache;

inline static void* PumpkinGetStaticTableDictionary(void* tableDataClass, const char* fieldName) {
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
    void* vtable = _monoClassGetVTable ? _monoClassGetVTable(_monoGetRootDomain(), tableDataClass) : nullptr;
    if (vtable && _monoFieldStaticGetValue) _monoFieldStaticGetValue(vtable, field, &dictionary);
    return dictionary;
}

inline static void* PumpkinFindIntDictionaryValue(void* dictionary, int wantedKey) {
    if (!IsValidMonoPointer(dictionary)) return nullptr;
    void* dictClass = SafeMonoObjectGetClass(dictionary);
    if (!dictClass) return nullptr;
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

inline float PumpkinGetTimeSec() {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return duration_cast<duration<float>>(steady_clock::now() - start).count();
}

inline void* PumpkinMonoArrayNew(void* domain, void* byteKlass, size_t count) {
    static void* (*p)(void*, void*, uintptr_t) = nullptr;
    if (!p) {
        void* h = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), 0);
        if (!h) h = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), RTLD_NOW);
        if (!h) h = xdl_open(OBFUSCATE("libmonosgen-2.0.so"), XDL_TRY_FORCE_LOAD);
        if (!h) return nullptr;
        p = (void* (*)(void*, void*, uintptr_t))xdl_sym(h, (const char*)OBFUSCATE("mono_array_new"), (size_t*)nullptr);
    }
    if (!p) return nullptr;
    return p(domain, byteKlass, count);
}

inline void* GetCorlibImage() {
    void* corlib = _monoImageLoaded(OBFUSCATE("mscorlib"));
    if (!corlib) corlib = _monoImageLoaded(OBFUSCATE("mscorlib.dll"));
    if (!corlib) corlib = _monoImageLoaded(OBFUSCATE("System.Private.CoreLib"));
    return corlib;
}

// Strict ServerC Resolver: ONLY resolves symbols using ServerC authenticated payload (g_pumpkinPn).
// NO hardcoded local fallback allowed!
inline bool TryResolvePumpkinCarving() {
    if (g_pumpkinCache.resolved) return true;
    if (g_pumpkinPn.empty() || !SesiAktif()) {
        LOGE("[Pumpkin] Server payload (g_pumpkinPn) is empty or session offline -> cannot resolve");
        return false;
    }

    auto PN = [](const char* k) -> std::string { return extractJsonString(g_pumpkinPn, k); };

    std::string imgDataName = PN("n_imgData");
    std::string imgLevelName = PN("n_imgLevel");
    std::string imgTableName = PN("n_imgTable");
    std::string nsPumpkinProto = PN("n_nsPumpkinProto");
    std::string clsPumpkinProto = PN("n_clsPumpkinProto");
    std::string startCarvingName = PN("n_startCarving");
    std::string finishCarvingName = PN("n_finishCarving");
    std::string nsCharProto = PN("n_nsCharProto");
    std::string clsCharProto = PN("n_clsCharProto");
    std::string poseDeleteName = PN("n_poseDelete");
    std::string nsHomelandComp = PN("n_nsHomelandComp");
    std::string clsPumpkinComp = PN("n_clsPumpkinComp");
    std::string clsBuildComp = PN("n_clsBuildComp");
    std::string clsFinishComp = PN("n_clsFinishComp");
    std::string clsBaseComp = PN("n_clsBaseComp");
    std::string mGetOwnerId = PN("n_mGetOwnerId");
    std::string mGetFirstParent = PN("n_mGetFirstParent");
    std::string clsTableData = PN("n_clsTableData");
    std::string mGetPumpkinRough = PN("n_mGetPumpkinRough");
    std::string mGetPumpkinBase = PN("n_mGetPumpkinBase");

    // Strictly enforce ServerC payload existence - NO LOCAL FALLBACK ALLOWED!
    if (imgDataName.empty() || imgLevelName.empty() ||
        nsPumpkinProto.empty() || clsPumpkinProto.empty() ||
        startCarvingName.empty() || finishCarvingName.empty() ||
        nsCharProto.empty() || clsCharProto.empty() || poseDeleteName.empty() ||
        nsHomelandComp.empty() || clsPumpkinComp.empty() || clsBuildComp.empty() ||
        clsFinishComp.empty() || clsBaseComp.empty() ||
        mGetOwnerId.empty() || mGetFirstParent.empty()) {
        LOGE("[Pumpkin] Server payload (g_pumpkinPn) missing critical names -> aborting resolve");
        return false;
    }

    void* imgProto = _monoImageLoaded(imgDataName.c_str());
    if (!imgProto) imgProto = _monoImageLoaded((imgDataName + ".dll").c_str());
    void* imgLevel = _monoImageLoaded(imgLevelName.c_str());
    if (!imgLevel) imgLevel = _monoImageLoaded((imgLevelName + ".dll").c_str());
    if (!imgProto || !imgLevel) return false;

    g_pumpkinCache.protocolKlass = _monoClassFromName(imgProto, nsPumpkinProto.c_str(), clsPumpkinProto.c_str());
    if (g_pumpkinCache.protocolKlass) {
        g_pumpkinCache.startMethod = FindMethodInHierarchy(g_pumpkinCache.protocolKlass, startCarvingName.c_str(), 2);
        g_pumpkinCache.finishMethod = FindMethodInHierarchy(g_pumpkinCache.protocolKlass, finishCarvingName.c_str(), 2);
    }

    void* charProtoKlass = _monoClassFromName(imgProto, nsCharProto.c_str(), clsCharProto.c_str());
    if (charProtoKlass) {
        g_pumpkinCache.poseDeleteMethod = FindMethodInHierarchy(charProtoKlass, poseDeleteName.c_str(), 1);
    }

    g_pumpkinCache.viewKlass = _monoClassFromName(imgLevel, nsHomelandComp.c_str(), clsPumpkinComp.c_str());
    g_pumpkinCache.buildKlass = _monoClassFromName(imgLevel, nsHomelandComp.c_str(), clsBuildComp.c_str());
    g_pumpkinCache.finishViewKlass = _monoClassFromName(imgLevel, nsHomelandComp.c_str(), clsFinishComp.c_str());
    g_pumpkinCache.baseViewKlass = _monoClassFromName(imgLevel, nsHomelandComp.c_str(), clsBaseComp.c_str());

    if (g_pumpkinCache.buildKlass) {
        g_pumpkinCache.buildOwnerMethod = FindMethodInHierarchy(g_pumpkinCache.buildKlass, mGetOwnerId.c_str(), 0);
        g_pumpkinCache.buildParentMethod = FindMethodInHierarchy(g_pumpkinCache.buildKlass, mGetFirstParent.c_str(), 0);
    }

    void* imgTable = !imgTableName.empty() ? _monoImageLoaded(imgTableName.c_str()) : nullptr;
    if (!imgTable && !imgTableName.empty()) imgTable = _monoImageLoaded((imgTableName + ".dll").c_str());
    if (!imgTable) imgTable = _monoImageLoaded(OBFUSCATE("EcsClient"));
    if (!imgTable) imgTable = _monoImageLoaded(OBFUSCATE("Assembly-CSharp"));

    if (imgTable && !clsTableData.empty()) {
        g_pumpkinCache.tableKlass = _monoClassFromName(imgTable, "", clsTableData.c_str());
        if (!g_pumpkinCache.tableKlass) g_pumpkinCache.tableKlass = _monoClassFromName(imgTable, "XD.GameGerm.ExpParser", clsTableData.c_str());
    }

    if (g_pumpkinCache.tableKlass) {
        g_pumpkinCache.dictRough = PumpkinGetStaticTableDictionary(g_pumpkinCache.tableKlass, "TablePumpkinroughs");
        if (!g_pumpkinCache.dictRough) g_pumpkinCache.dictRough = PumpkinGetStaticTableDictionary(g_pumpkinCache.tableKlass, "TablePumpkinrough");

        g_pumpkinCache.dictBase = PumpkinGetStaticTableDictionary(g_pumpkinCache.tableKlass, "TablePumpkinplanters");
        if (!g_pumpkinCache.dictBase) g_pumpkinCache.dictBase = PumpkinGetStaticTableDictionary(g_pumpkinCache.tableKlass, "TablePumpkinplanter");
        if (!g_pumpkinCache.dictBase) g_pumpkinCache.dictBase = PumpkinGetStaticTableDictionary(g_pumpkinCache.tableKlass, "TablePumpkinbases");
        if (!g_pumpkinCache.dictBase) g_pumpkinCache.dictBase = PumpkinGetStaticTableDictionary(g_pumpkinCache.tableKlass, "TablePumpkinbase");

        if (!mGetPumpkinRough.empty()) {
            g_pumpkinCache.getRoughMethod = FindMethodInHierarchy(g_pumpkinCache.tableKlass, mGetPumpkinRough.c_str(), 2);
            if (!g_pumpkinCache.getRoughMethod) g_pumpkinCache.getRoughMethod = FindMethodInHierarchy(g_pumpkinCache.tableKlass, mGetPumpkinRough.c_str(), 1);
        }

        if (!mGetPumpkinBase.empty()) {
            g_pumpkinCache.getBaseMethod = FindMethodInHierarchy(g_pumpkinCache.tableKlass, mGetPumpkinBase.c_str(), 2);
            if (!g_pumpkinCache.getBaseMethod) g_pumpkinCache.getBaseMethod = FindMethodInHierarchy(g_pumpkinCache.tableKlass, mGetPumpkinBase.c_str(), 1);
        }
        if (!g_pumpkinCache.getBaseMethod) {
            g_pumpkinCache.getBaseMethod = FindMethodInHierarchy(g_pumpkinCache.tableKlass, "GetPumpkinplanter", 2);
            if (!g_pumpkinCache.getBaseMethod) g_pumpkinCache.getBaseMethod = FindMethodInHierarchy(g_pumpkinCache.tableKlass, "GetPumpkinplanter", 1);
        }
    }

    bool ok = (g_pumpkinCache.protocolKlass != nullptr &&
               g_pumpkinCache.startMethod != nullptr &&
               g_pumpkinCache.finishMethod != nullptr &&
               g_pumpkinCache.viewKlass != nullptr &&
               g_pumpkinCache.buildKlass != nullptr &&
               g_pumpkinCache.finishViewKlass != nullptr &&
               g_pumpkinCache.baseViewKlass != nullptr &&
               g_pumpkinCache.buildOwnerMethod != nullptr &&
               g_pumpkinCache.buildParentMethod != nullptr &&
               g_pumpkinCache.poseDeleteMethod != nullptr);

    g_pumpkinCache.resolved = ok;
    return ok;
}

inline void* GetEntityFromComponent(void* compObj) {
    if (!IsValidMonoPointer(compObj)) return nullptr;
    void* klass = SafeMonoObjectGetClass(compObj);
    if (!klass) return nullptr;

    const char* entMeths[] = { "get_entity", "get_Entity", "GetEntity" };
    for (const char* mname : entMeths) {
        void* m = FindMethodInHierarchy(klass, mname, 0);
        if (m) {
            void* exc = nullptr;
            void* ent = _monoRuntimeInvoke(m, compObj, nullptr, &exc);
            if (ent && !exc && IsValidMonoPointer(ent)) return ent;
        }
    }

    const char* fnames[] = { "entity", "_entity", "Entity", "m_Entity", "_Entity" };
    for (const char* fn : fnames) {
        void* f = _monoClassGetFieldFromName(klass, fn);
        if (f) {
            void* ent = nullptr;
            _monoFieldGetValue(compObj, f, &ent);
            if (ent && IsValidMonoPointer(ent)) return ent;
        }
    }
    return nullptr;
}

static std::unordered_map<void*, uint32_t> g_compNetIdMap;

inline uint32_t ReadComponentNetId(void* compObj) {
    if (!IsValidMonoPointer(compObj)) return 0;
    void* klass = SafeMonoObjectGetClass(compObj);
    if (!klass) return 0;

    // 1. Check via Entity
    void* entObj = GetEntityFromComponent(compObj);
    if (entObj && IsValidMonoPointer(entObj)) {
        void* ek = SafeMonoObjectGetClass(entObj);
        if (ek) {
            const char* entMethods[] = { "GetNetId", "get_netId", "get_NetId", "get_Id", "GetId" };
            for (const char* mname : entMethods) {
                void* m = FindMethodInHierarchy(ek, mname, 0);
                if (m) {
                    void* exc = nullptr;
                    void* nRes = _monoRuntimeInvoke(m, entObj, nullptr, &exc);
                    if (nRes && !exc) {
                        void* unboxed = _monoObjectUnbox(nRes);
                        if (unboxed && *(uint32_t*)unboxed != 0) return *(uint32_t*)unboxed;
                    }
                }
            }
            const char* efnames[] = { "netId", "_netId", "NetId", "id", "_id", "Id" };
            for (const char* fn : efnames) {
                void* f = _monoClassGetFieldFromName(ek, fn);
                if (f) {
                    uint32_t nid = 0;
                    _monoFieldGetValue(entObj, f, reinterpret_cast<void**>(&nid));
                    if (nid != 0) return nid;
                }
            }
        }
    }

    // 2. Check directly on component
    const char* compMethods[] = { "GetNetId", "get_netId", "get_NetId", "get_Id", "GetId" };
    for (const char* mname : compMethods) {
        void* m = FindMethodInHierarchy(klass, mname, 0);
        if (m) {
            void* exc = nullptr;
            void* nRes = _monoRuntimeInvoke(m, compObj, nullptr, &exc);
            if (nRes && !exc) {
                void* unboxed = _monoObjectUnbox(nRes);
                if (unboxed && *(uint32_t*)unboxed != 0) return *(uint32_t*)unboxed;
            }
        }
    }

    const char* cfnames[] = { "netId", "_netId", "NetId", "id", "_id", "Id" };
    for (const char* fn : cfnames) {
        void* f = _monoClassGetFieldFromName(klass, fn);
        if (f) {
            uint32_t nid = 0;
            _monoFieldGetValue(compObj, f, reinterpret_cast<void**>(&nid));
            if (nid != 0) return nid;
        }
    }

    // 3. Fallback: ECS dict key
    auto it = g_compNetIdMap.find(compObj);
    if (it != g_compNetIdMap.end()) {
        return it->second;
    }

    return 0;
}

inline std::vector<void*> GetComponentsOfClass(void* compKlass) {
    std::vector<void*> result;
    if (!compKlass) return result;

    void* imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
    void* imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));

    // Primary Fast & Crash-Safe ECS Component scan via EntityWorld.GetComponentSystem(Type)
    void* worldKlass = imgLevel ? _monoClassFromName(imgLevel, OBFUSCATE("XDTGame.GAS.Client"), OBFUSCATE("EntityWorld")) : nullptr;
    if (!worldKlass && imgData) worldKlass = _monoClassFromName(imgData, OBFUSCATE("XDTGame.GAS.Client"), OBFUSCATE("EntityWorld"));
    if (!worldKlass && imgLevel) worldKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("EntityWorld"));
    if (!worldKlass && imgData) worldKlass = _monoClassFromName(imgData, OBFUSCATE("XDTLevelAndEntity.Core.World"), OBFUSCATE("EntityWorld"));

    if (worldKlass) {
        void* compType = _monoClassGetType ? _monoClassGetType(compKlass) : nullptr;
        void* compTypeObj = (compType && _monoTypeGetObject) ? _monoTypeGetObject(_monoGetRootDomain(), compType) : nullptr;
        void* sysObj = nullptr;
        if (compTypeObj) {
            void* m_getSys = FindMethodInHierarchy(worldKlass, OBFUSCATE("GetComponentSystem"), 1);
            if (m_getSys) {
                void* args[] = { compTypeObj };
                void* exc = nullptr;
                sysObj = _monoRuntimeInvoke(m_getSys, nullptr, args, &exc);
                if (exc) sysObj = nullptr;
            }
        }
        if (!sysObj && compType && _mono_class_inflate_generic_method && _mono_metadata_get_generic_inst) {
            void* openMethod = FindMethodInHierarchy(worldKlass, OBFUSCATE("GetComponentSystem"), 0);
            if (openMethod) {
                void* typeArgs[1] = { compType };
                void* methodInst = _mono_metadata_get_generic_inst(1, typeArgs);
                MonoGenericContext ctx = { nullptr, methodInst };
                void* closedMethod = _mono_class_inflate_generic_method(openMethod, &ctx);
                if (closedMethod) {
                    void* exc = nullptr;
                    sysObj = _monoRuntimeInvoke(closedMethod, nullptr, nullptr, &exc);
                    if (exc) sysObj = nullptr;
                }
            }
        }
        if (sysObj && IsValidMonoPointer(sysObj)) {
            void* sysKlass = SafeMonoObjectGetClass(sysObj);
            void* f_components = sysKlass ? FindFieldInHierarchy(sysKlass, OBFUSCATE("_components")) : nullptr;
            void* dictObj = nullptr;
            if (f_components) _monoFieldGetValue(sysObj, f_components, &dictObj);
            if (!dictObj && sysKlass) {
                void* m_allComps = FindMethodInHierarchy(sysKlass, OBFUSCATE("get_AllComponents"), 0);
                if (m_allComps) dictObj = _monoRuntimeInvoke(m_allComps, sysObj, nullptr, nullptr);
            }
            if (dictObj && IsValidMonoPointer(dictObj)) {
                void* dictKlass = SafeMonoObjectGetClass(dictObj);
                void* f_entries = dictKlass ? FindFieldInHierarchy(dictKlass, OBFUSCATE("entries")) : nullptr;
                if (!f_entries && dictKlass) f_entries = FindFieldInHierarchy(dictKlass, OBFUSCATE("_entries"));
                void* entriesArr = nullptr;
                if (f_entries) _monoFieldGetValue(dictObj, f_entries, &entriesArr);
                if (entriesArr && IsValidMonoPointer(entriesArr)) {
                    uintptr_t arrAddr = (uintptr_t)entriesArr;
                    int length = SafeRead<int>(arrAddr + OFF_ARR_COUNT, 0);
                    if (length > 0 && length < 200000) {
                        uintptr_t data = arrAddr + OFF_ARR_DATA;
                        constexpr int stride = 24;
                        for (int i = 0; i < length; i++) {
                            const uintptr_t entry = data + (i * stride);
                            if (SafeRead<int>(entry, -1) < 0) continue;
                            uint32_t dictKeyId = SafeRead<uint32_t>(entry + OFF_ENTRY_KEY, 0);
                            void* comp = SafeRead<void*>(entry + OFF_ENTRY_VALUE, nullptr);
                            if (comp && IsValidMonoPointer(comp)) {
                                if (dictKeyId != 0) g_compNetIdMap[comp] = dictKeyId;
                                result.push_back(comp);
                            }
                        }
                    }
                }
            }
        }
    }

    return result;
}

inline bool TryReadPumpkinComponentData(void* componentObj, int& finishedId, bool& rotate) {
    finishedId = 0;
    rotate = false;
    if (!IsValidMonoPointer(componentObj)) return false;
    void* compKlass = SafeMonoObjectGetClass(componentObj);
    if (!compKlass) return false;

    void* f_data = FindFieldInHierarchy(compKlass, OBFUSCATE("_componentData"));
    if (!f_data) f_data = FindFieldInHierarchy(compKlass, OBFUSCATE("componentData"));

    void* valObj = nullptr;
    if (f_data && _monoFieldGetValueObject) {
        valObj = _monoFieldGetValueObject(_monoGetRootDomain(), f_data, componentObj);
    }
    if (!valObj) {
        const char* propNames[] = { "get_ComponentData", "get__componentData", "get_componentData" };
        for (const char* pname : propNames) {
            void* m_get = FindMethodInHierarchy(compKlass, pname, 0);
            if (m_get) {
                void* exc = nullptr;
                valObj = _monoRuntimeInvoke(m_get, componentObj, nullptr, &exc);
                if (valObj && !exc) break;
            }
        }
    }
    if (!valObj) return false;

    void* dataKlass = SafeMonoObjectGetClass(valObj);
    if (!dataKlass) return false;

    void* f_finId = FindFieldInHierarchy(dataKlass, OBFUSCATE("finishedId"));
    if (!f_finId) f_finId = FindFieldInHierarchy(dataKlass, OBFUSCATE("FinishedId"));
    void* f_rot = FindFieldInHierarchy(dataKlass, OBFUSCATE("rotate"));
    if (!f_rot) f_rot = FindFieldInHierarchy(dataKlass, OBFUSCATE("Rotate"));
    if (!f_finId || !f_rot) return false;

    void* unboxed = _monoObjectUnbox(valObj);
    if (!unboxed) return false;

    int num = 2 * sizeof(void*);
    int offFin = _monoFieldGetOffset(f_finId) - num;
    int offRot = _monoFieldGetOffset(f_rot) - num;
    if (offFin < 0 || offRot < 0) return false;

    finishedId = *(int*)((uintptr_t)unboxed + offFin);
    rotate = (*(uint8_t*)((uintptr_t)unboxed + offRot) != 0);
    return true;
}

inline bool TryReadPumpkinFinishCreateByCarving(void* componentObj, bool& createByCarving) {
    createByCarving = false;
    if (!IsValidMonoPointer(componentObj)) return false;
    void* compKlass = SafeMonoObjectGetClass(componentObj);
    if (!compKlass) return false;

    void* f_data = FindFieldInHierarchy(compKlass, OBFUSCATE("_componentData"));
    if (!f_data) f_data = FindFieldInHierarchy(compKlass, OBFUSCATE("componentData"));

    void* valObj = nullptr;
    if (f_data && _monoFieldGetValueObject) {
        valObj = _monoFieldGetValueObject(_monoGetRootDomain(), f_data, componentObj);
    }
    if (!valObj) {
        const char* propNames[] = { "get_ComponentData", "get__componentData", "get_componentData" };
        for (const char* pname : propNames) {
            void* m_get = FindMethodInHierarchy(compKlass, pname, 0);
            if (m_get) {
                void* exc = nullptr;
                valObj = _monoRuntimeInvoke(m_get, componentObj, nullptr, &exc);
                if (valObj && !exc) break;
            }
        }
    }
    if (!valObj) return false;

    void* dataKlass = SafeMonoObjectGetClass(valObj);
    if (!dataKlass) return false;

    void* f_cbc = FindFieldInHierarchy(dataKlass, OBFUSCATE("createByCarving"));
    if (!f_cbc) f_cbc = FindFieldInHierarchy(dataKlass, OBFUSCATE("_createByCarving"));
    if (!f_cbc) f_cbc = FindFieldInHierarchy(dataKlass, OBFUSCATE("CreateByCarving"));
    if (!f_cbc) return false;

    void* unboxed = _monoObjectUnbox(valObj);
    if (!unboxed) return false;

    int num = 2 * sizeof(void*);
    int offCbc = _monoFieldGetOffset(f_cbc) - num;
    if (offCbc < 0) return false;

    createByCarving = (*(uint8_t*)((uintptr_t)unboxed + offCbc) != 0);
    return true;
}

inline bool TryPumpkinInvokeUInt(void* obj, void* method, uint32_t& value) {
    value = 0;
    if (!obj || !method) return false;
    void* exc = nullptr;
    void* res = _monoRuntimeInvoke(method, obj, nullptr, &exc);
    if (exc || !res) return false;
    void* unboxed = _monoObjectUnbox(res);
    if (!unboxed) return false;
    value = *(uint32_t*)unboxed;
    return true;
}

inline bool TryCreatePumpkinScoreList(void*& outListObj) {
    outListObj = nullptr;
    void* corlib = GetCorlibImage();
    if (!corlib) return false;

    void* typeKlass = _monoClassFromName(corlib, OBFUSCATE("System"), OBFUSCATE("Type"));
    void* actKlass = _monoClassFromName(corlib, OBFUSCATE("System"), OBFUSCATE("Activator"));
    if (!typeKlass || !actKlass) return false;

    void* m_getType = FindMethodInHierarchy(typeKlass, OBFUSCATE("GetType"), 1);
    void* m_createInst = FindMethodInHierarchy(actKlass, OBFUSCATE("CreateInstance"), 1);
    if (!m_getType || !m_createInst) return false;

    const char* candidates[] = {
        "System.Collections.Generic.List`1[System.Int32]",
        "System.Collections.Generic.List`1[[System.Int32, mscorlib]]",
        "System.Collections.Generic.List`1[[System.Int32, System.Private.CoreLib]]"
    };

    void* domain = _monoGetRootDomain();
    void* listObj = nullptr;
    for (const char* cand : candidates) {
        void* monoStr = _monoStringNew ? _monoStringNew(domain, cand) : nullptr;
        if (!monoStr) continue;
        void* tArgs[1] = { monoStr };
        void* exc = nullptr;
        void* typeObj = _monoRuntimeInvoke(m_getType, nullptr, tArgs, &exc);
        if (typeObj && !exc) {
            void* cArgs[1] = { typeObj };
            listObj = _monoRuntimeInvoke(m_createInst, nullptr, cArgs, &exc);
            if (listObj && !exc) break;
        }
    }
    if (!listObj) return false;

    void* listKlass = SafeMonoObjectGetClass(listObj);
    if (!listKlass) return false;
    void* m_add = FindMethodInHierarchy(listKlass, OBFUSCATE("Add"), 1);
    if (m_add) {
        int score = (g_pumpkinScore > 0) ? g_pumpkinScore : 100;
        int count = (g_pumpkinScoreCount > 0) ? g_pumpkinScoreCount : 4;
        void* args[] = { &score };
        for (int i = 0; i < count; i++) {
            _monoRuntimeInvoke(m_add, listObj, args, nullptr);
        }
    }
    outListObj = listObj;
    return (outListObj != nullptr);
}

inline bool TryPumpkinStart(uint32_t baseNetId, uint32_t pumpkinNetId) {
    if (!g_pumpkinCache.startMethod) return false;
    uint32_t bId = baseNetId;
    uint32_t pId = pumpkinNetId;
    void* args[] = { &bId, &pId };
    void* exc = nullptr;
    _monoRuntimeInvoke(g_pumpkinCache.startMethod, nullptr, args, &exc);
    return (exc == nullptr);
}

inline bool TryPumpkinFinish(uint32_t pumpkinNetId) {
    if (!g_pumpkinCache.finishMethod) return false;
    void* listObj = nullptr;
    if (!TryCreatePumpkinScoreList(listObj) || !listObj) return false;
    uint32_t pId = pumpkinNetId;
    void* args[] = { &pId, listObj };
    void* exc = nullptr;
    _monoRuntimeInvoke(g_pumpkinCache.finishMethod, nullptr, args, &exc);
    return (exc == nullptr);
}

inline bool TryPumpkinPoseDelete(uint32_t netId) {
    if (!g_pumpkinCache.poseDeleteMethod || netId == 0) return false;
    uint32_t n = netId;
    void* args[] = { &n };
    void* exc = nullptr;
    void* res = _monoRuntimeInvoke(g_pumpkinCache.poseDeleteMethod, nullptr, args, &exc);
    if (exc) return false;
    if (res) {
        void* unboxed = _monoObjectUnbox(res);
        if (unboxed) {
            bool ok = (*(uint8_t*)unboxed != 0);
            if (!ok) return false;
        }
    }
    return true;
}

inline bool PumpkinTableHasRow(void* tableDict, void* tableMethod, int staticId) {
    if (staticId <= 0) return false;
    if (tableDict) {
        void* val = PumpkinFindIntDictionaryValue(tableDict, staticId);
        if (val) return true;
    }
    if (tableMethod) {
        int sid = staticId;
        uint8_t flag = 0;
        void* args2[] = { &sid, &flag };
        void* args1[] = { &sid };
        void* exc = nullptr;
        void* res = _monoRuntimeInvoke(tableMethod, nullptr, args2, &exc);
        if (exc || !res) {
            exc = nullptr;
            res = _monoRuntimeInvoke(tableMethod, nullptr, args1, &exc);
        }
        if (!exc && res) return true;
    }
    return false;
}

inline bool TryPumpkinFindBagItem(void* tableDict, void* tableMethod, uint32_t& outBagItemNetId, int& outStaticId) {
    outBagItemNetId = 0;
    outStaticId = 0;
    if (!tableDict && !tableMethod) return false;

    void* imgSys = _monoImageLoaded(OBFUSCATE("XDTGameSystem"));
    if (!imgSys) imgSys = _monoImageLoaded(OBFUSCATE("XDTGameSystem.dll"));
    if (!imgSys) return false;

    void* sysKlass = _monoClassFromName(imgSys, OBFUSCATE("XDTGameSystem.GameplaySystem.BackPack"), OBFUSCATE("BackPackSystem"));
    if (!sysKlass) return false;

    void* m_getInst = FindMethodInHierarchy(sysKlass, OBFUSCATE("get_Instance"), 0);
    void* sysInst = m_getInst ? _monoRuntimeInvoke(m_getInst, nullptr, nullptr, nullptr) : nullptr;
    if (!sysInst) return false;

    void* m_getAllItem = FindMethodInHierarchy(sysKlass, OBFUSCATE("GetAllItem"), 1);
    if (!m_getAllItem) return false;

    int storageType = 1; // Backpack
    void* argsGetAll[1] = { &storageType };
    void* listObj = _monoRuntimeInvoke(m_getAllItem, sysInst, argsGetAll, nullptr);
    if (!listObj) return false;

    void* listKlass = SafeMonoObjectGetClass(listObj);
    if (!listKlass) return false;
    void* sizeField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_size"));
    void* itemsField = _monoClassGetFieldFromName(listKlass, OBFUSCATE("_items"));
    if (!sizeField || !itemsField) return false;

    int size = 0;
    _monoFieldGetValue(listObj, sizeField, (void**)&size);
    void* itemsArray = nullptr;
    _monoFieldGetValue(listObj, itemsField, &itemsArray);
    if (!itemsArray || size <= 0 || size > 5000) return false;

    void* bpItemKlass = _monoClassFromName(imgSys, OBFUSCATE("XDTGameSystem.UISystem.BackPack"), OBFUSCATE("BackpackItem"));
    if (!bpItemKlass) return false;
    uint32_t align = 0;
    int structSize = _monoClassValueSize ? _monoClassValueSize(bpItemKlass, &align) : 0;
    if (structSize <= 0) structSize = 0x20;

    uintptr_t arrayDataStart = (uintptr_t)itemsArray + OFF_ARR_DATA;
    for (int i = 0; i < size; i++) {
        uintptr_t structAddr = arrayDataStart + (i * structSize);
        uint32_t netId = SafeRead<uint32_t>(structAddr + OFF_BP_NETID, 0);
        int staticId = SafeRead<int>(structAddr + OFF_BP_STATICID, 0);
        int count = SafeRead<int>(structAddr + OFF_BP_COUNT, 0);

        if (netId == 0 || staticId <= 0 || count <= 0) continue;

        if (PumpkinTableHasRow(tableDict, tableMethod, staticId)) {
            outBagItemNetId = netId;
            outStaticId = staticId;
            return true;
        }
    }
    return false;
}

inline bool TryGetEntityPositionFromNetId(uint32_t nid, Vector3& outPos) {
    outPos = { 0, 0, 0 };
    if (nid == 0) return false;
    void* imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
    if (!imgLevel) return false;
    void* entMgrKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("Entities"));
    void* m_getAny = entMgrKlass ? FindMethodInHierarchy(entMgrKlass, OBFUSCATE("GetAnyEntity"), 1) : nullptr;
    if (m_getAny) {
        uint32_t n = nid; void* a[] = { &n };
        void* exc = nullptr;
        void* ent = _monoRuntimeInvoke(m_getAny, nullptr, a, &exc);
        if (ent && !exc && IsValidMonoPointer(ent)) {
            outPos = NativeGetPosition(ent);
            if (fabsf(outPos.x) > 0.01f || fabsf(outPos.z) > 0.01f) {
                return true;
            }
        }
    }
    return false;
}

inline bool TryGetEntityPositionFromComponent(void* compObj, Vector3& outPos) {
    outPos = { 0, 0, 0 };
    if (!IsValidMonoPointer(compObj)) return false;
    void* entObj = GetEntityFromComponent(compObj);
    if (entObj && IsValidMonoPointer(entObj)) {
        outPos = NativeGetPosition(entObj);
        if (fabsf(outPos.x) > 0.01f || fabsf(outPos.z) > 0.01f) {
            return true;
        }
    }
    uint32_t nid = ReadComponentNetId(compObj);
    if (nid != 0) {
        return TryGetEntityPositionFromNetId(nid, outPos);
    }
    return false;
}

inline uint32_t GetSelfPlayerNetId() {
    // 1. Exact Tester implementation: PlayerDataCenter.GetSelfNetPlayerId()
    void* imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (imgData) {
        void* pdcKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol"), OBFUSCATE("PlayerDataCenter"));
        if (!pdcKlass) pdcKlass = _monoClassFromName(imgData, OBFUSCATE(""), OBFUSCATE("PlayerDataCenter"));
        if (pdcKlass) {
            void* m_getNetId = FindMethodInHierarchy(pdcKlass, OBFUSCATE("GetSelfNetPlayerId"), 0);
            if (m_getNetId) {
                void* exc = nullptr;
                void* res = _monoRuntimeInvoke(m_getNetId, nullptr, nullptr, &exc);
                if (res && !exc) {
                    void* unboxed = _monoObjectUnbox(res);
                    if (unboxed) {
                        uint32_t nid = *(uint32_t*)unboxed;
                        if (nid != 0) {
                            g_gardenPlayerNetId = nid;
                            return nid;
                        }
                    }
                }
            }
        }
    }

    // 2. Check cached GlobalVars::g_gardenPlayerNetId
    if (g_gardenPlayerNetId != 0) {
        return g_gardenPlayerNetId;
    }

    // 3. Fallback: EntityUtil.GetSelfPlayer() -> entity -> netId
    void* imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
    if (imgLevel) {
        void* utilKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
        if (utilKlass) {
            void* m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
            if (m_getSelf) {
                void* selfPlayer = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, nullptr);
                if (selfPlayer && IsValidMonoPointer(selfPlayer)) {
                    void* spKlass = SafeMonoObjectGetClass(selfPlayer);
                    void* m_getEnt = spKlass ? FindMethodInHierarchy(spKlass, OBFUSCATE("get_entity"), 0) : nullptr;
                    if (m_getEnt) {
                        void* entObj = _monoRuntimeInvoke(m_getEnt, selfPlayer, nullptr, nullptr);
                        if (entObj && IsValidMonoPointer(entObj)) {
                            void* entKlass = SafeMonoObjectGetClass(entObj);
                            void* m_getNid = FindMethodInHierarchy(entKlass, OBFUSCATE("GetNetId"), 0);
                            if (!m_getNid) m_getNid = FindMethodInHierarchy(entKlass, OBFUSCATE("get_NetId"), 0);
                            if (!m_getNid) m_getNid = FindMethodInHierarchy(entKlass, OBFUSCATE("get_netId"), 0);
                            if (m_getNid) {
                                void* nObj = _monoRuntimeInvoke(m_getNid, entObj, nullptr, nullptr);
                                if (nObj) {
                                    void* unboxed = _monoObjectUnbox(nObj);
                                    if (unboxed) return *(uint32_t*)unboxed;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}

inline bool TryGetLocalPlayerPosition(Vector3& outPos) {
    outPos = { 0, 0, 0 };
    void* imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
    if (!imgLevel) return false;
    void* utilKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
    if (!utilKlass) return false;
    void* m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
    if (!m_getSelf) return false;
    void* selfPlayer = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, nullptr);
    if (!selfPlayer) return false;
    outPos = NativeGetPosition(selfPlayer);
    return true;
}

inline bool TryPumpkinActiveRoughStillCarvable(uint32_t pumpkinNetId) {
    if (pumpkinNetId == 0 || !g_pumpkinCache.viewKlass) return false;
    auto components = GetComponentsOfClass(g_pumpkinCache.viewKlass);
    if (components.empty()) return false;
    for (auto c : components) {
        if (ReadComponentNetId(c) == pumpkinNetId) {
            int finishedId = 0; bool rotate = false;
            if (!TryReadPumpkinComponentData(c, finishedId, rotate)) return true;
            return (!rotate && finishedId != 0);
        }
    }
    return false;
}

inline bool TryPumpkinHasOwnRough(uint32_t selfPlayerNetId) {
    if (!g_pumpkinCache.buildKlass || !g_pumpkinCache.viewKlass) return false;
    auto buildComps = GetComponentsOfClass(g_pumpkinCache.buildKlass);
    std::unordered_map<uint32_t, uint32_t> ownerByNetId;
    for (auto b : buildComps) {
        uint32_t bNetId = ReadComponentNetId(b);
        uint32_t owner = 0;
        if (bNetId != 0 && TryPumpkinInvokeUInt(b, g_pumpkinCache.buildOwnerMethod, owner)) {
            ownerByNetId[bNetId] = owner;
        }
    }
    auto viewComps = GetComponentsOfClass(g_pumpkinCache.viewKlass);
    for (auto v : viewComps) {
        uint32_t vNetId = ReadComponentNetId(v);
        if (vNetId == 0) continue;
        auto it = ownerByNetId.find(vNetId);
        if (it != ownerByNetId.end() && it->second == selfPlayerNetId) {
            int finishedId = 0; bool rotate = false;
            if (TryReadPumpkinComponentData(v, finishedId, rotate) && !rotate && finishedId != 0) {
                return true;
            }
        }
    }
    return false;
}

inline bool TryPumpkinFindOwnBase(uint32_t selfPlayerNetId, uint32_t& outBaseNetId, Vector3& outBasePos) {
    outBaseNetId = 0;
    outBasePos = { 0, 0, 0 };
    if (!g_pumpkinCache.baseViewKlass || !g_pumpkinCache.buildKlass) {
        return false;
    }

    auto buildComps = GetComponentsOfClass(g_pumpkinCache.buildKlass);
    std::unordered_map<uint32_t, uint32_t> ownerByNetId;
    for (auto b : buildComps) {
        uint32_t bNetId = ReadComponentNetId(b);
        uint32_t owner = 0;
        if (bNetId != 0 && TryPumpkinInvokeUInt(b, g_pumpkinCache.buildOwnerMethod, owner)) {
            ownerByNetId[bNetId] = owner;
        }
    }

    auto baseComps = GetComponentsOfClass(g_pumpkinCache.baseViewKlass);
    if (baseComps.empty()) {
        return false;
    }

    Vector3 playerPos;
    bool hasPlayerPos = TryGetLocalPlayerPosition(playerPos);
    float closestDist = 60.0f;
    bool found = false;

    for (auto bObj : baseComps) {
        uint32_t bNetId = ReadComponentNetId(bObj);
        auto it = ownerByNetId.find(bNetId);
        uint32_t owner = (it != ownerByNetId.end()) ? it->second : 0;

        if (bNetId == 0) {
            continue;
        }
        if (it == ownerByNetId.end()) {
            continue;
        }
        if (it->second != selfPlayerNetId) {
            continue;
        }

        Vector3 bPos = { 0, 0, 0 };
        bool hasBPos = TryGetEntityPositionFromComponent(bObj, bPos);
        if (hasPlayerPos && hasBPos) {
            float dx = playerPos.x - bPos.x;
            float dy = playerPos.y - bPos.y;
            float dz = playerPos.z - bPos.z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dist > 60.0f || (found && dist >= closestDist)) continue;
            closestDist = dist;
        } else if (found) continue;

        outBaseNetId = bNetId;
        outBasePos = hasBPos ? bPos : playerPos;
        found = true;
    }
    return found;
}

inline bool TryFindPumpkinRough(uint32_t ignoreRoughNetId, uint32_t& outBaseNetId, uint32_t& outPumpkinNetId, std::string& status) {
    outBaseNetId = 0;
    outPumpkinNetId = 0;
    status = "no rough";

    uint32_t selfPlayerNetId = GetSelfPlayerNetId();
    if (selfPlayerNetId == 0) {
        status = "self netId unavailable";
        return false;
    }

    auto buildComps = GetComponentsOfClass(g_pumpkinCache.buildKlass);
    std::unordered_map<uint32_t, void*> buildByNetId;
    for (auto b : buildComps) {
        uint32_t bNetId = ReadComponentNetId(b);
        if (bNetId != 0) buildByNetId[bNetId] = b;
    }

    auto viewComps = GetComponentsOfClass(g_pumpkinCache.viewKlass);
    if (viewComps.empty()) return false;

    Vector3 playerPos;
    bool hasPlayerPos = TryGetLocalPlayerPosition(playerPos);
    float closestDist = 60.0f;
    bool found = false;

    for (auto v : viewComps) {
        int finishedId = 0; bool rotate = false;
        if (!TryReadPumpkinComponentData(v, finishedId, rotate)) continue;
        if (rotate || finishedId == 0) continue; // must be uncarved rough!

        uint32_t vNetId = ReadComponentNetId(v);
        if (vNetId == 0 || vNetId == ignoreRoughNetId) continue;

        auto it = buildByNetId.find(vNetId);
        if (it == buildByNetId.end()) continue;
        void* bComp = it->second;

        uint32_t ownerId = 0;
        if (!TryPumpkinInvokeUInt(bComp, g_pumpkinCache.buildOwnerMethod, ownerId) || ownerId != selfPlayerNetId) continue;

        uint32_t parentNetId = 0;
        if (!TryPumpkinInvokeUInt(bComp, g_pumpkinCache.buildParentMethod, parentNetId) || parentNetId == 0) continue;

        Vector3 compPos = { 0, 0, 0 };
        if (hasPlayerPos && TryGetEntityPositionFromComponent(v, compPos)) {
            float dx = playerPos.x - compPos.x;
            float dy = playerPos.y - compPos.y;
            float dz = playerPos.z - compPos.z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dist > 60.0f || (found && dist >= closestDist)) continue;
            closestDist = dist;
        } else if (found) continue;

        outBaseNetId = parentNetId;
        outPumpkinNetId = vNetId;
        found = true;
    }

    if (found) {
        status = "rough " + std::to_string(outPumpkinNetId) + " base " + std::to_string(outBaseNetId);
    }
    return found;
}

inline bool TryPumpkinCollectOnce(uint32_t targetNetId, std::string& status) {
    status = "no carved pumpkin";
    uint32_t self = GetSelfPlayerNetId();
    if (self == 0) {
        status = "self netId unavailable";
        return true;
    }

    auto buildComps = GetComponentsOfClass(g_pumpkinCache.buildKlass);
    std::unordered_map<uint32_t, uint32_t> ownerByNetId;
    for (auto b : buildComps) {
        uint32_t bNetId = ReadComponentNetId(b);
        uint32_t owner = 0;
        if (bNetId != 0 && TryPumpkinInvokeUInt(b, g_pumpkinCache.buildOwnerMethod, owner)) {
            ownerByNetId[bNetId] = owner;
        }
    }

    auto finishComps = GetComponentsOfClass(g_pumpkinCache.finishViewKlass);

    uint32_t toCollectNetId = 0;

    for (auto fc : finishComps) {
        bool createByCarving = false;
        bool readCbc = TryReadPumpkinFinishCreateByCarving(fc, createByCarving);
        uint32_t netId = ReadComponentNetId(fc);
        auto it = ownerByNetId.find(netId);
        uint32_t owner = (it != ownerByNetId.end()) ? it->second : 0;

        if (owner != 0 && owner != self) continue;

        if (targetNetId != 0 && netId == targetNetId) {
            toCollectNetId = netId;
            break;
        }

        // Fallback: pick any own carved pumpkin
        if (toCollectNetId == 0 && (readCbc ? createByCarving : true)) {
            toCollectNetId = netId;
        }
    }

    if (toCollectNetId != 0) {
        LOGI("[Pumpkin] TryPumpkinCollectOnce: deleting carved pumpkin netId=%u", toCollectNetId);
        if (!TryPumpkinPoseDelete(toCollectNetId)) {
            status = "collect busy";
            return false;
        }
        status = "collected " + std::to_string(toCollectNetId);
        LOGI("[Pumpkin] TryPumpkinCollectOnce: collected %u successfully", toCollectNetId);
        return true;
    }
    return true;
}

inline bool SendBuildBatchPlaceItem(uint32_t bagItemNetId, uint32_t buildRootNetId, uint64_t putZoneId, const Vector3& localPos, int angleVal, uint64_t virtualLink = 0ULL) {
    if (bagItemNetId == 0 || buildRootNetId == 0) return false;
    bool success = false;

    void* imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol"));
    if (!imgData) imgData = _monoImageLoaded(OBFUSCATE("XDTDataAndProtocol.dll"));
    if (!imgData) return false;

    void* protoKlass = _monoClassFromName(imgData, OBFUSCATE("XDTDataAndProtocol.ProtocolService.Homeland"), OBFUSCATE("HomelandProtocolManager"));
    if (!protoKlass) return false;
    void* m_sendBuild = FindMethodInHierarchy(protoKlass, OBFUSCATE("SendBuildBatchOperation"), 3);
    if (!m_sendBuild) return false;

    void* placeDataKlass = nullptr;
    void* transDataKlass = nullptr;
    const char* imgNames[] = { "EcsClient", "Client", "XDTDataAndProtocol", "XDTLevelAndEntity" };
    for (const char* iname : imgNames) {
        void* simg = _monoImageLoaded(iname);
        if (!simg) simg = _monoImageLoaded((std::string(iname) + ".dll").c_str());
        if (!simg) continue;
        if (!placeDataKlass) placeDataKlass = _monoClassFromName(simg, OBFUSCATE("XDT.Scene.Shared.Modules.Build"), OBFUSCATE("BuildPlaceData"));
        if (!transDataKlass) transDataKlass = _monoClassFromName(simg, OBFUSCATE("XDT.Scene.Shared.Modules.Build"), OBFUSCATE("BuildTransformData"));
    }
    if (!placeDataKlass || !transDataKlass) return false;

    void* rootDomain = _monoGetRootDomain();
    void* transDataObj = _monoObjectNew ? _monoObjectNew(rootDomain, transDataKlass) : nullptr;
    void* placeDataObj = _monoObjectNew ? _monoObjectNew(rootDomain, placeDataKlass) : nullptr;
    if (!transDataObj || !placeDataObj) return false;

    void* corlib = GetCorlibImage();
    void* ulongKlass = corlib ? _monoClassFromName(corlib, OBFUSCATE("System"), OBFUSCATE("UInt64")) : nullptr;
    void* ulongArr = (ulongKlass && rootDomain) ? PumpkinMonoArrayNew(rootDomain, ulongKlass, 1) : nullptr;
    if (ulongArr) {
        uintptr_t arrData = (uintptr_t)ulongArr + 0x20;
        *(uint64_t*)arrData = (virtualLink != 0ULL) ? virtualLink : putZoneId;
    }

    void* f_levelObjNetId = _monoClassGetFieldFromName(transDataKlass, OBFUSCATE("LevelObjectNetId"));
    void* f_localPos = _monoClassGetFieldFromName(transDataKlass, OBFUSCATE("LocalPos"));
    void* f_angle = _monoClassGetFieldFromName(transDataKlass, OBFUSCATE("Angle"));
    void* f_vlinkHasChange = _monoClassGetFieldFromName(transDataKlass, OBFUSCATE("VirtualLinkHasChange"));
    void* f_vlink = _monoClassGetFieldFromName(transDataKlass, OBFUSCATE("VirtualLinkLevelObjectNetId"));

    if (f_levelObjNetId) _monoFieldSetValue(transDataObj, f_levelObjNetId, &putZoneId);
    if (f_localPos) _monoFieldSetValue(transDataObj, f_localPos, const_cast<Vector3*>(&localPos));
    if (f_angle) _monoFieldSetValue(transDataObj, f_angle, &angleVal);
    uint8_t vlinkChangeVal = 1;
    if (f_vlinkHasChange) _monoFieldSetValue(transDataObj, f_vlinkHasChange, &vlinkChangeVal);
    if (f_vlink && ulongArr) _monoFieldSetValue(transDataObj, f_vlink, ulongArr);

    void* f_buildType = _monoClassGetFieldFromName(placeDataKlass, OBFUSCATE("BuildType"));
    void* f_transformData = _monoClassGetFieldFromName(placeDataKlass, OBFUSCATE("TransformData"));
    uint8_t bType = 0;
    if (f_buildType) _monoFieldSetValue(placeDataObj, f_buildType, &bType);
    if (f_transformData) {
        void* unboxedTrans = _monoObjectUnbox(transDataObj);
        if (unboxedTrans) _monoFieldSetValue(placeDataObj, f_transformData, unboxedTrans);
        else _monoFieldSetValue(placeDataObj, f_transformData, transDataObj);
    }

    uint32_t argBag = bagItemNetId;
    uint32_t argRoot = buildRootNetId;
    void* args[] = { &argBag, &argRoot, placeDataObj };
    void* exc = nullptr;
    _monoRuntimeInvoke(m_sendBuild, nullptr, args, &exc);
    if (!exc) {
        success = true;
    }
    return success;
}

inline bool TryResolvePlaceTarget(uint32_t& buildRootNetId, uint64_t& putZoneId, Vector3& localPos, int& angleVal) {
    buildRootNetId = 0;
    putZoneId = 0;
    localPos = { 0, 0, 0 };
    angleVal = 0;

    void* imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity"));
    if (!imgLevel) imgLevel = _monoImageLoaded(OBFUSCATE("XDTLevelAndEntity.dll"));
    if (!imgLevel) return false;

    void* utilKlass = _monoClassFromName(imgLevel, OBFUSCATE("XDTLevelAndEntity.BaseSystem.EntitiesManager"), OBFUSCATE("EntityUtil"));
    if (!utilKlass) return false;
    void* m_getSelf = FindMethodInHierarchy(utilKlass, OBFUSCATE("GetSelfPlayer"), 0);
    if (!m_getSelf) return false;
    void* selfPlayer = _monoRuntimeInvoke(m_getSelf, nullptr, nullptr, nullptr);
    if (!selfPlayer) return false;

    void* spKlass = SafeMonoObjectGetClass(selfPlayer);
    if (!spKlass) return false;
    void* m_getFieldNetId = FindMethodInHierarchy(spKlass, OBFUSCATE("get_inFieldNetId"), 0);
    if (m_getFieldNetId) {
        void* fnObj = _monoRuntimeInvoke(m_getFieldNetId, selfPlayer, nullptr, nullptr);
        if (fnObj) {
            void* unboxed = _monoObjectUnbox(fnObj);
            if (unboxed) buildRootNetId = *(uint32_t*)unboxed;
        }
    }
    if (buildRootNetId == 0) {
        void* f_inFieldNetId = _monoClassGetFieldFromName(spKlass, OBFUSCATE("_inFieldNetId"));
        if (!f_inFieldNetId) f_inFieldNetId = _monoClassGetFieldFromName(spKlass, OBFUSCATE("inFieldNetId"));
        if (f_inFieldNetId) _monoFieldGetValue(selfPlayer, f_inFieldNetId, (void**)&buildRootNetId);
    }
    if (buildRootNetId == 0) return false;

    putZoneId = ((uint64_t)buildRootNetId) | 0x100000000ULL;
    Vector3 playerPos = NativeGetPosition(selfPlayer);
    Vector3 forward = { 0, 0, 1 };

    void* m_getEnt = FindMethodInHierarchy(spKlass, OBFUSCATE("get_entity"), 0);
    if (m_getEnt) {
        void* entObj = _monoRuntimeInvoke(m_getEnt, selfPlayer, nullptr, nullptr);
        if (entObj) {
            void* entKlass = SafeMonoObjectGetClass(entObj);
            void* m_getFwd = entKlass ? FindMethodInHierarchy(entKlass, OBFUSCATE("get_forward"), 0) : nullptr;
            if (m_getFwd) {
                void* fObj = _monoRuntimeInvoke(m_getFwd, entObj, nullptr, nullptr);
                if (fObj) {
                    void* unboxed = _monoObjectUnbox(fObj);
                    if (unboxed) forward = *(Vector3*)unboxed;
                }
            }
        }
    }
    forward.y = 0.0f;
    float fLen = sqrtf(forward.x * forward.x + forward.z * forward.z);
    if (fLen > 0.001f) {
        forward.x /= fLen;
        forward.z /= fLen;
        int yaw = (int)round(atan2f(forward.x, forward.z) * 180.0f / 3.14159265f);
        if (yaw < 0) yaw += 360;
        angleVal = yaw & 0x3FF;
    } else {
        forward = { 0, 0, 1 };
    }

    Vector3 worldTarget = {
        playerPos.x + forward.x * 1.5f,
        playerPos.y,
        playerPos.z + forward.z * 1.5f
    };
    localPos = worldTarget;
    ModMatrix4x4 w2l = ModMatrix4x4::Identity();
    if (TryGetFieldRootWorldToLocal(buildRootNetId, w2l)) {
        localPos = w2l.MultiplyPoint3x4(worldTarget);
    }
    return true;
}

inline bool TryPumpkinAutoPlace(std::string& status) {
    uint32_t selfPlayerNetId = GetSelfPlayerNetId();
    if (selfPlayerNetId == 0) {
        status = "self netId unavailable";
        return false;
    }
    if (TryPumpkinHasOwnRough(selfPlayerNetId)) {
        status = "pumpkin already placed";
        return false;
    }

    uint32_t baseNetId = 0;
    Vector3 basePos = { 0, 0, 0 };
    bool hasBase = TryPumpkinFindOwnBase(selfPlayerNetId, baseNetId, basePos);

    if (!hasBase) {
        // No pumpkin base on ground -> place base from backpack!
        uint32_t bagBaseNetId = 0; int baseSid = 0;
        if (!TryPumpkinFindBagItem(g_pumpkinCache.dictBase, g_pumpkinCache.getBaseMethod, bagBaseNetId, baseSid)) {
            status = "no pumpkin base in backpack";
            return false;
        }
        uint32_t buildRootNetId = 0; uint64_t putZoneId = 0; Vector3 localPos = { 0, 0, 0 }; int angle = 0;
        if (!TryResolvePlaceTarget(buildRootNetId, putZoneId, localPos, angle)) {
            status = "target unresolved (not in field)";
            return false;
        }
        if (!SendBuildBatchPlaceItem(bagBaseNetId, buildRootNetId, putZoneId, localPos, angle, 0ULL)) {
            status = "base place failed";
            return false;
        }
        status = "placed pumpkin base " + std::to_string(baseSid);
        return true;
    }

    // Pumpkin base exists on ground -> place pumpkin rough ON BASE!
    uint32_t bagRoughNetId = 0; int roughSid = 0;
    if (!TryPumpkinFindBagItem(g_pumpkinCache.dictRough, g_pumpkinCache.getRoughMethod, bagRoughNetId, roughSid)) {
        status = "no pumpkin in backpack";
        return false;
    }
    uint32_t buildRootNetId = 0; uint64_t putZoneId = 0; Vector3 dummyPos = { 0, 0, 0 }; int angle = 0;
    if (!TryResolvePlaceTarget(buildRootNetId, putZoneId, dummyPos, angle)) {
        status = "target unresolved";
        return false;
    }
    Vector3 worldTarget = basePos;
    worldTarget.y += 0.5f; // PumpkinRoughAboveBaseMeters = 0.5f
    Vector3 localPos = worldTarget;
    ModMatrix4x4 w2l = ModMatrix4x4::Identity();
    if (TryGetFieldRootWorldToLocal(buildRootNetId, w2l)) {
        localPos = w2l.MultiplyPoint3x4(worldTarget);
    }
    uint64_t virtualLink = ((uint64_t)baseNetId) | 0x100000000ULL;
    if (!SendBuildBatchPlaceItem(bagRoughNetId, buildRootNetId, putZoneId, localPos, angle, virtualLink)) {
        status = "pumpkin place failed";
        return false;
    }
    status = "placed pumpkin " + std::to_string(roughSid) + " on base " + std::to_string(baseNetId);
    return true;
}

// ---------- Main OnUpdate Tick: Auto Pumpkin Carving (100% Identical To PC SawitMod) ----------
inline void ProcessPumpkinCarvingOnUpdate() {
    static PumpkinApiState apiState = PumpkinApiState::FindRough;
    static uint32_t activeBaseNetId = 0;
    static uint32_t activePumpkinNetId = 0;
    static uint32_t ignoreRoughNetId = 0;
    static uint32_t collectNetId = 0;
    static bool collectPending = false;
    static float collectUntil = 0.0f;
    static float waitStartedAt = 0.0f;
    static float sessionDeadline = 0.0f;
    static float nextActionAt = 0.0f;
    static float placeNextAt = 0.0f;
    static bool wasEnabled = false;
    static bool resolved = false;

    // Strict Server-C gate: if session offline or payload wiped, shut down immediately!
    if (!g_isAutoPumpkinActive || !SesiAktif() || g_pumpkinPn.empty()) {
        if (wasEnabled) {
            wasEnabled = false;
            g_pumpkinStatus = "idle";
            activeBaseNetId = 0;
            activePumpkinNetId = 0;
            ignoreRoughNetId = 0;
            collectPending = false;
            apiState = PumpkinApiState::FindRough;
            g_pumpkinCache.resolved = false;
            LOGI("[Pumpkin] Auto Pumpkin Carving disabled/reset.");
        }
        if (!SesiAktif()) {
            g_isAutoPumpkinActive = false;
            g_pumpkinPn.clear();
            g_pumpkinCache.resolved = false;
        }
        return;
    }

    float now = PumpkinGetTimeSec();

    if (!wasEnabled) {
        wasEnabled = true;
        g_pumpkinDoneCount = 0;
        g_pumpkinStatus = "idle";
        apiState = PumpkinApiState::FindRough;
        activeBaseNetId = 0;
        activePumpkinNetId = 0;
        ignoreRoughNetId = 0;
        collectPending = false;
        nextActionAt = 0.0f;
        placeNextAt = 0.0f;
        resolved = TryResolvePumpkinCarving();
        if (!resolved) {
            g_pumpkinStatus = "resolve failed";
            g_isAutoPumpkinActive = false;
            g_pumpkinPn.clear();
            g_pumpkinCache.resolved = false;
            wasEnabled = false;
            LOGW("[Pumpkin] TryResolvePumpkinCarving FAILED (Server payload missing or invalid) -> cannot run");
            return;
        }
        LOGI("[Pumpkin] Auto Pumpkin Carving initialized and active.");
    }

    if (now < nextActionAt) return;

    switch (apiState) {
    case PumpkinApiState::FindRough: {
        if (collectPending) {
            std::string cStatus;
            if (!TryPumpkinCollectOnce(collectNetId, cStatus)) {
                g_pumpkinStatus = cStatus;
                nextActionAt = now + 0.05f;
                break;
            }
            if (cStatus == "no carved pumpkin" && now < collectUntil) {
                nextActionAt = now + 0.05f;
                break;
            }
            collectPending = false;
            collectNetId = 0;
            g_pumpkinStatus = cStatus;
            if (g_autoPumpkinPlace && (cStatus.rfind("collected ", 0) == 0 || cStatus == "no carved pumpkin")) {
                std::string pStatus;
                bool pOk = TryPumpkinAutoPlace(pStatus);
                g_pumpkinStatus = pStatus;
                placeNextAt = now + (pOk ? 1.0f : 2.0f);
                nextActionAt = placeNextAt;
            } else {
                nextActionAt = now;
            }
        } else {
            // If autoPumpkinCollect is on, check if there are any leftover carved pumpkins on ground to collect first!
            if (g_autoPumpkinCollect) {
                std::string cStatus;
                if (TryPumpkinCollectOnce(0, cStatus) && cStatus.rfind("collected ", 0) == 0) {
                    g_pumpkinStatus = cStatus;
                    nextActionAt = now + 0.25f;
                    break;
                }
            }

            uint32_t bNetId = 0; uint32_t pNetId = 0;
            std::string fStatus;
            if (!TryFindPumpkinRough(ignoreRoughNetId, bNetId, pNetId, fStatus)) {
                if (g_autoPumpkinPlace && fStatus == "no rough" && now >= placeNextAt) {
                    std::string pStatus;
                    bool pOk = TryPumpkinAutoPlace(pStatus);
                    placeNextAt = now + (pOk ? 1.0f : 0.25f);
                    g_pumpkinStatus = pStatus;
                    nextActionAt = placeNextAt;
                } else if (now < placeNextAt) {
                    nextActionAt = placeNextAt;
                } else {
                    g_pumpkinStatus = fStatus;
                    nextActionAt = now + 0.25f;
                }
            } else {
                activeBaseNetId = bNetId;
                activePumpkinNetId = pNetId;
                g_pumpkinStatus = fStatus;
                apiState = PumpkinApiState::Start;
            }
        }
        break;
    }
    case PumpkinApiState::Start: {
        if (!TryPumpkinStart(activeBaseNetId, activePumpkinNetId)) {
            g_pumpkinStatus = "Start failed";
            activePumpkinNetId = 0;
            activeBaseNetId = 0;
            apiState = PumpkinApiState::FindRough;
            nextActionAt = now + 0.35f;
        } else if (!TryPumpkinFinish(activePumpkinNetId)) {
            g_pumpkinStatus = "Finish failed";
            activePumpkinNetId = 0;
            activeBaseNetId = 0;
            apiState = PumpkinApiState::FindRough;
            nextActionAt = now + 0.35f;
        } else {
            g_pumpkinStatus = "Carving: WaitResult";
            apiState = PumpkinApiState::WaitResult;
            waitStartedAt = now;
            sessionDeadline = waitStartedAt + 8.0f;
        }
        break;
    }
    case PumpkinApiState::WaitResult: {
        bool stillCarvable = TryPumpkinActiveRoughStillCarvable(activePumpkinNetId);
        if (stillCarvable && now < sessionDeadline) {
            nextActionAt = now + 0.1f;
            break;
        }
        bool success = !stillCarvable;
        if (success) {
            g_pumpkinDoneCount++;
            g_pumpkinStatus = "done " + std::to_string(g_pumpkinDoneCount);
        } else {
            g_pumpkinStatus = "result timeout";
        }

        uint32_t finishedNetId = activePumpkinNetId;
        if (activePumpkinNetId != 0) {
            ignoreRoughNetId = activePumpkinNetId;
        }
        activePumpkinNetId = 0;
        activeBaseNetId = 0;
        apiState = PumpkinApiState::FindRough;

        if (success && g_autoPumpkinCollect) {
            collectNetId = finishedNetId;
            collectPending = true;
            collectUntil = now + 2.0f;
            nextActionAt = now + 0.1f;
        } else if (success && g_autoPumpkinPlace) {
            std::string pStatus;
            bool pOk = TryPumpkinAutoPlace(pStatus);
            g_pumpkinStatus = pStatus;
            placeNextAt = now + (pOk ? 1.0f : 2.0f);
            nextActionAt = placeNextAt;
        } else {
            nextActionAt = now + 0.25f;
        }
        break;
    }
    }
}

inline void RunAutoPumpkinCarvingLoop() {
    SMPlatform::SetBreadcrumbLight("pumpkin:loop");
    InvokeRequest req = FetchPayloadSync(WireForOp(OP_AUTO_PUMPKIN));
    if (req.op != OP_AUTO_PUMPKIN || req.methodDesc != "CUSTOM_AUTO_PUMPKIN" ||
        req.payloadRaw.empty() || !SesiAktif()) {
        LOGI("[Pumpkin] Server authorization FAILED or offline -> aborting Auto Pumpkin\n");
        g_pumpkinStatus = "Server payload unavailable (offline)";
        g_isAutoPumpkinActive = false;
        g_pumpkinPn.clear();
        g_pumpkinCache.resolved = false;
        return;
    }
    g_pumpkinPn = req.payloadRaw;
    g_pumpkinStatus = "idle";
    g_isAutoPumpkinActive = true;
}

inline void RunAutoPumpkinCarvingOnce() {
    RunAutoPumpkinCarvingLoop();
}
