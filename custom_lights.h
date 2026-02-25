#pragma once

#include <cstdint>
#include <vector>
#include <windows.h>

// Pull in remix types via the bridge header (already guarded for x86)
#include "remixapi/bridge_remix_api.h"

// ─── Animation ───────────────────────────────────────────────────────────────

enum class AnimationMode {
    None,
    Pulse,
    Strobe,
    FadeIn,
    FadeOut,
    Flicker,
    ColorCycle,
    Breathe,
    FireFlicker,
    ElectricFlicker
};

struct AnimationParams {
    AnimationMode mode         = AnimationMode::None;
    float         speed        = 1.0f;   // cycles / second
    float         minScale     = 0.0f;   // Pulse: floor of oscillation [0,1]
    float         strobeOnFrac = 0.5f;   // Strobe: fraction of cycle that is ON
    float         fadeDuration = 1.0f;   // FadeIn / FadeOut total seconds
    float         saturation   = 1.0f;
    float         elapsedTime  = 0.0f;   // accumulated, reset by user
};

// ─── Shaping (Sphere / Rect / Disk only) ─────────────────────────────────────

struct LightShaping {
    bool  enabled          = false;
    float direction[3]     = { 0.0f, -1.0f, 0.0f };
    float coneAngleDegrees = 45.0f;
    float coneSoftness     = 0.1f;
    float focusExponent    = 1.0f;
};

// ─── Light type ───────────────────────────────────────────────────────────────

enum class CustomLightType { Sphere, Rect, Disk, Cylinder, Distant, Dome };

// ─── CustomLight ──────────────────────────────────────────────────────────────

struct CustomLight {
    uint32_t        id   = 0;
    char            name[64] = "New Light";
    bool            enabled  = true;
    bool            dirty    = true;   // true → native handle recreated next EndFrame
    CustomLightType type     = CustomLightType::Sphere;

    // ── Radiance (all types) ──────────────────────────────────────────────────
    float color[3]                = { 1.0f, 1.0f, 1.0f }; // linear [0,1]
    float intensity               = 100.0f;
    float volumetricRadianceScale = 1.0f;

    // ── Position (Sphere / Rect / Disk / Cylinder) ────────────────────────────
    float position[3] = {};
    bool  followCamera    = false;
    float cameraOffset[3] = { 0.0f, 0.0f, 0.0f };

    // ── Sphere / Cylinder radius ──────────────────────────────────────────────
    float radius = 5.0f;

    // ── Rect / Disk orientation ───────────────────────────────────────────────
    // Both must stay unit-length; direction is computed as cross(xAxis,yAxis).
    float xAxis[3] = { 1.0f, 0.0f, 0.0f };
    float yAxis[3] = { 0.0f, 1.0f, 0.0f };
    // Rect extents
    float xSize = 10.0f;
    float ySize = 10.0f;
    // Disk radii
    float xRadius = 5.0f;
    float yRadius = 5.0f;

    // ── Cylinder ──────────────────────────────────────────────────────────────
    float axis[3]     = { 0.0f, 1.0f, 0.0f }; // normalized center axis
    float axisLength  = 10.0f;

    // ── Distant ───────────────────────────────────────────────────────────────
    float direction[3]           = { 0.0f, -1.0f, 0.0f };
    float angularDiameterDegrees = 0.5f;

    // ── Dome ──────────────────────────────────────────────────────────────────
    char  domeTexturePath[MAX_PATH] = {};
    float domeTransform[3][4]       = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0} };

    // ── Shaping (Sphere / Rect / Disk only) ───────────────────────────────────
    LightShaping shaping;

    // ── Animation ─────────────────────────────────────────────────────────────
    AnimationParams animation;

    // ── Runtime (not saved) ───────────────────────────────────────────────────
    remixapi_LightHandle nativeHandle = nullptr;
    uint64_t             stableHash   = 0; // FNV-1a over id, set once at creation
};

struct CameraState {
    bool  valid       = false;
    float row0[3]     = {};
    float row1[3]     = {};
    float row2[3]     = {};
    float position[3] = {};
};

// ─── CustomLightsManager ─────────────────────────────────────────────────────

class CustomLightsManager {
public:
    // Called from WrappedD3D9Device::BeginScene (after g_remixLightingManager.BeginFrame)
    void BeginFrame(float deltaSeconds);

    // Called from WrappedD3D9Device::Present (after g_remixLightingManager.EndFrame)
    void EndFrame(const CameraState& cam);

    // Light management
    CustomLight& AddLight(CustomLightType type);
    void         RemoveLight(uint32_t id);
    void         DestroyAllNativeHandles();

    // Persistence
    bool        SaveToFile(const char* path) const;
    bool        LoadFromFile(const char* path);
    void        SetSaveFilePath(const char* path);
    const char* SaveFilePath() const { return m_saveFilePath; }

    // Accessors
    std::vector<CustomLight>&       Lights()       { return m_lights; }
    const std::vector<CustomLight>& Lights() const { return m_lights; }

private:
    static float    ComputeAnimatedScale(const AnimationParams& anim);
    static bool     BuildNativeLightInfo(const CustomLight& l,
                                         float animScale,
                                         const float colorMul[3],
                                         remixapi_LightInfo*            outInfo,
                                         remixapi_LightInfoSphereEXT*   outSphere,
                                         remixapi_LightInfoRectEXT*     outRect,
                                         remixapi_LightInfoDiskEXT*     outDisk,
                                         remixapi_LightInfoCylinderEXT* outCylinder,
                                         remixapi_LightInfoDistantEXT*  outDistant,
                                         remixapi_LightInfoDomeEXT*     outDome,
                                         wchar_t*                       outDomePath);
    static void     ComputeAnimatedColorMultiplier(const AnimationParams& anim, float out[3]);
    static void     NormalizeInPlace(float v[3]);
    static void     Cross3(const float a[3], const float b[3], float out[3]);
    static uint64_t ComputeStableHash(uint32_t id);

    std::vector<CustomLight> m_lights;
    uint32_t                 m_nextId = 1;
    char                     m_saveFilePath[MAX_PATH] = "custom_lights.cltx";
};
