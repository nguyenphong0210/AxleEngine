#pragma once
#include <Axle/RenderWorld.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
namespace Axle {
    enum class ReflectionState : uint8_t { Disabled, AuthoredReady, DynamicPending, Fallback };
    enum class ReflectionFallback : uint8_t { None, SSR, LocalProbe, SkyIBL, NeutralAmbient };
    struct ReflectionFrame { ReflectionState State=ReflectionState::Disabled; ReflectionFallback Fallback=ReflectionFallback::SkyIBL; uint64_t ProbeGeneration=0, SceneRevision=0, SelectedProbeEntityID=0; uint32_t SelectedProbeCount=0; bool ParallaxCorrected=false, DynamicCaptureSupported=false, PrefilterReady=false; std::string CubemapPath, Diagnostic; };
    struct ReflectionInvalidation { bool TransformChanged=false, SceneRevisionChanged=false, GeometryChanged=false, MaterialChanged=false, LightingChanged=false, AssetChanged=false; bool Any() const { return TransformChanged||SceneRevisionChanged||GeometryChanged||MaterialChanged||LightingChanged||AssetChanged; } };
    class ReflectionService { public: ReflectionFrame Prepare(const RenderWorld&, const glm::vec3&, bool enableSSR, uint64_t sceneRevision) const; ReflectionInvalidation EvaluateInvalidation(const ReflectionInvalidation& changes) const { return changes; } static const char* FallbackName(ReflectionFallback); };
}
