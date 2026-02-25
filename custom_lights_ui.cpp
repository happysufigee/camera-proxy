#include "custom_lights_ui.h"
#include "custom_lights.h"
#include "remix_api.h"
#include "imgui/imgui.h"

#include <cstdio>
#include <cmath>

// ─── helpers ─────────────────────────────────────────────────────────────────

static const char* TypeLabel(CustomLightType t) {
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

static void NormalizeUI(float v[3]) {
    float len = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

static void ComputedDirText(const float xa[3], const float ya[3]) {
    float ax[3] = { xa[0], xa[1], xa[2] };
    float ay[3] = { ya[0], ya[1], ya[2] };
    NormalizeUI(ax); NormalizeUI(ay);
    float d[3] = {
        ax[1]*ay[2] - ax[2]*ay[1],
        ax[2]*ay[0] - ax[0]*ay[2],
        ax[0]*ay[1] - ax[1]*ay[0]
    };
    NormalizeUI(d);
    ImGui::TextDisabled("Direction (computed): %.3f  %.3f  %.3f", d[0], d[1], d[2]);
}

// ─── main function ────────────────────────────────────────────────────────────

void DrawCustomLightsTab(CustomLightsManager& manager) {
    static uint32_t      selectedId  = 0;
    static int           addTypeIdx  = 0;
    static char          filePath[MAX_PATH] = "custom_lights.cltx";
    static float         step        = 1.0f;

    static const char* kTypeNames[] = { "Sphere","Rect","Disk","Cylinder","Distant","Dome" };
    static const char* kAnimNames[] = {
        "None", "Pulse", "Strobe", "FadeIn", "FadeOut",
        "Flicker", "ColorCycle", "Breathe", "FireFlicker", "ElectricFlicker"
    };

    ImGui::Columns(3, "CLCols", true);

    // ─── Column 1: Light List ────────────────────────────────────────────────

    ImGui::Text("Lights");
    ImGui::Separator();

    ImGui::Combo("##addtype", &addTypeIdx, kTypeNames, 6);
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        CustomLight& nl = manager.AddLight(static_cast<CustomLightType>(addTypeIdx));
        selectedId = nl.id;
    }

    ImGui::BeginChild("CLList", ImVec2(0, -60), true);
    for (auto& l : manager.Lights()) {
        ImGui::PushID(static_cast<int>(l.id));
        bool en = l.enabled;
        if (ImGui::Checkbox("##en", &en)) { l.enabled = en; l.dirty = true; }
        ImGui::SameLine();
        char label[96];
        snprintf(label, sizeof(label), "[%s] %s###sl_%u", TypeLabel(l.type), l.name, l.id);
        bool sel = (selectedId == l.id);
        if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_None))
            selectedId = l.id;
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("Remove Selected")) manager.RemoveLight(selectedId);
    ImGui::SameLine();
    if (ImGui::Button("Destroy Handles")) manager.DestroyAllNativeHandles();

    // ─── Column 2: Editor ────────────────────────────────────────────────────

    ImGui::NextColumn();
    ImGui::Text("Edit Light");
    ImGui::Separator();

    // Find selected light
    CustomLight* lp = nullptr;
    for (auto& l : manager.Lights()) if (l.id == selectedId) { lp = &l; break; }

    if (!lp) {
        ImGui::TextDisabled("Select a light to edit.");
        ImGui::NextColumn();
        goto column3;
    }

    {
        CustomLight& l = *lp;

        // ── Common ──────────────────────────────────────────────────────────
        if (ImGui::InputText("Name", l.name, sizeof(l.name)))          l.dirty = true;

        bool en2 = l.enabled;
        if (ImGui::Checkbox("Enabled", &en2))                         { l.enabled = en2; l.dirty = true; }

        int typeIdx = static_cast<int>(l.type);
        if (ImGui::Combo("Type", &typeIdx, kTypeNames, 6)) {
            l.type = static_cast<CustomLightType>(typeIdx);
            l.dirty = true;
            // Reset type-specific orientation defaults on type change
            if (l.type == CustomLightType::Rect || l.type == CustomLightType::Disk) {
                l.xAxis[0]=1.f; l.xAxis[1]=0.f; l.xAxis[2]=0.f;
                l.yAxis[0]=0.f; l.yAxis[1]=1.f; l.yAxis[2]=0.f;
            }
            if (l.type == CustomLightType::Cylinder) {
                l.axis[0]=0.f; l.axis[1]=1.f; l.axis[2]=0.f;
            }
            if (l.type == CustomLightType::Distant) {
                l.direction[0]=0.f; l.direction[1]=-1.f; l.direction[2]=0.f;
            }
            if (l.type == CustomLightType::Dome) {
                l.domeTransform[0][0]=1.f; l.domeTransform[0][1]=0.f; l.domeTransform[0][2]=0.f; l.domeTransform[0][3]=0.f;
                l.domeTransform[1][0]=0.f; l.domeTransform[1][1]=1.f; l.domeTransform[1][2]=0.f; l.domeTransform[1][3]=0.f;
                l.domeTransform[2][0]=0.f; l.domeTransform[2][1]=0.f; l.domeTransform[2][2]=1.f; l.domeTransform[2][3]=0.f;
            }
        }

        // Color picker — float[4] shim, no alpha, no HDR (intensity handles brightness)
        ImGui::Separator();
        ImGui::Text("Color / Intensity");
        float col4[4] = { l.color[0], l.color[1], l.color[2], 1.0f };
        if (ImGui::ColorPicker4("##color", col4,
                ImGuiColorEditFlags_Float      |
                ImGuiColorEditFlags_NoAlpha    |
                ImGuiColorEditFlags_PickerHueWheel)) {
            l.color[0] = col4[0]; l.color[1] = col4[1]; l.color[2] = col4[2];
            l.dirty = true;
        }

        ImGui::SliderFloat("Intensity", &l.intensity, 0.0f, 100000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemEdited()) l.dirty = true;

        ImGui::SliderFloat("Volumetric Scale", &l.volumetricRadianceScale, 0.0f, 10.0f, "%.3f");
        if (ImGui::IsItemEdited()) l.dirty = true;

        // ── Position (not Distant, not Dome) ────────────────────────────────
        if (l.type != CustomLightType::Distant && l.type != CustomLightType::Dome) {
            ImGui::Separator();
            ImGui::Text("Position");
            ImGui::SliderFloat("Step", &step, 0.01f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::InputFloat3("##pos", l.position, "%.3f");
            if (ImGui::IsItemEdited()) l.dirty = true;
            // Per-axis step buttons
            const char* axLabels[3] = { "X","Y","Z" };
            for (int ax = 0; ax < 3; ax++) {
                ImGui::PushID(ax);
                ImGui::Text("%s:", axLabels[ax]); ImGui::SameLine();
                if (ImGui::SmallButton("+")) { l.position[ax] += step; l.dirty = true; }
                ImGui::SameLine();
                if (ImGui::SmallButton("-")) { l.position[ax] -= step; l.dirty = true; }
                if (ax < 2) ImGui::SameLine(0, 20);
                ImGui::PopID();
            }

            ImGui::Separator();
            bool fc = l.followCamera;
            if (ImGui::Checkbox("Follow camera##fc", &fc)) { l.followCamera = fc; l.dirty = true; }
            if (l.followCamera) {
                ImGui::TextDisabled("Camera-space offset (right/up/forward):");
                ImGui::InputFloat3("##camoffset", l.cameraOffset, "%.3f");
                if (ImGui::IsItemEdited()) l.dirty = true;
                const char* axLabels2[3] = { "R", "U", "F" };
                for (int ax = 0; ax < 3; ax++) {
                    ImGui::PushID(100 + ax);
                    ImGui::Text("%s:", axLabels2[ax]); ImGui::SameLine();
                    if (ImGui::SmallButton("+")) { l.cameraOffset[ax] += step; l.dirty = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-")) { l.cameraOffset[ax] -= step; l.dirty = true; }
                    if (ax < 2) ImGui::SameLine(0, 20);
                    ImGui::PopID();
                }
            }
        }

        // ── Type-specific ────────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Light Parameters");

        if (l.type == CustomLightType::Sphere) {
            ImGui::SliderFloat("Radius", &l.radius, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemEdited()) l.dirty = true;
        }

        if (l.type == CustomLightType::Rect || l.type == CustomLightType::Disk) {
            ImGui::InputFloat3("X Axis (norm)", l.xAxis, "%.3f");
            if (ImGui::IsItemEdited()) { NormalizeUI(l.xAxis); l.dirty = true; }
            ImGui::InputFloat3("Y Axis (norm)", l.yAxis, "%.3f");
            if (ImGui::IsItemEdited()) { NormalizeUI(l.yAxis); l.dirty = true; }
            ComputedDirText(l.xAxis, l.yAxis);
        }

        if (l.type == CustomLightType::Rect) {
            ImGui::SliderFloat("X Size", &l.xSize, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemEdited()) l.dirty = true;
            ImGui::SliderFloat("Y Size", &l.ySize, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemEdited()) l.dirty = true;
        }

        if (l.type == CustomLightType::Disk) {
            ImGui::SliderFloat("X Radius", &l.xRadius, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemEdited()) l.dirty = true;
            ImGui::SliderFloat("Y Radius", &l.yRadius, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemEdited()) l.dirty = true;
        }

        if (l.type == CustomLightType::Cylinder) {
            ImGui::InputFloat3("Axis (norm)", l.axis, "%.3f");
            if (ImGui::IsItemEdited()) { NormalizeUI(l.axis); l.dirty = true; }
            ImGui::SliderFloat("Radius", &l.radius, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemEdited()) l.dirty = true;
            ImGui::SliderFloat("Axis Length", &l.axisLength, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemEdited()) l.dirty = true;
        }

        if (l.type == CustomLightType::Distant) {
            ImGui::InputFloat3("Direction (norm)", l.direction, "%.3f");
            if (ImGui::IsItemEdited()) { NormalizeUI(l.direction); l.dirty = true; }
            ImGui::SliderFloat("Angular Diameter (deg)", &l.angularDiameterDegrees, 0.1f, 90.0f, "%.3f");
            if (ImGui::IsItemEdited()) l.dirty = true;
        }

        if (l.type == CustomLightType::Dome) {
            if (ImGui::InputText("Texture Path", l.domeTexturePath, MAX_PATH)) l.dirty = true;
            ImGui::Text("Transform (3x4):");
            if (ImGui::BeginTable("DomeTfm", 4, ImGuiTableFlags_BordersInner)) {
                for (int row = 0; row < 3; row++) {
                    ImGui::TableNextRow();
                    for (int col = 0; col < 4; col++) {
                        ImGui::TableSetColumnIndex(col);
                        ImGui::PushID(row*4+col);
                        ImGui::SetNextItemWidth(55.0f);
                        if (ImGui::InputFloat("##v", &l.domeTransform[row][col], 0.f, 0.f, "%.3f"))
                            l.dirty = true;
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
        }

        // ── Shaping (Sphere / Rect / Disk only) ─────────────────────────────
        bool canShape = (l.type == CustomLightType::Sphere ||
                         l.type == CustomLightType::Rect   ||
                         l.type == CustomLightType::Disk);
        if (canShape) {
            ImGui::Separator();
            ImGui::Text("Shaping");
            bool sh = l.shaping.enabled;
            if (ImGui::Checkbox("Enable##shaping", &sh)) { l.shaping.enabled = sh; l.dirty = true; }
            if (l.shaping.enabled) {
                ImGui::InputFloat3("Shaping Dir (norm)", l.shaping.direction, "%.3f");
                if (ImGui::IsItemEdited()) { NormalizeUI(l.shaping.direction); l.dirty = true; }
                ImGui::SliderFloat("Cone Angle (deg)",  &l.shaping.coneAngleDegrees, 1.0f, 179.0f, "%.2f");
                if (ImGui::IsItemEdited()) l.dirty = true;
                ImGui::SliderFloat("Cone Softness",     &l.shaping.coneSoftness,     0.0f, 1.0f, "%.3f");
                if (ImGui::IsItemEdited()) l.dirty = true;
                ImGui::SliderFloat("Focus Exponent",    &l.shaping.focusExponent,    0.0f, 100.0f, "%.2f");
                if (ImGui::IsItemEdited()) l.dirty = true;
            }
        }

        // ── Animation (all types) ────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Animation");
        int animIdx = static_cast<int>(l.animation.mode);
        if (ImGui::Combo("Mode##anim", &animIdx, kAnimNames, 10)) {
            l.animation.mode = static_cast<AnimationMode>(animIdx);
            l.dirty = true;
        }
        if (l.animation.mode != AnimationMode::None) {
            ImGui::SliderFloat("Speed (Hz)", &l.animation.speed, 0.01f, 20.0f, "%.2f");

            if (l.animation.mode == AnimationMode::Pulse) {
                ImGui::SliderFloat("Min Scale", &l.animation.minScale, 0.0f, 1.0f, "%.3f");
            }
            if (l.animation.mode == AnimationMode::Strobe) {
                ImGui::SliderFloat("On Fraction", &l.animation.strobeOnFrac, 0.0f, 1.0f, "%.3f");
            }
            if (l.animation.mode == AnimationMode::FadeIn ||
                l.animation.mode == AnimationMode::FadeOut) {
                ImGui::SliderFloat("Duration (s)", &l.animation.fadeDuration, 0.1f, 60.0f, "%.2f");
            }
            if (l.animation.mode == AnimationMode::Flicker ||
                l.animation.mode == AnimationMode::Breathe ||
                l.animation.mode == AnimationMode::FireFlicker ||
                l.animation.mode == AnimationMode::ElectricFlicker) {
                ImGui::SliderFloat("Intensity Floor", &l.animation.minScale, 0.0f, 1.0f, "%.3f");
                ImGui::TextDisabled("0 = full depth,  1 = no effect");
            }
            if (l.animation.mode == AnimationMode::ColorCycle) {
                ImGui::SliderFloat("Saturation", &l.animation.saturation, 0.0f, 1.0f, "%.3f");
                ImGui::TextDisabled("0 = white,  1 = full colour");
            }

            if (ImGui::Button("Reset Timer")) l.animation.elapsedTime = 0.0f;
            ImGui::SameLine();
            ImGui::Text("t=%.2fs", l.animation.elapsedTime);
        }
    }

    // ─── Column 3: File / Status ─────────────────────────────────────────────

    ImGui::NextColumn();
    column3:

    ImGui::Text("File");
    ImGui::Separator();
    ImGui::InputText("##filepath", filePath, sizeof(filePath));
    if (ImGui::Button("Save")) {
        manager.SaveToFile(filePath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        manager.LoadFromFile(filePath);
        manager.DestroyAllNativeHandles();
    }

    ImGui::Separator();
    ImGui::Text("Status");
    ImGui::Separator();

    int total  = static_cast<int>(manager.Lights().size());
    int active = 0;
    for (const auto& l : manager.Lights())
        if (l.enabled && l.nativeHandle) active++;

    ImGui::Text("Lights: %d", total);
    ImGui::Text("Active handles: %d", active);
    ImGui::Text("API: %s", remix_api::g_initialized ? "Ready" : "Not initialized");

    ImGui::Columns(1);
}
