#pragma once
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Log.hpp>
#include <glm/gtc/quaternion.hpp>

class RotatorScript : public Axle::ScriptableEntity {
public:
    float RotationSpeed = 90.0f; // degrees per second
    glm::vec3 Axis = glm::vec3(0.0f, 1.0f, 0.0f);
    bool Enabled = true;

    void OnCreate() override {
        AXLE_INFO("RotatorScript::OnCreate() initialized for Entity '{0}'", GetEntity().GetName());
    }

    void OnUpdate(Axle::Timestep ts) override {
        if (!Enabled) return;
        auto& tc = GetComponent<Axle::TransformComponent>();
        float deltaAngle = glm::radians(RotationSpeed * (float)ts);
        glm::quat deltaRot = glm::angleAxis(deltaAngle, glm::normalize(Axis));
        tc.Rotation = deltaRot * tc.Rotation;
    }

    void OnDestroy() override {
        AXLE_INFO("RotatorScript::OnDestroy() called for Entity '{0}'", GetEntity().GetName());
    }

    AXLE_SCRIPT_FIELDS(RotatorScript,
        AXLE_FIELD(RotationSpeed, -360.0f, 360.0f, "Rotation speed in deg/s"),
        AXLE_FIELD(Axis, "Rotation axis vector"),
        AXLE_FIELD(Enabled, "Enable continuous rotation")
    )
};

AXLE_REGISTER_SCRIPT(RotatorScript);
