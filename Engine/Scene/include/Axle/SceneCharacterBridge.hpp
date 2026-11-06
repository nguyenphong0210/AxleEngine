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

        // Pre-physics fixed update for kinematic character controllers
        static void OnFixedUpdate(SceneData& scene, Timestep fixedDt, PhysicsScene* physicsScene = nullptr);

        // Post-step telemetry sync (velocities, grounding, animator states, character VFX)
        static void OnPostStepSync(SceneData& scene, PhysicsScene* physicsScene = nullptr);

        // Animation and particle VFX updates
        static void UpdateAnimationAndVFX(SceneData& scene, Timestep fixedDt, PhysicsScene* physicsScene = nullptr);

        // Combined update (calls OnFixedUpdate, OnPostStepSync, and UpdateAnimationAndVFX)
        static void OnUpdate(SceneData& scene, Timestep ts, PhysicsScene* physicsScene = nullptr);

        // Stop controllers, VFX, and reset particle emitters
        static void OnRuntimeStop(SceneData& scene);

        // Clean up entity character controller and VFX on destruction
        static void TeardownEntityCharacter(SceneData& scene, entt::entity entityID);
    };

}
