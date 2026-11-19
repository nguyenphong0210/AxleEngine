#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <glm/mat4x4.hpp>

namespace Axle {
    class RHISceneRenderer;
    class RenderWorld;
    class PerspectiveCamera;
    struct LightingFrame;
    struct SceneRenderSettings;
}

namespace Axle::RHI {
    class RHIViewTargets;
}

namespace Axle::Passes {
    struct FroxelFogResources {
        RenderGraph::Resource SceneColor, Input, SceneDepth, Depth, FroxelScattering, FroxelIntegrated;
    };

    struct FroxelFogRecordInputs {
        const RenderWorld& World;
        const PerspectiveCamera& Camera;
        const SceneRenderSettings& Settings;
        const LightingFrame& Lighting;
        const glm::mat4& ProjectionZO;
        RHI::RHIViewTargets* Targets;
        nri::Texture* SceneTarget;
        nri::Texture* SceneDepthTarget;
        nri::Descriptor* SceneTargetRTV;
        nri::Format SceneTargetFormat;
        uint32_t Width, Height;
        uint32_t& DispatchCount;
    };

    class FroxelFogPass final {
    public:
        static void Declare(RenderGraph& graph, const FroxelFogResources& resources);
        static bool Record(RHISceneRenderer& renderer, const FroxelFogRecordInputs& inputs,
                           const PassRecordOps& ops);
    };
}
