#pragma once

#include "PassRecordOps.hpp"
#include <Axle/FroxelShaderConstants.hpp>
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
        const FroxelFogContract::FroxelShaderConstants& Constants;
        RHI::RHIViewTargets* Targets;
        RHI::RHITexture* HistoryRead;
        RHI::RHITexture* HistoryWrite;
        nri::Texture* SceneTarget;
        nri::Texture* SceneDepthTarget;
        nri::Descriptor* SceneTargetRTV;
        nri::Format SceneTargetFormat;
        uint32_t Width, Height;
        uint32_t& DispatchCount;
    };

    class FroxelFogPass final {
    public:
        static FroxelFogContract::FroxelShaderConstants BuildConstants(
            const FroxelFogContract::FroxelMediaState& media,
            const PerspectiveCamera& camera,
            const glm::mat4& projectionZO,
            const glm::mat4& viewProjZO,
            const RHI::RHIViewTargets* targets,
            const FroxelHistoryFrame& previous,
            bool hasHistoryRead,
            float historyWeight,
            uint32_t width,
            uint32_t height);
        static void Declare(RenderGraph& graph, const FroxelFogResources& resources);
        static bool Record(RHISceneRenderer& renderer, const FroxelFogRecordInputs& inputs,
                           const PassRecordOps& ops);
    };
}
