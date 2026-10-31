#include <Axle/CurveEditorWidget.hpp>
#include <imgui.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <string>

namespace Axle {

    bool CurveEditorWidget::DrawCurveEditor(const char* label, FloatCurve& curve, float height) {
        bool modified = false;

        ImGui::PushID(label);
        ImGui::Text("%s", label);
        ImGui::SameLine();

        // Preset shortcuts
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 2));
        if (ImGui::SmallButton("Lin")) {
            curve.SetLinear(1.0f, 0.0f);
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("EaseIn")) {
            curve.SetEaseIn(1.0f, 0.0f);
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("EaseOut")) {
            curve.SetEaseOut(1.0f, 0.0f);
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("EaseInOut")) {
            curve.SetEaseInOut(1.0f, 0.0f);
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Bell")) {
            curve.SetBell(0.0f, 1.0f, 0.0f);
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Flat")) {
            curve.SetConstant(1.0f);
            modified = true;
        }
        ImGui::PopStyleVar(2);

        float width = ImGui::GetContentRegionAvail().x;
        if (width < 50.0f) width = 50.0f;

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImVec2(width, height);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Background
        ImVec2 p0 = canvasPos;
        ImVec2 p1 = ImVec2(p0.x + canvasSize.x, p0.y + canvasSize.y);
        drawList->AddRectFilled(p0, p1, IM_COL32(20, 22, 26, 255), 4.0f);
        drawList->AddRect(p0, p1, IM_COL32(50, 55, 65, 255), 4.0f);

        // Grid lines (horizontal: 0.25, 0.5, 0.75; vertical: 0.25, 0.5, 0.75)
        for (float g = 0.25f; g <= 0.75f; g += 0.25f) {
            float y = p1.y - g * canvasSize.y;
            drawList->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), IM_COL32(35, 40, 48, 180));
            float x = p0.x + g * canvasSize.x;
            drawList->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(35, 40, 48, 180));
        }

        // Invisible button to capture mouse interaction
        ImGui::InvisibleButton("##CurveCanvas", canvasSize);
        bool isHovered = ImGui::IsItemHovered();
        bool isActive = ImGui::IsItemActive();

        auto ValueToScreen = [&](const glm::vec2& pt) -> ImVec2 {
            float sx = p0.x + std::clamp(pt.x, 0.0f, 1.0f) * canvasSize.x;
            float sy = p1.y - std::clamp(pt.y, 0.0f, 1.0f) * canvasSize.y;
            return ImVec2(sx, sy);
        };

        auto ScreenToValue = [&](const ImVec2& s) -> glm::vec2 {
            float vx = (canvasSize.x > 0.0f) ? (s.x - p0.x) / canvasSize.x : 0.0f;
            float vy = (canvasSize.y > 0.0f) ? (p1.y - s.y) / canvasSize.y : 0.0f;
            return glm::vec2(std::clamp(vx, 0.0f, 1.0f), std::clamp(vy, 0.0f, 1.0f));
        };

        ImVec2 sp0 = ValueToScreen(curve.P0);
        ImVec2 sp1 = ValueToScreen(curve.P1);
        ImVec2 sp2 = ValueToScreen(curve.P2);
        ImVec2 sp3 = ValueToScreen(curve.P3);

        // Tangent handles lines
        drawList->AddLine(sp0, sp1, IM_COL32(200, 180, 50, 160), 1.0f);
        drawList->AddLine(sp3, sp2, IM_COL32(200, 180, 50, 160), 1.0f);

        // Polyline curve preview
        const int numSegments = 32;
        ImVec2 polyPoints[numSegments + 1];
        for (int i = 0; i <= numSegments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(numSegments);
            float v = curve.Evaluate(t);
            polyPoints[i] = ValueToScreen(glm::vec2(t, v));
        }
        drawList->AddPolyline(polyPoints, numSegments + 1, IM_COL32(60, 200, 255, 255), false, 2.0f);

        // Control point handles interaction
        static int s_ActiveHandle = -1;
        ImVec2 mousePos = ImGui::GetIO().MousePos;

        if (ImGui::IsMouseClicked(0) && isHovered) {
            s_ActiveHandle = -1;
            auto CheckHandle = [&](const ImVec2& hp, int idx) {
                float distSq = (hp.x - mousePos.x) * (hp.x - mousePos.x) + (hp.y - mousePos.y) * (hp.y - mousePos.y);
                if (distSq <= 100.0f) s_ActiveHandle = idx;
            };
            CheckHandle(sp0, 0);
            if (s_ActiveHandle == -1) CheckHandle(sp1, 1);
            if (s_ActiveHandle == -1) CheckHandle(sp2, 2);
            if (s_ActiveHandle == -1) CheckHandle(sp3, 3);
        }

        if (ImGui::IsMouseReleased(0)) {
            s_ActiveHandle = -1;
        }

        if (s_ActiveHandle >= 0 && ImGui::IsMouseDragging(0)) {
            glm::vec2 val = ScreenToValue(mousePos);
            curve.Type = CurveType::CubicBezier;
            if (s_ActiveHandle == 0) {
                curve.P0.y = val.y;
            } else if (s_ActiveHandle == 1) {
                curve.P1 = val;
            } else if (s_ActiveHandle == 2) {
                curve.P2 = val;
            } else if (s_ActiveHandle == 3) {
                curve.P3.y = val.y;
            }
            modified = true;
        }

        // Draw handle dots
        auto DrawHandle = [&](const ImVec2& pt, ImU32 col, bool isCtrl) {
            drawList->AddCircleFilled(pt, isCtrl ? 4.0f : 5.0f, col);
            drawList->AddCircle(pt, isCtrl ? 4.0f : 5.0f, IM_COL32(255, 255, 255, 220), 0, 1.2f);
        };

        DrawHandle(sp0, IM_COL32(50, 220, 100, 255), false);
        DrawHandle(sp1, IM_COL32(240, 200, 60, 255), true);
        DrawHandle(sp2, IM_COL32(240, 200, 60, 255), true);
        DrawHandle(sp3, IM_COL32(230, 80, 80, 255), false);

        ImGui::PopID();
        return modified;
    }

    bool CurveEditorWidget::DrawGradientEditor(const char* label, ColorGradient& gradient, float height) {
        bool modified = false;

        ImGui::PushID(label);
        ImGui::Text("%s", label);
        ImGui::SameLine();

        // Presets
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 2));
        if (ImGui::SmallButton("Flame")) {
            gradient.SetFlamePreset();
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Smoke")) {
            gradient.SetSmokePreset();
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Sparks")) {
            gradient.SetSparksPreset();
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Rainbow")) {
            gradient.ColorKeys = {
                { 0.0f, glm::vec3(1.0f, 0.0f, 0.0f) },
                { 0.2f, glm::vec3(1.0f, 0.7f, 0.0f) },
                { 0.4f, glm::vec3(0.0f, 1.0f, 0.0f) },
                { 0.6f, glm::vec3(0.0f, 0.7f, 1.0f) },
                { 0.8f, glm::vec3(0.5f, 0.0f, 1.0f) },
                { 1.0f, glm::vec3(1.0f, 0.0f, 0.5f) }
            };
            gradient.AlphaKeys = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
            gradient.StartColor = glm::vec4(gradient.ColorKeys.front().Color, 1.0f);
            gradient.EndColor = glm::vec4(gradient.ColorKeys.back().Color, 1.0f);
            modified = true;
        }
        ImGui::PopStyleVar(2);

        float width = ImGui::GetContentRegionAvail().x;
        if (width < 50.0f) width = 50.0f;

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 1. Draw Checkerboard background for alpha transparency indication
        const float checkSize = 6.0f;
        for (float y = p0.y; y < p1.y; y += checkSize) {
            for (float x = p0.x; x < p1.x; x += checkSize) {
                int cx = static_cast<int>((x - p0.x) / checkSize);
                int cy = static_cast<int>((y - p0.y) / checkSize);
                ImU32 col = ((cx + cy) % 2 == 0) ? IM_COL32(40, 40, 45, 255) : IM_COL32(60, 60, 68, 255);
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + checkSize, p1.x), std::min(y + checkSize, p1.y)), col);
            }
        }

        // 2. Draw gradient ramp slices
        const int numSlices = 64;
        float sliceWidth = width / static_cast<float>(numSlices);
        for (int i = 0; i < numSlices; ++i) {
            float t0 = static_cast<float>(i) / static_cast<float>(numSlices);
            float t1 = static_cast<float>(i + 1) / static_cast<float>(numSlices);
            glm::vec4 c0 = gradient.Evaluate(t0);
            glm::vec4 c1 = gradient.Evaluate(t1);

            ImU32 col0 = IM_COL32(static_cast<int>(c0.r * 255), static_cast<int>(c0.g * 255), static_cast<int>(c0.b * 255), static_cast<int>(c0.a * 255));
            ImU32 col1 = IM_COL32(static_cast<int>(c1.r * 255), static_cast<int>(c1.g * 255), static_cast<int>(c1.b * 255), static_cast<int>(c1.a * 255));

            ImVec2 sp0 = ImVec2(p0.x + i * sliceWidth, p0.y);
            ImVec2 sp1 = ImVec2(p0.x + (i + 1) * sliceWidth, p1.y);

            drawList->AddRectFilledMultiColor(sp0, sp1, col0, col1, col1, col0);
        }

        // Border
        drawList->AddRect(p0, p1, IM_COL32(80, 85, 95, 255), 2.0f);

        // Invisible button for interaction
        ImGui::InvisibleButton("##GradientRamp", ImVec2(width, height + 16.0f));
        bool isHovered = ImGui::IsItemHovered();

        static int s_SelectedColorKey = -1;
        ImVec2 mousePos = ImGui::GetIO().MousePos;

        // Key markers
        float markerY = p1.y + 4.0f;
        for (size_t k = 0; k < gradient.ColorKeys.size(); ++k) {
            float kx = p0.x + gradient.ColorKeys[k].Time * width;
            ImVec2 tip = ImVec2(kx, p1.y);
            ImVec2 bl = ImVec2(kx - 5.0f, markerY + 8.0f);
            ImVec2 br = ImVec2(kx + 5.0f, markerY + 8.0f);

            const auto& rgb = gradient.ColorKeys[k].Color;
            ImU32 kcol = IM_COL32(static_cast<int>(rgb.r * 255), static_cast<int>(rgb.g * 255), static_cast<int>(rgb.b * 255), 255);

            drawList->AddTriangleFilled(tip, bl, br, kcol);
            drawList->AddTriangle(tip, bl, br, (s_SelectedColorKey == static_cast<int>(k)) ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 200), 1.2f);
        }

        // Handle clicking / selecting keys
        if (ImGui::IsMouseClicked(0) && isHovered) {
            s_SelectedColorKey = -1;
            for (size_t k = 0; k < gradient.ColorKeys.size(); ++k) {
                float kx = p0.x + gradient.ColorKeys[k].Time * width;
                if (std::abs(mousePos.x - kx) <= 7.0f && mousePos.y >= p0.y && mousePos.y <= markerY + 12.0f) {
                    s_SelectedColorKey = static_cast<int>(k);
                    break;
                }
            }
        }

        // Dragging key
        if (s_SelectedColorKey >= 0 && s_SelectedColorKey < static_cast<int>(gradient.ColorKeys.size()) && ImGui::IsMouseDragging(0)) {
            float newT = std::clamp((mousePos.x - p0.x) / width, 0.0f, 1.0f);
            gradient.ColorKeys[s_SelectedColorKey].Time = newT;
            modified = true;
        }

        // Double click to add key
        if (ImGui::IsMouseDoubleClicked(0) && isHovered) {
            float newT = std::clamp((mousePos.x - p0.x) / width, 0.0f, 1.0f);
            glm::vec4 sampleCol = gradient.Evaluate(newT);
            gradient.ColorKeys.push_back({ newT, glm::vec3(sampleCol) });
            std::sort(gradient.ColorKeys.begin(), gradient.ColorKeys.end(), [](const auto& a, const auto& b) {
                return a.Time < b.Time;
            });
            modified = true;
        }

        // Edit selected color key
        if (s_SelectedColorKey >= 0 && s_SelectedColorKey < static_cast<int>(gradient.ColorKeys.size())) {
            ImGui::Spacing();
            ImGui::PushItemWidth(180.0f);
            if (ImGui::ColorEdit3("Key Color", &gradient.ColorKeys[s_SelectedColorKey].Color.r)) {
                modified = true;
            }
            ImGui::SameLine();
            if (ImGui::SliderFloat("Key Time", &gradient.ColorKeys[s_SelectedColorKey].Time, 0.0f, 1.0f, "%.2f")) {
                modified = true;
            }
            ImGui::SameLine();
            if (gradient.ColorKeys.size() > 1 && ImGui::SmallButton("Delete Key")) {
                gradient.ColorKeys.erase(gradient.ColorKeys.begin() + s_SelectedColorKey);
                s_SelectedColorKey = -1;
                modified = true;
            }
            ImGui::PopItemWidth();
        }

        ImGui::PopID();
        return modified;
    }

}
