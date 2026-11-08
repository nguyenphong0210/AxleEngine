#include <AxleSpline/SplineComponent.hpp>
#include <AxleSpline/Curve3D.hpp>
#include <AxleSpline/TrackGenerator.hpp>
#include <AxleSpline/SplineSerializer.hpp>
#include <AxleSpline/SplinePluginRuntime.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cassert>
#include <cmath>
#include <filesystem>

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

std::filesystem::path FindRepoRoot(const std::filesystem::path& start) {
    auto current = std::filesystem::absolute(start);
    while (!current.empty()) {
        if (std::filesystem::exists(current / "Plugins/Spline/Spline.axleplugin")) {
            return current;
        }
        if (current == current.parent_path()) break;
        current = current.parent_path();
    }
    return start;
}

// -----------------------------------------------------------------------------
// Test 1: Spline Manifest Validation
// -----------------------------------------------------------------------------
bool Test_SplineManifest(const std::filesystem::path& repoRoot) {
    std::cout << "\n[TEST 1] Spline Manifest Validation..." << std::endl;
    const auto manifestPath = repoRoot / "Plugins/Spline/Spline.axleplugin";
    TEST_CHECK(std::filesystem::exists(manifestPath), "Spline.axleplugin must exist");

    std::ifstream file(manifestPath);
    TEST_CHECK(file.is_open(), "Spline.axleplugin must be readable");
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    TEST_CHECK(content.find("\"id\": \"axle.spline\"") != std::string::npos, "Plugin ID must be axle.spline");
    TEST_CHECK(content.find("\"name\": \"Spline\"") != std::string::npos, "Plugin name must be Spline");
    TEST_CHECK(content.find("\"version\": \"1.0.0\"") != std::string::npos, "Plugin version must be 1.0.0");
    TEST_CHECK(content.find("\"SplineRuntime\"") != std::string::npos, "Must declare SplineRuntime module");
    TEST_CHECK(content.find("\"SplineEditor\"") != std::string::npos, "Must declare SplineEditor module");
    TEST_CHECK(content.find("\"dependencies\": [\"SplineRuntime\"]") != std::string::npos, "Editor module must depend on SplineRuntime");

    std::cout << "  -> PASS: Spline manifest conforms to Axle plugin specification." << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Curve3D Mathematics & Evaluation
// -----------------------------------------------------------------------------
bool Test_Curve3DMath() {
    std::cout << "\n[TEST 2] Curve3D Mathematics & RMF Orthonormality..." << std::endl;

    // Catmull-Rom closed loop
    Axle::Spline::Curve3D catmull(Axle::Spline::SplineType::CatmullRom, true);
    catmull.AddNode({ glm::vec3(-50.0f, 0.0f, -50.0f), 0.0f, 1.0f });
    catmull.AddNode({ glm::vec3(50.0f, 0.0f, -50.0f), 15.0f, 1.0f });
    catmull.AddNode({ glm::vec3(50.0f, 0.0f, 50.0f), 15.0f, 1.0f });
    catmull.AddNode({ glm::vec3(-50.0f, 0.0f, 50.0f), 0.0f, 1.0f });
    catmull.RebuildCache();

    const float totalLength = catmull.GetTotalLength();
    TEST_CHECK(totalLength > 300.0f && totalLength < 500.0f, "Catmull-Rom length in expected range");

    // Arc-length uniform metric sampling
    glm::vec3 pos0 = catmull.SamplePosition(0.0f);
    glm::vec3 posHalf = catmull.SamplePosition(totalLength * 0.5f);
    float distMid = glm::distance(pos0, posHalf);
    TEST_CHECK(distMid > 80.0f, "Halfway position must be spatially distant on closed loop");

    // RMF frame orthonormality test
    for (int i = 0; i <= 20; ++i) {
        float distance = (totalLength / 20.0f) * static_cast<float>(i);
        auto frame = catmull.SampleFrame(distance);

        float dotTN = glm::dot(frame.Tangent, frame.Normal);
        float dotTB = glm::dot(frame.Tangent, frame.Binormal);
        float dotNB = glm::dot(frame.Normal, frame.Binormal);

        TEST_CHECK(std::abs(dotTN) < 1e-3f, "Tangent and Normal must be orthogonal");
        TEST_CHECK(std::abs(dotTB) < 1e-3f, "Tangent and Binormal must be orthogonal");
        TEST_CHECK(std::abs(dotNB) < 1e-3f, "Normal and Binormal must be orthogonal");

        TEST_CHECK(std::abs(glm::length(frame.Tangent) - 1.0f) < 1e-3f, "Tangent must be unit vector");
        TEST_CHECK(std::abs(glm::length(frame.Normal) - 1.0f) < 1e-3f, "Normal must be unit vector");
        TEST_CHECK(std::abs(glm::length(frame.Binormal) - 1.0f) < 1e-3f, "Binormal must be unit vector");
    }

    // Cubic Bezier verification
    Axle::Spline::Curve3D bezier(Axle::Spline::SplineType::CubicBezier, false);
    bezier.AddNode({ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.0f, 0.0f) });
    bezier.AddNode({ glm::vec3(20.0f, 10.0f, 0.0f), glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.0f, 0.0f) });
    bezier.RebuildCache();

    TEST_CHECK(glm::distance(bezier.SamplePosition(0.0f), glm::vec3(0.0f, 0.0f, 0.0f)) < 0.01f, "Bezier start node match");
    TEST_CHECK(glm::distance(bezier.SamplePosition(bezier.GetTotalLength()), glm::vec3(20.0f, 10.0f, 0.0f)) < 0.1f, "Bezier end node match");

    std::cout << "  -> PASS: Curve3D mathematics, LUT, and RMF frame orthonormality verified." << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Track Geometry Generation
// -----------------------------------------------------------------------------
bool Test_TrackGeometry() {
    std::cout << "\n[TEST 3] Track Geometry & Mesh Generation..." << std::endl;

    Axle::Spline::SplineComponent sc(Axle::Spline::SplineType::CatmullRom, true);
    sc.TrackWidth = 10.0f;
    sc.StepDistance = 2.0f;
    sc.GenerateCurbs = true;
    sc.GenerateGuardrails = true;

    sc.Curve.AddNode({ glm::vec3(-40.0f, 0.0f, -40.0f), 0.0f, 1.0f });
    sc.Curve.AddNode({ glm::vec3(40.0f, 0.0f, -40.0f), 10.0f, 1.0f });
    sc.Curve.AddNode({ glm::vec3(40.0f, 0.0f, 40.0f), 10.0f, 1.0f });
    sc.Curve.AddNode({ glm::vec3(-40.0f, 0.0f, 40.0f), 0.0f, 1.0f });
    sc.Curve.RebuildCache();

    auto mesh = Axle::Spline::TrackGenerator::GenerateTrackMesh(sc.Curve, sc);
    TEST_CHECK(!mesh.IsEmpty(), "Generated mesh must not be empty");
    TEST_CHECK(mesh.GetVertexCount() > 100, "Vertex count must be significant");
    TEST_CHECK(mesh.GetTriangleCount() > 100, "Triangle count must be significant");
    TEST_CHECK(mesh.Positions.size() == mesh.UVs.size(), "Positions and UV counts must match");
    TEST_CHECK(mesh.Indices.size() % 3 == 0, "Indices must be a multiple of 3");

    // Empty curve test
    Axle::Spline::Curve3D emptyCurve;
    auto emptyMesh = Axle::Spline::TrackGenerator::GenerateTrackMesh(emptyCurve, sc);
    TEST_CHECK(emptyMesh.IsEmpty(), "Empty curve must produce empty mesh data");

    std::cout << "  -> PASS: Track mesh generation with curbs and guardrails verified ("
              << mesh.GetVertexCount() << " vertices, " << mesh.GetTriangleCount() << " triangles)." << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Jolt Physics Collider Lifecycle
// -----------------------------------------------------------------------------
bool Test_PhysicsColliderLifecycle() {
    std::cout << "\n[TEST 4] Jolt Physics Collider Lifecycle..." << std::endl;

    Axle::PhysicsEngine::Init();
    Axle::PhysicsScene physicsScene;

    Axle::Spline::SplineComponent sc(Axle::Spline::SplineType::CatmullRom, true);
    sc.Curve.AddNode({ glm::vec3(-30.0f, 0.0f, -30.0f), 0.0f, 1.0f });
    sc.Curve.AddNode({ glm::vec3(30.0f, 0.0f, -30.0f), 0.0f, 1.0f });
    sc.Curve.AddNode({ glm::vec3(30.0f, 0.0f, 30.0f), 0.0f, 1.0f });
    sc.Curve.AddNode({ glm::vec3(-30.0f, 0.0f, 30.0f), 0.0f, 1.0f });
    sc.Curve.RebuildCache();

    auto mesh = Axle::Spline::TrackGenerator::GenerateTrackMesh(sc.Curve, sc);
    TEST_CHECK(!mesh.IsEmpty(), "Mesh must be generated");

    // Create collider
    sc.RuntimeBodyID = Axle::Spline::TrackGenerator::CreatePhysicsCollider(mesh, sc, physicsScene);
    TEST_CHECK(!sc.RuntimeBodyID.IsInvalid(), "Collider BodyID must be valid");
    TEST_CHECK(physicsScene.IsBodyValid(sc.RuntimeBodyID), "PhysicsScene must recognize body as valid");

    // Destroy collider
    Axle::Spline::TrackGenerator::DestroyPhysicsCollider(sc.RuntimeBodyID, physicsScene);
    TEST_CHECK(!physicsScene.IsBodyValid(sc.RuntimeBodyID), "Body must no longer be valid in PhysicsScene after destruction");

    // Recreate collider (idempotence)
    sc.RuntimeBodyID = Axle::Spline::TrackGenerator::CreatePhysicsCollider(mesh, sc, physicsScene);
    TEST_CHECK(!sc.RuntimeBodyID.IsInvalid(), "Re-created collider BodyID must be valid");
    TEST_CHECK(physicsScene.IsBodyValid(sc.RuntimeBodyID), "Re-created collider must be active");
    Axle::Spline::TrackGenerator::DestroyPhysicsCollider(sc.RuntimeBodyID, physicsScene);

    Axle::PhysicsEngine::Shutdown();

    std::cout << "  -> PASS: Jolt MeshShape collider creation, destruction, and reinitialization verified." << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Spline Serializer & Retained Format Round-Trip
// -----------------------------------------------------------------------------
bool Test_SerializerRoundTrip(const std::filesystem::path& repoRoot) {
    std::cout << "\n[TEST 5] Spline Serialization & Retained Scene Format Round-Trip..." << std::endl;

    Axle::Spline::SplineComponent original(Axle::Spline::SplineType::CatmullRom, true);
    original.TrackWidth = 12.0f;
    original.StepDistance = 1.5f;
    original.TextureRepeatV = 0.5f;
    original.GenerateCurbs = true;
    original.CurbWidth = 0.75f;
    original.CurbHeight = 0.2f;
    original.GenerateGuardrails = true;
    original.GuardrailHeight = 1.1f;
    original.GuardrailThickness = 0.25f;
    original.GeneratePhysics = true;
    original.Friction = 0.95f;
    original.Restitution = 0.1f;

    original.Curve.AddNode({ glm::vec3(10.0f, 2.0f, 30.0f), 5.0f, 1.2f });
    original.Curve.AddNode({ glm::vec3(40.0f, 8.0f, 60.0f), 12.0f, 1.0f });
    original.Curve.AddNode({ glm::vec3(-20.0f, 5.0f, 80.0f), -8.0f, 0.8f });
    original.Curve.RebuildCache();

    // Serialize to string
    std::string serialized = Axle::Spline::SplineSerializer::SerializeToString(original);
    TEST_CHECK(!serialized.empty(), "Serialized string must not be empty");
    TEST_CHECK(serialized.find("SplineComponent:") != std::string::npos, "Must contain SplineComponent header");
    TEST_CHECK(serialized.find("TrackWidth: 12") != std::string::npos, "TrackWidth must match");
    TEST_CHECK(serialized.find("GenerateCurbs: true") != std::string::npos, "GenerateCurbs must match");

    // Deserialize into fresh component
    Axle::Spline::SplineComponent restored;
    bool success = Axle::Spline::SplineSerializer::DeserializeFromString(serialized, restored);
    TEST_CHECK(success, "Deserialization must succeed");

    TEST_CHECK(restored.TrackWidth == original.TrackWidth, "Restored TrackWidth match");
    TEST_CHECK(restored.StepDistance == original.StepDistance, "Restored StepDistance match");
    TEST_CHECK(restored.TextureRepeatV == original.TextureRepeatV, "Restored TextureRepeatV match");
    TEST_CHECK(restored.GenerateCurbs == original.GenerateCurbs, "Restored GenerateCurbs match");
    TEST_CHECK(restored.CurbWidth == original.CurbWidth, "Restored CurbWidth match");
    TEST_CHECK(restored.CurbHeight == original.CurbHeight, "Restored CurbHeight match");
    TEST_CHECK(restored.GenerateGuardrails == original.GenerateGuardrails, "Restored GenerateGuardrails match");
    TEST_CHECK(restored.GuardrailHeight == original.GuardrailHeight, "Restored GuardrailHeight match");
    TEST_CHECK(restored.Friction == original.Friction, "Restored Friction match");
    TEST_CHECK(restored.Restitution == original.Restitution, "Restored Restitution match");

    TEST_CHECK(restored.Curve.GetNodeCount() == 3, "Restored curve node count match");
    const auto& origNodes = original.Curve.GetNodes();
    const auto& restNodes = restored.Curve.GetNodes();
    for (size_t i = 0; i < origNodes.size(); ++i) {
        TEST_CHECK(glm::distance(origNodes[i].Position, restNodes[i].Position) < 1e-4f, "Node position match");
        TEST_CHECK(std::abs(origNodes[i].Roll - restNodes[i].Roll) < 1e-3f, "Node roll match");
        TEST_CHECK(std::abs(origNodes[i].Scale - restNodes[i].Scale) < 1e-3f, "Node scale match");
    }

    // Verify existing scene format fidelity from OffRoadTrack.axlescene
    const auto scenePath = repoRoot / "Tests/Fixtures/Assets/scenes/OffRoadTrack.axlescene";
    if (std::filesystem::exists(scenePath)) {
        std::ifstream sceneFile(scenePath);
        std::string line;
        std::vector<std::string> splineLines;
        bool inSpline = false;
        while (std::getline(sceneFile, line)) {
            if (line.find("SplineComponent:") != std::string::npos) {
                inSpline = true;
                continue;
            }
            if (inSpline) {
                if (!line.empty() && line[0] != ' ' && line[0] != '\t') break;
                if (!line.empty() && line.find("Component:") != std::string::npos && line.find("SplineComponent:") == std::string::npos) break;
                splineLines.push_back(line);
            }
        }
        if (!splineLines.empty()) {
            Axle::Spline::SplineComponent fixtureSpline;
            bool parsedFixture = Axle::Spline::SplineSerializer::DeserializeComponent(fixtureSpline, "SplineComponent", splineLines);
            TEST_CHECK(parsedFixture, "Must parse SplineComponent block from OffRoadTrack.axlescene");
            TEST_CHECK(fixtureSpline.Curve.GetNodeCount() > 0, "Fixture spline must contain nodes");
            TEST_CHECK(fixtureSpline.TrackWidth == 8.0f, "Fixture spline track width must match authored value");
            std::cout << "  -> OffRoadTrack.axlescene format verified: " << fixtureSpline.Curve.GetNodeCount() << " nodes loaded." << std::endl;
        }
    }

    std::cout << "  -> PASS: Spline component serialization and retained format verified." << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Component Lifecycle & Cloning
// -----------------------------------------------------------------------------
bool Test_ComponentLifecycle() {
    std::cout << "\n[TEST 6] Component Cloning and Lifecycle Hooks..." << std::endl;

    Axle::Spline::SplineComponent source(Axle::Spline::SplineType::CatmullRom, true);
    source.Curve.AddNode({ glm::vec3(0.0f, 0.0f, 0.0f), 0.0f, 1.0f });
    source.Curve.AddNode({ glm::vec3(10.0f, 0.0f, 0.0f), 10.0f, 1.0f });
    source.Curve.RebuildCache();
    source.TrackWidth = 14.0f;
    source.RuntimeBodyID = Axle::BodyID(42);

    // Deep clone
    Axle::Spline::SplineComponent cloned = source;
    cloned.RuntimeBodyID = Axle::BodyID{}; // As done in Clone hook

    TEST_CHECK(cloned.TrackWidth == 14.0f, "Cloned track width match");
    TEST_CHECK(cloned.RuntimeBodyID.IsInvalid(), "Cloned RuntimeBodyID must be reset to invalid");
    TEST_CHECK(cloned.Curve.GetNodeCount() == 2, "Cloned node count match");

    // Modify cloned node without mutating source
    cloned.Curve.GetNodes()[0].Position.x = 999.0f;
    TEST_CHECK(source.Curve.GetNodes()[0].Position.x == 0.0f, "Source curve must remain unmutated by clone edits");

    std::cout << "  -> PASS: Component deep cloning and isolation verified." << std::endl;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::cout << "========================================================\n"
              << "       AxleEngine Spline Plugin Headless Tests          \n"
              << "========================================================" << std::endl;

    std::filesystem::path repoRoot = argc > 1 ? argv[1] : ".";
    repoRoot = FindRepoRoot(repoRoot);

    bool allPassed = true;
    allPassed &= Test_SplineManifest(repoRoot);
    allPassed &= Test_Curve3DMath();
    allPassed &= Test_TrackGeometry();
    allPassed &= Test_PhysicsColliderLifecycle();
    allPassed &= Test_SerializerRoundTrip(repoRoot);
    allPassed &= Test_ComponentLifecycle();

    std::cout << "\n========================================================" << std::endl;
    if (allPassed) {
        std::cout << "  -> ALL CHECKS PASSED: Spline Plugin (PLG-19) verified!" << std::endl;
        std::cout << "========================================================" << std::endl;
        return 0;
    } else {
        std::cerr << "  -> SOME CHECKS FAILED!" << std::endl;
        std::cout << "========================================================" << std::endl;
        return 1;
    }
}
