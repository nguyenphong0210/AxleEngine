#pragma once
#include <Axle/RenderWorld.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <unordered_set>

namespace Axle {
    class Scene;

    struct SceneExtractionOptions {
        const PerspectiveCamera* CameraOverride = nullptr;
        const std::unordered_set<uint32_t>* SelectedEntities = nullptr;
        bool ResolveResources = true;
    };

    // Extracts an immutable RenderWorld snapshot from a Scene.
    // Does not allocate GPU resources during ECS iteration and does not retain mutable ECS pointers.
    RenderWorld ExtractRenderWorld(Scene& scene, const SceneExtractionOptions& options = {});
    RenderWorld ExtractRenderWorld(const Scene& scene, const SceneExtractionOptions& options = {});
}
