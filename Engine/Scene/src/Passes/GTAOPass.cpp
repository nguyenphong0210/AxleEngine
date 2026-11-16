#include "GTAOPass.hpp"
#include "../RHISceneRendererPasses.hpp"
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RenderContracts.hpp>

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

    bool GTAOPass::Record(RHISceneRenderer& renderer, const GTAORecordInputs& i,
                          const PassRecordOps& ops) {
        auto& commands = renderer.Commands();
        auto* color = renderer.m_OpaqueSceneColor;
        auto* depth = renderer.m_OpaqueSceneDepth;
        auto* target = i.Targets->GetGTAO();
        if (!color || !depth || !target || !color->IsValid() || !depth->IsValid() || !target->IsValid()) return false;

        if (!ops.BeginPass("GTAOCopy")) return false;
        commands.CopyTexture(*color->GetNative(), nullptr, *i.SceneTarget, nullptr);
        commands.CopyTexture(*depth->GetNative(), nullptr, *i.SceneDepthTarget, nullptr);

        if (!ops.BeginPass("GTAOCompute")) return false;
        RHIGTAOConstants constants{};
        constants.InvProjection = glm::inverse(i.ProjectionZO);
        constants.View = i.Camera.GetViewMatrix();
        const uint32_t gtaoWidth = target->GetWidth();
        const uint32_t gtaoHeight = target->GetHeight();
        constants.ScreenSize = glm::vec4(static_cast<float>(gtaoWidth), static_cast<float>(gtaoHeight),
            1.0f / static_cast<float>(gtaoWidth), 1.0f / static_cast<float>(gtaoHeight));
        constants.GTAOParams = glm::vec4(i.Settings.GTAORadiusPixels * 0.5f,
            i.Settings.EnableGTAO ? i.Settings.GTAOStrength : 0.0f, 0.0025f, 2.0f);
        if (i.World.DirectionalLight && i.Settings.EnableContactShadows) {
            const glm::vec3 sunDir = glm::normalize(i.World.DirectionalLight->Direction);
            const glm::vec3 sunDirView = glm::mat3(i.Camera.GetViewMatrix()) * sunDir;
            constants.SunDirView = glm::vec4(sunDirView, 1.0f);
            constants.ContactParams = glm::vec4(i.Settings.ContactShadowLengthPixels, 16.0f,
                i.Settings.ContactShadowStrength, 0.02f);
        } else {
            constants.SunDirView = glm::vec4(0.0f);
            constants.ContactParams = glm::vec4(0.0f);
        }
        if (!ops.Uniform().UpdateData(&constants, sizeof(constants))) return false;

        auto& pool = renderer.m_GTAODescriptorPool;
        if (!pool.UpdateTexture(renderer.m_GTAOComputeTextureSet, 0, 0, depth->GetSRV())) return false;
        nri::Descriptor* normalSRV = renderer.m_OpaqueSceneNormal && renderer.m_OpaqueSceneNormal->IsValid()
            ? renderer.m_OpaqueSceneNormal->GetSRV() : RHI::RHIMaterial::GetDefaultBlackTexture()->GetSRV();
        if (!pool.UpdateTexture(renderer.m_GTAOComputeTextureSet, 1, 0, normalSRV) ||
            !pool.UpdateSampler(renderer.m_GTAOComputeTextureSet, 2, 0, renderer.m_GTAOSampler.GetNative()) ||
            !pool.UpdateStorageTexture(renderer.m_GTAOComputeStorageSet, 0, 0, target->GetUAV())) return false;
        commands.SetPipeline(renderer.m_GTAOComputePipeline);
        commands.SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_GTAOComputePipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
        pool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_GTAOComputeTextureSet, nri::BindPoint::COMPUTE) ||
            !ops.BindDescriptor(1, renderer.m_GTAOComputeStorageSet, nri::BindPoint::COMPUTE)) return false;
        commands.Dispatch((gtaoWidth + 7) / 8, (gtaoHeight + 7) / 8, 1);
        ++i.DispatchCount;

        if (!ops.BeginPass("GTAOApply") || !ops.Uniform().UpdateData(&constants, sizeof(constants)) ||
            !pool.UpdateTexture(renderer.m_GTAOApplyDescriptorSet, 0, 0, depth->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_GTAOApplyDescriptorSet, 1, 0, target->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_GTAOApplyDescriptorSet, 2, 0, color->GetSRV()) ||
            !pool.UpdateSampler(renderer.m_GTAOApplyDescriptorSet, 3, 0, renderer.m_GTAOSampler.GetNative())) return false;
        nri::Pipeline* applyPipeline = i.SceneTargetFormat == nri::Format::RGBA16_SFLOAT && renderer.m_GTAOApplyPipelineHDR
            ? renderer.m_GTAOApplyPipelineHDR : renderer.m_GTAOApplyPipelineSDR;
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
        commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_GTAOApplyPipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
        pool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_GTAOApplyDescriptorSet, nri::BindPoint::GRAPHICS)) return false;
        commands.Draw(3, 1, 0, 0);
        ops.EndRendering();
        return commands.IsRecording();
    }

    bool GTAOPass::IsPreLightingDataflowSupported() {
        return true;
    }

    bool GTAOPass::IsSeparateIndirectTargetRequired() {
        return OcclusionContract::IsSeparateIndirectTargetRequired(OcclusionContract::DataflowMode::PreLightingSeparated);
    }

    std::string_view GTAOPass::GetIndirectTargetDecisionRationale() {
        return OcclusionContract::GetIndirectTargetDecisionRationale();
    }
}
