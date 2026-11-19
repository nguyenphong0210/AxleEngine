#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <glm/mat4x4.hpp>

namespace Axle {
    class RHISceneRenderer;
    struct FroxelHistoryFrame;
    class PerspectiveCamera;
    namespace FroxelFogContract { struct FroxelMediaState; }
}

namespace Axle::RHI {
    class RHIViewTargets;
    class RHITexture;
}

namespace Axle::Passes {
    struct FroxelFogResources {
        RenderGraph::Resource SceneColor, Input, SceneDepth, Depth, FroxelScattering, FroxelIntegrated;
        RenderGraph::Resource CascadeAtlas{}, LocalShadowAtlas{};
        bool UseCascadeAtlas = false;
        bool UseLocalShadowAtlas = false;
        RenderGraph::Resource HistoryRead{}, HistoryWrite{};
        bool Reproject = false;
        bool HasHistory = false;
    };

    struct FroxelFogRecordInputs {
        const FroxelFogContract::FroxelMediaState& Media;
        const PerspectiveCamera& Camera;
        const glm::mat4& ProjectionZO;
        RHI::RHIViewTargets* Targets;
        RHI::RHITexture* HistoryRead;
        RHI::RHITexture* HistoryWrite;
        const FroxelHistoryFrame& Previous;
        float HistoryWeight;
        nri::Texture* SceneTarget;
        nri::Texture* SceneDepthTarget;
        nri::Descriptor* SceneTargetRTV;
        nri::Format SceneTargetFormat;
        uint32_t Width, Height;
        uint32_t& DispatchCount;
    };

    class FroxelFogPass final {
    public:
        static void Declare(RenderGraph& graph, const FroxelFogResources& resources);
        static bool Record(RHISceneRenderer& renderer, const FroxelFogRecordInputs& inputs,
                           const PassRecordOps& ops);
    };
}
