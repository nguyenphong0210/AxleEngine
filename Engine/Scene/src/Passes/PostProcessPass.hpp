#pragma once

#include <Axle/RenderGraph.hpp>
#include <array>

namespace Axle::Passes {
    struct PostProcessResources {
        RenderGraph::Resource SceneColor, SceneDepth, Output, HDR, FXAA;
        RenderGraph::Resource BloomInput, ExposureInput, ExposureOutput;
        RenderGraph::Resource TAAHistoryRead, TAAHistoryWrite;
        std::array<RenderGraph::Resource, 6> Bloom;
        bool UseBloom = false, UseAutoExposure = false, UseTAA = false;
        bool UseToneMap = false, UseFXAA = false;
    };

    class PostProcessPass final {
    public:
        static void Declare(RenderGraph& graph, const PostProcessResources& resources);
    };
}
