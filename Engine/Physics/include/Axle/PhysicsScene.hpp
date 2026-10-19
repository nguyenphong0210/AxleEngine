#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/PhysicsLayers.hpp>
#include <Axle/PhysicsMath.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Core/TempAllocator.h>
#include <Axle/JobSystemEnkiTS.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace Axle {

    using BodyID = JPH::BodyID;

    struct PhysicsSettings {
        u32 MaxBodies = 10240;
        u32 NumBodyMutexes = 0;
        u32 MaxBodyPairs = 10240;
        u32 MaxContactConstraints = 10240;
        u32 TempAllocatorSizeMB = 10;
        glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
    };

    struct RayCastHit {
        BodyID HitBodyID;
        glm::vec3 Position{ 0.0f };
        f32 Distance = 0.0f;
    };

    struct BoxBodyProps {
        glm::vec3 Position{ 0.0f };
        glm::vec3 HalfExtent{ 0.5f, 0.5f, 0.5f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        bool IsDynamic = true;
        f32 Friction = 0.5f;
        f32 Restitution = 0.0f;
        bool AllowSleeping = true;
        f32 Mass = 0.0f;
    };

    struct SphereBodyProps {
        glm::vec3 Position{ 0.0f };
        f32 Radius = 0.5f;
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        bool IsDynamic = true;
        f32 Friction = 0.5f;
        f32 Restitution = 0.0f;
        bool AllowSleeping = true;
        f32 Mass = 0.0f;
    };

    struct CylinderBodyProps {
        glm::vec3 Position{ 0.0f };
        f32 HalfHeight = 1.0f;
        f32 Radius = 0.5f;
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        bool IsDynamic = true;
        f32 Friction = 0.5f;
        f32 Restitution = 0.0f;
        bool AllowSleeping = true;
        f32 Mass = 0.0f;
    };

    struct ConeBodyProps {
        glm::vec3 Position{ 0.0f };
        f32 HalfHeight = 1.0f;
        f32 Radius = 0.5f;
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        bool IsDynamic = true;
        f32 Friction = 0.5f;
        f32 Restitution = 0.0f;
        bool AllowSleeping = true;
        f32 Mass = 0.0f;
    };

    struct PlaneBodyProps {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        f32 HalfExtent = 100.0f;
        f32 Friction = 0.6f;
        f32 Restitution = 0.0f;
    };

    struct PhysicsDrawSettings {
        bool DrawShapes = true;
        bool DrawWireframe = true;
        bool DrawBoundingBoxes = false;
        bool DrawVelocity = false;
        bool DrawCenterOfMass = false;
    };

    class PhysicsDebugRenderer;

    class PhysicsScene {
    public:
        PhysicsScene(const PhysicsSettings& settings = PhysicsSettings());
        ~PhysicsScene();

        // Step simulation by delta time
        void Step(Timestep ts, i32 collisionSteps = 1);

        // Debug visualization using PhysicsDebugRenderer
        void DrawDebug(PhysicsDebugRenderer& renderer, const PhysicsDrawSettings& settings = PhysicsDrawSettings());

        // Body creation and management
        BodyID CreateBox(const BoxBodyProps& props);
        BodyID CreateBox(const glm::vec3& position, const glm::vec3& halfExtent, bool isDynamic = true, f32 friction = 0.5f, f32 restitution = 0.0f, f32 mass = 0.0f);

        BodyID CreateSphere(const SphereBodyProps& props);
        BodyID CreateSphere(const glm::vec3& position, f32 radius, bool isDynamic = true, f32 friction = 0.5f, f32 restitution = 0.0f, f32 mass = 0.0f);

        BodyID CreateCylinder(const CylinderBodyProps& props);
        BodyID CreateCylinder(const glm::vec3& position, f32 halfHeight, f32 radius, bool isDynamic = true, f32 friction = 0.5f, f32 restitution = 0.0f, f32 mass = 0.0f);

        BodyID CreateCone(const ConeBodyProps& props);
        BodyID CreateCone(const glm::vec3& position, f32 halfHeight, f32 radius, bool isDynamic = true, f32 friction = 0.5f, f32 restitution = 0.0f, f32 mass = 0.0f);

        BodyID CreatePlane(const PlaneBodyProps& props);
        BodyID CreatePlane(const glm::vec3& position, const glm::vec3& normal = glm::vec3(0.0f, 1.0f, 0.0f), f32 halfExtent = 100.0f, f32 friction = 0.6f, f32 restitution = 0.0f);

        void DestroyBody(BodyID bodyID);
        void OptimizeBroadPhase();

        // Transform and motion queries / mutations
        glm::vec3 GetBodyPosition(BodyID bodyID) const;
        glm::vec3 GetCenterOfMassPosition(BodyID bodyID) const;
        glm::quat GetBodyRotation(BodyID bodyID) const;
        glm::mat4 GetBodyTransform(BodyID bodyID) const;

        void SetBodyPositionAndRotation(BodyID bodyID, const glm::vec3& position, const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), bool activate = true);
        glm::vec3 GetLinearVelocity(BodyID bodyID) const;
        void SetLinearVelocity(BodyID bodyID, const glm::vec3& velocity);

        bool IsBodyValid(BodyID bodyID) const;
        bool IsActive(BodyID bodyID) const;
        void ActivateBody(BodyID bodyID);
        void DeactivateBody(BodyID bodyID);

        // Raycasting
        bool CastRay(const glm::vec3& origin, const glm::vec3& direction, f32 maxDistance, RayCastHit& outHit) const;

        // Scene statistics
        u32 GetNumBodies() const;
        u32 GetNumActiveBodies() const;

        // Raw Jolt accessors
        JPH::PhysicsSystem& GetSystem() { return m_PhysicsSystem; }
        const JPH::PhysicsSystem& GetSystem() const { return m_PhysicsSystem; }

        JPH::BodyInterface& GetBodyInterface() { return m_PhysicsSystem.GetBodyInterface(); }
        const JPH::BodyInterface& GetBodyInterface() const { return m_PhysicsSystem.GetBodyInterface(); }

        // Constraints & Step Listeners
        void AddConstraint(JPH::Constraint* constraint);
        void RemoveConstraint(JPH::Constraint* constraint);
        void AddStepListener(JPH::PhysicsStepListener* listener);
        void RemoveStepListener(JPH::PhysicsStepListener* listener);

        void SetGravity(const glm::vec3& gravity);
        glm::vec3 GetGravity() const;

        JobSystemEnkiTS* GetJobSystem() { return m_JobSystem.get(); }
        const JobSystemEnkiTS* GetJobSystem() const { return m_JobSystem.get(); }

        JPH::TempAllocator* GetTempAllocator() { return m_TempAllocator.get(); }
        const JPH::TempAllocator* GetTempAllocator() const { return m_TempAllocator.get(); }

    private:
        PhysicsSettings m_Settings;

        // Jolt physics memory & unified job system
        std::unique_ptr<JPH::TempAllocatorImpl> m_TempAllocator;
        std::unique_ptr<JobSystemEnkiTS> m_JobSystem;

        // Broadphase and collision filters
        BPLayerInterfaceImpl m_BPLayerInterface;
        ObjectVsBroadPhaseLayerFilterImpl m_ObjectVsBPFilter;
        ObjectLayerPairFilterImpl m_ObjectLayerFilter;

        // Main Physics System
        JPH::PhysicsSystem m_PhysicsSystem;
    };

}
