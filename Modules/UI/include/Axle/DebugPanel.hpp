#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>

namespace Axle {

    struct TelemetryData {
        f32 DeltaTime = 0.0f;
        f32 FPS = 0.0f;
        glm::vec3 CameraPosition{ 0.0f };
        glm::vec3 CameraRotation{ 0.0f };
    };

    class DebugPanel {
    public:
        DebugPanel() = default;

        void OnImGuiRender(const TelemetryData& telemetry);
    };

}
