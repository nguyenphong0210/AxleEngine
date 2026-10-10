#include <Axle/DebugPanel.hpp>
#include <imgui.h>

namespace Axle {

    void DebugPanel::OnImGuiRender(TelemetryData& telemetry) {
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350.0f, 520.0f), ImGuiCond_FirstUseEver);

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

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.96f, 0.52f, 0.12f, 1.0f), "Dynamic Box (Orange):");
            ImGui::Text("  Pos: X: %.2f | Y: %.2f | Z: %.2f",
                        telemetry.BoxPosition.x, telemetry.BoxPosition.y, telemetry.BoxPosition.z);
            ImGui::Text("  Vel: X: %.2f | Y: %.2f | Z: %.2f (Speed: %.2f m/s)",
                        telemetry.BoxVelocity.x, telemetry.BoxVelocity.y, telemetry.BoxVelocity.z,
                        glm::length(telemetry.BoxVelocity));
            ImGui::Text("  State: %s", telemetry.IsBoxActive ? "Active" : "Sleeping");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.95f, 1.0f), "Dynamic Sphere (Cyan):");
            ImGui::Text("  Pos: X: %.2f | Y: %.2f | Z: %.2f",
                        telemetry.SpherePosition.x, telemetry.SpherePosition.y, telemetry.SpherePosition.z);
            ImGui::Text("  Vel: X: %.2f | Y: %.2f | Z: %.2f (Speed: %.2f m/s)",
                        telemetry.SphereVelocity.x, telemetry.SphereVelocity.y, telemetry.SphereVelocity.z,
                        glm::length(telemetry.SphereVelocity));
            ImGui::Text("  State: %s", telemetry.IsSphereActive ? "Active" : "Sleeping");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "Debug Collider Visualization");
            ImGui::Separator();
            ImGui::Checkbox("Show Physics Wireframe [F1]", &telemetry.ShowPhysicsWireframe);
            if (telemetry.ShowPhysicsWireframe) {
                ImGui::Checkbox("Draw Bounding Boxes", &telemetry.DrawBoundingBoxes);
                ImGui::Checkbox("Draw Center of Mass", &telemetry.DrawCenterOfMass);
                ImGui::Checkbox("Draw Velocity Vectors", &telemetry.DrawVelocity);
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Batched Lines: %u", telemetry.DebugLineCount);
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "[R] Key: Drop Box & Sphere Again");
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Controls Guide");
        ImGui::Separator();
        ImGui::BulletText("Right-click + Drag: Free-fly look");
        ImGui::BulletText("W / A / S / D: Move camera");
        ImGui::BulletText("E / Space: Ascend");
        ImGui::BulletText("Q / LShift: Descend");
        ImGui::BulletText("R: Reset Box & Sphere");
        ImGui::BulletText("F1: Toggle Physics Wireframe");

        ImGui::End();
    }

}
