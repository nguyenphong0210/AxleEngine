#pragma once
#include <cstdint>
#include <cmath>

namespace AxleVehicle {

inline constexpr std::uint32_t kChassisSchemaVersion = 1;
inline constexpr std::uint32_t kWheelSchemaVersion = 1;
inline constexpr const char* kChassisTypeId = "axle.vehicle/VehicleChassis";
inline constexpr const char* kWheelTypeId = "axle.vehicle/VehicleWheel";

struct ChassisConfig {
    float HalfExtents[3] = { 0.9f, 0.25f, 2.0f }; // Half width, half height, half length
    float Mass = 1500.0f;                          // kg
    float CenterOfMassOffset[3] = { 0.0f, 0.0f, 0.0f };
    float MaxEngineTorque = 500.0f;                // N.m
    float MinRPM = 1000.0f;
    float MaxRPM = 7000.0f;
    float MaxSteerAngle = 35.0f;                   // degrees
    float MaxBrakeTorque = 3000.0f;                // N.m
    float MaxHandbrakeTorque = 6000.0f;            // N.m
    bool FourWheelDrive = true;
    std::uint64_t WheelEntities[4] = { 0, 0, 0, 0 }; // FL, FR, RL, RR
};

struct ChassisRuntimeState {
    float Throttle = 0.0f;     // [-1.0f, 1.0f]
    float Steering = 0.0f;     // [-1.0f, 1.0f]
    float Brake = 0.0f;        // [0.0f, 1.0f]
    bool Handbrake = false;
    float CurrentSpeedKmh = 0.0f;
    float CurrentRPM = 1000.0f;
    int CurrentGear = 1;
    bool IsOnGround = false;
    int StartCount = 0;
    int StopCount = 0;
    int UpdateCount = 0;

    void Reset() {
        Throttle = 0.0f;
        Steering = 0.0f;
        Brake = 0.0f;
        Handbrake = false;
        CurrentSpeedKmh = 0.0f;
        CurrentRPM = 1000.0f;
        CurrentGear = 1;
        IsOnGround = false;
        StartCount = 0;
        StopCount = 0;
        UpdateCount = 0;
    }
};

struct VehicleChassisComponent {
    ChassisConfig Config;
    ChassisRuntimeState Runtime;

    bool Validate() const {
        for (float v : Config.HalfExtents) if (!std::isfinite(v) || v <= 0.0f) return false;
        if (!std::isfinite(Config.Mass) || Config.Mass <= 0.0f) return false;
        for (float v : Config.CenterOfMassOffset) if (!std::isfinite(v)) return false;
        if (!std::isfinite(Config.MaxEngineTorque) || Config.MaxEngineTorque <= 0.0f) return false;
        if (!std::isfinite(Config.MinRPM) || Config.MinRPM <= 0.0f) return false;
        if (!std::isfinite(Config.MaxRPM) || Config.MaxRPM <= Config.MinRPM) return false;
        if (!std::isfinite(Config.MaxSteerAngle) || Config.MaxSteerAngle < 0.0f || Config.MaxSteerAngle > 90.0f) return false;
        if (!std::isfinite(Config.MaxBrakeTorque) || Config.MaxBrakeTorque < 0.0f) return false;
        if (!std::isfinite(Config.MaxHandbrakeTorque) || Config.MaxHandbrakeTorque < 0.0f) return false;
        return true;
    }
};

struct WheelConfig {
    std::uint64_t ChassisEntity = 0;
    std::uint32_t WheelIndex = 0; // 0: FL, 1: FR, 2: RL, 3: RR
    float Offset[3] = { 0.0f, -0.15f, 0.0f };
    float Radius = 0.35f;
    float Width = 0.25f;
    float SuspensionMinLength = 0.15f;
    float SuspensionMaxLength = 0.45f;
    float SpringFrequency = 2.5f;
    float SpringDamping = 0.6f;
    bool CanSteer = false;
    bool IsDriven = true;
    float BrakeRatio = 1.0f;
    float HandbrakeRatio = 0.0f;
};

struct WheelRuntimeState {
    float CurrentSuspensionLength = 0.30f;
    bool IsOnGround = false;
    float SteerAngle = 0.0f;
    float RotationAngle = 0.0f;
    float AngularVelocity = 0.0f;
    float ContactNormal[3] = { 0.0f, 1.0f, 0.0f };
    float LateralSlip = 0.0f;
    float LongitudinalSlip = 0.0f;
    int StartCount = 0;
    int StopCount = 0;
    int UpdateCount = 0;

    void Reset() {
        CurrentSuspensionLength = 0.30f;
        IsOnGround = false;
        SteerAngle = 0.0f;
        RotationAngle = 0.0f;
        AngularVelocity = 0.0f;
        ContactNormal[0] = 0.0f; ContactNormal[1] = 1.0f; ContactNormal[2] = 0.0f;
        LateralSlip = 0.0f;
        LongitudinalSlip = 0.0f;
        StartCount = 0;
        StopCount = 0;
        UpdateCount = 0;
    }
};

struct VehicleWheelComponent {
    WheelConfig Config;
    WheelRuntimeState Runtime;

    bool Validate() const {
        if (Config.WheelIndex > 3) return false;
        for (float v : Config.Offset) if (!std::isfinite(v)) return false;
        if (!std::isfinite(Config.Radius) || Config.Radius <= 0.0f) return false;
        if (!std::isfinite(Config.Width) || Config.Width <= 0.0f) return false;
        if (!std::isfinite(Config.SuspensionMinLength) || Config.SuspensionMinLength < 0.0f) return false;
        if (!std::isfinite(Config.SuspensionMaxLength) || Config.SuspensionMaxLength <= Config.SuspensionMinLength) return false;
        if (!std::isfinite(Config.SpringFrequency) || Config.SpringFrequency <= 0.0f) return false;
        if (!std::isfinite(Config.SpringDamping) || Config.SpringDamping < 0.0f) return false;
        if (!std::isfinite(Config.BrakeRatio) || Config.BrakeRatio < 0.0f || Config.BrakeRatio > 1.0f) return false;
        if (!std::isfinite(Config.HandbrakeRatio) || Config.HandbrakeRatio < 0.0f || Config.HandbrakeRatio > 1.0f) return false;
        return true;
    }
};

} // namespace AxleVehicle
