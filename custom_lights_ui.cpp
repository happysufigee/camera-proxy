#include "custom_lights_ui.h"
#include "custom_lights.h"
#include "remix_api.h"
#include "imgui/imgui.h"

#include <cstdio>
#include <cmath>

static const char* TypeLabel(CustomLightType t) {
    switch (t) {
    case CustomLightType::Sphere: return "Sphere";
    case CustomLightType::Rect: return "Rect";
    case CustomLightType::Disk: return "Disk";
    case CustomLightType::Cylinder: return "Cylinder";
    case CustomLightType::Distant: return "Distant";
    case CustomLightType::Dome: return "Dome";
    }
    return "Sphere";
}

static void NormalizeUI(float v[3]) {
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

void DrawCustomLightsTab(CustomLightsManager& manager) {
    static uint32_t selectedId = 0;
    static char filePath[MAX_PATH] = "custom_lights.cltx";
    static bool radOpen[6] = { true, true, true, true, true, true };
    static bool posOpen[6] = { true, true, true, true, false, false };
    static bool shapeOpen[6] = { true, true, true, true, true, true };
    static bool shapingOpen[6] = { false, false, false, false, false, false };
    static bool animOpen[6] = { false, false, false, false, false, false };

    const char* kAnimNames[] = { "None", "Pulse", "Strobe", "FadeIn", "FadeOut", "Flicker", "ColorCycle", "Breathe", "FireFlicker", "ElectricFlicker" };

    ImGui::Columns(3, "CLCols", true);

    PushOverlayBoldFont(); ImGui::Text("Lights"); PopOverlayBoldFont();
    ImGui::Separator();
    if (ImGui::Button("+ Add Light")) ImGui::OpenPopup("AddLightPopup");
    if (ImGui::BeginPopup("AddLightPopup")) {
        for (int i = 0; i < 6; ++i) {
            char row[96];
            snprintf(row, sizeof(row), "%s - %s", TypeLabel(static_cast<CustomLightType>(i)), i == 5 ? "environment map light" : "direct light source");
            if (ImGui::Selectable(row)) {
                CustomLight& nl = manager.AddLight(static_cast<CustomLightType>(i));
                selectedId = nl.id;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::BeginChild("CLList", ImVec2(0, -60), true);
    for (auto& l : manager.Lights()) {
        bool en = l.enabled;
        ImGui::PushID(static_cast<int>(l.id));
        if (ImGui::Checkbox("##en", &en)) { l.enabled = en; l.dirty = true; }
        ImGui::SameLine();
        char label[96]; snprintf(label, sizeof(label), "[%s] %s###sl_%u", TypeLabel(l.type), l.name, l.id);
        if (ImGui::Selectable(label, selectedId == l.id)) selectedId = l.id;
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("Remove")) manager.RemoveLight(selectedId);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Permanently deletes the selected light from this session.");
    ImGui::SameLine();
    if (ImGui::Button("Clear Handles")) manager.DestroyAllNativeHandles();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Destroys native handles. They are recreated automatically next frame.");

    ImGui::NextColumn();
    PushOverlayBoldFont(); ImGui::Text("Edit Light"); PopOverlayBoldFont();
    ImGui::Separator();

    CustomLight* lp = nullptr;
    for (auto& l : manager.Lights()) if (l.id == selectedId) { lp = &l; break; }
    if (!lp) { ImGui::TextDisabled("Select a light to edit."); ImGui::NextColumn(); goto column3; }

    {
        CustomLight& l = *lp;
        int typeIdx = static_cast<int>(l.type);
        if (ImGui::InputText("Name", l.name, sizeof(l.name))) l.dirty = true;
        if (ImGui::Combo("Type", &typeIdx, "Sphere\0Rect\0Disk\0Cylinder\0Distant\0Dome\0")) {
            l.type = static_cast<CustomLightType>(typeIdx);
            l.dirty = true;
            radOpen[typeIdx] = true; posOpen[typeIdx] = typeIdx < 4; shapeOpen[typeIdx] = true; shapingOpen[typeIdx] = false; animOpen[typeIdx] = false;
        }
        if (ImGui::Checkbox("Enabled", &l.enabled)) l.dirty = true;

        const int t = static_cast<int>(l.type);
        ImGui::SetNextItemOpen(radOpen[t], ImGuiCond_Once);
        PushOverlayBoldFont();
        if (ImGui::CollapsingHeader("Radiance", &radOpen[t])) {
            PopOverlayBoldFont();
            float col4[4] = { l.color[0], l.color[1], l.color[2], 1.0f };
            ImGui::ColorEdit3("Color", col4); l.color[0] = col4[0]; l.color[1] = col4[1]; l.color[2] = col4[2];
            if (ImGui::ColorPicker4("##color", col4, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_PickerHueWheel)) {
                l.color[0] = col4[0]; l.color[1] = col4[1]; l.color[2] = col4[2]; l.dirty = true;
            }
            if (ImGui::SliderFloat("Intensity", &l.intensity, 0.0f, 100000.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) l.dirty = true;
            if (ImGui::SliderFloat("Volumetric Scale", &l.volumetricRadianceScale, 0.0f, 10.0f, "%.3f")) l.dirty = true;
        } else { PopOverlayBoldFont(); }

        ImGui::SetNextItemOpen(posOpen[t], ImGuiCond_Once);
        PushOverlayBoldFont();
        if (t < 4 && ImGui::CollapsingHeader("Position", &posOpen[t])) {
            PopOverlayBoldFont();
            if (ImGui::InputFloat3("Position", l.position, "%.3f")) l.dirty = true;
            if (ImGui::Checkbox("Follow camera", &l.followCamera)) l.dirty = true;
            if (l.followCamera && ImGui::InputFloat3("Camera offset", l.cameraOffset, "%.3f")) l.dirty = true;
        } else { PopOverlayBoldFont(); }

        ImGui::SetNextItemOpen(shapeOpen[t], ImGuiCond_Once);
        PushOverlayBoldFont();
        if (ImGui::CollapsingHeader("Shape", &shapeOpen[t])) {
            PopOverlayBoldFont();
            if (t == 0 || t == 3) if (ImGui::SliderFloat("Radius", &l.radius, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) l.dirty = true;
            if (t == 1 || t == 2) {
                if (ImGui::InputFloat3("X Axis", l.xAxis, "%.3f")) { NormalizeUI(l.xAxis); l.dirty = true; }
                if (ImGui::InputFloat3("Y Axis", l.yAxis, "%.3f")) { NormalizeUI(l.yAxis); l.dirty = true; }
            }
            if (t == 1) { if (ImGui::SliderFloat("X Size", &l.xSize, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) l.dirty = true; if (ImGui::SliderFloat("Y Size", &l.ySize, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) l.dirty = true; }
            if (t == 2) { if (ImGui::SliderFloat("X Radius", &l.xRadius, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) l.dirty = true; if (ImGui::SliderFloat("Y Radius", &l.yRadius, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) l.dirty = true; }
            if (t == 3) { if (ImGui::InputFloat3("Axis", l.axis, "%.3f")) { NormalizeUI(l.axis); l.dirty = true; } if (ImGui::SliderFloat("Axis Length", &l.axisLength, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) l.dirty = true; }
            if (t == 4) { if (ImGui::InputFloat3("Direction", l.direction, "%.3f")) { NormalizeUI(l.direction); l.dirty = true; } if (ImGui::SliderFloat("Angular Diameter", &l.angularDiameterDegrees, 0.1f, 90.0f, "%.3f")) l.dirty = true; }
            if (t == 5) { if (ImGui::InputText("Texture Path", l.domeTexturePath, MAX_PATH)) l.dirty = true; }
        } else { PopOverlayBoldFont(); }

        ImGui::SetNextItemOpen(shapingOpen[t], ImGuiCond_Once);
        PushOverlayBoldFont();
        if ((t == 0 || t == 1 || t == 2) && ImGui::CollapsingHeader("Shaping", &shapingOpen[t])) {
            PopOverlayBoldFont();
            if (ImGui::Checkbox("Enable", &l.shaping.enabled)) l.dirty = true;
            if (l.shaping.enabled) {
                if (ImGui::InputFloat3("Direction", l.shaping.direction, "%.3f")) { NormalizeUI(l.shaping.direction); l.dirty = true; }
                if (ImGui::SliderFloat("Cone Angle", &l.shaping.coneAngleDegrees, 1.0f, 179.0f, "%.2f")) l.dirty = true;
            }
        } else { PopOverlayBoldFont(); }

        ImGui::SetNextItemOpen(animOpen[t], ImGuiCond_Once);
        PushOverlayBoldFont();
        if (ImGui::CollapsingHeader("Animation", &animOpen[t])) {
            PopOverlayBoldFont();
            int animIdx = static_cast<int>(l.animation.mode);
            if (ImGui::Combo("Mode", &animIdx, kAnimNames, 10)) { l.animation.mode = static_cast<AnimationMode>(animIdx); l.dirty = true; }
            if (ImGui::SliderFloat("Speed (Hz)", &l.animation.speed, 0.01f, 20.0f, "%.2f")) l.dirty = true;
            if (l.animation.mode == AnimationMode::Pulse) ImGui::SliderFloat("Min Scale", &l.animation.minScale, 0.0f, 1.0f, "%.3f");
            if (l.animation.mode == AnimationMode::Strobe) ImGui::SliderFloat("On Fraction", &l.animation.strobeOnFrac, 0.0f, 1.0f, "%.3f");
            if (ImGui::Button("Reset Timer")) l.animation.elapsedTime = 0.0f;
            if (l.animation.mode == AnimationMode::Pulse || l.animation.mode == AnimationMode::Strobe) {
                float samples[64] = {};
                AnimationParams preview = l.animation;
                for (int i = 0; i < 64; ++i) {
                    preview.elapsedTime = (static_cast<float>(i) / 63.0f) / (preview.speed > 0.001f ? preview.speed : 1.0f);
                    samples[i] = CustomLightsManager::SampleAnimatedScale(preview);
                }
                ImGui::PlotLines("##wave", samples, 64, 0, nullptr, 0.0f, 1.05f, ImVec2(-1, 20));
            }
        } else { PopOverlayBoldFont(); }
    }

    ImGui::NextColumn();
column3:
    PushOverlayBoldFont(); ImGui::Text("File"); PopOverlayBoldFont();
    ImGui::Separator();
    ImGui::InputText("##filepath", filePath, sizeof(filePath));
    if (ImGui::Button("Save")) manager.SaveToFile(filePath);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save custom light setup to file.");
    ImGui::SameLine();
    if (ImGui::Button("Load")) { manager.LoadFromFile(filePath); manager.DestroyAllNativeHandles(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load lights and recreate handles next frame.");

    int total = static_cast<int>(manager.Lights().size());
    int active = 0; for (const auto& l : manager.Lights()) if (l.enabled && l.nativeHandle) active++;
    ImGui::Separator();
    PushOverlayBoldFont(); ImGui::Text("Status"); PopOverlayBoldFont();
    ImGui::Text("Lights: %d", total);
    ImGui::Text("Active handles: %d", active);
    ImGui::Text("API: %s", remix_api::g_initialized ? "Ready" : "Not initialized");

    ImGui::Columns(1);
}
