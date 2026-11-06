#pragma once
#include <Axle/Types.hpp>
#include <Axle/PhysicsScene.hpp>
#include <entt/entt.hpp>

namespace Axle {

    class SceneData;
    class Entity;

    class ScenePhysicsBridge {
    public:
        // Synchronize Jolt physics simulation body transforms into TransformComponents
        static void SynchronizeTransforms(SceneData& scene, PhysicsScene* physicsScene);

        // Zero-drift physics restoration for pristine scene reset
        static void RestoreBodies(SceneData& scene, PhysicsScene* physicsScene);

        // Clean up entity physics body and spatial index on destruction
        static void TeardownEntityPhysics(SceneData& scene, entt::entity entityID, PhysicsScene* physicsScene);

        // Find entity associated with a given Jolt BodyID
        static Entity FindEntityByBodyID(SceneData& scene, BodyID bodyID);

        // Create body in PhysicsScene for entity if it has RigidBody3D and CollisionShape3D
        static void CreateBodyForEntity(SceneData& scene, entt::entity entityID, PhysicsScene* physicsScene);
    };

}
