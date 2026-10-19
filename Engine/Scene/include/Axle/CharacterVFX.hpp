#pragma once

#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/ParticleSystem.hpp>
#include <Axle/ParticleEmitter.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>

namespace Axle {

    struct CharacterVFXConfig {
        // Footstep ground impact dust puffs
        float FootstepMinSpeed = 0.8f;         // Minimum movement speed (m/s) to emit footstep dust
        float StrideLength = 1.4f;            // Distance traversed per footstep (m)
        float FootSpread = 0.22f;             // Lateral offset between left and right feet (m)
        uint32_t WalkPuffCount = 8;           // Dust particles per walking step
        uint32_t RunPuffCount = 18;           // Dust particles per running step

        // Landing shockwave dust ripples
        float LandingImpactThreshold = 3.0f;  // Downward speed (m/s) required to trigger landing VFX
        float HeavyLandingThreshold = 7.5f;   // High-impact downward speed threshold
        uint32_t NormalLandingCount = 32;     // Radial dust particles for normal landing
        uint32_t HeavyLandingCount = 72;      // Radial dust particles for high impact landing
        float ShockwaveRadius = 0.5f;         // Initial ring radius (m)

        // Emitter configurations
        ParticleEmitterConfig FootstepConfig;
        ParticleEmitterConfig LandingShockwaveConfig;

        CharacterVFXConfig();
    };

    class CharacterVFX {
    public:
        CharacterVFX();
        explicit CharacterVFX(const CharacterVFXConfig& config);
        ~CharacterVFX() = default;

        void Init(const CharacterVFXConfig& config);
        void Reset();

        // Main update driven by CharacterControllerComponent telemetry
        void Update(Timestep ts,
                    const glm::vec3& characterPos,
                    const glm::quat& characterRot,
                    const glm::vec3& linearVelocity,
                    bool isGrounded,
                    float currentSpeed,
                    const glm::vec3& groundNormal = glm::vec3(0.0f, 1.0f, 0.0f));

        // Explicit triggers
        void TriggerFootstepPuff(const glm::vec3& footWorldPos, bool isRunning);
        void TriggerLandingShockwave(const glm::vec3& groundPos, float impactSpeed);

        // System Accessors
        std::shared_ptr<ParticleSystem> GetFootstepSystem() const { return m_FootstepSystem; }
        std::shared_ptr<ParticleSystem> GetLandingShockwaveSystem() const { return m_LandingShockwaveSystem; }

        // Configuration
        const CharacterVFXConfig& GetConfig() const { return m_Config; }
        CharacterVFXConfig& GetConfig() { return m_Config; }
        void SetConfig(const CharacterVFXConfig& config);

        // Telemetry & Metrics
        uint32_t GetFootstepCount() const { return m_FootstepCount; }
        uint32_t GetLandingCount() const { return m_LandingCount; }
        float GetLastImpactSpeed() const { return m_LastImpactSpeed; }
        bool WasJustAirborne() const { return m_WasAirborne; }
        uint32_t GetTotalActiveParticles() const;

    private:
        CharacterVFXConfig m_Config;

        std::shared_ptr<ParticleSystem> m_FootstepSystem;
        std::shared_ptr<ParticleSystem> m_LandingShockwaveSystem;

        // Locomotion tracking
        float m_DistanceTravelled = 0.0f;
        bool m_NextFootIsLeft = true;
        uint32_t m_FootstepCount = 0;

        // Airborne & Landing tracking
        bool m_WasAirborne = false;
        float m_PeakDownwardSpeed = 0.0f;
        uint32_t m_LandingCount = 0;
        float m_LastImpactSpeed = 0.0f;

        glm::vec3 m_LastPos{ 0.0f };
        glm::quat m_LastRot{ 1.0f, 0.0f, 0.0f, 0.0f };
    };

}
