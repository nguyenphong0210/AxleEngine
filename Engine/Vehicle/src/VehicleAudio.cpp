#include <Axle/VehicleAudio.hpp>
#include <Axle/Log.hpp>
#include <algorithm>
#include <cstdlib>

namespace Axle {

    VehicleAudio::VehicleAudio(const VehicleAudioConfig& config) {
        Init(config);
    }

    VehicleAudio::~VehicleAudio() {
        Stop();
    }

    void VehicleAudio::Init(const VehicleAudioConfig& config) {
        m_Config = config;

        if (!AudioEngine::IsInitialized()) {
            return;
        }

        // 1. Initialize Engine Power Source
        if (!m_Config.EnginePowerClipPath.empty()) {
            m_EnginePowerSource = AudioSource::Create(m_Config.EnginePowerClipPath);
            if (m_EnginePowerSource) {
                m_EnginePowerSource->SetLooping(true);
                m_EnginePowerSource->SetSpatial(m_Config.Spatialized);
                m_EnginePowerSource->SetMinDistance(m_Config.MinDistance);
                m_EnginePowerSource->SetMaxDistance(m_Config.MaxDistance);
                m_EnginePowerSource->SetVolume(0.0f);
            }
        }

        // 2. Initialize Engine Coast / Overrun Source
        if (!m_Config.EngineCoastClipPath.empty()) {
            m_EngineCoastSource = AudioSource::Create(m_Config.EngineCoastClipPath);
            if (m_EngineCoastSource) {
                m_EngineCoastSource->SetLooping(true);
                m_EngineCoastSource->SetSpatial(m_Config.Spatialized);
                m_EngineCoastSource->SetMinDistance(m_Config.MinDistance);
                m_EngineCoastSource->SetMaxDistance(m_Config.MaxDistance);
                m_EngineCoastSource->SetVolume(0.0f);
            }
        }

        // 3. Initialize Tire Squeal Source
        if (!m_Config.TireSquealClipPath.empty()) {
            m_TireSquealSource = AudioSource::Create(m_Config.TireSquealClipPath);
            if (m_TireSquealSource) {
                m_TireSquealSource->SetLooping(true);
                m_TireSquealSource->SetSpatial(m_Config.Spatialized);
                m_TireSquealSource->SetMinDistance(m_Config.MinDistance);
                m_TireSquealSource->SetMaxDistance(m_Config.MaxDistance);
                m_TireSquealSource->SetVolume(0.0f);
            }
        }

        m_CurrentThrottle = 0.0f;
        m_CurrentEnginePitch = m_Config.MinEnginePitch;
        m_CurrentEngineVolume = 0.0f;
        m_CurrentTireVolume = 0.0f;
        m_HasPrevSuspension = false;
        m_HasPrevVelocity = false;
        m_SuspensionCooldown = 0.0f;
        m_CollisionCooldown = 0.0f;
    }

    void VehicleAudio::Start() {
        m_IsPlaying = true;
        if (m_EnginePowerSource) {
            m_EnginePowerSource->Play();
        }
        if (m_EngineCoastSource) {
            m_EngineCoastSource->Play();
        }
    }

    void VehicleAudio::Stop() {
        m_IsPlaying = false;
        if (m_EnginePowerSource) {
            m_EnginePowerSource->Stop();
        }
        if (m_EngineCoastSource) {
            m_EngineCoastSource->Stop();
        }
        if (m_TireSquealSource) {
            m_TireSquealSource->Stop();
        }
    }

    void VehicleAudio::Update(Timestep ts, const VehicleTelemetryData& telemetry, float throttle, float brake, bool handbrake,
                             const glm::vec3& worldPos, const glm::vec3& forwardDir) {
        float dt = ts.GetSeconds();
        if (dt <= 0.0f) return;

        // -------------------------------------------------------------
        // 1. Granular Engine RPM Synthesizer & Throttle Crossfade
        // -------------------------------------------------------------
        float idleRPM = std::max(m_Config.IdleRPM, 500.0f);
        float maxRPM = std::max(m_Config.MaxRPM, idleRPM + 1000.0f);
        float clampedRPM = std::clamp(telemetry.EngineRPM, idleRPM, maxRPM);
        float normRPM = (clampedRPM - idleRPM) / (maxRPM - idleRPM);

        m_CurrentEnginePitch = m_Config.MinEnginePitch + normRPM * (m_Config.MaxEnginePitch - m_Config.MinEnginePitch);

        float throttleLerp = std::clamp(dt * m_Config.ThrottleSmoothing, 0.0f, 1.0f);
        float absThrottle = std::clamp(std::abs(throttle), 0.0f, 1.0f);
        m_CurrentThrottle = glm::mix(m_CurrentThrottle, absThrottle, throttleLerp);

        float powerVolume = m_Config.EngineMasterVolume * (0.25f + 0.75f * m_CurrentThrottle);
        float coastVolume = m_Config.EngineMasterVolume * (0.85f - 0.65f * m_CurrentThrottle);
        m_CurrentEngineVolume = powerVolume;

        if (m_EnginePowerSource) {
            m_EnginePowerSource->SetPitch(m_CurrentEnginePitch);
            m_EnginePowerSource->SetVolume(powerVolume);
            m_EnginePowerSource->SetPosition(worldPos);
            m_EnginePowerSource->SetDirection(forwardDir);
            m_EnginePowerSource->SetVelocity(telemetry.LinearVelocity);
            if (m_IsPlaying && !m_EnginePowerSource->IsPlaying()) {
                m_EnginePowerSource->Play();
            }
        }

        if (m_EngineCoastSource) {
            m_EngineCoastSource->SetPitch(m_CurrentEnginePitch);
            m_EngineCoastSource->SetVolume(coastVolume);
            m_EngineCoastSource->SetPosition(worldPos);
            m_EngineCoastSource->SetDirection(forwardDir);
            m_EngineCoastSource->SetVelocity(telemetry.LinearVelocity);
            if (m_IsPlaying && !m_EngineCoastSource->IsPlaying()) {
                m_EngineCoastSource->Play();
            }
        }

        // -------------------------------------------------------------
        // 2. Tire Squeal & Slip Acoustics
        // -------------------------------------------------------------
        float maxSlip = 0.0f;
        for (int i = 0; i < 4; ++i) {
            if (telemetry.Wheels[i].HasContact) {
                float slip = std::max(std::abs(telemetry.Wheels[i].LongitudinalSlip), std::abs(telemetry.Wheels[i].LateralSlip));
                if (slip > maxSlip) maxSlip = slip;
            }
        }

        // Handbrake drift boost to slip
        if (handbrake && telemetry.SpeedKmh > 5.0f) {
            if (maxSlip < 0.5f) maxSlip = 0.5f;
        }

        float targetTireVol = 0.0f;
        if (maxSlip > m_Config.SlipThreshold) {
            float slipRatio = std::clamp((maxSlip - m_Config.SlipThreshold) / (1.0f - m_Config.SlipThreshold), 0.0f, 1.0f);
            targetTireVol = slipRatio * m_Config.TireSquealMasterVolume;
        }

        float tireFadeLerp = std::clamp(dt * m_Config.TireSquealFadeSpeed, 0.0f, 1.0f);
        m_CurrentTireVolume = glm::mix(m_CurrentTireVolume, targetTireVol, tireFadeLerp);

        if (m_TireSquealSource) {
            m_TireSquealSource->SetPosition(worldPos);
            m_TireSquealSource->SetDirection(forwardDir);
            m_TireSquealSource->SetVelocity(telemetry.LinearVelocity);

            float speedNormalized = std::clamp(telemetry.SpeedKmh / 120.0f, 0.0f, 1.0f);
            float tirePitch = 0.95f + 0.25f * speedNormalized;
            m_TireSquealSource->SetPitch(tirePitch);

            if (m_CurrentTireVolume > 0.03f) {
                m_TireSquealSource->SetVolume(m_CurrentTireVolume);
                if (!m_TireSquealSource->IsPlaying()) {
                    m_TireSquealSource->Play();
                }
            } else {
                m_TireSquealSource->SetVolume(0.0f);
                if (m_TireSquealSource->IsPlaying()) {
                    m_TireSquealSource->Pause();
                }
            }
        }

        // -------------------------------------------------------------
        // 3. Suspension Travel Thuds
        // -------------------------------------------------------------
        if (m_SuspensionCooldown > 0.0f) {
            m_SuspensionCooldown -= dt;
        }

        if (m_HasPrevSuspension && dt > 1e-4f) {
            float maxCompVelocity = 0.0f;
            for (int i = 0; i < 4; ++i) {
                if (telemetry.Wheels[i].HasContact) {
                    float compVel = (m_PrevSuspensionLengths[i] - telemetry.Wheels[i].SuspensionLength) / dt;
                    if (compVel > maxCompVelocity) {
                        maxCompVelocity = compVel;
                    }
                }
            }

            if (maxCompVelocity > m_Config.SuspensionThudThreshold && m_SuspensionCooldown <= 0.0f) {
                float intensity = std::clamp(maxCompVelocity / (m_Config.SuspensionThudThreshold * 2.5f), 0.2f, 1.0f);
                float thudVol = intensity * m_Config.SuspensionMasterVolume;
                float thudPitch = 0.85f + 0.3f * ((std::rand() % 100) / 100.0f);

                TriggerSuspensionThud(worldPos, thudVol, thudPitch);
                m_SuspensionCooldown = 0.15f; // 150ms debounce
            }
        }

        for (int i = 0; i < 4; ++i) {
            m_PrevSuspensionLengths[i] = telemetry.Wheels[i].SuspensionLength;
        }
        m_HasPrevSuspension = true;

        // -------------------------------------------------------------
        // 4. Collision Impact System
        // -------------------------------------------------------------
        if (m_CollisionCooldown > 0.0f) {
            m_CollisionCooldown -= dt;
        }

        if (m_HasPrevVelocity && dt > 1e-4f) {
            glm::vec3 accel = (telemetry.LinearVelocity - m_PrevLinearVelocity) / dt;
            float decelMag = glm::length(accel);

            if (decelMag > m_Config.CollisionDecelThreshold && m_CollisionCooldown <= 0.0f) {
                float impactSeverity = std::clamp((decelMag - m_Config.CollisionDecelThreshold) / 30.0f, 0.2f, 1.0f);
                float impactVol = impactSeverity * m_Config.CollisionMasterVolume;
                float impactPitch = 0.8f + 0.4f * ((std::rand() % 100) / 100.0f);

                TriggerCollisionImpact(worldPos, impactVol, impactPitch);
                m_CollisionCooldown = 0.25f; // 250ms debounce
            }
        }

        m_PrevLinearVelocity = telemetry.LinearVelocity;
        m_HasPrevVelocity = true;
    }

    void VehicleAudio::TriggerSuspensionThud(const glm::vec3& pos, float volume, float pitch) {
        if (!m_Config.SuspensionThudClipPath.empty() && AudioEngine::IsInitialized()) {
            AudioEngine::PlayOneShot3D(m_Config.SuspensionThudClipPath, pos, volume, pitch);
        }
    }

    void VehicleAudio::TriggerCollisionImpact(const glm::vec3& pos, float volume, float pitch) {
        if (!m_Config.CollisionImpactClipPath.empty() && AudioEngine::IsInitialized()) {
            AudioEngine::PlayOneShot3D(m_Config.CollisionImpactClipPath, pos, volume, pitch);
        }
    }

}
