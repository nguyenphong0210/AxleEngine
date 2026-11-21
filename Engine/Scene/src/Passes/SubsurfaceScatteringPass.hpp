#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <glm/mat4x4.hpp>
#include <memory>

namespace Axle {
    class RHISceneRenderer;
    class RenderWorld;
    struct SceneRenderSettings;
}

namespace Axle::Passes {
    struct SubsurfaceResources {
        RenderGraph::Resource SceneColor, Input, SceneDepth, Depth, SceneNormal;
        RenderGraph::Resource Diffuse, Control, Crisp, Temp;
    };

    struct SubsurfaceRecordInputs {
        const SceneRenderSettings& Settings;
        const std::shared_ptr<RHI::RHIViewTargets>& Targets;
        const glm::mat4& ProjectionZO;
        nri::Texture* SceneTarget;
        nri::Texture* SceneDepthTarget;
        nri::Descriptor* SceneTargetRTV;
        nri::Pipeline* CompositePipeline;
        uint32_t Width, Height;
    };

    class SubsurfaceScatteringPass final {
    public:
        static bool SceneRequiresSubsurfacePass(const RenderWorld& world,
                                                const SceneRenderSettings& settings,
                                                uint8_t sampleCount);
        static void Declare(RenderGraph& graph, const SubsurfaceResources& resources);
        static bool Record(RHISceneRenderer& renderer,
                           const SubsurfaceRecordInputs& inputs,
                           const PassRecordOps& ops);
    };
}
