#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <array>
#include <memory>

namespace Axle { class RHISceneRenderer; }

namespace Axle::Passes {
    struct HiZResources {
        RenderGraph::Resource Depth;
        std::array<RenderGraph::Resource, 8> Levels;
    };

    class HiZGenerationPass final {
    public:
        static void Declare(RenderGraph& graph, const HiZResources& resources);
        static bool Record(RHISceneRenderer& renderer,
                           const std::shared_ptr<RHI::RHIViewTargets>& targets,
                           const PassRecordOps& ops, uint32_t& dispatchCount);
    };
}
