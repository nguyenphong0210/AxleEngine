#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <glm/mat4x4.hpp>
#include <memory>

namespace Axle {
    class RHISceneRenderer;
    class PerspectiveCamera;
    struct SceneRenderSettings;
}

namespace Axle::Passes {
    struct PlanarReflectionResources {
        RenderGraph::Resource SceneColor;
        RenderGraph::Resource Input;
        RenderGraph::Resource SceneDepth;
        RenderGraph::Resource Depth;
        RenderGraph::Resource SceneNormal;
        RenderGraph::Resource Environment;
        RenderGraph::Resource Response;
        RenderGraph::Resource PlanarColor;
        bool NeedsColorCopy = true;
        bool NeedsDepthCopy = true;
    };

    struct PlanarReflectionRecordInputs {
        const PerspectiveCamera& Camera;
        const SceneRenderSettings& Settings;
        const RenderPlanarReflector& Reflector;
        const glm::mat4& ReflectedViewProj;
        const std::shared_ptr<RHI::RHIViewTargets>& Targets;
        nri::Texture* SceneTarget = nullptr;
        nri::Texture* SceneDepthTarget = nullptr;
        nri::Descriptor* SceneTargetRTV = nullptr;
        nri::Format SceneTargetFormat = nri::Format::RGBA16_SFLOAT;
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool NeedsColorCopy = true;
        bool NeedsDepthCopy = true;
    };

    class PlanarReflectionPass final {
    public:
        static void Declare(RenderGraph& graph, const PlanarReflectionResources& resources);
        static bool Record(RHISceneRenderer& renderer,
                           const PlanarReflectionRecordInputs& inputs,
                           const PassRecordOps& ops);
    };
}
