#pragma once

#include <Axle/RenderGraph.hpp>

namespace Axle::Passes {
    struct FroxelFogResources {
        RenderGraph::Resource SceneColor, Input, SceneDepth, Depth, Storage;
    };

    class FroxelFogPass final {
    public:
        static void Declare(RenderGraph& graph, const FroxelFogResources& resources);
    };
}
