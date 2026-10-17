#pragma once
#include <Axle/Types.hpp>
#include <Axle/AudioClip.hpp>
#include <Axle/AudioSource.hpp>
#include <Axle/AudioListener.hpp>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>
#include <mutex>

struct ma_engine;

namespace Axle {

    struct AudioEngineSpecification {
        u32 SampleRate = 48000;
        u32 Channels = 2;
        f32 MasterVolume = 1.0f;
        bool HeadlessSafe = true;
    };

    class AudioEngine {
    public:
        static bool Init(const AudioEngineSpecification& spec = AudioEngineSpecification{});
        static void Shutdown();
        static bool IsInitialized();
        static bool IsHeadless();

        static void SetMasterVolume(f32 volume);
        static f32 GetMasterVolume();

        static void Update();

        // One-shot audio playback helpers
        static void PlayOneShot(const std::string& filepath, f32 volume = 1.0f, f32 pitch = 1.0f);
        static void PlayOneShot3D(const std::string& filepath, const glm::vec3& position, f32 volume = 1.0f, f32 pitch = 1.0f);

        // Native miniaudio engine handle access
        static ma_engine* GetNativeEngine();

    private:
        static bool s_Initialized;
        static bool s_HeadlessMode;
        static f32 s_MasterVolume;
        static std::unique_ptr<ma_engine> s_NativeEngine;
        static std::vector<std::shared_ptr<AudioSource>> s_ActiveOneShots;
        static std::mutex s_AudioMutex;
    };

}
