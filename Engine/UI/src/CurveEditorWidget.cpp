#include <Axle/CurveEditorWidget.hpp>
#include <imgui.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <string>
#include <unordered_map>

namespace Axle {

    namespace {
        struct CurveWidgetState {
            int ActiveHandle = -1;
        };

        struct GradientWidgetState {
            int SelectedColorKey = -1;
            int SelectedAlphaKey = -1;
            bool ActiveTrackIsAlpha = false;
        };

        static std::unordered_map<ImGuiID, CurveWidgetState> s_CurveStates;
        static std::unordered_map<ImGuiID, GradientWidgetState> s_GradientStates;
    }

    bool CurveEditorWidget::DrawCurveEditor(const char* label, FloatCurve& curve, float height) {
        bool modified = false;

        ImGuiID widgetId = ImGui::GetID(label);
        CurveWidgetState& state = s_CurveStates[widgetId];

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
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            curve.SetLinear(1.0f, 0.0f);
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
        ImVec2 mousePos = ImGui::GetIO().MousePos;

        if (ImGui::IsMouseClicked(0) && isHovered) {
            state.ActiveHandle = -1;
            auto CheckHandle = [&](const ImVec2& hp, int idx) {
                float distSq = (hp.x - mousePos.x) * (hp.x - mousePos.x) + (hp.y - mousePos.y) * (hp.y - mousePos.y);
                if (distSq <= 100.0f) state.ActiveHandle = idx;
            };
            CheckHandle(sp0, 0);
            if (state.ActiveHandle == -1) CheckHandle(sp1, 1);
            if (state.ActiveHandle == -1) CheckHandle(sp2, 2);
            if (state.ActiveHandle == -1) CheckHandle(sp3, 3);
        }

        if (ImGui::IsMouseReleased(0)) {
            state.ActiveHandle = -1;
        }

        if (state.ActiveHandle >= 0 && ImGui::IsMouseDragging(0)) {
            glm::vec2 val = ScreenToValue(mousePos);
            curve.Type = CurveType::CubicBezier;
            if (state.ActiveHandle == 0) {
                curve.P0.y = val.y;
            } else if (state.ActiveHandle == 1) {
                curve.P1 = val;
            } else if (state.ActiveHandle == 2) {
                curve.P2 = val;
            } else if (state.ActiveHandle == 3) {
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

        // Numeric entry row for control points
        ImGui::PushItemWidth(70.0f);
        if (ImGui::DragFloat("P0 Y", &curve.P0.y, 0.01f, 0.0f, 1.0f, "%.2f")) {
            curve.Type = CurveType::CubicBezier;
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::DragFloat2("P1 (T,V)", &curve.P1.x, 0.01f, 0.0f, 1.0f, "%.2f")) {
            curve.P1 = glm::clamp(curve.P1, glm::vec2(0.0f), glm::vec2(1.0f));
            curve.Type = CurveType::CubicBezier;
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::DragFloat2("P2 (T,V)", &curve.P2.x, 0.01f, 0.0f, 1.0f, "%.2f")) {
            curve.P2 = glm::clamp(curve.P2, glm::vec2(0.0f), glm::vec2(1.0f));
            curve.Type = CurveType::CubicBezier;
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::DragFloat("P3 Y", &curve.P3.y, 0.01f, 0.0f, 1.0f, "%.2f")) {
            curve.Type = CurveType::CubicBezier;
            modified = true;
        }
        ImGui::PopItemWidth();

        ImGui::PopID();
        return modified;
    }

    bool CurveEditorWidget::DrawGradientEditor(const char* label, ColorGradient& gradient, float height) {
        bool modified = false;

        ImGuiID widgetId = ImGui::GetID(label);
        GradientWidgetState& state = s_GradientStates[widgetId];

        ImGui::PushID(label);
        ImGui::Text("%s", label);
        ImGui::SameLine();

        // Presets
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 2));
        if (ImGui::SmallButton("Flame")) {
            gradient.SetFlamePreset();
            state.SelectedColorKey = -1;
            state.SelectedAlphaKey = -1;
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Smoke")) {
            gradient.SetSmokePreset();
            state.SelectedColorKey = -1;
            state.SelectedAlphaKey = -1;
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Sparks")) {
            gradient.SetSparksPreset();
            state.SelectedColorKey = -1;
            state.SelectedAlphaKey = -1;
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
            state.SelectedColorKey = -1;
            state.SelectedAlphaKey = -1;
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            gradient.SetSimple(glm::vec4(1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
            state.SelectedColorKey = -1;
            state.SelectedAlphaKey = -1;
            modified = true;
        }
        ImGui::PopStyleVar(2);

        float width = ImGui::GetContentRegionAvail().x;
        if (width < 50.0f) width = 50.0f;

        const float alphaTrackHeight = 12.0f;
        const float rampHeight = height;
        const float colorTrackHeight = 12.0f;
        const float totalHeight = alphaTrackHeight + rampHeight + colorTrackHeight;

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 pAlphaEnd = ImVec2(p0.x + width, p0.y + alphaTrackHeight);
        ImVec2 pRampStart = ImVec2(p0.x, pAlphaEnd.y);
        ImVec2 pRampEnd = ImVec2(p0.x + width, pRampStart.y + rampHeight);
        ImVec2 pColorStart = ImVec2(p0.x, pRampEnd.y);
        ImVec2 pColorEnd = ImVec2(p0.x + width, pColorStart.y + colorTrackHeight);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 1. Draw Alpha Track Background
        drawList->AddRectFilled(p0, pAlphaEnd, IM_COL32(30, 32, 38, 255));
        drawList->AddRect(p0, pAlphaEnd, IM_COL32(50, 55, 65, 255));

        // 2. Draw Checkerboard background for the gradient ramp
        const float checkSize = 6.0f;
        for (float y = pRampStart.y; y < pRampEnd.y; y += checkSize) {
            for (float x = pRampStart.x; x < pRampEnd.x; x += checkSize) {
                int cx = static_cast<int>((x - pRampStart.x) / checkSize);
                int cy = static_cast<int>((y - pRampStart.y) / checkSize);
                ImU32 col = ((cx + cy) % 2 == 0) ? IM_COL32(40, 40, 45, 255) : IM_COL32(60, 60, 68, 255);
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + checkSize, pRampEnd.x), std::min(y + checkSize, pRampEnd.y)), col);
            }
        }

        // 3. Draw gradient ramp slices
        const int numSlices = 64;
        float sliceWidth = width / static_cast<float>(numSlices);
        for (int i = 0; i < numSlices; ++i) {
            float t0 = static_cast<float>(i) / static_cast<float>(numSlices);
            float t1 = static_cast<float>(i + 1) / static_cast<float>(numSlices);
            glm::vec4 c0 = gradient.Evaluate(t0);
            glm::vec4 c1 = gradient.Evaluate(t1);

            ImU32 col0 = IM_COL32(static_cast<int>(c0.r * 255), static_cast<int>(c0.g * 255), static_cast<int>(c0.b * 255), static_cast<int>(c0.a * 255));
            ImU32 col1 = IM_COL32(static_cast<int>(c1.r * 255), static_cast<int>(c1.g * 255), static_cast<int>(c1.b * 255), static_cast<int>(c1.a * 255));

            ImVec2 sp0 = ImVec2(pRampStart.x + i * sliceWidth, pRampStart.y);
            ImVec2 sp1 = ImVec2(pRampStart.x + (i + 1) * sliceWidth, pRampEnd.y);

            drawList->AddRectFilledMultiColor(sp0, sp1, col0, col1, col1, col0);
        }
        drawList->AddRect(pRampStart, pRampEnd, IM_COL32(80, 85, 95, 255), 1.0f);

        // 4. Draw Color Track Background
        drawList->AddRectFilled(pColorStart, pColorEnd, IM_COL32(30, 32, 38, 255));
        drawList->AddRect(pColorStart, pColorEnd, IM_COL32(50, 55, 65, 255));

        // 5. Draw Alpha Markers (downward pointing triangles in alpha track)
        for (size_t k = 0; k < gradient.AlphaKeys.size(); ++k) {
            float kx = p0.x + gradient.AlphaKeys[k].Time * width;
            ImVec2 tip = ImVec2(kx, pRampStart.y);
            ImVec2 tl = ImVec2(kx - 5.0f, p0.y + 1.0f);
            ImVec2 tr = ImVec2(kx + 5.0f, p0.y + 1.0f);

            float a = gradient.AlphaKeys[k].Alpha;
            int gray = static_cast<int>(a * 255.0f);
            ImU32 acol = IM_COL32(gray, gray, gray, 255);

            drawList->AddTriangleFilled(tip, tl, tr, acol);
            bool isSelected = state.ActiveTrackIsAlpha && (state.SelectedAlphaKey == static_cast<int>(k));
            drawList->AddTriangle(tip, tl, tr, isSelected ? IM_COL32(255, 220, 50, 255) : IM_COL32(0, 0, 0, 220), isSelected ? 1.5f : 1.0f);
        }

        // 6. Draw Color Markers (upward pointing triangles in color track)
        for (size_t k = 0; k < gradient.ColorKeys.size(); ++k) {
            float kx = p0.x + gradient.ColorKeys[k].Time * width;
            ImVec2 tip = ImVec2(kx, pRampEnd.y);
            ImVec2 bl = ImVec2(kx - 5.0f, pColorEnd.y - 1.0f);
            ImVec2 br = ImVec2(kx + 5.0f, pColorEnd.y - 1.0f);

            const auto& rgb = gradient.ColorKeys[k].Color;
            ImU32 kcol = IM_COL32(static_cast<int>(rgb.r * 255), static_cast<int>(rgb.g * 255), static_cast<int>(rgb.b * 255), 255);

            drawList->AddTriangleFilled(tip, bl, br, kcol);
            bool isSelected = !state.ActiveTrackIsAlpha && (state.SelectedColorKey == static_cast<int>(k));
            drawList->AddTriangle(tip, bl, br, isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 220), isSelected ? 1.5f : 1.0f);
        }

        // Invisible button capturing the whole widget area
        ImGui::InvisibleButton("##GradientWidget", ImVec2(width, totalHeight));
        bool isHovered = ImGui::IsItemHovered();
        ImVec2 mousePos = ImGui::GetIO().MousePos;

        // Interaction: Selecting Keys
        if (ImGui::IsMouseClicked(0) && isHovered) {
            if (mousePos.y <= pRampStart.y + (rampHeight * 0.4f)) {
                // Alpha track or upper ramp clicked
                state.ActiveTrackIsAlpha = true;
                state.SelectedAlphaKey = -1;
                for (size_t k = 0; k < gradient.AlphaKeys.size(); ++k) {
                    float kx = p0.x + gradient.AlphaKeys[k].Time * width;
                    if (std::abs(mousePos.x - kx) <= 8.0f) {
                        state.SelectedAlphaKey = static_cast<int>(k);
                        break;
                    }
                }
            } else {
                // Color track or lower ramp clicked
                state.ActiveTrackIsAlpha = false;
                state.SelectedColorKey = -1;
                for (size_t k = 0; k < gradient.ColorKeys.size(); ++k) {
                    float kx = p0.x + gradient.ColorKeys[k].Time * width;
                    if (std::abs(mousePos.x - kx) <= 8.0f) {
                        state.SelectedColorKey = static_cast<int>(k);
                        break;
                    }
                }
            }
        }

        // Interaction: Dragging
        if (ImGui::IsMouseDragging(0)) {
            float newT = std::clamp((mousePos.x - p0.x) / width, 0.0f, 1.0f);
            if (state.ActiveTrackIsAlpha && state.SelectedAlphaKey >= 0 && state.SelectedAlphaKey < static_cast<int>(gradient.AlphaKeys.size())) {
                gradient.AlphaKeys[state.SelectedAlphaKey].Time = newT;
                modified = true;
            } else if (!state.ActiveTrackIsAlpha && state.SelectedColorKey >= 0 && state.SelectedColorKey < static_cast<int>(gradient.ColorKeys.size())) {
                gradient.ColorKeys[state.SelectedColorKey].Time = newT;
                modified = true;
            }
        }

        // Interaction: Double click to add key
        if (ImGui::IsMouseDoubleClicked(0) && isHovered) {
            float newT = std::clamp((mousePos.x - p0.x) / width, 0.0f, 1.0f);
            if (mousePos.y <= pRampStart.y + (rampHeight * 0.4f)) {
                // Add Alpha Key
                float sampleA = gradient.Evaluate(newT).a;
                gradient.AddAlphaKey(newT, sampleA);
                for (size_t k = 0; k < gradient.AlphaKeys.size(); ++k) {
                    if (std::abs(gradient.AlphaKeys[k].Time - newT) < 1e-4f) {
                        state.SelectedAlphaKey = static_cast<int>(k);
                        break;
                    }
                }
                state.ActiveTrackIsAlpha = true;
                modified = true;
            } else {
                // Add Color Key
                glm::vec3 sampleCol = glm::vec3(gradient.Evaluate(newT));
                gradient.AddColorKey(newT, sampleCol);
                for (size_t k = 0; k < gradient.ColorKeys.size(); ++k) {
                    if (std::abs(gradient.ColorKeys[k].Time - newT) < 1e-4f) {
                        state.SelectedColorKey = static_cast<int>(k);
                        break;
                    }
                }
                state.ActiveTrackIsAlpha = false;
                modified = true;
            }
        }

        // Re-sort keys on mouse release if dragging was done
        if (ImGui::IsMouseReleased(0) && modified) {
            if (state.ActiveTrackIsAlpha && state.SelectedAlphaKey >= 0 && state.SelectedAlphaKey < static_cast<int>(gradient.AlphaKeys.size())) {
                float curT = gradient.AlphaKeys[state.SelectedAlphaKey].Time;
                float curA = gradient.AlphaKeys[state.SelectedAlphaKey].Alpha;
                std::stable_sort(gradient.AlphaKeys.begin(), gradient.AlphaKeys.end(), [](const auto& a, const auto& b) {
                    return a.Time < b.Time;
                });
                for (size_t k = 0; k < gradient.AlphaKeys.size(); ++k) {
                    if (gradient.AlphaKeys[k].Time == curT && gradient.AlphaKeys[k].Alpha == curA) {
                        state.SelectedAlphaKey = static_cast<int>(k);
                        break;
                    }
                }
            } else if (!state.ActiveTrackIsAlpha && state.SelectedColorKey >= 0 && state.SelectedColorKey < static_cast<int>(gradient.ColorKeys.size())) {
                float curT = gradient.ColorKeys[state.SelectedColorKey].Time;
                glm::vec3 curC = gradient.ColorKeys[state.SelectedColorKey].Color;
                std::stable_sort(gradient.ColorKeys.begin(), gradient.ColorKeys.end(), [](const auto& a, const auto& b) {
                    return a.Time < b.Time;
                });
                for (size_t k = 0; k < gradient.ColorKeys.size(); ++k) {
                    if (gradient.ColorKeys[k].Time == curT && gradient.ColorKeys[k].Color == curC) {
                        state.SelectedColorKey = static_cast<int>(k);
                        break;
                    }
                }
            }
            if (!gradient.ColorKeys.empty()) {
                gradient.StartColor = glm::vec4(gradient.ColorKeys.front().Color, gradient.AlphaKeys.empty() ? 1.0f : gradient.AlphaKeys.front().Alpha);
                gradient.EndColor = glm::vec4(gradient.ColorKeys.back().Color, gradient.AlphaKeys.empty() ? 0.0f : gradient.AlphaKeys.back().Alpha);
            }
        }

        // Key Inspector & Numerical Controls
        if (state.ActiveTrackIsAlpha && state.SelectedAlphaKey >= 0 && state.SelectedAlphaKey < static_cast<int>(gradient.AlphaKeys.size())) {
            ImGui::Spacing();
            ImGui::PushItemWidth(140.0f);
            if (ImGui::SliderFloat("Alpha Value", &gradient.AlphaKeys[state.SelectedAlphaKey].Alpha, 0.0f, 1.0f, "%.2f")) {
                modified = true;
            }
            ImGui::SameLine();
            if (ImGui::SliderFloat("Alpha Time", &gradient.AlphaKeys[state.SelectedAlphaKey].Time, 0.0f, 1.0f, "%.2f")) {
                modified = true;
            }
            ImGui::SameLine();
            if (gradient.AlphaKeys.size() > 1 && ImGui::SmallButton("Delete Alpha Key")) {
                gradient.AlphaKeys.erase(gradient.AlphaKeys.begin() + state.SelectedAlphaKey);
                state.SelectedAlphaKey = -1;
                modified = true;
            }
            ImGui::PopItemWidth();
        } else if (!state.ActiveTrackIsAlpha && state.SelectedColorKey >= 0 && state.SelectedColorKey < static_cast<int>(gradient.ColorKeys.size())) {
            ImGui::Spacing();
            ImGui::PushItemWidth(180.0f);
            if (ImGui::ColorEdit3("Key Color", &gradient.ColorKeys[state.SelectedColorKey].Color.r)) {
                modified = true;
            }
            ImGui::SameLine();
            if (ImGui::SliderFloat("Key Time", &gradient.ColorKeys[state.SelectedColorKey].Time, 0.0f, 1.0f, "%.2f")) {
                modified = true;
            }
            ImGui::SameLine();
            if (gradient.ColorKeys.size() > 1 && ImGui::SmallButton("Delete Color Key")) {
                gradient.ColorKeys.erase(gradient.ColorKeys.begin() + state.SelectedColorKey);
                state.SelectedColorKey = -1;
                modified = true;
            }
            ImGui::PopItemWidth();
        }

        ImGui::PopID();
        return modified;
    }

}
