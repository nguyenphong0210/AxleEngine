#include "GTAOPass.hpp"

namespace Axle::Passes {
    void GTAOPass::Declare(RenderGraph& graph, const GTAOResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        using Load = RenderGraph::Load;
        graph.AddPass("GTAOCopy", {{r.SceneColor, Layout::CopySource}, {r.Input, Layout::CopyDest, Access::Write},
            {r.SceneDepth, Layout::CopySource}, {r.Depth, Layout::CopyDest, Access::Write}});
        graph.AddPass("GTAOCompute", {
            {r.Depth, Layout::ShaderResource}, {r.SceneNormal, Layout::ShaderResource},
            {r.Storage, Layout::ShaderResourceStorage, Access::Write}});
        graph.AddPass("GTAOApply", {
            {r.Depth, Layout::ShaderResource}, {r.Storage, Layout::ShaderResource},
            {r.Input, Layout::ShaderResource},
            {r.SceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
    }
}
