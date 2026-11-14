#pragma once
#include <Axle/RenderWorld.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <cstdint>
#include <string>
namespace Axle {
    enum class GIFrameStatus : uint8_t { Disabled, Experimental, Fallback };
    enum class GIFallback : uint8_t { None, IBL, AmbientDiffuse };
    struct GIFrame { GIFrameStatus Status=GIFrameStatus::Disabled; GIFallback Fallback=GIFallback::IBL; bool UpdateScheduled=false, IrradianceAvailable=false, VisibilityAvailable=false, SamplingEnabled=false; uint64_t SceneRevision=0; glm::ivec3 GridDimensions{0}; glm::vec3 GridSpacing{0.0f}; std::string Diagnostic; };
    class GIService { public: GIFrame Prepare(const RenderWorld&, const SceneRenderSettings&, bool computeSupported, uint64_t sceneRevision) const; };
}
