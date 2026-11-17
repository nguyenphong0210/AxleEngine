#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <cstdint>
#include <memory>

namespace Axle { class RHISceneRenderer; }

namespace Axle::Passes {

    struct ReflectionCaptureTargets {
        uint32_t Resolution = 0;
        std::shared_ptr<RHI::RHITexture> FaceColor;
        std::shared_ptr<RHI::RHITexture> OrientedColor;
        std::shared_ptr<RHI::RHITexture> FaceDepth;
        std::shared_ptr<RHI::RHIViewTargets> ViewTargets;

        bool IsValid() const;
        bool Configure(uint32_t resolution);
        void Reset();
    };

    struct ReflectionCaptureResources {
        RenderGraph::Resource SceneColor;
        RenderGraph::Resource OrientedColor;
        RenderGraph::Resource Cubemap;
        bool FlipHorizontal = true;
    };

    struct ReflectionCaptureInputs {
        RHI::RHITexture* SourceFace = nullptr;
        RHI::RHITexture* OrientedFace = nullptr;
        RHI::RHITexture* Cubemap = nullptr;
        uint32_t FaceIndex = 0;
        uint32_t Resolution = 0;
        bool FlipHorizontal = true;
        uint32_t& DispatchCount;
    };

    class ReflectionCapturePass final {
    public:
        static void DeclareOrientAndCopy(RenderGraph& graph, const ReflectionCaptureResources& resources);
        static bool RecordOrientAndCopy(RHISceneRenderer& renderer,
                                        const ReflectionCaptureInputs& inputs,
                                        const PassRecordOps& ops);
    };

} // namespace Axle::Passes
