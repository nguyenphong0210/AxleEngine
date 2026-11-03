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
#include <glm/gtc/matrix_transform.hpp>

class ThirdPersonCamera : public Axle::ScriptableEntity {
public:
    // --- Live Inspector Tunable Fields ---
    std::string TargetTag = "Player";    // Tag of the character entity to follow
    float Distance = 3.5f;               // Spring-arm distance in meters
    float TargetHeightOffset = 1.4f;     // Vertical height above target base to focus on
    float MinElevation = 0.3f;           // Minimum world Y height to prevent ground clipping
    float MouseSensitivity = 0.20f;      // Mouse look rotation sensitivity
    float PositionLag = 12.0f;           // Position smoothing factor (s^-1)
    float RotationLag = 15.0f;           // Rotation smoothing factor (s^-1)
    float MinPitch = -35.0f;             // Minimum pitch angle (degrees)
    float MaxPitch = 75.0f;              // Maximum pitch angle (degrees)
    float FieldOfView = 65.0f;           // Camera FOV in degrees

private:
    float m_Yaw = 0.0f;                  // Orbit yaw angle in degrees
    float m_Pitch = 15.0f;               // Orbit pitch angle in degrees
    glm::vec3 m_CurrentPosition{ 0.0f, 2.0f, 5.0f };
    glm::quat m_CurrentRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    bool m_FirstUpdate = true;
    glm::vec2 m_PrevMousePos{ 0.0f };

public:
    void OnCreate() override {
        AXLE_INFO("ThirdPersonCamera: Attached to entity '{0}'", GetEntityName());
        m_PrevMousePos = glm::vec2(Axle::Input::GetMouseX(), Axle::Input::GetMouseY());
    }

    void OnUpdate(Axle::Timestep ts) override {
        if (!HasComponent<Axle::TransformComponent>()) return;
        auto& tc = GetComponent<Axle::TransformComponent>();

        float dt = ts.GetSeconds();
        if (dt <= 0.0f) return;

        // 1. Process Mouse Look Orbit Input
        glm::vec2 currentMousePos(Axle::Input::GetMouseX(), Axle::Input::GetMouseY());
        if (m_FirstUpdate) {
            m_PrevMousePos = currentMousePos;
            m_CurrentPosition = tc.Translation;
            m_CurrentRotation = tc.Rotation;
            m_FirstUpdate = false;
        }

        glm::vec2 mouseDelta = currentMousePos - m_PrevMousePos;
        m_PrevMousePos = currentMousePos;

        // Update yaw and pitch if right mouse button is held, or if cursor is locked
        bool allowMouseLook = Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight) ||
                              Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonLeft);

        if (allowMouseLook) {
            m_Yaw += mouseDelta.x * MouseSensitivity;
            m_Pitch -= mouseDelta.y * MouseSensitivity;
            m_Pitch = std::clamp(m_Pitch, MinPitch, MaxPitch);
        }

        // 2. Find Chase Target Entity
        Axle::Entity target = FindTargetEntity();
        glm::vec3 targetBasePos(0.0f);
        if (target && target.HasComponent<Axle::TransformComponent>()) {
            targetBasePos = glm::vec3(target.GetWorldTransform()[3]);
        }

        glm::vec3 targetFocus = targetBasePos + glm::vec3(0.0f, TargetHeightOffset, 0.0f);

        // 3. Compute Desired Camera Orbit Position
        float pitchRad = glm::radians(m_Pitch);
        float yawRad = glm::radians(m_Yaw);

        // Camera offset relative to focus point
        glm::vec3 offset(
            std::cos(pitchRad) * std::sin(yawRad) * Distance,
            std::sin(pitchRad) * Distance,
            std::cos(pitchRad) * std::cos(yawRad) * Distance
        );

        glm::vec3 desiredPos = targetFocus + offset;
        if (desiredPos.y < MinElevation) {
            desiredPos.y = MinElevation;
        }

        // 4. Smooth Lag Interpolation (Spring-Arm)
        float posAlpha = std::min(1.0f, dt * PositionLag);
        m_CurrentPosition = glm::mix(m_CurrentPosition, desiredPos, posAlpha);

        // 5. Compute Look-At Rotation
        glm::vec3 lookDir = targetFocus - m_CurrentPosition;
        if (glm::length(lookDir) > 1e-4f) {
            lookDir = glm::normalize(lookDir);

            // Compute pitch and yaw from lookDir
            float lookYaw = std::atan2(-lookDir.x, -lookDir.z);
            float lookPitch = std::asin(std::clamp(lookDir.y, -1.0f, 1.0f));

            glm::quat targetRot = glm::angleAxis(lookYaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                  glm::angleAxis(lookPitch, glm::vec3(1.0f, 0.0f, 0.0f));

            float rotAlpha = std::min(1.0f, dt * RotationLag);
            m_CurrentRotation = glm::slerp(m_CurrentRotation, targetRot, rotAlpha);
        }

        // 6. Write final transforms to Camera entity
        tc.Translation = m_CurrentPosition;
        tc.Rotation = m_CurrentRotation;

        // Sync Camera FOV if CameraComponent is present
        if (HasComponent<Axle::CameraComponent>()) {
            auto& cc = GetComponent<Axle::CameraComponent>();
            cc.FOV = FieldOfView;
        }
    }

    void OnDestroy() override {
        AXLE_INFO("ThirdPersonCamera: Detached from entity '{0}'", GetEntityName());
    }

    void SetYawPitch(float yawDeg, float pitchDeg) {
        m_Yaw = yawDeg;
        m_Pitch = std::clamp(pitchDeg, MinPitch, MaxPitch);
    }

    float GetYaw() const { return m_Yaw; }
    float GetPitch() const { return m_Pitch; }

    std::string GetEntityName() const {
        return HasComponent<Axle::TagComponent>() ? GetComponent<Axle::TagComponent>().Tag : "Camera";
    }

private:
    Axle::Entity FindTargetEntity() {
        if (GetEntity() && GetEntity().GetScene()) {
            auto* scene = GetEntity().GetScene();

            // First priority: Match by TargetTag
            auto tagView = scene->GetRegistry().view<Axle::TagComponent>();
            for (auto ent : tagView) {
                if (tagView.get<Axle::TagComponent>(ent).Tag == TargetTag) {
                    return Axle::Entity{ ent, scene };
                }
            }

            // Second priority: Any entity holding CharacterControllerComponent
            auto ccView = scene->GetRegistry().view<Axle::CharacterControllerComponent>();
            for (auto ent : ccView) {
                return Axle::Entity{ ent, scene };
            }
        }
        return {};
    }

public:
    AXLE_SCRIPT_FIELDS(ThirdPersonCamera,
        AXLE_FIELD(TargetTag, "Tag of the target character entity to follow"),
        AXLE_FIELD(Distance, 1.0f, 15.0f, "Spring-arm camera distance in meters"),
        AXLE_FIELD(TargetHeightOffset, 0.0f, 3.0f, "Focus point vertical elevation above character base"),
        AXLE_FIELD(MinElevation, 0.0f, 5.0f, "Minimum camera world height to prevent ground clipping"),
        AXLE_FIELD(MouseSensitivity, 0.01f, 1.0f, "Mouse look orbit sensitivity"),
        AXLE_FIELD(PositionLag, 1.0f, 30.0f, "Spring-arm position tracking smoothing speed"),
        AXLE_FIELD(RotationLag, 1.0f, 30.0f, "Look-at rotation smoothing speed"),
        AXLE_FIELD(MinPitch, -80.0f, 0.0f, "Minimum pitch limit in degrees"),
        AXLE_FIELD(MaxPitch, 0.0f, 85.0f, "Maximum pitch limit in degrees"),
        AXLE_FIELD(FieldOfView, 30.0f, 120.0f, "Camera perspective field of view in degrees")
    )
};

AXLE_REGISTER_SCRIPT(ThirdPersonCamera);
