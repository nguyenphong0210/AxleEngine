#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <glm/mat4x4.hpp>

namespace Axle {
    class RHISceneRenderer;
    class RenderWorld;
    class PerspectiveCamera;
    struct SceneRenderSettings;
}

namespace Axle::Passes {
    struct FroxelFogResources {
        RenderGraph::Resource SceneColor, Input, SceneDepth, Depth, Storage;
    };

    struct FroxelFogRecordInputs {
        const RenderWorld& World;
        const PerspectiveCamera& Camera;
        const SceneRenderSettings& Settings;
        const glm::mat4& ProjectionZO;
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
