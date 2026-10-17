#include <Axle/AudioClip.hpp>
#include <Axle/AudioEngine.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <miniaudio.h>
#include <filesystem>

namespace Axle {

    static AudioFormat DetectFormat(const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
        if (ext == ".wav") return AudioFormat::WAV;
        if (ext == ".mp3") return AudioFormat::MP3;
        if (ext == ".flac") return AudioFormat::FLAC;
        return AudioFormat::Unknown;
    }

    AudioClip::AudioClip(const std::string& filepath, bool streamFromDisk)
        : m_FilePath(filepath), m_StreamFromDisk(streamFromDisk) {
        
        std::filesystem::path resolved = FileSystem::ResolveAssetPath(filepath);
        m_Name = resolved.stem().string();
        m_Format = DetectFormat(resolved);

        if (!std::filesystem::exists(resolved)) {
            AXLE_CORE_WARN("AudioClip: File not found at '{0}' (resolved: '{1}')", filepath, resolved.string());
            m_IsLoaded = false;
            return;
        }

        // Query metadata through miniaudio decoder
        ma_decoder decoder;
        ma_result result = ma_decoder_init_file(resolved.string().c_str(), nullptr, &decoder);
        if (result != MA_SUCCESS) {
            AXLE_CORE_WARN("AudioClip: Failed to open or decode audio file '{0}' (error: {1})", filepath, (int)result);
            m_IsLoaded = false;
            return;
        }

        m_Channels = decoder.outputChannels;
        m_SampleRate = decoder.outputSampleRate;

        ma_uint64 totalFrames = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) == MA_SUCCESS && m_SampleRate > 0) {
            m_LengthSeconds = static_cast<f32>(totalFrames) / static_cast<f32>(m_SampleRate);
        }

        ma_decoder_uninit(&decoder);
        m_IsLoaded = true;

        AXLE_CORE_INFO("AudioClip: Loaded '{0}' ({1}ch, {2}Hz, {3:.2f}s, Stream: {4})",
            m_Name, m_Channels, m_SampleRate, m_LengthSeconds, m_StreamFromDisk ? "Yes" : "No");
    }

    AudioClip::~AudioClip() = default;

    std::shared_ptr<AudioClip> AudioClip::Create(const std::string& filepath, bool streamFromDisk) {
        return std::make_shared<AudioClip>(filepath, streamFromDisk);
    }

}
