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

        if (!FileSystem::Exists(resolved)) {
            AXLE_CORE_WARN("AudioClip: File not found at '{0}' (resolved: '{1}')", filepath, resolved.string());
            m_IsLoaded = false;
            return;
        }

        // Try direct file decode if physically present on disk
        if (std::filesystem::exists(resolved)) {
            ma_decoder decoder;
            ma_result result = ma_decoder_init_file(resolved.string().c_str(), nullptr, &decoder);
            if (result == MA_SUCCESS) {
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
                return;
            }
        }

        // Fallback: Read binary through FileSystem (e.g. from mounted package) and decode from memory
        auto buffer = FileSystem::ReadBinaryFile(resolved);
        if (!buffer.empty()) {
            ma_decoder decoder;
            ma_result result = ma_decoder_init_memory(buffer.data(), buffer.size(), nullptr, &decoder);
            if (result == MA_SUCCESS) {
                m_Channels = decoder.outputChannels;
                m_SampleRate = decoder.outputSampleRate;

                ma_uint64 totalFrames = 0;
                if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) == MA_SUCCESS && m_SampleRate > 0) {
                    m_LengthSeconds = static_cast<f32>(totalFrames) / static_cast<f32>(m_SampleRate);
                }

                ma_decoder_uninit(&decoder);
                m_IsLoaded = true;

                AXLE_CORE_INFO("AudioClip: Loaded '{0}' from package memory ({1}ch, {2}Hz, {3:.2f}s)",
                    m_Name, m_Channels, m_SampleRate, m_LengthSeconds);
                return;
            }
        }

        AXLE_CORE_WARN("AudioClip: Failed to open or decode audio file '{0}'", filepath);
        m_IsLoaded = false;
    }

    AudioClip::~AudioClip() = default;

    std::shared_ptr<AudioClip> AudioClip::Create(const std::string& filepath, bool streamFromDisk) {
        return std::make_shared<AudioClip>(filepath, streamFromDisk);
    }

}
