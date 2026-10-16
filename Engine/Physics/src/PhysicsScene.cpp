#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/PhysicsDebugRenderer.hpp>
#include <Axle/Log.hpp>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCylinderShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <thread>
#include <algorithm>

namespace Axle {

    PhysicsScene::PhysicsScene(const PhysicsSettings& settings)
        : m_Settings(settings) {
        
        // Ensure core Jolt physics engine is initialized
        if (!PhysicsEngine::IsInitialized()) {
            PhysicsEngine::Init();
        }

        // 1. Allocate temporary buffer for physics updates
        m_TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(settings.TempAllocatorSizeMB * 1024 * 1024);

        // 2. Initialize multithreaded job system bridged to Axle::JobSystem (enkiTS)
        m_JobSystem = std::make_unique<JobSystemEnkiTS>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers
        );

        // 3. Initialize Jolt PhysicsSystem
        m_PhysicsSystem.Init(
            settings.MaxBodies,
            settings.NumBodyMutexes,
            settings.MaxBodyPairs,
            settings.MaxContactConstraints,
            m_BPLayerInterface,
            m_ObjectVsBPFilter,
            m_ObjectLayerFilter
        );

        SetGravity(settings.Gravity);

        AXLE_CORE_INFO("PhysicsScene initialized with JobSystemEnkiTS (Concurrency: {0}, MaxBodies: {1})", 
            m_JobSystem->GetMaxConcurrency(), settings.MaxBodies);
    }

    PhysicsScene::~PhysicsScene() {
        JPH::BodyIDVector bodyIDs;
        m_PhysicsSystem.GetBodies(bodyIDs);
        if (!bodyIDs.empty()) {
            auto& bi = m_PhysicsSystem.GetBodyInterface();
            bi.RemoveBodies(bodyIDs.data(), static_cast<int>(bodyIDs.size()));
            bi.DestroyBodies(bodyIDs.data(), static_cast<int>(bodyIDs.size()));
        }
        AXLE_CORE_INFO("PhysicsScene destroyed");
    }

    void PhysicsScene::Step(Timestep ts, i32 collisionSteps) {
        const f32 dt = ts.GetSeconds();
        if (dt <= 0.0f) return;

        // Jolt physics simulation update
        m_PhysicsSystem.Update(dt, collisionSteps, m_TempAllocator.get(), m_JobSystem.get());
    }

    void PhysicsScene::DrawDebug(PhysicsDebugRenderer& renderer, const PhysicsDrawSettings& settings) {
        JPH::DebugRenderer::sInstance = &renderer;

        JPH::BodyManager::DrawSettings joltSettings;
        joltSettings.mDrawShape = settings.DrawShapes;
        joltSettings.mDrawShapeWireframe = settings.DrawWireframe;
        joltSettings.mDrawBoundingBox = settings.DrawBoundingBoxes;
        joltSettings.mDrawVelocity = settings.DrawVelocity;
        joltSettings.mDrawCenterOfMassTransform = settings.DrawCenterOfMass;

        if (settings.DrawShapes || settings.DrawWireframe || settings.DrawBoundingBoxes || settings.DrawVelocity || settings.DrawCenterOfMass) {
            m_PhysicsSystem.DrawBodies(joltSettings, &renderer);
        }

        m_PhysicsSystem.DrawConstraints(&renderer);
        m_PhysicsSystem.DrawConstraintLimits(&renderer);
    }

    BodyID PhysicsScene::CreateBox(const BoxBodyProps& props) {
        JPH::BoxShapeSettings shapeSettings(ToJolt(props.HalfExtent));
        JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
        if (shapeResult.HasError()) {
            AXLE_CORE_ERROR("Failed to create Box shape: {0}", shapeResult.GetError().c_str());
            return BodyID();
        }

        const JPH::EMotionType motionType = props.IsDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static;
        const JPH::ObjectLayer layer = props.IsDynamic ? Layers::MOVING : Layers::NON_MOVING;

        JPH::BodyCreationSettings bodySettings(
            shapeResult.Get(),
            ToJolt(props.Position),
            ToJolt(props.Rotation),
            motionType,
            layer
        );
        bodySettings.mFriction = props.Friction;
        bodySettings.mRestitution = props.Restitution;
        bodySettings.mAllowSleeping = props.AllowSleeping;

        const JPH::EActivation activation = props.IsDynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
        const BodyID bodyID = m_PhysicsSystem.GetBodyInterface().CreateAndAddBody(bodySettings, activation);
        if (bodyID.IsInvalid()) {
            AXLE_CORE_ERROR("Failed to create and add Box body to physics system!");
        }
        return bodyID;
    }

    BodyID PhysicsScene::CreateBox(const glm::vec3& position, const glm::vec3& halfExtent, bool isDynamic, f32 friction, f32 restitution) {
        BoxBodyProps props;
        props.Position = position;
        props.HalfExtent = halfExtent;
        props.IsDynamic = isDynamic;
        props.Friction = friction;
        props.Restitution = restitution;
        return CreateBox(props);
    }

    BodyID PhysicsScene::CreateSphere(const SphereBodyProps& props) {
        JPH::SphereShapeSettings shapeSettings(props.Radius);
        JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
        if (shapeResult.HasError()) {
            AXLE_CORE_ERROR("Failed to create Sphere shape: {0}", shapeResult.GetError().c_str());
            return BodyID();
        }

        const JPH::EMotionType motionType = props.IsDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static;
        const JPH::ObjectLayer layer = props.IsDynamic ? Layers::MOVING : Layers::NON_MOVING;

        JPH::BodyCreationSettings bodySettings(
            shapeResult.Get(),
            ToJolt(props.Position),
            ToJolt(props.Rotation),
            motionType,
            layer
        );
        bodySettings.mFriction = props.Friction;
        bodySettings.mRestitution = props.Restitution;
        bodySettings.mAllowSleeping = props.AllowSleeping;

        const JPH::EActivation activation = props.IsDynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
        const BodyID bodyID = m_PhysicsSystem.GetBodyInterface().CreateAndAddBody(bodySettings, activation);
        if (bodyID.IsInvalid()) {
            AXLE_CORE_ERROR("Failed to create and add Sphere body to physics system!");
        }
        return bodyID;
    }

    BodyID PhysicsScene::CreateSphere(const glm::vec3& position, f32 radius, bool isDynamic, f32 friction, f32 restitution) {
        SphereBodyProps props;
        props.Position = position;
        props.Radius = radius;
        props.IsDynamic = isDynamic;
        props.Friction = friction;
        props.Restitution = restitution;
        return CreateSphere(props);
    }

    BodyID PhysicsScene::CreateCylinder(const CylinderBodyProps& props) {
        JPH::CylinderShapeSettings shapeSettings(props.HalfHeight, props.Radius);
        JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
        if (shapeResult.HasError()) {
            AXLE_CORE_ERROR("Failed to create Cylinder shape: {0}", shapeResult.GetError().c_str());
            return BodyID();
        }

        const JPH::EMotionType motionType = props.IsDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static;
        const JPH::ObjectLayer layer = props.IsDynamic ? Layers::MOVING : Layers::NON_MOVING;

        JPH::BodyCreationSettings bodySettings(
            shapeResult.Get(),
            ToJolt(props.Position),
            ToJolt(props.Rotation),
            motionType,
            layer
        );
        bodySettings.mFriction = props.Friction;
        bodySettings.mRestitution = props.Restitution;
        bodySettings.mAllowSleeping = props.AllowSleeping;

        const JPH::EActivation activation = props.IsDynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
        const BodyID bodyID = m_PhysicsSystem.GetBodyInterface().CreateAndAddBody(bodySettings, activation);
        if (bodyID.IsInvalid()) {
            AXLE_CORE_ERROR("Failed to create and add Cylinder body to physics system!");
        }
        return bodyID;
    }

    BodyID PhysicsScene::CreateCylinder(const glm::vec3& position, f32 halfHeight, f32 radius, bool isDynamic, f32 friction, f32 restitution) {
        CylinderBodyProps props;
        props.Position = position;
        props.HalfHeight = halfHeight;
        props.Radius = radius;
        props.IsDynamic = isDynamic;
        props.Friction = friction;
        props.Restitution = restitution;
        return CreateCylinder(props);
    }

    BodyID PhysicsScene::CreateCone(const ConeBodyProps& props) {
        JPH::TaperedCylinderShapeSettings shapeSettings(props.HalfHeight, 0.0f, props.Radius, 0.0f);
        JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
        if (shapeResult.HasError()) {
            AXLE_CORE_ERROR("Failed to create Cone shape: {0}", shapeResult.GetError().c_str());
            return BodyID();
        }

        const JPH::EMotionType motionType = props.IsDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static;
        const JPH::ObjectLayer layer = props.IsDynamic ? Layers::MOVING : Layers::NON_MOVING;

        JPH::BodyCreationSettings bodySettings(
            shapeResult.Get(),
            ToJolt(props.Position),
            ToJolt(props.Rotation),
            motionType,
            layer
        );
        bodySettings.mFriction = props.Friction;
        bodySettings.mRestitution = props.Restitution;
        bodySettings.mAllowSleeping = props.AllowSleeping;

        const JPH::EActivation activation = props.IsDynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
        const BodyID bodyID = m_PhysicsSystem.GetBodyInterface().CreateAndAddBody(bodySettings, activation);
        if (bodyID.IsInvalid()) {
            AXLE_CORE_ERROR("Failed to create and add Cone body to physics system!");
        }
        return bodyID;
    }

    BodyID PhysicsScene::CreateCone(const glm::vec3& position, f32 halfHeight, f32 radius, bool isDynamic, f32 friction, f32 restitution) {
        ConeBodyProps props;
        props.Position = position;
        props.HalfHeight = halfHeight;
        props.Radius = radius;
        props.IsDynamic = isDynamic;
        props.Friction = friction;
        props.Restitution = restitution;
        return CreateCone(props);
    }

    BodyID PhysicsScene::CreatePlane(const PlaneBodyProps& props) {
        glm::vec3 n = props.Normal;
        if (glm::dot(n, n) < 1e-6f) {
            n = glm::vec3(0.0f, 1.0f, 0.0f);
        } else {
            n = glm::normalize(n);
        }

        JPH::PlaneShapeSettings shapeSettings(JPH::Plane(ToJolt(n), 0.0f), nullptr, props.HalfExtent);
        JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
        if (shapeResult.HasError()) {
            AXLE_CORE_ERROR("Failed to create Plane shape: {0}", shapeResult.GetError().c_str());
            return BodyID();
        }

        JPH::BodyCreationSettings bodySettings(
            shapeResult.Get(),
            ToJolt(props.Position),
            ToJolt(props.Rotation),
            JPH::EMotionType::Static,
            Layers::NON_MOVING
        );
        bodySettings.mFriction = props.Friction;
        bodySettings.mRestitution = props.Restitution;

        const BodyID bodyID = m_PhysicsSystem.GetBodyInterface().CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
        if (bodyID.IsInvalid()) {
            AXLE_CORE_ERROR("Failed to create and add Plane body to physics system!");
        }
        return bodyID;
    }

    BodyID PhysicsScene::CreatePlane(const glm::vec3& position, const glm::vec3& normal, f32 halfExtent, f32 friction, f32 restitution) {
        PlaneBodyProps props;
        props.Position = position;
        props.Normal = normal;
        props.HalfExtent = halfExtent;
        props.Friction = friction;
        props.Restitution = restitution;
        return CreatePlane(props);
    }

    void PhysicsScene::DestroyBody(BodyID bodyID) {
        if (bodyID.IsInvalid()) return;
        auto& bi = m_PhysicsSystem.GetBodyInterface();
        bi.RemoveBody(bodyID);
        bi.DestroyBody(bodyID);
    }

    void PhysicsScene::OptimizeBroadPhase() {
        m_PhysicsSystem.OptimizeBroadPhase();
    }

    bool PhysicsScene::IsBodyValid(BodyID bodyID) const {
        if (bodyID.IsInvalid()) return false;
        return m_PhysicsSystem.GetBodyInterface().IsAdded(bodyID);
    }

    glm::vec3 PhysicsScene::GetBodyPosition(BodyID bodyID) const {
        if (!IsBodyValid(bodyID)) return glm::vec3(0.0f);
        return ToGlm(m_PhysicsSystem.GetBodyInterface().GetPosition(bodyID));
    }

    glm::vec3 PhysicsScene::GetCenterOfMassPosition(BodyID bodyID) const {
        if (!IsBodyValid(bodyID)) return glm::vec3(0.0f);
        return ToGlm(m_PhysicsSystem.GetBodyInterface().GetCenterOfMassPosition(bodyID));
    }

    glm::quat PhysicsScene::GetBodyRotation(BodyID bodyID) const {
        if (!IsBodyValid(bodyID)) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return ToGlm(m_PhysicsSystem.GetBodyInterface().GetRotation(bodyID));
    }

    glm::mat4 PhysicsScene::GetBodyTransform(BodyID bodyID) const {
        if (!IsBodyValid(bodyID)) return glm::mat4(1.0f);
        return ToGlm(m_PhysicsSystem.GetBodyInterface().GetWorldTransform(bodyID));
    }

    void PhysicsScene::SetBodyPositionAndRotation(BodyID bodyID, const glm::vec3& position, const glm::quat& rotation, bool activate) {
        if (!IsBodyValid(bodyID)) return;
        auto& bi = m_PhysicsSystem.GetBodyInterface();
        bi.SetPositionAndRotation(
            bodyID,
            ToJolt(position),
            ToJolt(rotation),
            activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate
        );
    }

    glm::vec3 PhysicsScene::GetLinearVelocity(BodyID bodyID) const {
        if (!IsBodyValid(bodyID)) return glm::vec3(0.0f);
        return ToGlm(m_PhysicsSystem.GetBodyInterface().GetLinearVelocity(bodyID));
    }

    void PhysicsScene::SetLinearVelocity(BodyID bodyID, const glm::vec3& velocity) {
        if (!IsBodyValid(bodyID)) return;
        m_PhysicsSystem.GetBodyInterface().SetLinearVelocity(bodyID, ToJolt(velocity));
    }

    bool PhysicsScene::IsActive(BodyID bodyID) const {
        if (!IsBodyValid(bodyID)) return false;
        return m_PhysicsSystem.GetBodyInterface().IsActive(bodyID);
    }

    void PhysicsScene::ActivateBody(BodyID bodyID) {
        if (!IsBodyValid(bodyID)) return;
        m_PhysicsSystem.GetBodyInterface().ActivateBody(bodyID);
    }

    void PhysicsScene::DeactivateBody(BodyID bodyID) {
        if (!IsBodyValid(bodyID)) return;
        m_PhysicsSystem.GetBodyInterface().DeactivateBody(bodyID);
    }

    u32 PhysicsScene::GetNumBodies() const {
        return m_PhysicsSystem.GetNumBodies();
    }

    u32 PhysicsScene::GetNumActiveBodies() const {
        return m_PhysicsSystem.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    }

    void PhysicsScene::SetGravity(const glm::vec3& gravity) {
        m_Settings.Gravity = gravity;
        m_PhysicsSystem.SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }

    glm::vec3 PhysicsScene::GetGravity() const {
        const JPH::Vec3 g = m_PhysicsSystem.GetGravity();
        return { g.GetX(), g.GetY(), g.GetZ() };
    }

    void PhysicsScene::AddConstraint(JPH::Constraint* constraint) {
        if (constraint) {
            m_PhysicsSystem.AddConstraint(constraint);
        }
    }

    void PhysicsScene::RemoveConstraint(JPH::Constraint* constraint) {
        if (constraint) {
            m_PhysicsSystem.RemoveConstraint(constraint);
        }
    }

    void PhysicsScene::AddStepListener(JPH::PhysicsStepListener* listener) {
        if (listener) {
            m_PhysicsSystem.AddStepListener(listener);
        }
    }

    void PhysicsScene::RemoveStepListener(JPH::PhysicsStepListener* listener) {
        if (listener) {
            m_PhysicsSystem.RemoveStepListener(listener);
        }
    }

    bool PhysicsScene::CastRay(const glm::vec3& origin, const glm::vec3& direction, f32 maxDistance, RayCastHit& outHit) const {
        const glm::vec3 normDir = (glm::length(direction) > 0.0001f) ? glm::normalize(direction) : glm::vec3(0.0f, -1.0f, 0.0f);
        JPH::RRayCast ray(ToJolt(origin), ToJolt(normDir * maxDistance));
        JPH::RayCastResult hit;

        if (m_PhysicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit)) {
            outHit.HitBodyID = hit.mBodyID;
            outHit.Distance = maxDistance * hit.mFraction;
            outHit.Position = origin + normDir * outHit.Distance;
            return true;
        }
        return false;
    }

}
