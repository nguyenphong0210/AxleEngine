#include "AxleVehicle/VehicleComponents.hpp"
#include "AxleVehicle/VehiclePhysics.hpp"
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsLayers.hpp>
#include <Axle/Timestep.hpp>

#include <iostream>
#include <cassert>
#include <cmath>
#include <limits>

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK FAILED: " #expr " at " __FILE__ ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (false)

using namespace AxleVehicle;

int main(int argc, char** argv) {
    std::cout << "[Test_VehiclePhysics] Starting PLG-15 Jolt vehicle physics verification..." << std::endl;

    // 1. Setup Headless Physics World with ground plane
    Axle::PhysicsScene scene;
    const Axle::BodyID groundID = scene.CreatePlane(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        250.0f
    );
    CHECK(!groundID.IsInvalid());
    const std::uint32_t baselineBodies = scene.GetNumBodies();
    CHECK(baselineBodies >= 1);

    // 2. Configure 4-Wheel Vehicle
    VehicleInitParams params;
    params.InitialPosition[0] = 0.0f;
    params.InitialPosition[1] = 1.0f; // Drop slightly above ground
    params.InitialPosition[2] = 0.0f;
    params.InitialRotation[0] = 1.0f; // w
    params.InitialRotation[1] = 0.0f;
    params.InitialRotation[2] = 0.0f;
    params.InitialRotation[3] = 0.0f;

    params.Chassis.HalfExtents[0] = 0.9f;
    params.Chassis.HalfExtents[1] = 0.25f;
    params.Chassis.HalfExtents[2] = 2.0f;
    params.Chassis.Mass = 1500.0f;
    params.Chassis.CenterOfMassOffset[1] = -0.1f;
    params.Chassis.MaxEngineTorque = 600.0f;
    params.Chassis.MinRPM = 1000.0f;
    params.Chassis.MaxRPM = 7000.0f;
    params.Chassis.MaxSteerAngle = 35.0f;
    params.Chassis.MaxBrakeTorque = 3500.0f;
    params.Chassis.MaxHandbrakeTorque = 7000.0f;
    params.Chassis.FourWheelDrive = true;

    for (std::uint32_t i = 0; i < 4; ++i) {
        params.Wheels[i].WheelIndex = i;
        params.Wheels[i].Radius = 0.35f;
        params.Wheels[i].Width = 0.25f;
        params.Wheels[i].SuspensionMinLength = 0.15f;
        params.Wheels[i].SuspensionMaxLength = 0.45f;
        params.Wheels[i].SpringFrequency = 2.5f;
        params.Wheels[i].SpringDamping = 0.6f;
        params.Wheels[i].CanSteer = (i < 2); // FL and FR steer
        params.Wheels[i].IsDriven = true;
        params.Wheels[i].BrakeRatio = 1.0f;
        params.Wheels[i].HandbrakeRatio = (i >= 2) ? 1.0f : 0.0f; // RL and RR handbrake
    }

    VehiclePhysics vehicle;
    std::string initError;
    CHECK(vehicle.Initialize(scene, params, &initError));
    CHECK(vehicle.IsValid());
    CHECK(scene.GetNumBodies() == baselineBodies + 1);

    ChassisRuntimeState chassisState{};
    WheelRuntimeState wheelStates[4]{};

    // 3. Verify Ground Settlement
    std::cout << "  - Testing gravity settlement and ground contact..." << std::endl;
    for (int step = 0; step < 60; ++step) {
        scene.Step(Axle::Timestep(1.0f / 60.0f));
    }
    vehicle.Synchronize(chassisState, wheelStates);

    CHECK(chassisState.IsOnGround);
    for (int i = 0; i < 4; ++i) {
        CHECK(wheelStates[i].IsOnGround);
        CHECK(wheelStates[i].CurrentSuspensionLength < params.Wheels[i].SuspensionMaxLength);
        CHECK(wheelStates[i].CurrentSuspensionLength > params.Wheels[i].SuspensionMinLength);
        CHECK(wheelStates[i].ContactNormal[1] > 0.9f); // Upwards normal
    }

    // 4. Verify Acceleration (Forward Throttle)
    std::cout << "  - Testing vehicle acceleration..." << std::endl;
    vehicle.SetDriverInput(1.0f, 0.0f, 0.0f, false);
    for (int step = 0; step < 90; ++step) {
        scene.Step(Axle::Timestep(1.0f / 60.0f));
    }
    vehicle.Synchronize(chassisState, wheelStates);

    CHECK(chassisState.CurrentSpeedKmh > 5.0f);
    CHECK(chassisState.CurrentRPM > params.Chassis.MinRPM);
    for (int i = 0; i < 4; ++i) {
        CHECK(wheelStates[i].AngularVelocity > 2.0f);
    }
    const float speedAfterAccel = chassisState.CurrentSpeedKmh;

    // 5. Verify Steering
    std::cout << "  - Testing vehicle steering..." << std::endl;
    vehicle.SetDriverInput(0.5f, 0.8f, 0.0f, false);
    for (int step = 0; step < 60; ++step) {
        scene.Step(Axle::Timestep(1.0f / 60.0f));
    }
    vehicle.Synchronize(chassisState, wheelStates);

    CHECK(std::abs(wheelStates[0].SteerAngle) > 0.05f || std::abs(wheelStates[1].SteerAngle) > 0.05f);
    CHECK(wheelStates[2].SteerAngle == 0.0f);
    CHECK(wheelStates[3].SteerAngle == 0.0f);

    // 6. Verify Service Brake
    std::cout << "  - Testing service braking..." << std::endl;
    vehicle.SetDriverInput(0.0f, 0.0f, 1.0f, false);
    for (int step = 0; step < 120; ++step) {
        scene.Step(Axle::Timestep(1.0f / 60.0f));
    }
    vehicle.Synchronize(chassisState, wheelStates);

    CHECK(chassisState.CurrentSpeedKmh < speedAfterAccel * 0.5f);

    // 7. Verify Handbrake
    std::cout << "  - Testing handbrake locking rear wheels..." << std::endl;
    // Accelerate again briefly
    vehicle.SetDriverInput(1.0f, 0.0f, 0.0f, false);
    for (int step = 0; step < 30; ++step) {
        scene.Step(Axle::Timestep(1.0f / 60.0f));
    }
    // Apply handbrake
    vehicle.SetDriverInput(0.0f, 0.0f, 0.0f, true);
    for (int step = 0; step < 60; ++step) {
        scene.Step(Axle::Timestep(1.0f / 60.0f));
    }
    vehicle.Synchronize(chassisState, wheelStates);

    CHECK(std::abs(wheelStates[2].AngularVelocity) < 1.0f);
    CHECK(std::abs(wheelStates[3].AngularVelocity) < 1.0f);

    // 8. Verify Reset Lifecycle
    std::cout << "  - Testing vehicle reset..." << std::endl;
    const float resetPos[3] = { 15.0f, 1.5f, -20.0f };
    const float resetRot[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    vehicle.Reset(resetPos, resetRot);
    vehicle.Synchronize(chassisState, wheelStates);

    CHECK(chassisState.CurrentSpeedKmh == 0.0f);
    const glm::vec3 currentPos = scene.GetBodyPosition(vehicle.GetChassisBodyID());
    CHECK(std::abs(currentPos.x - 15.0f) < 0.1f);
    CHECK(std::abs(currentPos.z - (-20.0f)) < 0.1f);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::abs(wheelStates[i].AngularVelocity) < 0.01f);
    }

    // 9. Verify Wheel World Transform
    std::cout << "  - Testing wheel world transform matrices..." << std::endl;
    for (std::uint32_t i = 0; i < 4; ++i) {
        float mat16[16]{};
        CHECK(vehicle.GetWheelWorldTransform(i, mat16));
        for (int m = 0; m < 16; ++m) {
            CHECK(std::isfinite(mat16[m]));
        }
        // m[15] (bottom-right of homogeneous matrix) should be 1.0f
        CHECK(std::abs(mat16[15] - 1.0f) < 1e-4f);
    }
    CHECK(!vehicle.GetWheelWorldTransform(99, nullptr)); // invalid wheel

    // 10. Verify Malformed Configuration Rejection & Rollback
    std::cout << "  - Testing malformed config rejection and rollback..." << std::endl;
    {
        const std::uint32_t countBefore = scene.GetNumBodies();

        VehicleInitParams badParams = params;
        badParams.Chassis.Mass = -500.0f; // Invalid negative mass
        VehiclePhysics badVehicle;
        std::string err;
        CHECK(!badVehicle.Initialize(scene, badParams, &err));
        CHECK(!badVehicle.IsValid());
        CHECK(!err.empty());
        CHECK(scene.GetNumBodies() == countBefore); // No body leaked!

        badParams = params;
        badParams.Wheels[0].Radius = 0.0f; // Zero radius
        CHECK(!badVehicle.Initialize(scene, badParams, &err));
        CHECK(!badVehicle.IsValid());
        CHECK(scene.GetNumBodies() == countBefore);

        badParams = params;
        badParams.Chassis.MaxRPM = 500.0f; // MaxRPM < MinRPM
        CHECK(!badVehicle.Initialize(scene, badParams, &err));
        CHECK(!badVehicle.IsValid());
        CHECK(scene.GetNumBodies() == countBefore);
    }

    // 11. Verify Teardown & Repeated Re-initialization
    std::cout << "  - Testing teardown and repeated recreation..." << std::endl;
    {
        vehicle.Teardown();
        CHECK(!vehicle.IsValid());
        CHECK(scene.GetNumBodies() == baselineBodies);

        // Re-initialize
        CHECK(vehicle.Initialize(scene, params));
        CHECK(vehicle.IsValid());
        CHECK(scene.GetNumBodies() == baselineBodies + 1);

        // Step again
        scene.Step(Axle::Timestep(1.0f / 60.0f));
        vehicle.Synchronize(chassisState, wheelStates);

        // Move semantics test
        VehiclePhysics movedVehicle(std::move(vehicle));
        CHECK(movedVehicle.IsValid());
        CHECK(!vehicle.IsValid());
        CHECK(scene.GetNumBodies() == baselineBodies + 1);

        movedVehicle.Teardown();
        CHECK(!movedVehicle.IsValid());
        CHECK(scene.GetNumBodies() == baselineBodies);
    }

    std::cout << "[Test_VehiclePhysics] ALL CHECKS PASSED!" << std::endl;
    return 0;
}
