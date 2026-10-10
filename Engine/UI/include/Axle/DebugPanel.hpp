#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>

namespace Axle {

    struct TelemetryData {
        f32 DeltaTime = 0.0f;
        f32 FPS = 0.0f;
        glm::vec3 CameraPosition{ 0.0f };
        glm::vec3 CameraRotation{ 0.0f };

        // Physics telemetry
        bool HasPhysics = false;
        glm::vec3 BoxPosition{ 0.0f };
        glm::vec3 BoxVelocity{ 0.0f };
        bool IsBoxActive = false;

        glm::vec3 SpherePosition{ 0.0f };
        glm::vec3 SphereVelocity{ 0.0f };
        bool IsSphereActive = false;

        u32 TotalBodies = 0;
        u32 ActiveBodies = 0;

        // JPH::DebugRenderer visualization
        bool ShowPhysicsWireframe = false;
        bool DrawBoundingBoxes = false;
        bool DrawVelocity = false;
        bool DrawCenterOfMass = false;
        u32 DebugLineCount = 0;

        // Vehicle dynamics telemetry
        bool HasVehicle = false;
        f32 VehicleSpeedKmh = 0.0f;
        f32 VehicleRPM = 0.0f;
        i32 VehicleGear = 0;
        bool WheelContacts[4]{ false, false, false, false };
        f32 WheelSuspension[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        bool IsChaseCamera = false;
    };

    class DebugPanel {
    public:
        DebugPanel() = default;

        void OnImGuiRender(TelemetryData& telemetry);
    };

}
