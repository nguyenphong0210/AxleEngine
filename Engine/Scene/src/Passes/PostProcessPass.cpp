#include "PostProcessPass.hpp"
#include "../RHISceneRendererPasses.hpp"
#include <Axle/Log.hpp>
#include <Axle/RenderContracts.hpp>
#include <cmath>

namespace Axle::Passes {
    using namespace RHI;
    void PostProcessPass::Declare(RenderGraph& graph, const PostProcessResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        using Load = RenderGraph::Load;
        if (r.UseAutoExposure) {
            if (!r.UseToneMap) graph.AddPass("AutoExposureCopy", {
                {r.SceneColor, Layout::CopySource}, {r.ExposureInput, Layout::CopyDest, Access::Write}});
            graph.AddPass("AutoExposureHistogramClear", {
                {r.ExposureInput, Layout::ShaderResource},
                {r.ExposureHistogram, Layout::ShaderResourceStorage, Access::Write}});
            graph.AddPass("AutoExposureHistogram", {
                {r.ExposureInput, Layout::ShaderResource},
                {r.ExposureHistogram, Layout::ShaderResourceStorage, Access::Write}});
            graph.AddPass("AutoExposure", {
                {r.ExposureHistogram, Layout::ShaderResource},
                {r.ExposureOutput, Layout::ShaderResourceStorage, Access::Write}});
        }
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
                    {r.Bloom[level], Layout::ColorAttachment, Access::ReadWrite, Load::Load}});
            }
            graph.AddPass("BloomApply", {
                {r.Bloom[0], Layout::ShaderResource}, {r.BloomInput, Layout::ShaderResource},
                {r.SceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
        }
        if (r.UseTAA) {
            if (!r.UseToneMap) {
                graph.AddPass("TAASourceCopy", {
                    {r.SceneColor, Layout::CopySource},
                    {r.TAAColor, Layout::CopyDest, Access::Write}});
            }
            graph.AddPass("TAADepthCopy", {
                {r.SceneDepth, Layout::CopySource},
                {r.TAADepth, Layout::CopyDest, Access::Write}});
            const auto source = r.UseToneMap ? r.HDR : r.TAAColor;
            graph.AddPass("TAAResolve", {
                {source, Layout::ShaderResource}, {r.TAADepth, Layout::ShaderResource},
                {r.TAAHistoryRead, Layout::ShaderResource},
                {r.TAAHistoryWrite, Layout::ColorAttachment, Access::Write, Load::Clear}});
            graph.AddPass("TAACopy", {
                {r.TAAHistoryWrite, Layout::CopySource},
                {r.UseToneMap ? r.HDR : r.SceneColor, Layout::CopyDest, Access::Write}});
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
            // 7b. Histogram-based eye adaptation runs on clean linear HDR BEFORE Bloom/UI
            // so Bloom halos and HUD/Editor overlays never bias luminance metering.
            if (useAutoExposure) {
                if (!useToneMap) {
                    if (!beginPass("AutoExposureCopy")) return false;
                    renderer.Commands().CopyTexture(*renderer.m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                }
                RHITexture* exposureSource = useToneMap && renderer.m_IntermediateHDRTarget ? renderer.m_IntermediateHDRTarget : renderer.m_OpaqueSceneColor;
                auto* exposureHistogram = i.ExposureHistogram;
                auto* exposureTarget = viewTargets->GetExposure();
                if (!exposureSource || !exposureHistogram || !exposureHistogram->IsValid() || !exposureTarget) return false;

                const uint32_t exposureWidth = useToneMap && renderer.m_IntermediateHDRTarget ? renderer.m_IntermediateHDRTarget->GetWidth() : width;
                const uint32_t exposureHeight = useToneMap && renderer.m_IntermediateHDRTarget ? renderer.m_IntermediateHDRTarget->GetHeight() : height;
                RHIExposureConstants exposureConstants{};
                exposureConstants.ScreenSize = glm::vec4(
                    static_cast<float>(exposureWidth),
                    static_cast<float>(exposureHeight),
                    1.0f / static_cast<float>(std::max(exposureWidth, 1u)),
                    1.0f / static_cast<float>(std::max(exposureHeight, 1u)));
                exposureConstants.ExposureParams = glm::vec4(
                    AutoExposureContract::MinLogLuminance,
                    AutoExposureContract::MaxLogLuminance,
                    std::max(settings.AutoExposureSpeed, 0.0f),
                    (std::isfinite(i.DeltaTime) && i.DeltaTime > 0.0f) ? i.DeltaTime : 0.0f);
                exposureConstants.ExposureLimits = glm::vec4(
                    settings.AutoExposureMin,
                    settings.AutoExposureMax,
                    settings.AutoExposureKeyValue,
                    i.ResetAutoExposure ? 1.0f : 0.0f);
                exposureConstants.ExposureFallback = glm::vec4(settings.Exposure, 0.0f, 0.0f, 0.0f);

                if (!beginPass("AutoExposureHistogramClear")) return false;
                if (!passUniform().UpdateData(&exposureConstants, sizeof(RHIExposureConstants))) return false;
                if (!renderer.m_AutoExposureDescriptorPool.UpdateTexture(renderer.m_AutoExposureHistTextureSet, 0, 0, exposureSource->GetSRV()) ||
                    !renderer.m_AutoExposureDescriptorPool.UpdateSampler(renderer.m_AutoExposureHistTextureSet, 1, 0, renderer.m_AutoExposureSampler.GetNative()) ||
                    !renderer.m_AutoExposureDescriptorPool.UpdateStorageTexture(renderer.m_AutoExposureHistStorageSet, 0, 0, exposureHistogram->GetUAV())) return false;
                renderer.Commands().SetPipeline(renderer.m_AutoExposureClearPipeline);
                renderer.Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_AutoExposureHistPipelineLayout);
                renderer.Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                renderer.m_AutoExposureDescriptorPool.Bind(renderer.Commands().GetNative());
                if (!bindDescriptorSet(0, renderer.m_AutoExposureHistTextureSet, nri::BindPoint::COMPUTE) ||
                    !bindDescriptorSet(1, renderer.m_AutoExposureHistStorageSet, nri::BindPoint::COMPUTE)) return false;
                renderer.Commands().Dispatch(1, 1, 1); ++dispatchCount;

                if (!beginPass("AutoExposureHistogram")) return false;
                if (!passUniform().UpdateData(&exposureConstants, sizeof(RHIExposureConstants))) return false;
                renderer.Commands().SetPipeline(renderer.m_AutoExposureHistogramPipeline);
                renderer.Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_AutoExposureHistPipelineLayout);
                renderer.Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                renderer.m_AutoExposureDescriptorPool.Bind(renderer.Commands().GetNative());
                if (!bindDescriptorSet(0, renderer.m_AutoExposureHistTextureSet, nri::BindPoint::COMPUTE) ||
                    !bindDescriptorSet(1, renderer.m_AutoExposureHistStorageSet, nri::BindPoint::COMPUTE)) return false;
                renderer.Commands().Dispatch((exposureWidth + 15u) / 16u, (exposureHeight + 15u) / 16u, 1u); ++dispatchCount;

                if (!beginPass("AutoExposure")) return false;
                if (!passUniform().UpdateData(&exposureConstants, sizeof(RHIExposureConstants))) return false;
                if (!renderer.m_AutoExposureDescriptorPool.UpdateTexture(renderer.m_AutoExposureTextureSet, 0, 0, exposureHistogram->GetSRV()) ||
                    !renderer.m_AutoExposureDescriptorPool.UpdateStorageTexture(renderer.m_AutoExposureStorageSet, 0, 0, exposureTarget->GetUAV())) return false;
                renderer.Commands().SetPipeline(renderer.m_AutoExposurePipeline);
                renderer.Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_AutoExposurePipelineLayout);
                renderer.Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                renderer.m_AutoExposureDescriptorPool.Bind(renderer.Commands().GetNative());
                if (!bindDescriptorSet(0, renderer.m_AutoExposureTextureSet, nri::BindPoint::COMPUTE) ||
                    !bindDescriptorSet(1, renderer.m_AutoExposureStorageSet, nri::BindPoint::COMPUTE)) return false;
                renderer.Commands().Dispatch(1, 1, 1); ++dispatchCount;
            }

            if (useBloom && renderer.m_OpaqueSceneColor && renderer.m_OpaqueSceneColor->IsValid()) {
                if (!beginPass("BloomCopy")) return false;
                renderer.Commands().CopyTexture(*renderer.m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                uint32_t bloomPassIndex = 0;
                auto makeBloomConstants = [&](uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH, float mode) {
                    RHIBloomConstants c{};
                    const float safeDstW = static_cast<float>(std::max(dstW, 1u));
                    const float safeDstH = static_cast<float>(std::max(dstH, 1u));
                    const float safeSrcW = static_cast<float>(std::max(srcW, 1u));
                    const float safeSrcH = static_cast<float>(std::max(srcH, 1u));
                    c.ScreenSize = glm::vec4(safeDstW, safeDstH, 1.0f / safeDstW, 1.0f / safeDstH);
                    c.BloomParams = glm::vec4(
                        std::max(settings.BloomThreshold, 0.0f),
                        std::max(settings.BloomIntensity, 0.0f),
                        std::max(settings.BloomKnee, 0.0f),
                        mode);
                    c.BloomTint = glm::vec4(
                        glm::max(settings.BloomTint, glm::vec3(0.0f)),
                        std::max(settings.BloomRadius, 0.25f));
                    c.SourceSize = glm::vec4(safeSrcW, safeSrcH, 1.0f / safeSrcW, 1.0f / safeSrcH);
                    return c;
                };
                auto dispatchBloomDown = [&](std::string_view passName, RHITexture* source, RHITexture* destination, uint32_t level) -> bool {
                    if (!source || !destination || !source->IsValid() || !destination->IsValid()) { AXLE_CORE_ERROR("Bloom pass {0}: invalid source/destination", passName); return false; }
                    if (!beginPass(passName)) return false;
                    const RHIBloomConstants bloomConstants = makeBloomConstants(
                        source->GetWidth(), source->GetHeight(),
                        destination->GetWidth(), destination->GetHeight(),
                        level == 0u ? 0.0f : 1.0f);
                    if (!passUniform().UpdateData(&bloomConstants, sizeof(RHIBloomConstants))) { AXLE_CORE_ERROR("Bloom pass {0}: constant update failed", passName); return false; }
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
                for (uint32_t level = 0; level < BloomContract::PyramidLevels; ++level) {
                    auto* source = level == 0 ? renderer.m_OpaqueSceneColor : viewTargets->GetBloom(level - 1);
                    if (!dispatchBloomDown("BloomDown" + std::to_string(level), source, viewTargets->GetBloom(level), level)) return false;
                }
                for (int level = static_cast<int>(BloomContract::PyramidLevels) - 2; level >= 0; --level) {
                    const uint32_t lvl = static_cast<uint32_t>(level);
                    RHITexture* source = viewTargets->GetBloom(lvl + 1u);
                    RHITexture* destination = viewTargets->GetBloom(lvl);
                    if (!source || !destination || !source->IsValid() || !destination->IsValid() || !destination->GetRTV()) return false;
                    if (!beginPass("BloomUp" + std::to_string(level))) return false;
                    const RHIBloomConstants upConstants = makeBloomConstants(
                        source->GetWidth(), source->GetHeight(),
                        destination->GetWidth(), destination->GetHeight(),
                        2.0f);
                    if (!passUniform().UpdateData(&upConstants, sizeof(RHIBloomConstants))) return false;
                    if (lvl >= renderer.m_BloomUpsampleDescriptorSets.size()) return false;
                    auto* upsampleSet = renderer.m_BloomUpsampleDescriptorSets[lvl];
                    if (!renderer.m_BloomDescriptorPool.UpdateTexture(upsampleSet, 0, 0, source->GetSRV()) ||
                        !renderer.m_BloomDescriptorPool.UpdateTexture(upsampleSet, 1, 0, source->GetSRV())) return false;
                    if (renderer.m_BloomSampler.IsValid() &&
                        !renderer.m_BloomDescriptorPool.UpdateSampler(upsampleSet, 2, 0, renderer.m_BloomSampler.GetNative())) return false;
                    nri::Pipeline* upsamplePipeline = (destination->GetFormat() == RHI::RHITextureFormat::RGBA16_FLOAT && renderer.m_BloomUpsamplePipelineHDR)
                        ? renderer.m_BloomUpsamplePipelineHDR : renderer.m_BloomUpsamplePipelineSDR;
                    if (!upsamplePipeline) return false;

                    nri::AttachmentDesc upAttachment = {};
                    upAttachment.descriptor = destination->GetRTV();
                    upAttachment.loadOp = nri::LoadOp::LOAD;
                    upAttachment.storeOp = nri::StoreOp::STORE;
                    nri::RenderingDesc upRenderingDesc = {};
                    upRenderingDesc.colors = &upAttachment;
                    upRenderingDesc.colorNum = 1;

                    beginRendering(upRenderingDesc);
                    const uint32_t dstW = destination->GetWidth();
                    const uint32_t dstH = destination->GetHeight();
                    nri::Viewport upVp = { 0.0f, 0.0f, static_cast<float>(dstW), static_cast<float>(dstH), 0.0f, 1.0f };
                    renderer.Commands().SetViewport(upVp);
                    nri::Rect upScissor = { 0, 0, static_cast<nri::Dim_t>(dstW), static_cast<nri::Dim_t>(dstH) };
                    renderer.Commands().SetScissor(upScissor);
                    renderer.Commands().SetPipeline(upsamplePipeline);
                    renderer.Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.m_BloomApplyPipelineLayout);
                    renderer.Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
                    renderer.m_BloomDescriptorPool.Bind(renderer.Commands().GetNative());
                    if (!bindDescriptorSet(0, upsampleSet)) return false;
                    renderer.Commands().Draw(3, 1, 0, 0);
                    endRendering();
                }

                if (!beginPass("BloomApply")) return false;
                auto* bloom0 = viewTargets->GetBloom(0);
                if (!bloom0 || !bloom0->IsValid()) return false;
                const RHIBloomConstants applyConstants = makeBloomConstants(
                    bloom0->GetWidth(), bloom0->GetHeight(), width, height, 3.0f);
                if (!passUniform().UpdateData(&applyConstants, sizeof(applyConstants))) return false;
                if (!renderer.m_BloomDescriptorPool.UpdateTexture(renderer.m_BloomApplyDescriptorSet, 0, 0, bloom0->GetSRV())) return false;
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

            // 7d. Temporal Anti-Aliasing (TAA) Pass
            if (useTAA && historyRead && historyWrite) {
                if (!useToneMap) {
                    if (!beginPass("TAASourceCopy")) return false;
                    renderer.Commands().CopyTexture(*renderer.m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                }
                if (!beginPass("TAADepthCopy")) return false;
                renderer.Commands().CopyTexture(*renderer.m_OpaqueSceneDepth->GetNative(), nullptr, *renderer.m_TAADepthSource, nullptr);

                if (!beginPass("TAAResolve")) return false;
                RHITAAConstants taaConstants{};
                taaConstants.InvViewProj = glm::inverse(viewProjZO);
                taaConstants.PrevViewProj = temporalSelection.HistoryValid ? renderer.ActiveViewContext().Temporal().GetPreviousViewProjection() : viewProjZO;
                taaConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                                                    1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
                taaConstants.TAAParams = glm::vec4(jitterX, jitterY, settings.TAAFeedbackMin, settings.TAAFeedbackMax);
                taaConstants.Reserved[0].x = temporalSelection.HistoryValid ? 1.0f : 0.0f;
                if (!passUniform().UpdateData(&taaConstants, sizeof(RHITAAConstants))) return false;

                nri::Descriptor* currentSRV = (useToneMap && renderer.m_IntermediateHDRTarget) ? renderer.m_IntermediateHDRTarget->GetSRV() : renderer.m_OpaqueSceneColor->GetSRV();
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
