#pragma once
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/Log.hpp>
#include <Axle/Vehicle.hpp>
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

class VehicleChaseCamera : public Axle::ScriptableEntity {
public:
    // Live Inspector Tunable Fields
    float Distance = 7.5f;              // Orbit distance from chase target in meters
    float HeightOffset = 0.8f;          // Target focus point height above vehicle center
    float MinElevation = 0.4f;          // Minimum world Y height to prevent ground clipping
    float ElevationOffset = 1.5f;       // Vertical lift added to orbit position
    float MouseSensitivity = 0.25f;     // Mouse look rotation sensitivity
    float AutoAlignSpeed = 3.5f;        // Responsive auto-alignment behind vehicle

private:
    float m_OrbitYaw = 180.0f;
    float m_OrbitPitch = 15.0f;
    glm::vec2 m_LastMousePos{ 0.0f, 0.0f };
    bool m_FirstMouseUpdate = true;

public:
    void OnCreate() override {
        AXLE_INFO("VehicleChaseCamera: Attached to entity '{0}'", GetEntityName());
        if (HasComponent<Axle::TransformComponent>()) {
            const auto& tc = GetComponent<Axle::TransformComponent>();
            glm::vec3 euler = glm::degrees(glm::eulerAngles(tc.Rotation));
            m_OrbitPitch = std::clamp(euler.x, -45.0f, 75.0f);
            m_OrbitYaw = euler.y;
        }
    }

    void OnUpdate(Axle::Timestep ts) override {
        Axle::Scene* scene = GetEntity().GetScene();
        if (!scene) return;

        // Find primary target entity holding a VehicleComponent
        Axle::Entity target;
        auto vehicleView = scene->GetRegistry().view<Axle::VehicleComponent>();
        for (auto entityID : vehicleView) {
            target = Axle::Entity{ entityID, scene };
            break;
        }
        if (!target) return;

        // 1. Process Mouse Look Drag (Orbit Yaw & Pitch)
        const auto [mouseX, mouseY] = Axle::Input::GetMousePosition();
        const glm::vec2 currentMousePos{ mouseX, mouseY };

        if (m_FirstMouseUpdate) {
            m_LastMousePos = currentMousePos;
            m_FirstMouseUpdate = false;
        }

        const glm::vec2 mouseDelta = currentMousePos - m_LastMousePos;
        m_LastMousePos = currentMousePos;

        const bool isDragging = Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight)
                             || Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonLeft);

        if (isDragging) {
            m_OrbitYaw -= mouseDelta.x * MouseSensitivity;
            m_OrbitPitch -= mouseDelta.y * MouseSensitivity;
            m_OrbitPitch = std::clamp(m_OrbitPitch, -45.0f, 75.0f);
        }

        // 2. Query target vehicle position, forward heading, and velocity
        glm::vec3 targetPos{ 0.0f };
        glm::vec3 targetFwd{ 0.0f, 0.0f, 1.0f };
        float targetSpeed = 0.0f;

        auto* runtimeVehicle = scene->GetRuntimeVehicle(target);
        if (runtimeVehicle) {
            targetPos = runtimeVehicle->GetPosition();
            targetFwd = runtimeVehicle->GetForwardVector();
            targetSpeed = runtimeVehicle->GetSpeedKmh() / 3.6f;
        } else if (target.HasComponent<Axle::TransformComponent>()) {
            const auto& tc = target.GetComponent<Axle::TransformComponent>();
            targetPos = tc.Translation;
            targetFwd = tc.Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        }

        // 3. Smooth auto-align behind vehicle when moving forward
        if (!isDragging && targetSpeed > 0.5f && AutoAlignSpeed > 0.0f) {
            const float targetYaw = glm::degrees(std::atan2(-targetFwd.x, -targetFwd.z));
            float diff = targetYaw - m_OrbitYaw;
            while (diff < -180.0f) diff += 360.0f;
            while (diff > 180.0f) diff -= 360.0f;
            m_OrbitYaw += diff * std::min(1.0f, ts.GetSeconds() * AutoAlignSpeed);
        }

        // 4. Calculate orbital camera world position
        const float radYaw = glm::radians(m_OrbitYaw);
        const float radPitch = glm::radians(m_OrbitPitch);

        const glm::vec3 lookTarget = targetPos + glm::vec3(0.0f, HeightOffset, 0.0f);

        glm::vec3 camPos;
        camPos.x = lookTarget.x + std::sin(radYaw) * std::cos(radPitch) * Distance;
        camPos.y = std::max(MinElevation, lookTarget.y + std::sin(radPitch) * Distance + ElevationOffset);
        camPos.z = lookTarget.z + std::cos(radYaw) * std::cos(radPitch) * Distance;

        const glm::vec3 lookDir = glm::normalize(lookTarget - camPos);

        const float pitch = glm::degrees(std::asin(std::clamp(lookDir.y, -0.999f, 0.999f)));
        const float yaw = glm::degrees(std::atan2(-lookDir.x, -lookDir.z));

        // 5. Update Camera Entity Transform
        if (HasComponent<Axle::TransformComponent>()) {
            auto& tc = GetComponent<Axle::TransformComponent>();
            tc.Translation = camPos;
            tc.Rotation = glm::quat(glm::vec3(glm::radians(pitch), glm::radians(yaw), 0.0f));
        }
    }

    void OnDestroy() override {
        AXLE_INFO("VehicleChaseCamera: Detached from entity '{0}'", GetEntityName());
    }

    std::string GetEntityName() const {
        return HasComponent<Axle::TagComponent>() ? GetComponent<Axle::TagComponent>().Tag : "Camera";
    }

    AXLE_SCRIPT_FIELDS(VehicleChaseCamera,
        AXLE_FIELD(Distance, 2.0f, 30.0f, "Distance from chase target"),
        AXLE_FIELD(HeightOffset, -2.0f, 5.0f, "Height offset of focus point above target"),
        AXLE_FIELD(MinElevation, 0.0f, 5.0f, "Minimum camera height above ground"),
        AXLE_FIELD(ElevationOffset, 0.0f, 5.0f, "Vertical lift added to orbit position"),
        AXLE_FIELD(MouseSensitivity, 0.05f, 2.0f, "Mouse drag rotation sensitivity"),
        AXLE_FIELD(AutoAlignSpeed, 0.0f, 15.0f, "Speed to auto-align behind vehicle when driving")
    )
};

AXLE_REGISTER_SCRIPT(VehicleChaseCamera);
