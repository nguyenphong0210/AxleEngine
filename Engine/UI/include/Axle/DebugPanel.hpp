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
    };

    class DebugPanel {
    public:
        DebugPanel() = default;

        void OnImGuiRender(const TelemetryData& telemetry);
    };

}
