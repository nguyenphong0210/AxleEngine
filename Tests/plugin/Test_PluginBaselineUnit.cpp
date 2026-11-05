#include "PluginTestCommon.hpp"
#include <AxleSpline/SplinePlugin.hpp>
#include <AxleSpline/Curve3D.hpp>
#include <AxleTerrain/TerrainPlugin.hpp>
#include <AxleTerrain/TerrainData.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/PhysicsDebugRenderer.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/UI/UIDataContext.hpp>

namespace Axle::PluginTests {

    static bool Test_SplinePluginMetadataAndLifecycle() {
        PLUGIN_CHECK(Spline::SplinePlugin::Name == "AxleSpline", "SplinePlugin name is AxleSpline");
        PLUGIN_CHECK(Spline::SplinePlugin::Version == "1.0.0", "SplinePlugin version is 1.0.0");
        PLUGIN_CHECK(!Spline::SplinePlugin::Description.empty(), "SplinePlugin description is non-empty");

        Spline::SplinePlugin::Initialize();
        PLUGIN_CHECK(Spline::SplinePlugin::IsInitialized(), "SplinePlugin is initialized after Initialize()");

        // Re-entrant safety
        Spline::SplinePlugin::Initialize();
        PLUGIN_CHECK(Spline::SplinePlugin::IsInitialized(), "SplinePlugin remains initialized upon re-entry");

        Spline::SplinePlugin::Shutdown();
        PLUGIN_CHECK(!Spline::SplinePlugin::IsInitialized(), "SplinePlugin is shut down cleanly");

        return true;
    }

    static bool Test_TerrainPluginMetadataAndLifecycle() {
        PLUGIN_CHECK(Terrain::TerrainPlugin::GetName() == "AxleTerrain", "TerrainPlugin name is AxleTerrain");
        PLUGIN_CHECK(Terrain::TerrainPlugin::GetVersion() == "1.0.0", "TerrainPlugin version is 1.0.0");

        Terrain::TerrainPlugin::Init();
        PLUGIN_CHECK(Terrain::TerrainPlugin::IsInitialized(), "TerrainPlugin is initialized after Init()");

        // Re-entrant safety
        Terrain::TerrainPlugin::Initialize();
        PLUGIN_CHECK(Terrain::TerrainPlugin::IsInitialized(), "TerrainPlugin remains initialized upon re-entry");

        Terrain::TerrainPlugin::Shutdown();
        PLUGIN_CHECK(!Terrain::TerrainPlugin::IsInitialized(), "TerrainPlugin is shut down cleanly");

        return true;
    }

    static bool Test_PluginDataStructuresIndependence() {
        // Verify Spline Curve3D functions without any Terrain dependency
        Spline::Curve3D curve;
        curve.AddNode(Spline::SplineNode{ glm::vec3(0.0f, 0.0f, 0.0f) });
        curve.AddNode(Spline::SplineNode{ glm::vec3(10.0f, 0.0f, 0.0f) });
        curve.AddNode(Spline::SplineNode{ glm::vec3(20.0f, 0.0f, 10.0f) });
        curve.RebuildCache();

        PLUGIN_CHECK(curve.GetNodeCount() == 3, "Curve3D has 3 nodes");
        float totalLen = curve.GetTotalLength();
        PLUGIN_CHECK(totalLen > 20.0f, "Curve3D length calculation is valid");

        glm::vec3 sampledPos = curve.SamplePosition(10.0f);
        PLUGIN_CHECK(sampledPos.x > 0.0f && sampledPos.x < 20.0f, "Sampled position in range");

        // Verify TerrainData operates independently
        Terrain::TerrainData terrainData(16, 16, 1.0f, 1.0f);
        terrainData.SetHeight(8, 8, 5.0f);

        PLUGIN_CHECK(terrainData.GetResolutionX() == 16, "TerrainData resolution X is 16");
        PLUGIN_CHECK(terrainData.GetResolutionZ() == 16, "TerrainData resolution Z is 16");
        PLUGIN_CHECK(Near(terrainData.GetHeight(8, 8), 5.0f), "Set terrain height is 5.0f");

        return true;
    }

    static bool Test_GeneralPhysicsLifecycleWithoutVehicle() {
        if (!PhysicsEngine::IsInitialized()) {
            PhysicsEngine::Init();
        }
        PLUGIN_CHECK(PhysicsEngine::IsInitialized(), "PhysicsEngine initialized");

        {
            PhysicsDebugRenderer debugRenderer;
            PLUGIN_CHECK(JPH::DebugRenderer::sInstance == &debugRenderer, "Jolt owns active debug renderer singleton");
        }
        PLUGIN_CHECK(JPH::DebugRenderer::sInstance == nullptr, "Base destructor releases debug renderer singleton");
        {
            PhysicsScene physicsScene;

            // Spawn static ground box
            BodyID groundID = physicsScene.CreateBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(50.0f, 0.5f, 50.0f), false);
            PLUGIN_CHECK(physicsScene.IsBodyValid(groundID), "Ground body is valid");

            // Spawn dynamic falling box at Y = 10.0
            BodyID boxID = physicsScene.CreateBox(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.5f, 0.5f, 0.5f), true, 0.5f, 0.0f, 5.0f);
            PLUGIN_CHECK(physicsScene.IsBodyValid(boxID), "Dynamic box body is valid");

            glm::vec3 initialPos = physicsScene.GetBodyPosition(boxID);
            PLUGIN_CHECK(Near(initialPos.y, 10.0f, 0.01f), "Initial box Y is 10.0");

            // Step simulation 10 frames
            for (int i = 0; i < 10; ++i) {
                physicsScene.Step(1.0f / 60.0f);
            }

            glm::vec3 steppedPos = physicsScene.GetBodyPosition(boxID);
            glm::vec3 velocity = physicsScene.GetLinearVelocity(boxID);

            // Gravity (-9.81 m/s^2) should pull box downwards
            PLUGIN_CHECK(steppedPos.y < initialPos.y, "Box fell downwards under gravity");
            PLUGIN_CHECK(velocity.y < 0.0f, "Box has negative vertical velocity");

            physicsScene.DestroyBody(boxID);
            physicsScene.DestroyBody(groundID);
            PLUGIN_CHECK(!physicsScene.IsBodyValid(boxID), "Box destroyed cleanly");
            PLUGIN_CHECK(!physicsScene.IsBodyValid(groundID), "Ground destroyed cleanly");
        }

        PhysicsEngine::Shutdown();
        PLUGIN_CHECK(!PhysicsEngine::IsInitialized(), "PhysicsEngine shut down cleanly");

        return true;
    }

    static bool Test_CleanEngineSceneLifecycle() {
        auto scene = std::make_shared<Scene>();
        PLUGIN_CHECK(scene != nullptr, "Scene constructed");

        // Populate standard entities
        Entity cam = scene->CreateEntity("MainCamera");
        cam.AddComponent<CameraComponent>();

        Entity light = scene->CreateEntity("DirectionalLight");
        light.AddComponent<DirectionalLightComponent>();

        Entity cube = scene->CreateEntity("StaticCube");
        cube.AddComponent<MeshComponent>();

        // Verify entities created cleanly
        PLUGIN_CHECK(scene->GetRegistry().view<TagComponent>().size() == 3, "Scene has 3 authored entities");

        // Run full lifecycle without any vehicle components or active vehicle objects
        scene->OnRuntimeStart(nullptr);
        scene->OnUpdateRuntime(0.016f, nullptr);
        scene->OnRuntimeStop(nullptr);

        // Verify scene remains intact and clean
        PLUGIN_CHECK(scene->GetRegistry().view<TagComponent>().size() == 3, "Scene entities preserved after lifecycle");
        PLUGIN_CHECK(cam.HasComponent<CameraComponent>(), "Camera preserved");
        PLUGIN_CHECK(light.HasComponent<DirectionalLightComponent>(), "Light preserved");

        return true;
    }

    static bool Test_UIDataContextGenericBindings() {
        UIDataContext rootContext;
        rootContext.SetInt("Telemetry.Speed", 120);
        rootContext.SetFloat("Telemetry.Rate", 0.75f);
        rootContext.SetBool("Telemetry.Alert", false);
        rootContext.SetString("Telemetry.Label", "4");

        PLUGIN_CHECK(rootContext.Has("Telemetry.Speed"), "rootContext has SpeedKmh");
        PLUGIN_CHECK(rootContext.Has("Telemetry.Rate"), "rootContext has RPMNormalized");
        PLUGIN_CHECK(rootContext.Has("Telemetry.Alert"), "rootContext has IsRedline");

        const auto* speedVal = rootContext.Get("Telemetry.Speed");
        PLUGIN_CHECK(speedVal != nullptr && std::holds_alternative<int32_t>(*speedVal), "Speed is int32_t");
        PLUGIN_CHECK(std::get<int32_t>(*speedVal) == 120, "Speed is 120");

        // Scoped child context override
        UIDataContext childContext(&rootContext);
        PLUGIN_CHECK(childContext.Has("Telemetry.Speed"), "childContext inherits SpeedKmh");

        childContext.SetInt("Telemetry.Speed", 240);
        const auto* childSpeedVal = childContext.Get("Telemetry.Speed");
        PLUGIN_CHECK(std::get<int32_t>(*childSpeedVal) == 240, "childContext overrides SpeedKmh to 240");

        // Parent context remains unchanged
        const auto* parentSpeedVal = rootContext.Get("Telemetry.Speed");
        PLUGIN_CHECK(std::get<int32_t>(*parentSpeedVal) == 120, "parentContext retains SpeedKmh 120");

        return true;
    }

} // namespace Axle::PluginTests

bool RunPluginBaselineUnitTests() {
    std::cout << "  [PluginBaseline] Running plugin baseline & retirement inventory tests...\n";
    if (!Axle::PluginTests::Test_SplinePluginMetadataAndLifecycle()) return false;
    if (!Axle::PluginTests::Test_TerrainPluginMetadataAndLifecycle()) return false;
    if (!Axle::PluginTests::Test_PluginDataStructuresIndependence()) return false;
    if (!Axle::PluginTests::Test_GeneralPhysicsLifecycleWithoutVehicle()) return false;
    if (!Axle::PluginTests::Test_CleanEngineSceneLifecycle()) return false;
    if (!Axle::PluginTests::Test_UIDataContextGenericBindings()) return false;
    std::cout << "  [PluginBaseline] All plugin baseline unit tests passed.\n";
    return true;
}
