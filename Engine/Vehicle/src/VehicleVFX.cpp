#include <Axle/VehicleVFX.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    VehicleVFX::VehicleVFX() {
        Init(VehicleVFXConfig());
    }

    VehicleVFX::VehicleVFX(const VehicleVFXConfig& config) {
        Init(config);
    }

    void VehicleVFX::Init(const VehicleVFXConfig& config) {
        m_Config = config;

        // Configure wheel smoke & skid emitters
        for (uint32_t i = 0; i < 4; ++i) {
            ParticleEmitterConfig smokeCfg = m_Config.BurnoutSmokeConfig;
            smokeCfg.Name = "WheelSmoke_" + std::to_string(i);
            smokeCfg.SimulationSpace = ParticleSimulationSpace::World;
            smokeCfg.EmissionRate = 0.0f; // Modulated dynamically
            m_WheelSmokeSystems[i] = std::make_shared<ParticleSystem>(smokeCfg);

            ParticleEmitterConfig roosterCfg = m_Config.RoosterTailConfig;
            roosterCfg.Name = "WheelRooster_" + std::to_string(i);
            roosterCfg.SimulationSpace = ParticleSimulationSpace::World;
            roosterCfg.EmissionRate = 0.0f; // Modulated dynamically
            m_WheelRoosterSystems[i] = std::make_shared<ParticleSystem>(roosterCfg);
        }

        // Configure exhaust flame & spark emitters
        ParticleEmitterConfig flameCfg = m_Config.ExhaustFlameConfig;
        flameCfg.Name = "ExhaustFlame";
        flameCfg.SimulationSpace = ParticleSimulationSpace::World;
        flameCfg.EmissionRate = 0.0f; // Bursts only
        m_ExhaustFlameSystem = std::make_shared<ParticleSystem>(flameCfg);

        ParticleEmitterConfig sparkCfg = m_Config.ExhaustSparkConfig;
        sparkCfg.Name = "ExhaustSparks";
        sparkCfg.SimulationSpace = ParticleSimulationSpace::World;
        sparkCfg.EmissionRate = 0.0f; // Bursts only
        m_ExhaustSparkSystem = std::make_shared<ParticleSystem>(sparkCfg);

        UpdateSurfaceColors();
        Reset();
    }

    void VehicleVFX::Reset() {
        for (uint32_t i = 0; i < 4; ++i) {
            if (m_WheelSmokeSystems[i]) m_WheelSmokeSystems[i]->Reset();
            if (m_WheelRoosterSystems[i]) m_WheelRoosterSystems[i]->Reset();
            m_WheelSkidding[i] = false;
            m_WheelRoosterTailing[i] = false;
        }

        if (m_ExhaustFlameSystem) m_ExhaustFlameSystem->Reset();
        if (m_ExhaustSparkSystem) m_ExhaustSparkSystem->Reset();

        m_PreviousThrottle = 0.0f;
        m_BackfireCooldownTimer = 0.0f;
        m_BackfireActiveTimer = 0.0f;
        m_BackfireCount = 0;
    }

    void VehicleVFX::SetConfig(const VehicleVFXConfig& config) {
        Init(config);
    }

    void VehicleVFX::SetSurfaceType(SurfaceVFXType surface) {
        m_Config.SurfaceType = surface;
        UpdateSurfaceColors();
    }

    void VehicleVFX::UpdateSurfaceColors() {
        glm::vec4 startCol(0.55f, 0.45f, 0.35f, 0.65f); // Default dirt brown
        glm::vec4 endCol(0.55f, 0.45f, 0.35f, 0.0f);

        switch (m_Config.SurfaceType) {
            case SurfaceVFXType::Asphalt:
                startCol = glm::vec4(0.35f, 0.35f, 0.35f, 0.5f);  // Dark asphalt dust
                endCol = glm::vec4(0.4f, 0.4f, 0.4f, 0.0f);
                break;
            case SurfaceVFXType::Dirt:
                startCol = glm::vec4(0.55f, 0.42f, 0.30f, 0.7f);  // Warm dirt brown
                endCol = glm::vec4(0.60f, 0.48f, 0.35f, 0.0f);
                break;
            case SurfaceVFXType::Gravel:
                startCol = glm::vec4(0.62f, 0.60f, 0.58f, 0.65f); // Grey crushed gravel
                endCol = glm::vec4(0.65f, 0.65f, 0.65f, 0.0f);
                break;
            case SurfaceVFXType::Mud:
                startCol = glm::vec4(0.32f, 0.24f, 0.16f, 0.85f); // Deep dark wet mud
                endCol = glm::vec4(0.35f, 0.26f, 0.18f, 0.0f);
                break;
            case SurfaceVFXType::Grass:
                startCol = glm::vec4(0.42f, 0.50f, 0.28f, 0.6f);  // Earthy green turf dust
                endCol = glm::vec4(0.45f, 0.52f, 0.30f, 0.0f);
                break;
            case SurfaceVFXType::Sand:
                startCol = glm::vec4(0.82f, 0.74f, 0.55f, 0.75f); // Pale yellow sand
                endCol = glm::vec4(0.85f, 0.78f, 0.60f, 0.0f);
                break;
        }

        for (uint32_t i = 0; i < 4; ++i) {
            if (m_WheelRoosterSystems[i]) {
                auto& cfg = m_WheelRoosterSystems[i]->GetConfig();
                cfg.ColorOverLifetime = ColorGradient(startCol, endCol);
            }
        }
    }

    void VehicleVFX::Update(Timestep ts,
                            const VehicleTelemetryData& telemetry,
                            float throttle,
                            float brake,
                            bool handbrake,
                            const glm::vec3& worldPos,
                            const glm::vec3& forwardDir,
                            const glm::quat& worldRot) {
        float dt = ts.GetSeconds();
        if (dt <= 0.0f) return;

        m_LastVehiclePos = worldPos;
        m_LastVehicleRot = worldRot;

        // 1. Timers
        if (m_BackfireCooldownTimer > 0.0f) {
            m_BackfireCooldownTimer -= dt;
        }
        if (m_BackfireActiveTimer > 0.0f) {
            m_BackfireActiveTimer -= dt;
        }

        // 2. Wheel Smoke & Rooster Tails per Wheel
        float speed = telemetry.SpeedMps;
        bool isLooseSurface = (m_Config.SurfaceType != SurfaceVFXType::Asphalt);

        for (uint32_t i = 0; i < 4; ++i) {
            const auto& wheel = telemetry.Wheels[i];
            glm::vec3 wheelPos = glm::vec3(wheel.WorldTransform[3]);
            glm::vec3 contactPos = wheelPos - glm::vec3(0.0f, m_Config.WheelRadius, 0.0f);

            // A. Tire Burnout & Skid Smoke
            float absLong = std::abs(wheel.LongitudinalSlip);
            float absLat = std::abs(wheel.LateralSlip);
            bool isSkidding = wheel.HasContact &&
                (absLong > m_Config.BurnoutSlipThreshold || absLat > m_Config.SkidSlipThreshold);

            m_WheelSkidding[i] = isSkidding;

            if (m_WheelSmokeSystems[i]) {
                if (isSkidding) {
                    float slipSeverity = std::max(absLong / m_Config.BurnoutSlipThreshold,
                                                  absLat / m_Config.SkidSlipThreshold);
                    slipSeverity = std::clamp(slipSeverity, 1.0f, 3.5f);

                    float targetRate = 45.0f * slipSeverity;
                    m_WheelSmokeSystems[i]->SetEmitting(true);
                    m_WheelSmokeSystems[i]->GetConfig().EmissionRate = targetRate;

                    // Wheel slip kicks particles along tire contact surface
                    glm::vec3 slipDir = -forwardDir;
                    if (absLat > absLong && glm::length(forwardDir) > 0.01f) {
                        glm::vec3 rightDir = glm::normalize(glm::cross(forwardDir, glm::vec3(0.0f, 1.0f, 0.0f)));
                        slipDir = (wheel.LateralSlip > 0.0f ? -rightDir : rightDir);
                    }
                    m_WheelSmokeSystems[i]->GetConfig().Shape.Direction = glm::normalize(slipDir + glm::vec3(0.0f, 0.4f, 0.0f));
                } else {
                    m_WheelSmokeSystems[i]->SetEmitting(false);
                    m_WheelSmokeSystems[i]->GetConfig().EmissionRate = 0.0f;
                }

                m_WheelSmokeSystems[i]->Update(dt, contactPos, worldRot);
            }

            // B. Off-Road Dirt / Gravel Rooster Tails
            bool isRoostering = wheel.HasContact && isLooseSurface && (speed >= m_Config.MinSpeedForRoosterTail);
            m_WheelRoosterTailing[i] = isRoostering;

            if (m_WheelRoosterSystems[i]) {
                if (isRoostering) {
                    float speedRatio = speed / m_Config.MinSpeedForRoosterTail;
                    float targetRate = 35.0f * speedRatio * m_Config.RoosterTailMultiplier;
                    targetRate = std::clamp(targetRate, 20.0f, 180.0f);

                    m_WheelRoosterSystems[i]->SetEmitting(true);
                    m_WheelRoosterSystems[i]->GetConfig().EmissionRate = targetRate;

                    // Rooster tail kicks upward and backwards behind tire
                    glm::vec3 kickDir = glm::normalize(-forwardDir * 0.7f + glm::vec3(0.0f, 0.7f, 0.0f));
                    m_WheelRoosterSystems[i]->GetConfig().Shape.Direction = kickDir;
                    m_WheelRoosterSystems[i]->GetConfig().InitialSpeed = FloatRange(speed * 0.4f, speed * 0.8f);
                } else {
                    m_WheelRoosterSystems[i]->SetEmitting(false);
                    m_WheelRoosterSystems[i]->GetConfig().EmissionRate = 0.0f;
                }

                m_WheelRoosterSystems[i]->Update(dt, contactPos, worldRot);
            }
        }

        // 3. Exhaust Backfire Flames & Popping Sparks (Throttle Lift-off Detection)
        bool highRPM = (telemetry.EngineRPM >= m_Config.BackfireMinRPM);
        bool hadThrottle = (m_PreviousThrottle >= m_Config.BackfireHighThrottleMin);
        bool droppedThrottle = (throttle <= m_Config.BackfireThrottleLiftThreshold);

        if (highRPM && hadThrottle && droppedThrottle && m_BackfireCooldownTimer <= 0.0f) {
            TriggerManualBackfire(m_Config.BackfireFlameCount, m_Config.BackfireSparkCount);
            m_BackfireCooldownTimer = m_Config.BackfireCooldown;
            m_BackfireActiveTimer = 0.15f;
        }

        // Update exhaust particle simulations
        glm::vec3 rearCenter = worldPos - (forwardDir * 2.0f);
        if (m_ExhaustFlameSystem) {
            m_ExhaustFlameSystem->Update(dt, rearCenter, worldRot);
        }
        if (m_ExhaustSparkSystem) {
            m_ExhaustSparkSystem->Update(dt, rearCenter, worldRot);
        }

        m_PreviousThrottle = throttle;
    }

    void VehicleVFX::TriggerManualBackfire(uint32_t flameCount, uint32_t sparkCount) {
        if (m_Config.ExhaustOffsets.empty()) {
            glm::vec3 defaultPos = m_LastVehiclePos;
            if (m_ExhaustFlameSystem) m_ExhaustFlameSystem->EmitBurst(flameCount, defaultPos, m_LastVehicleRot);
            if (m_ExhaustSparkSystem) m_ExhaustSparkSystem->EmitBurst(sparkCount, defaultPos, m_LastVehicleRot);
            m_BackfireCount++;
            return;
        }

        uint32_t numPipes = static_cast<uint32_t>(m_Config.ExhaustOffsets.size());
        uint32_t flamesPerPipe = std::max(1u, flameCount / numPipes);
        uint32_t sparksPerPipe = std::max(1u, sparkCount / numPipes);

        // Flame burst points backwards from exhaust tips
        glm::quat backfireRot = m_LastVehicleRot * glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));

        for (const auto& offset : m_Config.ExhaustOffsets) {
            glm::vec3 tipWorldPos = m_LastVehiclePos + (m_LastVehicleRot * offset);

            if (m_ExhaustFlameSystem) {
                m_ExhaustFlameSystem->EmitBurst(flamesPerPipe, tipWorldPos, backfireRot);
            }
            if (m_ExhaustSparkSystem) {
                m_ExhaustSparkSystem->EmitBurst(sparksPerPipe, tipWorldPos, backfireRot);
            }
        }

        m_BackfireCount++;
        m_BackfireActiveTimer = 0.15f;
    }

    std::shared_ptr<ParticleSystem> VehicleVFX::GetWheelSmokeSystem(uint32_t wheelIndex) {
        if (wheelIndex < 4) return m_WheelSmokeSystems[wheelIndex];
        return nullptr;
    }

    std::shared_ptr<ParticleSystem> VehicleVFX::GetWheelRoosterSystem(uint32_t wheelIndex) {
        if (wheelIndex < 4) return m_WheelRoosterSystems[wheelIndex];
        return nullptr;
    }

    bool VehicleVFX::IsWheelSkidding(uint32_t wheelIndex) const {
        if (wheelIndex < 4) return m_WheelSkidding[wheelIndex];
        return false;
    }

    bool VehicleVFX::IsRoosterTailing(uint32_t wheelIndex) const {
        if (wheelIndex < 4) return m_WheelRoosterTailing[wheelIndex];
        return false;
    }

    uint32_t VehicleVFX::GetTotalActiveParticles() const {
        uint32_t count = 0;
        for (uint32_t i = 0; i < 4; ++i) {
            if (m_WheelSmokeSystems[i]) count += m_WheelSmokeSystems[i]->GetActiveParticleCount();
            if (m_WheelRoosterSystems[i]) count += m_WheelRoosterSystems[i]->GetActiveParticleCount();
        }
        if (m_ExhaustFlameSystem) count += m_ExhaustFlameSystem->GetActiveParticleCount();
        if (m_ExhaustSparkSystem) count += m_ExhaustSparkSystem->GetActiveParticleCount();
        return count;
    }

}
