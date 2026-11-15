#include "FroxelFogPass.hpp"

namespace Axle::Passes {
    void FroxelFogPass::Declare(RenderGraph& graph, const FroxelFogResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        using Load = RenderGraph::Load;
        graph.AddPass("FogCopy", {{r.SceneColor, Layout::CopySource}, {r.Input, Layout::CopyDest, Access::Write},
            {r.SceneDepth, Layout::CopySource}, {r.Depth, Layout::CopyDest, Access::Write}});
        graph.AddPass("FogCompute", {
            {r.Depth, Layout::ShaderResource}, {r.Storage, Layout::ShaderResourceStorage, Access::Write}});
        graph.AddPass("FogApply", {
            {r.Input, Layout::ShaderResource}, {r.Storage, Layout::ShaderResource},
            {r.SceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
    }
}
