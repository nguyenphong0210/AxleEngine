#pragma once

#include <Axle/RenderGraph.hpp>
#include <array>

namespace Axle::Passes {
    struct ScreenSpaceReflectionResources {
        RenderGraph::Resource SceneColor, Input, SceneDepth, Depth, SceneNormal, Storage;
        std::array<RenderGraph::Resource, 8> HiZ;
    };

    class ScreenSpaceReflectionPass final {
    public:
        static void Declare(RenderGraph& graph, const ScreenSpaceReflectionResources& resources);
    };
}
