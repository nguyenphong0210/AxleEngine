#include <Axle/DebugPanel.hpp>
#include <imgui.h>

namespace Axle {

    void DebugPanel::OnImGuiRender(const TelemetryData& telemetry) {
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

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Controls Guide");
        ImGui::Separator();
        ImGui::BulletText("Right-click + Drag: Free-fly look");
        ImGui::BulletText("W / A / S / D: Di chuyen");
        ImGui::BulletText("E / Space: Len");
        ImGui::BulletText("Q / LShift: Xuong");

        ImGui::End();
    }

}
