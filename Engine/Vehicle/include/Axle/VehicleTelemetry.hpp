#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>

namespace Axle {

    struct WheelTelemetryData {
        glm::mat4 WorldTransform{ 1.0f };
        f32 SuspensionLength = 0.0f;
        f32 SteerAngle = 0.0f;
        f32 AngularVelocity = 0.0f;
        f32 LongitudinalSlip = 0.0f;
        f32 LateralSlip = 0.0f;
        bool HasContact = false;
    };

    struct VehicleTelemetryData {
        f32 SpeedKmh = 0.0f;
        f32 SpeedMps = 0.0f;
        f32 EngineRPM = 0.0f;
        i32 CurrentGear = 0;
        glm::vec3 Position{ 0.0f };
        glm::vec3 ForwardVector{ 0.0f, 0.0f, 1.0f };
        glm::vec3 LinearVelocity{ 0.0f };
        WheelTelemetryData Wheels[4];
    };

}
