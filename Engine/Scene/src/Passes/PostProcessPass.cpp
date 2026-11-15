#include "PostProcessPass.hpp"
#include "../RHISceneRendererPasses.hpp"
#include <Axle/Log.hpp>
#include <cmath>

namespace Axle::Passes {
    using namespace RHI;
    void PostProcessPass::Declare(RenderGraph& graph, const PostProcessResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        using Load = RenderGraph::Load;
        if (r.UseBloom) {
            graph.AddPass("BloomCopy", {{r.SceneColor, Layout::CopySource}, {r.BloomInput, Layout::CopyDest, Access::Write}});
            for (uint32_t level = 0; level < r.Bloom.size(); ++level) {
                graph.AddPass("BloomDown" + std::to_string(level), {
                    {level == 0 ? r.BloomInput : r.Bloom[level - 1], Layout::ShaderResource},
                    {r.Bloom[level], Layout::ShaderResourceStorage, Access::Write}});
            }
            for (int level = static_cast<int>(r.Bloom.size()) - 2; level >= 0; --level) {
                graph.AddPass("BloomUp" + std::to_string(level), {
                    {r.Bloom[level + 1], Layout::ShaderResource},
                    {r.Bloom[level], Layout::ShaderResourceStorage, Access::Write}});
            }
            graph.AddPass("BloomApply", {
                {r.Bloom[0], Layout::ShaderResource}, {r.BloomInput, Layout::ShaderResource},
                {r.SceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
        }
        if (r.UseAutoExposure) {
            if (!r.UseToneMap) graph.AddPass("AutoExposureCopy", {
                {r.SceneColor, Layout::CopySource}, {r.ExposureInput, Layout::CopyDest, Access::Write}});
            graph.AddPass("AutoExposure", {
                {r.ExposureInput, Layout::ShaderResource},
                {r.ExposureOutput, Layout::ShaderResourceStorage, Access::Write}});
        }
        if (r.UseTAA) {
            const auto source = r.UseToneMap ? r.HDR : r.SceneColor;
            graph.AddPass("TAAResolve", {
                {source, Layout::ShaderResource}, {r.SceneDepth, Layout::ShaderResource},
                {r.TAAHistoryRead, Layout::ShaderResource},
                {r.TAAHistoryWrite, Layout::ColorAttachment, Access::Write, Load::Clear}});
            graph.AddPass("TAACopy", {
                {r.TAAHistoryWrite, Layout::CopySource}, {source, Layout::CopyDest, Access::Write}});
        }
        if (r.UseToneMap) {
            if (r.UseAutoExposure) graph.AddPass("ToneMapping", {
                {r.HDR, Layout::ShaderResource}, {r.ExposureOutput, Layout::ShaderResource},
                {r.FXAA, Layout::ColorAttachment, Access::Write, Load::Clear}});
            else graph.AddPass("ToneMapping", {
                {r.HDR, Layout::ShaderResource},
                {r.FXAA, Layout::ColorAttachment, Access::Write, Load::Clear}});
        }
        if (r.UseFXAA) graph.AddPass("FXAA", {
            {r.FXAA, Layout::ShaderResource}, {r.Output, Layout::ColorAttachment, Access::Write, Load::Clear}});
    }

    bool PostProcessPass::Record(RHISceneRenderer& renderer, const PostProcessRecordInputs& i,
                                 const PassRecordOps& ops) {
        const auto& settings = i.Settings;
        const auto& viewTargets = i.Targets;
        const auto& temporalSelection = i.TemporalSelection;
        const auto& viewProjZO = i.ViewProjectionZO;
        auto* sceneTargetTexture = i.SceneTarget;
        auto* sceneTargetRTV = i.SceneTargetRTV;
        auto* colorTarget = i.ColorTarget;
        auto* colorRTV = i.ColorRTV;
        const auto sceneTargetFormat = i.SceneTargetFormat;
        const auto width = i.Width, height = i.Height;
        const auto jitterX = i.JitterX, jitterY = i.JitterY;
        const bool useBloom = i.UseBloom, useAutoExposure = i.UseAutoExposure;
        const bool useTAA = i.UseTAA, useToneMap = i.UseToneMap, useFXAA = i.UseFXAA;
        auto* historyRead = i.HistoryRead;
        auto* historyWrite = i.HistoryWrite;
        auto& dispatchCount = i.DispatchCount;
        const auto& beginPass = ops.BeginPass;
        const auto& passUniform = ops.Uniform;
        auto bindDescriptorSet = [&](uint32_t index, nri::DescriptorSet* set,
                                     nri::BindPoint point = nri::BindPoint::GRAPHICS) {
            return ops.BindDescriptor(index, set, point);
        };
        const auto& beginRendering = ops.BeginRendering;
        const auto& endRendering = ops.EndRendering;
            if (useBloom && renderer.m_OpaqueSceneColor && renderer.m_OpaqueSceneColor->IsValid()) {
                if (!beginPass("BloomCopy")) return false;
                renderer.Commands().CopyTexture(*renderer.m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                uint32_t bloomPassIndex = 0;
                RHIBloomConstants lastBloomConstants{};
                auto dispatchBloom = [&](std::string_view passName, RHITexture* source, RHITexture* destination, float mode, uint32_t level) -> bool {
                    if (!source || !destination || !source->IsValid() || !destination->IsValid()) { AXLE_CORE_ERROR("Bloom pass {0}: invalid source/destination", passName); return false; }
                    if (!beginPass(passName)) return false;
                    const float dstWidth = static_cast<float>(destination->GetWidth());
                    const float dstHeight = static_cast<float>(destination->GetHeight());
                    RHIBloomConstants bloomConstants{};
                    bloomConstants.ScreenSize = glm::vec4(dstWidth, dstHeight, 1.0f / dstWidth, 1.0f / dstHeight);
                    bloomConstants.BloomParams = glm::vec4(settings.BloomThreshold, settings.BloomIntensity, settings.BloomKnee, mode);
                    bloomConstants.BloomTint = glm::vec4(settings.BloomTint, settings.BloomRadius * std::pow(1.35f, static_cast<float>(level)));
                    if (!passUniform().UpdateData(&bloomConstants, sizeof(RHIBloomConstants))) { AXLE_CORE_ERROR("Bloom pass {0}: constant update failed", passName); return false; }
                    lastBloomConstants = bloomConstants;
                    if (bloomPassIndex >= renderer.m_BloomComputeTextureSets.size()) return false;
                    auto* textureSet = renderer.m_BloomComputeTextureSets[bloomPassIndex];
                    auto* storageSet = renderer.m_BloomComputeStorageSets[bloomPassIndex++];
                    if (!renderer.m_BloomDescriptorPool.UpdateTexture(textureSet, 0, 0, source->GetSRV()) ||
                        !renderer.m_BloomDescriptorPool.UpdateStorageTexture(storageSet, 0, 0, destination->GetUAV())) { AXLE_CORE_ERROR("Bloom pass {0}: descriptor update failed", passName); return false; }
                    if (renderer.m_BloomSampler.IsValid() &&
                        !renderer.m_BloomDescriptorPool.UpdateSampler(textureSet, 1, 0, renderer.m_BloomSampler.GetNative())) return false;
                    renderer.Commands().SetPipeline(renderer.m_BloomComputePipeline);
                    renderer.Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_BloomComputePipelineLayout);
                    renderer.Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                    renderer.m_BloomDescriptorPool.Bind(renderer.Commands().GetNative());
                     if (!bindDescriptorSet(0, textureSet, nri::BindPoint::COMPUTE) ||
                         !bindDescriptorSet(1, storageSet, nri::BindPoint::COMPUTE)) return false;
                    renderer.Commands().Dispatch((destination->GetWidth() + 7) / 8, (destination->GetHeight() + 7) / 8, 1); ++dispatchCount;
                    return true;
                };
                for (uint32_t level = 0; level < 6; ++level) {
                    auto* source = level == 0 ? renderer.m_OpaqueSceneColor : viewTargets->GetBloom(level - 1);
                    if (!dispatchBloom("BloomDown" + std::to_string(level), source, viewTargets->GetBloom(level), 0.0f, level)) return false;
                }
                for (int level = 4; level >= 0; --level) {
                    if (!dispatchBloom("BloomUp" + std::to_string(level), viewTargets->GetBloom(level + 1), viewTargets->GetBloom(level), 1.0f, static_cast<uint32_t>(level))) return false;
                }

                if (!beginPass("BloomApply")) return false;
                if (!passUniform().UpdateData(&lastBloomConstants, sizeof(lastBloomConstants))) return false;
                if (!renderer.m_BloomDescriptorPool.UpdateTexture(renderer.m_BloomApplyDescriptorSet, 0, 0, viewTargets->GetBloom(0)->GetSRV())) return false;
                if (!renderer.m_BloomDescriptorPool.UpdateTexture(renderer.m_BloomApplyDescriptorSet, 1, 0, renderer.m_OpaqueSceneColor->GetSRV())) return false;
                if (renderer.m_BloomSampler.IsValid()) {
                    if (!renderer.m_BloomDescriptorPool.UpdateSampler(renderer.m_BloomApplyDescriptorSet, 2, 0, renderer.m_BloomSampler.GetNative())) return false;
                }

                nri::Pipeline* bloomPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && renderer.m_BloomApplyPipelineHDR)
                    ? renderer.m_BloomApplyPipelineHDR : renderer.m_BloomApplyPipelineSDR;
                if (!bloomPipeline) return false;

                nri::AttachmentDesc bloomColorAttachment = {};
                bloomColorAttachment.descriptor = sceneTargetRTV;
                bloomColorAttachment.loadOp = nri::LoadOp::CLEAR;
                bloomColorAttachment.storeOp = nri::StoreOp::STORE;
                bloomColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

                nri::RenderingDesc bloomRenderingDesc = {};
                bloomRenderingDesc.colors = &bloomColorAttachment;
                bloomRenderingDesc.colorNum = 1;

                beginRendering(bloomRenderingDesc);
                nri::Viewport bloomVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
                renderer.Commands().SetViewport(bloomVp);
                nri::Rect bloomScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
                renderer.Commands().SetScissor(bloomScissor);

                renderer.Commands().SetPipeline(bloomPipeline);
                renderer.Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_BloomApplyPipelineLayout);
                renderer.Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
                renderer.m_BloomDescriptorPool.Bind(renderer.Commands().GetNative());
                 if (!bindDescriptorSet(0, renderer.m_BloomApplyDescriptorSet)) return false;
                renderer.Commands().Draw(3, 1, 0, 0);
                endRendering();
            }

            // 7c.5. Histogram-based eye adaptation on the linear HDR scene.
            if (useAutoExposure) {
                if (!useToneMap) {
                    if (!beginPass("AutoExposureCopy")) return false;
                    renderer.Commands().CopyTexture(*renderer.m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                }
                if (!beginPass("AutoExposure")) return false;
                const uint32_t exposureWidth = useToneMap && renderer.m_IntermediateHDRTarget ? renderer.m_IntermediateHDRTarget->GetWidth() : width;
                const uint32_t exposureHeight = useToneMap && renderer.m_IntermediateHDRTarget ? renderer.m_IntermediateHDRTarget->GetHeight() : height;
                RHIExposureConstants exposureConstants{};
                exposureConstants.ScreenSize = glm::vec4(static_cast<float>(exposureWidth), static_cast<float>(exposureHeight),
                    1.0f / static_cast<float>(exposureWidth), 1.0f / static_cast<float>(exposureHeight));
                exposureConstants.ExposureParams = glm::vec4(-10.0f, 6.0f, settings.AutoExposureSpeed, 1.0f / 60.0f);
                exposureConstants.ExposureLimits = glm::vec4(settings.AutoExposureMin, settings.AutoExposureMax, settings.AutoExposureKeyValue, 0.0f);
                if (!passUniform().UpdateData(&exposureConstants, sizeof(RHIExposureConstants))) return false;
                RHITexture* exposureSource = useToneMap && renderer.m_IntermediateHDRTarget ? renderer.m_IntermediateHDRTarget : renderer.m_OpaqueSceneColor;
                auto* exposureTarget = viewTargets->GetExposure();
                if (!exposureSource || !exposureTarget || !renderer.m_AutoExposureDescriptorPool.UpdateTexture(renderer.m_AutoExposureTextureSet, 0, 0, exposureSource->GetSRV()) ||
                    !renderer.m_AutoExposureDescriptorPool.UpdateSampler(renderer.m_AutoExposureTextureSet, 1, 0, renderer.m_AutoExposureSampler.GetNative()) ||
                    !renderer.m_AutoExposureDescriptorPool.UpdateStorageTexture(renderer.m_AutoExposureStorageSet, 0, 0, exposureTarget->GetUAV())) return false;
                renderer.Commands().SetPipeline(renderer.m_AutoExposurePipeline);
                renderer.Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_AutoExposurePipelineLayout);
                renderer.Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                renderer.m_AutoExposureDescriptorPool.Bind(renderer.Commands().GetNative());
                 if (!bindDescriptorSet(0, renderer.m_AutoExposureTextureSet, nri::BindPoint::COMPUTE) ||
                     !bindDescriptorSet(1, renderer.m_AutoExposureStorageSet, nri::BindPoint::COMPUTE)) return false;
                renderer.Commands().Dispatch(1, 1, 1); ++dispatchCount;
            }

            // 7d. Temporal Anti-Aliasing (TAA) Pass
            if (useTAA && historyRead && historyWrite) {
                if (!beginPass("TAAResolve")) return false;
                RHITAAConstants taaConstants{};
                taaConstants.InvViewProj = glm::inverse(viewProjZO);
                taaConstants.PrevViewProj = temporalSelection.HistoryValid ? renderer.m_ViewContext.Temporal().GetPreviousViewProjection() : viewProjZO;
                taaConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                                                    1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
                taaConstants.TAAParams = glm::vec4(jitterX, jitterY, settings.TAAFeedbackMin, settings.TAAFeedbackMax);
                taaConstants.Reserved[0].x = temporalSelection.HistoryValid ? 1.0f : 0.0f;
                if (!passUniform().UpdateData(&taaConstants, sizeof(RHITAAConstants))) return false;

                nri::Descriptor* currentSRV = renderer.m_IntermediateHDRTarget ? renderer.m_IntermediateHDRTarget->GetSRV() : renderer.m_OpaqueSceneColor->GetSRV();
                nri::Descriptor* depthSRV = renderer.m_OpaqueSceneDepth->GetSRV();
                if (!renderer.m_TAADescriptorPool.UpdateTexture(renderer.m_TAADescriptorSet, 0, 0, currentSRV) ||
                    !renderer.m_TAADescriptorPool.UpdateTexture(renderer.m_TAADescriptorSet, 1, 0, depthSRV) ||
                    !renderer.m_TAADescriptorPool.UpdateTexture(renderer.m_TAADescriptorSet, 2, 0, historyRead->GetSRV())) return false;
                if (renderer.m_TAALinearSampler.IsValid() && renderer.m_TAAPointSampler.IsValid()) {
                    if (!renderer.m_TAADescriptorPool.UpdateSampler(renderer.m_TAADescriptorSet, 3, 0, renderer.m_TAALinearSampler.GetNative())) return false;
                    if (!renderer.m_TAADescriptorPool.UpdateSampler(renderer.m_TAADescriptorSet, 4, 0, renderer.m_TAAPointSampler.GetNative())) return false;
                }

                nri::Pipeline* taaPipeline = (historyWrite->GetFormat() == RHI::RHITextureFormat::RGBA16_FLOAT && renderer.m_TAAPipelineHDR)
                    ? renderer.m_TAAPipelineHDR : renderer.m_TAAPipelineSDR;
                if (!taaPipeline) return false;

                nri::AttachmentDesc taaColorAttachment = {};
                taaColorAttachment.descriptor = historyWrite->GetRTV();
                taaColorAttachment.loadOp = nri::LoadOp::CLEAR;
                taaColorAttachment.storeOp = nri::StoreOp::STORE;
                taaColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

                nri::RenderingDesc taaRenderingDesc = {};
                taaRenderingDesc.colors = &taaColorAttachment;
                taaRenderingDesc.colorNum = 1;

                beginRendering(taaRenderingDesc);
                nri::Viewport taaVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
                renderer.Commands().SetViewport(taaVp);
                nri::Rect taaScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
                renderer.Commands().SetScissor(taaScissor);

                renderer.Commands().SetPipeline(taaPipeline);
                renderer.Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_TAAPipelineLayout);
                renderer.Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
                renderer.m_TAADescriptorPool.Bind(renderer.Commands().GetNative());
                 if (!bindDescriptorSet(0, renderer.m_TAADescriptorSet)) return false;
                renderer.Commands().Draw(3, 1, 0, 0);
                endRendering();

                if (!beginPass("TAACopy")) return false;
                renderer.Commands().CopyTexture(*sceneTargetTexture, nullptr, *historyWrite->GetNative(), nullptr);
            }

            // 8. Post-Processing Tone Mapping Pass
            if (useToneMap && renderer.m_IntermediateHDRTarget && renderer.m_IntermediateHDRTarget->IsValid()) {
                if (!beginPass("ToneMapping")) return false;
                RHIToneMappingConstants toneConstants{};
                // The compute pass owns auto exposure. Tone mapping samples its
                // published 1x1 texture; manual exposure remains the fallback.
                toneConstants.Params = glm::vec4(settings.Exposure, settings.Gamma, static_cast<float>(settings.ToneMappingMode), settings.WhitePoint);
                toneConstants.ColorGrading = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
                toneConstants.Reserved[0].x = useAutoExposure ? 1.0f : 0.0f;

                nri::Texture* toneOutTexture = colorTarget;
                nri::Descriptor* toneOutRTV = colorRTV;
                if (useFXAA) {
                    toneOutTexture = renderer.m_IntermediateFXAATarget->GetNative();
                    toneOutRTV = renderer.m_IntermediateFXAATarget->GetRTV();
                }

                if (!renderer.RenderToneMapping(renderer.Commands(),
                                  renderer.m_IntermediateHDRTarget->GetSRV(),
                                  toneOutTexture,
                                  toneOutRTV,
                                  width, height,
                                  renderer.m_CurrentColorFormat,
                                  toneConstants, &passUniform(),
                                  useAutoExposure ? viewTargets->GetExposure()->GetSRV() : nullptr)) return false;
            }

            // 9. Post-Processing FXAA Pass
            if (useFXAA && renderer.m_IntermediateFXAATarget && renderer.m_IntermediateFXAATarget->IsValid()) {
                if (!beginPass("FXAA")) return false;
                RHIFXAAConstants fxaaConstants{};
                fxaaConstants.ScreenSize = glm::vec4(
                    static_cast<float>(width),
                    static_cast<float>(height),
                    1.0f / static_cast<float>(width),
                    1.0f / static_cast<float>(height)
                );
                fxaaConstants.FXAAParams = glm::vec4(settings.FXAASubpixQuality, settings.FXAAEdgeThreshold, settings.FXAAEdgeThresholdMin, 0.0f);

                if (!renderer.RenderFXAA(renderer.Commands(),
                           renderer.m_IntermediateFXAATarget->GetSRV(),
                           colorTarget,
                           colorRTV,
                           width, height,
                           renderer.m_CurrentColorFormat,
                           fxaaConstants, nullptr, &passUniform())) return false;
            }

        return renderer.Commands().IsRecording();
    }

}
