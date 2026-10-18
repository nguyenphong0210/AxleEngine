#pragma once
#include <Axle/Types.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsMath.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace Axle {

    enum class CharacterGroundState {
        OnGround,
        OnSteepGround,
        NotSupported,
        InAir
    };

    struct CharacterControllerSettings {
        float Radius = 0.35f;            // Capsule cylinder radius in meters
        float HalfHeight = 0.65f;        // Capsule cylinder half height in meters (total height = 2*HalfHeight + 2*Radius = 2.0m)
        float Mass = 75.0f;              // Character mass in kg
        float MaxStrength = 2500.0f;     // Maximum push force exerted on dynamic bodies in Newtons
        float MaxSlopeAngle = 0.872665f; // Maximum walkable slope angle in radians (~50 degrees)
        float StepOffset = 0.4f;         // Maximum obstacle/stair step-up height in meters
        float StickToFloorStepDown = 0.5f; // Maximum downward projection distance for ground snapping
        glm::vec3 Gravity{ 0.0f, -19.62f, 0.0f }; // Downward gravity acceleration
        float CharacterPadding = 0.02f;
        float PredictiveContactDistance = 0.1f;
        float PenetrationRecoverySpeed = 1.0f;
        bool EnableInnerBody = true;     // Create proxy rigid body in moving layer
    };

    class CharacterController {
    public:
        CharacterController(PhysicsScene& scene,
                            const glm::vec3& position,
                            const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                            const CharacterControllerSettings& settings = CharacterControllerSettings{});
        ~CharacterController();

        // Target movement & jump control
        void SetTargetVelocity(const glm::vec3& horizontalVelocity);
        const glm::vec3& GetTargetVelocity() const { return m_TargetHorizontalVelocity; }

        void Jump(float jumpSpeed);

        // Simulation update step
        void Update(float dt);

        // Transform queries & mutations
        glm::vec3 GetPosition() const;
        void SetPosition(const glm::vec3& position);

        glm::quat GetRotation() const;
        void SetRotation(const glm::quat& rotation);

        glm::vec3 GetLinearVelocity() const;
        void SetLinearVelocity(const glm::vec3& velocity);

        // Ground & collision state queries
        CharacterGroundState GetGroundState() const;
        bool IsOnGround() const;
        bool IsInAir() const;
        bool IsOnSteepGround() const;

        glm::vec3 GetGroundNormal() const;
        glm::vec3 GetGroundPosition() const;
        glm::vec3 GetGroundVelocity() const;
        BodyID GetGroundBodyID() const;

        // Settings configuration
        const CharacterControllerSettings& GetSettings() const { return m_Settings; }
        CharacterControllerSettings& GetSettings() { return m_Settings; }

        void SetMaxSlopeAngle(float radians);
        void SetStepOffset(float stepOffset);
        void SetStickToFloorStepDown(float stepDown);

        // Raw Jolt CharacterVirtual
        JPH::CharacterVirtual* GetJoltCharacter() { return m_Character.GetPtr(); }
        const JPH::CharacterVirtual* GetJoltCharacter() const { return m_Character.GetPtr(); }

    private:
        void CreateJoltCharacter(const glm::vec3& position, const glm::quat& rotation);

    private:
        PhysicsScene& m_Scene;
        CharacterControllerSettings m_Settings;
        JPH::Ref<JPH::CharacterVirtual> m_Character;

        glm::vec3 m_TargetHorizontalVelocity{ 0.0f };
        float m_VerticalVelocity = 0.0f;
        bool m_JumpRequested = false;
        float m_PendingJumpSpeed = 0.0f;

        // Contact listener for dynamic body pushback
        class CharacterContactListenerImpl : public JPH::CharacterContactListener {
        public:
            virtual void OnContactAdded(const JPH::CharacterVirtual* inCharacter,
                                        const JPH::BodyID& inBodyID2,
                                        const JPH::SubShapeID& inSubShapeID2,
                                        JPH::RVec3Arg inContactPosition,
                                        JPH::Vec3Arg inContactNormal,
                                        JPH::CharacterContactSettings& ioSettings) override {
                ioSettings.mCanPushCharacter = true;
                ioSettings.mCanReceiveImpulses = true;
            }
        };

        std::unique_ptr<CharacterContactListenerImpl> m_ContactListener;
    };

}
