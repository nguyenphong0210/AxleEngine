#include "PostProcessPass.hpp"

namespace Axle::Passes {
    void PostProcessPass::Declare(RenderGraph& graph, const PostProcessResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        using Load = RenderGraph::Load;
        if (r.UseBloom) {
            graph.AddPass("BloomCopy", {{r.SceneColor, Layout::CopySource}, {r.BloomInput, Layout::CopyDest, Access::Write}});
            for (uint32_t level = 0; level < r.Bloom.size(); ++level) {
                graph.AddPass("BloomDown" + std::to_string(level), {
                    {level == 0 ? r.BloomInput : r.Bloom[level - 1], Layout::ShaderResource},
                    {r.Bloom[level], Layout::ShaderResourceStorage, Access::Write}});
            }
            for (int level = static_cast<int>(r.Bloom.size()) - 2; level >= 0; --level) {
                graph.AddPass("BloomUp" + std::to_string(level), {
                    {r.Bloom[level + 1], Layout::ShaderResource},
                    {r.Bloom[level], Layout::ShaderResourceStorage, Access::Write}});
            }
            graph.AddPass("BloomApply", {
                {r.Bloom[0], Layout::ShaderResource}, {r.BloomInput, Layout::ShaderResource},
                {r.SceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
        }
        if (r.UseAutoExposure) {
            if (!r.UseToneMap) graph.AddPass("AutoExposureCopy", {
                {r.SceneColor, Layout::CopySource}, {r.ExposureInput, Layout::CopyDest, Access::Write}});
            graph.AddPass("AutoExposure", {
                {r.ExposureInput, Layout::ShaderResource},
                {r.ExposureOutput, Layout::ShaderResourceStorage, Access::Write}});
        }
        if (r.UseTAA) {
            const auto source = r.UseToneMap ? r.HDR : r.SceneColor;
            graph.AddPass("TAAResolve", {
                {source, Layout::ShaderResource}, {r.SceneDepth, Layout::ShaderResource},
                {r.TAAHistoryRead, Layout::ShaderResource},
                {r.TAAHistoryWrite, Layout::ColorAttachment, Access::Write, Load::Clear}});
            graph.AddPass("TAACopy", {
                {r.TAAHistoryWrite, Layout::CopySource}, {source, Layout::CopyDest, Access::Write}});
        }
        if (r.UseToneMap) {
            if (r.UseAutoExposure) graph.AddPass("ToneMapping", {
                {r.HDR, Layout::ShaderResource}, {r.ExposureOutput, Layout::ShaderResource},
                {r.FXAA, Layout::ColorAttachment, Access::Write, Load::Clear}});
            else graph.AddPass("ToneMapping", {
                {r.HDR, Layout::ShaderResource},
                {r.FXAA, Layout::ColorAttachment, Access::Write, Load::Clear}});
        }
        if (r.UseFXAA) graph.AddPass("FXAA", {
            {r.FXAA, Layout::ShaderResource}, {r.Output, Layout::ColorAttachment, Access::Write, Load::Clear}});
    }
}
