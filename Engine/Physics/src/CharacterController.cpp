#include <Axle/CharacterController.hpp>
#include <Axle/Log.hpp>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <algorithm>
#include <cmath>

namespace Axle {

    CharacterController::CharacterController(PhysicsScene& scene,
                                             const glm::vec3& position,
                                             const glm::quat& rotation,
                                             const CharacterControllerSettings& settings)
        : m_Scene(scene), m_Settings(settings) {
        CreateJoltCharacter(position, rotation);
    }

    CharacterController::~CharacterController() {
        m_Character = nullptr;
        m_ContactListener = nullptr;
    }

    void CharacterController::CreateJoltCharacter(const glm::vec3& position, const glm::quat& rotation) {
        // Capsule standing upright with base sitting at (0, 0, 0)
        JPH::Ref<JPH::Shape> innerCapsule = new JPH::CapsuleShape(m_Settings.HalfHeight, m_Settings.Radius);
        JPH::Ref<JPH::RotatedTranslatedShapeSettings> standShape = new JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, m_Settings.HalfHeight + m_Settings.Radius, 0.0f),
            JPH::Quat::sIdentity(),
            innerCapsule
        );

        JPH::Shape::ShapeResult shapeResult = standShape->Create();
        if (!shapeResult.IsValid()) {
            AXLE_CORE_ERROR("Failed to create CharacterController capsule shape: {0}", shapeResult.GetError().c_str());
            return;
        }

        JPH::CharacterVirtualSettings charSettings;
        charSettings.mMass = m_Settings.Mass;
        charSettings.mMaxStrength = m_Settings.MaxStrength;
        charSettings.mShape = shapeResult.Get();
        charSettings.mMaxSlopeAngle = m_Settings.MaxSlopeAngle;
        charSettings.mCharacterPadding = m_Settings.CharacterPadding;
        charSettings.mPenetrationRecoverySpeed = m_Settings.PenetrationRecoverySpeed;
        charSettings.mPredictiveContactDistance = m_Settings.PredictiveContactDistance;
        charSettings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        charSettings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -(m_Settings.HalfHeight + m_Settings.Radius));

        if (m_Settings.EnableInnerBody) {
            charSettings.mInnerBodyShape = shapeResult.Get();
            charSettings.mInnerBodyLayer = Layers::MOVING;
        }

        m_ContactListener = std::make_unique<CharacterContactListenerImpl>();
        m_Character = new JPH::CharacterVirtual(&charSettings, ToJolt(position), ToJolt(rotation), 0, &m_Scene.GetSystem());
        m_Character->SetListener(m_ContactListener.get());
    }

    void CharacterController::SetTargetVelocity(const glm::vec3& horizontalVelocity) {
        m_TargetHorizontalVelocity = horizontalVelocity;
        m_TargetHorizontalVelocity.y = 0.0f; // Only horizontal plane
    }

    void CharacterController::Jump(float jumpSpeed) {
        m_JumpRequested = true;
        m_PendingJumpSpeed = jumpSpeed;
    }

    void CharacterController::Update(float dt) {
        if (!m_Character || dt <= 0.0f) return;

        auto groundState = m_Character->GetGroundState();
        JPH::Vec3 groundVelocity = m_Character->GetGroundVelocity();
        glm::vec3 currentLinearVel = ToGlm(m_Character->GetLinearVelocity());

        glm::vec3 newVelocity{ 0.0f };

        if (groundState == JPH::CharacterVirtual::EGroundState::OnGround
            && (currentLinearVel.y - groundVelocity.GetY()) < 0.1f) {
            newVelocity = ToGlm(groundVelocity);
            m_VerticalVelocity = 0.0f;

            if (m_JumpRequested) {
                newVelocity.y += m_PendingJumpSpeed;
                m_VerticalVelocity = m_PendingJumpSpeed;
                m_JumpRequested = false;
            } else {
                newVelocity.y += m_Settings.Gravity.y * dt;
            }
        } else {
            if (m_JumpRequested) {
                m_VerticalVelocity = m_PendingJumpSpeed;
                m_JumpRequested = false;
            } else {
                m_VerticalVelocity += m_Settings.Gravity.y * dt;
            }
            newVelocity.y = m_VerticalVelocity;
        }

        // Add target horizontal movement input
        newVelocity.x += m_TargetHorizontalVelocity.x;
        newVelocity.z += m_TargetHorizontalVelocity.z;

        // Cancel movement into slopes steeper than max walkable angle
        if (m_Character->IsSlopeTooSteep(m_Character->GetGroundNormal())) {
            newVelocity = ToGlm(m_Character->CancelVelocityTowardsSteepSlopes(ToJolt(newVelocity)));
        }

        m_Character->SetLinearVelocity(ToJolt(newVelocity));

        // Configure extended settings for stair stepping & ground snapping
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -m_Settings.StickToFloorStepDown, 0.0f);
        updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, m_Settings.StepOffset, 0.0f);
        updateSettings.mWalkStairsMinStepForward = 0.02f;
        updateSettings.mWalkStairsStepForwardTest = 0.15f;
        updateSettings.mWalkStairsStepDownExtra = JPH::Vec3::sZero();

        JPH::TempAllocator* allocator = m_Scene.GetTempAllocator();
        JPH::TempAllocatorMalloc fallbackAllocator;
        JPH::TempAllocator& activeAllocator = allocator ? *allocator : fallbackAllocator;

        auto& physicsSystem = m_Scene.GetSystem();

        m_Character->ExtendedUpdate(
            dt,
            ToJolt(m_Settings.Gravity),
            updateSettings,
            physicsSystem.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
            physicsSystem.GetDefaultLayerFilter(Layers::MOVING),
            { },
            { },
            activeAllocator
        );
    }

    glm::vec3 CharacterController::GetPosition() const {
        return m_Character ? ToGlm(m_Character->GetPosition()) : glm::vec3(0.0f);
    }

    void CharacterController::SetPosition(const glm::vec3& position) {
        if (m_Character) {
            m_Character->SetPosition(ToJolt(position));
        }
    }

    glm::quat CharacterController::GetRotation() const {
        return m_Character ? ToGlm(m_Character->GetRotation()) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    void CharacterController::SetRotation(const glm::quat& rotation) {
        if (m_Character) {
            m_Character->SetRotation(ToJolt(rotation));
        }
    }

    glm::vec3 CharacterController::GetLinearVelocity() const {
        return m_Character ? ToGlm(m_Character->GetLinearVelocity()) : glm::vec3(0.0f);
    }

    void CharacterController::SetLinearVelocity(const glm::vec3& velocity) {
        if (m_Character) {
            m_Character->SetLinearVelocity(ToJolt(velocity));
        }
    }

    CharacterGroundState CharacterController::GetGroundState() const {
        if (!m_Character) return CharacterGroundState::InAir;
        switch (m_Character->GetGroundState()) {
            case JPH::CharacterVirtual::EGroundState::OnGround: return CharacterGroundState::OnGround;
            case JPH::CharacterVirtual::EGroundState::OnSteepGround: return CharacterGroundState::OnSteepGround;
            case JPH::CharacterVirtual::EGroundState::NotSupported: return CharacterGroundState::NotSupported;
            case JPH::CharacterVirtual::EGroundState::InAir:
            default: return CharacterGroundState::InAir;
        }
    }

    bool CharacterController::IsOnGround() const {
        return GetGroundState() == CharacterGroundState::OnGround;
    }

    bool CharacterController::IsInAir() const {
        return GetGroundState() == CharacterGroundState::InAir;
    }

    bool CharacterController::IsOnSteepGround() const {
        return GetGroundState() == CharacterGroundState::OnSteepGround;
    }

    glm::vec3 CharacterController::GetGroundNormal() const {
        return m_Character ? ToGlm(m_Character->GetGroundNormal()) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    glm::vec3 CharacterController::GetGroundPosition() const {
        return m_Character ? ToGlm(m_Character->GetGroundPosition()) : glm::vec3(0.0f);
    }

    glm::vec3 CharacterController::GetGroundVelocity() const {
        return m_Character ? ToGlm(m_Character->GetGroundVelocity()) : glm::vec3(0.0f);
    }

    BodyID CharacterController::GetGroundBodyID() const {
        return m_Character ? m_Character->GetGroundBodyID() : BodyID();
    }

    void CharacterController::SetMaxSlopeAngle(float radians) {
        m_Settings.MaxSlopeAngle = radians;
        if (m_Character) {
            m_Character->SetMaxSlopeAngle(radians);
        }
    }

    void CharacterController::SetStepOffset(float stepOffset) {
        m_Settings.StepOffset = stepOffset;
    }

    void CharacterController::SetStickToFloorStepDown(float stepDown) {
        m_Settings.StickToFloorStepDown = stepDown;
    }

}
