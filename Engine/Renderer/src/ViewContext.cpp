#include <Axle/ViewContext.hpp>

namespace Axle {

    uint64_t HashViewFeaturePolicy(const SceneRenderSettings& settings) {
        uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](const auto& value) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            for (size_t i = 0; i < sizeof(value); ++i) {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        };
        mix(settings.EnableShadows);
        mix(settings.LocalShadowResolution);
        mix(settings.MaxShadowedSpotLights);
        mix(settings.MaxShadowedPointLights);
        mix(settings.EnableSkybox);
        mix(settings.EnableStudioLightLobes);
        mix(settings.EnableParticles);
        mix(settings.EnableToneMapping);
        mix(settings.EnableFXAA);
        mix(settings.EnableGTAO);
        mix(settings.EnableContactShadows);
        mix(settings.EnableSSR);
        mix(settings.EnableVolumetricFog);
        mix(settings.EnableDDGI);
        mix(settings.EnableBloom);
        mix(settings.EnableAutoExposure);
        mix(settings.EnableTAA);
        mix(settings.MSAASamples);
        mix(settings.Wireframe);
        mix(settings.ShowPhysicsDebug);
        mix(settings.ToneMappingMode);
        mix(settings.Gamma);
        mix(settings.Exposure);
        mix(settings.TAAJitterSpread);
        mix(settings.TAAFeedbackMin);
        mix(settings.TAAFeedbackMax);
        return hash;
    }

    ViewContext::~ViewContext() { Shutdown(); }

    bool ViewContext::Init(uint32_t uniformBytes, uint32_t uploadBytes) {
        if (m_Initialized) return true;
        if (!m_Frame.Init(uniformBytes, uploadBytes)) return false;
        m_Initialized = true;
        m_FrameActive = false;
        m_FrameIndex = 0;
        m_DeviceEpoch = 0;
        m_TargetGeneration = 0;
        m_HasInput = false;
        m_ResetReasons = ViewResetReason::Initial;
        m_Temporal.Reset();
        m_PostProcess.Reset();
        m_Diagnostics.BeginFrame(0);
        return true;
    }

    void ViewContext::Shutdown() {
        if (m_FrameActive) m_Frame.Abandon();
        m_Frame = RHI::RHIFrameContext{};
        m_FrameActive = false;
        m_Initialized = false;
        m_FrameIndex = 0;
        m_DeviceEpoch = 0;
        m_TargetGeneration = 0;
        m_HasInput = false;
        m_ResetReasons = ViewResetReason::None;
        m_Input = ViewContextInput{};
        m_Temporal.Reset();
        m_PostProcess.Reset();
        m_Diagnostics.BeginFrame(0);
    }

    ViewResetReason ViewContext::Observe(const ViewContextInput& input, uint64_t deviceEpoch) {
        const uint64_t targetGeneration=input.Targets ? input.Targets->GetGeneration() : 0;
        ViewResetReason reasons = m_HasInput ? ViewResetReason::None : ViewResetReason::Initial;
        if (m_HasInput) {
            if (input.Width != m_Input.Width || input.Height != m_Input.Height) reasons |= ViewResetReason::Resize;
            if (input.Kind != m_Input.Kind) reasons |= ViewResetReason::ViewSwitch;
            if (input.SceneRevision != m_Input.SceneRevision) reasons |= ViewResetReason::SceneRevision;
            if (input.MaterialGeneration != m_Input.MaterialGeneration) reasons |= ViewResetReason::MaterialGeneration;
            if (input.FeatureGeneration != m_Input.FeatureGeneration) reasons |= ViewResetReason::FeaturePolicy;
            if (input.Targets != m_Input.Targets || targetGeneration != m_TargetGeneration) reasons |= ViewResetReason::TargetGeneration;
        }
        if (input.CameraCut) reasons |= ViewResetReason::CameraCut;
        if (m_HasInput && deviceEpoch != m_DeviceEpoch) reasons |= ViewResetReason::DeviceEpoch;

        m_Input = input;
        m_TargetGeneration = targetGeneration;
        m_DeviceEpoch = deviceEpoch;
        m_HasInput = true;
        m_ResetReasons = reasons;
        if (Any(reasons & (ViewResetReason::Initial | ViewResetReason::Resize | ViewResetReason::ViewSwitch |
                           ViewResetReason::CameraCut | ViewResetReason::SceneRevision | ViewResetReason::MaterialGeneration |
                           ViewResetReason::FeaturePolicy | ViewResetReason::TargetGeneration | ViewResetReason::DeviceEpoch))) {
            m_FrameIndex = 0;
            m_Temporal.Reset(static_cast<uint32_t>(reasons));
            m_PostProcess.Reset();
        }
        return reasons;
    }

    bool ViewContext::ObserveTargets(std::shared_ptr<RHI::RHIViewTargets> targets) {
        const uint64_t generation=targets ? targets->GetGeneration() : 0;
        const bool changed=targets != m_Input.Targets || generation != m_TargetGeneration;
        m_Input.Targets=std::move(targets);
        m_TargetGeneration=generation;
        if (changed) RequestReset(ViewResetReason::TargetGeneration);
        m_Diagnostics.SetTargetGeneration(generation);
        m_Diagnostics.SetFrameIndex(m_FrameIndex);
        return changed;
    }

    bool ViewContext::ObserveMaterialGeneration(uint64_t generation) {
        m_Diagnostics.SetMaterialGeneration(generation);
        if (m_Input.MaterialGeneration == generation) return false;
        m_Input.MaterialGeneration = generation;
        m_ResetReasons |= ViewResetReason::MaterialGeneration;
        m_FrameIndex = 0;
        m_Temporal.InvalidateContents();
        m_Diagnostics.SetFrameIndex(m_FrameIndex);
        // Keep the frame's selected output/AA policy while restarting adaptation.
        const bool configured = m_PostProcess.IsConfigured();
        const bool hdr = m_PostProcess.GetPolicy().Output == PostProcessOutput::HDR;
        m_PostProcess.Reset();
        if (configured) m_PostProcess.BeginFrame(m_Input.Settings, hdr);
        return true;
    }

    void ViewContext::RequestReset(ViewResetReason reason) {
        m_ResetReasons |= reason;
        if (Any(reason & (ViewResetReason::Initial | ViewResetReason::Resize | ViewResetReason::ViewSwitch |
                          ViewResetReason::CameraCut | ViewResetReason::SceneRevision | ViewResetReason::MaterialGeneration |
                          ViewResetReason::FeaturePolicy | ViewResetReason::TargetGeneration | ViewResetReason::DeviceEpoch))) {
            m_FrameIndex = 0;
            m_Temporal.Reset(static_cast<uint32_t>(reason));
            m_PostProcess.Reset();
        }
    }

    ViewResetReason ViewContext::ConsumeResetReasons() {
        const auto result = m_ResetReasons;
        m_ResetReasons = ViewResetReason::None;
        return result;
    }

    bool ViewContext::BeginFrame(uint32_t timeoutMilliseconds) {
        if (!m_Initialized || m_FrameActive || !m_Frame.Begin(timeoutMilliseconds)) return false;
        m_FrameActive = true;
        const auto targets = m_Input.Targets;
        m_Diagnostics.BeginFrame(m_FrameIndex, targets ? targets->GetGeneration() : 0);
        return true;
    }

    bool ViewContext::EndFrameAndSubmit() {
        if (!m_FrameActive || !m_Frame.End() || !m_Frame.Submit()) {
            m_FrameActive = false;
            return false;
        }
        m_FrameActive = false;
        ++m_FrameIndex;
        return static_cast<bool>(m_Frame.GetCompletion());
    }

    bool ViewContext::AbandonFrame() {
        if (!m_FrameActive) return true;
        const bool result = m_Frame.Abandon();
        m_FrameActive = false;
        return result;
    }

}
