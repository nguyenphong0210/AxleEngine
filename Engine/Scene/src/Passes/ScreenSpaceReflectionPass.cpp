#include "ScreenSpaceReflectionPass.hpp"
#include "HiZGenerationPass.hpp"
#include "../RHISceneRendererPasses.hpp"
#include <Axle/RHI/RHIMaterial.hpp>
#include <algorithm>

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
        for (size_t level = 0; level < std::min<size_t>(r.HiZ.size(), RHI::RHIViewTargets::MinSSRHiZLevels); ++level) {
            computeUses.push_back({r.HiZ[level], Layout::ShaderResource});
        }
        graph.AddPass("SSRCompute", std::move(computeUses));
        graph.AddPass("SSRApply", {
            {r.Environment, Layout::ShaderResource}, {r.Response, Layout::ShaderResource}, {r.Depth, Layout::ShaderResource},
            {r.Storage, Layout::ShaderResource}, {r.Input, Layout::ShaderResource},
            {r.SceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
    }

    bool ScreenSpaceReflectionPass::Record(RHISceneRenderer& renderer,
                                           const ScreenSpaceReflectionRecordInputs& i,
                                           const PassRecordOps& ops) {
        auto& commands = renderer.Commands();
        auto* color = renderer.m_OpaqueSceneColor;
        auto* depth = renderer.m_OpaqueSceneDepth;
        auto* storage = renderer.m_ScreenStorageTarget;
        if (!color || !depth || !storage || !color->IsValid() || !depth->IsValid() ||
            !storage->IsValid() || !i.SceneTarget || !i.SceneDepthTarget || !i.SceneTargetRTV) return false;

        if (!ops.BeginPass("SSRCopy")) return false;
        commands.CopyTexture(*color->GetNative(), nullptr, *i.SceneTarget, nullptr);
        commands.CopyTexture(*depth->GetNative(), nullptr, *i.SceneDepthTarget, nullptr);
        if (!HiZGenerationPass::Record(renderer, i.Targets, ops, i.DispatchCount)) return false;

        if (!ops.BeginPass("SSRCompute")) return false;
        RHISSRConstants constants{};
        constants.View = i.Camera.GetViewMatrix();
        constants.InvView = glm::inverse(constants.View);
        constants.Projection = i.ProjectionZO;
        constants.InvProjection = glm::inverse(i.ProjectionZO);
        constants.ScreenSize = glm::vec4(static_cast<float>(i.Width), static_cast<float>(i.Height),
            1.0f / static_cast<float>(i.Width), 1.0f / static_cast<float>(i.Height));
        constants.SSRParams = glm::vec4(i.Settings.SSRLengthPixels, 32.0f, i.Settings.SSRStrength, 0.5f);
        if (renderer.m_HasActiveReflectionProbe) {
            const auto& probe = renderer.m_ActiveReflectionProbe;
            constants.ProbeBoxMin = glm::vec4(probe.Position - probe.BoxExtents * 0.5f,
                probe.ParallaxCorrected ? 1.0f : 0.0f);
            constants.ProbeBoxMax = glm::vec4(probe.Position + probe.BoxExtents * 0.5f, probe.Intensity);
            constants.ProbePosition = glm::vec4(probe.Position, 1.0f);
        } else {
            constants.ProbeBoxMin = glm::vec4(-10.0f, -10.0f, -10.0f, 1.0f);
            constants.ProbeBoxMax = glm::vec4(10.0f, 10.0f, 10.0f, 1.0f);
            constants.ProbePosition = glm::vec4(0.0f);
        }
        if (!ops.Uniform().UpdateData(&constants, sizeof(constants))) return false;

        auto& pool = renderer.m_SSRDescriptorPool;
        for (uint32_t level = 0; level < 8; ++level) {
            auto* hiz = i.Targets->GetHiZ(level);
            if (!hiz || !hiz->IsValid() ||
                !pool.UpdateTexture(renderer.m_SSRComputeTextureSet, 0, level, hiz->GetSRV())) return false;
        }
        nri::Descriptor* normalSRV = renderer.m_OpaqueSceneNormal && renderer.m_OpaqueSceneNormal->IsValid()
            ? renderer.m_OpaqueSceneNormal->GetSRV() : RHI::RHIMaterial::GetDefaultBlackTexture()->GetSRV();
        if (!pool.UpdateTexture(renderer.m_SSRComputeTextureSet, 1, 0, normalSRV) ||
            !pool.UpdateTexture(renderer.m_SSRComputeTextureSet, 2, 0, color->GetSRV()) ||
            !pool.UpdateSampler(renderer.m_SSRComputeTextureSet, 3, 0, renderer.m_SSRSampler.GetNative()) ||
            !pool.UpdateStorageTexture(renderer.m_SSRComputeStorageSet, 0, 0, storage->GetUAV())) return false;
        commands.SetPipeline(renderer.m_SSRComputePipeline);
        commands.SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_SSRComputePipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
        pool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_SSRComputeTextureSet, nri::BindPoint::COMPUTE) ||
            !ops.BindDescriptor(1, renderer.m_SSRComputeStorageSet, nri::BindPoint::COMPUTE)) return false;
        commands.Dispatch((i.Width + 7) / 8, (i.Height + 7) / 8, 1);
        ++i.DispatchCount;

        auto* environment = i.Targets->Get(RHI::RHIViewTarget::ReflectionEnvironment);
        auto* response = i.Targets->Get(RHI::RHIViewTarget::ReflectionResponse);
        if (!environment || !response) return false;
        if (!ops.BeginPass("SSRApply") || !ops.Uniform().UpdateData(&constants, sizeof(constants)) ||
            !pool.UpdateTexture(renderer.m_SSRApplyDescriptorSet, 0, 0, depth->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_SSRApplyDescriptorSet, 1, 0, storage->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_SSRApplyDescriptorSet, 2, 0, color->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_SSRApplyDescriptorSet, 3, 0, environment->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_SSRApplyDescriptorSet, 4, 0, response->GetSRV()) ||
            !pool.UpdateSampler(renderer.m_SSRApplyDescriptorSet, 5, 0, renderer.m_SSRSampler.GetNative())) return false;
        nri::Pipeline* applyPipeline = i.SceneTargetFormat == nri::Format::RGBA16_SFLOAT && renderer.m_SSRApplyPipelineHDR
            ? renderer.m_SSRApplyPipelineHDR : renderer.m_SSRApplyPipelineSDR;
        if (!applyPipeline) return false;
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
        commands.SetPipeline(applyPipeline);
        commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_SSRApplyPipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
        pool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_SSRApplyDescriptorSet, nri::BindPoint::GRAPHICS)) return false;
        commands.Draw(3, 1, 0, 0);
        ops.EndRendering();
        return commands.IsRecording();
    }
}
