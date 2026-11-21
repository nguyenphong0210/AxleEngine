#include "DDGIPass.hpp"
#include "../RHISceneRendererPasses.hpp"
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/ViewContext.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Axle::Passes {
    using namespace RHI;
    static_assert(sizeof(DDGICaptureDraw{}.PushConstants) == sizeof(RHIObjectPushConstants),
        "DDGICaptureDraw push constant staging must match 128-byte RHIObjectPushConstants");

    bool DDGIPass::EnsureScratchTargets(RHISceneRenderer& renderer) {
        auto& device = RHIDevice::Get();
        if (!device.CanCreateResources()) {
            return false;
        }
        uint32_t maxCols = 0, maxRows = 0, maxWidth = 0, maxHeight = 0;
        DDGIContract::ComputeCaptureScratchLayout(
            DDGIContract::HardMaxProbesPerFrame, maxCols, maxRows, maxWidth, maxHeight);
        const uint64_t epoch = device.GetEpoch();

        if (!renderer.m_DDGICaptureColor.IsValid() ||
            renderer.m_DDGICaptureColor.GetWidth() != maxWidth ||
            renderer.m_DDGICaptureColor.GetHeight() != maxHeight ||
            renderer.m_DDGICaptureColor.GetHandle().Epoch != epoch) {
            if (!renderer.m_DDGICaptureColor.Init(RHITextureDesc::RenderTarget2D(
                    maxWidth, maxHeight, RHITextureFormat::RGBA16_FLOAT, "DDGICaptureColor"))) {
                return false;
            }
        }
        if (!renderer.m_DDGICaptureDepth.IsValid() ||
            renderer.m_DDGICaptureDepth.GetWidth() != maxWidth ||
            renderer.m_DDGICaptureDepth.GetHeight() != maxHeight ||
            renderer.m_DDGICaptureDepth.GetHandle().Epoch != epoch) {
            if (!renderer.m_DDGICaptureDepth.Init(RHITextureDesc::DepthStencil2D(
                    maxWidth, maxHeight, RHITextureFormat::D32_FLOAT, "DDGICaptureDepth"))) {
                return false;
            }
        }
        return renderer.m_DDGICaptureColor.GetRTV() && renderer.m_DDGICaptureColor.GetSRV() &&
               renderer.m_DDGICaptureDepth.GetDSV() && renderer.m_DDGICaptureDepth.GetSRV();
    }

    bool DDGIPass::BuildPlan(
        RHISceneRenderer& renderer,
        ViewContext& viewCtx,
        const RenderWorld& world,
        const DDGIHistoryFrame& previous,
        uint64_t stateHash,
        const GIFrame& giFrame,
        const RHI::RHISceneConstants& mainSceneConstants,
        const RHISkyboxConstants* skyboxBaseConstants,
        bool hasSkyboxDraw,
        RHIViewTargets* viewTargets,
        std::vector<DDGICaptureDraw>&& draws,
        DDGIPlan& outPlan) {
        outPlan = DDGIPlan{};
        if (!giFrame.UpdateScheduled || !world.DDGIProbeGrid || !viewTargets) {
            return true;
        }
        auto* irrad = viewTargets->GetDDGI();
        auto* dist = viewTargets->GetDDGIDistance();
        if (!irrad || !irrad->IsValid() || !irrad->GetUAV() || !irrad->GetSRV() ||
            !dist || !dist->IsValid() || !dist->GetUAV() || !dist->GetSRV()) {
            return false;
        }
        if (!EnsureScratchTargets(renderer)) {
            return false;
        }

        const auto& grid = *world.DDGIProbeGrid;
        const auto extent = DDGIContract::ComputeAtlasExtent(grid.GridDimensions);
        if (!extent.Valid || extent.ProbeCount == 0 ||
            irrad->GetWidth() != extent.IrradianceWidth || irrad->GetHeight() != extent.IrradianceHeight ||
            dist->GetWidth() != extent.DistanceWidth || dist->GetHeight() != extent.DistanceHeight) {
            return false;
        }

        const bool hasHistory = previous.Valid;
        const bool resetSeed = !hasHistory;
        const uint32_t startCursor = resetSeed ? 0u : (previous.NextProbeCursor % extent.ProbeCount);
        const uint32_t updateCount = DDGIContract::ComputeStaggeredUpdateCount(
            extent.ProbeCount, giFrame.UpdateFraction, giFrame.MaxProbesPerFrame, resetSeed);
        if (updateCount == 0u) {
            return false;
        }

        uint32_t capCols = 0, capRows = 0, capW = 0, capH = 0;
        DDGIContract::ComputeCaptureScratchLayout(updateCount, capCols, capRows, capW, capH);

        const float maxRayDist = DDGIContract::ComputeProbeMaxDistance(grid.GridSpacing);
        const float captureNear = 0.05f;
        const float captureFar = std::max(maxRayDist * 2.0f, 100.0f);

        outPlan.Active = true;
        outPlan.ResetSeed = resetSeed;
        outPlan.HasHistory = hasHistory;
        outPlan.HasSkyboxDraw = hasSkyboxDraw && skyboxBaseConstants != nullptr;
        outPlan.StateHash = stateHash;
        outPlan.ProbeCount = extent.ProbeCount;
        outPlan.StartProbeCursor = startCursor;
        outPlan.UpdateCount = updateCount;
        outPlan.NextProbeCursor = (startCursor + updateCount) % extent.ProbeCount;
        outPlan.CaptureCols = capCols;
        outPlan.CaptureRows = capRows;
        outPlan.CaptureWidth = capW;
        outPlan.CaptureHeight = capH;
        outPlan.CaptureNear = captureNear;
        outPlan.CaptureFar = captureFar;
        outPlan.ProbeMaxDistance = maxRayDist;
        outPlan.AtlasExtent = extent;
        outPlan.Draws = std::move(draws);

        auto& cb = outPlan.Constants;
        cb.ScreenSize = mainSceneConstants.CameraPosition; // Populated accurately in Constants.ScreenSize below if needed; keep view extents:
        cb.ScreenSize = glm::vec4(
            static_cast<float>(viewCtx.GetInput().Width),
            static_cast<float>(viewCtx.GetInput().Height),
            1.0f / static_cast<float>(std::max(1u, viewCtx.GetInput().Width)),
            1.0f / static_cast<float>(std::max(1u, viewCtx.GetInput().Height)));
        cb.AtlasSize = glm::vec4(
            static_cast<float>(extent.IrradianceWidth),
            static_cast<float>(extent.IrradianceHeight),
            static_cast<float>(extent.DistanceWidth),
            static_cast<float>(extent.DistanceHeight));
        cb.GridDimensions = glm::vec4(
            static_cast<float>(grid.GridDimensions.x),
            static_cast<float>(grid.GridDimensions.y),
            static_cast<float>(grid.GridDimensions.z),
            static_cast<float>(extent.ProbeCount));
        cb.GridOrigin = glm::vec4(grid.GridOrigin, static_cast<float>(extent.AtlasCols));
        cb.GridSpacing = glm::vec4(grid.GridSpacing, maxRayDist);
        cb.Params = glm::vec4(
            giFrame.EnergyMultiplier,
            giFrame.Hysteresis,
            giFrame.ViewBias,
            giFrame.NormalBias);
        cb.UpdateParams = glm::vec4(
            static_cast<float>(startCursor),
            static_cast<float>(updateCount),
            static_cast<float>(capCols),
            static_cast<float>(DDGIContract::CaptureFaceResolution));
        cb.Flags = glm::vec4(
            1.0f, // Forward diffuse GI sampling enabled after DDGIUpdate completes
            hasHistory ? 1.0f : 0.0f,
            resetSeed ? 1.0f : 0.0f,
            static_cast<float>(extent.AtlasRows));
        cb.ClipParams = glm::vec4(captureNear, captureFar, 0.0f, 0.0f);

        // Multi-bounce capture samples prior-frame DDGI irradiance only when steady-state history exists.
        auto* captureConstsCB = viewCtx.PreparePassUniform("DDGICaptureConsts", sizeof(DDGIContract::DDGIShaderConstants));
        if (!captureConstsCB) {
            return false;
        }
        DDGIContract::DDGIShaderConstants captureConstants = cb;
        captureConstants.Flags.x = hasHistory ? 1.0f : 0.0f;
        // Apply the authored energy gain at the receiver without amplifying the feedback loop.
        captureConstants.Params.x = 1.0f;
        const auto& shadowView = mainSceneConstants.View;
        captureConstants.CaptureShadowDepth = -glm::vec4(
            shadowView[0][2], shadowView[1][2], shadowView[2][2], shadowView[3][2]);
        if (!captureConstsCB->UpdateData(&captureConstants, sizeof(captureConstants))) {
            return false;
        }
        outPlan.CaptureConstantsCB = captureConstsCB;

        const uint32_t totalFaces = updateCount * DDGIContract::FacesPerProbe;
        outPlan.Faces.reserve(totalFaces);
        for (uint32_t slot = 0; slot < updateCount; ++slot) {
            const uint32_t probeIdx = (startCursor + slot) % extent.ProbeCount;
            const glm::vec3 probePos = DDGIContract::ProbeIndexToWorldPos(probeIdx, grid);
            for (uint32_t face = 0; face < DDGIContract::FacesPerProbe; ++face) {
                const uint32_t linearIdx = slot * DDGIContract::FacesPerProbe + face;
                const glm::ivec2 tile = DDGIContract::CaptureSlotToTile(slot, face, capCols);
                const PerspectiveCamera faceCam = ReflectionCaptureContract::BuildCubeFaceCamera(
                    probePos, face, captureNear, captureFar);

                auto* sceneCB = viewCtx.PreparePassUniform(
                    "DDGICapScene_" + std::to_string(linearIdx), sizeof(RHISceneConstants));
                if (!sceneCB) {
                    return false;
                }
                RHISceneConstants faceScene = mainSceneConstants;
                faceScene.View = faceCam.GetViewMatrix();
                faceScene.Projection = faceCam.GetProjectionMatrixZO();
                faceScene.ViewProjection = faceCam.GetViewProjectionMatrixZO();
                faceScene.CameraPosition = glm::vec4(probePos, 1.0f); // Always linear HDR capture
                if (!sceneCB->UpdateData(&faceScene, sizeof(RHISceneConstants))) {
                    return false;
                }

                RHIBuffer* skyCB = nullptr;
                if (outPlan.HasSkyboxDraw) {
                    skyCB = viewCtx.PreparePassUniform(
                        "DDGICapSky_" + std::to_string(linearIdx), sizeof(RHISkyboxConstants));
                    if (!skyCB) {
                        return false;
                    }
                    RHISkyboxConstants faceSky = *skyboxBaseConstants;
                    faceSky.InvViewProjection = glm::inverse(
                        faceCam.GetUnclippedProjectionMatrixZO() * faceCam.GetViewMatrix());
                    faceSky.CameraPosition = glm::vec4(probePos, 1.0f);
                    faceSky.Params.z = -1.0f; // Force linear HDR radiance into capture atlas
                    faceSky.Params.w = 1.0f;
                    if (faceSky.Reserved[0].x > 0.5f) {
                        faceSky.Reserved[0].z = skyboxBaseConstants->Reserved[0].z;
                    }
                    if (!skyCB->UpdateData(&faceSky, sizeof(RHISkyboxConstants))) {
                        return false;
                    }
                }

                DDGICaptureFaceTarget target{};
                target.ProbeSlot = slot;
                target.FaceIndex = face;
                target.TileX = static_cast<uint32_t>(tile.x);
                target.TileY = static_cast<uint32_t>(tile.y);
                target.SceneCB = sceneCB;
                target.SkyCB = skyCB;
                outPlan.Faces.push_back(target);
            }
        }
        return true;
    }

    void DDGIPass::Declare(
        RenderGraph& graph,
        const DDGIPlan& plan,
        RenderGraph::Resource captureColor,
        RenderGraph::Resource captureDepth,
        RenderGraph::Resource irradAtlas,
        RenderGraph::Resource distAtlas,
        RenderGraph::Resource shadowAtlas,
        RenderGraph::Resource localShadowAtlas,
        bool hasCascadeAtlas,
        bool hasLocalAtlas) {
        if (!plan.Active) {
            return;
        }
        using Layout = RHIResourceLayout;
        using Access = RenderGraph::Access;
        using Load = RenderGraph::Load;

        std::vector<RenderGraph::Use> capUses{
            {captureColor, Layout::ColorAttachment, Access::Write, Load::Clear},
            {captureDepth, Layout::DepthStencilWrite, Access::Write, Load::Clear}
        };
        if (!plan.ResetSeed) {
            capUses.push_back({irradAtlas, Layout::ShaderResource});
            capUses.push_back({distAtlas, Layout::ShaderResource});
        }
        if (hasCascadeAtlas) {
            capUses.push_back({shadowAtlas, Layout::ShaderResource});
        }
        if (hasLocalAtlas) {
            capUses.push_back({localShadowAtlas, Layout::ShaderResource});
        }
        graph.AddPass("DDGICapture", std::move(capUses));

        if (plan.ResetSeed) {
            graph.AddPass("DDGISeed", {
                {irradAtlas, Layout::ShaderResourceStorage, Access::Write},
                {distAtlas, Layout::ShaderResourceStorage, Access::Write}
            });
        }

        graph.AddPass("DDGIUpdate", {
            {captureColor, Layout::ShaderResource},
            {captureDepth, Layout::ShaderResource},
            {irradAtlas, Layout::ShaderResourceStorage, Access::ReadWrite},
            {distAtlas, Layout::ShaderResourceStorage, Access::ReadWrite}
        });
    }

    bool DDGIPass::PrepareGraphBindings(
        RHISceneRenderer& renderer,
        RenderGraph& graph,
        const DDGIPlan& plan,
        RHITexture* irradAtlas,
        RHITexture* distAtlas,
        nri::DescriptorSet* captureOcclusionSet,
        bool hasSkybox) {
        if (!plan.Active) {
            return true;
        }
        auto* neutral = RHIMaterial::GetDefaultWhiteTexture().get();
        if (!irradAtlas || !distAtlas || !irradAtlas->GetUAV() || !distAtlas->GetUAV() ||
            !irradAtlas->GetSRV() || !distAtlas->GetSRV() || !neutral || !neutral->GetSRV() ||
            !renderer.m_DDGICaptureColor.GetSRV() || !renderer.m_DDGICaptureDepth.GetSRV()) {
            return false;
        }

        if (!renderer.m_DDGIConstantBuffer.UpdateData(&plan.Constants, sizeof(plan.Constants))) {
            return false;
        }
        if (!captureOcclusionSet || !renderer.m_ForwardOcclusionDescriptorSet ||
            !renderer.m_DescriptorPool.UpdateTextureRange(captureOcclusionSet, 3, 0,
                {plan.HasHistory ? irradAtlas->GetSRV() : neutral->GetSRV(),
                 plan.HasHistory ? distAtlas->GetSRV() : neutral->GetSRV()}) ||
            !renderer.m_DescriptorPool.UpdateTextureRange(renderer.m_ForwardOcclusionDescriptorSet, 3, 0,
                {irradAtlas->GetSRV(), distAtlas->GetSRV()})) {
            return false;
        }

        // Capture pass descriptor & buffer read declarations
        if (!plan.CaptureConstantsCB || !plan.CaptureConstantsCB->GetCBV() ||
            !graph.DeclareRead("DDGICapture", plan.CaptureConstantsCB->GetCBV()) ||
            !graph.DeclareRead("DDGICapture", captureOcclusionSet) ||
            !graph.DeclareRead("DDGICapture", renderer.m_ForwardFogFallbackDescriptorSet) ||
            !graph.DeclareRead("DDGICapture", renderer.m_DisabledFogConstantBuffer.GetCBV()) ||
            !graph.DeclareRead("DDGICapture", renderer.m_DefaultDescriptorSet)) {
            return false;
        }
        for (auto* cbv : {
            renderer.m_LightBuffer.GetCBV(),
            renderer.m_CascadedShadows.GetConstantsCBV(),
            renderer.m_LocalShadows.GetConstantsCBV(),
            renderer.m_ReflectionProbeBuffer.GetCBV()
        }) {
            if (!cbv || !graph.DeclareRead("DDGICapture", cbv)) {
                return false;
            }
        }
        for (uint32_t slot = 0; slot < MaxActiveReflectionProbes; ++slot) {
            const auto& probeTex = renderer.m_ActiveReflectionProbePrefilters[slot];
            if (probeTex && probeTex->IsValid() && !renderer.IsCaptureFeedbackTexture(*probeTex)) {
                if (!graph.DeclareRead("DDGICapture", probeTex->GetSRV())) {
                    return false;
                }
            }
        }
        if (hasSkybox && renderer.m_SkyboxDescriptorSet &&
            !graph.DeclareRead("DDGICapture", renderer.m_SkyboxDescriptorSet)) {
            return false;
        }
        for (const auto& face : plan.Faces) {
            if (!face.SceneCB || !graph.DeclareRead("DDGICapture", face.SceneCB->GetCBV())) {
                return false;
            }
            if (face.SkyCB && !graph.DeclareRead("DDGICapture", face.SkyCB->GetCBV())) {
                return false;
            }
        }
        auto& resources = RHIDevice::Get().GetResources();
        for (const auto& draw : plan.Draws) {
            if (!draw.Mesh || !draw.Mesh->IsValid() || !draw.MaterialCBV || !draw.MaterialSet) {
                return false;
            }
            if (!graph.DeclareBuffer("DDGICapture", resources.Acquire(draw.Mesh->GetVertexHandle()), RHIAccess::VertexBuffer) ||
                !graph.DeclareBuffer("DDGICapture", resources.Acquire(draw.Mesh->GetIndexHandle()), RHIAccess::IndexBuffer) ||
                !graph.DeclareRead("DDGICapture", draw.MaterialCBV) ||
                !graph.DeclareRead("DDGICapture", draw.MaterialSet)) {
                return false;
            }
        }

        // Rebuild compute descriptor arena safely so concurrent views/resizes never mutate in-flight sets
        if (!renderer.m_DDGIDescriptorPool.Init(renderer.m_DDGIDescriptorPool.GetDesc())) {
            return false;
        }
        renderer.m_DDGITextureSet = renderer.m_DDGIDescriptorPool.AllocateDescriptorSet(renderer.m_DDGIPipelineLayout, 0);
        renderer.m_DDGIStorageSet = renderer.m_DDGIDescriptorPool.AllocateDescriptorSet(renderer.m_DDGIPipelineLayout, 1);
        if (!renderer.m_DDGITextureSet || !renderer.m_DDGIStorageSet) {
            return false;
        }
        if (!renderer.m_DDGIDescriptorPool.UpdateTexture(renderer.m_DDGITextureSet, 0, 0, renderer.m_DDGICaptureColor.GetSRV()) ||
            !renderer.m_DDGIDescriptorPool.UpdateTexture(renderer.m_DDGITextureSet, 0, 1, renderer.m_DDGICaptureDepth.GetSRV()) ||
            !renderer.m_DDGIDescriptorPool.UpdateSampler(renderer.m_DDGITextureSet, 1, 0, renderer.m_DDGISampler.GetNative()) ||
            !renderer.m_DDGIDescriptorPool.UpdateStorageTexture(renderer.m_DDGIStorageSet, 0, 0, irradAtlas->GetUAV()) ||
            !renderer.m_DDGIDescriptorPool.UpdateStorageTexture(renderer.m_DDGIStorageSet, 0, 1, distAtlas->GetUAV())) {
            return false;
        }

        // Only declare SRV descriptor set reads via DeclareRead; UAV storage sets use ShaderResourceStorage in Declare()
        if (plan.ResetSeed && !graph.DeclareRead("DDGISeed", renderer.m_DDGITextureSet)) {
            return false;
        }
        return graph.DeclareRead("DDGIUpdate", renderer.m_DDGITextureSet);
    }

    bool DDGIPass::RecordCapture(
        RHISceneRenderer& renderer,
        const DDGIPlan& plan,
        const PassRecordOps& ops,
        nri::Pipeline* skyboxPipeline,
        nri::DescriptorSet* captureOcclusionSet,
        uint32_t& outDrawCount) {
        if (!plan.Active) {
            return true;
        }
        auto* capturePipeline = renderer.GetOrCreateDDGICapturePipeline();
        if (!capturePipeline || !renderer.m_DDGICaptureColor.IsValid() || !renderer.m_DDGICaptureDepth.IsValid()) {
            return false;
        }
        if (!ops.BeginPass("DDGICapture")) {
            return false;
        }

        nri::AttachmentDesc colorAttach{};
        colorAttach.descriptor = renderer.m_DDGICaptureColor.GetRTV();
        colorAttach.loadOp = nri::LoadOp::CLEAR;
        colorAttach.storeOp = nri::StoreOp::STORE;
        colorAttach.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f }; // Alpha 1.0 keeps sky/miss distinct from backface 0.0

        nri::AttachmentDesc depthAttach{};
        depthAttach.descriptor = renderer.m_DDGICaptureDepth.GetDSV();
        depthAttach.loadOp = nri::LoadOp::CLEAR;
        depthAttach.storeOp = nri::StoreOp::STORE;
        depthAttach.clearValue.depthStencil.depth = 1.0f;

        nri::RenderingDesc renderDesc{};
        renderDesc.colors = &colorAttach;
        renderDesc.colorNum = 1;
        renderDesc.depth = depthAttach;
        ops.BeginRendering(renderDesc);

        auto& cmd = renderer.Commands();
        const float faceResF = static_cast<float>(DDGIContract::CaptureFaceResolution);
        const auto faceResI = static_cast<nri::Dim_t>(DDGIContract::CaptureFaceResolution);

        // Pass 1 across all probe face tiles: opaque geometry
        if (!plan.Draws.empty()) {
            cmd.SetPipeline(capturePipeline);
            cmd.SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_PipelineLayout);
            cmd.SetRootDescriptor(1, renderer.m_LightBuffer.GetCBV());
            cmd.SetRootDescriptor(3, renderer.m_CascadedShadows.GetConstantsCBV());
            cmd.SetRootDescriptor(4, renderer.m_LocalShadows.GetConstantsCBV());
            cmd.SetRootDescriptor(5, renderer.m_ReflectionProbeBuffer.GetCBV());
            cmd.SetRootDescriptor(6, renderer.m_DisabledFogConstantBuffer.GetCBV());
            cmd.SetRootDescriptor(7, plan.CaptureConstantsCB->GetCBV());
            renderer.m_DescriptorPool.Bind(cmd.GetNative());
            if (!ops.BindDescriptor(1, captureOcclusionSet, nri::BindPoint::GRAPHICS) ||
                !ops.BindDescriptor(2, renderer.m_ForwardFogFallbackDescriptorSet, nri::BindPoint::GRAPHICS)) {
                ops.EndRendering();
                return false;
            }

            for (const auto& face : plan.Faces) {
                cmd.SetViewport({ static_cast<float>(face.TileX) * faceResF,
                                  static_cast<float>(face.TileY) * faceResF,
                                  faceResF, faceResF, 0.0f, 1.0f });
                cmd.SetScissor({ static_cast<int16_t>(face.TileX * DDGIContract::CaptureFaceResolution),
                                 static_cast<int16_t>(face.TileY * DDGIContract::CaptureFaceResolution),
                                 faceResI, faceResI });
                cmd.SetRootDescriptor(0, face.SceneCB->GetCBV());

                for (const auto& draw : plan.Draws) {
                    cmd.SetRootDescriptor(2, draw.MaterialCBV);
                    if (!ops.BindDescriptor(0, draw.MaterialSet, nri::BindPoint::GRAPHICS)) {
                        ops.EndRendering();
                        return false;
                    }
                    cmd.SetRootConstants(0, draw.PushConstants.data(), static_cast<uint32_t>(sizeof(draw.PushConstants)));
                    draw.Mesh->Bind(cmd);
                    draw.Mesh->Draw(cmd);
                    outDrawCount += static_cast<uint32_t>(draw.Mesh->GetSubmeshCount());
                }
            }
        }

        // Pass 2 across all probe face tiles: procedural/HDR skybox fill at depth == 1.0
        if (plan.HasSkyboxDraw && skyboxPipeline && renderer.m_SkyboxDescriptorSet) {
            cmd.SetPipeline(skyboxPipeline);
            cmd.SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_SkyboxPipelineLayout);
            renderer.m_DescriptorPool.Bind(cmd.GetNative());
            if (!ops.BindDescriptor(0, renderer.m_SkyboxDescriptorSet, nri::BindPoint::GRAPHICS)) {
                ops.EndRendering();
                return false;
            }

            for (const auto& face : plan.Faces) {
                if (!face.SkyCB) {
                    continue;
                }
                cmd.SetViewport({ static_cast<float>(face.TileX) * faceResF,
                                  static_cast<float>(face.TileY) * faceResF,
                                  faceResF, faceResF, 0.0f, 1.0f });
                cmd.SetScissor({ static_cast<int16_t>(face.TileX * DDGIContract::CaptureFaceResolution),
                                 static_cast<int16_t>(face.TileY * DDGIContract::CaptureFaceResolution),
                                 faceResI, faceResI });
                cmd.SetRootDescriptor(0, face.SkyCB->GetCBV());
                cmd.Draw(3, 1, 0, 0);
                ++outDrawCount;
            }
        }

        ops.EndRendering();
        return cmd.IsRecording();
    }

    bool DDGIPass::RecordSeedAndUpdate(
        RHISceneRenderer& renderer,
        const DDGIPlan& plan,
        const PassRecordOps& ops,
        uint32_t& outDispatchCount) {
        if (!plan.Active) {
            return true;
        }
        auto& cmd = renderer.Commands();
        auto& pool = renderer.m_DDGIDescriptorPool;

        if (plan.ResetSeed) {
            if (!renderer.m_DDGISeedPipeline) {
                return false;
            }
            if (!ops.BeginPass("DDGISeed") ||
                !ops.Uniform().UpdateData(&plan.Constants, sizeof(plan.Constants))) {
                return false;
            }
            cmd.SetPipeline(renderer.m_DDGISeedPipeline);
            cmd.SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_DDGIPipelineLayout);
            cmd.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
            pool.Bind(cmd.GetNative());
            if (!ops.BindDescriptor(0, renderer.m_DDGITextureSet, nri::BindPoint::COMPUTE) ||
                !ops.BindDescriptor(1, renderer.m_DDGIStorageSet, nri::BindPoint::COMPUTE)) {
                return false;
            }
            cmd.Dispatch((plan.AtlasExtent.DistanceWidth + 7u) / 8u,
                         (plan.AtlasExtent.DistanceHeight + 7u) / 8u, 1u);
            ++outDispatchCount;
        }

        if (!renderer.m_DDGIIrradiancePipeline || !renderer.m_DDGIDistancePipeline) {
            return false;
        }
        if (!ops.BeginPass("DDGIUpdate") ||
            !ops.Uniform().UpdateData(&plan.Constants, sizeof(plan.Constants))) {
            return false;
        }
        cmd.SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_DDGIPipelineLayout);
        cmd.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
        pool.Bind(cmd.GetNative());
        if (!ops.BindDescriptor(0, renderer.m_DDGITextureSet, nri::BindPoint::COMPUTE) ||
            !ops.BindDescriptor(1, renderer.m_DDGIStorageSet, nri::BindPoint::COMPUTE)) {
            return false;
        }

        cmd.SetPipeline(renderer.m_DDGIIrradiancePipeline);
        cmd.Dispatch(plan.UpdateCount, 1u, 1u);
        ++outDispatchCount;

        cmd.SetPipeline(renderer.m_DDGIDistancePipeline);
        cmd.Dispatch(plan.UpdateCount, 1u, 1u);
        ++outDispatchCount;

        return cmd.IsRecording();
    }

} // namespace Axle::Passes
