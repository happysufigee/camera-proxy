#include "remix_interface.h"

#include "remixapi/bridge_remix_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
static float ClampPositive(float v, float fallback) {
    return (std::isfinite(v) && v > 0.0f) ? v : fallback;
}
}

RemixInterface::~RemixInterface() {
    Shutdown();
}

void RemixInterface::WriteStatus(const char* msg) {
    if (!msg) return;
    std::snprintf(m_lastStatus, sizeof(m_lastStatus), "%s", msg);
}

bool RemixInterface::IsValidDllName(const char* remixDllName) const {
    if (!remixDllName || !remixDllName[0]) return false;
    const char* ext = std::strrchr(remixDllName, '.');
    if (!ext) return false;
#ifdef _WIN32
    return _stricmp(ext, ".dll") == 0;
#else
    return std::strcmp(ext, ".dll") == 0;
#endif
}

bool RemixInterface::BuildLightInfo(const RemixLightDesc& desc,
                                    uint64_t stableHash,
                                    remixapi_LightInfo* outInfo,
                                    remixapi_LightInfoSphereEXT* outSphere,
                                    remixapi_LightInfoDistantEXT* outDistant) const {
    if (!outInfo || !outSphere || !outDistant) return false;

    *outInfo = {};
    *outSphere = {};
    *outDistant = {};

    outInfo->sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
    outInfo->pNext = nullptr;
    outInfo->hash = stableHash;
    outInfo->radiance = {
        ClampPositive(desc.color[0] * desc.intensity, 0.0f),
        ClampPositive(desc.color[1] * desc.intensity, 0.0f),
        ClampPositive(desc.color[2] * desc.intensity, 0.0f)
    };

    if (desc.type == RemixLightType::Directional) {
        outDistant->sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
        outDistant->direction = { desc.direction[0], desc.direction[1], desc.direction[2] };
        outDistant->angularDiameterDegrees = 0.5f;
        outDistant->volumetricRadianceScale = 1.0f;
        outInfo->pNext = outDistant;
        return true;
    }

    outSphere->sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
    outSphere->position = { desc.position[0], desc.position[1], desc.position[2] };
    outSphere->radius = ClampPositive(desc.range, 1.0f);
    outSphere->volumetricRadianceScale = 1.0f;
    outSphere->shaping_hasvalue = 0;

    if (desc.type == RemixLightType::Spot) {
        outSphere->shaping_hasvalue = 1;
        outSphere->shaping_value.direction = { desc.direction[0], desc.direction[1], desc.direction[2] };
        outSphere->shaping_value.coneAngleDegrees = ClampPositive(desc.coneAngle, 45.0f);
        outSphere->shaping_value.coneSoftness = 0.0f;
        outSphere->shaping_value.focusExponent = 1.0f;
    }

    outInfo->pNext = outSphere;
    return true;
}

bool RemixInterface::Initialize(const char* remixDllName) {
    if (m_runtimeReady) return true;

    if (!IsValidDllName(remixDllName)) {
        WriteStatus("Invalid RemixDllName in camera_proxy.ini (must be a .dll file name).");
        return true;
    }

    wchar_t widePath[MAX_PATH] = {};
    const int converted = MultiByteToWideChar(CP_UTF8, 0, remixDllName, -1, widePath, MAX_PATH);
    if (converted <= 0) {
        const int ansi = MultiByteToWideChar(CP_ACP, 0, remixDllName, -1, widePath, MAX_PATH);
        if (ansi <= 0) {
            WriteStatus("Failed to convert RemixDllName to UTF-16.");
            return true;
        }
    }

    remixapi_Interface api = {};
    HMODULE remixDll = nullptr;
    remixapi_ErrorCode status = remixapi_lib_loadRemixDllAndInitialize(widePath, &api, &remixDll);
    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
        // fallback for case where Remix is already bound to d3d9 export module
        status = remixapi::bridge_initRemixApi(&api);
        if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
            WriteStatus("Remix API init failed; lighting forwarding in safe fallback mode.");
            return true;
        }
    }

    m_api = api;
    m_remixDllModule = remixDll;
    m_runtimeReady = (m_api.CreateLight && m_api.DestroyLight && m_api.DrawLightInstance && m_api.Present);
    WriteStatus(m_runtimeReady ? "Remix API initialized." : "Remix API incomplete; fallback mode.");
    return true;
}

void RemixInterface::BeginFrame() {
    if (!m_runtimeReady || !m_api.Startup || m_started) return;
    remixapi_StartupInfo info = {};
    info.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
    const remixapi_ErrorCode status = m_api.Startup(&info);
    if (status == REMIXAPI_ERROR_CODE_SUCCESS) {
        m_started = true;
    }
}

void RemixInterface::EndFrame() {
    if (!m_runtimeReady || !m_started || !m_api.Present) return;
    remixapi_PresentInfo presentInfo = {};
    presentInfo.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
    m_api.Present(&presentInfo);
}

void RemixInterface::Shutdown() {
    if (!m_runtimeReady) return;

    for (auto& kv : m_liveHandles) {
        if (kv.second && m_api.DestroyLight) {
            m_api.DestroyLight(kv.second);
        }
    }
    m_liveHandles.clear();

    if (m_api.Shutdown && m_started) {
        m_api.Shutdown();
    }
    m_started = false;

    if (m_remixDllModule) {
        FreeLibrary(m_remixDllModule);
        m_remixDllModule = nullptr;
    }

    m_api = {};
    m_runtimeReady = false;
}

RemixLightHandle RemixInterface::CreateLight(const RemixLightDesc& desc, uint64_t stableHash) {
    RemixLightHandle logical = m_mockHandleCounter++;
    if (!m_runtimeReady) return logical;

    remixapi_LightInfo info = {};
    remixapi_LightInfoSphereEXT sphere = {};
    remixapi_LightInfoDistantEXT distant = {};
    if (!BuildLightInfo(desc, stableHash, &info, &sphere, &distant)) return 0;

    remixapi_LightHandle nativeHandle = nullptr;
    if (m_api.CreateLight(&info, &nativeHandle) != REMIXAPI_ERROR_CODE_SUCCESS || !nativeHandle) return 0;

    m_liveHandles[logical] = nativeHandle;
    return logical;
}

bool RemixInterface::UpdateLight(RemixLightHandle handle, const RemixLightDesc& desc, uint64_t stableHash) {
    if (handle == 0) return false;
    if (!m_runtimeReady) return true;

    auto it = m_liveHandles.find(handle);
    if (it == m_liveHandles.end()) return false;

    remixapi_LightInfo info = {};
    remixapi_LightInfoSphereEXT sphere = {};
    remixapi_LightInfoDistantEXT distant = {};
    if (!BuildLightInfo(desc, stableHash, &info, &sphere, &distant)) return false;

    remixapi_LightHandle newHandle = nullptr;
    if (m_api.CreateLight(&info, &newHandle) != REMIXAPI_ERROR_CODE_SUCCESS || !newHandle) {
        return false;
    }

    if (m_api.DestroyLight(it->second) != REMIXAPI_ERROR_CODE_SUCCESS) {
        m_api.DestroyLight(newHandle);
        return false;
    }

    it->second = newHandle;
    return true;
}

bool RemixInterface::DestroyLight(RemixLightHandle handle) {
    if (handle == 0) return false;
    if (!m_runtimeReady) return true;

    auto it = m_liveHandles.find(handle);
    if (it == m_liveHandles.end()) return true;

    const bool ok = (m_api.DestroyLight(it->second) == REMIXAPI_ERROR_CODE_SUCCESS);
    m_liveHandles.erase(it);
    return ok;
}

bool RemixInterface::DrawLight(RemixLightHandle handle) {
    if (handle == 0) return false;
    if (!m_runtimeReady) return true;

    auto it = m_liveHandles.find(handle);
    if (it == m_liveHandles.end()) return false;
    return m_api.DrawLightInstance(it->second) == REMIXAPI_ERROR_CODE_SUCCESS;
}
