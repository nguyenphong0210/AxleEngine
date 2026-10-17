#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/VehicleTelemetry.hpp>
#include <Axle/AudioSource.hpp>
#include <Axle/AudioEngine.hpp>

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Axle {

    struct VehicleAudioConfig {
        // Granular Engine RPM Synthesizer
        std::string EnginePowerClipPath;
        std::string EngineCoastClipPath;
        float MinEnginePitch = 0.6f;
        float MaxEnginePitch = 2.4f;
        float EngineMasterVolume = 0.8f;
        float ThrottleSmoothing = 6.0f;
        float IdleRPM = 1000.0f;
        float MaxRPM = 7000.0f;

        // Tire Squeal & Slip Acoustics
        std::string TireSquealClipPath;
        float SlipThreshold = 0.22f;
        float TireSquealMasterVolume = 0.7f;
        float TireSquealFadeSpeed = 10.0f;

        // Suspension Travel Thuds
        std::string SuspensionThudClipPath;
        float SuspensionThudThreshold = 1.8f; // Compression velocity threshold in m/s
        float SuspensionMasterVolume = 0.6f;

        // Collision Impact System
        std::string CollisionImpactClipPath;
        float CollisionDecelThreshold = 18.0f; // Deceleration threshold in m/s^2
        float CollisionMasterVolume = 0.9f;

        // 3D Spatial Audio
        bool Spatialized = true;
        float MinDistance = 2.0f;
        float MaxDistance = 80.0f;
    };

    class VehicleAudio {
    public:
        VehicleAudio() = default;
        explicit VehicleAudio(const VehicleAudioConfig& config);
        ~VehicleAudio();

        void Init(const VehicleAudioConfig& config);
        void Start();
        void Stop();
        void Update(Timestep ts, const VehicleTelemetryData& telemetry, float throttle, float brake, bool handbrake,
                    const glm::vec3& worldPos, const glm::vec3& forwardDir);

        void TriggerSuspensionThud(const glm::vec3& pos, float volume, float pitch);
        void TriggerCollisionImpact(const glm::vec3& pos, float volume, float pitch);

        const VehicleAudioConfig& GetConfig() const { return m_Config; }
        void SetConfig(const VehicleAudioConfig& config) { m_Config = config; }

        float GetCurrentPitch() const { return m_CurrentEnginePitch; }
        float GetCurrentEngineVolume() const { return m_CurrentEngineVolume; }
        float GetCurrentTireVolume() const { return m_CurrentTireVolume; }
        bool IsPlaying() const { return m_IsPlaying; }

        std::shared_ptr<AudioSource> GetEnginePowerSource() const { return m_EnginePowerSource; }
        std::shared_ptr<AudioSource> GetEngineCoastSource() const { return m_EngineCoastSource; }
        std::shared_ptr<AudioSource> GetTireSquealSource() const { return m_TireSquealSource; }

    private:
        VehicleAudioConfig m_Config;
        bool m_IsPlaying = false;

        std::shared_ptr<AudioSource> m_EnginePowerSource;
        std::shared_ptr<AudioSource> m_EngineCoastSource;
        std::shared_ptr<AudioSource> m_TireSquealSource;

        float m_CurrentEnginePitch = 1.0f;
        float m_CurrentEngineVolume = 0.0f;
        float m_CurrentThrottle = 0.0f;
        float m_CurrentTireVolume = 0.0f;

        float m_PrevSuspensionLengths[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        bool m_HasPrevSuspension = false;
        float m_SuspensionCooldown = 0.0f;

        glm::vec3 m_PrevLinearVelocity{ 0.0f };
        bool m_HasPrevVelocity = false;
        float m_CollisionCooldown = 0.0f;
    };

}
