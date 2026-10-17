#include <Axle/AudioEngine.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <miniaudio.h>
#include <algorithm>

namespace Axle {

    bool AudioEngine::s_Initialized = false;
    bool AudioEngine::s_HeadlessMode = false;
    f32 AudioEngine::s_MasterVolume = 1.0f;
    std::unique_ptr<ma_engine> AudioEngine::s_NativeEngine = nullptr;
    std::vector<std::shared_ptr<AudioSource>> AudioEngine::s_ActiveOneShots;
    std::mutex AudioEngine::s_AudioMutex;

    bool AudioEngine::Init(const AudioEngineSpecification& spec) {
        if (s_Initialized) return true;

        s_MasterVolume = spec.MasterVolume;
        s_NativeEngine = std::make_unique<ma_engine>();

        ma_engine_config config = ma_engine_config_init();
        config.sampleRate = spec.SampleRate;
        config.channels = spec.Channels;

        ma_result result = ma_engine_init(&config, s_NativeEngine.get());
        if (result != MA_SUCCESS) {
            if (spec.HeadlessSafe) {
                s_HeadlessMode = true;
                s_Initialized = true;
                s_NativeEngine.reset();
                AXLE_CORE_WARN("AudioEngine: No physical audio device detected or backend error ({0}). Operating in Headless/Mock Audio Mode.", (int)result);
                return true;
            } else {
                AXLE_CORE_CRITICAL("AudioEngine: Failed to initialize miniaudio engine! (error: {0})", (int)result);
                s_NativeEngine.reset();
                s_Initialized = false;
                return false;
            }
        }

        s_HeadlessMode = false;
        s_Initialized = true;
        SetMasterVolume(s_MasterVolume);

        AXLE_CORE_INFO("AudioEngine: Successfully initialized miniaudio subsystem ({0}Hz, {1}ch, Master Volume: {2:.2f})",
            spec.SampleRate, spec.Channels, s_MasterVolume);

        return true;
    }

    void AudioEngine::Shutdown() {
        if (!s_Initialized) return;

        {
            std::lock_guard<std::mutex> lock(s_AudioMutex);
            for (auto& sound : s_ActiveOneShots) {
                if (sound) sound->Unload();
            }
            s_ActiveOneShots.clear();
        }

        if (s_NativeEngine && !s_HeadlessMode) {
            ma_engine_uninit(s_NativeEngine.get());
        }
        s_NativeEngine.reset();

        s_Initialized = false;
        s_HeadlessMode = false;

        AXLE_CORE_INFO("AudioEngine: Audio subsystem shut down successfully.");
    }

    bool AudioEngine::IsInitialized() {
        return s_Initialized;
    }

    bool AudioEngine::IsHeadless() {
        return s_HeadlessMode;
    }

    void AudioEngine::SetMasterVolume(f32 volume) {
        s_MasterVolume = std::clamp(volume, 0.0f, 1.0f);
        if (s_NativeEngine && !s_HeadlessMode) {
            ma_engine_set_volume(s_NativeEngine.get(), s_MasterVolume);
        }
    }

    f32 AudioEngine::GetMasterVolume() {
        return s_MasterVolume;
    }

    void AudioEngine::Update() {
        if (!s_Initialized) return;

        std::lock_guard<std::mutex> lock(s_AudioMutex);
        s_ActiveOneShots.erase(
            std::remove_if(s_ActiveOneShots.begin(), s_ActiveOneShots.end(),
                [](const std::shared_ptr<AudioSource>& src) {
                    return !src || !src->IsPlaying();
                }),
            s_ActiveOneShots.end()
        );
    }

    void AudioEngine::PlayOneShot(const std::string& filepath, f32 volume, f32 pitch) {
        if (!s_Initialized || filepath.empty()) return;

        auto source = AudioSource::Create(filepath, false);
        if (source) {
            source->SetSpatial(false);
            source->SetVolume(volume);
            source->SetPitch(pitch);
            source->SetLooping(false);
            source->Play();

            std::lock_guard<std::mutex> lock(s_AudioMutex);
            s_ActiveOneShots.push_back(source);
        }
    }

    void AudioEngine::PlayOneShot3D(const std::string& filepath, const glm::vec3& position, f32 volume, f32 pitch) {
        if (!s_Initialized || filepath.empty()) return;

        auto source = AudioSource::Create(filepath, false);
        if (source) {
            source->SetSpatial(true);
            source->SetPosition(position);
            source->SetVolume(volume);
            source->SetPitch(pitch);
            source->SetLooping(false);
            source->Play();

            std::lock_guard<std::mutex> lock(s_AudioMutex);
            s_ActiveOneShots.push_back(source);
        }
    }

    ma_engine* AudioEngine::GetNativeEngine() {
        return s_NativeEngine.get();
    }

}
