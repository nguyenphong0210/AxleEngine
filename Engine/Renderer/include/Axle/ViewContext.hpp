#pragma once

#include <Axle/RHI/RHIFrameContext.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/ViewRequest.hpp>
#include <Axle/TemporalService.hpp>
#include <Axle/PostProcessService.hpp>
#include <Axle/RenderDiagnostics.hpp>
#include <cstdint>
#include <memory>

namespace Axle {

    enum class ViewResetReason : uint32_t {
        None = 0,
        Initial = 1u << 0,
        Resize = 1u << 1,
        ViewSwitch = 1u << 2,
        CameraCut = 1u << 3,
        SceneRevision = 1u << 4,
        MaterialGeneration = 1u << 5,
        FeaturePolicy = 1u << 6,
        TargetGeneration = 1u << 7,
        DeviceEpoch = 1u << 8
    };

    inline ViewResetReason operator|(ViewResetReason a, ViewResetReason b) {
        return static_cast<ViewResetReason>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline ViewResetReason operator&(ViewResetReason a, ViewResetReason b) {
        return static_cast<ViewResetReason>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    inline ViewResetReason& operator|=(ViewResetReason& a, ViewResetReason b) {
        a = a | b;
        return a;
    }
    inline bool Any(ViewResetReason value) { return value != ViewResetReason::None; }

    uint64_t HashViewFeaturePolicy(const SceneRenderSettings& settings);

    // Immutable inputs observed at the start of one render view. The context
    // owns the resulting per-view state and never reads ECS state itself.
    struct ViewContextInput {
        RenderViewKind Kind = RenderViewKind::Main;
        uint32_t Width = 1;
        uint32_t Height = 1;
        PerspectiveCamera Camera;
        SceneRenderSettings Settings;
        std::shared_ptr<RHI::RHIViewTargets> Targets;
        glm::vec2 Jitter{ 0.0f };
        uint64_t SceneRevision = 0;
        uint64_t MaterialGeneration = 0;
        uint64_t FeatureGeneration = 0;
        bool CameraCut = false;
    };

    // Owns one view's frame recording, target generation identity, reset state
    // and completion token. Passes may borrow Frame()/Commands() while active;
    // they never submit or recycle the underlying frame themselves.
    class ViewContext {
    public:
        ViewContext() = default;
        ~ViewContext();

        ViewContext(const ViewContext&) = delete;
        ViewContext& operator=(const ViewContext&) = delete;
        ViewContext(ViewContext&&) noexcept = delete;
        ViewContext& operator=(ViewContext&&) noexcept = delete;

        bool Init(uint32_t uniformBytes = 65536, uint32_t uploadBytes = 1048576);
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        // Observes view identity before recording. The returned reasons are
        // latched until the next Observe call and reset temporal/frame index
        // state where the identity changed.
        ViewResetReason Observe(const ViewContextInput& input, uint64_t deviceEpoch);
        // Adopt final targets after frame acquisition has waited for prior GPU
        // work, but before jitter/history selection or graph resource imports.
        bool ObserveTargets(std::shared_ptr<RHI::RHIViewTargets> targets);
        bool ObserveMaterialGeneration(uint64_t generation);
        void RequestReset(ViewResetReason reason);
        ViewResetReason GetResetReasons() const { return m_ResetReasons; }
        ViewResetReason ConsumeResetReasons();

        bool BeginFrame(uint32_t timeoutMilliseconds = 2000);
        bool EndFrameAndSubmit();
        bool AbandonFrame();
        bool IsFrameActive() const { return m_FrameActive; }
        void SetJitter(const glm::vec2& jitter) { m_Input.Jitter = jitter; }

        RHI::RHIFrameContext& Frame() { return m_Frame; }
        const RHI::RHIFrameContext& Frame() const { return m_Frame; }
        RHI::RHICommandBuffer* Commands() { return m_Frame.GetCommands(); }
        RHI::RHICompletionToken GetCompletion() const { return m_Frame.GetCompletion(); }
        uint64_t GetFrameIndex() const { return m_FrameIndex; }

        const ViewContextInput& GetInput() const { return m_Input; }
        std::shared_ptr<RHI::RHIViewTargets> GetTargets() const { return m_Input.Targets; }
        uint64_t GetDeviceEpoch() const { return m_DeviceEpoch; }

        TemporalService& Temporal() { return m_Temporal; }
        const TemporalService& Temporal() const { return m_Temporal; }
        PostProcessService& PostProcess() { return m_PostProcess; }
        const PostProcessService& PostProcess() const { return m_PostProcess; }
        RenderDiagnostics& Diagnostics() { return m_Diagnostics; }
        const RenderDiagnostics& Diagnostics() const { return m_Diagnostics; }

    private:
        bool m_Initialized = false;
        bool m_FrameActive = false;
        uint64_t m_FrameIndex = 0;
        uint64_t m_DeviceEpoch = 0;
        // Snapshot independently: m_Input.Targets may be reconfigured in place.
        // The weak identity survives after submission so a caller can replace
        // its wrapper without this context retaining a retired generation.
        std::weak_ptr<RHI::RHIViewTargets> m_TargetIdentity;
        uint64_t m_TargetGeneration = 0;
        bool m_HasInput = false;
        ViewContextInput m_Input;
        ViewResetReason m_ResetReasons = ViewResetReason::None;
        RHI::RHIFrameContext m_Frame;
        TemporalService m_Temporal;
        PostProcessService m_PostProcess;
        RenderDiagnostics m_Diagnostics;
    };

}
