#pragma once

#include "AxleVehicle/VehicleComponents.hpp"
#include <string>
#include <algorithm>
#include <cmath>

namespace AxleVehicle {

struct VehicleAudioConfig {
    std::string EngineAudioClipPath;
    std::string TireSquealClipPath;
    float MinPitch = 0.6f;
    float MaxPitch = 2.4f;
    float MasterVolume = 0.8f;
    float IdleRPM = 1000.0f;
    float MaxRPM = 7000.0f;
    float TireSlipThreshold = 0.25f;
};

class VehicleAudioAdapter {
public:
    VehicleAudioAdapter() = default;
    explicit VehicleAudioAdapter(const VehicleAudioConfig& config) : m_Config(config) {}

    void Start() { m_IsActive = true; }
    void Stop() { m_IsActive = false; m_EnginePitch = 1.0f; m_EngineVolume = 0.0f; m_TireSquealVolume = 0.0f; }
    void Reset() { Stop(); Start(); }

    bool IsActive() const { return m_IsActive; }

    void Update(float dt, const ChassisRuntimeState& chassis, const WheelRuntimeState wheels[4]) {
        if (!m_IsActive) return;

        // 1. Synthesize Engine Pitch and Volume based on RPM and Throttle
        const float rpmClamped = std::clamp(chassis.CurrentRPM, m_Config.IdleRPM, m_Config.MaxRPM);
        const float rpmFraction = (m_Config.MaxRPM > m_Config.IdleRPM)
            ? (rpmClamped - m_Config.IdleRPM) / (m_Config.MaxRPM - m_Config.IdleRPM)
            : 0.0f;
        m_EnginePitch = m_Config.MinPitch + (m_Config.MaxPitch - m_Config.MinPitch) * rpmFraction;

        const float throttleClamped = std::clamp(std::abs(chassis.Throttle), 0.0f, 1.0f);
        m_EngineVolume = m_Config.MasterVolume * (0.35f + 0.65f * throttleClamped);

        // 2. Synthesize Tire Squeal based on Wheel Slip
        bool isSquealing = false;
        for (int i = 0; i < 4; ++i) {
            if (wheels[i].IsOnGround) {
                if (std::abs(wheels[i].LongitudinalSlip) > m_Config.TireSlipThreshold ||
                    std::abs(wheels[i].LateralSlip) > m_Config.TireSlipThreshold) {
                    isSquealing = true;
                    break;
                }
            }
        }

        if (isSquealing) {
            m_TireSquealVolume = std::min(1.0f, m_TireSquealVolume + dt * 8.0f);
        } else {
            m_TireSquealVolume = std::max(0.0f, m_TireSquealVolume - dt * 6.0f);
        }
    }

    float GetEnginePitch() const { return m_EnginePitch; }
    float GetEngineVolume() const { return m_EngineVolume; }
    float GetTireSquealVolume() const { return m_TireSquealVolume; }

    const VehicleAudioConfig& GetConfig() const { return m_Config; }
    void SetConfig(const VehicleAudioConfig& config) { m_Config = config; }

private:
    VehicleAudioConfig m_Config{};
    bool m_IsActive = false;
    float m_EnginePitch = 1.0f;
    float m_EngineVolume = 0.0f;
    float m_TireSquealVolume = 0.0f;
};

} // namespace AxleVehicle
