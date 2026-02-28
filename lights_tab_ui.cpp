#include "lights_tab_ui.h"

#include "remix_api.h"
#include "remix_lighting_manager.h"
#include "imgui/imgui.h"

#include <vector>
#include <cstdio>

static const char* LightTypeName(RemixLightType t) {
    switch (t) {
    case RemixLightType::Directional: return "Directional";
    case RemixLightType::Point: return "Point";
    case RemixLightType::Spot: return "Spot";
    case RemixLightType::Ambient: return "Ambient";
    default: return "Unknown";
    }
}

static ImVec4 LightTypeColor(RemixLightType t) {
    switch (t) {
    case RemixLightType::Spot: return ImVec4(0.92f, 0.30f, 0.30f, 1.00f);
    case RemixLightType::Point: return ImVec4(0.95f, 0.55f, 0.25f, 1.00f);
    case RemixLightType::Directional: return ImVec4(0.35f, 0.60f, 0.95f, 1.00f);
    case RemixLightType::Ambient: return ImVec4(0.55f, 0.55f, 0.60f, 1.00f);
    default: return ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    }
}

void DrawRemixLightsTab(RemixLightingManager& manager, bool showRuntimeStatus) {
    static uint64_t selectedSignature = 0;
    static char dumpPath[260] = "lights_dump.json";

    auto& settings = manager.Settings();
    const auto& active = manager.ActiveLights();
    int dir = 0, point = 0, spot = 0, ambient = 0;
    int activeHandles = 0;
    for (const auto& kv : active) {
        if (kv.second.handle) ++activeHandles;
        switch (kv.second.type) {
        case RemixLightType::Directional: dir++; break;
        case RemixLightType::Point: point++; break;
        case RemixLightType::Spot: spot++; break;
        case RemixLightType::Ambient: ambient++; break;
        }
    }

    const int total = dir + point + spot + ambient;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    if (ImGui::BeginTable("RemixLightsLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.56f);
        ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.44f);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("LightsListPanel", ImVec2(0, 0), true);
        PushOverlayBoldFont();
        ImGui::Text("Active Lights");
        PopOverlayBoldFont();
        ImGui::Separator();
        ImGui::Text("Total: %d", static_cast<int>(active.size()));

        const float barHeight = 10.0f;
        ImVec2 barStart = ImGui::GetCursorScreenPos();
        ImVec2 barSize(ImGui::GetContentRegionAvail().x, barHeight);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float x = barStart.x;
        auto drawSegment = [&](int count, ImVec4 c) {
            if (total <= 0 || count <= 0) return;
            float w = barSize.x * (static_cast<float>(count) / static_cast<float>(total));
            dl->AddRectFilled(ImVec2(x, barStart.y), ImVec2(x + w, barStart.y + barSize.y), ImGui::ColorConvertFloat4ToU32(c), 4.0f);
            x += w;
        };
        drawSegment(dir, LightTypeColor(RemixLightType::Directional));
        drawSegment(point, LightTypeColor(RemixLightType::Point));
        drawSegment(spot, LightTypeColor(RemixLightType::Spot));
        drawSegment(ambient, LightTypeColor(RemixLightType::Ambient));
        ImGui::Dummy(barSize);

        ImGui::TextColored(LightTypeColor(RemixLightType::Directional), "D:%d", dir); ImGui::SameLine();
        ImGui::TextColored(LightTypeColor(RemixLightType::Point), "P:%d", point); ImGui::SameLine();
        ImGui::TextColored(LightTypeColor(RemixLightType::Spot), "S:%d", spot); ImGui::SameLine();
        ImGui::TextColored(LightTypeColor(RemixLightType::Ambient), "A:%d", ambient);

        ImGui::BeginChild("LightsList", ImVec2(0, 280), true);
        int idx = 0;
        for (const auto& kv : active) {
            const ManagedLight& l = kv.second;
            ImGui::PushID(static_cast<int>(idx++));
            char label[128];
            snprintf(label, sizeof(label), "[%d] %s  I:%.2f###sig_%llu", idx, LightTypeName(l.type), l.intensity, static_cast<unsigned long long>(l.signatureHash));
            bool selected = (selectedSignature == l.signatureHash);
            if (ImGui::Selectable(label, selected)) {
                selectedSignature = l.signatureHash;
            }
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            dl->AddRectFilled(ImVec2(min.x + 4, min.y + 3), ImVec2(min.x + 14, min.y + 13), ImGui::ColorConvertFloat4ToU32(ImVec4(l.color[0], l.color[1], l.color[2], 1.0f)), 2.0f);
            ImVec4 chip = LightTypeColor(l.type);
            dl->AddRectFilled(ImVec2(max.x - 18, min.y + 3), ImVec2(max.x - 6, min.y + 13), ImGui::ColorConvertFloat4ToU32(chip), 2.0f);
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("LightsDetailsPanel", ImVec2(0, 0), true);
        PushOverlayBoldFont();
        ImGui::Text("Light Details & Controls");
        PopOverlayBoldFont();
        ImGui::Separator();

        auto it = active.find(selectedSignature);
        if (it != active.end()) {
            const ManagedLight& l = it->second;
            ImGui::Text("Handle: %p", l.handle);
            ImGui::Text("Type: %s", LightTypeName(l.type));
            ImGui::Text("Color: %.3f %.3f %.3f", l.color[0], l.color[1], l.color[2]);
            ImGui::Text("World direction: %.3f %.3f %.3f", l.direction[0], l.direction[1], l.direction[2]);
            ImGui::Text("World position: %.3f %.3f %.3f", l.position[0], l.position[1], l.position[2]);
            ImGui::Text("Intensity: %.3f", l.intensity);
            ImGui::Text("Cone angle: %.3f", l.coneAngle);
            ImGui::Text("Range: %.3f", l.range);
            ImGui::Text("Signature hash: %llu", static_cast<unsigned long long>(l.signatureHash));
            ImGui::Separator();
        } else {
            ImGui::TextDisabled("Select a light to inspect details.");
            ImGui::Separator();
        }

        ImGui::Checkbox("Enable Remix Lighting Forwarding", &settings.enabled);
        ImGui::SliderFloat("Intensity Multiplier", &settings.intensityMultiplier, 0.0f, 10.0f, "%.2f");
        ImGui::SliderInt("Grace Period", &settings.graceThreshold, 0, 10);
        ImGui::SliderFloat("Ambient Radius", &settings.ambientRadius, 1.0f, 1000000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("Directional", &settings.enableDirectional);
        ImGui::Checkbox("Point", &settings.enablePoint);
        ImGui::Checkbox("Spot", &settings.enableSpot);
        ImGui::Checkbox("Ambient", &settings.enableAmbient);
        if (ImGui::Button("Force Destroy All Lights")) {
            manager.DestroyAllLights();
        }
        ImGui::Checkbox("Debug: Disable Deduplication", &settings.disableDeduplication);
        ImGui::Checkbox("Debug: Freeze Light Updates", &settings.freezeLightUpdates);
        ImGui::InputText("Dump Path", dumpPath, sizeof(dumpPath));
        if (ImGui::Button("Dump Lights To JSON")) {
            manager.DumpLightsToJson(dumpPath);
        }

        float frac = active.empty() ? 0.0f : (static_cast<float>(activeHandles) / static_cast<float>(active.size()));
        ImGui::ProgressBar(frac, ImVec2(-1, 6), "");
        ImGui::Text("Active handles: %d/%d", activeHandles, static_cast<int>(active.size()));
        if (showRuntimeStatus) {
            ImGui::TextWrapped("Runtime: %s", remix_api::g_initialized ? "Remix API ready" : "Remix API not initialized");
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}
