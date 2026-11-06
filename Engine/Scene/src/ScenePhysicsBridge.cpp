#include <Axle/ScenePhysicsBridge.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>

namespace Axle {

    void ScenePhysicsBridge::SynchronizeTransforms(SceneData& scene, PhysicsScene* physicsScene) {
        if (!physicsScene) return;

        auto& registry = scene.GetRegistry();
        auto physicsView = registry.view<RigidBody3D, TransformComponent>();
        for (auto entity : physicsView) {
            if (registry.all_of<CharacterControllerComponent>(entity)) {
                continue; // Skip character controller; transform is managed by its controller
            }
            const auto& rb = registry.get<RigidBody3D>(entity);
            if (rb.MotionType == BodyMotionType::Static) {
                continue; // Static bodies never move; prevents visual mesh drift
            }

            auto& tc = registry.get<TransformComponent>(entity);
            if (!rb.RuntimeBodyID.IsInvalid()) {
                glm::vec3 worldPos = physicsScene->GetBodyPosition(rb.RuntimeBodyID);
                glm::quat worldRot = physicsScene->GetBodyRotation(rb.RuntimeBodyID);

                // Compensate for collider offset
                if (registry.all_of<CollisionShape3D>(entity)) {
                    const auto& cs = registry.get<CollisionShape3D>(entity);
                    worldPos -= worldRot * cs.Offset;
                }

                // Convert world space to local space if entity has a parent
                if (registry.all_of<HierarchyComponent>(entity)) {
                    const auto& hc = registry.get<HierarchyComponent>(entity);
                    if (hc.InheritTransform && hc.Parent != entt::null && registry.valid(hc.Parent) && registry.all_of<TransformComponent>(hc.Parent)) {
                        glm::mat4 parentWorld = scene.GetWorldTransform(hc.Parent);
                        glm::mat4 invParent = glm::inverse(parentWorld);
                        glm::mat4 childWorldMat = glm::translate(glm::mat4(1.0f), worldPos) * glm::mat4_cast(worldRot);
                        glm::mat4 childLocalMat = invParent * childWorldMat;
                        tc.Translation = glm::vec3(childLocalMat[3]);

                        glm::vec3 scale(
                            glm::length(glm::vec3(childLocalMat[0])),
                            glm::length(glm::vec3(childLocalMat[1])),
                            glm::length(glm::vec3(childLocalMat[2]))
                        );
                        glm::mat3 rotMatrix(1.0f);
                        if (scale.x > 1e-6f) rotMatrix[0] = glm::vec3(childLocalMat[0]) / scale.x;
                        if (scale.y > 1e-6f) rotMatrix[1] = glm::vec3(childLocalMat[1]) / scale.y;
                        if (scale.z > 1e-6f) rotMatrix[2] = glm::vec3(childLocalMat[2]) / scale.z;
                        tc.Rotation = glm::quat_cast(rotMatrix);
                        continue;
                    }
                }

                tc.Translation = worldPos;
                tc.Rotation = worldRot;
            }
        }

        // Propagate hierarchy transforms (Root -> Children)
        scene.UpdateWorldTransformsParallel(64);
    }

    void ScenePhysicsBridge::RestoreBodies(SceneData& scene, PhysicsScene* physicsScene) {
        if (!physicsScene) return;

        auto& registry = scene.GetRegistry();
        auto view = registry.view<RigidBody3D, TransformComponent>();
        for (auto entity : view) {
            const auto& rb = view.get<RigidBody3D>(entity);
            const auto& tc = view.get<TransformComponent>(entity);

            if (!rb.RuntimeBodyID.IsInvalid() && physicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                glm::vec3 offset(0.0f);
                if (registry.all_of<CollisionShape3D>(entity)) {
                    offset = registry.get<CollisionShape3D>(entity).Offset;
                }
                glm::vec3 worldPos = tc.Translation + tc.Rotation * offset;
                physicsScene->SetBodyPositionAndRotation(rb.RuntimeBodyID, worldPos, tc.Rotation, true);
                physicsScene->SetLinearVelocity(rb.RuntimeBodyID, glm::vec3(0.0f));
                physicsScene->GetBodyInterface().SetAngularVelocity(rb.RuntimeBodyID, JPH::Vec3::sZero());
            }
        }
    }

    void ScenePhysicsBridge::TeardownEntityPhysics(SceneData& scene, entt::entity entityID, PhysicsScene* physicsScene) {
        auto& registry = scene.GetRegistry();
        if (!registry.valid(entityID)) return;

        if (physicsScene) {
            if (registry.all_of<RigidBody3D>(entityID)) {
                auto& rb = registry.get<RigidBody3D>(entityID);
                if (!rb.RuntimeBodyID.IsInvalid()) {
                    physicsScene->DestroyBody(rb.RuntimeBodyID);
                    rb.RuntimeBodyID = {};
                }
            }
        }
    }

    Entity ScenePhysicsBridge::FindEntityByBodyID(SceneData& scene, BodyID bodyID) {
        if (bodyID.IsInvalid()) return {};

        auto& registry = scene.GetRegistry();
        auto view = registry.view<RigidBody3D>();
        for (auto entityID : view) {
            const auto& rb = view.get<RigidBody3D>(entityID);
            if (rb.RuntimeBodyID == bodyID) {
                return { entityID, &scene };
            }
        }
        return {};
    }

    void ScenePhysicsBridge::CreateBodyForEntity(SceneData& scene, entt::entity entityID, PhysicsScene* physicsScene) {
        if (!physicsScene) return;

        auto& registry = scene.GetRegistry();
        if (!registry.valid(entityID)) return;

        if (!registry.all_of<RigidBody3D, CollisionShape3D, TransformComponent>(entityID)) return;

        auto& rb = registry.get<RigidBody3D>(entityID);
        const auto& shape = registry.get<CollisionShape3D>(entityID);
        const auto& tc = registry.get<TransformComponent>(entityID);

        BodyID newBodyID{};
        switch (shape.ShapeType) {
            case CollisionShapeType::Box: {
                BoxBodyProps bProps;
                bProps.Position = tc.Translation;
                bProps.Rotation = tc.Rotation;
                bProps.HalfExtent = shape.HalfExtent;
                bProps.IsDynamic = rb.IsDynamic;
                bProps.Friction = shape.Friction;
                bProps.Restitution = shape.Restitution;
                bProps.Mass = rb.Mass;
                newBodyID = physicsScene->CreateBox(bProps);
                break;
            }
            case CollisionShapeType::Sphere: {
                SphereBodyProps sProps;
                sProps.Position = tc.Translation;
                sProps.Radius = shape.Radius;
                sProps.IsDynamic = rb.IsDynamic;
                sProps.Friction = shape.Friction;
                sProps.Restitution = shape.Restitution;
                sProps.Mass = rb.Mass;
                newBodyID = physicsScene->CreateSphere(sProps);
                break;
            }
            case CollisionShapeType::Cylinder: {
                CylinderBodyProps cProps;
                cProps.Position = tc.Translation;
                cProps.HalfHeight = shape.HalfHeight;
                cProps.Radius = shape.Radius;
                cProps.IsDynamic = rb.IsDynamic;
                cProps.Friction = shape.Friction;
                cProps.Restitution = shape.Restitution;
                cProps.Mass = rb.Mass;
                newBodyID = physicsScene->CreateCylinder(cProps);
                break;
            }
            default:
                break;
        }

        if (!newBodyID.IsInvalid()) {
            rb.RuntimeBodyID = newBodyID;
        }
    }

}
