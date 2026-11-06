#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <entt/entt.hpp>

namespace Axle {

    class SceneData;
    class PhysicsScene;

    class SceneAudioBridge {
    public:
        // Initialize audio sources and trigger PlayOnAwake
        static void OnRuntimeStart(SceneData& scene);

        // Update listener and 3D spatial audio positions/orientations
        static void OnUpdate(SceneData& scene, Timestep ts, PhysicsScene* physicsScene = nullptr);

        // Stop all active audio sources
        static void OnRuntimeStop(SceneData& scene);

        // Clean up entity audio on destruction
        static void TeardownEntityAudio(SceneData& scene, entt::entity entityID);
    };

}
