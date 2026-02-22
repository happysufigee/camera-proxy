#pragma once

#include <windows.h>
#include <cstdint>
#include <unordered_map>

#include "remixapi/bridge_remix_api.h"

using RemixLightHandle = uint64_t;

enum class RemixLightType {
    Directional = 0,
    Point,
    Spot,
    Ambient
};

struct RemixLightDesc {
    RemixLightType type = RemixLightType::Point;
    float position[3] = {};
    float direction[3] = {0.0f, -1.0f, 0.0f};
    float color[3] = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 1.0f;
    float coneAngle = 45.0f;
};

class RemixInterface {
public:
    ~RemixInterface();
    bool Initialize(const char* remixDllName);
    void SetHwnd(HWND hwnd);
    void BeginFrame();
    void EndFrame();
    void Shutdown();

    RemixLightHandle CreateLight(const RemixLightDesc& desc, uint64_t stableHash);
    bool UpdateLight(RemixLightHandle handle, const RemixLightDesc& desc, uint64_t stableHash);
    bool DestroyLight(RemixLightHandle handle);
    bool DrawLight(RemixLightHandle handle);

    bool IsRuntimeReady() const { return m_runtimeReady; }
    const char* LastStatus() const { return m_lastStatus; }

private:
    void WriteStatus(const char* msg);
    bool IsValidDllName(const char* remixDllName) const;
    bool BuildLightInfo(const RemixLightDesc& desc,
                        uint64_t stableHash,
                        remixapi_LightInfo* outInfo,
                        remixapi_LightInfoSphereEXT* outSphere,
                        remixapi_LightInfoDistantEXT* outDistant) const;

    bool m_runtimeReady = false;
    bool m_started = false;
    HWND m_hwnd = nullptr;
    bool m_usingBridgeMode = false;
    uint32_t m_lightLogCount = 0;
    RemixLightHandle m_mockHandleCounter = 1;
    char m_lastStatus[256] = "uninitialized";

    HMODULE m_remixDllModule = nullptr;
    remixapi_Interface m_api = {};
    std::unordered_map<RemixLightHandle, remixapi_LightHandle> m_liveHandles;
};
