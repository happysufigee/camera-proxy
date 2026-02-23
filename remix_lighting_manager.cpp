    #include "remix_lighting_manager.h"
#include "remix_api.h"
#include "remix_logger.h"

#include <algorithm>
#include <cmath>
#include <fstream>

// ─── helpers ─────────────────────────────────────────────────────────────────

static float ClampSafe(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float ClampPositive(float v, float fallback) {
    return (std::isfinite(v) && v > 0.0f) ? v : fallback;
}

// ─── public ──────────────────────────────────────────────────────────────────

bool RemixLightingManager::Initialize() {
    return remix_api::init();
}

void RemixLightingManager::BeginFrame() {
    m_ambientSubmittedThisFrame = false;
    for (auto& kv : m_activeLights) {
        kv.second.updatedThisFrame = false;
        kv.second.framesAlive++;
    }
}

void RemixLightingManager::EndFrame() {
    if (!remix_api::g_initialized) return;

    std::vector<uint64_t> stale;
    for (auto& kv : m_activeLights) {
        ManagedLight& l = kv.second;

        if (l.drawCounter > 0 && l.handle) {
            remix_api::g_api.DrawLightInstance(l.handle);
            l.drawCounter--;
        }

        if (!l.updatedThisFrame) {
            l.framesSinceUpdate++;
            if (l.framesSinceUpdate > static_cast<uint32_t>((std::max)(0, m_settings.graceThreshold))) {
                if (l.handle) remix_api::g_api.DestroyLight(l.handle);
                stale.push_back(kv.first);
            }
        } else {
            l.framesSinceUpdate = 0;
        }
    }
    for (uint64_t key : stale) m_activeLights.erase(key);
}

void RemixLightingManager::DestroyAllLights() {
    if (remix_api::g_initialized) {
        for (auto& kv : m_activeLights) {
            if (kv.second.handle)
                remix_api::g_api.DestroyLight(kv.second.handle);
        }
    }
    m_activeLights.clear();
}

bool RemixLightingManager::DumpLightsToJson(const char* path) const {
    if (!path || !path[0]) return false;
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return false;
    f << "{\n  \"activeLights\": [\n";
    bool first = true;
    for (const auto& kv : m_activeLights) {
        const ManagedLight& l = kv.second;
        if (!first) f << ",\n";
        first = false;
        f << "    {\"handle\": " << reinterpret_cast<uintptr_t>(l.handle)
          << ", \"signature\": "  << l.signatureHash
          << ", \"type\": "       << static_cast<int>(l.type)
          << ", \"intensity\": "  << l.intensity
          << ", \"framesAlive\": "<< l.framesAlive << "}";
    }
    f << "\n  ]\n}\n";
    return true;
}

// ─── private helpers ─────────────────────────────────────────────────────────

void RemixLightingManager::Normalize(float v[3]) const {
    float len = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

bool RemixLightingManager::IsFinite3(const float v[3]) const {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

float RemixLightingManager::ComputeIntensity(const float color[3]) const {
    float i = std::sqrt(color[0]*color[0] + color[1]*color[1] + color[2]*color[2]);
    return ClampSafe(i * m_settings.intensityMultiplier, 0.0f, 50000.0f);
}

bool RemixLightingManager::InvertMatrix(const D3DMATRIX& m, D3DMATRIX* out) const {
    if (!out) return false;
    float det =
        m._11*(m._22*m._33 - m._23*m._32) -
        m._12*(m._21*m._33 - m._23*m._31) +
        m._13*(m._21*m._32 - m._22*m._31);
    if (std::fabs(det) < 1e-8f) return false;
    float id = 1.0f / det;
    out->_11 =  (m._22*m._33 - m._23*m._32) * id;
    out->_12 = -(m._12*m._33 - m._13*m._32) * id;
    out->_13 =  (m._12*m._23 - m._13*m._22) * id;
    out->_21 = -(m._21*m._33 - m._23*m._31) * id;
    out->_22 =  (m._11*m._33 - m._13*m._31) * id;
    out->_23 = -(m._11*m._23 - m._13*m._21) * id;
    out->_31 =  (m._21*m._32 - m._22*m._31) * id;
    out->_32 = -(m._11*m._32 - m._12*m._31) * id;
    out->_33 =  (m._11*m._22 - m._12*m._21) * id;
    out->_14 = out->_24 = out->_34 = 0.0f; out->_44 = 1.0f;
    out->_41 = -(m._41*out->_11 + m._42*out->_21 + m._43*out->_31);
    out->_42 = -(m._41*out->_12 + m._42*out->_22 + m._43*out->_32);
    out->_43 = -(m._41*out->_13 + m._42*out->_23 + m._43*out->_33);
    return true;
}

void RemixLightingManager::TransformPosition(const D3DMATRIX& m, const float in[3], float out[3]) const {
    out[0] = in[0]*m._11 + in[1]*m._21 + in[2]*m._31 + m._41;
    out[1] = in[0]*m._12 + in[1]*m._22 + in[2]*m._32 + m._42;
    out[2] = in[0]*m._13 + in[1]*m._23 + in[2]*m._33 + m._43;
}

void RemixLightingManager::TransformDirection(const D3DMATRIX& m, const float in[3], float out[3]) const {
    out[0] = in[0]*m._11 + in[1]*m._21 + in[2]*m._31;
    out[1] = in[0]*m._12 + in[1]*m._22 + in[2]*m._32;
    out[2] = in[0]*m._13 + in[1]*m._23 + in[2]*m._33;
}

uint64_t RemixLightingManager::ComputeSignature(const ManagedLight& l) const {
    auto q = [](float v){ return static_cast<int64_t>(std::llround(v * 1000.0f)); };
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t x){ h ^= x; h *= 1099511628211ull; };
    mix(static_cast<uint64_t>(l.type));
    mix(static_cast<uint64_t>(q(l.position[0]))); mix(static_cast<uint64_t>(q(l.position[1]))); mix(static_cast<uint64_t>(q(l.position[2])));
    mix(static_cast<uint64_t>(q(l.direction[0]))); mix(static_cast<uint64_t>(q(l.direction[1]))); mix(static_cast<uint64_t>(q(l.direction[2])));
    mix(static_cast<uint64_t>(q(l.color[0]))); mix(static_cast<uint64_t>(q(l.color[1]))); mix(static_cast<uint64_t>(q(l.color[2])));
    mix(static_cast<uint64_t>(q(l.intensity)));
    mix(static_cast<uint64_t>(q(l.coneAngle)));
    return h;
}

void RemixLightingManager::FillRawRegisters(ManagedLight& light, int base, const float constants[][4]) {
    light.rawRegisterBase  = base;
    light.rawRegisterCount = 4;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            light.rawRegisters[i][j] = constants[base + i][j];
}

bool RemixLightingManager::BuildNativeLightInfo(const ManagedLight& l,
                                                remixapi_LightInfo*           outInfo,
                                                remixapi_LightInfoSphereEXT*  outSphere,
                                                remixapi_LightInfoDistantEXT* outDistant) const {
    if (!outInfo || !outSphere || !outDistant) return false;
    *outInfo    = {};
    *outSphere  = {};
    *outDistant = {};

    outInfo->sType    = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
    outInfo->pNext    = nullptr;
    outInfo->hash     = l.signatureHash;
    outInfo->radiance = {
        ClampPositive(l.color[0] * l.intensity, 0.0f),
        ClampPositive(l.color[1] * l.intensity, 0.0f),
        ClampPositive(l.color[2] * l.intensity, 0.0f)
    };

    if (l.type == RemixLightType::Directional) {
        outDistant->sType                   = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
        outDistant->direction               = { l.direction[0], l.direction[1], l.direction[2] };
        outDistant->angularDiameterDegrees  = 0.5f;
        outDistant->volumetricRadianceScale = 1.0f;
        outInfo->pNext = outDistant;
        return true;
    }

    outSphere->sType                   = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
    outSphere->position                = { l.position[0], l.position[1], l.position[2] };
    outSphere->radius                  = ClampPositive(l.range, 1.0f);
    outSphere->volumetricRadianceScale = 1.0f;
    outSphere->shaping_hasvalue        = 0;

    if (l.type == RemixLightType::Spot) {
        outSphere->shaping_hasvalue             = 1;
        outSphere->shaping_value.direction      = { l.direction[0], l.direction[1], l.direction[2] };
        outSphere->shaping_value.coneAngleDegrees = ClampPositive(l.coneAngle, 45.0f);
        outSphere->shaping_value.coneSoftness   = 0.0f;
        outSphere->shaping_value.focusExponent  = 1.0f;
    }

    outInfo->pNext = outSphere;
    return true;
}

// ─── SubmitManagedLight ───────────────────────────────────────────────────────

void RemixLightingManager::SubmitManagedLight(ManagedLight& candidate) {
    if (!m_settings.enabled || m_settings.freezeLightUpdates) return;
    if (candidate.type == RemixLightType::Directional && !m_settings.enableDirectional) return;
    if (candidate.type == RemixLightType::Point       && !m_settings.enablePoint)       return;
    if (candidate.type == RemixLightType::Spot        && !m_settings.enableSpot)        return;
    if (candidate.type == RemixLightType::Ambient     && !m_settings.enableAmbient)     return;

    candidate.signatureHash = ComputeSignature(candidate);

    // ── deduplication: update existing light ──────────────────────────────────
    if (!m_settings.disableDeduplication) {
        auto it = m_activeLights.find(candidate.signatureHash);
        if (it != m_activeLights.end()) {
            ManagedLight& existing = it->second;
            // Copy dynamic fields
            for (int i = 0; i < 3; ++i) {
                existing.color[i]     = candidate.color[i];
                existing.position[i]  = candidate.position[i];
                existing.direction[i] = candidate.direction[i];
            }
            existing.intensity = candidate.intensity;
            existing.range     = candidate.range;
            existing.coneAngle = candidate.coneAngle;

            if (remix_api::g_initialized && existing.handle) {
                // Recreate in place (Remix update pattern: create new, destroy old)
                remixapi_LightInfo           info    = {};
                remixapi_LightInfoSphereEXT  sphere  = {};
                remixapi_LightInfoDistantEXT distant = {};
                if (BuildNativeLightInfo(existing, &info, &sphere, &distant)) {
                    remixapi_LightHandle newHandle = nullptr;
                    if (remix_api::g_api.CreateLight(&info, &newHandle) == REMIXAPI_ERROR_CODE_SUCCESS && newHandle) {
                        remix_api::g_api.DestroyLight(existing.handle);
                        existing.handle = newHandle;
                    }
                }
            }

            existing.updatedThisFrame = true;
            existing.drawCounter      = 1;
            return;
        }
    }

    // ── new light ─────────────────────────────────────────────────────────────
    if (!remix_api::g_initialized) {
        RemixLog("SubmitManagedLight: API not initialized, dropping light (hash=%llu)", candidate.signatureHash);
        return;
    }

    remixapi_LightInfo           info    = {};
    remixapi_LightInfoSphereEXT  sphere  = {};
    remixapi_LightInfoDistantEXT distant = {};
    if (!BuildNativeLightInfo(candidate, &info, &sphere, &distant)) return;

    remixapi_LightHandle handle = nullptr;
    const remixapi_ErrorCode r  = remix_api::g_api.CreateLight(&info, &handle);
    if (r != REMIXAPI_ERROR_CODE_SUCCESS || !handle) {
        RemixLog("SubmitManagedLight: CreateLight failed (hash=%llu status=%d)", candidate.signatureHash, static_cast<int>(r));
        return;
    }

    candidate.handle          = handle;
    candidate.updatedThisFrame = true;
    candidate.drawCounter      = 1;
    m_activeLights[candidate.signatureHash] = candidate;
}

// ─── ProcessDrawCall ──────────────────────────────────────────────────────────

void RemixLightingManager::ProcessDrawCall(const ShaderLightingMetadata& meta,
                                           const float constants[][4],
                                           const D3DMATRIX& world,
                                           const D3DMATRIX& view,
                                           bool hasWorld,
                                           bool hasView) {
    if (!meta.isFFPLighting || !m_settings.enabled) return;

    int base       = meta.lightingConstantBase >= 0 ? meta.lightingConstantBase : 0;
    int lightCount = 1;
    if (meta.constantUsage && meta.constantCount > 0) {
        int run = 0;
        for (int i = base; i < meta.constantCount; ++i) {
            if (meta.constantUsage[i]) run++; else if (run > 0) break;
        }
        lightCount = (std::max)(1, run / 4);
        lightCount = (std::min)(lightCount, 8);
    }

    D3DMATRIX toWorld  = {};
    bool canTransform  = true;
    if (meta.lightSpace == LightingSpace::View) {
        if (!hasView || !InvertMatrix(view, &toWorld)) canTransform = false;
    } else if (meta.lightSpace == LightingSpace::Object) {
        if (!hasWorld) canTransform = false;
        else toWorld = world;
    }

    for (int i = 0; i < lightCount; ++i) {
        ManagedLight l = {};
        int reg = base + i * 4;
        float dir[3]   = { constants[reg][0],     constants[reg][1],     constants[reg][2]     };
        float color[3] = { constants[reg+1][0],   constants[reg+1][1],   constants[reg+1][2]   };
        float pos[3]   = { constants[reg+2][0],   constants[reg+2][1],   constants[reg+2][2]   };
        float atten    = constants[reg+3][0];
        float cone     = constants[reg+3][1];

        bool hasDir   = std::fabs(dir[0]) + std::fabs(dir[1]) + std::fabs(dir[2]) > 0.0001f;
        bool hasPos   = std::fabs(pos[0]) + std::fabs(pos[1]) + std::fabs(pos[2]) > 0.0001f;
        bool hasAtten = std::fabs(atten) > 0.0001f;

        if (!hasDir && !hasPos) {
            if (m_ambientSubmittedThisFrame) continue;
            l.type = RemixLightType::Ambient;
            m_ambientSubmittedThisFrame = true;
        } else if (hasDir && hasPos && cone > 0.001f) {
            l.type = RemixLightType::Spot;
        } else if (hasPos && hasAtten) {
            l.type = RemixLightType::Point;
        } else {
            l.type = RemixLightType::Directional;
        }

        l.color[0] = ClampSafe(color[0], 0.0f, 1000.0f);
        l.color[1] = ClampSafe(color[1], 0.0f, 1000.0f);
        l.color[2] = ClampSafe(color[2], 0.0f, 1000.0f);
        l.intensity = ComputeIntensity(l.color);
        l.range     = ClampSafe(hasAtten ? (1.0f / (std::max)(0.001f, std::fabs(atten))) : 20.0f,
                                0.01f, 100000.0f);
        if (l.type == RemixLightType::Ambient)
            l.range = ClampSafe(m_settings.ambientRadius, 1.0f, 1000000.0f);

        const float coneRad = ClampSafe(cone > 0.001f ? cone : 0.785398f, 0.01f, 3.12f);
        l.coneAngle = coneRad * 57.2957795f;

        for (int c = 0; c < 3; ++c) { l.direction[c] = dir[c]; l.position[c] = pos[c]; }
        Normalize(l.direction);

        if (canTransform && meta.lightSpace != LightingSpace::World) {
            TransformPosition (toWorld, l.position,  l.position);
            TransformDirection(toWorld, l.direction, l.direction);
            Normalize(l.direction);
        }

        if (!IsFinite3(l.color) || !IsFinite3(l.position) || !IsFinite3(l.direction)) continue;
        FillRawRegisters(l, reg, constants);
        SubmitManagedLight(l);
    }
}