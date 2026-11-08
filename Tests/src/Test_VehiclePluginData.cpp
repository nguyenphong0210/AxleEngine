#include "AxleVehicle/VehicleComponents.hpp"
#include "AxleVehicle/VehicleCodecs.hpp"
#include <Axle/Plugin.hpp>
#include <Axle/PluginSDK.hpp>

#include <iostream>
#include <cassert>
#include <limits>
#include <cmath>

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK FAILED: " #expr " at " __FILE__ ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (false)

using namespace AxleVehicle;

int main(int argc, char** argv) {
    std::cout << "[Test_VehiclePluginData] Starting PLG-14 vehicle data and codec checks..." << std::endl;

    // 1. Check Default Construction & Validation
    {
        VehicleChassisComponent defaultChassis{};
        CHECK(defaultChassis.Validate());
        CHECK(defaultChassis.Config.Mass == 1500.0f);
        CHECK(defaultChassis.Config.FourWheelDrive == true);

        VehicleWheelComponent defaultWheel{};
        CHECK(defaultWheel.Validate());
        CHECK(defaultWheel.Config.Radius == 0.35f);
        CHECK(defaultWheel.Config.WheelIndex == 0);
    }

    // 2. Check Validation Boundaries & Reject Invalid Values
    {
        VehicleChassisComponent chassis{};
        chassis.Config.Mass = -10.0f; // negative mass
        CHECK(!chassis.Validate());

        chassis.Config.Mass = std::numeric_limits<float>::quiet_NaN();
        CHECK(!chassis.Validate());

        chassis.Config.Mass = 1500.0f;
        chassis.Config.HalfExtents[0] = 0.0f; // zero width
        CHECK(!chassis.Validate());

        chassis.Config.HalfExtents[0] = 0.9f;
        chassis.Config.MaxSteerAngle = 100.0f; // beyond 90 deg
        CHECK(!chassis.Validate());

        chassis.Config.MaxSteerAngle = 35.0f;
        chassis.Config.MinRPM = 8000.0f;
        chassis.Config.MaxRPM = 6000.0f; // MaxRPM < MinRPM
        CHECK(!chassis.Validate());

        VehicleWheelComponent wheel{};
        wheel.Config.WheelIndex = 5; // invalid wheel index > 3
        CHECK(!wheel.Validate());

        wheel.Config.WheelIndex = 0;
        wheel.Config.Radius = -0.1f;
        CHECK(!wheel.Validate());

        wheel.Config.Radius = 0.35f;
        wheel.Config.SuspensionMaxLength = 0.1f;
        wheel.Config.SuspensionMinLength = 0.2f; // min > max
        CHECK(!wheel.Validate());

        wheel.Config.SuspensionMinLength = 0.1f;
        wheel.Config.SuspensionMaxLength = 0.4f;
        wheel.Config.BrakeRatio = 1.5f; // > 1.0f
        CHECK(!wheel.Validate());
    }

    // 3. Check Codec Round-Trip (Chassis)
    {
        VehicleChassisComponent sourceChassis{};
        sourceChassis.Config.HalfExtents[0] = 1.0f;
        sourceChassis.Config.HalfExtents[1] = 0.3f;
        sourceChassis.Config.HalfExtents[2] = 2.2f;
        sourceChassis.Config.Mass = 1650.0f;
        sourceChassis.Config.CenterOfMassOffset[1] = -0.1f;
        sourceChassis.Config.MaxEngineTorque = 650.0f;
        sourceChassis.Config.MinRPM = 900.0f;
        sourceChassis.Config.MaxRPM = 6800.0f;
        sourceChassis.Config.MaxSteerAngle = 32.5f;
        sourceChassis.Config.MaxBrakeTorque = 3500.0f;
        sourceChassis.Config.MaxHandbrakeTorque = 7000.0f;
        sourceChassis.Config.FourWheelDrive = false;
        sourceChassis.Config.WheelEntities[0] = 1001;
        sourceChassis.Config.WheelEntities[1] = 1002;
        sourceChassis.Config.WheelEntities[2] = 1003;
        sourceChassis.Config.WheelEntities[3] = 1004;

        std::string serialized;
        CHECK(SerializeChassis(sourceChassis, serialized));
        CHECK(!serialized.empty());
        CHECK(serialized.find("chassis-v1") != std::string::npos);

        VehicleChassisComponent roundtripChassis{};
        CHECK(DeserializeChassis(serialized, roundtripChassis));
        CHECK(roundtripChassis.Validate());
        CHECK(roundtripChassis.Config.Mass == 1650.0f);
        CHECK(roundtripChassis.Config.FourWheelDrive == false);
        CHECK(roundtripChassis.Config.WheelEntities[0] == 1001);
        CHECK(roundtripChassis.Config.WheelEntities[1] == 1002);
        CHECK(roundtripChassis.Config.WheelEntities[2] == 1003);
        CHECK(roundtripChassis.Config.WheelEntities[3] == 1004);
        CHECK(roundtripChassis.Runtime.CurrentSpeedKmh == 0.0f);
    }

    // 4. Check Codec Round-Trip (Wheel)
    {
        VehicleWheelComponent sourceWheel{};
        sourceWheel.Config.ChassisEntity = 5000;
        sourceWheel.Config.WheelIndex = 1;
        sourceWheel.Config.Offset[0] = 0.95f;
        sourceWheel.Config.Offset[1] = -0.15f;
        sourceWheel.Config.Offset[2] = 1.4f;
        sourceWheel.Config.Radius = 0.38f;
        sourceWheel.Config.Width = 0.28f;
        sourceWheel.Config.SuspensionMinLength = 0.18f;
        sourceWheel.Config.SuspensionMaxLength = 0.48f;
        sourceWheel.Config.SpringFrequency = 2.8f;
        sourceWheel.Config.SpringDamping = 0.65f;
        sourceWheel.Config.CanSteer = true;
        sourceWheel.Config.IsDriven = true;
        sourceWheel.Config.BrakeRatio = 0.7f;
        sourceWheel.Config.HandbrakeRatio = 0.0f;

        std::string serialized;
        CHECK(SerializeWheel(sourceWheel, serialized));
        CHECK(!serialized.empty());
        CHECK(serialized.find("wheel-v1") != std::string::npos);

        VehicleWheelComponent roundtripWheel{};
        CHECK(DeserializeWheel(serialized, roundtripWheel));
        CHECK(roundtripWheel.Validate());
        CHECK(roundtripWheel.Config.ChassisEntity == 5000);
        CHECK(roundtripWheel.Config.WheelIndex == 1);
        CHECK(roundtripWheel.Config.Radius == 0.38f);
        CHECK(roundtripWheel.Config.CanSteer == true);
        CHECK(roundtripWheel.Config.BrakeRatio == 0.7f);
        CHECK(roundtripWheel.Runtime.IsOnGround == false);
    }

    // 5. Deliberate Rejection of Legacy Formats & Malformed Payloads
    {
        VehicleChassisComponent comp{};
        // Legacy VehicleComponent payload
        CHECK(!DeserializeChassis("VehicleComponent { ChassisMass: 1500, WheelRadius: 0.35 }", comp));
        // Missing tag or invalid prefix
        CHECK(!DeserializeChassis("chassis-v0 0.9 0.25 2.0 1500 ...", comp));
        CHECK(!DeserializeChassis("chassis-v1 0.9 nan 2.0 1500 0 0 0 500 1000 7000 35 3000 6000 1 0 0 0 0", comp));
        CHECK(!DeserializeChassis("", comp));
        // Truncated data
        CHECK(!DeserializeChassis("chassis-v1 0.9 0.25", comp));

        VehicleWheelComponent wheel{};
        // Legacy WheelComponent / VehicleWheel3D payload
        CHECK(!DeserializeWheel("WheelComponent { WheelIndex: 0, Radius: 0.35 }", wheel));
        CHECK(!DeserializeWheel("VehicleWheel3D { offset: [0, 0, 0] }", wheel));
        CHECK(!DeserializeWheel("wheel-v0 0 0 0 0", wheel));
        CHECK(!DeserializeWheel("", wheel));
    }

    // 6. UUID / Entity Reference Remapping & Independent Clone State
    {
        VehicleChassisComponent origChassis{};
        origChassis.Config.WheelEntities[0] = 100;
        origChassis.Config.WheelEntities[1] = 101;
        origChassis.Config.WheelEntities[2] = 102;
        origChassis.Config.WheelEntities[3] = 103;

        Axle::SDK::EntityRemap remaps[5] = {
            { 99, 990 },
            { 100, 200 },
            { 101, 201 },
            { 102, 202 },
            { 103, 203 }
        };

        // Deep clone with remapping
        VehicleChassisComponent cloneChassis = origChassis;
        for (std::uint32_t w = 0; w < 4; ++w) {
            for (const auto& remap : remaps) {
                if (cloneChassis.Config.WheelEntities[w] == remap.Source) {
                    cloneChassis.Config.WheelEntities[w] = remap.Destination;
                    break;
                }
            }
        }

        CHECK(cloneChassis.Config.WheelEntities[0] == 200);
        CHECK(cloneChassis.Config.WheelEntities[1] == 201);
        CHECK(cloneChassis.Config.WheelEntities[2] == 202);
        CHECK(cloneChassis.Config.WheelEntities[3] == 203);

        // Verify independent clone state
        cloneChassis.Config.Mass = 2500.0f;
        CHECK(origChassis.Config.Mass == 1500.0f);
        CHECK(origChassis.Config.WheelEntities[0] == 100);

        // Wheel remapping
        VehicleWheelComponent origWheel{};
        origWheel.Config.ChassisEntity = 99;
        origWheel.Config.WheelIndex = 0;

        VehicleWheelComponent cloneWheel = origWheel;
        for (const auto& remap : remaps) {
            if (cloneWheel.Config.ChassisEntity == remap.Source) {
                cloneWheel.Config.ChassisEntity = remap.Destination;
                break;
            }
        }
        CHECK(cloneWheel.Config.ChassisEntity == 990);
        cloneWheel.Config.Radius = 0.5f;
        CHECK(origWheel.Config.Radius == 0.35f);
    }

    // 7. Verify Plugin Manifest Discovery
    if (argc >= 2) {
        std::filesystem::path repoRoot = argv[1];
        Axle::Plugin::DiscoveryOptions options;
        options.Roots = { repoRoot / "Plugins" };
        auto catalog = Axle::Plugin::Discover(options);
        
        bool foundVehicle = false;
        for (const auto& plugin : catalog.Plugins) {
            if (plugin.Id == "axle.vehicle") {
                foundVehicle = true;
                CHECK(plugin.Name == "Vehicle");
                CHECK(plugin.Version == "1.0.0");
                CHECK(plugin.Modules.size() == 2);
                CHECK(plugin.Modules[0].Name == "VehicleRuntime");
                CHECK(plugin.Modules[0].Kind == Axle::Plugin::ModuleKind::Runtime);
                CHECK(plugin.Modules[1].Name == "VehicleEditor");
                CHECK(plugin.Modules[1].Kind == Axle::Plugin::ModuleKind::Editor);
                break;
            }
        }
        CHECK(foundVehicle);

        // 8. Verify Resolution and Build Plan Generation
        auto resolution = Axle::Plugin::Resolve(catalog, { "axle.vehicle" }, Axle::Plugin::ProductKind::Editor);
        CHECK(resolution.IsValid());
        CHECK(resolution.Plugins.size() == 1);
        CHECK(resolution.Plugins[0].Id == "axle.vehicle");

        auto plan = Axle::Plugin::CreateBuildPlan(resolution, Axle::Plugin::ProductKind::Editor, "Debug", "fake-sdk", "fake-toolchain");
        CHECK(plan.IsValid());
        CHECK(plan.Artifacts.size() == 2);
        bool hasRuntime = false, hasEditor = false;
        for (const auto& a : plan.Artifacts) {
            if (a.ModuleName == "VehicleRuntime") hasRuntime = true;
            if (a.ModuleName == "VehicleEditor") hasEditor = true;
        }
        CHECK(hasRuntime);
        CHECK(hasEditor);
        CHECK(!plan.Inputs.empty());
    }

    std::cout << "[Test_VehiclePluginData] ALL CHECKS PASSED!" << std::endl;
    return 0;
}
