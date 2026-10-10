#include <Axle/DebugPanel.hpp>
#include <imgui.h>

namespace Axle {

    void DebugPanel::OnImGuiRender(const TelemetryData& telemetry) {
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340.0f, 320.0f), ImGuiCond_FirstUseEver);

        ImGui::Begin("Axle Engine - Telemetry");

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Performance");
        ImGui::Separator();
        ImGui::Text("Frame Time: %.3f ms", telemetry.DeltaTime * 1000.0f);
        ImGui::Text("Frames Per Second: %.1f FPS", telemetry.FPS);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Camera Transform");
        ImGui::Separator();
        ImGui::Text("Position: X: %.2f | Y: %.2f | Z: %.2f", 
                    telemetry.CameraPosition.x, telemetry.CameraPosition.y, telemetry.CameraPosition.z);
        ImGui::Text("Rotation: Pitch: %.1f deg | Yaw: %.1f deg", 
                    telemetry.CameraRotation.x, telemetry.CameraRotation.y);

        if (telemetry.HasPhysics) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Jolt Physics Simulation");
            ImGui::Separator();
            ImGui::Text("Bodies: Total: %u | Active: %u", telemetry.TotalBodies, telemetry.ActiveBodies);
            ImGui::Text("Box Pos: X: %.2f | Y: %.2f | Z: %.2f",
                        telemetry.BoxPosition.x, telemetry.BoxPosition.y, telemetry.BoxPosition.z);
            ImGui::Text("Box Vel: X: %.2f | Y: %.2f | Z: %.2f (Speed: %.2f m/s)",
                        telemetry.BoxVelocity.x, telemetry.BoxVelocity.y, telemetry.BoxVelocity.z,
                        glm::length(telemetry.BoxVelocity));
            ImGui::Text("Box State: %s", telemetry.IsBoxActive ? "Active (Moving / Falling)" : "Sleeping (Resting on floor)");
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "[R] Key: Drop Box Again");
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Controls Guide");
        ImGui::Separator();
        ImGui::BulletText("Right-click + Drag: Free-fly look");
        ImGui::BulletText("W / A / S / D: Move camera");
        ImGui::BulletText("E / Space: Ascend");
        ImGui::BulletText("Q / LShift: Descend");
        ImGui::BulletText("R: Reset Falling Box");

        ImGui::End();
    }

}
