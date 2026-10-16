#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Axle {

    enum WheelIndex : u32 {
        Wheel_FrontLeft  = 0,
        Wheel_FrontRight = 1,
        Wheel_RearLeft   = 2,
        Wheel_RearRight  = 3
    };

    struct WheelConfig {
        glm::vec3 Position{ 0.0f, -0.15f, 0.0f }; // Hard point offset relative to chassis
        f32 Radius = 0.35f;
        f32 Width = 0.25f;
        f32 SuspensionMinLength = 0.20f;
        f32 SuspensionMaxLength = 0.50f;
        f32 SpringFrequency = 2.0f;
        f32 SpringDamping = 0.5f;

        bool CanSteer = false;
        f32 MaxSteerAngleDegrees = 32.0f;
        bool IsDriven = true;
        f32 MaxBrakeTorque = 3000.0f;
        f32 MaxHandBrakeTorque = 0.0f;
    };

    struct VehicleConfig {
        glm::vec3 InitialPosition{ 0.0f, 0.95f, 0.0f };
        glm::quat InitialRotation{ 1.0f, 0.0f, 0.0f, 0.0f };

        // Chassis properties
        glm::vec3 ChassisHalfExtent{ 0.9f, 0.25f, 2.0f };
        f32 ChassisMass = 1500.0f;
        glm::vec3 CenterOfMassOffset{ 0.0f, 0.0f, 0.0f };

        // Wheel geometry
        f32 WheelRadius = 0.35f;
        f32 WheelWidth = 0.25f;
        f32 WheelOffsetHorizontal = 1.4f;  // Front/rear distance from chassis center (Z-axis)
        f32 WheelOffsetLateral = 0.95f;     // Left/right distance from chassis center (X-axis)
        f32 WheelOffsetVertical = 0.15f;    // Hard point attachment height below chassis center

        // Suspension properties
        f32 SuspensionMinLength = 0.20f;
        f32 SuspensionMaxLength = 0.50f;
        f32 SpringFrequency = 2.0f;         // Spring oscillation frequency (Hz)
        f32 SpringDamping = 0.5f;           // Suspension damping ratio

        // Steering & powertrain
        f32 MaxSteerAngleDegrees = 32.0f;
        bool FourWheelDrive = true;         // All-wheel drive for immediate reliable traction
        f32 MaxEngineTorque = 800.0f;       // Peak engine torque (N.m)
        f32 MaxRPM = 7000.0f;               // Redline RPM
        f32 MinRPM = 1000.0f;               // Idle RPM
        f32 MaxBrakeTorque = 3000.0f;       // Service brake torque (N.m)
        f32 MaxHandBrakeTorque = 6000.0f;   // Handbrake torque applied to rear wheels (N.m)

        // Dynamic N-Wheeled Configuration (supports 2, 4, 6, 8... N wheels!)
        std::vector<WheelConfig> CustomWheels;
    };

}
