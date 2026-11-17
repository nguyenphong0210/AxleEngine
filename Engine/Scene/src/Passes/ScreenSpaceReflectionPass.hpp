#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <memory>
#include <vector>
#include <glm/mat4x4.hpp>

namespace Axle {
    class RHISceneRenderer;
    class PerspectiveCamera;
    struct SceneRenderSettings;
}

namespace Axle::Passes {
    struct ScreenSpaceReflectionResources {
        RenderGraph::Resource SceneColor, Input, SceneDepth, Depth, SceneNormal, Storage;
        std::vector<RenderGraph::Resource> HiZ;
    };

    struct ScreenSpaceReflectionRecordInputs {
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

    class ScreenSpaceReflectionPass final {
    public:
        static void Declare(RenderGraph& graph, const ScreenSpaceReflectionResources& resources);
        static bool Record(RHISceneRenderer& renderer,
                           const ScreenSpaceReflectionRecordInputs& inputs,
                           const PassRecordOps& ops);
    };
}
