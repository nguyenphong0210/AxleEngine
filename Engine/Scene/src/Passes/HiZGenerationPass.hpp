#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <memory>
#include <vector>

namespace Axle { class RHISceneRenderer; }

namespace Axle::Passes {
    struct HiZResources {
        RenderGraph::Resource Depth;
        std::vector<RenderGraph::Resource> Levels;
    };

    struct HiZVisualizationResources {
        RenderGraph::Resource LevelTexture;
        RenderGraph::Resource SceneColor;
    };

    struct HiZVisualizationInputs {
        const std::shared_ptr<RHI::RHIViewTargets>& Targets;
        nri::Descriptor* SceneTargetRTV = nullptr;
        nri::Format SceneTargetFormat = nri::Format::RGBA8_UNORM;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t Level = 0;
    };

    class HiZGenerationPass final {
    public:
        static void Declare(RenderGraph& graph, const HiZResources& resources);
        static bool Record(RHISceneRenderer& renderer,
                           const std::shared_ptr<RHI::RHIViewTargets>& targets,
                           const PassRecordOps& ops, uint32_t& dispatchCount);
        static void DeclareVisualization(RenderGraph& graph, const HiZVisualizationResources& resources);
        static bool RecordVisualization(RHISceneRenderer& renderer,
                                        const HiZVisualizationInputs& inputs,
                                        const PassRecordOps& ops);
    };
}
