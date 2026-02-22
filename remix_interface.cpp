#include "remix_interface.h"

#include "remix_logger.h"

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
    if (m_runtimeReady) {
        RemixLog("Initialize: already ready, skipping.");
        return true;
    }

    RemixLog("=== RemixInterface::Initialize BEGIN ===");
    RemixLog("remixDllName = '%s'", remixDllName ? remixDllName : "<null>");

    remixapi_Interface api = {};
    HMODULE dll = nullptr;
    remixapi_ErrorCode status = REMIXAPI_ERROR_CODE_LOAD_LIBRARY_FAILURE;

    if (IsValidDllName(remixDllName)) {
        wchar_t wide[MAX_PATH] = {};
        int conv = MultiByteToWideChar(CP_UTF8, 0, remixDllName, -1, wide, MAX_PATH);
        if (conv <= 0)
            conv = MultiByteToWideChar(CP_ACP, 0, remixDllName, -1, wide, MAX_PATH);

        if (conv > 0) {
            DWORD attr = GetFileAttributesW(wide);
            RemixLog("Path A: file '%s' attributes = 0x%08X (%s)", remixDllName, attr,
                     attr != INVALID_FILE_ATTRIBUTES ? "FILE EXISTS" : "FILE NOT FOUND");

            if (attr != INVALID_FILE_ATTRIBUTES) {
                RemixLog("Path A: calling remixapi_lib_loadRemixDllAndInitialize...");
                status = remixapi_lib_loadRemixDllAndInitialize(wide, &api, &dll);
                RemixLog("Path A result: %d  dll=%p", static_cast<int>(status), dll);
            }
        } else {
            RemixLog("Path A: wide-string conversion failed.");
        }
    } else {
        RemixLog("Path A: skipped - no valid DLL name (got: '%s').", remixDllName ? remixDllName : "<null>");
    }

    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
        HMODULE hD3D9 = GetModuleHandleA("d3d9.dll");
        HMODULE hRmx = GetModuleHandleA("d3d9_remix.dll");
        RemixLog("Path B: bridge_initRemixApi()");
        RemixLog("  d3d9.dll       = %p %s", hD3D9, hD3D9 ? "(in process)" : "(NOT IN PROCESS)");
        RemixLog("  d3d9_remix.dll = %p %s", hRmx, hRmx ? "(in process)" : "(not in process)");
        RemixLog("  NOTE: .trex/bridge.conf must have 'exposeRemixApi = True'");

        api = {};
        status = remixapi::bridge_initRemixApi(&api);
        RemixLog("Path B result: %d", static_cast<int>(status));

        if (status == REMIXAPI_ERROR_CODE_GET_PROC_ADDRESS_FAILURE) {
            RemixLog("  => d3d9.dll found but does NOT export remixapi_InitializeLibrary");
            RemixLog("     Fix: set 'exposeRemixApi = True' in .trex/bridge.conf");
        } else if (status == REMIXAPI_ERROR_CODE_LOAD_LIBRARY_FAILURE) {
            RemixLog("  => d3d9.dll not in process yet - Initialize() called too early");
        }
    }

    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
        RemixLog("Path C: direct GetProcAddress on d3d9_remix.dll...");
        HMODULE hDirect = GetModuleHandleA("d3d9_remix.dll");
        if (!hDirect) hDirect = LoadLibraryA("d3d9_remix.dll");
        RemixLog("  d3d9_remix.dll handle = %p", hDirect);
        if (hDirect) {
            using PFN_Init = remixapi_ErrorCode(REMIXAPI_CALL*)(const remixapi_InitializeLibraryInfo*, remixapi_Interface*);
            auto pfn = reinterpret_cast<PFN_Init>(GetProcAddress(hDirect, "remixapi_InitializeLibrary"));
            RemixLog("  remixapi_InitializeLibrary = %p", pfn);
            if (pfn) {
                const remixapi_InitializeLibraryInfo info{REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO,
                                                          nullptr,
                                                          REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR,
                                                                                REMIXAPI_VERSION_MINOR,
                                                                                REMIXAPI_VERSION_PATCH)};
                api = {};
                status = pfn(&info, &api);
                RemixLog("Path C result: %d", static_cast<int>(status));
            }
        }
    }

    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
        RemixLog("FATAL: all init paths failed. Safe fallback mode.");
        WriteStatus("Remix API init failed; safe fallback mode.");
        return true;
    }

    m_api = api;
    m_remixDllModule = dll;
    m_usingBridgeMode = (dll == nullptr);

    RemixLog("Init succeeded. Bridge mode: %s", m_usingBridgeMode ? "YES" : "NO");
    RemixLog("Function pointers:");
    RemixLog("  Startup           = %p", m_api.Startup);
    RemixLog("  Shutdown          = %p", m_api.Shutdown);
    RemixLog("  Present           = %p", m_api.Present);
    RemixLog("  CreateLight       = %p", m_api.CreateLight);
    RemixLog("  DestroyLight      = %p", m_api.DestroyLight);
    RemixLog("  DrawLightInstance = %p", m_api.DrawLightInstance);

    m_runtimeReady = (m_api.Startup && m_api.CreateLight && m_api.DestroyLight && m_api.DrawLightInstance && m_api.Present);

    if (m_runtimeReady) {
        RemixLog("m_runtimeReady = true");
        RemixLog("NEXT: WrappedD3D9Device constructor must call SetHwnd() before first BeginScene");
        WriteStatus("Remix API initialized - awaiting SetHwnd.");
    } else {
        RemixLog("ERROR: m_runtimeReady = false - one or more required ptrs null.");
        WriteStatus("Remix API incomplete; fallback mode.");
    }

    RemixLog("=== RemixInterface::Initialize END ===");
    return true;
}

void RemixInterface::SetHwnd(HWND hwnd) {
    m_hwnd = hwnd;
    RemixLog("SetHwnd: hwnd=%p", hwnd);
}

void RemixInterface::BeginFrame() {
    if (!m_runtimeReady) return;
    if (m_started) return;

    if (!m_api.Startup) {
        RemixLog("BeginFrame: Startup ptr is null.");
        return;
    }
    if (!m_hwnd) {
        RemixLog("BeginFrame: m_hwnd is null - WrappedD3D9Device not yet created, skipping Startup.");
        return;
    }

    RemixLog("BeginFrame: calling Startup(hwnd=%p)...", m_hwnd);
    remixapi_StartupInfo info = {};
    info.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
    info.hwnd = m_hwnd;
    info.disableSrgbConversionForOutput = FALSE;
    info.forceNoVkSwapchain = FALSE;

    const remixapi_ErrorCode status = m_api.Startup(&info);
    RemixLog("BeginFrame: Startup returned %d", static_cast<int>(status));
    if (status == REMIXAPI_ERROR_CODE_SUCCESS) {
        m_started = true;
        RemixLog("BeginFrame: m_started = true - Remix renderer is now active.");
    } else {
        RemixLog("ERROR: Startup failed. Lights will not render until this succeeds.");
    }
}

void RemixInterface::EndFrame() {
    if (!m_runtimeReady || !m_started) return;

    if (m_usingBridgeMode) {
        return;
    }
    if (!m_api.Present) {
        RemixLog("EndFrame: Present ptr is null.");
        return;
    }

    const remixapi_ErrorCode status = m_api.Present(nullptr);
    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
        RemixLog("EndFrame: Present(NULL) failed, code=%d", static_cast<int>(status));
    }
}

void RemixInterface::Shutdown() {
    RemixLog("Shutdown: runtimeReady=%d started=%d liveHandles=%zu", m_runtimeReady, m_started, m_liveHandles.size());
    if (!m_runtimeReady) {
        RemixLog("Shutdown: nothing to do.");
        return;
    }

    for (auto& kv : m_liveHandles) {
        if (kv.second && m_api.DestroyLight) {
            m_api.DestroyLight(kv.second);
        }
    }
    m_liveHandles.clear();

    if (m_remixDllModule && m_started) {
        RemixLog("Shutdown: calling remixapi_lib_shutdownAndUnloadRemixDll...");
        remixapi_lib_shutdownAndUnloadRemixDll(&m_api, m_remixDllModule);
        m_remixDllModule = nullptr;
    } else if (m_api.Shutdown && m_started) {
        RemixLog("Shutdown: bridge mode - calling m_api.Shutdown()");
        m_api.Shutdown();
    }

    m_started = false;
    m_api = {};
    m_runtimeReady = false;
    RemixLog("Shutdown: complete.");
}

RemixLightHandle RemixInterface::CreateLight(const RemixLightDesc& desc, uint64_t stableHash) {
    RemixLightHandle logical = m_mockHandleCounter++;
    if (!m_runtimeReady) {
        if (m_lightLogCount++ < 10)
            RemixLog("CreateLight: runtime not ready, mock handle=%llu", logical);
        return logical;
    }

    remixapi_LightInfo info = {};
    remixapi_LightInfoSphereEXT sphere = {};
    remixapi_LightInfoDistantEXT distant = {};
    if (!BuildLightInfo(desc, stableHash, &info, &sphere, &distant)) return 0;

    remixapi_LightHandle nativeHandle = nullptr;
    remixapi_ErrorCode r = m_api.CreateLight(&info, &nativeHandle);
    if (m_lightLogCount++ < 10)
        RemixLog("CreateLight[%u]: hash=%llu status=%d native=%p", m_lightLogCount, stableHash, static_cast<int>(r), nativeHandle);
    if (r != REMIXAPI_ERROR_CODE_SUCCESS || !nativeHandle) return 0;

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
    remixapi_ErrorCode createStatus = m_api.CreateLight(&info, &newHandle);
    if (createStatus != REMIXAPI_ERROR_CODE_SUCCESS || !newHandle) {
        if (m_lightLogCount++ < 10)
            RemixLog("UpdateLight: CreateLight failed handle=%llu status=%d", handle, static_cast<int>(createStatus));
        return false;
    }

    remixapi_ErrorCode destroyStatus = m_api.DestroyLight(it->second);
    if (destroyStatus != REMIXAPI_ERROR_CODE_SUCCESS) {
        if (m_lightLogCount++ < 10)
            RemixLog("UpdateLight: DestroyLight failed handle=%llu status=%d", handle, static_cast<int>(destroyStatus));
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

    remixapi_ErrorCode status = m_api.DestroyLight(it->second);
    const bool ok = (status == REMIXAPI_ERROR_CODE_SUCCESS);
    if (!ok && m_lightLogCount++ < 10)
        RemixLog("DestroyLight: failed handle=%llu status=%d", handle, static_cast<int>(status));
    m_liveHandles.erase(it);
    return ok;
}

bool RemixInterface::DrawLight(RemixLightHandle handle) {
    if (handle == 0) return false;
    if (!m_runtimeReady) return true;

    auto it = m_liveHandles.find(handle);
    if (it == m_liveHandles.end()) return false;
    remixapi_ErrorCode status = m_api.DrawLightInstance(it->second);
    if (status != REMIXAPI_ERROR_CODE_SUCCESS && m_lightLogCount++ < 10)
        RemixLog("DrawLight: failed handle=%llu status=%d", handle, static_cast<int>(status));
    return status == REMIXAPI_ERROR_CODE_SUCCESS;
}
