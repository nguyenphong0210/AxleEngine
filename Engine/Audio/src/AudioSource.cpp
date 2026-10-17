#include <Axle/AudioSource.hpp>
#include <Axle/AudioEngine.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <miniaudio.h>
#include <filesystem>

namespace Axle {

    static ma_attenuation_model ToMAAttenuationModel(AttenuationModel model) {
        switch (model) {
            case AttenuationModel::None:        return ma_attenuation_model_none;
            case AttenuationModel::Inverse:     return ma_attenuation_model_inverse;
            case AttenuationModel::Linear:      return ma_attenuation_model_linear;
            case AttenuationModel::Exponential: return ma_attenuation_model_exponential;
            default:                            return ma_attenuation_model_inverse;
        }
    }

    AudioSource::AudioSource() = default;

    AudioSource::AudioSource(const std::shared_ptr<AudioClip>& clip) {
        Load(clip);
    }

    AudioSource::AudioSource(const std::string& filepath, bool streamFromDisk) {
        Load(filepath, streamFromDisk);
    }

    AudioSource::~AudioSource() {
        Unload();
    }

    AudioSource::AudioSource(AudioSource&& other) noexcept {
        *this = std::move(other);
    }

    AudioSource& AudioSource::operator=(AudioSource&& other) noexcept {
        if (this != &other) {
            Unload();

            m_Clip = std::move(other.m_Clip);
            m_Sound = std::move(other.m_Sound);
            m_Volume = other.m_Volume;
            m_Pitch = other.m_Pitch;
            m_Looping = other.m_Looping;
            m_Spatial = other.m_Spatial;
            m_Position = other.m_Position;
            m_Velocity = other.m_Velocity;
            m_Direction = other.m_Direction;
            m_AttenuationModel = other.m_AttenuationModel;
            m_MinDistance = other.m_MinDistance;
            m_MaxDistance = other.m_MaxDistance;
            m_RollOff = other.m_RollOff;
            m_DopplerFactor = other.m_DopplerFactor;
            m_ConeInnerAngle = other.m_ConeInnerAngle;
            m_ConeOuterAngle = other.m_ConeOuterAngle;
            m_ConeOuterGain = other.m_ConeOuterGain;
            m_IsInitialized = other.m_IsInitialized;

            other.m_IsInitialized = false;
        }
        return *this;
    }

    bool AudioSource::Load(const std::shared_ptr<AudioClip>& clip) {
        if (!clip) return false;
        return Load(clip->GetFilePath(), clip->IsStreaming());
    }

    bool AudioSource::Load(const std::string& filepath, bool streamFromDisk) {
        Unload();

        if (filepath.empty()) return false;

        m_Clip = AudioClip::Create(filepath, streamFromDisk);
        if (!m_Clip || !m_Clip->IsLoaded()) {
            AXLE_CORE_WARN("AudioSource: Failed to initialize clip from '{0}'", filepath);
            return false;
        }

        if (AudioEngine::IsHeadless() || !AudioEngine::IsInitialized()) {
            // Headless unit-testing fallback
            m_IsInitialized = true;
            return true;
        }

        ma_engine* engine = AudioEngine::GetNativeEngine();
        if (!engine) return false;

        m_Sound = std::make_unique<ma_sound>();
        std::filesystem::path resolved = FileSystem::ResolveAssetPath(filepath);

        ma_uint32 flags = streamFromDisk ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
        ma_result result = ma_sound_init_from_file(
            engine,
            resolved.string().c_str(),
            flags,
            nullptr,
            nullptr,
            m_Sound.get()
        );

        if (result != MA_SUCCESS) {
            AXLE_CORE_WARN("AudioSource: miniaudio failed to initialize sound '{0}' (error: {1})", filepath, (int)result);
            m_Sound.reset();
            m_IsInitialized = false;
            return false;
        }

        m_IsInitialized = true;

        // Apply cached properties
        SetVolume(m_Volume);
        SetPitch(m_Pitch);
        SetLooping(m_Looping);
        SetSpatial(m_Spatial);
        SetPosition(m_Position);
        SetVelocity(m_Velocity);
        SetDirection(m_Direction);
        SetAttenuationModel(m_AttenuationModel);
        SetMinDistance(m_MinDistance);
        SetMaxDistance(m_MaxDistance);
        SetRollOff(m_RollOff);
        SetDopplerFactor(m_DopplerFactor);
        SetCone(m_ConeInnerAngle, m_ConeOuterAngle, m_ConeOuterGain);

        return true;
    }

    void AudioSource::Unload() {
        if (m_Sound && m_IsInitialized && AudioEngine::IsInitialized() && !AudioEngine::IsHeadless()) {
            ma_sound_uninit(m_Sound.get());
        }
        m_Sound.reset();
        m_Clip = nullptr;
        m_IsInitialized = false;
    }

    void AudioSource::Play() {
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_start(m_Sound.get());
        }
    }

    void AudioSource::Pause() {
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_stop(m_Sound.get());
        }
    }

    void AudioSource::Stop() {
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_stop(m_Sound.get());
            ma_sound_seek_to_pcm_frame(m_Sound.get(), 0);
        }
    }

    void AudioSource::Rewind() {
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_seek_to_pcm_frame(m_Sound.get(), 0);
        }
    }

    bool AudioSource::IsPlaying() const {
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            return ma_sound_is_playing(m_Sound.get()) == MA_TRUE;
        }
        return false;
    }

    bool AudioSource::IsPaused() const {
        return !IsPlaying() && (GetCursorSeconds() > 0.0f);
    }

    bool AudioSource::IsStopped() const {
        return !IsPlaying() && (GetCursorSeconds() == 0.0f);
    }

    SoundPlayState AudioSource::GetPlayState() const {
        if (IsPlaying()) return SoundPlayState::Playing;
        if (GetCursorSeconds() > 0.0f) return SoundPlayState::Paused;
        return SoundPlayState::Stopped;
    }

    void AudioSource::SetLooping(bool loop) {
        m_Looping = loop;
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_looping(m_Sound.get(), loop ? MA_TRUE : MA_FALSE);
        }
    }

    void AudioSource::SetVolume(f32 volume) {
        m_Volume = std::max(0.0f, volume);
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_volume(m_Sound.get(), m_Volume);
        }
    }

    void AudioSource::SetPitch(f32 pitch) {
        m_Pitch = std::clamp(pitch, 0.05f, 5.0f);
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_pitch(m_Sound.get(), m_Pitch);
        }
    }

    void AudioSource::SetSpatial(bool spatial) {
        m_Spatial = spatial;
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_spatialization_enabled(m_Sound.get(), spatial ? MA_TRUE : MA_FALSE);
        }
    }

    void AudioSource::SetPosition(const glm::vec3& position) {
        m_Position = position;
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_position(m_Sound.get(), position.x, position.y, position.z);
        }
    }

    void AudioSource::SetVelocity(const glm::vec3& velocity) {
        m_Velocity = velocity;
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_velocity(m_Sound.get(), velocity.x, velocity.y, velocity.z);
        }
    }

    void AudioSource::SetDirection(const glm::vec3& direction) {
        m_Direction = direction;
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_direction(m_Sound.get(), direction.x, direction.y, direction.z);
        }
    }

    void AudioSource::SetAttenuationModel(AttenuationModel model) {
        m_AttenuationModel = model;
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_attenuation_model(m_Sound.get(), ToMAAttenuationModel(model));
        }
    }

    void AudioSource::SetMinDistance(f32 minDistance) {
        m_MinDistance = std::max(0.001f, minDistance);
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_min_distance(m_Sound.get(), m_MinDistance);
        }
    }

    void AudioSource::SetMaxDistance(f32 maxDistance) {
        m_MaxDistance = std::max(m_MinDistance, maxDistance);
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_max_distance(m_Sound.get(), m_MaxDistance);
        }
    }

    void AudioSource::SetRollOff(f32 rollOff) {
        m_RollOff = std::max(0.0f, rollOff);
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_rolloff(m_Sound.get(), m_RollOff);
        }
    }

    void AudioSource::SetDopplerFactor(f32 factor) {
        m_DopplerFactor = std::max(0.0f, factor);
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_doppler_factor(m_Sound.get(), m_DopplerFactor);
        }
    }

    void AudioSource::SetCone(f32 innerAngleRadians, f32 outerAngleRadians, f32 outerGain) {
        m_ConeInnerAngle = innerAngleRadians;
        m_ConeOuterAngle = outerAngleRadians;
        m_ConeOuterGain = outerGain;
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_set_cone(m_Sound.get(), innerAngleRadians, outerAngleRadians, outerGain);
        }
    }

    f32 AudioSource::GetCursorSeconds() const {
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            float cursor = 0.0f;
            if (ma_sound_get_cursor_in_seconds(m_Sound.get(), &cursor) == MA_SUCCESS) {
                return cursor;
            }
        }
        return 0.0f;
    }

    void AudioSource::SetCursorSeconds(f32 seconds) {
        if (m_Sound && m_IsInitialized && !AudioEngine::IsHeadless()) {
            ma_sound_seek_to_second(m_Sound.get(), std::max(0.0f, seconds));
        }
    }

    std::shared_ptr<AudioSource> AudioSource::Create(const std::shared_ptr<AudioClip>& clip) {
        return std::make_shared<AudioSource>(clip);
    }

    std::shared_ptr<AudioSource> AudioSource::Create(const std::string& filepath, bool streamFromDisk) {
        return std::make_shared<AudioSource>(filepath, streamFromDisk);
    }

}
