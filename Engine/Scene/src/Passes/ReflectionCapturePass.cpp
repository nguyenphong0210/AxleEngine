#include "ReflectionCapturePass.hpp"
#include "../RHISceneRendererPasses.hpp"
#include <algorithm>

namespace Axle::Passes {

    bool ReflectionCaptureTargets::IsValid() const {
        return Resolution > 0 &&
               FaceColor && FaceColor->IsValid() &&
               OrientedColor && OrientedColor->IsValid() &&
               FaceDepth && FaceDepth->IsValid() &&
               ViewTargets && ViewTargets->IsValid();
    }

    bool ReflectionCaptureTargets::Configure(uint32_t resolution) {
        if (resolution == 0) {
            return false;
        }
        if (IsValid() && Resolution == resolution) {
            return true;
        }

        auto faceColor = std::make_shared<RHI::RHITexture>();
        auto orientedColor = std::make_shared<RHI::RHITexture>();
        auto faceDepth = std::make_shared<RHI::RHITexture>();
        auto viewTargets = std::make_shared<RHI::RHIViewTargets>();

        if (!faceColor->Init(RHI::RHITextureDesc::RenderTarget2D(
                resolution, resolution, RHI::RHITextureFormat::RGBA16_FLOAT, "ReflectionCaptureFaceColor"))) {
            return false;
        }
        if (!orientedColor->Init(RHI::RHITextureDesc::StorageTexture2D(
                resolution, resolution, RHI::RHITextureFormat::RGBA16_FLOAT, "ReflectionCaptureOrientedColor"))) {
            return false;
        }
        if (!faceDepth->Init(RHI::RHITextureDesc::DepthStencil2D(
                resolution, resolution, RHI::RHITextureFormat::D32_FLOAT, "ReflectionCaptureFaceDepth"))) {
            return false;
        }

        RHI::RHIViewTargetDesc targetDesc{};
        targetDesc.Width = resolution;
        targetDesc.Height = resolution;
        targetDesc.ColorFormat = RHI::RHITextureFormat::RGBA16_FLOAT;
        targetDesc.DepthFormat = RHI::RHITextureFormat::D32_FLOAT;
        targetDesc.EnableDepth = true;
        targetDesc.Samples = 1;
        targetDesc.OwnPrimary = false;
        if (!viewTargets->Configure(targetDesc, faceColor.get(), faceDepth.get())) {
            return false;
        }

        Resolution = resolution;
        FaceColor = std::move(faceColor);
        OrientedColor = std::move(orientedColor);
        FaceDepth = std::move(faceDepth);
        ViewTargets = std::move(viewTargets);
        return true;
    }

    void ReflectionCaptureTargets::Reset() {
        ViewTargets.reset();
        FaceDepth.reset();
        OrientedColor.reset();
        FaceColor.reset();
        Resolution = 0;
    }

    void ReflectionCapturePass::DeclareOrientAndCopy(RenderGraph& graph, const ReflectionCaptureResources& r) {
        using Layout = RHI::RHIResourceLayout;
        using Access = RenderGraph::Access;
        if (r.FlipHorizontal) {
            graph.AddPass("CaptureOrient", {
                {r.SceneColor, Layout::ShaderResource},
                {r.OrientedColor, Layout::ShaderResourceStorage, Access::Write}});
            graph.AddPass("CaptureCopy", {
                {r.OrientedColor, Layout::CopySource},
                {r.Cubemap, Layout::CopyDest, Access::Write}});
        } else {
            graph.AddPass("CaptureCopy", {
                {r.SceneColor, Layout::CopySource},
                {r.Cubemap, Layout::CopyDest, Access::Write}});
        }
    }

    bool ReflectionCapturePass::RecordOrientAndCopy(RHISceneRenderer& renderer,
                                                    const ReflectionCaptureInputs& i,
                                                    const PassRecordOps& ops) {
        if (!i.SourceFace || !i.SourceFace->IsValid() ||
            !i.Cubemap || !i.Cubemap->IsValid() ||
            i.Resolution == 0 || i.FaceIndex >= 6) {
            return false;
        }

        auto& commands = renderer.Commands();
        RHI::RHITexture* copySource = i.SourceFace;

        if (i.FlipHorizontal) {
            if (!i.OrientedFace || !i.OrientedFace->IsValid() ||
                !renderer.m_CaptureOrientationPipeline ||
                !renderer.m_CaptureOrientationTextureSet ||
                !renderer.m_CaptureOrientationStorageSet) {
                return false;
            }
            if (!ops.BeginPass("CaptureOrient")) {
                return false;
            }

            RHICubemapCaptureConstants constants{};
            constants.Params = glm::uvec4(i.Resolution, 1u, i.FaceIndex, 0u);
            if (!ops.Uniform().UpdateData(&constants, sizeof(constants))) {
                return false;
            }

            auto& pool = renderer.m_CaptureOrientationDescriptorPool;
            if (!pool.UpdateTexture(renderer.m_CaptureOrientationTextureSet, 0, 0, i.SourceFace->GetSRV()) ||
                !pool.UpdateStorageTexture(renderer.m_CaptureOrientationStorageSet, 0, 0, i.OrientedFace->GetUAV())) {
                return false;
            }

            commands.SetPipeline(renderer.m_CaptureOrientationPipeline);
            commands.SetPipelineLayout(nri::BindPoint::COMPUTE, renderer.m_CaptureOrientationPipelineLayout);
            commands.SetRootDescriptor(0, ops.Uniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
            pool.Bind(commands.GetNative());
            if (!ops.BindDescriptor(0, renderer.m_CaptureOrientationTextureSet, nri::BindPoint::COMPUTE) ||
                !ops.BindDescriptor(1, renderer.m_CaptureOrientationStorageSet, nri::BindPoint::COMPUTE)) {
                return false;
            }
            const uint32_t groups = (i.Resolution + 7u) / 8u;
            commands.Dispatch(groups, groups, 1u);
            ++i.DispatchCount;
            copySource = i.OrientedFace;
        }

        if (!ops.BeginPass("CaptureCopy")) {
            return false;
        }

        const auto dim = static_cast<nri::Dim_t>(i.Resolution);
        nri::TextureRegionDesc srcRegion{};
        srcRegion.x = 0;
        srcRegion.y = 0;
        srcRegion.z = 0;
        srcRegion.width = dim;
        srcRegion.height = dim;
        srcRegion.depth = 1;
        srcRegion.mipOffset = 0;
        srcRegion.layerOffset = 0;
        srcRegion.planes = nri::PlaneBits::COLOR;

        nri::TextureRegionDesc dstRegion{};
        dstRegion.x = 0;
        dstRegion.y = 0;
        dstRegion.z = 0;
        dstRegion.width = dim;
        dstRegion.height = dim;
        dstRegion.depth = 1;
        dstRegion.mipOffset = 0;
        dstRegion.layerOffset = static_cast<nri::Dim_t>(i.FaceIndex);
        dstRegion.planes = nri::PlaneBits::COLOR;

        commands.CopyTexture(*i.Cubemap->GetNative(), &dstRegion, *copySource->GetNative(), &srcRegion);
        return commands.IsRecording();
    }

} // namespace Axle::Passes
