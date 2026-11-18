#include "GTAOPass.hpp"
#include "../RHISceneRendererPasses.hpp"
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RenderContracts.hpp>
#include <algorithm>

namespace Axle::Passes {
    namespace {
        RHIGTAOConstants BuildConstants(const GTAORecordInputs& i) {
            RHIGTAOConstants constants{};
            constants.InvProjection = glm::inverse(i.ProjectionZO);
            constants.View = i.Camera.GetViewMatrix();
            const uint32_t width = i.Targets->GetGTAO()->GetWidth();
            const uint32_t height = i.Targets->GetGTAO()->GetHeight();
            constants.ScreenSize = glm::vec4(float(width), float(height), 1.0f / width, 1.0f / height);
            const float maxRadiusPx = std::max(i.Settings.GTAORadiusPixels * 2.0f, 16.0f);
            const float projRadiusScale = 0.5f * static_cast<float>(height) * std::abs(i.ProjectionZO[1][1]);
            constants.GTAOParams = glm::vec4(i.Settings.GTAORadiusPixels * 0.5f,
                i.Settings.EnableGTAO ? i.Settings.GTAOStrength : 0.0f, 0.0025f, std::max(0.1f, i.Settings.GTAOPower));
            constants.Reserved[0] = glm::vec4(
                std::max(0.05f, i.Settings.GTAOWorldRadius),
                projRadiusScale,
                std::clamp(i.Settings.GTAOFalloffStart, 0.05f, 0.95f),
                maxRadiusPx);
            if (i.World.DirectionalLight && i.Settings.EnableContactShadows) {
                const auto direction = i.World.DirectionalLight->Direction;
                const glm::vec3 sunDir = glm::dot(direction, direction) > 1e-12f ? glm::normalize(direction) : glm::vec3(0, 1, 0);
                constants.SunDirView = glm::vec4(glm::mat3(i.Camera.GetViewMatrix()) * sunDir, 1.0f);
                constants.ContactParams = glm::vec4(i.Settings.ContactShadowLengthPixels * 0.5f, 16.0f,
                    i.Settings.ContactShadowStrength, 0.02f);
            }
            return constants;
        }
    }

    void GTAOPass::DeclarePreLighting(RenderGraph& graph, const GTAOResources& r) {
        graph.AddPass("GTAOCompute", {
            {r.Depth, RHI::RHIResourceLayout::ShaderResource}, {r.SceneNormal, RHI::RHIResourceLayout::ShaderResource},
            {r.Storage, RHI::RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}});
        graph.AddPass("GTAOUpsample", {
            {r.Depth, RHI::RHIResourceLayout::ShaderResource}, {r.SceneNormal, RHI::RHIResourceLayout::ShaderResource},
            {r.Storage, RHI::RHIResourceLayout::ShaderResource},
            {r.ResolvedStorage, RHI::RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}});
    }

    bool GTAOPass::RecordPreLighting(RHISceneRenderer& renderer, const GTAORecordInputs& i,
                                     const PassRecordOps& ops) {
        return RecordCompute(renderer, i, ops) && RecordUpsample(renderer, i, ops);
    }

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

        if (!RecordCompute(renderer, i, ops)) return false;
        auto constants = BuildConstants(i);
        auto& pool = renderer.m_GTAODescriptorPool;

        // Reconstruction uses full-resolution scene texels and queries AO
        // dimensions separately. This bounded post-composite remains available
        // as a legacy fallback; the native forward path consumes visibility.
        const uint32_t fullWidth = std::max(1u, i.Width);
        const uint32_t fullHeight = std::max(1u, i.Height);
        constants.ScreenSize = glm::vec4(static_cast<float>(fullWidth), static_cast<float>(fullHeight),
            1.0f / static_cast<float>(fullWidth), 1.0f / static_cast<float>(fullHeight));

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

    bool GTAOPass::RecordCompute(RHISceneRenderer& renderer, const GTAORecordInputs& i,
                                 const PassRecordOps& ops) {
        auto& commands = renderer.Commands();
        auto* depth = renderer.m_OpaqueSceneDepth;
        auto* target = i.Targets->GetGTAO();
        if (!depth || !target || !depth->IsValid() || !target->IsValid()) return false;
        if (!ops.BeginPass("GTAOCompute")) return false;
        auto constants = BuildConstants(i);
        const uint32_t gtaoWidth = target->GetWidth();
        const uint32_t gtaoHeight = target->GetHeight();
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

        return commands.IsRecording();
    }

    bool GTAOPass::RecordUpsample(RHISceneRenderer& renderer, const GTAORecordInputs& i,
                                  const PassRecordOps& ops) {
        auto& commands = renderer.Commands();
        auto* depth = renderer.m_OpaqueSceneDepth;
        auto* halfTarget = i.Targets->GetGTAO();
        auto* resolvedTarget = i.Targets->GetGTAOResolved();
        if (!depth || !halfTarget || !resolvedTarget ||
            !depth->IsValid() || !halfTarget->IsValid() || !resolvedTarget->IsValid() ||
            !renderer.m_GTAOUpsamplePipeline) {
            return false;
        }
        if (!ops.BeginPass("GTAOUpsample")) return false;
        auto constants = BuildConstants(i);
        const uint32_t fullWidth = resolvedTarget->GetWidth();
        const uint32_t fullHeight = resolvedTarget->GetHeight();
        constants.ScreenSize = glm::vec4(static_cast<float>(fullWidth), static_cast<float>(fullHeight),
            1.0f / static_cast<float>(fullWidth), 1.0f / static_cast<float>(fullHeight));
        if (!ops.Uniform().UpdateData(&constants, sizeof(constants))) return false;

        auto& pool = renderer.m_GTAODescriptorPool;
        if (!pool.UpdateTexture(renderer.m_GTAOUpsampleTextureSet, 0, 0, depth->GetSRV())) return false;
        nri::Descriptor* normalSRV = renderer.m_OpaqueSceneNormal && renderer.m_OpaqueSceneNormal->IsValid()
            ? renderer.m_OpaqueSceneNormal->GetSRV() : RHI::RHIMaterial::GetDefaultBlackTexture()->GetSRV();
        if (!pool.UpdateTexture(renderer.m_GTAOUpsampleTextureSet, 1, 0, normalSRV) ||
            !pool.UpdateTexture(renderer.m_GTAOUpsampleTextureSet, 2, 0, halfTarget->GetSRV()) ||
            !pool.UpdateSampler(renderer.m_GTAOUpsampleTextureSet, 3, 0, renderer.m_GTAOSampler.GetNative()) ||
            !pool.UpdateStorageTexture(renderer.m_GTAOUpsampleStorageSet, 0, 0, resolvedTarget->GetUAV())) return false;
        commands.SetPipeline(renderer.m_GTAOUpsamplePipeline);
        commands.SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_GTAOUpsamplePipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
        pool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_GTAOUpsampleTextureSet, nri::BindPoint::COMPUTE) ||
            !ops.BindDescriptor(1, renderer.m_GTAOUpsampleStorageSet, nri::BindPoint::COMPUTE)) return false;
        commands.Dispatch((fullWidth + 7) / 8, (fullHeight + 7) / 8, 1);
        ++i.DispatchCount;

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
