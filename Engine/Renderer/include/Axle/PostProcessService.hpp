#pragma once

#include <Axle/SceneRenderSettings.hpp>
#include <algorithm>
#include <cstdint>

namespace Axle {

    enum class PostProcessOutput : uint8_t { SDR, HDR };
    enum class PostProcessAA : uint8_t { None, FXAA, TAA, MSAA };

    struct PostProcessPolicy {
        bool ToneMapping = false;
        bool Bloom = false;
        bool AutoExposure = false;
        PostProcessAA AntiAliasing = PostProcessAA::None;
        PostProcessOutput Output = PostProcessOutput::SDR;
        int ToneMappingMode = 0;
        float Exposure = 1.0f;
        float Gamma = 2.2f;
        float WhitePoint = 4.0f;
        float BloomThreshold = 1.0f;
        float BloomIntensity = 0.8f;
        float BloomKnee = 0.5f;
        float BloomRadius = 1.5f;
        float AutoExposureMin = 0.1f;
        float AutoExposureMax = 10.0f;
        float AutoExposureSpeed = 3.0f;
        float AutoExposureKeyValue = 0.18f;
    };

    class PostProcessService {
    public:
        PostProcessPolicy BuildPolicy(const SceneRenderSettings& settings, bool hdrOutput) const;
        void BeginFrame(const SceneRenderSettings& settings, bool hdrOutput);
        void Reset();
        float UpdateExposure(float averageLuminance, float deltaSeconds);

        const PostProcessPolicy& GetPolicy() const { return m_Policy; }
        float GetAdaptedExposure() const { return m_AdaptedExposure; }
        bool IsConfigured() const { return m_Configured; }

    private:
        PostProcessPolicy m_Policy{};
        float m_AdaptedExposure = 1.0f;
        bool m_Configured = false;
    };
}
