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
        RenderGraph::Resource TAAHistoryRead, TAAHistoryWrite;
        std::array<RenderGraph::Resource, 6> Bloom;
        bool UseBloom = false, UseAutoExposure = false, UseTAA = false;
        bool UseToneMap = false, UseFXAA = false;
    };

    struct PostProcessRecordInputs {
        const SceneRenderSettings& Settings;
        const std::shared_ptr<RHI::RHIViewTargets>& Targets;
        const TemporalHistorySelection& TemporalSelection;
        const glm::mat4& ViewProjectionZO;
        nri::Texture* SceneTarget;
        nri::Descriptor* SceneTargetRTV;
        nri::Texture* ColorTarget;
        nri::Descriptor* ColorRTV;
        nri::Format SceneTargetFormat;
        uint32_t Width, Height;
        float JitterX, JitterY;
        float DeltaTime;
        bool ResetAutoExposure;
        bool UseBloom, UseAutoExposure, UseTAA, UseToneMap, UseFXAA;
        RHI::RHITexture* HistoryRead;
        RHI::RHITexture* HistoryWrite;
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
