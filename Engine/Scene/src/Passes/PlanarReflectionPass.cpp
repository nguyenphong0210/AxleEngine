#include "PlanarReflectionPass.hpp"
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <algorithm>
#include <cmath>

namespace Axle::Passes {
    void PlanarReflectionPass::Declare(RenderGraph& graph, const PlanarReflectionResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        using Load = RenderGraph::Load;

        if (r.NeedsColorCopy || r.NeedsDepthCopy) {
            std::vector<RenderGraph::Use> copyUses;
            if (r.NeedsColorCopy) {
                copyUses.push_back({r.SceneColor, Layout::CopySource});
                copyUses.push_back({r.Input, Layout::CopyDest, Access::Write});
            }
            if (r.NeedsDepthCopy) {
                copyUses.push_back({r.SceneDepth, Layout::CopySource});
                copyUses.push_back({r.Depth, Layout::CopyDest, Access::Write});
            }
            graph.AddPass("PlanarCopy", std::move(copyUses));
        }

        graph.AddPass("PlanarResolve", {
            {r.Depth, Layout::ShaderResource},
            {r.SceneNormal, Layout::ShaderResource},
            {r.Input, Layout::ShaderResource},
            {r.Environment, Layout::ShaderResource},
            {r.Response, Layout::ShaderResource},
            {r.PlanarColor, Layout::ShaderResource},
            {r.SceneColor, Layout::ColorAttachment, Access::Write, Load::Load}});

        // Explicit release pass ordering read completion on the imported planar color texture.
        graph.AddPass("PlanarRelease", {
            {r.PlanarColor, Layout::ShaderResource}},
            {"PlanarResolve"});
    }

    bool PlanarReflectionPass::Record(RHISceneRenderer& renderer,
                                      const PlanarReflectionRecordInputs& i,
                                      const PassRecordOps& ops) {
        auto& commands = renderer.Commands();
        auto* color = renderer.m_OpaqueSceneColor;
        auto* depth = renderer.m_OpaqueSceneDepth;
        auto* normal = renderer.m_OpaqueSceneNormal;
        auto* envSpec = i.Targets ? i.Targets->Get(RHI::RHIViewTarget::ReflectionEnvironment) : nullptr;
        auto* response = i.Targets ? i.Targets->Get(RHI::RHIViewTarget::ReflectionResponse) : nullptr;
        auto* planarColor = i.Targets ? i.Targets->GetPlanarColor() : nullptr;

        if (!color || !color->IsValid() || !depth || !depth->IsValid() ||
            !normal || !normal->IsValid() || !envSpec || !envSpec->IsValid() ||
            !response || !response->IsValid() || !planarColor || !planarColor->IsValid() ||
            !i.SceneTarget || !i.SceneTargetRTV || !i.SceneDepthTarget ||
            i.Width == 0 || i.Height == 0) {
            return false;
        }

        if (i.NeedsColorCopy || i.NeedsDepthCopy) {
            if (!ops.BeginPass("PlanarCopy")) {
                return false;
            }
            if (i.NeedsColorCopy) {
                commands.CopyTexture(*color->GetNative(), nullptr, *i.SceneTarget, nullptr);
            }
            if (i.NeedsDepthCopy) {
                commands.CopyTexture(*depth->GetNative(), nullptr, *i.SceneDepthTarget, nullptr);
            }
        }

        nri::Pipeline* applyPipeline = renderer.GetOrCreatePlanarApplyPipeline(i.SceneTargetFormat);
        if (!applyPipeline || !renderer.m_PlanarApplyDescriptorSet ||
            !renderer.m_PlanarDescriptorPool.IsValid() ||
            !renderer.m_PlanarSampler.IsValid()) {
            return false;
        }

        const PlanarReflectionContract::PlanarReflectionSurface surface =
            PlanarReflectionContract::BuildSurfaceFromReflector(i.Reflector);
        if (!surface.Valid) {
            return false;
        }

        const float maxRoughness = std::min(
            std::isfinite(i.Settings.PlanarReflectionMaxRoughness)
                ? std::clamp(i.Settings.PlanarReflectionMaxRoughness, 0.02f, 1.0f)
                : PlanarReflectionContract::DefaultMaxRoughness,
            std::isfinite(i.Reflector.MaxRoughness)
                ? std::clamp(i.Reflector.MaxRoughness, 0.02f, 1.0f)
                : PlanarReflectionContract::DefaultMaxRoughness);

        const float strength =
            std::clamp(std::isfinite(i.Settings.PlanarReflectionStrength) ? i.Settings.PlanarReflectionStrength : 1.0f, 0.0f, 2.0f) *
            std::clamp(std::isfinite(i.Reflector.Intensity) ? i.Reflector.Intensity : 1.0f, 0.0f, 2.0f);

        RHIPlanarReflectionConstants constants{};
        constants.View = i.Camera.GetViewMatrix();
        constants.InvView = glm::inverse(constants.View);
        constants.InvProjection = glm::inverse(i.ProjectionZO);
        constants.ReflectedViewProj = i.ReflectedViewProj;
        constants.PlaneWorld = glm::vec4(surface.Normal, -glm::dot(surface.Normal, surface.Position)); // Geometric receiver plane, without capture clipping bias.
        constants.PlaneOriginExtent = glm::vec4(surface.Position, surface.HalfExtents.x);
        constants.PlaneTangentExtent = glm::vec4(surface.Tangent, surface.HalfExtents.y);
        constants.PlaneBitangentFade = glm::vec4(surface.Bitangent, std::min(surface.EdgeFadeDistance, std::min(surface.HalfExtents.x, surface.HalfExtents.y)));
        constants.PlanarParams = glm::vec4(
            strength,
            maxRoughness,
            0.02f,
            i.Reflector.ClipToExtents ? 1.0f : 0.0f);
        constants.PlanarScreenSize = glm::vec4(
            static_cast<float>(i.Width),
            static_cast<float>(i.Height),
            static_cast<float>(planarColor->GetWidth()),
            static_cast<float>(planarColor->GetHeight()));

        if (!ops.BeginPass("PlanarResolve")) {
            return false;
        }
        if (!ops.Uniform().UpdateData(&constants, sizeof(constants))) {
            return false;
        }

        auto& pool = renderer.m_PlanarDescriptorPool;
        if (!pool.UpdateTexture(renderer.m_PlanarApplyDescriptorSet, 0, 0, depth->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_PlanarApplyDescriptorSet, 1, 0, normal->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_PlanarApplyDescriptorSet, 2, 0, color->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_PlanarApplyDescriptorSet, 3, 0, envSpec->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_PlanarApplyDescriptorSet, 4, 0, response->GetSRV()) ||
            !pool.UpdateTexture(renderer.m_PlanarApplyDescriptorSet, 5, 0, planarColor->GetSRV()) ||
            !pool.UpdateSampler(renderer.m_PlanarApplyDescriptorSet, 6, 0, renderer.m_PlanarSampler.GetNative())) {
            return false;
        }

        nri::AttachmentDesc applyColorAttachment{};
        applyColorAttachment.descriptor = i.SceneTargetRTV;
        applyColorAttachment.loadOp = nri::LoadOp::LOAD;
        applyColorAttachment.storeOp = nri::StoreOp::STORE;

        nri::RenderingDesc applyDesc{};
        applyDesc.colors = &applyColorAttachment;
        applyDesc.colorNum = 1;

        ops.BeginRendering(applyDesc);
        commands.SetViewport({0.0f, 0.0f, static_cast<float>(i.Width), static_cast<float>(i.Height), 0.0f, 1.0f});
        commands.SetScissor({0, 0, static_cast<nri::Dim_t>(i.Width), static_cast<nri::Dim_t>(i.Height)});
        commands.SetPipeline(applyPipeline);
        commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_PlanarApplyPipelineLayout);
        commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
        pool.Bind(commands.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_PlanarApplyDescriptorSet, nri::BindPoint::GRAPHICS)) {
            ops.EndRendering();
            return false;
        }
        commands.Draw(3, 1, 0, 0);
        renderer.m_LastDrawCallCount++;
        ops.EndRendering();

        return ops.BeginPass("PlanarRelease");
    }
}
