#pragma once
#include <Axle/RenderWorld.hpp>
#include <Axle/RHI/RHIResource.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>
namespace Axle {
    enum class ReflectionState : uint8_t { Disabled, AuthoredReady, DynamicPending, Fallback, AuthoredPending };
    enum class ReflectionFallback : uint8_t { None, SSR, LocalProbe, SkyIBL, NeutralAmbient };
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
    };
    // Native owners publish receipts only after a complete live prefilter exists.
    struct ReflectionPublication {
        RHI::RHIResourceHandle PrefilterGeneration;
        std::string Diagnostic;
    };
    struct ReflectionInvalidation { bool TransformChanged=false, SceneRevisionChanged=false, GeometryChanged=false, MaterialChanged=false, LightingChanged=false, AssetChanged=false; bool Any() const { return TransformChanged||SceneRevisionChanged||GeometryChanged||MaterialChanged||LightingChanged||AssetChanged; } };
    class ReflectionService {
    public:
        ReflectionFrame Prepare(const RenderWorld&, const glm::vec3&, bool enableSSR, uint64_t sceneRevision) const;
        ReflectionFrame ResolvePublication(ReflectionFrame, const ReflectionPublication&, bool ssrAvailable, bool skyIBLAvailable) const;
        ReflectionInvalidation EvaluateInvalidation(const ReflectionInvalidation& changes) const { return changes; }
        static const char* FallbackName(ReflectionFallback);
    };
}
