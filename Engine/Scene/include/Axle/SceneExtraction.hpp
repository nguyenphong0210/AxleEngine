#pragma once
#include <Axle/RenderWorld.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <unordered_set>

namespace Axle {
    class Scene;
    struct SceneRenderSettings;

    struct SceneExtractionOptions {
        const PerspectiveCamera* CameraOverride = nullptr;
        const std::unordered_set<uint32_t>* SelectedEntities = nullptr;
        bool ResolveResources = true; // Compatibility field; GPU resolution belongs to RenderService.
    };

    bool ResolveRenderWorldResources(RenderWorld& world);
    std::optional<RenderVolumetricFog> ExtractVolumetricFog(const Scene& scene);
    std::optional<RenderDDGIProbeGrid> ExtractDDGIProbeGrid(const Scene& scene);
    bool SceneRequiresSubsurfacePass(const Scene& scene, const SceneRenderSettings& settings, uint8_t sampleCount = 1u);

    // Extracts an immutable RenderWorld snapshot from a Scene.
    // Captures owned material/pose/particle values on the scene owner thread.
    // Does not allocate GPU resources during ECS iteration and does not retain mutable ECS pointers.
    RenderWorld ExtractRenderWorld(Scene& scene, const SceneExtractionOptions& options = {});
    RenderWorld ExtractRenderWorld(const Scene& scene, const SceneExtractionOptions& options = {});
}
