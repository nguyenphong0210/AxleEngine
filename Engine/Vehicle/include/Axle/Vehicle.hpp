#pragma once
#include <Axle/Types.hpp>
#include <Axle/VehicleConfig.hpp>
#include <Axle/VehicleTelemetry.hpp>
#include <Axle/PhysicsScene.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace Axle {

    class Vehicle {
    public:
        Vehicle(PhysicsScene& scene, const VehicleConfig& config = VehicleConfig());
        ~Vehicle();

        // Driving controls input
        void SetDriverInput(f32 forward, f32 right, f32 brake, f32 handBrake);

        // Reset vehicle to specified position and orientation
        void Reset(const glm::vec3& position, const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

        // State & transform queries
        VehicleTelemetryData GetTelemetry() const;
        glm::mat4 GetChassisTransform() const;
        glm::mat4 GetWheelTransform(u32 wheelIndex) const;
        glm::vec3 GetPosition() const;
        glm::vec3 GetForwardVector() const;

        BodyID GetChassisBodyID() const { return m_ChassisBodyID; }
        const VehicleConfig& GetConfig() const { return m_Config; }

        JPH::VehicleConstraint* GetConstraint() { return m_Constraint.GetPtr(); }
        const JPH::VehicleConstraint* GetConstraint() const { return m_Constraint.GetPtr(); }

    private:
        PhysicsScene* m_PhysicsScene = nullptr;
        VehicleConfig m_Config;

        BodyID m_ChassisBodyID;
        JPH::Ref<JPH::VehicleConstraint> m_Constraint;
        JPH::WheeledVehicleController* m_Controller = nullptr;
        JPH::RefConst<JPH::VehicleCollisionTester> m_CollisionTester;
    };

}
