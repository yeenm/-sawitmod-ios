#include "ModFeatures.h"
#include "ModUtils.h"
#include "Global.h"
#include "NetAuth.h"
#include "PayloadVM.h"
#include "Includes/obfuscate.h"
#include "Includes/Logger.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace {

// =========================================================
// CAMERA CONTROLS (FOV, Distance, Offset, Roll, NoClip)
// Logika 1:1 SawitMod PC + Android Safety Guard
// =========================================================
struct CameraModCache {
    bool initialized = false;
    uint64_t lastFetchAttempt = 0;
    std::string lastDiagnostic = "Initializing...";

    void *xdtCamMgrKlass = nullptr;
    void *m_get_inst = nullptr;
    void *m_get_current_ctrl = nullptr;
    void *f_current_ctrl = nullptr;
    void *followCtrlKlass = nullptr;
    void *m_setFov = nullptr;
    void *m_setDist = nullptr;
    void *m_setFocusLocalOffset = nullptr;
    void *m_setRollAngle = nullptr;
    void *f_ignoreCollision = nullptr;
    void *f_rollAngle = nullptr;
    void *cameraKlass = nullptr;
    void *m_set_fieldOfView = nullptr;
};

CameraModCache g_camModCache;
std::string g_cachedControllerName;

inline uint64_t GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

void *GetFollowCameraControllerNative() {
    void *domain = _monoGetRootDomain ? _monoGetRootDomain() : nullptr;
    if (domain && _monoThreadAttach) {
        _monoThreadAttach(domain);
    }

    if (!g_camModCache.initialized) {
        uint64_t now = GetCurrentTimeMs();
        if (now - g_camModCache.lastFetchAttempt < 1500) return nullptr;
        g_camModCache.lastFetchAttempt = now;

        if (!SesiAktif()) {
            g_camModCache.lastDiagnostic = "Waiting login session";
            return nullptr;
        }

        // 1. Resolve Assembly / Image (Verified dari IL dump)
        void *imgLevel = _monoImageLoaded ? _monoImageLoaded("XDTLevelAndEntity") : nullptr;
        if (!imgLevel && _monoImageLoaded) {
            imgLevel = _monoImageLoaded("XDTLevelAndEntity.dll");
        }
        if (!imgLevel) {
            g_camModCache.lastDiagnostic = "Waiting XDTLevelAndEntity assembly";
            return nullptr;
        }

        // 2. Resolve XDTCameraManager
        if (_monoClassFromName) {
            g_camModCache.xdtCamMgrKlass = _monoClassFromName(imgLevel,
                "ScriptsRefactory.LevelAndEntity.BaseSystem.CameraManager", "XDTCameraManager");
            if (!g_camModCache.xdtCamMgrKlass) {
                g_camModCache.xdtCamMgrKlass = _monoClassFromName(imgLevel,
                    "XDTLevelAndEntity.BaseSystem.CameraManager", "XDTCameraManager");
            }
        }

        if (!g_camModCache.xdtCamMgrKlass) {
            g_camModCache.lastDiagnostic = "Class XDTCameraManager not found";
            return nullptr;
        }

        // 3. Resolve Manager methods (get_Instance & get_CurrentController) + backing field
        g_camModCache.m_get_inst = FindMethodInHierarchy(g_camModCache.xdtCamMgrKlass, "get_Instance", 0);
        g_camModCache.m_get_current_ctrl = FindMethodInHierarchy(g_camModCache.xdtCamMgrKlass, "get_CurrentController", 0);
        if (!g_camModCache.m_get_current_ctrl) {
            g_camModCache.m_get_current_ctrl = FindMethodInHierarchy(g_camModCache.xdtCamMgrKlass, "get_CurrentCameraController", 0);
        }

        if (_monoClassGetFieldFromName) {
            g_camModCache.f_current_ctrl = _monoClassGetFieldFromName(g_camModCache.xdtCamMgrKlass, "_currentController");
        }

        // 4. Resolve FollowCameraController class & methods
        if (_monoClassFromName) {
            g_camModCache.followCtrlKlass = _monoClassFromName(imgLevel,
                "ScriptsRefactory.LevelAndEntity.BaseSystem.CameraManager.Controller", "FollowCameraController");
            if (!g_camModCache.followCtrlKlass) {
                g_camModCache.followCtrlKlass = _monoClassFromName(imgLevel,
                    "ScriptsRefactory.LevelAndEntity.BaseSystem.CameraManager", "FollowCameraController");
            }
        }

        if (g_camModCache.followCtrlKlass) {
            g_camModCache.m_setFov = FindMethodInHierarchy(g_camModCache.followCtrlKlass, "SetFov", 1);
            g_camModCache.m_setDist = FindMethodInHierarchy(g_camModCache.followCtrlKlass, "SetDistance", 1);
            g_camModCache.m_setFocusLocalOffset = FindMethodInHierarchy(g_camModCache.followCtrlKlass, "SetFocusLocalOffset", 1);
            g_camModCache.m_setRollAngle = FindMethodInHierarchy(g_camModCache.followCtrlKlass, "SetRollAngle", 1);

            if (_monoClassGetFieldFromName) {
                g_camModCache.f_ignoreCollision = _monoClassGetFieldFromName(g_camModCache.followCtrlKlass, "ignoreCollision");
                g_camModCache.f_rollAngle = _monoClassGetFieldFromName(g_camModCache.followCtrlKlass, "_rollAngle");
            }
        }

        // 5. Fallback Camera.main set_fieldOfView
        void *imgCore = _monoImageLoaded ? _monoImageLoaded("UnityEngine.CoreModule") : nullptr;
        if (!imgCore && _monoImageLoaded) imgCore = _monoImageLoaded("UnityEngine.CoreModule.dll");
        if (!imgCore && _monoImageLoaded) imgCore = _monoImageLoaded("EngineWrapper");
        if (!imgCore && _monoImageLoaded) imgCore = _monoImageLoaded("UnityEngine");
        if (imgCore && _monoClassFromName) {
            g_camModCache.cameraKlass = _monoClassFromName(imgCore, "UnityEngine", "Camera");
            if (g_camModCache.cameraKlass) {
                g_camModCache.m_set_fieldOfView = FindMethodInHierarchy(g_camModCache.cameraKlass, "set_fieldOfView", 1);
            }
        }

        g_camModCache.initialized = true;
        g_camModCache.lastDiagnostic = "Ready";
    }

    // 6. Dapatkan XDTCameraManager instance via get_Instance (persis PC)
    if (!g_camModCache.m_get_inst || !_monoRuntimeInvoke) return nullptr;
    void *exc = nullptr;
    void *mgrInst = _monoRuntimeInvoke(g_camModCache.m_get_inst, nullptr, nullptr, &exc);
    if (exc || !mgrInst || (uintptr_t)mgrInst < 0x10000) {
        g_camModCache.lastDiagnostic = "Waiting in-game camera manager";
        return nullptr;
    }

    // 7. Dapatkan controller instance (get_CurrentController atau field _currentController)
    void *ctrlInst = nullptr;
    if (g_camModCache.m_get_current_ctrl) {
        exc = nullptr;
        ctrlInst = _monoRuntimeInvoke(g_camModCache.m_get_current_ctrl, mgrInst, nullptr, &exc);
        if (exc) ctrlInst = nullptr;
    }

    if (!ctrlInst && g_camModCache.f_current_ctrl && _monoFieldGetValue) {
        _monoFieldGetValue(mgrInst, g_camModCache.f_current_ctrl, &ctrlInst);
    }

    if (!ctrlInst || (uintptr_t)ctrlInst < 0x10000) {
        g_camModCache.lastDiagnostic = "Waiting active camera controller";
        return nullptr;
    }

    // 8. Resolusi dinamis actual class controller
    if (_monoObjectGetClass) {
        void *actualKlass = _monoObjectGetClass(ctrlInst);
        if (actualKlass && (uintptr_t)actualKlass >= 0x10000) {
            if (actualKlass != g_camModCache.followCtrlKlass) {
                g_camModCache.followCtrlKlass = actualKlass;
                if (!g_camModCache.m_setFov) g_camModCache.m_setFov = FindMethodInHierarchy(actualKlass, "SetFov", 1);
                if (!g_camModCache.m_setDist) g_camModCache.m_setDist = FindMethodInHierarchy(actualKlass, "SetDistance", 1);
                if (!g_camModCache.m_setFocusLocalOffset) g_camModCache.m_setFocusLocalOffset = FindMethodInHierarchy(actualKlass, "SetFocusLocalOffset", 1);
                if (!g_camModCache.m_setRollAngle) g_camModCache.m_setRollAngle = FindMethodInHierarchy(actualKlass, "SetRollAngle", 1);
                if (_monoClassGetFieldFromName) {
                    if (!g_camModCache.f_ignoreCollision) g_camModCache.f_ignoreCollision = _monoClassGetFieldFromName(actualKlass, "ignoreCollision");
                    if (!g_camModCache.f_rollAngle) g_camModCache.f_rollAngle = _monoClassGetFieldFromName(actualKlass, "_rollAngle");
                }
            }
            if (_monoClassGetName && g_cachedControllerName.empty()) {
                const char *cn = _monoClassGetName(actualKlass);
                if (cn) g_cachedControllerName = cn;
            }
        }
    }

    g_camModCache.lastDiagnostic = "Connected";
    return ctrlInst;
}

void ApplyCameraSettingsNative() {
    if (!g_cameraModActive) return;

    try {
        void *ctrlInst = GetFollowCameraControllerNative();
        if (ctrlInst && (uintptr_t)ctrlInst >= 0x10000 && _monoRuntimeInvoke) {
            void *exc = nullptr;

            // 1. FOV
            if (g_cameraFovActive && g_camModCache.m_setFov) {
                float fov = g_cameraFov;
                void *args[] = { &fov };
                exc = nullptr;
                _monoRuntimeInvoke(g_camModCache.m_setFov, ctrlInst, args, &exc);
            }

            // 2. Distance Multiplier
            if (g_cameraDistActive && g_camModCache.m_setDist) {
                float dist = g_cameraDist;
                void *args[] = { &dist };
                exc = nullptr;
                _monoRuntimeInvoke(g_camModCache.m_setDist, ctrlInst, args, &exc);
            }

            // 3. Focus Offset (Shoulder / Height / Forward)
            if (g_cameraOffsetActive && g_camModCache.m_setFocusLocalOffset) {
                Vector3 offset(g_cameraOffsetX, g_cameraOffsetY, g_cameraOffsetZ);
                void *args[] = { &offset };
                exc = nullptr;
                _monoRuntimeInvoke(g_camModCache.m_setFocusLocalOffset, ctrlInst, args, &exc);
            }

            // 4. Roll Angle (method invoke)
            if (g_cameraRollActive && g_camModCache.m_setRollAngle) {
                float roll = g_cameraRoll;
                void *args[] = { &roll };
                exc = nullptr;
                _monoRuntimeInvoke(g_camModCache.m_setRollAngle, ctrlInst, args, &exc);
            }

            // 5. No Clip / Ignore Collision (Hanya ditulis jika fitur aktif & field valid)
            if (g_cameraNoClipActive && g_camModCache.f_ignoreCollision && _monoFieldGetOffset) {
                int off = _monoFieldGetOffset(g_camModCache.f_ignoreCollision);
                if (off > 0) {
                    *reinterpret_cast<bool*>((uintptr_t)ctrlInst + off) = true;
                }
            }
        }

        // Companion / Fallback Camera.main set_fieldOfView
        if (g_cameraFovActive && g_camModCache.m_set_fieldOfView && _monoRuntimeInvoke) {
            void *mainCam = GetMainCameraNative();
            if (mainCam && (uintptr_t)mainCam >= 0x10000) {
                float fov = g_cameraFov;
                void *args[] = { &fov };
                void *exc = nullptr;
                _monoRuntimeInvoke(g_camModCache.m_set_fieldOfView, mainCam, args, &exc);
            }
        }
    } catch (...) {
        LOGE(OBFUSCATE("[CameraMod] Exception in ApplyCameraSettingsNative"));
    }
}

void ResetCameraSettings() {
    g_cameraModActive = false;
    g_cameraFovActive = false;
    g_cameraFov = 60.0f;
    g_cameraDistActive = false;
    g_cameraDist = 1.0f;
    g_cameraOffsetActive = false;
    g_cameraOffsetX = 0.0f;
    g_cameraOffsetY = 0.0f;
    g_cameraOffsetZ = 0.0f;
    g_cameraRollActive = false;
    g_cameraRoll = 0.0f;
    g_cameraNoClipActive = false;
    g_cachedControllerName.clear();
}

std::string GetCameraStatusString() {
    if (!g_cameraModActive) return "Disabled";
    if (g_camModCache.lastDiagnostic == "Connected") {
        if (!g_cachedControllerName.empty()) {
            return "Connected [" + g_cachedControllerName + "]";
        }
        return "Connected";
    }
    return g_camModCache.lastDiagnostic;
}
