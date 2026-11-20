#pragma once

#include <Axle/RHI/RHIFrameContext.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/ViewRequest.hpp>
#include <Axle/TemporalService.hpp>
#include <Axle/UUID.hpp>
#include <Axle/PostProcessService.hpp>
#include <Axle/RenderDiagnostics.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

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
        uint64_t SceneInstanceID = 0;
        float DeltaTime = 1.0f / 60.0f;
    };

    struct FroxelHistoryFrame {
        glm::mat4 ViewProjection{1.0f};
        glm::vec4 ViewDepthRow{0.0f};
        uint64_t MediumKey = 0;
        uint32_t Slot = 0;
        bool Valid = false;
    };

    struct DDGIHistoryFrame {
        uint64_t StateHash = 0;
        uint32_t NextProbeCursor = 0;
        uint32_t ProbeCount = 0;
        uint32_t UpdatedProbeCount = 0;
        bool Valid = false;
    };

    struct ExposureHistoryFrame {
        bool Valid = false;
        bool AutoExposureEnabled = false;
    };

    struct TemporalObjectKey {
        uint64_t ID = 0;
        bool IsStableUUID = false;
        bool operator==(const TemporalObjectKey&) const = default;
    };

    struct TemporalObjectKeyHash {
        size_t operator()(const TemporalObjectKey& k) const noexcept {
            const uint64_t mixed = k.ID ^ (k.IsStableUUID ? 0x9e3779b97f4a7c15ull : 0x517cc1b727220a95ull);
            return std::hash<uint64_t>{}(mixed);
        }
    };

    inline TemporalObjectKey MakeTemporalObjectKey(uint32_t entityID, UUID stableUUID) {
        const uint64_t uuidValue = static_cast<uint64_t>(stableUUID);
        if (uuidValue != 0u) return { uuidValue, true };
        return { static_cast<uint64_t>(entityID), false };
    }

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
        RHI::RHIBuffer* PreparePassUniform(const std::string& passName, uint64_t bytes);
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

        FroxelHistoryFrame GetFroxelHistory() const;
        void CommitFroxelHistory(const FroxelHistoryFrame& frame);
        DDGIHistoryFrame GetDDGIHistory() const;
        void CommitDDGIHistory(const DDGIHistoryFrame& frame);
        void InvalidateDDGIHistory();
        ExposureHistoryFrame GetExposureHistory() const;
        void CommitExposureHistory(const ExposureHistoryFrame& frame);
        void InvalidateExposureHistory();

        std::optional<glm::mat4> FindPreviousObjectTransform(uint32_t entityID, UUID stableUUID) const;
        void CommitTemporalObjectTransforms(const std::vector<std::pair<TemporalObjectKey, glm::mat4>>& transforms,
                                            bool valid);
        void InvalidateTemporalObjectTransforms();
        size_t GetCachedTemporalObjectCount() const;

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
        std::unordered_map<std::string, RHI::RHIBuffer> m_PassUniforms;
        TemporalService m_Temporal;
        struct FroxelHistoryEntry {
            FroxelHistoryFrame Frame;
            uint64_t TargetGeneration, SceneRevision, SceneInstanceID, FeatureGeneration, MaterialGeneration;
            uint32_t Width, Height;
            RenderViewKind Kind;
        };
        struct DDGIHistoryEntry {
            DDGIHistoryFrame Frame;
            uint64_t TargetGeneration, SceneRevision, SceneInstanceID, FeatureGeneration, MaterialGeneration;
            RenderViewKind Kind;
        };
        struct ExposureHistoryEntry {
            ExposureHistoryFrame Frame;
            uint64_t TargetGeneration, SceneInstanceID;
            RenderViewKind Kind;
        };
        struct TemporalObjectCacheEntry {
            uint64_t TargetGeneration = 0;
            uint64_t SceneRevision = 0;
            uint64_t SceneInstanceID = 0;
            RenderViewKind Kind = RenderViewKind::Main;
            std::unordered_map<TemporalObjectKey, glm::mat4, TemporalObjectKeyHash> PrevWorldTransforms;
            bool Valid = false;
        };
        // Concurrently live view targets retain their own history without being kept alive here.
        using ViewTargetIdentity = std::weak_ptr<RHI::RHIViewTargets>;
        std::map<ViewTargetIdentity, FroxelHistoryEntry, std::owner_less<ViewTargetIdentity>> m_FroxelHistories;
        std::map<ViewTargetIdentity, DDGIHistoryEntry, std::owner_less<ViewTargetIdentity>> m_DDGIHistories;
        std::map<ViewTargetIdentity, ExposureHistoryEntry, std::owner_less<ViewTargetIdentity>> m_ExposureHistories;
        std::map<ViewTargetIdentity, TemporalObjectCacheEntry, std::owner_less<ViewTargetIdentity>> m_TemporalObjectCaches;
        PostProcessService m_PostProcess;
        RenderDiagnostics m_Diagnostics;
    };

}
