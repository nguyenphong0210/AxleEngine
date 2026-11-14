#include <Axle/PostProcessService.hpp>
#include <cmath>

namespace Axle {

    PostProcessPolicy PostProcessService::BuildPolicy(const SceneRenderSettings& settings, bool hdrOutput) const {
        PostProcessPolicy policy;
        policy.ToneMapping = settings.EnableToneMapping;
        policy.Bloom = settings.EnableBloom;
        policy.AutoExposure = settings.EnableAutoExposure;
        policy.Output = hdrOutput ? PostProcessOutput::HDR : PostProcessOutput::SDR;
        policy.ToneMappingMode = settings.ToneMappingMode;
        policy.Exposure = settings.Exposure;
        policy.Gamma = settings.Gamma;
        policy.WhitePoint = settings.WhitePoint;
        policy.BloomThreshold = settings.BloomThreshold;
        policy.BloomIntensity = settings.BloomIntensity;
        policy.BloomKnee = settings.BloomKnee;
        policy.BloomRadius = settings.BloomRadius;
        policy.AutoExposureMin = std::min(settings.AutoExposureMin, settings.AutoExposureMax);
        policy.AutoExposureMax = std::max(settings.AutoExposureMin, settings.AutoExposureMax);
        policy.AutoExposureSpeed = std::max(settings.AutoExposureSpeed, 0.0f);
        policy.AutoExposureKeyValue = std::max(settings.AutoExposureKeyValue, 0.0001f);
        if (settings.MSAASamples > 1) policy.AntiAliasing = PostProcessAA::MSAA;
        else if (settings.EnableTAA) policy.AntiAliasing = PostProcessAA::TAA;
        else if (settings.EnableFXAA) policy.AntiAliasing = PostProcessAA::FXAA;
        return policy;
    }

    void PostProcessService::BeginFrame(const SceneRenderSettings& settings, bool hdrOutput) {
        const PostProcessPolicy next = BuildPolicy(settings, hdrOutput);
        if (!m_Configured || next.Output != m_Policy.Output || next.AntiAliasing != m_Policy.AntiAliasing) {
            m_AdaptedExposure = next.Exposure;
        }
        m_Policy = next;
        m_AdaptedExposure = std::clamp(m_AdaptedExposure, m_Policy.AutoExposureMin, m_Policy.AutoExposureMax);
        m_Configured = true;
    }

    void PostProcessService::Reset() {
        m_Policy = {};
        m_AdaptedExposure = 1.0f;
        m_Configured = false;
    }

    float PostProcessService::UpdateExposure(float averageLuminance, float deltaSeconds) {
        if (!m_Configured || !m_Policy.AutoExposure) {
            m_AdaptedExposure = m_Policy.Exposure;
            return m_AdaptedExposure;
        }
        const float luminance = std::max(averageLuminance, 0.0001f);
        const float target = std::clamp(m_Policy.AutoExposureKeyValue / luminance,
                                       m_Policy.AutoExposureMin, m_Policy.AutoExposureMax);
        const float blend = 1.0f - std::exp(-std::max(deltaSeconds, 0.0f) * m_Policy.AutoExposureSpeed);
        m_AdaptedExposure = std::clamp(m_AdaptedExposure + (target - m_AdaptedExposure) * blend,
                                       m_Policy.AutoExposureMin, m_Policy.AutoExposureMax);
        return m_AdaptedExposure;
    }
}
