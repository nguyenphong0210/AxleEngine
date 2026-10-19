#pragma once

#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/VehicleTelemetry.hpp>
#include <Axle/ParticleSystem.hpp>
#include <Axle/ParticleEmitter.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>
#include <string>

namespace Axle {

    enum class SurfaceVFXType : uint8_t {
        Asphalt = 0,
        Dirt,
        Gravel,
        Mud,
        Grass,
        Sand
    };

    struct VehicleVFXConfig {
        // Tire Burnout Smoke & Skid Slip Thresholds
        float BurnoutSlipThreshold = 0.30f;    // Longitudinal slip threshold (wheelspin or lockup)
        float SkidSlipThreshold = 0.25f;       // Lateral slip threshold (drifting / slide)
        float MinSpeedForSkidDebris = 2.0f;    // Minimum linear speed (m/s) to kick up skid debris

        // Off-Road Dirt/Gravel Rooster Tails
        float MinSpeedForRoosterTail = 4.0f;   // Minimum speed (m/s, ~14 km/h) to kick up rooster tails
        float RoosterTailMultiplier = 1.0f;    // Emission multiplier
        SurfaceVFXType SurfaceType = SurfaceVFXType::Dirt; // Active surface material type

        // Exhaust Backfire Flames & Popping Sparks
        float BackfireMinRPM = 5200.0f;        // Minimum engine RPM required for lift-off backfire
        float BackfireThrottleLiftThreshold = 0.15f; // Current throttle below this triggers backfire
        float BackfireHighThrottleMin = 0.60f; // Previous throttle must have been above this
        float BackfireCooldown = 0.35f;        // Cooldown between backfire bursts in seconds
        uint32_t BackfireFlameCount = 18;      // Flame particles per backfire burst
        uint32_t BackfireSparkCount = 28;      // Spark particles per backfire burst

        // Exhaust geometry
        std::vector<glm::vec3> ExhaustOffsets = {
            glm::vec3(-0.55f, 0.25f, -2.1f),   // Left exhaust tip (local space)
            glm::vec3(0.55f, 0.25f, -2.1f)     // Right exhaust tip (local space)
        };

        // Wheel geometry
        float WheelRadius = 0.35f;

        // Custom Particle Emitter Configurations
        ParticleEmitterConfig BurnoutSmokeConfig = ParticleEmitterConfig::CreateTireBurnout();
        ParticleEmitterConfig SkidDebrisConfig = ParticleEmitterConfig::CreateDustTrail();
        ParticleEmitterConfig RoosterTailConfig = ParticleEmitterConfig::CreateDustTrail();
        ParticleEmitterConfig ExhaustFlameConfig = ParticleEmitterConfig::CreateFire();
        ParticleEmitterConfig ExhaustSparkConfig = ParticleEmitterConfig::CreateSparks();
    };

    class VehicleVFX {
    public:
        VehicleVFX();
        explicit VehicleVFX(const VehicleVFXConfig& config);
        ~VehicleVFX() = default;

        void Init(const VehicleVFXConfig& config);
        void Reset();

        // Main simulation update driven by vehicle telemetry
        void Update(Timestep ts,
                    const VehicleTelemetryData& telemetry,
                    float throttle,
                    float brake,
                    bool handbrake,
                    const glm::vec3& worldPos,
                    const glm::vec3& forwardDir,
                    const glm::quat& worldRot);

        // Explicit manual backfire trigger
        void TriggerManualBackfire(uint32_t flameCount = 18, uint32_t sparkCount = 28);

        // Surface settings
        void SetSurfaceType(SurfaceVFXType surface);
        SurfaceVFXType GetSurfaceType() const { return m_Config.SurfaceType; }

        // Configuration
        const VehicleVFXConfig& GetConfig() const { return m_Config; }
        VehicleVFXConfig& GetConfig() { return m_Config; }
        void SetConfig(const VehicleVFXConfig& config);

        // Particle System Accessors
        std::shared_ptr<ParticleSystem> GetWheelSmokeSystem(uint32_t wheelIndex);
        std::shared_ptr<ParticleSystem> GetWheelRoosterSystem(uint32_t wheelIndex);
        std::shared_ptr<ParticleSystem> GetExhaustFlameSystem() const { return m_ExhaustFlameSystem; }
        std::shared_ptr<ParticleSystem> GetExhaustSparkSystem() const { return m_ExhaustSparkSystem; }

        // State Queries
        bool IsWheelSkidding(uint32_t wheelIndex) const;
        bool IsRoosterTailing(uint32_t wheelIndex) const;
        bool IsBackfireActive() const { return m_BackfireActiveTimer > 0.0f; }
        uint32_t GetBackfireCount() const { return m_BackfireCount; }
        uint32_t GetTotalActiveParticles() const;

    private:
        void UpdateSurfaceColors();

    private:
        VehicleVFXConfig m_Config;

        // Emitter systems per wheel (4 wheels)
        std::shared_ptr<ParticleSystem> m_WheelSmokeSystems[4];
        std::shared_ptr<ParticleSystem> m_WheelRoosterSystems[4];

        // Exhaust systems
        std::shared_ptr<ParticleSystem> m_ExhaustFlameSystem;
        std::shared_ptr<ParticleSystem> m_ExhaustSparkSystem;

        // Dynamic Telemetry & Tracking
        bool m_WheelSkidding[4] = { false, false, false, false };
        bool m_WheelRoosterTailing[4] = { false, false, false, false };

        float m_PreviousThrottle = 0.0f;
        float m_BackfireCooldownTimer = 0.0f;
        float m_BackfireActiveTimer = 0.0f;
        uint32_t m_BackfireCount = 0;

        glm::vec3 m_LastVehiclePos{ 0.0f };
        glm::quat m_LastVehicleRot{ 1.0f, 0.0f, 0.0f, 0.0f };
    };

}
