#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <memory>

namespace Axle {

    enum class AudioFormat {
        Unknown = 0,
        WAV,
        MP3,
        FLAC
    };

    class AudioClip {
    public:
        AudioClip(const std::string& filepath, bool streamFromDisk = false);
        ~AudioClip();

        const std::string& GetFilePath() const { return m_FilePath; }
        const std::string& GetName() const { return m_Name; }
        bool IsLoaded() const { return m_IsLoaded; }
        bool IsStreaming() const { return m_StreamFromDisk; }
        f32 GetLengthSeconds() const { return m_LengthSeconds; }
        u32 GetChannels() const { return m_Channels; }
        u32 GetSampleRate() const { return m_SampleRate; }
        AudioFormat GetFormat() const { return m_Format; }

        static std::shared_ptr<AudioClip> Create(const std::string& filepath, bool streamFromDisk = false);

    private:
        std::string m_FilePath;
        std::string m_Name;
        bool m_StreamFromDisk = false;
        bool m_IsLoaded = false;
        f32 m_LengthSeconds = 0.0f;
        u32 m_Channels = 0;
        u32 m_SampleRate = 0;
        AudioFormat m_Format = AudioFormat::Unknown;
    };

}
