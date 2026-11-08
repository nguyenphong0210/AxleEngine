#pragma once

#include "AxleVehicle/VehicleComponents.hpp"
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsMath.hpp>
#include <Axle/PhysicsLayers.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>

#include <string>
#include <cstdint>

namespace AxleVehicle {

struct VehicleInitParams {
    float InitialPosition[3] = { 0.0f, 1.0f, 0.0f };
    float InitialRotation[4] = { 1.0f, 0.0f, 0.0f, 0.0f }; // w, x, y, z
    ChassisConfig Chassis;
    WheelConfig Wheels[4];
};

class VehiclePhysics {
public:
    VehiclePhysics() = default;
    ~VehiclePhysics();

    VehiclePhysics(const VehiclePhysics&) = delete;
    VehiclePhysics& operator=(const VehiclePhysics&) = delete;
    VehiclePhysics(VehiclePhysics&& other) noexcept;
    VehiclePhysics& operator=(VehiclePhysics&& other) noexcept;

    // Creation with automatic rollback on malformed input
    bool Initialize(Axle::PhysicsScene& scene, const VehicleInitParams& params, std::string* outError = nullptr);
    void Teardown();

    bool IsValid() const { return m_Constraint != nullptr && !m_ChassisBodyID.IsInvalid(); }

    // Driver controls
    void SetDriverInput(float forward, float right, float brake, bool handbrake);

    // Dynamic reset to initial or target transform
    void Reset(const float position[3], const float rotation[4]);

    // Telemetry and state synchronization
    void Synchronize(ChassisRuntimeState& outChassis, WheelRuntimeState outWheels[4]) const;

    // Wheel world matrix (column-major 4x4)
    bool GetWheelWorldTransform(std::uint32_t wheelIndex, float outMatrix16[16]) const;

    // Accessors
    Axle::BodyID GetChassisBodyID() const { return m_ChassisBodyID; }
    JPH::VehicleConstraint* GetConstraint() { return m_Constraint.GetPtr(); }
    const JPH::VehicleConstraint* GetConstraint() const { return m_Constraint.GetPtr(); }
    JPH::WheeledVehicleController* GetController() { return m_Controller; }
    const JPH::WheeledVehicleController* GetController() const { return m_Controller; }

private:
    Axle::PhysicsScene* m_PhysicsScene = nullptr;
    Axle::BodyID m_ChassisBodyID;
    JPH::Ref<JPH::VehicleConstraint> m_Constraint;
    JPH::Ref<JPH::VehicleCollisionTester> m_CollisionTester;
    JPH::WheeledVehicleController* m_Controller = nullptr;
    ChassisConfig m_ChassisConfig{};
    WheelConfig m_WheelConfigs[4]{};
};

} // namespace AxleVehicle
