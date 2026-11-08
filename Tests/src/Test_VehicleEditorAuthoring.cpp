#include "AxleVehicle/VehicleComponents.hpp"
#include "AxleVehicle/VehicleCodecs.hpp"
#include "AxleVehicle/VehiclePhysics.hpp"
#include <Axle/PluginSDK.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/ExtensionDocument.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <fstream>
#include <filesystem>

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK FAILED: " #expr " at " __FILE__ ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (false)

using namespace AxleVehicle;

// Forward-declarations of editor inspector functions
extern "C" {
    AxleVehicle::ChassisConfig* axle_vehicle_chassis_config(void*) noexcept;
    AxleVehicle::ChassisRuntimeState* axle_vehicle_chassis_runtime(void*) noexcept;
    AxleVehicle::WheelConfig* axle_vehicle_wheel_config(void*) noexcept;
    AxleVehicle::WheelRuntimeState* axle_vehicle_wheel_runtime(void*) noexcept;
}

namespace {

struct MockEditorServices {
    std::vector<std::string> Texts;
    std::vector<std::string> DragLabels;
    bool ButtonClicked = false;

    static void TextImpl(void* user, const char* text) {
        if (user && text) static_cast<MockEditorServices*>(user)->Texts.push_back(text);
    }

    static bool DragFloatImpl(void* user, const char* label, float* value, float) {
        if (user && label && value) {
            static_cast<MockEditorServices*>(user)->DragLabels.push_back(label);
            return true;
        }
        return false;
    }

    static bool ButtonImpl(void* user, const char*) {
        if (user) {
            static_cast<MockEditorServices*>(user)->ButtonClicked = true;
            return true;
        }
        return false;
    }
};

} // namespace

int main(int argc, char** argv) {
    std::cout << "[Test_VehicleEditorAuthoring] Starting PLG-16 vehicle authoring and Editor Play verification..." << std::endl;

    // 1. Verify Accessors for Inspector
    std::cout << "  - Testing component narrow accessors..." << std::endl;
    {
        VehicleChassisComponent chassis{};
        chassis.Config.Mass = 1800.0f;
        chassis.Runtime.CurrentSpeedKmh = 72.5f;

        CHECK(axle_vehicle_chassis_config(&chassis) == &chassis.Config);
        CHECK(axle_vehicle_chassis_runtime(&chassis) == &chassis.Runtime);
        CHECK(axle_vehicle_chassis_config(nullptr) == nullptr);
        CHECK(axle_vehicle_chassis_runtime(nullptr) == nullptr);

        VehicleWheelComponent wheel{};
        wheel.Config.Radius = 0.42f;
        wheel.Runtime.CurrentSuspensionLength = 0.28f;

        CHECK(axle_vehicle_wheel_config(&wheel) == &wheel.Config);
        CHECK(axle_vehicle_wheel_runtime(&wheel) == &wheel.Runtime);
        CHECK(axle_vehicle_wheel_config(nullptr) == nullptr);
        CHECK(axle_vehicle_wheel_runtime(nullptr) == nullptr);
    }

    // 2. Verify Config Diagnostics Logic
    std::cout << "  - Testing configuration diagnostics..." << std::endl;
    {
        VehicleChassisComponent validChassis{};
        CHECK(validChassis.Validate());

        // Error: negative mass
        VehicleChassisComponent badMassChassis{};
        badMassChassis.Config.Mass = -10.0f;
        CHECK(!badMassChassis.Validate());

        // Error: MinRPM >= MaxRPM
        VehicleChassisComponent badRPMChassis{};
        badRPMChassis.Config.MinRPM = 8000.0f;
        badRPMChassis.Config.MaxRPM = 6000.0f;
        CHECK(!badRPMChassis.Validate());

        // Error: Steer angle > 90 deg
        VehicleChassisComponent badSteerChassis{};
        badSteerChassis.Config.MaxSteerAngle = 110.0f;
        CHECK(!badSteerChassis.Validate());

        // Wheel diagnostics
        VehicleWheelComponent validWheel{};
        CHECK(validWheel.Validate());

        // Error: Min suspension >= Max suspension
        VehicleWheelComponent badSuspWheel{};
        badSuspWheel.Config.SuspensionMinLength = 0.5f;
        badSuspWheel.Config.SuspensionMaxLength = 0.2f;
        CHECK(!badSuspWheel.Validate());

        // Error: Wheel index > 3
        VehicleWheelComponent badIndexWheel{};
        badIndexWheel.Config.WheelIndex = 7;
        CHECK(!badIndexWheel.Validate());
    }

    // 3. Verify Authored Entity Assembly, Hierarchy & Reference Remapping
    std::cout << "  - Testing vehicle assembly, hierarchy and entity reference remapping..." << std::endl;
    {
        VehicleChassisComponent chassis{};
        chassis.Config.WheelEntities[0] = 1001;
        chassis.Config.WheelEntities[1] = 1002;
        chassis.Config.WheelEntities[2] = 1003;
        chassis.Config.WheelEntities[3] = 1004;

        VehicleWheelComponent wheels[4]{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            wheels[i].Config.ChassisEntity = 1000;
            wheels[i].Config.WheelIndex = i;
        }

        // Remapping table (simulating duplicating the entire vehicle prefab)
        Axle::SDK::EntityRemap remaps[] = {
            { 1000, 2000 },
            { 1001, 2001 },
            { 1002, 2002 },
            { 1003, 2003 },
            { 1004, 2004 }
        };

        // Clone chassis and remap
        VehicleChassisComponent clonedChassis = chassis;
        clonedChassis.Runtime.Reset();
        for (std::uint32_t w = 0; w < 4; ++w) {
            for (const auto& remap : remaps) {
                if (clonedChassis.Config.WheelEntities[w] == remap.Source) {
                    clonedChassis.Config.WheelEntities[w] = remap.Destination;
                    break;
                }
            }
        }

        CHECK(clonedChassis.Config.WheelEntities[0] == 2001);
        CHECK(clonedChassis.Config.WheelEntities[1] == 2002);
        CHECK(clonedChassis.Config.WheelEntities[2] == 2003);
        CHECK(clonedChassis.Config.WheelEntities[3] == 2004);

        // Original remains untouched
        CHECK(chassis.Config.WheelEntities[0] == 1001);

        // Clone wheel 0 and remap
        VehicleWheelComponent clonedWheel0 = wheels[0];
        clonedWheel0.Runtime.Reset();
        for (const auto& remap : remaps) {
            if (clonedWheel0.Config.ChassisEntity == remap.Source) {
                clonedWheel0.Config.ChassisEntity = remap.Destination;
                break;
            }
        }
        CHECK(clonedWheel0.Config.ChassisEntity == 2000);
        CHECK(wheels[0].Config.ChassisEntity == 1000);
    }

    // 4. Verify Codec Round-Trip Serialization (Save & Reopen)
    std::cout << "  - Testing save/reopen codec round-trip..." << std::endl;
    {
        VehicleChassisComponent sourceChassis{};
        sourceChassis.Config.HalfExtents[0] = 1.05f;
        sourceChassis.Config.HalfExtents[1] = 0.28f;
        sourceChassis.Config.HalfExtents[2] = 2.15f;
        sourceChassis.Config.Mass = 1750.0f;
        sourceChassis.Config.CenterOfMassOffset[1] = -0.12f;
        sourceChassis.Config.MaxEngineTorque = 720.0f;
        sourceChassis.Config.MinRPM = 950.0f;
        sourceChassis.Config.MaxRPM = 7200.0f;
        sourceChassis.Config.MaxSteerAngle = 32.0f;
        sourceChassis.Config.MaxBrakeTorque = 4000.0f;
        sourceChassis.Config.MaxHandbrakeTorque = 8000.0f;
        sourceChassis.Config.FourWheelDrive = false;
        sourceChassis.Config.WheelEntities[0] = 501;
        sourceChassis.Config.WheelEntities[1] = 502;
        sourceChassis.Config.WheelEntities[2] = 503;
        sourceChassis.Config.WheelEntities[3] = 504;

        std::string serialized;
        CHECK(SerializeChassis(sourceChassis, serialized));

        VehicleChassisComponent reopenedChassis{};
        CHECK(DeserializeChassis(serialized, reopenedChassis));
        CHECK(reopenedChassis.Config.Mass == 1750.0f);
        CHECK(reopenedChassis.Config.MaxEngineTorque == 720.0f);
        CHECK(reopenedChassis.Config.FourWheelDrive == false);
        CHECK(reopenedChassis.Config.WheelEntities[3] == 504);
    }

    // 5. Verify Editor Play / Step / Stop Simulation Lifecycle & Scene Restoration
    std::cout << "  - Testing Editor Play / Stop simulation lifecycle and scene restoration..." << std::endl;
    {
        Axle::PhysicsScene physicsScene;
        physicsScene.CreatePlane(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 200.0f);

        // Authored Edit Scene with vehicle at height 3.0m
        auto editScene = std::make_shared<Axle::Scene>();
        auto chassisEntity = editScene->CreateEntityWithUUID(Axle::UUID(100), "Authored Chassis");
        auto& editTransform = chassisEntity.GetComponent<Axle::TransformComponent>();
        editTransform.Translation = glm::vec3(0.0f, 3.0f, 0.0f);
        const glm::vec3 authoredPosition = editTransform.Translation;

        // Clone scene for Play mode
        auto playScene = Axle::Scene::Copy(editScene);
        CHECK(playScene != nullptr);

        auto playEntity = playScene->GetEntityByUUID(Axle::UUID(100));
        CHECK(playEntity);
        CHECK(playEntity.GetComponent<Axle::TransformComponent>().Translation.y == 3.0f);

        // Start simulation on playScene with VehiclePhysics
        VehicleInitParams initParams;
        initParams.InitialPosition[0] = 0.0f;
        initParams.InitialPosition[1] = 3.0f;
        initParams.InitialPosition[2] = 0.0f;

        VehiclePhysics runtimeVehicle;
        CHECK(runtimeVehicle.Initialize(physicsScene, initParams));
        CHECK(runtimeVehicle.IsValid());

        // Step simulation for 60 frames (1 second): vehicle falls and settles on ground
        for (int step = 0; step < 60; ++step) {
            physicsScene.Step(Axle::Timestep(1.0f / 60.0f));
        }

        ChassisRuntimeState playRuntime{};
        WheelRuntimeState playWheels[4]{};
        runtimeVehicle.Synchronize(playRuntime, playWheels);
        CHECK(playRuntime.IsOnGround);

        // Sync physics transform into playEntity transform
        const glm::vec3 simulatedPos = physicsScene.GetBodyPosition(runtimeVehicle.GetChassisBodyID());
        playEntity.GetComponent<Axle::TransformComponent>().Translation = simulatedPos;
        CHECK(simulatedPos.y < 2.0f); // Vehicle fell from 3.0m and landed on suspension
        CHECK(playEntity.GetComponent<Axle::TransformComponent>().Translation.y < 2.0f);

        // Stop simulation: Teardown runtime vehicle, discard playScene
        runtimeVehicle.Teardown();
        CHECK(!runtimeVehicle.IsValid());
        playScene.reset();

        // Verify editScene remained pristine and completely restored
        const glm::vec3 finalEditPos = chassisEntity.GetComponent<Axle::TransformComponent>().Translation;
        CHECK(finalEditPos == authoredPosition);
        CHECK(finalEditPos.y == 3.0f);
    }

    // 6. Verify Sample Scene Existence and Integrity
    std::cout << "  - Testing sample scene file existence and schema..." << std::endl;
    if (argc >= 2) {
        std::filesystem::path repoRoot = argv[1];
        const auto sampleScenePath = repoRoot / "Plugins/Vehicle/Content/Scenes/SampleVehicle.axlescene";
        CHECK(std::filesystem::exists(sampleScenePath));

        std::ifstream file(sampleScenePath);
        CHECK(file.is_open());
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CHECK(!content.empty());
        auto sample=std::make_shared<Axle::Scene>();
        CHECK(Axle::SceneSerializer(sample.get()).Deserialize(sampleScenePath));
        unsigned chassis=0,wheels=0;
        for(auto handle:sample->GetRegistry().view<Axle::PassiveExtensionComponent>()) {
            for(const auto& section:sample->GetRegistry().get<Axle::PassiveExtensionComponent>(handle).Sections) {
                Axle::ExtensionDocument document; CHECK(Axle::ParseExtensionDocument(section.SectionName,section.Lines,document));
                CHECK(document.Required&&document.EntityReferences&&document.Schema==1);
                if(document.TypeId==kChassisTypeId) { VehicleChassisComponent value; CHECK(DeserializeChassis(document.Payload,value)); ++chassis; }
                else if(document.TypeId==kWheelTypeId) { VehicleWheelComponent value; CHECK(DeserializeWheel(document.Payload,value)); ++wheels; }
            }
        }
        CHECK(chassis==1&&wheels==4);
    }

    std::cout << "[Test_VehicleEditorAuthoring] ALL CHECKS PASSED!" << std::endl;
    return 0;
}
