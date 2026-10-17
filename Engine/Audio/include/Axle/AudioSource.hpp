#pragma once
#include <Axle/Types.hpp>
#include <Axle/AudioClip.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>

struct ma_sound;

namespace Axle {

    enum class AttenuationModel {
        None = 0,
        Inverse,
        Linear,
        Exponential
    };

    enum class SoundPlayState {
        Stopped = 0,
        Playing,
        Paused
    };

    class AudioSource {
    public:
        AudioSource();
        explicit AudioSource(const std::shared_ptr<AudioClip>& clip);
        explicit AudioSource(const std::string& filepath, bool streamFromDisk = false);
        ~AudioSource();

        AudioSource(const AudioSource&) = delete;
        AudioSource& operator=(const AudioSource&) = delete;

        AudioSource(AudioSource&& other) noexcept;
        AudioSource& operator=(AudioSource&& other) noexcept;

        bool Load(const std::shared_ptr<AudioClip>& clip);
        bool Load(const std::string& filepath, bool streamFromDisk = false);
        void Unload();

        void Play();
        void Pause();
        void Stop();
        void Rewind();

        bool IsPlaying() const;
        bool IsPaused() const;
        bool IsStopped() const;
        SoundPlayState GetPlayState() const;

        void SetLooping(bool loop);
        bool IsLooping() const { return m_Looping; }

        void SetVolume(f32 volume);
        f32 GetVolume() const { return m_Volume; }

        void SetPitch(f32 pitch);
        f32 GetPitch() const { return m_Pitch; }

        // 3D Spatial Audio properties
        void SetSpatial(bool spatial);
        bool IsSpatial() const { return m_Spatial; }

        void SetPosition(const glm::vec3& position);
        glm::vec3 GetPosition() const { return m_Position; }

        void SetVelocity(const glm::vec3& velocity);
        glm::vec3 GetVelocity() const { return m_Velocity; }

        void SetDirection(const glm::vec3& direction);
        glm::vec3 GetDirection() const { return m_Direction; }

        void SetAttenuationModel(AttenuationModel model);
        AttenuationModel GetAttenuationModel() const { return m_AttenuationModel; }

        void SetMinDistance(f32 minDistance);
        f32 GetMinDistance() const { return m_MinDistance; }

        void SetMaxDistance(f32 maxDistance);
        f32 GetMaxDistance() const { return m_MaxDistance; }

        void SetRollOff(f32 rollOff);
        f32 GetRollOff() const { return m_RollOff; }

        void SetDopplerFactor(f32 factor);
        f32 GetDopplerFactor() const { return m_DopplerFactor; }

        void SetCone(f32 innerAngleRadians, f32 outerAngleRadians, f32 outerGain);
        f32 GetConeInnerAngle() const { return m_ConeInnerAngle; }
        f32 GetConeOuterAngle() const { return m_ConeOuterAngle; }
        f32 GetConeOuterGain() const { return m_ConeOuterGain; }

        f32 GetCursorSeconds() const;
        void SetCursorSeconds(f32 seconds);

        std::shared_ptr<AudioClip> GetClip() const { return m_Clip; }

        static std::shared_ptr<AudioSource> Create(const std::shared_ptr<AudioClip>& clip);
        static std::shared_ptr<AudioSource> Create(const std::string& filepath, bool streamFromDisk = false);

    private:
        std::shared_ptr<AudioClip> m_Clip;
        std::unique_ptr<ma_sound> m_Sound;

        f32 m_Volume = 1.0f;
        f32 m_Pitch = 1.0f;
        bool m_Looping = false;
        bool m_Spatial = true;

        glm::vec3 m_Position = glm::vec3(0.0f);
        glm::vec3 m_Velocity = glm::vec3(0.0f);
        glm::vec3 m_Direction = glm::vec3(0.0f, 0.0f, -1.0f);

        AttenuationModel m_AttenuationModel = AttenuationModel::Inverse;
        f32 m_MinDistance = 1.0f;
        f32 m_MaxDistance = 50.0f;
        f32 m_RollOff = 1.0f;
        f32 m_DopplerFactor = 1.0f;

        f32 m_ConeInnerAngle = 6.2831853f; // 2 * PI (omnidirectional by default)
        f32 m_ConeOuterAngle = 6.2831853f;
        f32 m_ConeOuterGain = 0.0f;

        bool m_IsInitialized = false;
    };

}
