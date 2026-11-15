#include "ScreenSpaceReflectionPass.hpp"
#include "HiZGenerationPass.hpp"

namespace Axle::Passes {
    void ScreenSpaceReflectionPass::Declare(RenderGraph& graph, const ScreenSpaceReflectionResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        using Load = RenderGraph::Load;
        graph.AddPass("SSRCopy", {{r.SceneColor, Layout::CopySource}, {r.Input, Layout::CopyDest, Access::Write},
            {r.SceneDepth, Layout::CopySource}, {r.Depth, Layout::CopyDest, Access::Write}});
        HiZGenerationPass::Declare(graph, {r.Depth, r.HiZ});
        std::vector<RenderGraph::Use> computeUses{
            {r.Depth, Layout::ShaderResource}, {r.SceneNormal, Layout::ShaderResource},
            {r.Input, Layout::ShaderResource}, {r.Storage, Layout::ShaderResourceStorage, Access::Write}};
        for (const auto& hiz : r.HiZ) computeUses.push_back({hiz, Layout::ShaderResource});
        graph.AddPass("SSRCompute", std::move(computeUses));
        graph.AddPass("SSRApply", {
            {r.SceneNormal, Layout::ShaderResource}, {r.Depth, Layout::ShaderResource},
            {r.Storage, Layout::ShaderResource}, {r.Input, Layout::ShaderResource},
            {r.SceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
    }
}
