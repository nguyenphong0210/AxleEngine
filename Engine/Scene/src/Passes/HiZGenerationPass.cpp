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
        for (uint32_t level = 0; level < 8; ++level) {
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
}
