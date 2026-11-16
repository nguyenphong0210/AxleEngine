#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <glm/mat4x4.hpp>
#include <memory>

namespace Axle {
    class RHISceneRenderer;
    class RenderWorld;
    class PerspectiveCamera;
    struct SceneRenderSettings;
}

namespace Axle::Passes {
    struct GTAOResources {
        RenderGraph::Resource SceneColor, Input, SceneDepth, Depth, SceneNormal, Storage;
    };

    struct GTAORecordInputs {
        const RenderWorld& World;
        const PerspectiveCamera& Camera;
        const SceneRenderSettings& Settings;
        const std::shared_ptr<RHI::RHIViewTargets>& Targets;
        const glm::mat4& ProjectionZO;
        nri::Texture* SceneTarget;
        nri::Texture* SceneDepthTarget;
        nri::Descriptor* SceneTargetRTV;
        nri::Format SceneTargetFormat;
        uint32_t Width, Height;
        uint32_t& DispatchCount;
    };

    class GTAOPass final {
    public:
        static void Declare(RenderGraph& graph, const GTAOResources& resources);
        static bool Record(RHISceneRenderer& renderer, const GTAORecordInputs& inputs,
                           const PassRecordOps& ops);

        // M15-U4 pre-lighting dataflow and indirect target query contracts
        static bool IsPreLightingDataflowSupported();
        static bool IsSeparateIndirectTargetRequired();
        static std::string_view GetIndirectTargetDecisionRationale();
    };
}
