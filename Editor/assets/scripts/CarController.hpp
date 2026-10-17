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

    // Vehicle Lighting Fields
    bool HeadlightsEnabled = true;   // Headlights on/off (toggle with 'L')
    bool AutoBrakeLights = true;     // Dynamically glow brake lights when decelerating
    float HeadlightIntensity = 15.0f;
    float TaillightIdleIntensity = 1.0f;
    float TaillightBrakeIntensity = 6.0f;

private:
    float m_CurrentSteering = 0.0f;
    bool m_WasInputActive = false;
    bool m_PrevKeyL = false;

public:
    void OnCreate() override {
        AXLE_INFO("CarController: Attached to entity '{0}' (Input window: {1})", GetEntityName(), (void*)Axle::Input::GetContext());
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

        // Automatic transmission brake vs reverse logic:
        // - If moving forward (> 2.0 km/h) and pressing backward (S / Down), apply service brakes
        // - If moving backward (< -2.0 km/h) and pressing forward (W / Up), apply service brakes
        // - When nearly stopped (|forwardSpeed| <= 2.0 km/h), allow direct throttle into forward or reverse gear
        const float forwardSpeed = vc.GetForwardSpeedKmh();
        if (targetThrottle < 0.0f && forwardSpeed > 2.0f) {
            targetBrake = std::abs(targetThrottle);
            targetThrottle = 0.0f;
        } else if (targetThrottle > 0.0f && forwardSpeed < -2.0f) {
            targetBrake = targetThrottle;
            targetThrottle = 0.0f;
        }

        // 3. Dispatch to ECS VehicleComponent
        const bool inputActive = (targetThrottle != 0.0f || std::abs(m_CurrentSteering) > 0.05f || targetBrake > 0.0f || handbrake);
        if (inputActive || m_WasInputActive) {
            vc.SetDriverInput(targetThrottle, m_CurrentSteering, targetBrake, handbrake);
        }

        // Log driver input state change for live console feedback
        if (inputActive && !m_WasInputActive) {
            AXLE_INFO("CarController: Driver input active [Throttle: {0:.1f}, Steer: {1:.2f}, Brake: {2:.1f}, Handbrake: {3}]",
                targetThrottle, m_CurrentSteering, targetBrake, handbrake ? "YES" : "NO");
            m_WasInputActive = true;
        } else if (!inputActive && m_WasInputActive) {
            m_WasInputActive = false;
        }

        // 4. Vehicle Lighting Inputs & Dynamic Actuation
        bool currentKeyL = Axle::Input::IsKeyPressed(Axle::KeyCode::L);
        if (currentKeyL && !m_PrevKeyL) {
            HeadlightsEnabled = !HeadlightsEnabled;
            AXLE_INFO("CarController: Headlights toggled -> {0}", HeadlightsEnabled ? "ON" : "OFF");
        }
        m_PrevKeyL = currentKeyL;

        const bool isBraking = (targetBrake > 0.05f || handbrake);

        // 5. Update child entities (Wheels, Headlights, Taillights, Emission)
        for (auto child : GetEntity().GetChildren()) {
            if (!child) continue;

            // Wheels: Steer angle & AWD/RWD distribution
            if (child.HasComponent<Axle::WheelComponent>()) {
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

            // Headlights: SpotLightComponent actuation
            if (child.HasComponent<Axle::SpotLightComponent>()) {
                auto& slc = child.GetComponent<Axle::SpotLightComponent>();
                slc.Enabled = HeadlightsEnabled;
                slc.Intensity = HeadlightIntensity;
            }

            // Taillights / Brake lights: PointLightComponent actuation
            if (child.HasComponent<Axle::PointLightComponent>()) {
                auto& plc = child.GetComponent<Axle::PointLightComponent>();
                if (AutoBrakeLights) {
                    plc.Intensity = isBraking ? TaillightBrakeIntensity : TaillightIdleIntensity;
                }
            }

            // Material emission synchronisation for visual glow
            if (child.HasComponent<Axle::TagComponent>() && child.HasComponent<Axle::MaterialComponent>()) {
                const auto& tag = child.GetComponent<Axle::TagComponent>().Tag;
                auto& mat = child.GetComponent<Axle::MaterialComponent>();
                if (tag.find("Taillight") != std::string::npos || tag.find("BrakeLight") != std::string::npos) {
                    mat.EmissionIntensity = isBraking ? TaillightBrakeIntensity : TaillightIdleIntensity;
                } else if (tag.find("Headlight") != std::string::npos) {
                    mat.EmissionIntensity = HeadlightsEnabled ? 5.0f : 0.0f;
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
        AXLE_FIELD(EnableBoost, "Enable nitro speed boost"),
        AXLE_FIELD(HeadlightsEnabled, "Toggle vehicle headlights"),
        AXLE_FIELD(AutoBrakeLights, "Dynamic brake lights on deceleration")
    )
};

AXLE_REGISTER_SCRIPT(CarController);
