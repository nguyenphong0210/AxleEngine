#include "FroxelFogPass.hpp"
#include "../RHISceneRendererPasses.hpp"

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

    bool FroxelFogPass::Record(RHISceneRenderer& renderer, const FroxelFogRecordInputs& i,
                               const PassRecordOps& ops) {
        auto& commands = renderer.Commands();
        auto* color = renderer.m_OpaqueSceneColor;
        auto* depth = renderer.m_OpaqueSceneDepth;
        auto* storage = renderer.m_ScreenStorageTarget;
        if (!color || !depth || !storage || !color->IsValid() || !depth->IsValid() ||
            !storage->IsValid() || !i.SceneTarget || !i.SceneDepthTarget || !i.SceneTargetRTV) return false;

        if (!ops.BeginPass("FogCopy")) return false;
        commands.CopyTexture(*color->GetNative(), nullptr, *i.SceneTarget, nullptr);
        commands.CopyTexture(*depth->GetNative(), nullptr, *i.SceneDepthTarget, nullptr);
        if (!ops.BeginPass("FogCompute")) return false;

        RHIVolumetricFogConstants constants{};
        constants.View = i.Camera.GetViewMatrix();
        constants.InvView = glm::inverse(constants.View);
        constants.Projection = i.ProjectionZO;
        constants.InvProjection = glm::inverse(i.ProjectionZO);
        constants.ScreenSize = glm::vec4(static_cast<float>(i.Width), static_cast<float>(i.Height),
            1.0f / static_cast<float>(i.Width), 1.0f / static_cast<float>(i.Height));
        if (i.World.DirectionalLight) {
            const glm::vec3 sunDir = -glm::normalize(i.World.DirectionalLight->Direction);
            constants.SunDirectionIntensity = glm::vec4(sunDir, i.World.DirectionalLight->Intensity);
            constants.SunColor = glm::vec4(i.World.DirectionalLight->Color, 1.0f);
        } else {
            constants.SunDirectionIntensity = glm::vec4(0.45f, 0.85f, 0.25f, 1.0f);
            constants.SunColor = glm::vec4(1.0f, 0.98f, 0.92f, 1.0f);
        }
        if (i.World.VolumetricFog && i.World.VolumetricFog->Enabled) {
            const auto& fog = *i.World.VolumetricFog;
            constants.FogParams = glm::vec4(fog.Density, fog.HeightFalloff,
                fog.ScatteringAsymmetry, fog.MaxDistance);
            constants.FogColorIntensity = glm::vec4(fog.Color, fog.Intensity);
        } else {
            constants.FogParams = glm::vec4(i.Settings.FogDensity, i.Settings.FogHeightFalloff,
                i.Settings.FogScattering, i.Settings.FogMaxDistance);
            constants.FogColorIntensity = glm::vec4(i.Settings.FogColor, 1.0f);
        }
        constants.DDGIParams = glm::vec4(i.Settings.DDGIMultiplier, 0.0f, 0.0f, 0.0f);
        if (!ops.Uniform().UpdateData(&constants, sizeof(constants))) return false;

        auto& pool = renderer.m_FogDescriptorPool;
        if (!pool.UpdateTexture(renderer.m_FogComputeTextureSet, 0, 0, depth->GetSRV()) ||
            !pool.UpdateSampler(renderer.m_FogComputeTextureSet, 1, 0, renderer.m_FogSampler.GetNative()) ||
            !pool.UpdateStorageTexture(renderer.m_FogComputeStorageSet, 0, 0, storage->GetUAV())) return false;
        commands.SetPipeline(renderer.m_FogComputePipeline);
        commands.SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_FogComputePipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
        pool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_FogComputeTextureSet, nri::BindPoint::COMPUTE) ||
            !ops.BindDescriptor(1, renderer.m_FogComputeStorageSet, nri::BindPoint::COMPUTE)) return false;
        commands.Dispatch((i.Width + 7) / 8, (i.Height + 7) / 8, 1);
        ++i.DispatchCount;

        if (!ops.BeginPass("FogApply") || !ops.Uniform().UpdateData(&constants, sizeof(constants)) ||
            !pool.UpdateTexture(renderer.m_FogApplyDescriptorSet, 0, 0, color->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_FogApplyDescriptorSet, 1, 0, storage->GetSRV()) ||
            !pool.UpdateSampler(renderer.m_FogApplyDescriptorSet, 2, 0, renderer.m_FogSampler.GetNative())) return false;
        nri::Pipeline* applyPipeline = i.SceneTargetFormat == nri::Format::RGBA16_SFLOAT && renderer.m_FogApplyPipelineHDR
            ? renderer.m_FogApplyPipelineHDR : renderer.m_FogApplyPipelineSDR;
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
        commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_FogApplyPipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
        pool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_FogApplyDescriptorSet, nri::BindPoint::GRAPHICS)) return false;
        commands.Draw(3, 1, 0, 0);
        ops.EndRendering();
        return commands.IsRecording();
    }
}
