#include "FroxelFogPass.hpp"
#include "../RHISceneRendererPasses.hpp"
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/RenderContracts.hpp>
#include <cmath>
#include <utility>
#include <vector>

namespace Axle::Passes {
    void FroxelFogPass::Declare(RenderGraph& graph, const FroxelFogResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        using Load = RenderGraph::Load;
        graph.AddPass("FogCopy", {
            {r.SceneColor, Layout::CopySource},
            {r.Input, Layout::CopyDest, Access::Write},
            {r.SceneDepth, Layout::CopySource},
            {r.Depth, Layout::CopyDest, Access::Write}});
        std::vector<RenderGraph::Use> injectUsages;
        injectUsages.reserve(3);
        if (r.UseCascadeAtlas) {
            injectUsages.push_back({r.CascadeAtlas, Layout::ShaderResource});
        }
        if (r.UseLocalShadowAtlas) {
            injectUsages.push_back({r.LocalShadowAtlas, Layout::ShaderResource});
        }
        injectUsages.push_back({r.FroxelScattering, Layout::ShaderResourceStorage, Access::Write});
        graph.AddPass("FogInject", std::move(injectUsages));
        graph.AddPass("FogIntegrate", {
            {r.FroxelScattering, Layout::ShaderResource},
            {r.FroxelIntegrated, Layout::ShaderResourceStorage, Access::Write}});
        graph.AddPass("FogApply", {
            {r.Input, Layout::ShaderResource},
            {r.Depth, Layout::ShaderResource},
            {r.FroxelIntegrated, Layout::ShaderResource},
            {r.FroxelScattering, Layout::ShaderResource},
            {r.SceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
    }

    bool FroxelFogPass::Record(RHISceneRenderer& renderer, const FroxelFogRecordInputs& i,
                               const PassRecordOps& ops) {
        auto& commands = renderer.Commands();
        auto* color = renderer.m_OpaqueSceneColor;
        auto* depth = renderer.m_OpaqueSceneDepth;
        auto* scattering = i.Targets ? i.Targets->GetFroxelScattering() : nullptr;
        auto* integrated = i.Targets ? i.Targets->GetFroxelIntegrated() : nullptr;
        if (!color || !depth || !scattering || !integrated ||
            !color->IsValid() || !depth->IsValid() || !scattering->IsValid() || !integrated->IsValid() ||
            !scattering->GetUAV() || !scattering->GetSRV() || !integrated->GetUAV() || !integrated->GetSRV() ||
            !i.SceneTarget || !i.SceneDepthTarget || !i.SceneTargetRTV || i.Width == 0 || i.Height == 0 ||
            !renderer.m_FogInjectPipeline || !renderer.m_FogIntegratePipeline) {
            return false;
        }

        const uint32_t gridW = scattering->GetWidth();
        const uint32_t gridH = scattering->GetHeight();
        const uint32_t gridD = scattering->GetDepth();
        if (gridW == 0 || gridH == 0 || gridD == 0 ||
            integrated->GetWidth() != gridW || integrated->GetHeight() != gridH || integrated->GetDepth() != gridD) {
            return false;
        }

        const auto& media = i.Media;

        RHIVolumetricFogConstants constants{};
        constants.InvView = glm::inverse(i.Camera.GetViewMatrix());
        constants.InvProjection = glm::inverse(i.ProjectionZO);
        constants.ScreenSize = glm::vec4(static_cast<float>(i.Width), static_cast<float>(i.Height),
            1.0f / static_cast<float>(i.Width), 1.0f / static_cast<float>(i.Height));
        constants.SunDirectionIntensity = glm::vec4(media.SunDirectionToLight, media.HasDirectionalSun ? 1.0f : 0.0f);
        constants.SunRadiance = glm::vec4(media.SunRadiance, 0.0f);
        constants.FogParams = glm::vec4(media.EffectiveBaseDensity, media.HeightFalloff, media.ScatteringAsymmetry, 0.0f);
        constants.FogAlbedo = glm::vec4(media.Albedo, 0.0f);
        constants.FroxelGridParams = glm::vec4(static_cast<float>(gridW), static_cast<float>(gridH),
            static_cast<float>(gridD), i.Camera.IsOrthographic() ? 1.0f : 0.0f);
        constants.AmbientRadiance = glm::vec4(media.AmbientRadiance, 0.0f);
        const float logRatio = std::log(media.MaxDistance / media.NearDistance);
        constants.FroxelDepthParams = glm::vec4(logRatio, media.NearDistance, media.MaxDistance, 0.0f);

        if (!ops.BeginPass("FogCopy")) return false;
        commands.CopyTexture(*color->GetNative(), nullptr, *i.SceneTarget, nullptr);
        commands.CopyTexture(*depth->GetNative(), nullptr, *i.SceneDepthTarget, nullptr);

        auto& pool = renderer.m_FogDescriptorPool;
        nri::Descriptor* lightCBV = renderer.m_LightBuffer.IsValid() ? renderer.m_LightBuffer.GetCBV() : nullptr;
        nri::Descriptor* cascadeCBV = renderer.m_CascadedShadows.GetConstantsCBV();
        nri::Descriptor* localShadowCBV = renderer.m_LocalShadows.GetConstantsCBV();
        nri::Descriptor* cascadeSRV = renderer.m_CascadedShadows.GetSRV();
        nri::Descriptor* localShadowSRV = renderer.m_LocalShadows.GetSRV();
        if (!lightCBV || !cascadeCBV || !localShadowCBV || !cascadeSRV || !localShadowSRV ||
            !renderer.m_FogShadowSampler.IsValid() || !renderer.m_FogInjectTextureSet || !renderer.m_FogInjectStorageSet) {
            return false;
        }

        if (!ops.BeginPass("FogInject") || !ops.Uniform().UpdateData(&constants, sizeof(constants)) ||
            !pool.UpdateTexture(renderer.m_FogInjectTextureSet, 0, 0, cascadeSRV) ||
            !pool.UpdateTexture(renderer.m_FogInjectTextureSet, 1, 0, localShadowSRV) ||
            !pool.UpdateStorageTexture(renderer.m_FogInjectStorageSet, 0, 0, scattering->GetUAV())) {
            return false;
        }
        commands.SetPipeline(renderer.m_FogInjectPipeline);
        commands.SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_FogInjectPipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
        commands.SetRootDescriptor(1, lightCBV, 0, nri::BindPoint::COMPUTE);
        commands.SetRootDescriptor(2, cascadeCBV, 0, nri::BindPoint::COMPUTE);
        commands.SetRootDescriptor(3, localShadowCBV, 0, nri::BindPoint::COMPUTE);
        pool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_FogInjectTextureSet, nri::BindPoint::COMPUTE) ||
            !ops.BindDescriptor(1, renderer.m_FogInjectStorageSet, nri::BindPoint::COMPUTE)) {
            return false;
        }
        commands.Dispatch((gridW + 3u) / 4u, (gridH + 3u) / 4u, (gridD + 3u) / 4u);
        ++i.DispatchCount;

        if (!ops.BeginPass("FogIntegrate") || !ops.Uniform().UpdateData(&constants, sizeof(constants)) ||
            !pool.UpdateTexture(renderer.m_FogIntegrateTextureSet, 0, 0, scattering->GetSRV()) ||
            !pool.UpdateStorageTexture(renderer.m_FogIntegrateStorageSet, 0, 0, integrated->GetUAV())) {
            return false;
        }
        commands.SetPipeline(renderer.m_FogIntegratePipeline);
        commands.SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_FogIntegratePipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
        pool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_FogIntegrateTextureSet, nri::BindPoint::COMPUTE) ||
            !ops.BindDescriptor(1, renderer.m_FogIntegrateStorageSet, nri::BindPoint::COMPUTE)) {
            return false;
        }
        commands.Dispatch((gridW + 7u) / 8u, (gridH + 7u) / 8u, 1u);
        ++i.DispatchCount;

        if (!ops.BeginPass("FogApply") || !ops.Uniform().UpdateData(&constants, sizeof(constants)) ||
            !pool.UpdateTexture(renderer.m_FogApplyDescriptorSet, 0, 0, color->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_FogApplyDescriptorSet, 1, 0, depth->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_FogApplyDescriptorSet, 2, 0, integrated->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_FogApplyDescriptorSet, 3, 0, scattering->GetSRV())) {
            return false;
        }
        nri::Pipeline* applyPipeline = renderer.m_FogApplyPipelineHDR;
        if (i.SceneTargetFormat == nri::Format::RGBA8_UNORM && renderer.m_FogApplyPipelineSDR) {
            applyPipeline = renderer.m_FogApplyPipelineSDR;
        } else if (i.SceneTargetFormat != nri::Format::RGBA16_SFLOAT || !applyPipeline) {
            applyPipeline = renderer.GetOrCreateFogApplyPipeline(i.SceneTargetFormat);
        }
        if (!applyPipeline) return false;

        nri::AttachmentDesc attachment{};
        attachment.descriptor = i.SceneTargetRTV;
        attachment.loadOp = nri::LoadOp::CLEAR;
        attachment.storeOp = nri::StoreOp::STORE;
        attachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };
        nri::RenderingDesc rendering{};
        rendering.colors = &attachment;
        rendering.colorNum = 1;
        ops.BeginRendering(rendering);
        commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(i.Width), static_cast<float>(i.Height), 0.0f, 1.0f });
        commands.SetScissor({ 0, 0, static_cast<nri::Dim_t>(i.Width), static_cast<nri::Dim_t>(i.Height) });
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
