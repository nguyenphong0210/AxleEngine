#include "HiZGenerationPass.hpp"
#include "../RHISceneRendererPasses.hpp"

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

    bool HiZGenerationPass::Record(RHISceneRenderer& renderer,
                                   const std::shared_ptr<RHI::RHIViewTargets>& targets,
                                   const PassRecordOps& ops, uint32_t& dispatchCount) {
        auto& commands = renderer.Commands();
        auto& pool = renderer.m_HiZDescriptorPool;
        const uint32_t levelCount = targets ? targets->GetHiZLevelCount() : 0;
        if (levelCount == 0 || levelCount > renderer.m_HiZTextureSets.size()) return false;
        for (uint32_t level = 0; level < levelCount; ++level) {
            if (!ops.BeginPass("HiZ" + std::to_string(level))) return false;
            auto* source = level == 0 ? renderer.m_OpaqueSceneDepth : targets->GetHiZ(level - 1);
            auto* destination = targets->GetHiZ(level);
            if (!source || !destination || !source->IsValid() || !destination->IsValid()) return false;
            RHIHiZConstants constants{};
            constants.SourceSize = glm::vec4(static_cast<float>(source->GetWidth()),
                static_cast<float>(source->GetHeight()),
                1.0f / static_cast<float>(source->GetWidth()),
                1.0f / static_cast<float>(source->GetHeight()));
            constants.DestinationSize = glm::vec4(static_cast<float>(destination->GetWidth()),
                static_cast<float>(destination->GetHeight()),
                1.0f / static_cast<float>(destination->GetWidth()),
                1.0f / static_cast<float>(destination->GetHeight()));
            constants.Params.x = level == 0 ? 1.0f : 0.0f;
            constants.Params.y = static_cast<float>(level);
            if (!ops.Uniform().UpdateData(&constants, sizeof(constants))) return false;
            auto* textureSet = renderer.m_HiZTextureSets[level];
            auto* storageSet = renderer.m_HiZStorageSets[level];
            if (!textureSet || !storageSet ||
                !pool.UpdateTexture(textureSet, 0, 0, source->GetSRV()) ||
                !pool.UpdateSampler(textureSet, 1, 0, renderer.m_HiZSampler.GetNative()) ||
                !pool.UpdateStorageTexture(storageSet, 0, 0, destination->GetUAV())) return false;
            commands.SetPipeline(renderer.m_HiZPipeline);
            commands.SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_HiZPipelineLayout);
            commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
            pool.Bind(commands.GetNative());
            if (!ops.BindDescriptor(0, textureSet, nri::BindPoint::COMPUTE) ||
                !ops.BindDescriptor(1, storageSet, nri::BindPoint::COMPUTE)) return false;
            commands.Dispatch((destination->GetWidth() + 7) / 8,
                (destination->GetHeight() + 7) / 8, 1);
            ++dispatchCount;
        }
        return commands.IsRecording();
    }

    void HiZGenerationPass::DeclareVisualization(RenderGraph& graph, const HiZVisualizationResources& r) {
        graph.AddPass("HiZVisualize", {
            {r.LevelTexture, RHI::RHIResourceLayout::ShaderResource},
            {r.SceneColor, RHI::RHIResourceLayout::ColorAttachment, RenderGraph::Access::Write, RenderGraph::Load::Clear}});
    }

    bool HiZGenerationPass::RecordVisualization(RHISceneRenderer& renderer,
                                                const HiZVisualizationInputs& i,
                                                const PassRecordOps& ops) {
        auto& commands = renderer.Commands();
        auto* hiz = i.Targets ? i.Targets->GetHiZ(i.Level) : nullptr;
        if (!hiz || !hiz->IsValid() || !i.SceneTargetRTV || i.Width == 0 || i.Height == 0 ||
            !renderer.m_HiZVisualizeTextureSet) return false;
        nri::Pipeline* pipeline = i.Pipeline;
        if (!pipeline) return false;
        if (!ops.BeginPass("HiZVisualize")) return false;
        RHIHiZConstants constants{};
        constants.SourceSize = glm::vec4(static_cast<float>(hiz->GetWidth()), static_cast<float>(hiz->GetHeight()),
            1.0f / static_cast<float>(hiz->GetWidth()), 1.0f / static_cast<float>(hiz->GetHeight()));
        constants.DestinationSize = glm::vec4(static_cast<float>(i.Width), static_cast<float>(i.Height),
            1.0f / static_cast<float>(i.Width), 1.0f / static_cast<float>(i.Height));
        constants.Params = glm::vec4(0.0f, static_cast<float>(i.Level), 1.0f, 0.0f);
        if (!ops.Uniform().UpdateData(&constants, sizeof(constants)) ||
            !renderer.m_HiZDescriptorPool.UpdateTexture(renderer.m_HiZVisualizeTextureSet, 0, 0, hiz->GetSRV())) return false;
        nri::AttachmentDesc attachment{};
        attachment.descriptor = i.SceneTargetRTV;
        attachment.loadOp = nri::LoadOp::CLEAR;
        attachment.storeOp = nri::StoreOp::STORE;
        attachment.clearValue.color.f = {0.0f, 0.0f, 0.0f, 1.0f};
        nri::RenderingDesc rendering{};
        rendering.colors = &attachment;
        rendering.colorNum = 1;
        ops.BeginRendering(rendering);
        commands.SetViewport({0.0f, 0.0f, static_cast<float>(i.Width), static_cast<float>(i.Height), 0.0f, 1.0f});
        commands.SetScissor({0, 0, static_cast<nri::Dim_t>(i.Width), static_cast<nri::Dim_t>(i.Height)});
        commands.SetPipeline(pipeline);
        commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_HiZVisualizePipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
        renderer.m_HiZDescriptorPool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_HiZVisualizeTextureSet, nri::BindPoint::GRAPHICS)) {
            ops.EndRendering();
            return false;
        }
        commands.Draw(3, 1, 0, 0);
        ops.EndRendering();
        return commands.IsRecording();
    }
}
