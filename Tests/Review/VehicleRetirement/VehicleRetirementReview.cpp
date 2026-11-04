#include <Axle/AssemblySerializer.hpp>
#include <Axle/Components.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneSerializer.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using namespace Axle;

std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read fixture output");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

Entity LegacyCar(Scene& scene) {
    auto car = scene.CreateEntity("LegacyCar");
    car.AddComponent<LegacyComponentsHolder>().Components = {
        {"VehicleComponent", {"ChassisMass: 1500.0", "MaxEngineTorque: 850.0"}},
        {"VehicleAudioComponent", {"Enabled: true", "EnginePowerClipPath: assets/audio/engine.wav"}},
        {"VehicleVFXComponent", {"Enabled: true", "SurfaceType: 0"}}
    };
    auto& rb = car.AddComponent<RigidBody3D>();
    rb.MotionType = BodyMotionType::Static;
    rb.IsDynamic = false;
    car.AddComponent<CollisionShape3D>();
    return car;
}

Entity Find(Scene& scene, const std::string& tag) {
    for (auto handle : scene.GetRegistry().view<TagComponent>()) {
        if (scene.GetRegistry().get<TagComponent>(handle).Tag == tag) return {handle, &scene};
    }
    return {};
}

bool SamePayload(Entity a, Entity b) {
    if (!a || !b || !a.HasComponent<LegacyComponentsHolder>() || !b.HasComponent<LegacyComponentsHolder>()) return false;
    const auto& left = a.GetComponent<LegacyComponentsHolder>().Components;
    const auto& right = b.GetComponent<LegacyComponentsHolder>().Components;
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i].TypeName != right[i].TypeName || left[i].Lines != right[i].Lines) return false;
    }
    return true;
}

bool Run(const std::string& scenario, const std::filesystem::path& workspace) {
    if (scenario == "assembly-editor-authored") {
        const auto original = std::filesystem::path(AXLE_RETIREMENT_SOURCE_DIR)
            / "Tests/Fixtures/Assets/prefabs/SportsCar_Assembly.axle";
        const auto input = workspace / "SportsCar-input.axle";
        const auto output = workspace / "SportsCar-saved.axle";
        std::filesystem::copy_file(original, input, std::filesystem::copy_options::overwrite_existing);
        if (Read(input).find("VehicleComponent:") == std::string::npos) return false;
        const auto data = AssemblySerializer::LoadAssemblyData(input);
        if (!data.IsValid || !AssemblySerializer::SaveAssemblyData(output, data)) return false;
        const bool kept = Read(output).find("VehicleComponent:") != std::string::npos;
        std::cout << "authored_editor_kept_legacy=" << kept << '\n';
        return kept;
    }
    auto source = std::make_shared<Scene>();
    Entity car = LegacyCar(*source);
    if (scenario == "scene-basic") {
        Scene target;
        PhysicsScene physics;
        SceneSerializer writer(source.get());
        SceneSerializer reader(&target, &physics);
        const bool loaded = reader.DeserializeFromString(writer.SerializeToString());
        std::cout << "scene_bodies=" << physics.GetNumBodies() << '\n';
        return loaded && SamePayload(car, Find(target, "LegacyCar")) && physics.GetNumBodies() == 0;
    }
    if (scenario == "scene-copy") {
        auto copy = Scene::Copy(source);
        auto copied = Find(*copy, "LegacyCar");
        auto duplicate = source->DuplicateEntity(car);
        if (!SamePayload(car, copied) || !SamePayload(car, duplicate)) return false;
        copied.GetComponent<LegacyComponentsHolder>().Components[0].Lines[0] = "Changed: copy only";
        return car.GetComponent<LegacyComponentsHolder>().Components[0].Lines[0] == "ChassisMass: 1500.0";
    }
    if (scenario == "duplicate-physics") {
        PhysicsScene physics;
        auto duplicate = source->DuplicateEntity(car, &physics);
        std::cout << "duplicate_bodies=" << physics.GetNumBodies() << '\n';
        return duplicate && SamePayload(car, duplicate) && physics.GetNumBodies() == 0;
    }
    if (scenario == "scene-nested" || scenario == "scene-alias") {
        const std::string section = scenario == "scene-nested"
            ? "    VehicleComponent:\n      ChassisMass: 1500.0\n      Tuning:\n        TorqueCurve: [900, 7000]\n      # authored note\n"
            : "    VehicleWheel3D:\n      Radius: 0.35\n";
        const std::string input = "Scene: Legacy\nEntities:\n  - Entity: 101\n    TagComponent:\n      Tag: Legacy\n" + section;
        Scene target;
        SceneSerializer serializer(&target);
        if (!serializer.DeserializeFromString(input)) return false;
        const auto output = serializer.SerializeToString();
        std::ofstream(workspace / (scenario + ".axlescene")) << output;
        std::cout << "original_section_retained=" << (output.find(section) != std::string::npos) << '\n';
        return output.find(section) != std::string::npos;
    }
    if (scenario == "assembly-basic" || scenario == "assembly-physics" || scenario == "assembly-editor" || scenario == "assembly-nested") {
        if (scenario == "assembly-nested") {
            auto& lines = car.GetComponent<LegacyComponentsHolder>().Components[0].Lines;
            lines.push_back("Tuning:");
            lines.push_back("  TorqueCurve: [900, 7000]");
            lines.push_back("# authored note");
        }
        const auto path = workspace / (scenario + "-input.axle");
        if (!AssemblySerializer::Serialize(path, car)) return false;
        if (scenario == "assembly-editor") {
            const auto data = AssemblySerializer::LoadAssemblyData(path);
            const auto output = workspace / "assembly-editor-saved.axle";
            if (!data.IsValid || !AssemblySerializer::SaveAssemblyData(output, data)) return false;
            const bool kept = Read(output).find("VehicleComponent:") != std::string::npos;
            std::cout << "editor_kept_legacy=" << kept << '\n';
            return kept;
        }
        Scene target;
        PhysicsScene physics;
        auto spawned = AssemblySerializer::Deserialize(path, &target,
            scenario == "assembly-physics" ? &physics : nullptr);
        std::cout << "assembly_bodies=" << physics.GetNumBodies() << '\n';
        return SamePayload(car, spawned) && physics.GetNumBodies() == 0;
    }
    if (scenario == "lifecycle") {
        auto ball = source->CreateEntity("OrdinaryBody");
        ball.GetComponent<TransformComponent>().Translation = {0.0f, 10.0f, 0.0f};
        ball.AddComponent<RigidBody3D>();
        ball.AddComponent<CollisionShape3D>();
        Scene target;
        PhysicsScene physics;
        SceneSerializer reader(&target, &physics);
        if (!reader.DeserializeFromString(SceneSerializer(source.get()).SerializeToString())) return false;
        auto body = Find(target, "OrdinaryBody");
        if (!body || physics.GetNumBodies() != 1) return false;
        const auto id = body.GetComponent<RigidBody3D>().RuntimeBodyID;
        const float before = physics.GetBodyPosition(id).y;
        target.OnRuntimeStart(&physics);
        for (int i = 0; i < 12; ++i) {
            physics.Step(1.0f / 60.0f);
            target.OnUpdateRuntime(1.0f / 60.0f, &physics);
        }
        const float after = physics.GetBodyPosition(id).y;
        target.OnRuntimeStop(&physics);
        JobSystem::WaitForAll();
        std::cout << "ordinary_before=" << before << " ordinary_after=" << after
                  << " bodies=" << physics.GetNumBodies() << '\n';
        return after < before && physics.GetNumBodies() == 1 && target.GetPhysicsScene() == nullptr
            && SamePayload(car, Find(target, "LegacyCar"));
    }
    throw std::runtime_error("Unknown scenario: " + scenario);
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: AxleVehicleRetirementReview <scenario> <writable-workspace>\n";
        return 2;
    }
    Log::Init();
    JobSystem::Init(4);
    PhysicsEngine::Init();
    bool passed = false;
    try {
        const auto workspace = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(workspace);
        passed = Run(argv[1], workspace);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
    }
    JobSystem::WaitForAll();
    PhysicsEngine::Shutdown();
    JobSystem::Shutdown();
    std::cout << "RESULT " << argv[1] << ' ' << (passed ? "PASS" : "FAIL") << std::endl;
    return passed ? 0 : 1;
}
