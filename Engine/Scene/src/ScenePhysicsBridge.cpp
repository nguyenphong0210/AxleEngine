#include <Axle/ScenePhysicsBridge.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>

namespace Axle {

    void ScenePhysicsBridge::SynchronizeTransforms(SceneData& scene, PhysicsScene* physicsScene) {
        if (!physicsScene || !scene.CanMutate()) return;
        auto& registry = scene.GetRegistry();
        // Validate the forest, then consume body poses from roots to leaves so
        // a child's local pose uses its parent's current post-step world pose.
        scene.UpdateWorldTransforms();
        std::vector<std::pair<entt::entity, glm::mat4>> pending;
        for (auto entity : registry.view<TransformComponent>()) {
            const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
            if (!hierarchy || hierarchy->Parent == entt::null) pending.emplace_back(entity, glm::mat4(1.0f));
        }
        while (!pending.empty()) {
            auto [entity, parentWorld] = pending.back();
            pending.pop_back();
            auto& transform = registry.get<TransformComponent>(entity);
            const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
            const auto inherited = hierarchy ? InheritedParentTransform(parentWorld, *hierarchy) : parentWorld;
            const auto* body = registry.try_get<RigidBody3D>(entity);
            if (body && body->MotionType != BodyMotionType::Static && !registry.all_of<CharacterControllerComponent>(entity) &&
                !body->RuntimeBodyID.IsInvalid() && physicsScene->IsBodyValid(body->RuntimeBodyID)) {
                auto position = physicsScene->GetBodyPosition(body->RuntimeBodyID);
                const auto rotation = physicsScene->GetBodyRotation(body->RuntimeBodyID);
                if (const auto* shape = registry.try_get<CollisionShape3D>(entity)) position -= rotation * shape->Offset;
                const auto local = glm::inverse(inherited) *
                    (glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation));
                transform.Translation = glm::vec3(local[3]);
                auto axes = glm::mat3(local);
                for (int axis = 0; axis < 3; ++axis) {
                    const auto length = glm::length(axes[axis]);
                    axes[axis] = length > 1e-6f ? axes[axis] / length : glm::vec3(axis == 0, axis == 1, axis == 2);
                }
                transform.Rotation = glm::normalize(glm::quat_cast(axes));
            }
            transform.LocalMatrix = transform.GetTransform();
            transform.WorldMatrix = inherited * transform.LocalMatrix;
            if (hierarchy) {
                for (auto child : hierarchy->Children) pending.emplace_back(child, transform.WorldMatrix);
            }
        }
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
        if (!rb.RuntimeBodyID.IsInvalid() && physicsScene->IsBodyValid(rb.RuntimeBodyID)) return;
        const auto& shape = registry.get<CollisionShape3D>(entityID);
        const auto& tc = registry.get<TransformComponent>(entityID);
        const auto world = scene.RecomputeWorldTransform(entityID);
        auto axes = glm::mat3(world);
        for (int axis = 0; axis < 3; ++axis) {
            const auto length = glm::length(axes[axis]);
            axes[axis] = length > 1e-6f ? axes[axis] / length : glm::vec3(axis == 0, axis == 1, axis == 2);
        }
        const auto rotation = glm::normalize(glm::quat_cast(axes));
        // Authored collider offsets live in the entity's rotated frame.
        const auto position = glm::vec3(world[3]) + rotation * shape.Offset;

        BodyID newBodyID{};
        switch (shape.ShapeType) {
            case CollisionShapeType::Box: {
                BoxBodyProps bProps;
                bProps.Position = position;
                bProps.Rotation = rotation;
                bProps.HalfExtent = shape.HalfExtent;
                bProps.IsDynamic = rb.MotionType != BodyMotionType::Static;
                bProps.Friction = shape.Friction;
                bProps.Restitution = shape.Restitution;
                bProps.Mass = rb.Mass;
                newBodyID = physicsScene->CreateBox(bProps);
                break;
            }
            case CollisionShapeType::Sphere: {
                SphereBodyProps sProps;
                sProps.Position = position;
                sProps.Radius = shape.Radius;
                sProps.IsDynamic = rb.MotionType != BodyMotionType::Static;
                sProps.Friction = shape.Friction;
                sProps.Restitution = shape.Restitution;
                sProps.Mass = rb.Mass;
                newBodyID = physicsScene->CreateSphere(sProps);
                break;
            }
            case CollisionShapeType::Cylinder: {
                CylinderBodyProps cProps;
                cProps.Position = position;
                cProps.HalfHeight = shape.HalfHeight;
                cProps.Radius = shape.Radius;
                cProps.IsDynamic = rb.MotionType != BodyMotionType::Static;
                cProps.Friction = shape.Friction;
                cProps.Restitution = shape.Restitution;
                cProps.Mass = rb.Mass;
                newBodyID = physicsScene->CreateCylinder(cProps);
                break;
            }
            default:
                if (shape.ShapeType == CollisionShapeType::Plane)
                    newBodyID = physicsScene->CreatePlane(position, glm::vec3(0, 1, 0), tc.Scale.x * 0.5f, shape.Friction, shape.Restitution);
                else if (shape.ShapeType == CollisionShapeType::Cone)
                    newBodyID = physicsScene->CreateCone(position, shape.HalfHeight, shape.Radius, rb.MotionType != BodyMotionType::Static, shape.Friction, shape.Restitution, rb.Mass);
                break;
        }

        if (!newBodyID.IsInvalid()) {
            rb.RuntimeBodyID = newBodyID;
            physicsScene->SetBodyPositionAndRotation(newBodyID, position, rotation);
            physicsScene->GetBodyInterface().SetGravityFactor(newBodyID, rb.GravityFactor);
            if (rb.MotionType == BodyMotionType::Kinematic)
                physicsScene->GetBodyInterface().SetMotionType(newBodyID, JPH::EMotionType::Kinematic, JPH::EActivation::Activate);
        }
    }

}
