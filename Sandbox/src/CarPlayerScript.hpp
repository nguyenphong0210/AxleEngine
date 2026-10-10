#pragma once
#include <Axle/ScriptableEntity.hpp>
#include <Axle/Components.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/Vehicle.hpp>

class CarPlayerScript : public Axle::ScriptableEntity {
public:
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
};
