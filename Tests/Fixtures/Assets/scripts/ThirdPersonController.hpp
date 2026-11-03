#pragma once
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/Log.hpp>
#include <Axle/Scene.hpp>
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

class ThirdPersonController : public Axle::ScriptableEntity {
public:
    // --- Live Inspector Tunable Fields ---
    float WalkSpeed = 3.5f;               // Walking speed in m/s
    float RunSpeed = 7.0f;                // Sprinting / running speed in m/s
    float JumpSpeed = 6.0f;               // Vertical jump impulse speed in m/s
    float TurnSmoothing = 12.0f;          // Smooth yaw rotation rate (s^-1)
    bool OrientRotationToMovement = true; // Auto-align character yaw towards velocity
    bool EnableSprint = true;             // Toggle Left Shift sprint functionality
    std::string CameraTag = "Main Camera";// Tag used to find camera entity for camera-relative movement
    float JumpCooldown = 0.2f;            // Cooldown between jumps in seconds

    // Virtual / Synthetic input injection (useful for AI, automated tests, cutscenes)
    bool UseVirtualInput = false;
    float VirtualInputX = 0.0f;
    float VirtualInputZ = 0.0f;
    bool VirtualSprint = false;
    bool VirtualJump = false;
    bool DirectVirtualDirection = false;
    glm::vec3 VirtualMoveDirection{ 0.0f };

private:
    float m_JumpTimer = 0.0f;
    bool m_PrevSpace = false;

public:
    void OnCreate() override {
        AXLE_INFO("ThirdPersonController: Attached to entity '{0}'", GetEntityName());
        ApplyTuning();
    }

    void OnUpdate(Axle::Timestep ts) override {
        if (!HasComponent<Axle::CharacterControllerComponent>()) return;
        auto& ccc = GetComponent<Axle::CharacterControllerComponent>();

        float dt = ts.GetSeconds();
        if (m_JumpTimer > 0.0f) {
            m_JumpTimer -= dt;
        }

        // 1. Sync live Inspector parameters to CharacterControllerComponent
        ApplyTuning();

        // 2. Poll directional player inputs (WASD / Arrow Keys or Virtual Input)
        float inputX = 0.0f; // Strafe Right (+1) / Left (-1)
        float inputZ = 0.0f; // Forward (+1) / Backward (-1)
        bool isSprinting = false;
        bool jumpTriggered = false;

        if (UseVirtualInput) {
            inputX = VirtualInputX;
            inputZ = VirtualInputZ;
            isSprinting = EnableSprint && VirtualSprint;
            if (VirtualJump && m_JumpTimer <= 0.0f && ccc.IsGrounded) {
                jumpTriggered = true;
                m_JumpTimer = JumpCooldown;
            }
        } else {
            if (Axle::Input::IsKeyPressed(Axle::KeyCode::W) || Axle::Input::IsKeyPressed(Axle::KeyCode::Up)) {
                inputZ += 1.0f;
            }
            if (Axle::Input::IsKeyPressed(Axle::KeyCode::S) || Axle::Input::IsKeyPressed(Axle::KeyCode::Down)) {
                inputZ -= 1.0f;
            }
            if (Axle::Input::IsKeyPressed(Axle::KeyCode::D) || Axle::Input::IsKeyPressed(Axle::KeyCode::Right)) {
                inputX += 1.0f;
            }
            if (Axle::Input::IsKeyPressed(Axle::KeyCode::A) || Axle::Input::IsKeyPressed(Axle::KeyCode::Left)) {
                inputX -= 1.0f;
            }

            // Sprinting (Left Shift)
            isSprinting = EnableSprint && (Axle::Input::IsKeyPressed(Axle::KeyCode::LeftShift) ||
                                           Axle::Input::IsKeyPressed(Axle::KeyCode::RightShift));

            // Jump (Space key)
            bool spacePressed = Axle::Input::IsKeyPressed(Axle::KeyCode::Space);
            if (spacePressed && !m_PrevSpace && m_JumpTimer <= 0.0f && ccc.IsGrounded) {
                jumpTriggered = true;
                m_JumpTimer = JumpCooldown;
            }
            m_PrevSpace = spacePressed;
        }

        // 3. Transform input into camera-relative world direction
        glm::vec3 wishDir{ 0.0f };
        if (DirectVirtualDirection) {
            wishDir = VirtualMoveDirection;
            float inputMag = glm::length(wishDir);
            if (inputMag > 1e-4f) {
                wishDir /= inputMag;
            } else {
                wishDir = glm::vec3(0.0f);
            }
        } else {
            glm::vec3 camForward{ 0.0f, 0.0f, -1.0f };
            glm::vec3 camRight{ 1.0f, 0.0f, 0.0f };

            // Attempt to find camera orientation
            Axle::Entity camEntity = FindCameraEntity();
            if (camEntity && camEntity.HasComponent<Axle::TransformComponent>()) {
                glm::mat4 camWorld = camEntity.GetWorldTransform();
                glm::quat camRot = glm::quat_cast(camWorld);

                // Forward in OpenGL/AxleEngine is -Z
                camForward = glm::normalize(camRot * glm::vec3(0.0f, 0.0f, -1.0f));
                camForward.y = 0.0f;
                if (glm::length(camForward) > 1e-4f) {
                    camForward = glm::normalize(camForward);
                } else {
                    camForward = glm::vec3(0.0f, 0.0f, -1.0f);
                }

                camRight = glm::normalize(camRot * glm::vec3(1.0f, 0.0f, 0.0f));
                camRight.y = 0.0f;
                if (glm::length(camRight) > 1e-4f) {
                    camRight = glm::normalize(camRight);
                } else {
                    camRight = glm::vec3(1.0f, 0.0f, 0.0f);
                }
            }

            wishDir = camForward * inputZ + camRight * inputX;
            float inputMag = glm::length(wishDir);
            if (inputMag > 1e-4f) {
                wishDir /= inputMag;
            } else {
                wishDir = glm::vec3(0.0f);
            }
        }

        // 4. Dispatch to CharacterControllerComponent
        ccc.MoveDirection = wishDir;
        ccc.IsRunning = isSprinting;
        if (jumpTriggered) {
            ccc.JumpRequested = true;
        }

        // 5. Forward state to AnimatorComponent if present
        if (HasComponent<Axle::AnimatorComponent>()) {
            auto& ac = GetComponent<Axle::AnimatorComponent>();
            ac.SetFloat("Speed", ccc.CurrentSpeed);
            ac.SetBool("IsGrounded", ccc.IsGrounded);
            ac.SetFloat("VerticalVelocity", ccc.VerticalVelocity);
            if (jumpTriggered) {
                ac.SetTrigger("Jump");
            }
        }
    }

    void OnDestroy() override {
        AXLE_INFO("ThirdPersonController: Detached from entity '{0}'", GetEntityName());
    }

    void ApplyTuning() {
        if (!HasComponent<Axle::CharacterControllerComponent>()) return;
        auto& ccc = GetComponent<Axle::CharacterControllerComponent>();
        ccc.WalkSpeed = WalkSpeed;
        ccc.RunSpeed = RunSpeed;
        ccc.JumpSpeed = JumpSpeed;
        ccc.TurnSmoothing = TurnSmoothing;
        ccc.OrientRotationToMovement = OrientRotationToMovement;
    }

    std::string GetEntityName() const {
        return HasComponent<Axle::TagComponent>() ? GetComponent<Axle::TagComponent>().Tag : "Character";
    }

private:
    Axle::Entity FindCameraEntity() {
        if (GetEntity() && GetEntity().GetScene()) {
            auto* scene = GetEntity().GetScene();
            auto view = scene->GetRegistry().view<Axle::TagComponent, Axle::CameraComponent>();
            for (auto ent : view) {
                if (view.get<Axle::TagComponent>(ent).Tag == CameraTag) {
                    return Axle::Entity{ ent, scene };
                }
            }
            // Fallback: Primary Camera Entity
            return scene->GetPrimaryCameraEntity();
        }
        return {};
    }

public:
    AXLE_SCRIPT_FIELDS(ThirdPersonController,
        AXLE_FIELD(WalkSpeed, 0.5f, 15.0f, "Standard walking speed in m/s"),
        AXLE_FIELD(RunSpeed, 1.0f, 30.0f, "Sprinting / running speed in m/s"),
        AXLE_FIELD(JumpSpeed, 1.0f, 20.0f, "Vertical jump impulse speed in m/s"),
        AXLE_FIELD(TurnSmoothing, 1.0f, 30.0f, "Character smooth yaw turn rate"),
        AXLE_FIELD(OrientRotationToMovement, "Align character orientation towards move direction"),
        AXLE_FIELD(EnableSprint, "Enable sprint with Left Shift"),
        AXLE_FIELD(CameraTag, "Tag of camera entity for camera-relative movement")
    )
};

AXLE_REGISTER_SCRIPT(ThirdPersonController);
