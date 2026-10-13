#include <Axle/DebugPanel.hpp>
#include <imgui.h>

namespace Axle {

    void DebugPanel::OnImGuiRender(TelemetryData& telemetry, bool* p_open) {
        if (p_open && !*p_open) return;

        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340.0f, 480.0f), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Performance & Debug", p_open)) {
            ImGui::End();
            return;
        }

        // 1. Performance Profiler
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Performance Metrics");
        ImGui::Separator();
        
        ImVec4 fpsColor = telemetry.FPS >= 55.0f ? ImVec4(0.3f, 0.95f, 0.4f, 1.0f)
                        : (telemetry.FPS >= 30.0f ? ImVec4(1.0f, 0.85f, 0.2f, 1.0f)
                                                  : ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextColored(fpsColor, "Frames Per Second: %.1f FPS", telemetry.FPS);
        ImGui::Text("Frame Time: %.2f ms", telemetry.DeltaTime * 1000.0f);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Camera Transform");
        ImGui::Separator();
        ImGui::Text("Pos: (%.2f, %.2f, %.2f)", 
                    telemetry.CameraPosition.x, telemetry.CameraPosition.y, telemetry.CameraPosition.z);
        ImGui::Text("Rot: Pitch %.1f deg | Yaw %.1f deg", 
                    telemetry.CameraRotation.x, telemetry.CameraRotation.y);

        // 2. Jolt Physics Diagnostics & Visualization
        if (telemetry.HasPhysics) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Jolt Physics Engine");
            ImGui::Separator();
            ImGui::Text("Bodies: %u Total (%u Active)", telemetry.TotalBodies, telemetry.ActiveBodies);

            ImGui::Spacing();
            ImGui::Checkbox("Show Physics Wireframe [F1]", &telemetry.ShowPhysicsWireframe);
            if (telemetry.ShowPhysicsWireframe) {
                ImGui::Indent(12.0f);
                ImGui::Checkbox("Draw Bounding Boxes", &telemetry.DrawBoundingBoxes);
                ImGui::Checkbox("Draw Center of Mass", &telemetry.DrawCenterOfMass);
                ImGui::Checkbox("Draw Velocity Vectors", &telemetry.DrawVelocity);
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Debug Lines: %u", telemetry.DebugLineCount);
                ImGui::Unindent(12.0f);
            }
        }

        // 3. Controls Quick Guide
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Editor Controls Guide")) {
            ImGui::BulletText("Play/Stop Simulation: Toolbar [Play] / [Stop]");
            ImGui::BulletText("View Navigation: Hold RMB + WASD to fly (Shift: Speed up)");
            ImGui::BulletText("Transform Gizmo: W (Move), E (Rotate), R (Scale)");
            ImGui::BulletText("Physics Wireframe: F1 toggle");
            ImGui::BulletText("Vehicle Controls: WASD / Arrows (Space: Handbrake)");
            ImGui::BulletText("Reset Vehicle: [R] Key");
        }

        ImGui::End();
    }

}
