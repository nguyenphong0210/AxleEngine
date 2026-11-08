#pragma once

#include "AxleVehicle/VehicleComponents.hpp"
#include <string>
#include <algorithm>
#include <cmath>

namespace AxleVehicle {

struct VehicleVFXConfig {
    float BurnoutSlipThreshold = 0.30f;
    float SkidSlipThreshold = 0.25f;
    float BackfireMinRPM = 5000.0f;
    float BackfireLiftThreshold = 0.15f;
    float BackfireHighThrottleMin = 0.60f;
    float BackfireCooldown = 0.40f;
    std::string TireSmokeEffectPath;
    std::string ExhaustFlameEffectPath;
};

class VehicleVFXAdapter {
public:
    VehicleVFXAdapter() = default;
    explicit VehicleVFXAdapter(const VehicleVFXConfig& config) : m_Config(config) {}

    void Start() { m_IsActive = true; }
    void Stop() {
        m_IsActive = false;
        for (int i = 0; i < 4; ++i) m_WheelSmokeActive[i] = false;
        m_BackfireActive = false;
        m_BackfireCooldownTimer = 0.0f;
    }
    void Reset() { Stop(); Start(); }

    bool IsActive() const { return m_IsActive; }

    void Update(float dt, const ChassisRuntimeState& chassis, const WheelRuntimeState wheels[4]) {
        if (!m_IsActive) return;

        // 1. Tire Smoke / Skid Particles per Wheel
        for (int i = 0; i < 4; ++i) {
            if (wheels[i].IsOnGround) {
                const bool longitudinalSkid = std::abs(wheels[i].LongitudinalSlip) > m_Config.BurnoutSlipThreshold;
                const bool lateralSkid = std::abs(wheels[i].LateralSlip) > m_Config.SkidSlipThreshold;
                m_WheelSmokeActive[i] = (longitudinalSkid || lateralSkid);
            } else {
                m_WheelSmokeActive[i] = false;
            }
        }

        // 2. Exhaust Backfire on High-RPM Throttle Lift-Off
        if (m_BackfireCooldownTimer > 0.0f) {
            m_BackfireCooldownTimer -= dt;
            if (m_BackfireCooldownTimer <= 0.0f) {
                m_BackfireActive = false;
            }
        }

        const float currentThrottle = std::abs(chassis.Throttle);
        if (m_BackfireCooldownTimer <= 0.0f &&
            m_PreviousThrottle >= m_Config.BackfireHighThrottleMin &&
            currentThrottle <= m_Config.BackfireLiftThreshold &&
            chassis.CurrentRPM >= m_Config.BackfireMinRPM) {
            m_BackfireActive = true;
            m_BackfireCooldownTimer = m_Config.BackfireCooldown;
            ++m_BackfireBurstCount;
        }

        m_PreviousThrottle = currentThrottle;
    }

    bool IsWheelSmokeActive(int wheelIndex) const {
        return (wheelIndex >= 0 && wheelIndex < 4) ? m_WheelSmokeActive[wheelIndex] : false;
    }

    bool IsBackfireActive() const { return m_BackfireActive; }
    std::uint32_t GetBackfireBurstCount() const { return m_BackfireBurstCount; }

    const VehicleVFXConfig& GetConfig() const { return m_Config; }
    void SetConfig(const VehicleVFXConfig& config) { m_Config = config; }

private:
    VehicleVFXConfig m_Config{};
    bool m_IsActive = false;
    bool m_WheelSmokeActive[4] = { false, false, false, false };
    bool m_BackfireActive = false;
    float m_PreviousThrottle = 0.0f;
    float m_BackfireCooldownTimer = 0.0f;
    std::uint32_t m_BackfireBurstCount = 0;
};

} // namespace AxleVehicle
