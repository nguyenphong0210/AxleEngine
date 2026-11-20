#pragma once

#include "PassRecordOps.hpp"
#include <Axle/RenderGraph.hpp>
#include <Axle/TemporalService.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <array>
#include <memory>

namespace Axle {
    class RHISceneRenderer;
    struct SceneRenderSettings;
}

namespace Axle::Passes {
    struct PostProcessResources {
        RenderGraph::Resource SceneColor, SceneDepth, Output, HDR, FXAA;
        RenderGraph::Resource BloomInput, ExposureInput, ExposureHistogram, ExposureOutput;
        RenderGraph::Resource TAAColor, TAADepth, TAAVelocity, TAAHistoryRead, TAAHistoryWrite;
        std::array<RenderGraph::Resource, 6> Bloom;
        bool UseBloom = false, UseAutoExposure = false, UseTAA = false, UseVelocity = false;
        bool UseToneMap = false, UseFXAA = false;
    };

    struct PostProcessRecordInputs {
        const SceneRenderSettings& Settings;
        const std::shared_ptr<RHI::RHIViewTargets>& Targets;
        const TemporalHistorySelection& TemporalSelection;
        const glm::mat4& ViewProjectionZO;
        const glm::mat4& UnjitteredViewProjectionZO;
        nri::Texture* SceneTarget;
        nri::Descriptor* SceneTargetRTV;
        nri::Texture* ColorTarget;
        nri::Descriptor* ColorRTV;
        nri::Format SceneTargetFormat;
        uint32_t Width, Height;
        float JitterX, JitterY;
        float DeltaTime;
        bool ResetAutoExposure;
        bool UseBloom, UseAutoExposure, UseTAA, UseVelocity, UseToneMap, UseFXAA;
        RHI::RHITexture* HistoryRead;
        RHI::RHITexture* HistoryWrite;
        RHI::RHITexture* VelocityTexture;
        RHI::RHITexture* ExposureHistogram;
        uint32_t& DispatchCount;
    };

    class PostProcessPass final {
    public:
        static void Declare(RenderGraph& graph, const PostProcessResources& resources);
        static bool Record(RHISceneRenderer& renderer, const PostProcessRecordInputs& inputs,
                           const PassRecordOps& ops);
    };
}
