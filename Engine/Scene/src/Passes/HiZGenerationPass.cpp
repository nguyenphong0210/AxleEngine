#include "HiZGenerationPass.hpp"

namespace Axle::Passes {
    void HiZGenerationPass::Declare(RenderGraph& graph, const HiZResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        for (uint32_t level = 0; level < r.Levels.size(); ++level) {
            graph.AddPass("HiZ" + std::to_string(level), {
                {level == 0 ? r.Depth : r.Levels[level - 1], Layout::ShaderResource},
                {r.Levels[level], Layout::ShaderResourceStorage, Access::Write}});
        }
    }
}
