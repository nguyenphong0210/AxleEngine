#include <Axle/CharacterVFX.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    CharacterVFXConfig::CharacterVFXConfig() {
        // Footstep ground impact dust puffs
        FootstepConfig.Name = "CharacterFootstepDust";
        FootstepConfig.SimulationSpace = ParticleSimulationSpace::World;
        FootstepConfig.EmissionRate = 0.0f; // Bursts per footstep
        FootstepConfig.MaxParticles = 600;
        FootstepConfig.Lifetime = FloatRange(0.35f, 0.65f);
        FootstepConfig.InitialSpeed = FloatRange(0.6f, 1.8f);
        FootstepConfig.StartSize = FloatRange(0.12f, 0.22f);
        FootstepConfig.EndSize = FloatRange(0.45f, 0.75f);
        FootstepConfig.ConstantAcceleration = glm::vec3(0.0f);
        FootstepConfig.GravityModifier = 0.05f;
        FootstepConfig.Drag = 1.2f;
        FootstepConfig.ColorOverLifetime = ColorGradient(
            glm::vec4(0.65f, 0.60f, 0.52f, 0.65f),
            glm::vec4(0.70f, 0.65f, 0.58f, 0.0f)
        );
        FootstepConfig.Shape.Type = ParticleEmitterShapeType::Sphere;
        FootstepConfig.Shape.Radius = 0.12f;
        FootstepConfig.Shape.Direction = glm::vec3(0.0f, 1.0f, 0.0f);

        // Landing shockwave radial dust ripple
        LandingShockwaveConfig.Name = "CharacterLandingShockwave";
        LandingShockwaveConfig.SimulationSpace = ParticleSimulationSpace::World;
        LandingShockwaveConfig.EmissionRate = 0.0f; // Bursts on impact
        LandingShockwaveConfig.MaxParticles = 1000;
        LandingShockwaveConfig.Lifetime = FloatRange(0.50f, 0.90f);
        LandingShockwaveConfig.InitialSpeed = FloatRange(2.2f, 4.8f);
        LandingShockwaveConfig.StartSize = FloatRange(0.18f, 0.32f);
        LandingShockwaveConfig.EndSize = FloatRange(0.85f, 1.50f);
        LandingShockwaveConfig.ConstantAcceleration = glm::vec3(0.0f);
        LandingShockwaveConfig.GravityModifier = 0.02f;
        LandingShockwaveConfig.Drag = 1.5f;
        LandingShockwaveConfig.ColorOverLifetime = ColorGradient(
            glm::vec4(0.72f, 0.66f, 0.56f, 0.80f),
            glm::vec4(0.76f, 0.70f, 0.60f, 0.0f)
        );
        LandingShockwaveConfig.Shape.Type = ParticleEmitterShapeType::Hemisphere;
        LandingShockwaveConfig.Shape.Radius = 0.40f;
        LandingShockwaveConfig.Shape.Direction = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    CharacterVFX::CharacterVFX() {
        Init(CharacterVFXConfig());
    }

    CharacterVFX::CharacterVFX(const CharacterVFXConfig& config) {
        Init(config);
    }

    void CharacterVFX::Init(const CharacterVFXConfig& config) {
        m_Config = config;

        m_FootstepSystem = std::make_shared<ParticleSystem>(m_Config.FootstepConfig);
        m_LandingShockwaveSystem = std::make_shared<ParticleSystem>(m_Config.LandingShockwaveConfig);

        Reset();
    }

    void CharacterVFX::Reset() {
        if (m_FootstepSystem) m_FootstepSystem->Reset();
        if (m_LandingShockwaveSystem) m_LandingShockwaveSystem->Reset();

        m_DistanceTravelled = 0.0f;
        m_NextFootIsLeft = true;
        m_FootstepCount = 0;

        m_WasAirborne = false;
        m_PeakDownwardSpeed = 0.0f;
        m_LandingCount = 0;
        m_LastImpactSpeed = 0.0f;
    }

    void CharacterVFX::SetConfig(const CharacterVFXConfig& config) {
        Init(config);
    }

    void CharacterVFX::TriggerFootstepPuff(const glm::vec3& footWorldPos, bool isRunning) {
        if (!m_FootstepSystem) return;

        uint32_t count = isRunning ? m_Config.RunPuffCount : m_Config.WalkPuffCount;
        m_FootstepSystem->EmitBurst(count, footWorldPos, m_LastRot);
        m_FootstepCount++;
    }

    void CharacterVFX::TriggerLandingShockwave(const glm::vec3& groundPos, float impactSpeed) {
        if (!m_LandingShockwaveSystem) return;

        bool isHeavy = (impactSpeed >= m_Config.HeavyLandingThreshold);
        uint32_t count = isHeavy ? m_Config.HeavyLandingCount : m_Config.NormalLandingCount;

        m_LandingShockwaveSystem->EmitBurst(count, groundPos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        m_LandingCount++;
        m_LastImpactSpeed = impactSpeed;
    }

    void CharacterVFX::Update(Timestep ts,
                              const glm::vec3& characterPos,
                              const glm::quat& characterRot,
                              const glm::vec3& linearVelocity,
                              bool isGrounded,
                              float currentSpeed,
                              const glm::vec3& groundNormal) {
        float dt = ts.GetSeconds();
        if (dt <= 0.0f) return;

        m_LastPos = characterPos;
        m_LastRot = characterRot;

        // 1. Airborne & Freefall Tracking vs Landing Detection
        if (!isGrounded) {
            m_WasAirborne = true;
            if (linearVelocity.y < 0.0f) {
                m_PeakDownwardSpeed = std::max(m_PeakDownwardSpeed, -linearVelocity.y);
            }
            m_DistanceTravelled = 0.0f;
        } else {
            // Grounded state: Check if character just landed
            if (m_WasAirborne) {
                if (m_PeakDownwardSpeed >= m_Config.LandingImpactThreshold) {
                    TriggerLandingShockwave(characterPos, m_PeakDownwardSpeed);
                }
                m_WasAirborne = false;
                m_PeakDownwardSpeed = 0.0f;
            }

            // 2. Footstep Cadence Tracking while Grounded
            if (currentSpeed >= m_Config.FootstepMinSpeed) {
                m_DistanceTravelled += currentSpeed * dt;
                if (m_DistanceTravelled >= m_Config.StrideLength) {
                    m_DistanceTravelled -= m_Config.StrideLength;

                    // Compute alternating foot placement
                    glm::vec3 rightDir = characterRot * glm::vec3(1.0f, 0.0f, 0.0f);
                    float footSign = m_NextFootIsLeft ? -1.0f : 1.0f;
                    glm::vec3 footPos = characterPos + (rightDir * (footSign * m_Config.FootSpread));

                    bool isRunning = (currentSpeed > 5.0f);
                    TriggerFootstepPuff(footPos, isRunning);

                    m_NextFootIsLeft = !m_NextFootIsLeft;
                }
            } else {
                m_DistanceTravelled = 0.0f;
            }
        }

        // 3. Step particle simulation dynamics
        if (m_FootstepSystem) {
            m_FootstepSystem->Update(dt, characterPos, characterRot);
        }
        if (m_LandingShockwaveSystem) {
            m_LandingShockwaveSystem->Update(dt, characterPos, characterRot);
        }
    }

    uint32_t CharacterVFX::GetTotalActiveParticles() const {
        uint32_t total = 0;
        if (m_FootstepSystem) total += m_FootstepSystem->GetActiveParticleCount();
        if (m_LandingShockwaveSystem) total += m_LandingShockwaveSystem->GetActiveParticleCount();
        return total;
    }

}
