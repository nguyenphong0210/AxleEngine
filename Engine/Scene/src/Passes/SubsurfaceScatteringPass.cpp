#include "SubsurfaceScatteringPass.hpp"
#include "../RHISceneRendererPasses.hpp"
#include <Axle/RenderWorld.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <algorithm>
#include <cmath>

namespace Axle::Passes {
    namespace {
        RHISSSConstants BuildSSSConstants(const SubsurfaceRecordInputs& i, float dirX, float dirY) {
            RHISSSConstants constants{};
            constants.InvProjection = glm::inverse(i.ProjectionZO);
            const uint32_t w = std::max(1u, i.Width);
            const uint32_t h = std::max(1u, i.Height);
            constants.ScreenSize = glm::vec4(static_cast<float>(w), static_cast<float>(h),
                                             1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
            const float projScale = 0.5f * std::abs(i.ProjectionZO[1][1]);
            constants.DirectionAndProj = glm::vec4(dirX, dirY, projScale, 0.035f);
            const bool linearHDR = i.SceneTargetFormat == nri::Format::RGBA16_SFLOAT;
            constants.SSSParams = glm::vec4(0.06f, 0.25f, 0.65f, linearHDR ? 1.0f : 0.0f);
            return constants;
        }
    }

    bool SubsurfaceScatteringPass::SceneRequiresSubsurfacePass(const RenderWorld& world,
                                                               const SceneRenderSettings& settings,
                                                               uint8_t sampleCount) {
        if (!settings.EnableSubsurfaceScattering || sampleCount != 1u) {
            return false;
        }
        return std::any_of(world.Meshes.begin(), world.Meshes.end(), [](const auto& mesh) {
            if (!mesh.NativeView || !mesh.NativeView->IsValid()) return false;
            if (mesh.Material.RHIMaterialAsset) {
                const auto& desc = mesh.Material.RHIMaterialAsset->GetDesc();
                return desc.SurfaceType < SurfaceType::Transparent &&
                       desc.SubsurfaceEnabled && desc.SubsurfaceRadius > 0.0f;
            }
            return mesh.Material.SubsurfaceEnabled && mesh.Material.SubsurfaceRadius > 0.0f;
        });
    }

    void SubsurfaceScatteringPass::Declare(RenderGraph& graph, const SubsurfaceResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        using Load = RenderGraph::Load;

        graph.AddPass("SSSCopy", {
            {r.SceneColor, Layout::CopySource}, {r.Input, Layout::CopyDest, Access::Write},
            {r.SceneDepth, Layout::CopySource}, {r.Depth, Layout::CopyDest, Access::Write}});
        graph.AddPass("SSSBlurH", {
            {r.Diffuse, Layout::ShaderResource}, {r.Depth, Layout::ShaderResource},
            {r.SceneNormal, Layout::ShaderResource}, {r.Control, Layout::ShaderResource},
            {r.Input, Layout::ShaderResource},
            {r.Temp, Layout::ColorAttachment, Access::Write, Load::Clear}});
        graph.AddPass("SSSBlurV", {
            {r.Temp, Layout::ShaderResource}, {r.Depth, Layout::ShaderResource},
            {r.SceneNormal, Layout::ShaderResource}, {r.Control, Layout::ShaderResource},
            {r.Input, Layout::ShaderResource},
            {r.SceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
    }

    bool SubsurfaceScatteringPass::Record(RHISceneRenderer& renderer,
                                          const SubsurfaceRecordInputs& i,
                                          const PassRecordOps& ops) {
        auto& commands = renderer.Commands();
        auto* specularCopy = renderer.m_OpaqueSceneColor;
        auto* depthCopy = renderer.m_OpaqueSceneDepth;
        auto* normalTex = renderer.m_OpaqueSceneNormal;
        auto* sssDiffuse = i.Targets ? i.Targets->GetSubsurfaceDiffuse() : nullptr;
        auto* sssControl = i.Targets ? i.Targets->GetSubsurfaceControl() : nullptr;
        auto* sssTemp = i.Targets ? i.Targets->GetSubsurfaceTemp() : nullptr;
        if (!specularCopy || !depthCopy || !normalTex || !sssDiffuse || !sssControl || !sssTemp ||
            !specularCopy->IsValid() || !depthCopy->IsValid() || !normalTex->IsValid() ||
            !sssDiffuse->IsValid() || !sssControl->IsValid() || !sssTemp->IsValid() ||
            !i.SceneTarget || !i.SceneDepthTarget || !i.SceneTargetRTV ||
            !renderer.m_SSSBlurHPipeline || !renderer.m_SSSBlurVPipelineHDR || !renderer.m_SSSBlurVPipelineSDR) {
            return false;
        }

        if (!ops.BeginPass("SSSCopy")) return false;
        commands.CopyTexture(*specularCopy->GetNative(), nullptr, *i.SceneTarget, nullptr);
        commands.CopyTexture(*depthCopy->GetNative(), nullptr, *i.SceneDepthTarget, nullptr);

        auto& pool = renderer.m_SSSDescriptorPool;
        const nri::Viewport vp{0.0f, 0.0f, static_cast<float>(i.Width), static_cast<float>(i.Height), 0.0f, 1.0f};
        const nri::Rect scissor{0, 0, static_cast<nri::Dim_t>(i.Width), static_cast<nri::Dim_t>(i.Height)};

        // Horizontal Separable SSS Pass
        {
            if (!ops.BeginPass("SSSBlurH")) return false;
            const auto hConstants = BuildSSSConstants(i, 1.0f, 0.0f);
            if (!ops.Uniform().UpdateData(&hConstants, sizeof(hConstants)) ||
                !pool.UpdateTexture(renderer.m_SSSBlurHDescriptorSet, 0, 0, sssDiffuse->GetSRV()) ||
                !pool.UpdateTexture(renderer.m_SSSBlurHDescriptorSet, 1, 0, depthCopy->GetSRV()) ||
                !pool.UpdateTexture(renderer.m_SSSBlurHDescriptorSet, 2, 0, normalTex->GetSRV()) ||
                !pool.UpdateTexture(renderer.m_SSSBlurHDescriptorSet, 3, 0, sssControl->GetSRV()) ||
                !pool.UpdateTexture(renderer.m_SSSBlurHDescriptorSet, 4, 0, specularCopy->GetSRV()) ||
                !pool.UpdateSampler(renderer.m_SSSBlurHDescriptorSet, 5, 0, renderer.m_SSSLinearSampler.GetNative()) ||
                !pool.UpdateSampler(renderer.m_SSSBlurHDescriptorSet, 6, 0, renderer.m_SSSPointSampler.GetNative())) {
                return false;
            }
            nri::AttachmentDesc att{};
            att.descriptor = sssTemp->GetRTV();
            att.loadOp = nri::LoadOp::CLEAR;
            att.storeOp = nri::StoreOp::STORE;
            att.clearValue.color.f = {0.0f, 0.0f, 0.0f, 0.0f};
            nri::RenderingDesc renderDesc{};
            renderDesc.colors = &att;
            renderDesc.colorNum = 1;
            ops.BeginRendering(renderDesc);
            commands.SetViewport(vp);
            commands.SetScissor(scissor);
            commands.SetPipeline(renderer.m_SSSBlurHPipeline);
            commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_SSSPipelineLayout);
            commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
            pool.Bind(commands.GetNative());
            if (!ops.BindDescriptor(0, renderer.m_SSSBlurHDescriptorSet, nri::BindPoint::GRAPHICS)) {
                ops.EndRendering();
                return false;
            }
            commands.Draw(3, 1, 0, 0);
            ops.EndRendering();
        }

        // Vertical Separable SSS Pass + Recombine with crisp specular & untouched non-SSS scene
        {
            if (!ops.BeginPass("SSSBlurV")) return false;
            const auto vConstants = BuildSSSConstants(i, 0.0f, 1.0f);
            nri::Pipeline* vPipeline = i.SceneTargetFormat == nri::Format::RGBA16_SFLOAT
                ? renderer.m_SSSBlurVPipelineHDR
                : renderer.m_SSSBlurVPipelineSDR;
            if (!ops.Uniform().UpdateData(&vConstants, sizeof(vConstants)) ||
                !pool.UpdateTexture(renderer.m_SSSBlurVDescriptorSet, 0, 0, sssTemp->GetSRV()) ||
                !pool.UpdateTexture(renderer.m_SSSBlurVDescriptorSet, 1, 0, depthCopy->GetSRV()) ||
                !pool.UpdateTexture(renderer.m_SSSBlurVDescriptorSet, 2, 0, normalTex->GetSRV()) ||
                !pool.UpdateTexture(renderer.m_SSSBlurVDescriptorSet, 3, 0, sssControl->GetSRV()) ||
                !pool.UpdateTexture(renderer.m_SSSBlurVDescriptorSet, 4, 0, specularCopy->GetSRV()) ||
                !pool.UpdateSampler(renderer.m_SSSBlurVDescriptorSet, 5, 0, renderer.m_SSSLinearSampler.GetNative()) ||
                !pool.UpdateSampler(renderer.m_SSSBlurVDescriptorSet, 6, 0, renderer.m_SSSPointSampler.GetNative())) {
                return false;
            }
            nri::AttachmentDesc att{};
            att.descriptor = i.SceneTargetRTV;
            att.loadOp = nri::LoadOp::CLEAR;
            att.storeOp = nri::StoreOp::STORE;
            att.clearValue.color.f = {0.0f, 0.0f, 0.0f, 1.0f};
            nri::RenderingDesc renderDesc{};
            renderDesc.colors = &att;
            renderDesc.colorNum = 1;
            ops.BeginRendering(renderDesc);
            commands.SetViewport(vp);
            commands.SetScissor(scissor);
            commands.SetPipeline(vPipeline);
            commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_SSSPipelineLayout);
            commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
            pool.Bind(commands.GetNative());
            if (!ops.BindDescriptor(0, renderer.m_SSSBlurVDescriptorSet, nri::BindPoint::GRAPHICS)) {
                ops.EndRendering();
                return false;
            }
            commands.Draw(3, 1, 0, 0);
            ops.EndRendering();
        }
        return commands.IsRecording();
    }
}
