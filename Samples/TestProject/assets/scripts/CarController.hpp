#pragma once
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/Log.hpp>
#include <algorithm>
#include <cmath>

class CarController : public Axle::ScriptableEntity {
public:
    // Live Inspector Tunable Fields
    float Horsepower = 450.0f;       // Engine power (translates to MaxEngineTorque ~ Horsepower * 2.2f)
    float MaxSteerAngle = 35.0f;     // Maximum wheel steering angle in degrees
    float BrakePower = 4000.0f;      // Braking force in N.m
    bool AllWheelDrive = true;       // Toggle AWD vs RWD
    float SteerSpeed = 4.5f;         // Steering responsiveness
    float BoostMultiplier = 1.5f;    // Nitro boost factor
    bool EnableBoost = true;

private:
    float m_CurrentSteering = 0.0f;

public:
    void OnCreate() override {
        AXLE_INFO("CarController: Attached to entity '{0}'", GetEntityName());
        ApplyTuning();
    }

    void OnUpdate(Axle::Timestep ts) override {
        if (!HasComponent<Axle::VehicleComponent>()) return;
        auto& vc = GetComponent<Axle::VehicleComponent>();

        // 1. Sync live Inspector fields to VehicleComponent drivetrain
        ApplyTuning();

        // 2. Poll Driver Inputs (Keyboard WASD / Arrows / Space / Shift)
        float targetThrottle = 0.0f;
        float targetSteer = 0.0f;
        float targetBrake = 0.0f;
        bool handbrake = false;

        // Forward / Reverse
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::W) || Axle::Input::IsKeyPressed(Axle::KeyCode::Up)) {
            targetThrottle += 1.0f;
        }
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::S) || Axle::Input::IsKeyPressed(Axle::KeyCode::Down)) {
            targetThrottle -= 1.0f;
        }

        // Steering
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::A) || Axle::Input::IsKeyPressed(Axle::KeyCode::Left)) {
            targetSteer -= 1.0f;
        }
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::D) || Axle::Input::IsKeyPressed(Axle::KeyCode::Right)) {
            targetSteer += 1.0f;
        }

        // Handbrake
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Space)) {
            handbrake = true;
        }

        // Boost / Nitro (Left Shift)
        if (EnableBoost && Axle::Input::IsKeyPressed(Axle::KeyCode::LeftShift)) {
            targetThrottle *= BoostMultiplier;
        }

        // Smooth steering response
        float dt = ts.GetSeconds();
        m_CurrentSteering += (targetSteer - m_CurrentSteering) * std::min(1.0f, dt * SteerSpeed);

        // Reverse gear handling: if moving forward and pressing backward, brake first
        if (targetThrottle < 0.0f && vc.GetSpeedKmh() > 2.0f) {
            targetBrake = std::abs(targetThrottle);
            targetThrottle = 0.0f;
        }

        // 3. Dispatch to ECS VehicleComponent
        vc.SetDriverInput(targetThrottle, m_CurrentSteering, targetBrake, handbrake);

        // 4. Update child wheel entities' MaxSteerAngle if steerable
        for (auto child : GetEntity().GetChildren()) {
            if (child && child.HasComponent<Axle::WheelComponent>()) {
                auto& wc = child.GetComponent<Axle::WheelComponent>();
                if (wc.CanSteer) {
                    wc.MaxSteerAngle = MaxSteerAngle;
                }
                if (!AllWheelDrive && wc.IsDriven) {
                    if (child.HasComponent<Axle::TransformComponent>()) {
                        const auto& wt = child.GetComponent<Axle::TransformComponent>();
                        wc.IsDriven = (wt.Translation.z < 0.0f);
                    }
                }
            }
        }
    }

    void OnDestroy() override {
        AXLE_INFO("CarController: Detached from entity '{0}'", GetEntityName());
    }

    void ApplyTuning() {
        if (!HasComponent<Axle::VehicleComponent>()) return;
        auto& vc = GetComponent<Axle::VehicleComponent>();
        vc.MaxEngineTorque = Horsepower * 2.2f;
        vc.MaxBrakeTorque = BrakePower;
        vc.FourWheelDrive = AllWheelDrive;
    }

    std::string GetEntityName() const {
        return HasComponent<Axle::TagComponent>() ? GetComponent<Axle::TagComponent>().Tag : "Car";
    }

    AXLE_SCRIPT_FIELDS(CarController,
        AXLE_FIELD(Horsepower, 50.0f, 2500.0f, "Engine horsepower rating (affects max drive torque)"),
        AXLE_FIELD(MaxSteerAngle, 10.0f, 55.0f, "Maximum front wheel steer angle in degrees"),
        AXLE_FIELD(BrakePower, 200.0f, 20000.0f, "Main service braking force in N.m"),
        AXLE_FIELD(AllWheelDrive, "Enable All-Wheel Drive (AWD) vs Rear-Wheel Drive (RWD)"),
        AXLE_FIELD(SteerSpeed, 1.0f, 20.0f, "Steering wheel response speed"),
        AXLE_FIELD(BoostMultiplier, 1.0f, 3.0f, "Nitro boost multiplier on Left Shift"),
        AXLE_FIELD(EnableBoost, "Enable nitro speed boost")
    )
};

AXLE_REGISTER_SCRIPT(CarController);
