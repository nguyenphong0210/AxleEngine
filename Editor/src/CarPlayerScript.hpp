#pragma once
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/Vehicle.hpp>

class CarPlayerScript : public Axle::ScriptableEntity {
public:
    float ForwardTorque = 1500.0f;
    float MaxSteerAngle = 35.0f;
    float BrakeTorque = 2500.0f;
    bool EnableBoost = true;

    void OnCreate() override {
    }

    void OnUpdate(Axle::Timestep ts) override {
        (void)ts;
        if (!HasComponent<Axle::VehicleComponent>()) return;
        auto& vc = GetComponent<Axle::VehicleComponent>();
        if (!vc.VehicleInstance) return;

        Axle::f32 forwardInput = 0.0f;
        Axle::f32 rightInput = 0.0f;
        Axle::f32 brakeInput = 0.0f;
        Axle::f32 handBrakeInput = 0.0f;

        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Up) || Axle::Input::IsKeyPressed(Axle::KeyCode::W)) {
            forwardInput += 1.0f;
        }
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Down) || Axle::Input::IsKeyPressed(Axle::KeyCode::S)) {
            forwardInput -= 1.0f;
        }
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Left) || Axle::Input::IsKeyPressed(Axle::KeyCode::A)) {
            rightInput -= 1.0f;
        }
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Right) || Axle::Input::IsKeyPressed(Axle::KeyCode::D)) {
            rightInput += 1.0f;
        }
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Space)) {
            handBrakeInput = 1.0f;
        }

        vc.VehicleInstance->SetDriverInput(forwardInput, rightInput, brakeInput, handBrakeInput);
    }

    void OnDestroy() override {
    }

    static void RegisterFields(Axle::ScriptFieldRegistry& reg) {
        reg.AddField("ForwardTorque", &CarPlayerScript::ForwardTorque, 100.0f, 10000.0f, "Engine drive torque in N.m");
        reg.AddField("MaxSteerAngle", &CarPlayerScript::MaxSteerAngle, 10.0f, 60.0f, "Maximum steering angle in degrees");
        reg.AddField("BrakeTorque", &CarPlayerScript::BrakeTorque, 100.0f, 10000.0f, "Braking force in N.m");
        reg.AddField("EnableBoost", &CarPlayerScript::EnableBoost, "Enable speed boost");
    }
};

AXLE_REGISTER_SCRIPT(CarPlayerScript);

