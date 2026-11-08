#include <Axle/Entity.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <AxleTerrain/TerrainComponent.hpp>
#include <AxleTerrain/TerrainData.hpp>
#include <AxleTerrain/TerrainMesh.hpp>
#include <AxleTerrain/TerrainCollider.hpp>
#include <AxleTerrain/TerrainSerializer.hpp>
#include <AxleTerrain/TerrainSplineCarver.hpp>
#include <AxleTerrain/TerrainPluginRuntime.hpp>
#include <AxleTerrainEditor/TerrainSculptor.hpp>
#include <AxleSpline/SplineComponent.hpp>

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

std::filesystem::path FindRepoRoot(const std::filesystem::path& start) {
    auto current = std::filesystem::absolute(start);
    while (!current.empty()) {
        if (std::filesystem::exists(current / "Plugins/Terrain/Terrain.axleplugin")) {
            return current;
        }
        if (current == current.parent_path()) break;
        current = current.parent_path();
    }
    return start;
}

bool Test_PluginManifest(const std::filesystem::path& repoRoot) {
    std::filesystem::path manifestPath = repoRoot / "Plugins/Terrain/Terrain.axleplugin";
    TEST_CHECK(std::filesystem::exists(manifestPath), "Terrain.axleplugin manifest must exist");

    std::ifstream file(manifestPath);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    TEST_CHECK(content.find("\"id\": \"axle.terrain\"") != std::string::npos, "Manifest id must be axle.terrain");
    TEST_CHECK(content.find("\"TerrainRuntime\"") != std::string::npos, "Must declare TerrainRuntime module");
    TEST_CHECK(content.find("\"TerrainEditor\"") != std::string::npos, "Must declare TerrainEditor module");
    TEST_CHECK(content.find("\"axle.spline:SplineRuntime\"") != std::string::npos, "TerrainRuntime must depend on axle.spline:SplineRuntime");

    std::cout << "[PASS] Test_PluginManifest\n";
    return true;
}

bool Test_TerrainDataMath() {
    Axle::Terrain::TerrainData data(64, 64, 2.0f, 2.0f);
    TEST_CHECK(data.GetResolutionX() == 64, "ResX must be 64");
    TEST_CHECK(data.GetResolutionZ() == 64, "ResZ must be 64");
    TEST_CHECK(std::abs(data.GetWidth() - 126.0f) < 1e-4f, "Width must be (64-1)*2 = 126m");
    TEST_CHECK(std::abs(data.GetLength() - 126.0f) < 1e-4f, "Length must be (64-1)*2 = 126m");

    data.GenerateFlat(10.0f);
    TEST_CHECK(std::abs(data.GetHeight(0, 0) - 10.0f) < 1e-4f, "Flat height must be 10.0");
    TEST_CHECK(std::abs(data.SampleHeight(30.0f, 30.0f) - 10.0f) < 1e-4f, "Sampled height must be 10.0");

    data.SetHeight(10, 10, 25.0f);
    TEST_CHECK(std::abs(data.GetHeight(10, 10) - 25.0f) < 1e-4f, "Height at (10,10) must be 25.0");

    glm::vec3 normal = data.CalculateGridNormal(10, 10);
    float len = glm::length(normal);
    TEST_CHECK(std::abs(len - 1.0f) < 1e-3f, "Normal must be normalized unit vector");

    data.AutoGenerateSplatmap();
    const auto& splat = data.GetSplatWeight(0, 0);
    float sumWeights = splat.r + splat.g + splat.b + splat.a;
    TEST_CHECK(std::abs(sumWeights - 1.0f) < 1e-3f, "Splat weights must sum to 1.0");

    std::cout << "[PASS] Test_TerrainDataMath\n";
    return true;
}

bool Test_TerrainChunking() {
    Axle::Terrain::TerrainData data(64, 64, 1.0f, 1.0f);
    data.GenerateHills(0.05f, 5.0f, 2);

    std::vector<Axle::Terrain::TerrainChunk> chunks;
    Axle::Terrain::TerrainMesh::InitializeChunks(data, chunks, 32);
    TEST_CHECK(chunks.size() == 4, "64x64 grid with chunk size 32 should produce 2x2 = 4 chunks");

    Axle::Terrain::TerrainMesh::BuildAllChunksParallel(data, chunks);
    for (const auto& chunk : chunks) {
        TEST_CHECK(!chunk.Vertices.empty(), "Chunk vertices must not be empty");
        TEST_CHECK(!chunk.Indices.empty(), "Chunk indices must not be empty");
        TEST_CHECK(chunk.Indices.size() % 3 == 0, "Chunk indices must be multiples of 3");
    }

    auto vao = Axle::Terrain::TerrainMesh::BuildCombinedVAO(data);
    TEST_CHECK(vao != nullptr, "Combined VAO must be generated");

    std::cout << "[PASS] Test_TerrainChunking\n";
    return true;
}

bool Test_TerrainPhysicsCollider() {
    Axle::Terrain::TerrainData data(32, 32, 1.0f, 1.0f);
    data.GenerateHills(0.05f, 4.0f, 2);

    Axle::PhysicsScene physicsScene;

    Axle::BodyID bodyID = Axle::Terrain::TerrainCollider::CreateTerrainCollider(data, physicsScene, 0.8f, 0.1f);
    TEST_CHECK(!bodyID.IsInvalid(), "Terrain collider body must be valid");
    TEST_CHECK(physicsScene.IsBodyValid(bodyID), "PhysicsScene must recognize body as valid");

    // Raycast test
    glm::vec3 rayOrigin(15.0f, 50.0f, 15.0f);
    glm::vec3 rayDir(0.0f, -1.0f, 0.0f);
    glm::vec3 hitPoint{ 0.0f };
    bool hit = Axle::Terrain::TerrainCollider::RaycastTerrain(rayOrigin, rayDir, data, hitPoint);
    TEST_CHECK(hit, "Raycast downward from above terrain should hit surface");
    TEST_CHECK(std::abs(hitPoint.x - 15.0f) < 1.0f && std::abs(hitPoint.z - 15.0f) < 1.0f, "Hit X/Z should match ray");

    // Update body
    Axle::BodyID updatedID = Axle::Terrain::TerrainCollider::UpdateTerrainCollider(bodyID, data, physicsScene, 0.8f, 0.1f);
    TEST_CHECK(!updatedID.IsInvalid(), "Updated body ID must be valid");

    Axle::Terrain::TerrainCollider::DestroyTerrainCollider(updatedID, physicsScene);
    TEST_CHECK(!physicsScene.IsBodyValid(updatedID), "Body must be destroyed in physics scene");

    std::cout << "[PASS] Test_TerrainPhysicsCollider\n";
    return true;
}

bool Test_TerrainSplineCarver() {
    Axle::Terrain::TerrainData data(64, 64, 1.0f, 1.0f);
    data.GenerateHills(0.05f, 10.0f, 2);

    Axle::Spline::SplineComponent splineComp(Axle::Spline::SplineType::CatmullRom, false);
    splineComp.Curve.AddNode({ glm::vec3(10.0f, 5.0f, 10.0f), 0.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(30.0f, 5.0f, 30.0f), 0.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(50.0f, 5.0f, 50.0f), 0.0f, 1.0f });
    splineComp.Curve.RebuildCache();

    // Snap spline to terrain
    bool snapped = Axle::Terrain::TerrainSplineCarver::SnapSplineToTerrain(splineComp, data, 0.2f);
    TEST_CHECK(snapped, "SnapSplineToTerrain must succeed");

    float expectedHeight = data.SampleHeight(30.0f, 30.0f) + 0.2f;
    TEST_CHECK(std::abs(splineComp.Curve.GetNodes()[1].Position.y - expectedHeight) < 1e-3f, "Snapped node Y must match terrain height + offset");

    // Carve spline road into terrain
    Axle::Terrain::CarveSettings carveSettings;
    carveSettings.RoadWidth = 6.0f;
    carveSettings.ShoulderWidth = 4.0f;
    bool carved = Axle::Terrain::TerrainSplineCarver::CarveSplineRoad(data, splineComp.Curve, carveSettings);
    TEST_CHECK(carved, "CarveSplineRoad must succeed");

    std::cout << "[PASS] Test_TerrainSplineCarver\n";
    return true;
}

bool Test_TerrainSculpting() {
    Axle::Terrain::TerrainData data(64, 64, 1.0f, 1.0f);
    data.GenerateFlat(0.0f);

    Axle::Terrain::SculptSettings settings;
    settings.Mode = Axle::Terrain::BrushMode::Raise;
    settings.Falloff = Axle::Terrain::BrushFalloff::Smoothstep;
    settings.Radius = 10.0f;
    settings.Strength = 5.0f;

    auto result = Axle::Terrain::TerrainSculptor::ApplyBrush(data, 32.0f, 32.0f, settings, 0.1f);
    TEST_CHECK(result.Modified, "Brush deformation must modify terrain");

    float centerHeight = data.SampleHeight(32.0f, 32.0f);
    TEST_CHECK(centerHeight > 0.0f, "Center height must be raised");

    std::cout << "[PASS] Test_TerrainSculpting\n";
    return true;
}

bool Test_TerrainSerializationAndCloning() {
    Axle::Terrain::TerrainComponent tc(128, 128, 1.5f, 1.5f);
    tc.UseChunkedLOD = true;
    tc.ChunkSize = 16;
    tc.Friction = 0.92f;
    tc.Restitution = 0.08f;
    tc.DataFilePath = "assets/terrain/test.axleterrain";

    std::string text = Axle::Terrain::TerrainSerializer::SerializeToString(tc);
    TEST_CHECK(text.find("ResolutionX: 128") != std::string::npos, "Serialized text must contain ResolutionX");
    TEST_CHECK(text.find("SpacingX: 1.5") != std::string::npos, "Serialized text must contain SpacingX");
    TEST_CHECK(text.find("Friction: 0.92") != std::string::npos, "Serialized text must contain Friction");

    Axle::Terrain::TerrainComponent deserialized;
    bool ok = Axle::Terrain::TerrainSerializer::DeserializeFromString(deserialized, text);
    TEST_CHECK(ok, "DeserializeFromString must succeed");
    TEST_CHECK(deserialized.Data.GetResolutionX() == 128, "Deserialized ResX must match");
    TEST_CHECK(std::abs(deserialized.Data.GetSpacingX() - 1.5f) < 1e-4f, "Deserialized spacing must match");
    TEST_CHECK(deserialized.UseChunkedLOD == true, "Deserialized UseChunkedLOD must match");
    TEST_CHECK(deserialized.ChunkSize == 16, "Deserialized ChunkSize must match");
    TEST_CHECK(std::abs(deserialized.Friction - 0.92f) < 1e-4f, "Deserialized friction must match");

    // Test cloning
    Axle::Terrain::TerrainComponent clone = deserialized;
    TEST_CHECK(clone.Data.GetResolutionX() == 128, "Cloned ResX must match");
    TEST_CHECK(clone.PhysicsBodyID.IsInvalid(), "Cloned body ID must be invalid/reset");

    std::cout << "[PASS] Test_TerrainSerializationAndCloning\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Axle::Log::Init();
    Axle::JobSystem::Init(2);
    Axle::PhysicsEngine::Init();

    std::cout << "Running AxleTerrainPluginTests...\n";

    std::filesystem::path startPath = (argc > 1) ? argv[1] : ".";
    std::filesystem::path repoRoot = FindRepoRoot(startPath);

    if (!Test_PluginManifest(repoRoot)) return 1;
    if (!Test_TerrainDataMath()) return 1;
    if (!Test_TerrainChunking()) return 1;
    if (!Test_TerrainPhysicsCollider()) return 1;
    if (!Test_TerrainSplineCarver()) return 1;
    if (!Test_TerrainSculpting()) return 1;
    if (!Test_TerrainSerializationAndCloning()) return 1;

    std::cout << "All Terrain Plugin Tests PASSED successfully!\n";
    Axle::JobSystem::Shutdown();
    return 0;
}
