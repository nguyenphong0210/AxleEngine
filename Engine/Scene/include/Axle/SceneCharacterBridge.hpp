#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <entt/entt.hpp>

namespace Axle {

    class SceneData;
    class PhysicsScene;

    class SceneCharacterBridge {
    public:
        // Initialize character controllers, character VFX, and particle emitters
        static void OnRuntimeStart(SceneData& scene, PhysicsScene* physicsScene);

        // Update controllers, animator sync, VFX, and particle emitters
        static void OnUpdate(SceneData& scene, Timestep ts, PhysicsScene* physicsScene = nullptr);

        // Stop controllers, VFX, and reset particle emitters
        static void OnRuntimeStop(SceneData& scene);

        // Clean up entity character controller and VFX on destruction
        static void TeardownEntityCharacter(SceneData& scene, entt::entity entityID);
    };

}
