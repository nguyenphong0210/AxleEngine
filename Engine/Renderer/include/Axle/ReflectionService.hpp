#pragma once
#include <Axle/RenderWorld.hpp>
#include <Axle/RHI/RHIResource.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace Axle {
    static constexpr uint32_t MaxActiveReflectionProbes = 4u;
    enum class ReflectionState : uint8_t { Disabled, AuthoredReady, DynamicPending, Fallback, AuthoredPending, DynamicReady };
    enum class ReflectionFallback : uint8_t { None, SSR, LocalProbe, SkyIBL, NeutralAmbient };
    enum class DynamicCaptureLifecycleState : uint8_t {
        Idle = 0,
        Pending = 1,
        Ready = 2,
        Stale = 3,
        Failed = 4
    };

    struct ReflectionCaptureDomainStamps {
        uint64_t TransformStamp = 0;
        uint64_t SceneRevision = 0;
        uint64_t SceneInstanceID = 0;
        uint64_t SceneLifetimeEpoch = 0;
        uint64_t GeometryStamp = 0;
        uint64_t MaterialStamp = 0;
        uint64_t LightingStamp = 0;
        uint64_t AssetStamp = 0;
        uint64_t SettingsStamp = 0;
        uint64_t DeviceEpoch = 0;
        uint64_t CombinedStamp = 0;

        bool operator==(const ReflectionCaptureDomainStamps&) const = default;
    };

    struct ReflectionInvalidation {
        bool TransformChanged = false;
        bool SceneRevisionChanged = false;
        bool GeometryChanged = false;
        bool MaterialChanged = false;
        bool LightingChanged = false;
        bool AssetChanged = false;
        bool SettingsChanged = false;
        bool DeviceEpochChanged = false;
        bool ManualRequested = false;

        bool Any() const {
            return TransformChanged || SceneRevisionChanged || GeometryChanged ||
                   MaterialChanged || LightingChanged || AssetChanged ||
                   SettingsChanged || DeviceEpochChanged || ManualRequested;
        }
    };

    struct DynamicReflectionProbeStatus {
        std::string ProbeKey;
        uint64_t EntityID = 0;
        UUID StableUUID = 0;
        ReflectionProbeCaptureMode CaptureMode = ReflectionProbeCaptureMode::Automatic;
        DynamicCaptureLifecycleState State = DynamicCaptureLifecycleState::Idle;
        bool HasGeneration = false;
        bool Stale = false;
        bool Pending = false;
        bool Failed = false;
        bool ManualRefreshRequired = false;
        uint64_t ActiveGenerationID = 0;
        uint64_t ActiveInputStamp = 0;
        uint64_t TargetInputStamp = 0;
        uint64_t CachedBytes = 0;
        uint64_t LastUsedSequence = 0;
        uint32_t CompletedFaces = 0;
        uint32_t TotalFaces = 6;
        uint32_t FailureCount = 0;
        ReflectionInvalidation PendingInvalidation{};
        std::string LastDiagnostic;
    };

    struct DynamicReflectionCaptureCacheStats {
        uint64_t BudgetBytes = 64ull * 1024ull * 1024ull;
        uint64_t UsedBytes = 0;
        uint32_t CachedProbeCount = 0;
        uint32_t PendingJobCount = 0;
        uint32_t StaleProbeCount = 0;
        uint32_t FailedProbeCount = 0;
        uint64_t EvictionCount = 0;
        uint64_t LRUClock = 0;
        uint32_t LastScheduledProbes = 0;
        uint32_t LastScheduledFaces = 0;
    };

    struct ReflectionProbeInfluence {
        float Coverage = 0.0f;
        float BlendWeight = 0.0f;
        float InnerRatio = 0.0f;
        float OuterRatio = 0.0f;
        bool InsideCore = false;
    };

    struct ReflectionProbeBasis {
        glm::vec3 Position{ 0.0f };
        glm::vec3 HalfExtents{ 5.0f };
        glm::vec3 AxisX{ 1.0f, 0.0f, 0.0f };
        glm::vec3 AxisY{ 0.0f, 1.0f, 0.0f };
        glm::vec3 AxisZ{ 0.0f, 0.0f, 1.0f };
        float Radius = 10.0f;
        float BlendDistance = 10.0f;
        float Intensity = 1.0f;
        int Shape = 0; // 0: Box, 1: Sphere
        bool ParallaxCorrected = true;
    };

    struct ReflectionProbeCandidate {
        RenderReflectionProbe Probe{};
        float InfluenceCoverage = 0.0f;
        float RawBlendWeight = 0.0f;
        float NormalizedWeight = 0.0f;
        bool DynamicCapture = false;
        bool Ready = false;
        bool Pending = false;
        bool Stale = false;
        bool Failed = false;
        uint32_t CompletedFaces = 0;
        uint32_t TotalFaces = 0;
        uint64_t ActiveGenerationID = 0;
        RHI::RHIResourceHandle PrefilterGeneration{};
        std::string Diagnostic;
    };

    struct ReflectionEnvironmentBlend {
        uint32_t ActiveProbeCount = 0;
        float TotalProbeCoverage = 0.0f;
        float SkyFallbackWeight = 1.0f;
        float SSRWeight = 0.0f;
        float ProbeWeight = 0.0f;
        float SkyWeight = 1.0f;
        glm::vec3 ProbeColor{ 0.0f };
        glm::vec3 BlendedColor{ 0.0f };
    };

    struct ReflectionFrame {
        ReflectionState State=ReflectionState::Disabled;
        ReflectionFallback Fallback=ReflectionFallback::NeutralAmbient;
        RHI::RHIResourceHandle ProbeGeneration;
        uint64_t SceneRevision=0, SelectedProbeEntityID=0;
        uint32_t SelectedProbeCount=0;
        bool ParallaxCorrected=false, DynamicCaptureSupported=false, PrefilterReady=false;
        // Private worlds may have absent/duplicate entity IDs. Consume the exact
        // immutable candidate instead of looking up another probe by ID.
        std::optional<RenderReflectionProbe> SelectedProbe;
        std::string CubemapPath, Diagnostic;
        bool CapturePending=false, CaptureStale=false, CaptureFailed=false;
        uint32_t CaptureCompletedFaces=0, CaptureTotalFaces=0;
        uint64_t ActiveGenerationID=0;
        std::vector<ReflectionProbeCandidate> ActiveProbes;
        uint32_t ActiveProbeCount = 0;
        uint32_t MissingProbeCount = 0;
        uint32_t StaleProbeCount = 0;
        float TotalProbeCoverage = 0.0f;
        float SkyFallbackWeight = 1.0f;
    };
    struct ReflectionProbePublication {
        RHI::RHIResourceHandle PrefilterGeneration;
        std::string Diagnostic;
        bool Ready = false;
        bool CapturePending = false;
        bool CaptureStale = false;
        bool CaptureFailed = false;
        uint32_t CaptureCompletedFaces = 0;
        uint32_t CaptureTotalFaces = 0;
        uint64_t ActiveGenerationID = 0;
    };
    // Native owners publish receipts only after a complete live prefilter exists.
    struct ReflectionPublication {
        RHI::RHIResourceHandle PrefilterGeneration;
        std::string Diagnostic;
        bool CapturePending = false;
        bool CaptureStale = false;
        bool CaptureFailed = false;
        uint32_t CaptureCompletedFaces = 0;
        uint32_t CaptureTotalFaces = 0;
        uint64_t ActiveGenerationID = 0;
        std::vector<ReflectionProbePublication> ProbePublications;
    };
    class ReflectionService {
    public:
        ReflectionFrame Prepare(const RenderWorld&, const glm::vec3&, bool enableSSR, uint64_t sceneRevision) const;
        ReflectionFrame PrepareView(const RenderWorld&, const glm::vec3&, std::span<const glm::vec3> samplePoints, bool enableSSR, uint64_t sceneRevision) const;
        ReflectionFrame ResolvePublication(ReflectionFrame, const ReflectionPublication&, bool ssrAvailable, bool skyIBLAvailable) const;
        ReflectionInvalidation EvaluateInvalidation(const ReflectionInvalidation& changes) const { return changes; }
        ReflectionInvalidation EvaluateInvalidation(
            const ReflectionCaptureDomainStamps& published,
            const ReflectionCaptureDomainStamps& current,
            bool manualRequested = false) const;
        static ReflectionProbeBasis BuildProbeBasis(const RenderReflectionProbe& probe);
        static ReflectionProbeInfluence EvaluateProbeInfluence(const RenderReflectionProbe& probe, const glm::vec3& samplePosition);
        static glm::vec3 ApplyProbeParallax(const RenderReflectionProbe& probe, const glm::vec3& worldPos, const glm::vec3& reflDir);
        static ReflectionEnvironmentBlend EvaluateEnvironmentBlend(
            const std::vector<ReflectionProbeCandidate>& candidates,
            const glm::vec3& worldPos,
            const glm::vec3& reflDir,
            float ssrConfidence,
            float ssrStrength,
            const glm::vec3& ssrHitColor,
            const glm::vec3& skyColor,
            const std::function<glm::vec3(const RenderReflectionProbe&, const glm::vec3&, size_t)>& sampleProbeFn);
        static const char* FallbackName(ReflectionFallback);
    };
}
