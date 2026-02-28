#include "custom_lights.h"
#include "remix_api.h"
#include "remix_logger.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

FILE* OpenFileForReadWrite(const char* path, const char* mode) {
#ifdef _MSC_VER
    FILE* f = nullptr;
    return (fopen_s(&f, path, mode) == 0) ? f : nullptr;
#else
    return fopen(path, mode);
#endif
}

int ScanFloat3(const char* text, float* a, float* b, float* c) {
#ifdef _MSC_VER
    return sscanf_s(text, "%f %f %f", a, b, c);
#else
    return sscanf(text, "%f %f %f", a, b, c);
#endif
}

int ScanFloat12(const char* text,
                float* m00, float* m01, float* m02, float* m03,
                float* m10, float* m11, float* m12, float* m13,
                float* m20, float* m21, float* m22, float* m23) {
#ifdef _MSC_VER
    return sscanf_s(text, "%f %f %f %f %f %f %f %f %f %f %f %f",
                    m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23);
#else
    return sscanf(text, "%f %f %f %f %f %f %f %f %f %f %f %f",
                  m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23);
#endif
}

} // namespace

// ─── internal helpers ─────────────────────────────────────────────────────────

void CustomLightsManager::NormalizeInPlace(float v[3]) {
    float len = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

void CustomLightsManager::Cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

uint64_t CustomLightsManager::ComputeStableHash(uint32_t id) {
    // FNV-1a over the 4 bytes of id
    uint64_t h = 14695981039346656037ull;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&id);
    for (int i = 0; i < 4; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= 1099511628211ull;
    }
    return h;
}

// ─── public ───────────────────────────────────────────────────────────────────

CustomLight& CustomLightsManager::AddLight(CustomLightType type) {
    CustomLight l = {};
    l.id          = m_nextId++;
    l.type        = type;
    l.stableHash  = ComputeStableHash(l.id);
    l.dirty       = true;
    l.enabled     = true;

    // Per-type sensible defaults
    switch (type) {
    case CustomLightType::Sphere:
        snprintf(l.name, sizeof(l.name), "Sphere %u", l.id);
        l.radius = 5.0f;
        break;
    case CustomLightType::Rect:
        snprintf(l.name, sizeof(l.name), "Rect %u", l.id);
        l.xAxis[0]=1.0f; l.xAxis[1]=0.0f; l.xAxis[2]=0.0f;
        l.yAxis[0]=0.0f; l.yAxis[1]=1.0f; l.yAxis[2]=0.0f;
        l.xSize=10.0f; l.ySize=10.0f;
        break;
    case CustomLightType::Disk:
        snprintf(l.name, sizeof(l.name), "Disk %u", l.id);
        l.xAxis[0]=1.0f; l.xAxis[1]=0.0f; l.xAxis[2]=0.0f;
        l.yAxis[0]=0.0f; l.yAxis[1]=1.0f; l.yAxis[2]=0.0f;
        l.xRadius=5.0f; l.yRadius=5.0f;
        break;
    case CustomLightType::Cylinder:
        snprintf(l.name, sizeof(l.name), "Cylinder %u", l.id);
        l.axis[0]=0.0f; l.axis[1]=1.0f; l.axis[2]=0.0f;
        l.radius=3.0f; l.axisLength=10.0f;
        break;
    case CustomLightType::Distant:
        snprintf(l.name, sizeof(l.name), "Distant %u", l.id);
        l.direction[0]=0.0f; l.direction[1]=-1.0f; l.direction[2]=0.0f;
        l.angularDiameterDegrees=0.5f;
        break;
    case CustomLightType::Dome:
        snprintf(l.name, sizeof(l.name), "Dome %u", l.id);
        l.domeTransform[0][0]=1.0f; l.domeTransform[1][1]=1.0f; l.domeTransform[2][2]=1.0f;
        break;
    }

    m_lights.push_back(l);
    RemixLog("CustomLights: AddLight id=%u type=%d hash=%llu", l.id, (int)type, l.stableHash);
    return m_lights.back();
}

void CustomLightsManager::RemoveLight(uint32_t id) {
    for (auto it = m_lights.begin(); it != m_lights.end(); ++it) {
        if (it->id == id) {
            if (it->nativeHandle && remix_api::g_initialized)
                remix_api::g_api.DestroyLight(it->nativeHandle);
            RemixLog("CustomLights: RemoveLight id=%u", id);
            m_lights.erase(it);
            return;
        }
    }
}

void CustomLightsManager::DestroyAllNativeHandles() {
    for (auto& l : m_lights) {
        if (l.nativeHandle && remix_api::g_initialized)
            remix_api::g_api.DestroyLight(l.nativeHandle);
        l.nativeHandle = nullptr;
        l.dirty = true;
    }
    RemixLog("CustomLights: DestroyAllNativeHandles (%zu lights)", m_lights.size());
}

void CustomLightsManager::SetSaveFilePath(const char* path) {
    if (path) snprintf(m_saveFilePath, sizeof(m_saveFilePath), "%s", path);
}

// ─── per-frame ────────────────────────────────────────────────────────────────

void CustomLightsManager::BeginFrame(float deltaSeconds) {
    for (auto& l : m_lights)
        l.animation.elapsedTime += deltaSeconds;
}

float CustomLightsManager::SampleAnimatedScale(const AnimationParams& anim) {
    switch (anim.mode) {
    case AnimationMode::None:
        return 1.0f;
    case AnimationMode::Pulse: {
        float t = sinf(anim.elapsedTime * anim.speed * 6.2831853f) * 0.5f + 0.5f;
        return anim.minScale + t * (1.0f - anim.minScale);
    }
    case AnimationMode::Strobe:
        return fmodf(anim.elapsedTime * anim.speed, 1.0f) < anim.strobeOnFrac ? 1.0f : 0.0f;
    case AnimationMode::FadeIn: {
        float t = anim.fadeDuration > 0.0f ? anim.elapsedTime / anim.fadeDuration : 1.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    case AnimationMode::FadeOut: {
        float t = anim.fadeDuration > 0.0f ? 1.0f - anim.elapsedTime / anim.fadeDuration : 0.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    case AnimationMode::Flicker: {
        float t = anim.elapsedTime * anim.speed;
        float n = sinf(t * 23.4f + 0.8f) * sinf(t * 7.1f + 2.3f) * 0.5f + 0.5f;
        return anim.minScale + n * (1.0f - anim.minScale);
    }
    case AnimationMode::ColorCycle:
        return 1.0f;
    case AnimationMode::Breathe: {
        float phase = fmodf(anim.elapsedTime * anim.speed, 1.0f);
        float half = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
        float smooth = half * half * (3.0f - 2.0f * half);
        return anim.minScale + smooth * (1.0f - anim.minScale);
    }
    case AnimationMode::FireFlicker: {
        float t = anim.elapsedTime * anim.speed;
        float n1 = sinf(t * 3.0f * 6.2831853f + 0.0f) * 0.5f + 0.5f;
        float n2 = sinf(t * 11.0f * 6.2831853f + 1.7f) * 0.5f + 0.5f;
        float n = n1 * 0.7f + n2 * 0.3f;
        return anim.minScale + n * (1.0f - anim.minScale);
    }
    case AnimationMode::ElectricFlicker: {
        float t = anim.elapsedTime * anim.speed;
        float n = sinf(t * 37.0f + 0.5f) * sinf(t * 17.3f + 1.1f);
        n = n * 0.5f + 0.5f;
        float threshold = 1.0f - anim.minScale;
        return (n > threshold) ? 0.0f : 1.0f;
    }
    }
    return 1.0f;
}

void CustomLightsManager::ComputeAnimatedColorMultiplier(const AnimationParams& anim,
                                                         float out[3]) {
    if (anim.mode == AnimationMode::ColorCycle) {
        float hue = fmodf(anim.elapsedTime * anim.speed, 1.0f);
        float s = anim.saturation < 0.0f ? 0.0f : (anim.saturation > 1.0f ? 1.0f : anim.saturation);
        float h6 = hue * 6.0f;
        int hi = static_cast<int>(h6) % 6;
        float f = h6 - static_cast<float>(static_cast<int>(h6));
        float p = 1.0f - s;
        float q = 1.0f - s * f;
        float t2 = 1.0f - s * (1.0f - f);
        switch (hi) {
        case 0: out[0]=1.f; out[1]=t2;  out[2]=p;   break;
        case 1: out[0]=q;   out[1]=1.f; out[2]=p;   break;
        case 2: out[0]=p;   out[1]=1.f; out[2]=t2;  break;
        case 3: out[0]=p;   out[1]=q;   out[2]=1.f; break;
        case 4: out[0]=t2;  out[1]=p;   out[2]=1.f; break;
        default:out[0]=1.f; out[1]=p;   out[2]=q;   break;
        }
    } else {
        out[0] = out[1] = out[2] = 1.0f;
    }
}

// ─── BuildNativeLightInfo ─────────────────────────────────────────────────────

bool CustomLightsManager::BuildNativeLightInfo(const CustomLight& l,
                                                float animScale,
                                                const float colorMul[3],
                                                remixapi_LightInfo*            outInfo,
                                                remixapi_LightInfoSphereEXT*   outSphere,
                                                remixapi_LightInfoRectEXT*     outRect,
                                                remixapi_LightInfoDiskEXT*     outDisk,
                                                remixapi_LightInfoCylinderEXT* outCylinder,
                                                remixapi_LightInfoDistantEXT*  outDistant,
                                                remixapi_LightInfoDomeEXT*     outDome,
                                                wchar_t*                       outDomePath) {
    outInfo->sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
    outInfo->pNext = nullptr;
    outInfo->hash  = l.stableHash;
    outInfo->radiance.x = l.color[0] * colorMul[0] * l.intensity * animScale;
    outInfo->radiance.y = l.color[1] * colorMul[1] * l.intensity * animScale;
    outInfo->radiance.z = l.color[2] * colorMul[2] * l.intensity * animScale;

    auto fillShaping = [&](remixapi_LightInfoLightShaping& sh) {
        float sd[3] = { l.shaping.direction[0], l.shaping.direction[1], l.shaping.direction[2] };
        NormalizeInPlace(sd);
        sh.direction.x        = sd[0];
        sh.direction.y        = sd[1];
        sh.direction.z        = sd[2];
        sh.coneAngleDegrees   = l.shaping.coneAngleDegrees;
        sh.coneSoftness       = l.shaping.coneSoftness;
        sh.focusExponent      = l.shaping.focusExponent;
    };

    switch (l.type) {

    case CustomLightType::Sphere:
        outSphere->sType    = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
        outSphere->pNext    = nullptr;
        outSphere->position = { l.position[0], l.position[1], l.position[2] };
        outSphere->radius   = l.radius;
        outSphere->volumetricRadianceScale = l.volumetricRadianceScale;
        if (l.shaping.enabled) {
            outSphere->shaping_hasvalue = 1;
            fillShaping(outSphere->shaping_value);
        } else {
            outSphere->shaping_hasvalue = 0;
        }
        outInfo->pNext = outSphere;
        return true;

    case CustomLightType::Rect: {
        float xa[3] = { l.xAxis[0], l.xAxis[1], l.xAxis[2] };
        float ya[3] = { l.yAxis[0], l.yAxis[1], l.yAxis[2] };
        NormalizeInPlace(xa); NormalizeInPlace(ya);
        float d[3]; Cross3(xa, ya, d); NormalizeInPlace(d);
        outRect->sType     = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT;
        outRect->pNext     = nullptr;
        outRect->position  = { l.position[0], l.position[1], l.position[2] };
        outRect->xAxis     = { xa[0], xa[1], xa[2] };
        outRect->xSize     = l.xSize;
        outRect->yAxis     = { ya[0], ya[1], ya[2] };
        outRect->ySize     = l.ySize;
        outRect->direction = { d[0], d[1], d[2] };
        outRect->volumetricRadianceScale = l.volumetricRadianceScale;
        if (l.shaping.enabled) {
            outRect->shaping_hasvalue = 1;
            fillShaping(outRect->shaping_value);
        } else {
            outRect->shaping_hasvalue = 0;
        }
        outInfo->pNext = outRect;
        return true;
    }

    case CustomLightType::Disk: {
        float xa[3] = { l.xAxis[0], l.xAxis[1], l.xAxis[2] };
        float ya[3] = { l.yAxis[0], l.yAxis[1], l.yAxis[2] };
        NormalizeInPlace(xa); NormalizeInPlace(ya);
        float d[3]; Cross3(xa, ya, d); NormalizeInPlace(d);
        outDisk->sType     = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT;
        outDisk->pNext     = nullptr;
        outDisk->position  = { l.position[0], l.position[1], l.position[2] };
        outDisk->xAxis     = { xa[0], xa[1], xa[2] };
        outDisk->xRadius   = l.xRadius;
        outDisk->yAxis     = { ya[0], ya[1], ya[2] };
        outDisk->yRadius   = l.yRadius;
        outDisk->direction = { d[0], d[1], d[2] };
        outDisk->volumetricRadianceScale = l.volumetricRadianceScale;
        if (l.shaping.enabled) {
            outDisk->shaping_hasvalue = 1;
            fillShaping(outDisk->shaping_value);
        } else {
            outDisk->shaping_hasvalue = 0;
        }
        outInfo->pNext = outDisk;
        return true;
    }

    case CustomLightType::Cylinder: {
        // Cylinder has NO shaping field in the API
        float ax[3] = { l.axis[0], l.axis[1], l.axis[2] };
        NormalizeInPlace(ax);
        outCylinder->sType      = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT;
        outCylinder->pNext      = nullptr;
        outCylinder->position   = { l.position[0], l.position[1], l.position[2] };
        outCylinder->radius     = l.radius;
        outCylinder->axis       = { ax[0], ax[1], ax[2] };
        outCylinder->axisLength = l.axisLength;
        outCylinder->volumetricRadianceScale = l.volumetricRadianceScale;
        outInfo->pNext = outCylinder;
        return true;
    }

    case CustomLightType::Distant: {
        // Distant has NO shaping field in the API
        float dir[3] = { l.direction[0], l.direction[1], l.direction[2] };
        NormalizeInPlace(dir);
        outDistant->sType                    = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
        outDistant->pNext                    = nullptr;
        outDistant->direction                = { dir[0], dir[1], dir[2] };
        outDistant->angularDiameterDegrees   = l.angularDiameterDegrees;
        outDistant->volumetricRadianceScale  = l.volumetricRadianceScale;
        outInfo->pNext = outDistant;
        return true;
    }

    case CustomLightType::Dome:
        // Dome: no position, no shaping. radiance is ignored by Remix but still set above.
        outDome->sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT;
        outDome->pNext = nullptr;
        memcpy(outDome->transform.matrix, l.domeTransform, sizeof(l.domeTransform));
        outDome->colorTexture = nullptr;
        if (l.domeTexturePath[0] && outDomePath) {
            MultiByteToWideChar(CP_UTF8, 0, l.domeTexturePath, -1, outDomePath, MAX_PATH);
            outDome->colorTexture = outDomePath;
        }
        outInfo->pNext = outDome;
        return true;
    }

    return false;
}

// ─── EndFrame ─────────────────────────────────────────────────────────────────

void CustomLightsManager::EndFrame(const CameraState& cam) {
    for (auto& l : m_lights) {
        if (!l.enabled) {
            if (l.nativeHandle && remix_api::g_initialized) {
                remix_api::g_api.DestroyLight(l.nativeHandle);
                l.nativeHandle = nullptr;
            }
            continue;
        }

        if (!remix_api::g_initialized) continue;

        const float animScale = SampleAnimatedScale(l.animation);
        float colorMul[3];
        ComputeAnimatedColorMultiplier(l.animation, colorMul);

        CustomLight submitLight = l;
        if (l.followCamera && cam.valid) {
            const float* o = l.cameraOffset;
            submitLight.position[0] = o[0]*cam.row0[0] + o[1]*cam.row1[0] + o[2]*cam.row2[0] + cam.position[0];
            submitLight.position[1] = o[0]*cam.row0[1] + o[1]*cam.row1[1] + o[2]*cam.row2[1] + cam.position[1];
            submitLight.position[2] = o[0]*cam.row0[2] + o[1]*cam.row1[2] + o[2]*cam.row2[2] + cam.position[2];
        }

        const bool  animated  = (l.animation.mode != AnimationMode::None);
        const bool  needsRecreate = (l.nativeHandle == nullptr) || l.dirty || animated;

        if (needsRecreate) {
            remixapi_LightInfo            info     = {};
            remixapi_LightInfoSphereEXT   sphere   = {};
            remixapi_LightInfoRectEXT     rect     = {};
            remixapi_LightInfoDiskEXT     disk     = {};
            remixapi_LightInfoCylinderEXT cylinder = {};
            remixapi_LightInfoDistantEXT  distant  = {};
            remixapi_LightInfoDomeEXT     dome     = {};
            wchar_t                       domePath[MAX_PATH] = {};

            if (!BuildNativeLightInfo(submitLight, animScale, colorMul,
                                      &info, &sphere, &rect, &disk,
                                      &cylinder, &distant, &dome, domePath))
                continue;

            // SDK update pattern: destroy old handle first, then create with same hash
            if (l.nativeHandle) {
                remix_api::g_api.DestroyLight(l.nativeHandle);
                l.nativeHandle = nullptr;
            }
            remix_api::g_api.CreateLight(&info, &l.nativeHandle);
            l.dirty = false;
        }

        if (l.nativeHandle)
            remix_api::g_api.DrawLightInstance(l.nativeHandle);
    }
}

// ─── File I/O ─────────────────────────────────────────────────────────────────

static const char* TypeToStr(CustomLightType t) {
    switch (t) {
    case CustomLightType::Sphere:   return "Sphere";
    case CustomLightType::Rect:     return "Rect";
    case CustomLightType::Disk:     return "Disk";
    case CustomLightType::Cylinder: return "Cylinder";
    case CustomLightType::Distant:  return "Distant";
    case CustomLightType::Dome:     return "Dome";
    }
    return "Sphere";
}

static const char* AnimToStr(AnimationMode m) {
    switch (m) {
    case AnimationMode::None:    return "None";
    case AnimationMode::Pulse:   return "Pulse";
    case AnimationMode::Strobe:  return "Strobe";
    case AnimationMode::FadeIn:  return "FadeIn";
    case AnimationMode::FadeOut: return "FadeOut";
    case AnimationMode::Flicker: return "Flicker";
    case AnimationMode::ColorCycle: return "ColorCycle";
    case AnimationMode::Breathe: return "Breathe";
    case AnimationMode::FireFlicker: return "FireFlicker";
    case AnimationMode::ElectricFlicker: return "ElectricFlicker";
    }
    return "None";
}

static CustomLightType StrToType(const char* s) {
    if (!s) return CustomLightType::Sphere;
    if (strcmp(s,"Rect")     == 0) return CustomLightType::Rect;
    if (strcmp(s,"Disk")     == 0) return CustomLightType::Disk;
    if (strcmp(s,"Cylinder") == 0) return CustomLightType::Cylinder;
    if (strcmp(s,"Distant")  == 0) return CustomLightType::Distant;
    if (strcmp(s,"Dome")     == 0) return CustomLightType::Dome;
    return CustomLightType::Sphere;
}

static AnimationMode StrToAnim(const char* s) {
    if (!s) return AnimationMode::None;
    if (strcmp(s,"Pulse")   == 0) return AnimationMode::Pulse;
    if (strcmp(s,"Strobe")  == 0) return AnimationMode::Strobe;
    if (strcmp(s,"FadeIn")  == 0) return AnimationMode::FadeIn;
    if (strcmp(s,"FadeOut") == 0) return AnimationMode::FadeOut;
    if (strcmp(s,"Flicker") == 0) return AnimationMode::Flicker;
    if (strcmp(s,"ColorCycle") == 0) return AnimationMode::ColorCycle;
    if (strcmp(s,"Breathe") == 0) return AnimationMode::Breathe;
    if (strcmp(s,"FireFlicker") == 0) return AnimationMode::FireFlicker;
    if (strcmp(s,"ElectricFlicker") == 0) return AnimationMode::ElectricFlicker;
    return AnimationMode::None;
}

bool CustomLightsManager::SaveToFile(const char* path) const {
    if (!path || !path[0]) return false;
    FILE* f = OpenFileForReadWrite(path, "w");
    if (!f) { RemixLog("CustomLights: SaveToFile failed to open '%s'", path); return false; }

    for (const auto& l : m_lights) {
        fprintf(f, "[Light]\n");
        fprintf(f, "id=%u\n",      l.id);
        fprintf(f, "name=%s\n",    l.name);
        fprintf(f, "enabled=%d\n", l.enabled ? 1 : 0);
        fprintf(f, "type=%s\n",    TypeToStr(l.type));
        fprintf(f, "color=%.4f %.4f %.4f\n", l.color[0], l.color[1], l.color[2]);
        fprintf(f, "intensity=%.4f\n",        l.intensity);
        fprintf(f, "volumetricScale=%.4f\n",  l.volumetricRadianceScale);
        fprintf(f, "position=%.4f %.4f %.4f\n", l.position[0], l.position[1], l.position[2]);
        fprintf(f, "radius=%.4f\n",           l.radius);
        fprintf(f, "xAxis=%.4f %.4f %.4f\n",  l.xAxis[0], l.xAxis[1], l.xAxis[2]);
        fprintf(f, "yAxis=%.4f %.4f %.4f\n",  l.yAxis[0], l.yAxis[1], l.yAxis[2]);
        fprintf(f, "xSize=%.4f\n",            l.xSize);
        fprintf(f, "ySize=%.4f\n",            l.ySize);
        fprintf(f, "xRadius=%.4f\n",          l.xRadius);
        fprintf(f, "yRadius=%.4f\n",          l.yRadius);
        fprintf(f, "axis=%.4f %.4f %.4f\n",   l.axis[0], l.axis[1], l.axis[2]);
        fprintf(f, "axisLength=%.4f\n",       l.axisLength);
        fprintf(f, "direction=%.4f %.4f %.4f\n", l.direction[0], l.direction[1], l.direction[2]);
        fprintf(f, "angularDiam=%.4f\n",      l.angularDiameterDegrees);
        fprintf(f, "domeTex=%s\n",            l.domeTexturePath);
        fprintf(f, "domeTransform=%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                l.domeTransform[0][0], l.domeTransform[0][1], l.domeTransform[0][2], l.domeTransform[0][3],
                l.domeTransform[1][0], l.domeTransform[1][1], l.domeTransform[1][2], l.domeTransform[1][3],
                l.domeTransform[2][0], l.domeTransform[2][1], l.domeTransform[2][2], l.domeTransform[2][3]);
        fprintf(f, "shaping=%d\n",            l.shaping.enabled ? 1 : 0);
        fprintf(f, "shaping_dir=%.4f %.4f %.4f\n", l.shaping.direction[0], l.shaping.direction[1], l.shaping.direction[2]);
        fprintf(f, "shaping_cone=%.4f\n",     l.shaping.coneAngleDegrees);
        fprintf(f, "shaping_soft=%.4f\n",     l.shaping.coneSoftness);
        fprintf(f, "shaping_focus=%.4f\n",    l.shaping.focusExponent);
        fprintf(f, "anim=%s\n",               AnimToStr(l.animation.mode));
        fprintf(f, "anim_speed=%.4f\n",       l.animation.speed);
        fprintf(f, "anim_min=%.4f\n",         l.animation.minScale);
        fprintf(f, "anim_strobe_on=%.4f\n",   l.animation.strobeOnFrac);
        fprintf(f, "anim_fade_dur=%.4f\n",    l.animation.fadeDuration);
        fprintf(f, "anim_saturation=%.4f\n",  l.animation.saturation);
        fprintf(f, "followCamera=%d\n",       l.followCamera ? 1 : 0);
        fprintf(f, "cameraOffset=%.4f %.4f %.4f\n",
                l.cameraOffset[0], l.cameraOffset[1], l.cameraOffset[2]);
        fprintf(f, "\n");
    }

    fclose(f);
    RemixLog("CustomLights: saved %zu lights to '%s'", m_lights.size(), path);
    return true;
}

bool CustomLightsManager::LoadFromFile(const char* path) {
    if (!path || !path[0]) return false;
    FILE* f = OpenFileForReadWrite(path, "r");
    if (!f) { RemixLog("CustomLights: LoadFromFile failed to open '%s'", path); return false; }

    m_lights.clear();
    uint32_t maxId = 0;
    CustomLight* cur = nullptr;

    char line[MAX_PATH + 64];
    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;

        if (strcmp(line, "[Light]") == 0) {
            m_lights.push_back({});
            cur = &m_lights.back();
            cur->nativeHandle = nullptr;
            cur->dirty        = true;
            continue;
        }
        if (!cur) continue;

        // Split key=value
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;

        if      (strcmp(key,"id")             == 0) { cur->id = (uint32_t)atoi(val); cur->stableHash = ComputeStableHash(cur->id); if (cur->id > maxId) maxId = cur->id; }
        else if (strcmp(key,"name")           == 0) { snprintf(cur->name, sizeof(cur->name), "%s", val); }
        else if (strcmp(key,"enabled")        == 0) { cur->enabled = atoi(val) != 0; }
        else if (strcmp(key,"type")           == 0) { cur->type = StrToType(val); }
        else if (strcmp(key,"color")          == 0) { ScanFloat3(val, &cur->color[0], &cur->color[1], &cur->color[2]); }
        else if (strcmp(key,"intensity")      == 0) { cur->intensity = (float)atof(val); }
        else if (strcmp(key,"volumetricScale")== 0) { cur->volumetricRadianceScale = (float)atof(val); }
        else if (strcmp(key,"position")       == 0) { ScanFloat3(val, &cur->position[0], &cur->position[1], &cur->position[2]); }
        else if (strcmp(key,"radius")         == 0) { cur->radius = (float)atof(val); }
        else if (strcmp(key,"xAxis")          == 0) { ScanFloat3(val, &cur->xAxis[0], &cur->xAxis[1], &cur->xAxis[2]); }
        else if (strcmp(key,"yAxis")          == 0) { ScanFloat3(val, &cur->yAxis[0], &cur->yAxis[1], &cur->yAxis[2]); }
        else if (strcmp(key,"xSize")          == 0) { cur->xSize = (float)atof(val); }
        else if (strcmp(key,"ySize")          == 0) { cur->ySize = (float)atof(val); }
        else if (strcmp(key,"xRadius")        == 0) { cur->xRadius = (float)atof(val); }
        else if (strcmp(key,"yRadius")        == 0) { cur->yRadius = (float)atof(val); }
        else if (strcmp(key,"axis")           == 0) { ScanFloat3(val, &cur->axis[0], &cur->axis[1], &cur->axis[2]); }
        else if (strcmp(key,"axisLength")     == 0) { cur->axisLength = (float)atof(val); }
        else if (strcmp(key,"direction")      == 0) { ScanFloat3(val, &cur->direction[0], &cur->direction[1], &cur->direction[2]); }
        else if (strcmp(key,"angularDiam")    == 0) { cur->angularDiameterDegrees = (float)atof(val); }
        else if (strcmp(key,"domeTex")        == 0) { snprintf(cur->domeTexturePath, sizeof(cur->domeTexturePath), "%s", val); }
        else if (strcmp(key,"domeTransform")  == 0) {
            ScanFloat12(val,
                        &cur->domeTransform[0][0], &cur->domeTransform[0][1], &cur->domeTransform[0][2], &cur->domeTransform[0][3],
                        &cur->domeTransform[1][0], &cur->domeTransform[1][1], &cur->domeTransform[1][2], &cur->domeTransform[1][3],
                        &cur->domeTransform[2][0], &cur->domeTransform[2][1], &cur->domeTransform[2][2], &cur->domeTransform[2][3]);
        }
        else if (strcmp(key,"shaping")        == 0) { cur->shaping.enabled = atoi(val) != 0; }
        else if (strcmp(key,"shaping_dir")    == 0) { ScanFloat3(val, &cur->shaping.direction[0], &cur->shaping.direction[1], &cur->shaping.direction[2]); }
        else if (strcmp(key,"shaping_cone")   == 0) { cur->shaping.coneAngleDegrees = (float)atof(val); }
        else if (strcmp(key,"shaping_soft")   == 0) { cur->shaping.coneSoftness = (float)atof(val); }
        else if (strcmp(key,"shaping_focus")  == 0) { cur->shaping.focusExponent = (float)atof(val); }
        else if (strcmp(key,"anim")           == 0) { cur->animation.mode = StrToAnim(val); }
        else if (strcmp(key,"anim_speed")     == 0) { cur->animation.speed = (float)atof(val); }
        else if (strcmp(key,"anim_min")       == 0) { cur->animation.minScale = (float)atof(val); }
        else if (strcmp(key,"anim_strobe_on") == 0) { cur->animation.strobeOnFrac = (float)atof(val); }
        else if (strcmp(key,"anim_fade_dur")  == 0) { cur->animation.fadeDuration = (float)atof(val); }
        else if (strcmp(key,"anim_saturation") == 0) { cur->animation.saturation = (float)atof(val); }
        else if (strcmp(key,"followCamera") == 0) { cur->followCamera = atoi(val) != 0; }
        else if (strcmp(key,"cameraOffset") == 0) { ScanFloat3(val, &cur->cameraOffset[0], &cur->cameraOffset[1], &cur->cameraOffset[2]); }
    }

    fclose(f);
    m_nextId = maxId + 1;
    RemixLog("CustomLights: loaded %zu lights from '%s'", m_lights.size(), path);
    return true;
}
