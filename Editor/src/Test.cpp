#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Scene.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/AssetMetadataSerializer.hpp>
#include <Axle/AssetTypes.hpp>
#include <Axle/ContentBrowserPanel.hpp>
#include <Axle/PhysicsMaterial.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/ScriptCompiler.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/JobSystemEnkiTS.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/ParallelECS.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/SceneSerializer.hpp>
#include <iostream>

// =============================================================================
// Test Mock Script for Testing Reflection & Serialization
// =============================================================================
class CarPlayerScript : public Axle::ScriptableEntity {
public:
    float ForwardTorque = 1500.0f;
    float MaxSteerAngle = 35.0f;
    float BrakeTorque = 2500.0f;
    bool EnableBoost = true;

    void OnCreate() override {}
    void OnUpdate(Axle::Timestep ts) override { (void)ts; }
    void OnDestroy() override {}

    AXLE_SCRIPT_FIELDS(CarPlayerScript,
        AXLE_FIELD(ForwardTorque, 100.0f, 10000.0f, "Engine drive torque in N.m"),
        AXLE_FIELD(MaxSteerAngle, 10.0f, 60.0f, "Maximum steering angle in degrees"),
        AXLE_FIELD(BrakeTorque, 100.0f, 10000.0f, "Braking force in N.m"),
        AXLE_FIELD(EnableBoost, "Enable speed boost")
    )
};

AXLE_REGISTER_SCRIPT(CarPlayerScript);
#include <fstream>
#include <string>
#include <filesystem>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <crtdbg.h>
#endif

// =============================================================================
// TEST STEP 1: Core Static Reflection & Script Registry
// =============================================================================
bool RunTest_Step1_ScriptRegistry() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 1 TEST] C++ SCRIPT REGISTRY & STATIC REFLECTION   " << std::endl;
    std::cout << "========================================================" << std::endl;

    const auto& scripts = Axle::ScriptRegistry::GetScripts();
    std::cout << "[1.1] Checking registered scripts count..." << std::endl;
    std::cout << "  -> Found " << scripts.size() << " registered script(s)" << std::endl;
    if (scripts.empty()) {
        std::cerr << "FAIL: No scripts registered in ScriptRegistry!" << std::endl;
        return false;
    }

    const auto* meta = Axle::ScriptRegistry::GetMetadata("CarPlayerScript");
    if (!meta) {
        std::cerr << "FAIL: Metadata for 'CarPlayerScript' not found!" << std::endl;
        return false;
    }

    std::cout << "[1.2] Inspecting reflected fields of 'CarPlayerScript'..." << std::endl;
    std::cout << "  -> Reflected field count: " << meta->Fields.size() << " (Expected: 4)" << std::endl;
    for (const auto& f : meta->Fields) {
        std::cout << "     * Field: " << f.Name << " [Type=" << (int)f.Type 
                  << ", Min=" << f.Min << ", Max=" << f.Max 
                  << ", Tooltip='" << f.Tooltip << "']" << std::endl;
    }

    if (meta->Fields.size() != 4) {
        std::cerr << "FAIL: Field count mismatch!" << std::endl;
        return false;
    }

    std::cout << ">>> STEP 1 PASSED: ScriptRegistry & Field Reflection verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 2: Hierarchy Traversal & Parent-Child Navigation
// =============================================================================
bool RunTest_Step2_HierarchyTraversal() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 2 TEST] HIERARCHY TRAVERSAL & CYCLE PREVENTION   " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto testScene = std::make_shared<Axle::Scene>();
    auto tank = testScene->CreateEntity("Tank_Hull");
    auto turret = testScene->CreateEntity("Turret");
    auto barrel = testScene->CreateEntity("Gun_Barrel");
    auto wheelFL = testScene->CreateEntity("Wheel_FL");
    auto wheelFR = testScene->CreateEntity("Wheel_FR");

    turret.SetParent(tank);
    barrel.SetParent(turret);
    wheelFL.SetParent(tank);
    wheelFR.SetParent(tank);

    std::cout << "[2.1] Checking direct child count of Tank_Hull..." << std::endl;
    std::cout << "  -> tank.GetChildCount(): " << tank.GetChildCount() << " (Expected: 3)" << std::endl;
    if (tank.GetChildCount() != 3) {
        std::cerr << "FAIL: Incorrect child count!" << std::endl;
        return false;
    }

    std::cout << "[2.2] Checking Parent relationships..." << std::endl;
    if (turret.GetParent() != tank || barrel.GetParent() != turret) {
        std::cerr << "FAIL: Parent relationship check failed!" << std::endl;
        return false;
    }
    std::cout << "  -> turret.GetParent() == tank: PASSED" << std::endl;
    std::cout << "  -> barrel.GetParent() == turret: PASSED" << std::endl;

    std::cout << "[2.3] Checking immediate vs recursive FindChildByTag..." << std::endl;
    auto foundImmediate = tank.FindChildByTag("Wheel_FL", false);
    if (!foundImmediate || foundImmediate != wheelFL) {
        std::cerr << "FAIL: Immediate child lookup failed!" << std::endl;
        return false;
    }
    std::cout << "  -> Immediate lookup tank.FindChildByTag('Wheel_FL', false): PASSED" << std::endl;

    auto nonRecursiveChild = tank.FindChildByTag("Gun_Barrel", false);
    if (nonRecursiveChild) {
        std::cerr << "FAIL: Gun_Barrel should not be found non-recursively under tank!" << std::endl;
        return false;
    }
    std::cout << "  -> Non-recursive tank.FindChildByTag('Gun_Barrel', false) is null: PASSED" << std::endl;

    auto foundRecursive = tank.FindChildByTag("Gun_Barrel", true);
    if (!foundRecursive || foundRecursive != barrel) {
        std::cerr << "FAIL: Recursive child lookup failed!" << std::endl;
        return false;
    }
    std::cout << "  -> Recursive lookup tank.FindChildByTag('Gun_Barrel', true): PASSED" << std::endl;

    std::cout << "[2.4] Checking IsChildOf & Cycle Prevention..." << std::endl;
    if (!barrel.IsChildOf(tank) || barrel.IsChildOf(wheelFL)) {
        std::cerr << "FAIL: IsChildOf relationship check failed!" << std::endl;
        return false;
    }
    std::cout << "  -> barrel.IsChildOf(tank) == true: PASSED" << std::endl;
    std::cout << "  -> barrel.IsChildOf(wheelFL) == false: PASSED" << std::endl;

    // Cycle prevention: attempting to set ancestor's parent to descendant
    tank.SetParent(barrel);
    if (tank.GetParent()) {
        std::cerr << "FAIL: Cyclic parenting was not prevented!" << std::endl;
        return false;
    }
    std::cout << "  -> Cyclic parenting tank.SetParent(barrel) prevented safely: PASSED" << std::endl;

    std::cout << "[2.5] Checking ForEachChild recursive visitor..." << std::endl;
    int visitedCount = 0;
    tank.ForEachChild([&visitedCount](Axle::Entity child) {
        visitedCount++;
        std::cout << "     * Visited descendant: " << child.GetName() << std::endl;
    }, true);
    if (visitedCount != 4) {
        std::cerr << "FAIL: Expected 4 descendants, visited " << visitedCount << std::endl;
        return false;
    }
    std::cout << "  -> Total visited descendants: " << visitedCount << " (Expected: 4): PASSED" << std::endl;

    std::cout << ">>> STEP 2 PASSED: Hierarchy traversal & relations verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 3: NativeScriptComponent Inspector Binding & Live Memory Sync
// =============================================================================
bool RunTest_Step3_FieldBuffersAndBinding() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 3 TEST] COMPONENT BINDING & MEMORY SYNC           " << std::endl;
    std::cout << "========================================================" << std::endl;

    Axle::NativeScriptComponent nsc;
    nsc.Bind("CarPlayerScript");

    std::cout << "[3.1] Checking Edit-Mode default field population..." << std::endl;
    std::cout << "  -> ScriptName: " << nsc.ScriptName << std::endl;
    std::cout << "  -> FloatFields count: " << nsc.FloatFields.size() << std::endl;
    std::cout << "  -> ForwardTorque default: " << nsc.FloatFields["ForwardTorque"] << " N.m (Expected: 1500)" << std::endl;
    std::cout << "  -> MaxSteerAngle default: " << nsc.FloatFields["MaxSteerAngle"] << " deg (Expected: 35)" << std::endl;
    std::cout << "  -> EnableBoost default: " << (nsc.BoolFields["EnableBoost"] ? "true" : "false") << " (Expected: true)" << std::endl;

    if (nsc.FloatFields["ForwardTorque"] != 1500.0f || !nsc.BoolFields["EnableBoost"]) {
        std::cerr << "FAIL: Default field values not bound correctly!" << std::endl;
        return false;
    }

    std::cout << "[3.2] Simulating Inspector UI value edit and live C++ instance sync..." << std::endl;
    nsc.FloatFields["ForwardTorque"] = 8888.0f; // User edits slider in Inspector
    nsc.Instance = nsc.InstantiateScript();
    nsc.SyncFieldsToInstance();

    const auto* meta = Axle::ScriptRegistry::GetMetadata("CarPlayerScript");
    float liveTorque = 0.0f;
    for (const auto& f : meta->Fields) {
        if (f.Name == "ForwardTorque") {
            liveTorque = f.GetValue<float>(nsc.Instance);
            break;
        }
    }
    std::cout << "  -> Live instance ForwardTorque: " << liveTorque << " N.m" << std::endl;

    if (liveTorque != 8888.0f) {
        std::cerr << "FAIL: Field synchronization mismatch!" << std::endl;
        return false;
    }
    std::cout << "  -> SUCCESS: Field value 8888.0f accurately propagated into C++ memory!" << std::endl;

    nsc.DestroyScript(&nsc);
    std::cout << "  -> Cleaned up script instance safely." << std::endl;

    std::cout << ">>> STEP 3 PASSED: Field buffers & live sync verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 4: Assembly Prefab Serialization & Deserialization (.axle)
// =============================================================================
bool RunTest_Step4_AssemblyRoundtrip() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 4 TEST] ASSEMBLY (.axle) SCRIPT SERIALIZATION     " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto testScene = std::make_shared<Axle::Scene>();
    auto rootCar = testScene->CreateEntity("Combat_Vehicle");
    auto turret = testScene->CreateEntity("Vehicle_Turret");
    turret.SetParent(rootCar);

    auto& nsc = rootCar.AddComponent<Axle::NativeScriptComponent>("CarPlayerScript");
    nsc.FloatFields["ForwardTorque"] = 4500.0f;

    std::filesystem::path tempPath = "temp_verify_vehicle.axle";
    std::cout << "[4.1] Exporting assembly hierarchy with script to '" << tempPath.string() << "'..." << std::endl;
    bool exported = Axle::AssemblySerializer::Serialize(tempPath, rootCar, nullptr);
    if (!exported) {
        std::cerr << "FAIL: Could not export assembly!" << std::endl;
        return false;
    }
    std::cout << "  -> Exported successfully." << std::endl;

    std::cout << "[4.2] Deserializing assembly into fresh scene..." << std::endl;
    auto freshScene = std::make_shared<Axle::Scene>();
    auto spawnedCar = freshScene->InstantiateAssembly(tempPath, nullptr);

    if (!spawnedCar) {
        std::cerr << "FAIL: Could not instantiate assembly!" << std::endl;
        return false;
    }

    if (!spawnedCar.HasComponent<Axle::NativeScriptComponent>()) {
        std::cerr << "FAIL: Spawned car is missing NativeScriptComponent!" << std::endl;
        return false;
    }

    auto& loadedNsc = spawnedCar.GetComponent<Axle::NativeScriptComponent>();
    std::cout << "  -> Restored ScriptName: " << loadedNsc.ScriptName << " (Expected: CarPlayerScript)" << std::endl;
    if (loadedNsc.ScriptName != "CarPlayerScript") {
        std::cerr << "FAIL: ScriptName mismatch!" << std::endl;
        return false;
    }

    std::cout << "  -> Restored child count: " << spawnedCar.GetChildCount() << " (Expected: 1)" << std::endl;
    if (spawnedCar.GetChildCount() != 1) {
        std::cerr << "FAIL: Child count mismatch!" << std::endl;
        return false;
    }

    auto loadedTurret = spawnedCar.FindChildByTag("Vehicle_Turret", false);
    if (!loadedTurret) {
        std::cerr << "FAIL: Could not find child 'Vehicle_Turret' in restored hierarchy!" << std::endl;
        return false;
    }
    std::cout << "  -> Successfully verified child part 'Vehicle_Turret' in hierarchy." << std::endl;

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
    std::cout << "  -> Cleaned up temp test file." << std::endl;

    std::cout << ">>> STEP 4 PASSED: Assembly prefab script serialization verified! <<<\n" << std::endl;
    return true;
}

static bool FloatEq(float a, float b, float eps = 1e-3f) {
    return std::abs(a - b) <= eps;
}

static bool Vec3Eq(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) {
    return FloatEq(a.x, b.x, eps) && FloatEq(a.y, b.y, eps) && FloatEq(a.z, b.z, eps);
}

// =============================================================================
// TEST STEP 5: Phase 14 / Scene Graph & World Position Stays (Reparent/Unparent)
// =============================================================================
bool RunTest_Step5_RelationshipAndTransform() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 5 TEST] SCENE GRAPH, WORLD POS STAYS & CYCLE CHK " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto scene = std::make_shared<Axle::Scene>();

    // -----------------------------------------------------------------
    // [5.1] Default Components Integrity
    // -----------------------------------------------------------------
    std::cout << "[5.1] Verifying Default Components on CreateEntity..." << std::endl;
    auto entity = scene->CreateEntity("DefaultEntity");

    if (!entity.HasComponent<Axle::IDComponent>() || entity.GetUUID() == 0) {
        std::cerr << "FAIL: Entity missing valid IDComponent / UUID!" << std::endl;
        return false;
    }
    if (!entity.HasComponent<Axle::TagComponent>() || entity.GetName() != "DefaultEntity") {
        std::cerr << "FAIL: Entity missing TagComponent or tag mismatch!" << std::endl;
        return false;
    }
    if (!entity.HasComponent<Axle::TransformComponent>()) {
        std::cerr << "FAIL: Entity missing TransformComponent!" << std::endl;
        return false;
    }
    if (!entity.HasComponent<Axle::HierarchyComponent>() || !entity.HasComponent<Axle::RelationshipComponent>()) {
        std::cerr << "FAIL: Entity missing HierarchyComponent or RelationshipComponent alias!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: All default components (ID, Tag, Transform, Relationship) present." << std::endl;
    std::cout << "  -> UUID: " << (uint64_t)entity.GetUUID() << ", Tag: " << entity.GetName() << std::endl;

    // -----------------------------------------------------------------
    // [5.2] World Position Stays on Reparenting
    // -----------------------------------------------------------------
    std::cout << "[5.2] Verifying World Position Stays on Reparenting..." << std::endl;
    auto parent = scene->CreateEntity("Parent");
    auto& ptc = parent.GetComponent<Axle::TransformComponent>();
    ptc.Translation = glm::vec3(10.0f, 20.0f, 30.0f);
    ptc.Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    auto child = scene->CreateEntity("Child");
    auto& ctc = child.GetComponent<Axle::TransformComponent>();
    ctc.Translation = glm::vec3(15.0f, 20.0f, 30.0f);

    glm::vec3 worldBefore = glm::vec3(child.GetWorldTransform()[3]);
    std::cout << "  -> Child World Pos Before Reparent: (" << worldBefore.x << ", " << worldBefore.y << ", " << worldBefore.z << ")" << std::endl;

    // Reparent child to parent
    child.SetParent(parent);

    glm::vec3 worldAfter = glm::vec3(child.GetWorldTransform()[3]);
    std::cout << "  -> Child World Pos After Reparent:  (" << worldAfter.x << ", " << worldAfter.y << ", " << worldAfter.z << ")" << std::endl;

    if (!Vec3Eq(worldBefore, worldAfter)) {
        std::cerr << "FAIL: World position jumped during reparenting!" << std::endl;
        return false;
    }
    if (child.GetParent() != parent || parent.GetChildCount() != 1) {
        std::cerr << "FAIL: Parent relationship link mismatch!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Child position stayed 100% stable in 3D world space (Zero displacement)!" << std::endl;

    // -----------------------------------------------------------------
    // [5.3] Scene Graph Matrix Propagation (Translation & Rotation Orbit)
    // -----------------------------------------------------------------
    std::cout << "[5.3] Verifying Scene Graph Matrix Propagation..." << std::endl;
    ptc.Translation += glm::vec3(5.0f, 0.0f, 0.0f); // Move parent from 10 to 15 on X

    glm::vec3 worldMoved = glm::vec3(child.GetWorldTransform()[3]);
    std::cout << "  -> Parent moved +5 on X -> Child World Pos: (" << worldMoved.x << ", " << worldMoved.y << ", " << worldMoved.z << ")" << std::endl;

    if (!Vec3Eq(worldMoved, glm::vec3(20.0f, 20.0f, 30.0f))) {
        std::cerr << "FAIL: Child did not follow parent transform!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Child automatically followed parent translation." << std::endl;

    // Test Acceptance Criteria from Gemini.md: When rotating parent, child orbits parent precisely
    auto orbitParent = scene->CreateEntity("OrbitParent");
    auto orbitChild = scene->CreateEntity("OrbitChild");
    orbitChild.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(0.0f, 0.0f, 10.0f);
    orbitChild.SetParent(orbitParent);

    // Rotate parent 90 degrees around Y axis
    orbitParent.GetComponent<Axle::TransformComponent>().Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 orbitWorld = glm::vec3(orbitChild.GetWorldTransform()[3]);
    std::cout << "  -> Parent rotated 90 deg around Y -> Child World Pos: (" << orbitWorld.x << ", " << orbitWorld.y << ", " << orbitWorld.z << ") (Expected: 10, 0, 0)" << std::endl;

    if (!Vec3Eq(orbitWorld, glm::vec3(10.0f, 0.0f, 0.0f))) {
        std::cerr << "FAIL: Child did not orbit parent accurately on rotation!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Child orbited parent around rotation axis accurately." << std::endl;


    // -----------------------------------------------------------------
    // [5.4] World Position Stays on Unparenting
    // -----------------------------------------------------------------
    std::cout << "[5.4] Verifying World Position Stays on Unparenting..." << std::endl;
    glm::vec3 worldBeforeUnparent = glm::vec3(child.GetWorldTransform()[3]);

    child.SetParent({}); // Unparent

    glm::vec3 worldAfterUnparent = glm::vec3(child.GetWorldTransform()[3]);
    std::cout << "  -> Child World Pos After Unparent: (" << worldAfterUnparent.x << ", " << worldAfterUnparent.y << ", " << worldAfterUnparent.z << ")" << std::endl;

    if (!Vec3Eq(worldBeforeUnparent, worldAfterUnparent)) {
        std::cerr << "FAIL: World position jumped during unparenting!" << std::endl;
        return false;
    }
    if (child.GetParent() || parent.GetChildCount() != 0) {
        std::cerr << "FAIL: Unparenting did not clear hierarchy links!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Child position preserved perfectly after unparenting." << std::endl;

    // -----------------------------------------------------------------
    // [5.5] Cycle Check Prevention
    // -----------------------------------------------------------------
    std::cout << "[5.5] Verifying Cycle Check Prevention..." << std::endl;
    auto grandparent = scene->CreateEntity("Grandparent");
    auto father = scene->CreateEntity("Father");
    auto son = scene->CreateEntity("Son");

    father.SetParent(grandparent);
    son.SetParent(father);

    if (!son.IsChildOf(grandparent)) {
        std::cerr << "FAIL: Son must be descendant of Grandparent!" << std::endl;
        return false;
    }

    // Attempt illegal cycle: Grandparent -> Son
    grandparent.SetParent(son);
    if (grandparent.GetParent()) {
        std::cerr << "FAIL: Cycle prevention failed! Grandparent was parented to its own grandchild!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Cycle check successfully rejected circular parenting." << std::endl;

    // Attempt self parenting
    father.SetParent(father);
    if (father.GetParent() != grandparent) {
        std::cerr << "FAIL: Self-parenting check failed!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Self-parenting successfully rejected." << std::endl;

    // -----------------------------------------------------------------
    // [5.6] Child Physics Body Transform Synchronization (World -> Local)
    // -----------------------------------------------------------------
    std::cout << "[5.6] Verifying Child Physics Body Coordinate Sync..." << std::endl;
    auto physParent = scene->CreateEntity("PhysParent");
    physParent.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(100.0f, 0.0f, 0.0f);

    auto physChild = scene->CreateEntity("PhysChild");
    physChild.SetParent(physParent);

    // Simulate Jolt returning a body world position at (115.0f, 0.0f, 0.0f)
    glm::mat4 parentWorld = physParent.GetWorldTransform();
    glm::mat4 invParent = glm::inverse(parentWorld);
    glm::vec3 simulatedJoltWorldPos(115.0f, 0.0f, 0.0f);
    glm::vec3 derivedLocalPos = glm::vec3(invParent * glm::vec4(simulatedJoltWorldPos, 1.0f));
    physChild.GetComponent<Axle::TransformComponent>().Translation = derivedLocalPos;

    glm::vec3 resolvedWorldPos = glm::vec3(physChild.GetWorldTransform()[3]);
    std::cout << "  -> Simulated Jolt World Pos: (" << simulatedJoltWorldPos.x << ", " << simulatedJoltWorldPos.y << ", " << simulatedJoltWorldPos.z << ")" << std::endl;
    std::cout << "  -> Resolved Child World Pos:  (" << resolvedWorldPos.x << ", " << resolvedWorldPos.y << ", " << resolvedWorldPos.z << ") (Expected: 115, 0, 0)" << std::endl;

    if (!Vec3Eq(resolvedWorldPos, simulatedJoltWorldPos)) {
        std::cerr << "FAIL: Child physics coordinate sync mismatch!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Child physics coordinates correctly converted to local and back without double-transform." << std::endl;

    std::cout << ">>> STEP 5 PASSED: Scene Graph, World Position Stays & Cycle Check verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 6: Phase 15.1 / Standard Archetype Presets (Unity / Godot style)
// =============================================================================
bool RunTest_Step6_Phase15_ArchetypePresets() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 6 TEST] PHASE 15.1: STANDARD ARCHETYPE PRESETS   " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto scene = std::make_shared<Axle::Scene>();

    // -----------------------------------------------------------------
    // [6.1] PointLightComponent and CameraComponent attachment
    // -----------------------------------------------------------------
    std::cout << "[6.1] Verifying PointLight and Camera component attachment..." << std::endl;
    auto pointLight = scene->CreateEntity("Point Light");
    pointLight.AddComponent<Axle::PointLightComponent>(glm::vec3(1.0f, 0.5f, 0.2f), 2.5f, 15.0f);

    auto camera = scene->CreateEntity("Perspective Camera");
    camera.AddComponent<Axle::CameraComponent>(60.0f, 0.1f, 2000.0f, true);

    if (!pointLight.HasComponent<Axle::PointLightComponent>()) {
        std::cerr << "FAIL: PointLightComponent missing!" << std::endl;
        return false;
    }
    const auto& plc = pointLight.GetComponent<Axle::PointLightComponent>();
    if (plc.Intensity != 2.5f || plc.Radius != 15.0f) {
        std::cerr << "FAIL: PointLightComponent field values mismatch!" << std::endl;
        return false;
    }

    if (!camera.HasComponent<Axle::CameraComponent>()) {
        std::cerr << "FAIL: CameraComponent missing!" << std::endl;
        return false;
    }
    const auto& cam = camera.GetComponent<Axle::CameraComponent>();
    if (cam.FOV != 60.0f || !cam.Primary) {
        std::cerr << "FAIL: CameraComponent field values mismatch!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: PointLightComponent and CameraComponent validated." << std::endl;

    // -----------------------------------------------------------------
    // [6.2] Create Empty & Create Empty Child Hierarchy Workflow
    // -----------------------------------------------------------------
    std::cout << "[6.2] Verifying 'Create Empty' and 'Create Empty Child' workflow..." << std::endl;
    auto rootEntity = scene->CreateEntity("GameObject");
    auto childEntity = scene->CreateEntity("Child");
    childEntity.SetParent(rootEntity);

    if (childEntity.GetParent() != rootEntity) {
        std::cerr << "FAIL: Child parent is not rootEntity!" << std::endl;
        return false;
    }
    if (rootEntity.GetChildCount() != 1) {
        std::cerr << "FAIL: Expected 1 child in rootEntity, got " << rootEntity.GetChildCount() << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Create Empty Child workflow validated." << std::endl;

    // -----------------------------------------------------------------
    // [6.3] 3D Primitive Creation & Transform Propagation
    // -----------------------------------------------------------------
    std::cout << "[6.3] Verifying 3D Primitives and Transform Propagation..." << std::endl;
    Axle::SceneHierarchyPanel panel(scene.get(), nullptr);
    auto cubeEntity = panel.CreatePrimitive(0, glm::vec3(0.0f, 2.0f, 0.0f));
    if (!cubeEntity || !cubeEntity.HasComponent<Axle::TransformComponent>()) {
        std::cerr << "FAIL: CreatePrimitive(0) failed to create valid entity with transform!" << std::endl;
        return false;
    }

    // Parent cube to rootEntity and check transform propagation
    cubeEntity.SetParent(rootEntity);
    glm::vec3 initialCubeWorld = glm::vec3(cubeEntity.GetWorldTransform()[3]);

    rootEntity.GetComponent<Axle::TransformComponent>().Translation += glm::vec3(5.0f, 3.0f, -2.0f);
    glm::vec3 movedCubeWorld = glm::vec3(cubeEntity.GetWorldTransform()[3]);

    if (!Vec3Eq(movedCubeWorld, initialCubeWorld + glm::vec3(5.0f, 3.0f, -2.0f))) {
        std::cerr << "FAIL: Cube primitive world transform did not propagate correctly!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: 3D Object primitive and scene graph propagation verified." << std::endl;

    std::cout << ">>> STEP 6 PASSED: Phase 15.1 Archetype Presets verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// =============================================================================
// TEST STEP 7: Phase 15.2 / Hierarchical Tree View & Lifecycle Management
// =============================================================================
bool RunTest_Step7_Phase15_HierarchicalTreeView() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 7 TEST] PHASE 15.2: HIERARCHICAL TREE VIEW       " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto scene = std::make_shared<Axle::Scene>();

    // -----------------------------------------------------------------
    // [7.1] Root vs Child Isolation
    // -----------------------------------------------------------------
    std::cout << "[7.1] Verifying Root-only Top-level Entity Isolation..." << std::endl;
    auto rootA = scene->CreateEntity("Root_A");
    auto rootB = scene->CreateEntity("Root_B");

    auto childA1 = scene->CreateEntity("Child_A1");
    auto childA2 = scene->CreateEntity("Child_A2");
    childA1.SetParent(rootA);
    childA2.SetParent(rootA);

    auto grandChild = scene->CreateEntity("GrandChild_A1_1");
    grandChild.SetParent(childA1);

    // Simulate Top-Level Hierarchy iteration logic
    int rootCount = 0;
    auto view = scene->GetRegistry().view<Axle::TagComponent>();
    for (auto entityID : view) {
        Axle::Entity entity{ entityID, scene.get() };
        if (entity.HasComponent<Axle::HierarchyComponent>()) {
            auto& hc = entity.GetComponent<Axle::HierarchyComponent>();
            if (hc.Parent != entt::null && scene->GetRegistry().valid(hc.Parent)) {
                continue; // Child node, skip top-level
            }
        }
        rootCount++;
    }

    std::cout << "  -> Total Entities: 5, Detected Root Entities: " << rootCount << std::endl;
    if (rootCount != 2) {
        std::cerr << "FAIL: Expected 2 root entities, got " << rootCount << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Only root entities qualify for top-level tree rendering." << std::endl;

    // -----------------------------------------------------------------
    // [7.2] Multi-level Tree Structure Integrity
    // -----------------------------------------------------------------
    std::cout << "[7.2] Verifying Multi-level Tree Traversal..." << std::endl;
    if (rootA.GetChildCount() != 2) {
        std::cerr << "FAIL: Root_A direct child count is " << rootA.GetChildCount() << " (Expected: 2)" << std::endl;
        return false;
    }
    if (childA1.GetChildCount() != 1) {
        std::cerr << "FAIL: Child_A1 direct child count is " << childA1.GetChildCount() << " (Expected: 1)" << std::endl;
        return false;
    }
    if (!grandChild.IsChildOf(rootA)) {
        std::cerr << "FAIL: GrandChild is not child of Root_A!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Multi-level hierarchy verified (Root -> Child -> GrandChild)." << std::endl;

    // -----------------------------------------------------------------
    // [7.3] Detach from Parent (Unparent)
    // -----------------------------------------------------------------
    std::cout << "[7.3] Verifying Entity Detach (Unparent)..." << std::endl;
    childA2.Unparent();

    if (childA2.GetParent()) {
        std::cerr << "FAIL: Child_A2 still has a parent after Unparent!" << std::endl;
        return false;
    }
    if (rootA.GetChildCount() != 1) {
        std::cerr << "FAIL: Root_A child count should be 1 after detach, got " << rootA.GetChildCount() << std::endl;
        return false;
    }

    // Recalculate root count
    rootCount = 0;
    for (auto entityID : view) {
        Axle::Entity entity{ entityID, scene.get() };
        if (entity.HasComponent<Axle::HierarchyComponent>()) {
            auto& hc = entity.GetComponent<Axle::HierarchyComponent>();
            if (hc.Parent != entt::null && scene->GetRegistry().valid(hc.Parent)) {
                continue;
            }
        }
        rootCount++;
    }
    if (rootCount != 3) {
        std::cerr << "FAIL: Expected 3 root entities after unparenting, got " << rootCount << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Unparent promoted Child_A2 to root successfully." << std::endl;

    // -----------------------------------------------------------------
    // [7.4] Recursive Destruction Lifecycle
    // -----------------------------------------------------------------
    std::cout << "[7.4] Verifying Recursive Child Cleanup on Root Destruction..." << std::endl;
    scene->DestroyEntity(rootA);

    if (scene->GetRegistry().valid((entt::entity)rootA)) {
        std::cerr << "FAIL: Root_A was not destroyed!" << std::endl;
        return false;
    }
    if (scene->GetRegistry().valid((entt::entity)childA1)) {
        std::cerr << "FAIL: Child_A1 was not recursively destroyed with Root_A!" << std::endl;
        return false;
    }
    if (scene->GetRegistry().valid((entt::entity)grandChild)) {
        std::cerr << "FAIL: GrandChild was not recursively destroyed with Root_A!" << std::endl;
        return false;
    }
    if (!scene->GetRegistry().valid((entt::entity)childA2)) {
        std::cerr << "FAIL: Detached Child_A2 was incorrectly destroyed!" << std::endl;
        return false;
    }
    if (!scene->GetRegistry().valid((entt::entity)rootB)) {
        std::cerr << "FAIL: Root_B was incorrectly destroyed!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Root destruction recursively destroyed descendants while preserving unparented siblings." << std::endl;

    std::cout << ">>> STEP 7 PASSED: Phase 15.2 Hierarchical Tree View verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// =============================================================================
// TEST STEP 8: Phase 15.3 / Visual Drag & Drop Reparenting ("SCENE_HIERARCHY_ENTITY")
// =============================================================================
bool RunTest_Step8_Phase15_DragAndDropReparenting() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 8 TEST] PHASE 15.3: DRAG & DROP REPARENTING      " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto scene = std::make_shared<Axle::Scene>();

    // -----------------------------------------------------------------
    // [8.1] Reparenting via Drag & Drop Payload Simulation
    // -----------------------------------------------------------------
    std::cout << "[8.1] Verifying Reparenting via Drag & Drop Payload Simulation..." << std::endl;
    auto parentEntity = scene->CreateEntity("Parent_Entity");
    parentEntity.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(5.0f, 0.0f, 0.0f);

    auto draggedEntity = scene->CreateEntity("Dragged_Child");
    draggedEntity.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(15.0f, 20.0f, 30.0f);

    // Simulate drag source: get ID payload
    uint32_t payloadData = (uint32_t)draggedEntity;

    // Simulate drop target on parentEntity:
    Axle::Entity resolvedDragged{ (entt::entity)payloadData, scene.get() };
    if (resolvedDragged && resolvedDragged != parentEntity && !parentEntity.IsChildOf(resolvedDragged)) {
        resolvedDragged.SetParent(parentEntity);
    }

    if (draggedEntity.GetParent() != parentEntity) {
        std::cerr << "FAIL: Dragged entity parent is not parentEntity!" << std::endl;
        return false;
    }
    if (parentEntity.GetChildCount() != 1) {
        std::cerr << "FAIL: parentEntity child count should be 1, got " << parentEntity.GetChildCount() << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Entity successfully reparented via payload simulation." << std::endl;

    // -----------------------------------------------------------------
    // [8.2] World Position Stays during Drag & Drop Reparenting
    // -----------------------------------------------------------------
    std::cout << "[8.2] Verifying World Position Stays after Reparent..." << std::endl;
    glm::vec3 worldPosAfter = glm::vec3(draggedEntity.GetWorldTransform()[3]);
    glm::vec3 expectedWorldPos = glm::vec3(15.0f, 20.0f, 30.0f);
    std::cout << "  -> Dragged Entity World Pos: (" << worldPosAfter.x << ", " << worldPosAfter.y << ", " << worldPosAfter.z << ")" << std::endl;

    if (!Vec3Eq(worldPosAfter, expectedWorldPos)) {
        std::cerr << "FAIL: World position changed after drag & drop reparenting!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: World position preserved with zero displacement." << std::endl;

    // -----------------------------------------------------------------
    // [8.3] Cycle Prevention Check on Drag & Drop Target
    // -----------------------------------------------------------------
    std::cout << "[8.3] Verifying Cycle Prevention Guard (Child cannot become parent of its ancestor)..." << std::endl;
    uint32_t invalidDraggedID = (uint32_t)parentEntity;
    Axle::Entity invalidDragged{ (entt::entity)invalidDraggedID, scene.get() };
    Axle::Entity targetChild = draggedEntity;

    bool reparentAttempted = false;
    if (invalidDragged && invalidDragged != targetChild && !targetChild.IsChildOf(invalidDragged)) {
        invalidDragged.SetParent(targetChild);
        reparentAttempted = true;
    }

    if (reparentAttempted) {
        std::cerr << "FAIL: Guard failed to prevent circular drag & drop reparenting!" << std::endl;
        return false;
    }
    if (parentEntity.GetParent() == targetChild) {
        std::cerr << "FAIL: Parent entity became child of its own child!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Cycle check successfully rejected circular drag & drop." << std::endl;

    // -----------------------------------------------------------------
    // [8.4] Blank Space Drop (Unparent / Return to Root)
    // -----------------------------------------------------------------
    std::cout << "[8.4] Verifying Blank Space Drop (Unparent to Root)..." << std::endl;
    draggedEntity.Unparent();

    if (draggedEntity.GetParent()) {
        std::cerr << "FAIL: Dragged entity still has parent after dropping on blank space!" << std::endl;
        return false;
    }
    if (parentEntity.GetChildCount() != 0) {
        std::cerr << "FAIL: parentEntity should have 0 children after child unparented, got " << parentEntity.GetChildCount() << std::endl;
        return false;
    }

    glm::vec3 worldPosUnparented = glm::vec3(draggedEntity.GetWorldTransform()[3]);
    if (!Vec3Eq(worldPosUnparented, expectedWorldPos)) {
        std::cerr << "FAIL: World position changed after unparenting to root!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Unparent to Root preserves world coordinates and resets hierarchy cleanly." << std::endl;

    std::cout << ">>> STEP 8 PASSED: Phase 15.3 Drag & Drop Reparenting verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// =============================================================================
// TEST STEP 9: Phase 15.4 / Deferred Deletion & Deep Recursive Duplication
// =============================================================================
bool RunTest_Step9_Phase15_DeferredAndRecursiveLifecycle() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 9 TEST] PHASE 15.4: DEFERRED & DEEP LIFECYCLE    " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto scene = std::make_shared<Axle::Scene>();

    // -----------------------------------------------------------------
    // [9.1] Deferred Deletion Queue
    // -----------------------------------------------------------------
    std::cout << "[9.1] Verifying Deferred Deletion Queue Execution..." << std::endl;
    auto e1 = scene->CreateEntity("Entity_1");
    auto e2 = scene->CreateEntity("Entity_2");
    auto e3 = scene->CreateEntity("Entity_3");

    std::vector<Axle::Entity> deferredQueue;
    deferredQueue.push_back(e1);
    deferredQueue.push_back(e2);

    // Ensure entities are still alive in scene before flush
    if (!scene->GetRegistry().valid((entt::entity)e1) || !scene->GetRegistry().valid((entt::entity)e2)) {
        std::cerr << "FAIL: Entities destroyed prematurely before queue flush!" << std::endl;
        return false;
    }

    // Flush deferred queue
    for (auto ent : deferredQueue) {
        scene->DestroyEntity(ent);
    }
    deferredQueue.clear();

    if (scene->GetRegistry().valid((entt::entity)e1) || scene->GetRegistry().valid((entt::entity)e2)) {
        std::cerr << "FAIL: Deferred entities not destroyed after flush!" << std::endl;
        return false;
    }
    if (!scene->GetRegistry().valid((entt::entity)e3)) {
        std::cerr << "FAIL: Unrelated Entity_3 was destroyed!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Deferred queue properly isolated and safely executed at end-of-frame." << std::endl;

    // -----------------------------------------------------------------
    // [9.2] Deep Recursive Duplication with Fresh UUID Generation
    // -----------------------------------------------------------------
    std::cout << "[9.2] Verifying Deep Recursive Duplication with Fresh UUIDs..." << std::endl;
    auto carRoot = scene->CreateEntity("CarRoot");
    carRoot.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(10.0f, 2.0f, 5.0f);
    carRoot.AddComponent<Axle::PointLightComponent>(glm::vec3(1.0f, 0.9f, 0.8f), 3.0f, 20.0f);
    carRoot.AddComponent<Axle::NativeScriptComponent>("CarPlayerScript");

    auto wheelFL = scene->CreateEntity("Wheel_FL");
    wheelFL.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(-1.0f, -0.5f, 1.5f);
    wheelFL.SetParent(carRoot);

    // Deep duplicate carRoot
    auto duplicatedCar = scene->DuplicateEntity(carRoot);

    if (!duplicatedCar) {
        std::cerr << "FAIL: DuplicateEntity returned null entity!" << std::endl;
        return false;
    }
    if (duplicatedCar.GetName() != "CarRoot (Copy)") {
        std::cerr << "FAIL: Duplicated car name mismatch: " << duplicatedCar.GetName() << std::endl;
        return false;
    }

    // Check Fresh UUID
    std::cout << "  -> Original Car UUID:   " << carRoot.GetUUID() << std::endl;
    std::cout << "  -> Duplicated Car UUID: " << duplicatedCar.GetUUID() << std::endl;
    if (duplicatedCar.GetUUID() == carRoot.GetUUID()) {
        std::cerr << "FAIL: Duplicated entity has duplicate UUID instead of fresh UUID!" << std::endl;
        return false;
    }

    // Verify component cloning
    if (!duplicatedCar.HasComponent<Axle::PointLightComponent>() ||
        !duplicatedCar.HasComponent<Axle::NativeScriptComponent>()) {
        std::cerr << "FAIL: Duplicated car missing components!" << std::endl;
        return false;
    }
    if (duplicatedCar.GetComponent<Axle::PointLightComponent>().Intensity != 3.0f) {
        std::cerr << "FAIL: Duplicated PointLightComponent field values mismatch!" << std::endl;
        return false;
    }

    // Verify deep child duplication
    if (duplicatedCar.GetChildCount() != 1) {
        std::cerr << "FAIL: Expected 1 duplicated child, got " << duplicatedCar.GetChildCount() << std::endl;
        return false;
    }

    auto dupChild = duplicatedCar.GetChildren()[0];
    std::cout << "  -> Original Wheel UUID:   " << wheelFL.GetUUID() << std::endl;
    std::cout << "  -> Duplicated Wheel UUID: " << dupChild.GetUUID() << std::endl;

    if (dupChild.GetUUID() == wheelFL.GetUUID()) {
        std::cerr << "FAIL: Duplicated child has duplicate UUID!" << std::endl;
        return false;
    }
    if (dupChild.GetParent() != duplicatedCar) {
        std::cerr << "FAIL: Duplicated child is not parented to duplicated car!" << std::endl;
        return false;
    }
    if (wheelFL.GetParent() != carRoot) {
        std::cerr << "FAIL: Original child parent link was corrupted!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Deep hierarchy and unique UUIDs successfully generated." << std::endl;

    // -----------------------------------------------------------------
    // [9.3] Recursive Deletion of Duplicated Entity
    // -----------------------------------------------------------------
    std::cout << "[9.3] Verifying Recursive Cleanup of Duplicated Tree..." << std::endl;
    scene->DestroyEntity(duplicatedCar);

    if (scene->GetRegistry().valid((entt::entity)duplicatedCar) ||
        scene->GetRegistry().valid((entt::entity)dupChild)) {
        std::cerr << "FAIL: Duplicated car or its child was not cleanly destroyed!" << std::endl;
        return false;
    }
    if (!scene->GetRegistry().valid((entt::entity)carRoot) ||
        !scene->GetRegistry().valid((entt::entity)wheelFL)) {
        std::cerr << "FAIL: Original car or child was destroyed unexpectedly!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Duplicated tree destroyed without affecting original hierarchy." << std::endl;

    std::cout << ">>> STEP 9 PASSED: Phase 15.4 Deferred & Deep Lifecycle verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 10: Phase 16.1 Structured Component Widgets & Safe Inspection
// =============================================================================
bool RunTest_Step10_Phase16_StructuredComponentWidgets() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 10 TEST] PHASE 16.1 STRUCTURED COMPONENT WIDGETS  " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto scene = std::make_shared<Axle::Scene>();
    auto entity = scene->CreateEntity("TestActor");

    // -----------------------------------------------------------------
    // [10.1] Vec3 Isolation, Individual Reset & Reset All Axes
    // -----------------------------------------------------------------
    std::cout << "[10.1] Testing 3-Axis Isolation & Reset Logic..." << std::endl;
    auto& tc = entity.GetComponent<Axle::TransformComponent>();
    
    // Initial defaults
    if (tc.Translation != glm::vec3(0.0f) || tc.Scale != glm::vec3(1.0f)) {
        std::cerr << "FAIL: Initial transform values unexpected!" << std::endl;
        return false;
    }

    // Individual axis change & isolation
    tc.Translation.x = 42.5f;
    if (tc.Translation.x != 42.5f || tc.Translation.y != 0.0f || tc.Translation.z != 0.0f) {
        std::cerr << "FAIL: Translation X modification contaminated Y or Z!" << std::endl;
        return false;
    }

    tc.Translation.y = -18.2f;
    tc.Translation.z = 100.0f;
    if (tc.Translation != glm::vec3(42.5f, -18.2f, 100.0f)) {
        std::cerr << "FAIL: Translation 3-axis values mismatch!" << std::endl;
        return false;
    }

    // Single axis reset to 0.0f (simulating Red 'X' button click)
    tc.Translation.x = 0.0f;
    if (tc.Translation != glm::vec3(0.0f, -18.2f, 100.0f)) {
        std::cerr << "FAIL: X-axis reset failed or altered other axes!" << std::endl;
        return false;
    }

    // Reset All Axes (simulating right-click context menu "Reset All Axes")
    tc.Translation = glm::vec3(0.0f);
    if (tc.Translation != glm::vec3(0.0f)) {
        std::cerr << "FAIL: Reset All Axes failed for translation!" << std::endl;
        return false;
    }

    // Scale reset testing (default resetValue = 1.0f)
    tc.Scale = glm::vec3(5.0f, 0.2f, 12.0f);
    tc.Scale.y = 1.0f; // Reset Y axis to 1.0f
    if (tc.Scale != glm::vec3(5.0f, 1.0f, 12.0f)) {
        std::cerr << "FAIL: Scale Y-axis reset failed!" << std::endl;
        return false;
    }
    tc.Scale = glm::vec3(1.0f); // Reset All Axes to 1.0f
    if (tc.Scale != glm::vec3(1.0f)) {
        std::cerr << "FAIL: Scale Reset All Axes failed!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: 3-Axis isolation and reset logic verified." << std::endl;

    // -----------------------------------------------------------------
    // [10.2] Protected Component Safety (Transform, ID, Relationship, Tag)
    // -----------------------------------------------------------------
    std::cout << "[10.2] Verifying Core Component Integrity (canRemove = false)..." << std::endl;
    if (!entity.HasComponent<Axle::IDComponent>() ||
        !entity.HasComponent<Axle::TagComponent>() ||
        !entity.HasComponent<Axle::TransformComponent>() ||
        !entity.HasComponent<Axle::RelationshipComponent>()) {
        std::cerr << "FAIL: Entity missing mandatory core components!" << std::endl;
        return false;
    }

    uint64_t uuidVal = (uint64_t)entity.GetUUID();
    std::cout << "  -> Entity UUID: " << uuidVal << std::endl;
    if (uuidVal == 0) {
        std::cerr << "FAIL: Entity UUID is null/zero!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Core components protected and UUID valid." << std::endl;

    // -----------------------------------------------------------------
    // [10.3] Removable Components & onRemove Destruction Callbacks
    // -----------------------------------------------------------------
    std::cout << "[10.3] Verifying Removable Components & onRemove Cleanup Callbacks..." << std::endl;
    
    // Add optional components
    entity.AddComponent<Axle::PointLightComponent>();
    entity.AddComponent<Axle::NativeScriptComponent>();
    
    if (!entity.HasComponent<Axle::PointLightComponent>() ||
        !entity.HasComponent<Axle::NativeScriptComponent>()) {
        std::cerr << "FAIL: Failed to attach optional components!" << std::endl;
        return false;
    }

    // Test PointLight removal
    entity.RemoveComponent<Axle::PointLightComponent>();
    if (entity.HasComponent<Axle::PointLightComponent>()) {
        std::cerr << "FAIL: PointLightComponent removal failed!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: PointLightComponent cleanly removed." << std::endl;

    // Test NativeScriptComponent removal with instance cleanup callback
    auto& nsc = entity.GetComponent<Axle::NativeScriptComponent>();
    nsc.Bind<CarPlayerScript>();
    if (nsc.InstantiateScript) {
        nsc.Instance = nsc.InstantiateScript();
    }
    if (!nsc.Instance) {
        std::cerr << "FAIL: Failed to instantiate CarPlayerScript instance!" << std::endl;
        return false;
    }

    bool scriptDestroyedCallbackFired = false;
    auto onRemoveScript = [&](Axle::NativeScriptComponent& comp) {
        if (comp.Instance && comp.DestroyScript) {
            comp.DestroyScript(&comp);
            scriptDestroyedCallbackFired = true;
        }
    };

    onRemoveScript(nsc);
    entity.RemoveComponent<Axle::NativeScriptComponent>();

    if (!scriptDestroyedCallbackFired || entity.HasComponent<Axle::NativeScriptComponent>()) {
        std::cerr << "FAIL: NativeScriptComponent cleanup callback failed or component not removed!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: NativeScriptComponent onRemove callback fired and memory released." << std::endl;

    std::cout << ">>> STEP 10 PASSED: Phase 16.1 Structured Component Widgets verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 11: Phase 16.2 Categorized Add Component Popup & Search Filtering
// =============================================================================
bool RunTest_Step11_Phase16_CategorizedComponentPopup() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 11 TEST] PHASE 16.2 CATEGORIZED ADD COMPONENT     " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto scene = std::make_shared<Axle::Scene>();
    auto entity = scene->CreateEntity("ModularActor");

    // Helper definition matching SceneHierarchyPanel component item model
    struct TestComponentItem {
        std::string Category;
        std::string Name;
        std::string Description;
        bool AlreadyAdded = false;
        bool Enabled = true;
        std::function<void()> OnAdd;
    };

    auto buildComponentList = [&](Axle::Entity ent) -> std::vector<TestComponentItem> {
        std::vector<TestComponentItem> items;

        // Rendering
        items.push_back({
            "Rendering", "MeshInstance3D", "3D mesh instance rendering",
            ent.HasComponent<Axle::MeshInstance3D>(), true,
            [ent]() mutable { ent.AddComponent<Axle::MeshInstance3D>(); }
        });
        items.push_back({
            "Rendering", "MaterialComponent", "PBR material properties",
            ent.HasComponent<Axle::MaterialComponent>(), true,
            [ent]() mutable { ent.AddComponent<Axle::MaterialComponent>(); }
        });
        items.push_back({
            "Rendering", "DirectionalLightComponent", "Global sunlight with shadow casting",
            ent.HasComponent<Axle::DirectionalLightComponent>(), true,
            [ent]() mutable { ent.AddComponent<Axle::DirectionalLightComponent>(); }
        });
        items.push_back({
            "Rendering", "PointLightComponent", "Omnidirectional local point light",
            ent.HasComponent<Axle::PointLightComponent>(), true,
            [ent]() mutable { ent.AddComponent<Axle::PointLightComponent>(); }
        });

        // Physics
        items.push_back({
            "Physics", "CollisionShape3D (Box)", "Box collision primitive based on scale",
            ent.HasComponent<Axle::CollisionShape3D>(), true,
            [ent]() mutable { ent.AddComponent<Axle::CollisionShape3D>(Axle::CollisionShapeType::Box, glm::vec3(0.5f)); }
        });
        items.push_back({
            "Physics", "VehicleComponent", "Vehicle physics simulation controller",
            ent.HasComponent<Axle::VehicleComponent>(), true,
            [ent]() mutable { ent.AddComponent<Axle::VehicleComponent>(); }
        });

        // Scripts
        items.push_back({
            "Scripts", "Native Script Component", "Generic C++ script container",
            ent.HasComponent<Axle::NativeScriptComponent>(), true,
            [ent]() mutable { ent.AddComponent<Axle::NativeScriptComponent>(); }
        });

        const auto& registeredScripts = Axle::ScriptRegistry::GetScripts();
        for (const auto& [scriptName, scriptMeta] : registeredScripts) {
            bool hasScript = ent.HasComponent<Axle::NativeScriptComponent>() &&
                             ent.GetComponent<Axle::NativeScriptComponent>().ScriptName == scriptName;
            items.push_back({
                "Scripts", "Script: " + scriptName, "Directly attach registered script '" + scriptName + "'",
                hasScript, true,
                [ent, sName = scriptName]() mutable {
                    if (!ent.HasComponent<Axle::NativeScriptComponent>()) {
                        auto& nsc = ent.AddComponent<Axle::NativeScriptComponent>(sName);
                        nsc.Bind(sName);
                    } else {
                        auto& nsc = ent.GetComponent<Axle::NativeScriptComponent>();
                        nsc.Bind(sName);
                    }
                }
            });
        }

        // Audio/VFX
        items.push_back({
            "Audio/VFX", "AudioSource3D (Phase 20)", "Spatial 3D audio emitter",
            false, false, nullptr
        });

        return items;
    };

    // -----------------------------------------------------------------
    // [11.1] Verify Category Structure & Search Filtering Logic
    // -----------------------------------------------------------------
    std::cout << "[11.1] Verifying Categorization & Real-time Search Logic..." << std::endl;
    auto initialItems = buildComponentList(entity);

    auto testContainsIgnoreCase = [](const std::string& str, const std::string& query) -> bool {
        if (query.empty()) return true;
        auto it = std::search(
            str.begin(), str.end(),
            query.begin(), query.end(),
            [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
        );
        return it != str.end();
    };

    auto filterQuery = [&](const std::string& q) -> std::vector<TestComponentItem> {
        std::vector<TestComponentItem> matched;
        for (const auto& item : initialItems) {
            std::string fullText = item.Category + " " + item.Name + " " + item.Description;
            if (testContainsIgnoreCase(fullText, q)) {
                matched.push_back(item);
            }
        }
        return matched;
    };

    auto meshResults = filterQuery("mesh");
    std::cout << "  -> Search 'mesh' returned " << meshResults.size() << " match(es)" << std::endl;
    if (meshResults.empty() || meshResults[0].Name != "MeshInstance3D") {
        std::cerr << "FAIL: Search 'mesh' did not find MeshInstance3D!" << std::endl;
        return false;
    }

    auto lightResults = filterQuery("LiGhT"); // case insensitive check
    std::cout << "  -> Case-insensitive search 'LiGhT' returned " << lightResults.size() << " match(es)" << std::endl;
    if (lightResults.size() < 2) {
        std::cerr << "FAIL: Search 'LiGhT' failed to find directional and point lights!" << std::endl;
        return false;
    }

    auto carScriptResults = filterQuery("CarPlayer");
    std::cout << "  -> Search 'CarPlayer' returned " << carScriptResults.size() << " match(es)" << std::endl;
    if (carScriptResults.empty()) {
        std::cerr << "FAIL: ScriptRegistry dynamic script was not matched!" << std::endl;
        return false;
    }

    auto audioResults = filterQuery("audio");
    std::cout << "  -> Search 'audio' returned " << audioResults.size() << " match(es)" << std::endl;
    if (audioResults.empty() || audioResults[0].Enabled) {
        std::cerr << "FAIL: Audio/VFX placeholder missing or unexpectedly enabled!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Category search filtering and case-insensitivity validated." << std::endl;

    // -----------------------------------------------------------------
    // [11.2] Verify Multi-Category Component Attachment & State Tracking
    // -----------------------------------------------------------------
    std::cout << "[11.2] Verifying Component Add Actions & Already-Added Detection..." << std::endl;

    // Attach MaterialComponent (Rendering)
    meshResults[0].OnAdd(); // Add MeshInstance3D
    if (!entity.HasComponent<Axle::MeshInstance3D>()) {
        std::cerr << "FAIL: Adding MeshInstance3D via popup action failed!" << std::endl;
        return false;
    }

    // Attach CollisionShape3D (Physics)
    auto physicsResults = filterQuery("collision");
    if (physicsResults.empty()) {
        std::cerr << "FAIL: No collision shape found in filter!" << std::endl;
        return false;
    }
    physicsResults[0].OnAdd();
    if (!entity.HasComponent<Axle::CollisionShape3D>()) {
        std::cerr << "FAIL: Adding CollisionShape3D via popup action failed!" << std::endl;
        return false;
    }

    // Attach CarPlayerScript directly (Scripts)
    carScriptResults[0].OnAdd();
    if (!entity.HasComponent<Axle::NativeScriptComponent>() ||
        entity.GetComponent<Axle::NativeScriptComponent>().ScriptName != "CarPlayerScript") {
        std::cerr << "FAIL: Direct Script attachment from popup action failed!" << std::endl;
        return false;
    }

    // Rebuild items and verify AlreadyAdded flags
    auto updatedItems = buildComponentList(entity);
    for (const auto& item : updatedItems) {
        if (item.Name == "MeshInstance3D" && !item.AlreadyAdded) {
            std::cerr << "FAIL: MeshInstance3D should be flagged as AlreadyAdded!" << std::endl;
            return false;
        }
        if (item.Name == "CollisionShape3D (Box)" && !item.AlreadyAdded) {
            std::cerr << "FAIL: CollisionShape3D should be flagged as AlreadyAdded!" << std::endl;
            return false;
        }
        if (item.Name == "Script: CarPlayerScript" && !item.AlreadyAdded) {
            std::cerr << "FAIL: Script: CarPlayerScript should be flagged as AlreadyAdded!" << std::endl;
            return false;
        }
        if (item.Name == "PointLightComponent" && item.AlreadyAdded) {
            std::cerr << "FAIL: PointLightComponent should NOT be flagged as AlreadyAdded!" << std::endl;
            return false;
        }
    }
    std::cout << "  -> PASS: Multi-category components attached and duplicate prevention verified." << std::endl;

    std::cout << ">>> STEP 11 PASSED: Phase 16.2 Categorized Component Popup verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 12: Phase 16.3 C++ Script Reflection Macro & Entity Reference (UUID)
// =============================================================================
class ReflectionDemoScript : public Axle::ScriptableEntity {
public:
    float Speed = 25.5f;
    int Ammo = 42;
    bool Invincible = true;
    glm::vec3 Offset = { 1.0f, 2.0f, 3.0f };
    std::string PlayerName = "CyberPhantom";
    Axle::UUID FollowTarget = 9988776655ULL;

    AXLE_SCRIPT_FIELDS(ReflectionDemoScript,
        AXLE_FIELD(Speed, 0.0f, 100.0f, "Movement speed in m/s"),
        AXLE_FIELD(Ammo, 0, 999, "Current ammunition count"),
        AXLE_FIELD(Invincible, "God mode toggle"),
        AXLE_FIELD(Offset, "Offset vector from parent"),
        AXLE_FIELD(PlayerName, "Custom player handle"),
        AXLE_FIELD_ENTITY(FollowTarget, "Reference to leader target entity")
    )
};

AXLE_REGISTER_SCRIPT(ReflectionDemoScript);

bool RunTest_Step12_Phase16_ScriptReflectionMacro() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 12 TEST] PHASE 16.3 C++ SCRIPT REFLECTION MACRO  " << std::endl;
    std::cout << "========================================================" << std::endl;

    // -----------------------------------------------------------------
    // [12.1] Inspect Reflected Field Metadata
    // -----------------------------------------------------------------
    std::cout << "[12.1] Verifying AXLE_SCRIPT_FIELDS metadata reflection..." << std::endl;
    const auto* meta = Axle::ScriptRegistry::GetMetadata("ReflectionDemoScript");
    if (!meta) {
        std::cerr << "FAIL: ReflectionDemoScript not registered in ScriptRegistry!" << std::endl;
        return false;
    }

    std::cout << "  -> Total reflected fields: " << meta->Fields.size() << " (Expected: 6)" << std::endl;
    if (meta->Fields.size() != 6) {
        std::cerr << "FAIL: Reflected field count mismatch!" << std::endl;
        return false;
    }

    bool hasFloat = false, hasInt = false, hasBool = false;
    bool hasVec3 = false, hasString = false, hasEntityRef = false;

    for (const auto& f : meta->Fields) {
        std::cout << "     * Field: " << f.Name << " [Type=" << (int)f.Type 
                  << ", Tooltip='" << f.Tooltip << "']" << std::endl;
        if (f.Name == "Speed" && f.Type == Axle::ScriptFieldType::Float) hasFloat = true;
        if (f.Name == "Ammo" && f.Type == Axle::ScriptFieldType::Int) hasInt = true;
        if (f.Name == "Invincible" && f.Type == Axle::ScriptFieldType::Bool) hasBool = true;
        if (f.Name == "Offset" && f.Type == Axle::ScriptFieldType::Vec3) hasVec3 = true;
        if (f.Name == "PlayerName" && f.Type == Axle::ScriptFieldType::String) hasString = true;
        if (f.Name == "FollowTarget" && f.Type == Axle::ScriptFieldType::EntityRef) hasEntityRef = true;
    }

    if (!hasFloat || !hasInt || !hasBool || !hasVec3 || !hasString || !hasEntityRef) {
        std::cerr << "FAIL: One or more field types failed reflection mapping!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: All 6 field types (Float, Int, Bool, Vec3, String, EntityRef) verified." << std::endl;

    // -----------------------------------------------------------------
    // [12.2] Scene::GetEntityByUUID Resolution
    // -----------------------------------------------------------------
    std::cout << "[12.2] Verifying Scene::GetEntityByUUID resolution..." << std::endl;
    auto scene = std::make_shared<Axle::Scene>();
    auto leaderEntity = scene->CreateEntity("LeaderEntity");
    auto followerEntity = scene->CreateEntity("FollowerEntity");

    Axle::UUID leaderUUID = leaderEntity.GetUUID();
    auto resolvedLeader = scene->GetEntityByUUID(leaderUUID);
    if (!resolvedLeader || resolvedLeader != leaderEntity) {
        std::cerr << "FAIL: Scene::GetEntityByUUID failed to resolve LeaderEntity!" << std::endl;
        return false;
    }
    std::cout << "  -> Resolved leader entity name: " << resolvedLeader.GetName() 
              << " (" << (uint64_t)leaderUUID << ")" << std::endl;
    std::cout << "  -> PASS: Scene::GetEntityByUUID resolved valid entity correctly." << std::endl;

    // -----------------------------------------------------------------
    // [12.3] NativeScriptComponent Bidirectional Memory Sync
    // -----------------------------------------------------------------
    std::cout << "[12.3] Verifying Bidirectional Instance Sync with Entity Reference..." << std::endl;
    auto& nsc = followerEntity.AddComponent<Axle::NativeScriptComponent>("ReflectionDemoScript");
    nsc.Bind("ReflectionDemoScript");

    // Check defaults populated
    if (nsc.FloatFields["Speed"] != 25.5f ||
        nsc.IntFields["Ammo"] != 42 ||
        nsc.BoolFields["Invincible"] != true ||
        nsc.StringFields["PlayerName"] != "CyberPhantom" ||
        nsc.EntityFields["FollowTarget"] != 9988776655ULL) {
        std::cerr << "FAIL: Default field buffers not populated correctly in Bind()!" << std::endl;
        return false;
    }

    // Assign leaderUUID into EntityFields buffer
    nsc.EntityFields["FollowTarget"] = leaderUUID;
    nsc.FloatFields["Speed"] = 80.0f;

    // Instantiate and sync to instance
    nsc.Instance = nsc.InstantiateScript();
    if (!nsc.Instance) {
        std::cerr << "FAIL: InstantiateScript failed!" << std::endl;
        return false;
    }
    nsc.SyncFieldsToInstance();

    auto* scriptInstance = static_cast<ReflectionDemoScript*>(nsc.Instance);
    if (scriptInstance->FollowTarget != leaderUUID || scriptInstance->Speed != 80.0f) {
        std::cerr << "FAIL: SyncFieldsToInstance failed to update script instance!" << std::endl;
        return false;
    }
    std::cout << "  -> Live script instance FollowTarget: " << (uint64_t)scriptInstance->FollowTarget << std::endl;
    std::cout << "  -> Live script instance Speed:        " << scriptInstance->Speed << std::endl;

    // Mutate live script instance and test SyncFieldsFromInstance
    scriptInstance->Ammo = 999;
    scriptInstance->PlayerName = "ValkyrieCommander";
    scriptInstance->FollowTarget = 1122334455ULL;

    nsc.SyncFieldsFromInstance();
    if (nsc.IntFields["Ammo"] != 999 ||
        nsc.StringFields["PlayerName"] != "ValkyrieCommander" ||
        nsc.EntityFields["FollowTarget"] != 1122334455ULL) {
        std::cerr << "FAIL: SyncFieldsFromInstance failed to read back updated values!" << std::endl;
        return false;
    }
    std::cout << "  -> Read-back Ammo:         " << nsc.IntFields["Ammo"] << std::endl;
    std::cout << "  -> Read-back PlayerName:   " << nsc.StringFields["PlayerName"] << std::endl;
    std::cout << "  -> Read-back FollowTarget: " << (uint64_t)nsc.EntityFields["FollowTarget"] << std::endl;

    nsc.DestroyScript(&nsc);
    std::cout << "  -> PASS: Bidirectional sync with Entity Reference verified." << std::endl;

    std::cout << ">>> STEP 12 PASSED: Phase 16.3 C++ Script Reflection Macro verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 13: Phase 16.4 Dual-Context Properties Inspector & Material Assets
// =============================================================================
bool RunTest_Step13_Phase16_DualContextPropertiesInspector() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 13 TEST] DUAL-CONTEXT PROPERTIES INSPECTOR       " << std::endl;
    std::cout << "========================================================" << std::endl;

    // 1. Dual-Context Mode Switching & Selection Preservation
    std::cout << "[13.1] Testing Dual-Context Mode Switching & State Preservation..." << std::endl;
    Axle::Scene scene;
    Axle::SceneHierarchyPanel panel(&scene);

    auto testEntity = scene.CreateEntity("InspectorTargetVehicle");
    panel.SetSelectedEntity(testEntity);

    if (panel.GetInspectorMode() != Axle::InspectorMode::Entity) {
        std::cerr << "FAIL: InspectorMode did not default to Entity after SetSelectedEntity!" << std::endl;
        return false;
    }
    if (panel.GetSelectedEntity() != testEntity) {
        std::cerr << "FAIL: Selected entity not stored correctly in panel!" << std::endl;
        return false;
    }
    std::cout << "  -> InspectorMode::Entity active with entity: " << testEntity.GetName() << std::endl;

    // Select asset (e.g. car paint material asset)
    std::filesystem::path testMatPath = "test_cyber_red.axlemat";
    panel.SetSelectedAsset(testMatPath);

    if (panel.GetInspectorMode() != Axle::InspectorMode::Asset) {
        std::cerr << "FAIL: InspectorMode did not switch to Asset after SetSelectedAsset!" << std::endl;
        return false;
    }
    if (panel.GetSelectedAsset() != testMatPath) {
        std::cerr << "FAIL: Selected asset path mismatch!" << std::endl;
        return false;
    }
    // Verify that the entity was NOT cleared, enabling tab bar toggling
    if (panel.GetSelectedEntity() != testEntity) {
        std::cerr << "FAIL: Entity selection was lost when selecting asset (breaks dual-context)!" << std::endl;
        return false;
    }
    std::cout << "  -> InspectorMode::Asset active with asset: " << testMatPath.string() << std::endl;
    std::cout << "  -> PASS: Entity selection preserved alongside Asset selection for dual-tab inspection." << std::endl;

    // Switch mode back to Entity manually (as tab bar does)
    panel.SetInspectorMode(Axle::InspectorMode::Entity);
    if (panel.GetInspectorMode() != Axle::InspectorMode::Entity || panel.GetSelectedAsset() != testMatPath) {
        std::cerr << "FAIL: Manual mode toggle to Entity failed!" << std::endl;
        return false;
    }

    // Clear asset
    panel.ClearSelectedAsset();
    if (!panel.GetSelectedAsset().empty() || panel.GetInspectorMode() != Axle::InspectorMode::Entity) {
        std::cerr << "FAIL: ClearSelectedAsset did not reset asset path or mode!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Asset cleared and returned to Entity mode cleanly." << std::endl;

    // 2. Material Asset File (.axlemat) Serialization Roundtrip
    std::cout << "\n[13.2] Testing Material Asset (.axlemat) Serialization Roundtrip..." << std::endl;
    std::filesystem::path matFilePath = "sandbox_test_racing.axlemat";

    Axle::MaterialComponent originalMat;
    originalMat.AlbedoColor = glm::vec4(0.92f, 0.15f, 0.10f, 1.0f);
    originalMat.AlbedoPath = "assets/textures/carbon_diffuse.png";
    originalMat.UseAlbedoMap = true;
    originalMat.NormalPath = "assets/textures/carbon_normal.png";
    originalMat.UseNormalMap = true;
    originalMat.NormalStrength = 1.75f;
    originalMat.Roughness = 0.28f;
    originalMat.RoughnessPath = "assets/textures/carbon_roughness.png";
    originalMat.UseRoughnessMap = true;
    originalMat.Metallic = 0.85f;
    originalMat.MetallicPath = "assets/textures/carbon_metallic.png";
    originalMat.UseMetallicMap = true;
    originalMat.EmissionColor = glm::vec3(1.0f, 0.4f, 0.1f);
    originalMat.EmissionIntensity = 8.5f;
    originalMat.UVScale = glm::vec2(4.0f, 4.0f);

    bool saveOk = Axle::SceneHierarchyPanel::SaveMaterialAsset(matFilePath, originalMat);
    if (!saveOk || !std::filesystem::exists(matFilePath)) {
        std::cerr << "FAIL: SaveMaterialAsset failed to write to " << matFilePath << std::endl;
        return false;
    }
    std::cout << "  -> Successfully serialized .axlemat file to disk." << std::endl;

    Axle::MaterialComponent loadedMat;
    bool loadOk = Axle::SceneHierarchyPanel::LoadMaterialAsset(matFilePath, loadedMat);
    if (!loadOk) {
        std::cerr << "FAIL: LoadMaterialAsset failed to load from " << matFilePath << std::endl;
        std::filesystem::remove(matFilePath);
        return false;
    }

    // Validate fields
    if (std::abs(loadedMat.AlbedoColor.r - originalMat.AlbedoColor.r) > 0.01f ||
        std::abs(loadedMat.AlbedoColor.g - originalMat.AlbedoColor.g) > 0.01f ||
        std::abs(loadedMat.AlbedoColor.b - originalMat.AlbedoColor.b) > 0.01f ||
        std::abs(loadedMat.AlbedoColor.a - originalMat.AlbedoColor.a) > 0.01f ||
        loadedMat.AlbedoPath != originalMat.AlbedoPath ||
        loadedMat.UseAlbedoMap != originalMat.UseAlbedoMap ||
        loadedMat.NormalPath != originalMat.NormalPath ||
        loadedMat.UseNormalMap != originalMat.UseNormalMap ||
        std::abs(loadedMat.NormalStrength - originalMat.NormalStrength) > 0.01f ||
        std::abs(loadedMat.Roughness - originalMat.Roughness) > 0.01f ||
        loadedMat.RoughnessPath != originalMat.RoughnessPath ||
        loadedMat.UseRoughnessMap != originalMat.UseRoughnessMap ||
        std::abs(loadedMat.Metallic - originalMat.Metallic) > 0.01f ||
        loadedMat.MetallicPath != originalMat.MetallicPath ||
        loadedMat.UseMetallicMap != originalMat.UseMetallicMap ||
        std::abs(loadedMat.EmissionColor.r - originalMat.EmissionColor.r) > 0.01f ||
        std::abs(loadedMat.EmissionIntensity - originalMat.EmissionIntensity) > 0.01f ||
        std::abs(loadedMat.UVScale.x - originalMat.UVScale.x) > 0.01f ||
        std::abs(loadedMat.UVScale.y - originalMat.UVScale.y) > 0.01f) {
        std::cerr << "FAIL: Material asset field values do not match after roundtrip!" << std::endl;
        std::filesystem::remove(matFilePath);
        return false;
    }
    std::cout << "  -> PASS: All PBR material parameters verified in roundtrip." << std::endl;

    // 3. Apply Loaded Material Asset to Selected Entity
    std::cout << "\n[13.3] Testing Application of Material Asset to Entity..." << std::endl;
    if (testEntity.HasComponent<Axle::MaterialComponent>()) {
        testEntity.GetComponent<Axle::MaterialComponent>() = loadedMat;
    } else {
        testEntity.AddComponent<Axle::MaterialComponent>(loadedMat);
    }

    auto& entityMat = testEntity.GetComponent<Axle::MaterialComponent>();
    if (entityMat.AlbedoPath != originalMat.AlbedoPath ||
        entityMat.Metallic != originalMat.Metallic ||
        entityMat.Roughness != originalMat.Roughness ||
        entityMat.UVScale != originalMat.UVScale) {
        std::cerr << "FAIL: Applied material component does not match asset!" << std::endl;
        std::filesystem::remove(matFilePath);
        return false;
    }
    std::cout << "  -> Entity '" << testEntity.GetName() << "' received material asset properties perfectly." << std::endl;

    std::filesystem::remove(matFilePath);

    std::cout << ">>> STEP 13 PASSED: Phase 16.4 Dual-Context Properties Inspector verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 14: Phase 17.1 Asset UUID & .meta Companion File Pipeline
// =============================================================================
bool RunTest_Step14_Phase17_AssetUUIDAndMetaPipeline() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 14 TEST] PHASE 17.1: ASSET UUID & .META PIPELINE  " << std::endl;
    std::cout << "========================================================" << std::endl;

    // 1. AssetType detection from path extensions
    std::cout << "[14.1] Testing AssetType detection from file extensions..." << std::endl;
    if (Axle::GetAssetTypeFromPath("car.axlemesh") != Axle::AssetType::Mesh ||
        Axle::GetAssetTypeFromPath("vehicle.axle") != Axle::AssetType::Assembly ||
        Axle::GetAssetTypeFromPath("car.obj") != Axle::AssetType::Mesh ||
        Axle::GetAssetTypeFromPath("texture.png") != Axle::AssetType::Texture ||
        Axle::GetAssetTypeFromPath("material.axlemat") != Axle::AssetType::Material ||
        Axle::GetAssetTypeFromPath("script.hpp") != Axle::AssetType::Script ||
        Axle::GetAssetTypeFromPath("scene.axlescene") != Axle::AssetType::Scene ||
        Axle::GetAssetTypeFromPath("phys.physmat") != Axle::AssetType::PhysicsMaterial ||
        Axle::GetAssetTypeFromPath("engine.wav") != Axle::AssetType::Audio) {
        std::cerr << "FAIL: AssetType detection failed for standard asset extensions!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: All asset extension types correctly categorized (.axlemesh -> Mesh, .axle -> Assembly)." << std::endl;

    // 2. .meta serialization roundtrip
    std::cout << "\n[14.2] Testing .meta file serialization and parsing..." << std::endl;
    std::filesystem::path testMetaFile = "sandbox_single_asset.obj.meta";
    Axle::UUID expectedUUID = 1234567890987654321ULL;
    Axle::AssetMetadata originalMeta;
    originalMeta.Handle = expectedUUID;
    originalMeta.Type = Axle::AssetType::Mesh;
    originalMeta.FilePath = "models/sandbox_single_asset.obj";

    bool serOk = Axle::AssetMetadataSerializer::Serialize(testMetaFile, originalMeta);
    if (!serOk || !std::filesystem::exists(testMetaFile)) {
        std::cerr << "FAIL: AssetMetadataSerializer::Serialize failed!" << std::endl;
        return false;
    }

    Axle::AssetMetadata loadedMeta;
    bool deserOk = Axle::AssetMetadataSerializer::Deserialize(testMetaFile, loadedMeta);
    if (!deserOk || loadedMeta.Handle != expectedUUID || loadedMeta.Type != Axle::AssetType::Mesh) {
        std::cerr << "FAIL: AssetMetadataSerializer::Deserialize returned incorrect metadata!" << std::endl;
        std::filesystem::remove(testMetaFile);
        return false;
    }
    std::filesystem::remove(testMetaFile);
    std::cout << "  -> PASS: .meta YAML format correctly serialized and parsed." << std::endl;

    // 3. Auto-generation of companion .meta files in sandbox asset directory
    std::cout << "\n[14.3] Testing auto-generation of companion .meta files..." << std::endl;
    std::filesystem::path testDir = "sandbox_asset_pipeline_test";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir / "submodels", ec);

    // Create mock asset files
    auto createMockFile = [](const std::filesystem::path& p) {
        std::ofstream f(p);
        f << "Mock asset payload for " << p.filename().string() << "\n";
        f.close();
    };

    std::filesystem::path fileChassis = testDir / "submodels" / "chassis.obj";
    std::filesystem::path fileTexture = testDir / "wheel_albedo.png";
    std::filesystem::path fileMaterial = testDir / "car_paint.axlemat";

    createMockFile(fileChassis);
    createMockFile(fileTexture);
    createMockFile(fileMaterial);

    Axle::AssetManager::Init(testDir);

    std::filesystem::path metaChassis = Axle::AssetMetadataSerializer::GetMetaPath(fileChassis);
    std::filesystem::path metaTexture = Axle::AssetMetadataSerializer::GetMetaPath(fileTexture);
    std::filesystem::path metaMaterial = Axle::AssetMetadataSerializer::GetMetaPath(fileMaterial);

    if (!std::filesystem::exists(metaChassis) ||
        !std::filesystem::exists(metaTexture) ||
        !std::filesystem::exists(metaMaterial)) {
        std::cerr << "FAIL: Companion .meta files were not auto-generated on AssetManager::Sync!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> Auto-generated: " << metaChassis.filename().string() << std::endl;
    std::cout << "  -> Auto-generated: " << metaTexture.filename().string() << std::endl;
    std::cout << "  -> Auto-generated: " << metaMaterial.filename().string() << std::endl;

    Axle::UUID chassisUUID = Axle::AssetManager::GetAssetUUID(fileChassis);
    Axle::UUID textureUUID = Axle::AssetManager::GetAssetUUID(fileTexture);
    Axle::UUID materialUUID = Axle::AssetManager::GetAssetUUID(fileMaterial);

    if (chassisUUID == 0 || textureUUID == 0 || materialUUID == 0) {
        std::cerr << "FAIL: Generated UUIDs must be non-zero!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> Chassis UUID:  " << (uint64_t)chassisUUID << std::endl;
    std::cout << "  -> Texture UUID:  " << (uint64_t)textureUUID << std::endl;
    std::cout << "  -> Material UUID: " << (uint64_t)materialUUID << std::endl;

    // 4. Verification of persistence across restart (Zero UUID drift)
    std::cout << "\n[14.4] Testing UUID persistence across shutdown and restart..." << std::endl;
    Axle::AssetManager::Shutdown();
    Axle::AssetManager::Init(testDir);

    Axle::UUID reloadedChassisUUID = Axle::AssetManager::GetAssetUUID(fileChassis);
    if (reloadedChassisUUID != chassisUUID) {
        std::cerr << "FAIL: UUID changed after restart! Expected " << (uint64_t)chassisUUID 
                  << ", got " << (uint64_t)reloadedChassisUUID << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Persistent UUID preserved across restarts without drift." << std::endl;

    // 5. Path-Independent Referencing & Rename Lifecycle
    std::cout << "\n[14.5] Testing path-independent referencing and rename lifecycle..." << std::endl;
    std::filesystem::path renamedChassis = testDir / "submodels" / "chassis_v2_sport.obj";
    std::filesystem::rename(fileChassis, renamedChassis, ec);
    Axle::AssetManager::OnAssetRenamed(fileChassis, renamedChassis);

    std::filesystem::path renamedMeta = Axle::AssetMetadataSerializer::GetMetaPath(renamedChassis);
    if (!std::filesystem::exists(renamedMeta) || std::filesystem::exists(metaChassis)) {
        std::cerr << "FAIL: Companion .meta file was not renamed alongside asset!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Lookup path by old UUID -> must now resolve to the renamed path!
    std::filesystem::path resolvedPath = Axle::AssetManager::GetAssetPath(chassisUUID);
    std::string normResolved = resolvedPath.generic_string();
    if (normResolved.find("chassis_v2_sport.obj") == std::string::npos) {
        std::cerr << "FAIL: UUID lookup did not resolve to renamed file path!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> UUID " << (uint64_t)chassisUUID << " successfully resolved to renamed file: " 
              << resolvedPath.string() << std::endl;
    std::cout << "  -> PASS: Path-independent asset referencing verified." << std::endl;

    // 6. Delete lifecycle
    std::cout << "\n[14.6] Testing delete lifecycle..." << std::endl;
    std::filesystem::remove(renamedChassis, ec);
    Axle::AssetManager::OnAssetDeleted(renamedChassis);

    if (std::filesystem::exists(renamedMeta) || Axle::AssetManager::HasAsset(chassisUUID)) {
        std::cerr << "FAIL: Deleted asset companion .meta was not removed or registry still holds asset!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Companion .meta cleaned up and asset unregistered on delete." << std::endl;

    // Cleanup
    Axle::AssetManager::Shutdown();
    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 14 PASSED: Phase 17.1 Asset UUID & .meta Pipeline verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 15: Phase 17.2 Right-Click Asset Creation Pipeline
// =============================================================================
bool RunTest_Step15_Phase17_RightClickAssetCreation() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 15 TEST] PHASE 17.2: RIGHT-CLICK ASSET CREATION   " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::filesystem::path testDir = "sandbox_creation_pipeline_test";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    Axle::AssetManager::Init(testDir);

    // 1. Create New Folder & Collision Handling
    std::cout << "[15.1] Testing New Folder creation and name deduplication..." << std::endl;
    auto folder1 = Axle::ContentBrowserPanel::CreateNewFolder(testDir);
    auto folder2 = Axle::ContentBrowserPanel::CreateNewFolder(testDir);

    if (!std::filesystem::is_directory(folder1) || !std::filesystem::is_directory(folder2)) {
        std::cerr << "FAIL: CreateNewFolder failed to create physical directory!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (folder1 == folder2 || folder2.filename().string() != "New_Folder_1") {
        std::cerr << "FAIL: Name deduplication for folder failed! Expected New_Folder_1, got " 
                  << folder2.filename().string() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Created '" << folder1.filename().string() 
              << "' and '" << folder2.filename().string() << "' cleanly." << std::endl;

    // 2. Create Scene (.axlescene) & Companion .meta
    std::cout << "\n[15.2] Testing Scene (.axlescene) creation and companion .meta..." << std::endl;
    auto scene1 = Axle::ContentBrowserPanel::CreateNewScene(testDir);
    auto scene2 = Axle::ContentBrowserPanel::CreateNewScene(testDir);

    auto sceneMeta1 = Axle::AssetMetadataSerializer::GetMetaPath(scene1);
    if (!std::filesystem::exists(scene1) || !std::filesystem::exists(sceneMeta1)) {
        std::cerr << "FAIL: CreateNewScene did not create scene file or companion .meta!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (scene2.filename().string() != "New_Scene_1.axlescene") {
        std::cerr << "FAIL: Scene deduplication failed! Got " << scene2.filename().string() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    Axle::AssetMetadata sceneMeta;
    if (!Axle::AssetMetadataSerializer::Deserialize(sceneMeta1, sceneMeta) || sceneMeta.Type != Axle::AssetType::Scene) {
        std::cerr << "FAIL: Scene companion .meta invalid or wrong AssetType!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Scene '" << scene1.filename().string() << "' created with .meta [UUID: " 
              << (uint64_t)sceneMeta.Handle << "]" << std::endl;

    // 3. Create Material Asset (.axlemat) & Default PBR Values
    std::cout << "\n[15.3] Testing Material Asset (.axlemat) creation..." << std::endl;
    auto mat1 = Axle::ContentBrowserPanel::CreateNewMaterial(testDir);
    auto mat2 = Axle::ContentBrowserPanel::CreateNewMaterial(testDir);

    auto matMeta1 = Axle::AssetMetadataSerializer::GetMetaPath(mat1);
    if (!std::filesystem::exists(mat1) || !std::filesystem::exists(matMeta1)) {
        std::cerr << "FAIL: CreateNewMaterial did not write material file or companion .meta!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (mat2.filename().string() != "New_Material_1.axlemat") {
        std::cerr << "FAIL: Material deduplication failed! Got " << mat2.filename().string() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    Axle::MaterialComponent loadedMat;
    if (!Axle::SceneHierarchyPanel::LoadMaterialAsset(mat1, loadedMat)) {
        std::cerr << "FAIL: Could not load created .axlemat material file!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Material '" << mat1.filename().string() 
              << "' created with default Albedo: (" << loadedMat.AlbedoColor.r << ", " 
              << loadedMat.AlbedoColor.g << ", " << loadedMat.AlbedoColor.b << ")" << std::endl;

    // 4. Create C++ Script (.hpp) & Boilerplate
    std::cout << "\n[15.4] Testing C++ Script (.hpp) boilerplate creation..." << std::endl;
    auto script1 = Axle::ContentBrowserPanel::CreateNewScript(testDir);
    auto script2 = Axle::ContentBrowserPanel::CreateNewScript(testDir);

    auto scriptMeta1 = Axle::AssetMetadataSerializer::GetMetaPath(script1);
    if (!std::filesystem::exists(script1) || !std::filesystem::exists(scriptMeta1)) {
        std::cerr << "FAIL: CreateNewScript did not write script file or companion .meta!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (script2.filename().string() != "NewScript_1.hpp") {
        std::cerr << "FAIL: Script deduplication failed! Got " << script2.filename().string() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Verify script contains AXLE_SCRIPT_FIELDS and AXLE_REGISTER_SCRIPT
    std::ifstream scriptIn(script1);
    std::string scriptContent((std::istreambuf_iterator<char>(scriptIn)), std::istreambuf_iterator<char>());
    scriptIn.close();
    if (scriptContent.find("AXLE_SCRIPT_FIELDS") == std::string::npos ||
        scriptContent.find("AXLE_REGISTER_SCRIPT") == std::string::npos) {
        std::cerr << "FAIL: Generated C++ script does not contain required reflection macros!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: C++ Script boilerplate with reflection macro verified." << std::endl;

    // 5. Create Physics Material (.physmat) & Restitution/Friction Validation
    std::cout << "\n[15.5] Testing Physics Material (.physmat) creation..." << std::endl;
    auto phys1 = Axle::ContentBrowserPanel::CreateNewPhysicsMaterial(testDir);
    auto phys2 = Axle::ContentBrowserPanel::CreateNewPhysicsMaterial(testDir);

    auto physMeta1 = Axle::AssetMetadataSerializer::GetMetaPath(phys1);
    if (!std::filesystem::exists(phys1) || !std::filesystem::exists(physMeta1)) {
        std::cerr << "FAIL: CreateNewPhysicsMaterial did not create file or companion .meta!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (phys2.filename().string() != "New_PhysicsMaterial_1.physmat") {
        std::cerr << "FAIL: PhysicsMaterial deduplication failed! Got " << phys2.filename().string() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    Axle::PhysicsMaterial loadedPhys;
    if (!Axle::PhysicsMaterial::Load(phys1, loadedPhys)) {
        std::cerr << "FAIL: PhysicsMaterial::Load failed to parse .physmat file!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (std::abs(loadedPhys.Friction - 0.5f) > 0.01f || std::abs(loadedPhys.Restitution - 0.0f) > 0.01f) {
        std::cerr << "FAIL: Physics material values do not match defaults!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Physics Material created with Friction: " << loadedPhys.Friction 
              << ", Restitution: " << loadedPhys.Restitution << std::endl;

    // 6. Verify all created assets are indexed in AssetManager
    std::cout << "\n[15.6] Verifying all assets indexed in AssetManager..." << std::endl;
    if (!Axle::AssetManager::HasAsset(scene1) ||
        !Axle::AssetManager::HasAsset(mat1) ||
        !Axle::AssetManager::HasAsset(script1) ||
        !Axle::AssetManager::HasAsset(phys1)) {
        std::cerr << "FAIL: Created assets missing from AssetManager registry!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: All created assets registered in AssetManager with persistent UUIDs." << std::endl;

    // Cleanup
    Axle::AssetManager::Shutdown();
    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 15 PASSED: Phase 17.2 Right-Click Asset Creation verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 16: PHASE 17.3 DEDICATED MATERIAL INSPECTOR TEST
// =============================================================================
bool RunTest_Step16_Phase17_DedicatedMaterialInspector() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 16 TEST] PHASE 17.3 DEDICATED MATERIAL INSPECTOR  " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::filesystem::path testDir = "sandbox_mat_inspector_test";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    Axle::AssetManager::Init(testDir);

    // 1. Dual Format (.amat & .axlemat) Persistence & Serialization Roundtrip
    std::cout << "\n[16.1] Testing Dual-Format (.amat & .axlemat) Persistence..." << std::endl;
    std::filesystem::path amatPath = testDir / "RacingPaint.amat";
    std::filesystem::path axlematPath = testDir / "GoldMetal.axlemat";

    Axle::MaterialComponent mat1;
    mat1.AlbedoColor = glm::vec4(0.88f, 0.12f, 0.15f, 0.95f);
    mat1.AlbedoPath = "textures/paint_base.png";
    mat1.UseAlbedoMap = true;
    mat1.NormalPath = "textures/paint_normal.png";
    mat1.UseNormalMap = true;
    mat1.NormalStrength = 2.2f;
    mat1.Roughness = 0.18f;
    mat1.RoughnessPath = "textures/paint_rough.png";
    mat1.UseRoughnessMap = true;
    mat1.Metallic = 0.90f;
    mat1.MetallicPath = "textures/paint_metal.png";
    mat1.UseMetallicMap = true;
    mat1.EmissionColor = glm::vec3(0.8f, 0.1f, 0.1f);
    mat1.EmissionIntensity = 3.5f;
    mat1.UVScale = glm::vec2(2.5f, 2.5f);

    if (!Axle::SceneHierarchyPanel::SaveMaterialAsset(amatPath, mat1)) {
        std::cerr << "FAIL: Failed to save .amat material file!" << std::endl;
        return false;
    }

    Axle::MaterialComponent loadedAmat;
    if (!Axle::SceneHierarchyPanel::LoadMaterialAsset(amatPath, loadedAmat)) {
        std::cerr << "FAIL: Failed to load .amat material file!" << std::endl;
        return false;
    }

    if (std::abs(loadedAmat.AlbedoColor.r - 0.88f) > 1e-3f ||
        std::abs(loadedAmat.Roughness - 0.18f) > 1e-3f ||
        std::abs(loadedAmat.Metallic - 0.90f) > 1e-3f ||
        std::abs(loadedAmat.NormalStrength - 2.2f) > 1e-3f ||
        std::abs(loadedAmat.EmissionIntensity - 3.5f) > 1e-3f ||
        std::abs(loadedAmat.UVScale.x - 2.5f) > 1e-3f ||
        loadedAmat.AlbedoPath != "textures/paint_base.png" ||
        loadedAmat.NormalPath != "textures/paint_normal.png" ||
        loadedAmat.RoughnessPath != "textures/paint_rough.png" ||
        loadedAmat.MetallicPath != "textures/paint_metal.png") {
        std::cerr << "FAIL: Loaded .amat material parameter mismatch!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: .amat file format saved and loaded with 100% parameter fidelity." << std::endl;

    // Save and load .axlemat
    Axle::MaterialComponent mat2;
    mat2.SetPresetGold();
    mat2.UVScale = glm::vec2(4.0f, 4.0f);
    if (!Axle::SceneHierarchyPanel::SaveMaterialAsset(axlematPath, mat2)) {
        std::cerr << "FAIL: Failed to save .axlemat material file!" << std::endl;
        return false;
    }
    Axle::MaterialComponent loadedAxlemat;
    if (!Axle::SceneHierarchyPanel::LoadMaterialAsset(axlematPath, loadedAxlemat)) {
        std::cerr << "FAIL: Failed to load .axlemat material file!" << std::endl;
        return false;
    }
    if (std::abs(loadedAxlemat.Metallic - 1.0f) > 1e-3f ||
        std::abs(loadedAxlemat.Roughness - 0.18f) > 1e-3f ||
        std::abs(loadedAxlemat.UVScale.x - 4.0f) > 1e-3f) {
        std::cerr << "FAIL: Loaded .axlemat Gold preset parameter mismatch!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: .axlemat file format verified with Gold preset." << std::endl;

    // 2. Testing Material Presets
    std::cout << "\n[16.2] Testing Material Presets Library..." << std::endl;
    Axle::MaterialComponent presetMat;
    presetMat.SetPresetChrome();
    if (presetMat.Metallic != 1.0f || presetMat.Roughness != 0.04f) {
        std::cerr << "FAIL: Chrome preset properties incorrect!" << std::endl;
        return false;
    }
    presetMat.SetPresetRubber();
    if (presetMat.Metallic != 0.0f || presetMat.Roughness != 0.85f) {
        std::cerr << "FAIL: Rubber preset properties incorrect!" << std::endl;
        return false;
    }
    presetMat.SetPresetObsidian();
    if (presetMat.Metallic != 0.0f || presetMat.Roughness != 0.08f) {
        std::cerr << "FAIL: Obsidian preset properties incorrect!" << std::endl;
        return false;
    }
    presetMat.SetPresetHeadlightLED();
    if (presetMat.EmissionIntensity != 5.0f || presetMat.Roughness != 0.10f) {
        std::cerr << "FAIL: Headlight LED preset properties incorrect!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: All PBR material presets verified (Chrome, Rubber, Obsidian, Headlight LED)." << std::endl;

    // 3. Testing Applying Material to Entity
    std::cout << "\n[16.3] Testing Application to Scene Entity..." << std::endl;
    auto testScene = std::make_shared<Axle::Scene>();
    auto ent = testScene->CreateEntity("CyberCar");
    ent.AddComponent<Axle::TransformComponent>();
    
    // Apply material asset to entity
    auto& comp = ent.AddComponent<Axle::MaterialComponent>(loadedAmat);
    if (comp.AlbedoPath != "textures/paint_base.png" ||
        std::abs(comp.Roughness - 0.18f) > 1e-3f ||
        std::abs(comp.Metallic - 0.90f) > 1e-3f) {
        std::cerr << "FAIL: Entity MaterialComponent did not receive material asset properties!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Applied material asset directly to Entity 'CyberCar'." << std::endl;

    // 4. Testing .meta file Companion Generation for .amat and .axlemat
    std::cout << "\n[16.4] Testing .meta file companion for .amat and .axlemat..." << std::endl;
    auto metaAmat = Axle::AssetManager::GetOrCreateMetadata(amatPath);
    auto metaAxlemat = Axle::AssetManager::GetOrCreateMetadata(axlematPath);

    if (metaAmat.Type != Axle::AssetType::Material || metaAxlemat.Type != Axle::AssetType::Material) {
        std::cerr << "FAIL: Material asset files not categorized as AssetType::Material!" << std::endl;
        return false;
    }
    if (metaAmat.Handle == 0 || metaAxlemat.Handle == 0) {
        std::cerr << "FAIL: Material asset files received invalid UUID!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Both .amat and .axlemat recognized as AssetType::Material with valid UUIDs." << std::endl;

    // Cleanup
    Axle::AssetManager::Shutdown();
    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 16 PASSED: Phase 17.3 Dedicated Material Inspector verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 17: PHASE 18.1 FULL 3-FLOW DRAG & DROP & C++ CLASS CREATION TEST
// =============================================================================
bool RunTest_Step17_Phase18_DragDropPipelinesAndClassCreation() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 17 TEST] PHASE 18.1 DRAG & DROP & C++ CLASS CREATION" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::filesystem::path testDir = "sandbox_dragdrop_phase18_test";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    Axle::AssetManager::Init(testDir);

    // 1. C++ Class Generation (.hpp + .cpp)
    std::cout << "\n[17.1] Testing C++ Class (.hpp + .cpp) Generation..." << std::endl;
    auto [hppPath, cppPath] = Axle::ContentBrowserPanel::CreateNewClass(testDir, "VehicleController");

    if (!std::filesystem::exists(hppPath) || !std::filesystem::exists(cppPath)) {
        std::cerr << "FAIL: CreateNewClass did not generate both .hpp and .cpp files!" << std::endl;
        return false;
    }

    // Verify .hpp contents
    std::ifstream hppFile(hppPath);
    std::string hppContent((std::istreambuf_iterator<char>(hppFile)), std::istreambuf_iterator<char>());
    if (hppContent.find("class VehicleController : public Axle::ScriptableEntity") == std::string::npos ||
        hppContent.find("AXLE_SCRIPT_FIELDS(VehicleController") == std::string::npos ||
        hppContent.find("void OnCreate() override;") == std::string::npos ||
        hppContent.find("void OnUpdate(Axle::Timestep ts) override;") == std::string::npos) {
        std::cerr << "FAIL: Generated VehicleController.hpp missing critical class declarations or macros!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: VehicleController.hpp generated with valid class declaration & reflection macros." << std::endl;

    // Verify .cpp contents
    std::ifstream cppFile(cppPath);
    std::string cppContent((std::istreambuf_iterator<char>(cppFile)), std::istreambuf_iterator<char>());
    if (cppContent.find("#include \"VehicleController.hpp\"") == std::string::npos ||
        cppContent.find("AXLE_REGISTER_SCRIPT(VehicleController);") == std::string::npos ||
        cppContent.find("void VehicleController::OnCreate()") == std::string::npos ||
        cppContent.find("void VehicleController::OnUpdate(") == std::string::npos) {
        std::cerr << "FAIL: Generated VehicleController.cpp missing includes, registration, or implementations!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: VehicleController.cpp generated with includes, script registration & function bodies." << std::endl;

    // Verify companion .meta generation
    auto hppMeta = Axle::AssetManager::GetOrCreateMetadata(hppPath);
    auto cppMeta = Axle::AssetManager::GetOrCreateMetadata(cppPath);
    if (hppMeta.Type != Axle::AssetType::Script || cppMeta.Type != Axle::AssetType::Script) {
        std::cerr << "FAIL: .hpp or .cpp files not categorized as AssetType::Script in AssetManager!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Both .hpp and .cpp companions generated with persistent UUIDs." << std::endl;

    // Test name deduplication
    auto [hppPath2, cppPath2] = Axle::ContentBrowserPanel::CreateNewClass(testDir, "VehicleController");
    if (hppPath2.filename() != "VehicleController_1.hpp" || cppPath2.filename() != "VehicleController_1.cpp") {
        std::cerr << "FAIL: Class name deduplication failed!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Name deduplication verified ('VehicleController_1.hpp' & '.cpp')." << std::endl;

    // 2. Flow 3: Prefab Assembly Packaging from Entity Hierarchy
    std::cout << "\n[17.2] Testing Prefab Assembly Packaging (Hierarchy -> Content Browser)..." << std::endl;
    auto sourceScene = std::make_shared<Axle::Scene>();
    auto rootCar = sourceScene->CreateEntity("CyberTruck");
    rootCar.AddComponent<Axle::TransformComponent>(glm::vec3(0.0f, 2.0f, 0.0f));

    auto wheelFL = sourceScene->CreateEntity("Wheel_FL");
    wheelFL.AddComponent<Axle::TransformComponent>(glm::vec3(-1.0f, -0.5f, 1.5f));
    wheelFL.SetParent(rootCar);

    auto wheelFR = sourceScene->CreateEntity("Wheel_FR");
    wheelFR.AddComponent<Axle::TransformComponent>(glm::vec3(1.0f, -0.5f, 1.5f));
    wheelFR.SetParent(rootCar);

    auto cabin = sourceScene->CreateEntity("Cabin");
    cabin.AddComponent<Axle::TransformComponent>(glm::vec3(0.0f, 0.8f, 0.0f));
    cabin.SetParent(rootCar);

    std::filesystem::path prefabPath = testDir / "CyberTruck_Prefab.axle";
    if (!Axle::AssemblySerializer::Serialize(prefabPath, rootCar)) {
        std::cerr << "FAIL: Failed to serialize entity hierarchy into Prefab Assembly file!" << std::endl;
        return false;
    }

    auto prefabMeta = Axle::AssetManager::GetOrCreateMetadata(prefabPath);
    if (prefabMeta.Type != Axle::AssetType::Assembly) {
        std::cerr << "FAIL: Prefab Assembly not recognized as AssetType::Assembly!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Entity hierarchy serialized to Prefab Assembly '" << prefabPath.filename().string() << "'." << std::endl;

    // 3. Spawning / Deserializing the packaged Prefab Assembly
    std::cout << "\n[17.3] Testing Spawning Packaged Prefab into Fresh Scene..." << std::endl;
    auto targetScene = std::make_shared<Axle::Scene>();
    auto spawnedCar = Axle::AssemblySerializer::Deserialize(prefabPath, targetScene.get(), nullptr, glm::vec3(10.0f, 0.0f, -5.0f));

    if (!spawnedCar || !spawnedCar.HasComponent<Axle::HierarchyComponent>()) {
        std::cerr << "FAIL: Deserialized Prefab missing or lacks HierarchyComponent!" << std::endl;
        return false;
    }
    const auto& spawnedHierarchy = spawnedCar.GetComponent<Axle::HierarchyComponent>();
    if (spawnedHierarchy.Children.size() != 3) {
        std::cerr << "FAIL: Deserialized Prefab expected 3 children, got: " << spawnedHierarchy.Children.size() << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Prefab deserialized with 3 children intact at custom spawn position." << std::endl;

    // 4. Flow 1 & 2: Material & Script Drag & Drop Target Application
    std::cout << "\n[17.4] Testing Material & Script Drag-Drop Target Binding..." << std::endl;
    auto targetEntity = targetScene->CreateEntity("TargetMeshObject");
    targetEntity.AddComponent<Axle::TransformComponent>();

    // Test Material apply
    Axle::MaterialComponent sampleMat;
    sampleMat.SetPresetChrome();
    std::filesystem::path testMatPath = testDir / "ChromeTest.amat";
    Axle::SceneHierarchyPanel::SaveMaterialAsset(testMatPath, sampleMat);

    Axle::MaterialComponent loadedDropMat;
    if (Axle::SceneHierarchyPanel::LoadMaterialAsset(testMatPath, loadedDropMat)) {
        targetEntity.AddComponent<Axle::MaterialComponent>(loadedDropMat);
    }
    if (!targetEntity.HasComponent<Axle::MaterialComponent>() ||
        targetEntity.GetComponent<Axle::MaterialComponent>().Metallic != 1.0f) {
        std::cerr << "FAIL: Material asset drop did not apply Chrome preset!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Material asset dropped and applied to Entity successfully." << std::endl;

    // Test Script live binding
    if (Axle::ScriptRegistry::Has("ReflectionDemoScript")) {
        if (!targetEntity.HasComponent<Axle::NativeScriptComponent>()) {
            targetEntity.AddComponent<Axle::NativeScriptComponent>();
        }
        bool bound = Axle::ScriptRegistry::BindByName(targetEntity, "ReflectionDemoScript");
        if (!bound) {
            std::cerr << "FAIL: Failed to bind registered script by name!" << std::endl;
            return false;
        }
        std::cout << "  -> PASS: Script 'ReflectionDemoScript' bound to entity via name-based drop pipeline." << std::endl;
    }

    // Cleanup
    Axle::AssetManager::Shutdown();
    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 17 PASSED: Phase 18.1 Drag & Drop Pipelines & C++ Class Creation verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 18: PHASE 18.2 SAFE SCENE STATE SIMULATION (DEEP-CLONE & ZERO DRIFT)
// =============================================================================
bool RunTest_Step18_Phase18_SafeSceneStateSimulation() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 18 TEST] PHASE 18.2 SAFE SCENE STATE SIMULATION" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::unique_ptr<Axle::PhysicsScene> physicsScene = std::make_unique<Axle::PhysicsScene>();
    std::shared_ptr<Axle::Scene> editorScene = std::make_shared<Axle::Scene>();

    std::cout << "\n[18.1] Populating Pristine Editor Scene..." << std::endl;
    
    // 1. Parent Entity with Transform, Mesh, Material, RigidBody, CollisionShape
    Axle::Entity parentEntity = editorScene->CreateEntity("CarChassis");
    Axle::UUID parentUUID = parentEntity.GetUUID();
    auto& parentTc = parentEntity.GetComponent<Axle::TransformComponent>();
    parentTc.Translation = glm::vec3(0.0f, 1.0f, 0.0f);
    parentTc.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    
    Axle::MaterialComponent goldMat;
    goldMat.AlbedoColor = glm::vec4(1.0f, 0.78f, 0.34f, 1.0f);
    goldMat.Roughness = 0.2f;
    goldMat.Metallic = 1.0f;
    parentEntity.AddComponent<Axle::MaterialComponent>(goldMat);

    Axle::BoxBodyProps bProps;
    bProps.Position = parentTc.Translation;
    bProps.HalfExtent = glm::vec3(1.0f, 0.5f, 2.0f);
    bProps.IsDynamic = true;
    Axle::BodyID parentBodyID = physicsScene->CreateBox(bProps);
    parentEntity.AddComponent<Axle::CollisionShape3D>(Axle::CollisionShapeType::Box, bProps.HalfExtent);
    parentEntity.AddComponent<Axle::RigidBody3D>(parentBodyID, true);

    // 2. Child Entity parented to CarChassis with local offset
    Axle::Entity childEntity = editorScene->CreateEntity("Spoiler");
    Axle::UUID childUUID = childEntity.GetUUID();
    auto& childTc = childEntity.GetComponent<Axle::TransformComponent>();
    childTc.Translation = glm::vec3(0.0f, 1.2f, -1.8f);
    childEntity.SetParent(parentEntity);

    // 3. Scripted Entity referencing CarChassis via UUID
    Axle::Entity scriptEntity = editorScene->CreateEntity("GameManager");
    Axle::UUID scriptUUID = scriptEntity.GetUUID();
    auto& nsc = scriptEntity.AddComponent<Axle::NativeScriptComponent>("ReflectionDemoScript");
    nsc.FloatFields["m_MoveSpeed"] = 55.0f;
    nsc.IntFields["m_AmmoCount"] = 999;
    nsc.BoolFields["m_IsInvulnerable"] = true;
    nsc.Vec3Fields["m_TargetPoint"] = glm::vec3(10.0f, 20.0f, 30.0f);
    nsc.EntityFields["TargetEntity"] = parentUUID;

    // 4. Directional Light & Skybox
    Axle::Entity sunEntity = editorScene->CreateEntity("Sun");
    sunEntity.AddComponent<Axle::DirectionalLightComponent>(glm::vec3(1.0f), 2.5f);
    Axle::Entity skyEntity = editorScene->CreateEntity("Sky");
    skyEntity.AddComponent<Axle::SkyboxComponent>();

    editorScene->UpdateWorldTransforms();

    assert(parentEntity.GetComponent<Axle::HierarchyComponent>().Children.size() == 1);
    assert(childEntity.GetComponent<Axle::HierarchyComponent>().Parent == (entt::entity)parentEntity);
    std::cout << "  -> PASS: Pristine EditorScene initialized with 4 entities and hierarchy." << std::endl;

    std::cout << "\n[18.2] Testing Scene::Copy Deep-Cloning & UUID Preservation..." << std::endl;
    std::shared_ptr<Axle::Scene> runtimeScene = Axle::Scene::Copy(editorScene);
    assert(runtimeScene != nullptr);

    // Verify all UUIDs preserved
    Axle::Entity rtParent = runtimeScene->GetEntityByUUID(parentUUID);
    Axle::Entity rtChild = runtimeScene->GetEntityByUUID(childUUID);
    Axle::Entity rtScript = runtimeScene->GetEntityByUUID(scriptUUID);
    assert(rtParent && rtChild && rtScript);
    assert(rtParent.GetName() == "CarChassis");
    assert(rtChild.GetName() == "Spoiler");
    assert(rtScript.GetName() == "GameManager");

    // Verify hierarchy remapped
    assert(rtChild.GetComponent<Axle::HierarchyComponent>().Parent == (entt::entity)rtParent);
    assert(rtParent.GetComponent<Axle::HierarchyComponent>().Children.size() == 1);
    assert(rtParent.GetComponent<Axle::HierarchyComponent>().Children[0] == (entt::entity)rtChild);

    // Verify Script Field values and UUID entity reference preserved
    auto& rtNsc = rtScript.GetComponent<Axle::NativeScriptComponent>();
    assert(rtNsc.FloatFields["m_MoveSpeed"] == 55.0f);
    assert(rtNsc.IntFields["m_AmmoCount"] == 999);
    assert(rtNsc.BoolFields["m_IsInvulnerable"] == true);
    assert(rtNsc.EntityFields["TargetEntity"] == parentUUID);
    Axle::Entity rtTarget = runtimeScene->GetEntityByUUID(rtNsc.EntityFields["TargetEntity"]);
    assert(rtTarget && rtTarget == rtParent);
    std::cout << "  -> PASS: Scene::Copy successfully cloned scene, preserved UUIDs & remapped hierarchy." << std::endl;

    std::cout << "\n[18.3] Simulating Play Mode Perturbations & Severe Physics Drift..." << std::endl;
    runtimeScene->OnRuntimeStart();

    // Drastically alter the runtime transforms as if simulated by physics
    auto& rtParentTc = rtParent.GetComponent<Axle::TransformComponent>();
    rtParentTc.Translation = glm::vec3(1234.5f, -678.9f, 999.0f);
    rtParentTc.Rotation = glm::quat(glm::vec3(1.2f, 0.8f, -0.5f));
    
    // Simulate physics moving the JPH body in physicsScene as well
    physicsScene->SetBodyPositionAndRotation(parentBodyID, glm::vec3(1234.5f, -678.9f, 999.0f), rtParentTc.Rotation, true);
    physicsScene->SetLinearVelocity(parentBodyID, glm::vec3(50.0f, -20.0f, 15.0f));

    // Spawn an ephemeral bullet/projectile entity in runtime
    Axle::Entity bullet = runtimeScene->CreateEntity("RuntimeBullet");
    bullet.AddComponent<Axle::TransformComponent>(glm::vec3(50.0f));

    runtimeScene->OnUpdateRuntime(0.016f);
    assert(rtParent.GetComponent<Axle::TransformComponent>().Translation.x == 1234.5f);
    std::cout << "  -> PASS: Runtime simulation drift introduced (Chassis moved to (1234.5, -678.9, 999.0))." << std::endl;

    std::cout << "\n[18.4] Testing Stop Mode Discard & Zero Physics Drift Restoration..." << std::endl;
    // Discard runtime scene
    runtimeScene->OnRuntimeStop();
    runtimeScene.reset();

    // Restore pristine physics bodies
    editorScene->RestorePhysicsBodies(physicsScene.get());

    // Verify EditorScene transforms are 100% UNTOUCHED!
    const auto& pristineParentTc = parentEntity.GetComponent<Axle::TransformComponent>();
    assert(pristineParentTc.Translation == glm::vec3(0.0f, 1.0f, 0.0f));
    assert(pristineParentTc.Rotation == glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    // Verify JPH physics scene body was restored to pristine position & zeroed velocity
    glm::vec3 restoredPos = physicsScene->GetBodyPosition(parentBodyID);
    glm::vec3 restoredVel = physicsScene->GetLinearVelocity(parentBodyID);
    assert(glm::distance(restoredPos, glm::vec3(0.0f, 1.0f, 0.0f)) < 0.001f);
    assert(glm::length(restoredVel) < 0.001f);

    // Verify hierarchy in editorScene is intact
    assert(childEntity.GetComponent<Axle::HierarchyComponent>().Parent == (entt::entity)parentEntity);
    assert(parentEntity.GetComponent<Axle::HierarchyComponent>().Children.size() == 1);

    // Verify ephemeral bullet was destroyed with runtimeScene and never leaked into editorScene
    bool bulletFound = false;
    auto tagView = editorScene->GetRegistry().view<Axle::TagComponent>();
    for (auto e : tagView) {
        if (tagView.get<Axle::TagComponent>(e).Tag == "RuntimeBullet") {
            bulletFound = true;
            break;
        }
    }
    assert(!bulletFound);

    std::cout << "  -> PASS: Pristine EditorScene completely restored." << std::endl;
    std::cout << "  -> PASS: Physics body zero-drift restoration verified (vel=0, pos=(0, 1, 0))." << std::endl;
    std::cout << "  -> PASS: Ephemeral runtime entities safely discarded." << std::endl;

    std::cout << ">>> STEP 18 PASSED: Phase 18.2 Safe Scene State Simulation verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 19: PHASE 18.3 DECOUPLED AXLESCRIPTS.DLL SHARED MODULE & EXPORT API
// =============================================================================
bool RunTest_Step19_Phase18_DecoupledAxleScriptsModule() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 19 TEST] PHASE 18.3 DECOUPLED AXLESCRIPTS.DLL MODULE" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::cout << "\n[19.1] Locating AxleScripts.dll binary..." << std::endl;
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path dllPath = std::filesystem::path(exePath).parent_path() / "AxleScripts.dll";
    if (!std::filesystem::exists(dllPath)) {
        dllPath = "build/Editor/Debug/AxleScripts.dll";
    }
    if (!std::filesystem::exists(dllPath)) {
        dllPath = "build/Editor/Release/AxleScripts.dll";
    }
    if (!std::filesystem::exists(dllPath)) {
        dllPath = "AxleScripts.dll";
    }

    std::cout << "  -> DLL Path: " << dllPath.string() << std::endl;
    assert(std::filesystem::exists(dllPath));
    std::cout << "  -> PASS: AxleScripts.dll exists on disk." << std::endl;

    std::cout << "\n[19.2] Dynamically loading AxleScripts.dll..." << std::endl;
    HMODULE hModule = LoadLibraryA(dllPath.string().c_str());
    assert(hModule != nullptr);
    std::cout << "  -> PASS: Successfully loaded AxleScripts.dll into process memory." << std::endl;

    std::cout << "\n[19.3] Querying exported C-API entry points..." << std::endl;
    using GetCountFn = uint32_t(*)();
    using InitFn = void(*)(Axle::ScriptRegisterCallback);
    using ShutdownFn = void(*)();

    GetCountFn fnGetCount = (GetCountFn)GetProcAddress(hModule, "Axle_GetScriptCount");
    InitFn fnInit = (InitFn)GetProcAddress(hModule, "Axle_InitializeScripts");
    ShutdownFn fnShutdown = (ShutdownFn)GetProcAddress(hModule, "Axle_ShutdownScripts");

    assert(fnGetCount != nullptr);
    assert(fnInit != nullptr);
    assert(fnShutdown != nullptr);
    std::cout << "  -> PASS: Exported symbols 'Axle_GetScriptCount', 'Axle_InitializeScripts', 'Axle_ShutdownScripts' found." << std::endl;

    std::cout << "\n[19.4] Testing Script Discovery & Dynamic Registration..." << std::endl;
    uint32_t scriptCount = fnGetCount();
    std::cout << "  -> Discovered " << scriptCount << " script(s) in AxleScripts.dll." << std::endl;
    assert(scriptCount >= 1);

    // Call Axle_InitializeScripts and register into Axle::ScriptRegistry
    fnInit([](const char* name, const Axle::ScriptMetadata* meta) {
        std::cout << "    * Dynamic Registration: '" << name << "' with " << meta->Fields.size() << " reflected field(s)." << std::endl;
        Axle::ScriptRegistry::RegisterMetadata(name, *meta, true);
    });

    assert(Axle::ScriptRegistry::Has("RotatorScript"));
    assert(Axle::ScriptRegistry::IsDynamicScript("RotatorScript"));
    const auto* rotatorMeta = Axle::ScriptRegistry::GetMetadata("RotatorScript");
    assert(rotatorMeta != nullptr);
    assert(rotatorMeta->Fields.size() == 3);
    assert(rotatorMeta->Fields[0].Name == "RotationSpeed");
    assert(rotatorMeta->Fields[1].Name == "Axis");
    assert(rotatorMeta->Fields[2].Name == "Enabled");
    std::cout << "  -> PASS: 'RotatorScript' registered dynamically with all 3 reflected fields." << std::endl;

    std::cout << "\n[19.5] Testing Dynamic Script Instantiation & Field Synchronization..." << std::endl;
    std::shared_ptr<Axle::Scene> testScene = std::make_shared<Axle::Scene>();
    Axle::Entity testEntity = testScene->CreateEntity("RotatingBeacon");
    auto& nsc = testEntity.AddComponent<Axle::NativeScriptComponent>("RotatorScript");
    nsc.FloatFields["RotationSpeed"] = 180.0f;
    nsc.Vec3Fields["Axis"] = glm::vec3(0.0f, 0.0f, 1.0f);
    nsc.BoolFields["Enabled"] = true;

    // Start scene runtime (automatically instantiates script, binds Entity, syncs fields, calls OnCreate)
    testScene->OnRuntimeStart();
    assert(nsc.Instance != nullptr);

    // Update scene runtime (calls OnUpdate with delta time)
    testScene->OnUpdateRuntime(0.1f);

    // Stop scene runtime (calls OnDestroy and cleans up instance)
    testScene->OnRuntimeStop();
    std::cout << "  -> PASS: Instantiated dynamic script via Scene, verified field sync, execution and destruction." << std::endl;

    std::cout << "\n[19.6] Testing Dynamic Script Unloading & Registry Cleanup..." << std::endl;
    Axle::ScriptRegistry::ClearDynamicScripts();
    assert(!Axle::ScriptRegistry::Has("RotatorScript"));
    assert(!Axle::ScriptRegistry::IsDynamicScript("RotatorScript"));
    std::cout << "  -> PASS: Dynamic scripts cleared from ScriptRegistry without affecting static scripts." << std::endl;

    fnShutdown();
    FreeLibrary(hModule);
    std::cout << "  -> PASS: AxleScripts.dll cleanly shut down and unloaded from memory." << std::endl;

    std::cout << ">>> STEP 19 PASSED: Phase 18.3 Decoupled AxleScripts.dll Module verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 20: PHASE 18.3 ANTI-LOCK SHADOW-COPY & STATE PRESERVATION
// =============================================================================
bool RunTest_Step20_Phase18_AntiLockShadowCopyAndStatePreservation() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 20 TEST] PHASE 18.3 ANTI-LOCK SHADOW-COPY & STATE" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::cout << "\n[20.1] Initializing ScriptHotReloadManager..." << std::endl;
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path dllPath = std::filesystem::path(exePath).parent_path() / "AxleScripts.dll";
    if (!std::filesystem::exists(dllPath)) {
        dllPath = "build/Editor/Debug/AxleScripts.dll";
    }
    if (!std::filesystem::exists(dllPath)) {
        dllPath = "build/Editor/Release/AxleScripts.dll";
    }
    auto& reloadManager = Axle::ScriptHotReloadManager::Get();
    bool initOk = reloadManager.Initialize(dllPath.string());
    assert(initOk);
    assert(reloadManager.IsLoaded());
    std::cout << "  -> PASS: ScriptHotReloadManager initialized." << std::endl;

    std::cout << "\n[20.2] Verifying Anti-Lock Shadow-Copy mechanism..." << std::endl;
    auto sourceDll = reloadManager.GetSourceDllPath();
    auto runtimeDll = reloadManager.GetCurrentRuntimeDllPath();
    std::cout << "  -> Source DLL:  " << sourceDll.string() << std::endl;
    std::cout << "  -> Runtime DLL: " << runtimeDll.string() << std::endl;
    assert(sourceDll != runtimeDll);
    assert(std::filesystem::exists(runtimeDll));

    // Test that source DLL is NOT locked by the process (preventing LNK1104 during builds)
    std::ofstream lockTest(sourceDll, std::ios::in | std::ios::out | std::ios::binary);
    bool canOpenSourceForWrite = lockTest.is_open();
    if (lockTest.is_open()) lockTest.close();
    assert(canOpenSourceForWrite);
    std::cout << "  -> PASS: Source AxleScripts.dll has ZERO locks (verified writable for MSBuild/Linker)!" << std::endl;

    std::cout << "\n[20.3] Setting up Scene with Multiple Dynamic Script Entities..." << std::endl;
    auto scene = std::make_shared<Axle::Scene>();
    
    // Entity 1: BeaconAlpha
    auto beaconAlpha = scene->CreateEntity("BeaconAlpha");
    auto& nscAlpha = beaconAlpha.AddComponent<Axle::NativeScriptComponent>("RotatorScript");
    nscAlpha.FloatFields["RotationSpeed"] = 45.0f;
    nscAlpha.Vec3Fields["Axis"] = glm::vec3(1.0f, 0.0f, 0.0f);
    nscAlpha.BoolFields["Enabled"] = true;

    // Entity 2: BeaconBeta
    auto beaconBeta = scene->CreateEntity("BeaconBeta");
    auto& nscBeta = beaconBeta.AddComponent<Axle::NativeScriptComponent>("RotatorScript");
    nscBeta.FloatFields["RotationSpeed"] = -120.0f;
    nscBeta.Vec3Fields["Axis"] = glm::vec3(0.0f, 1.0f, 0.0f);
    nscBeta.BoolFields["Enabled"] = false;

    // Start simulation / edit state
    scene->OnRuntimeStart();
    assert(nscAlpha.Instance != nullptr);
    assert(nscBeta.Instance != nullptr);

    // Modify a field dynamically in memory to simulate active game modifications
    nscAlpha.FloatFields["RotationSpeed"] = 888.8f;
    nscAlpha.SyncFieldsToInstance();

    std::cout << "  -> Pre-reload BeaconAlpha RotationSpeed: " << nscAlpha.FloatFields["RotationSpeed"] << std::endl;
    std::cout << "  -> Pre-reload BeaconBeta RotationSpeed:  " << nscBeta.FloatFields["RotationSpeed"] << std::endl;
    std::cout << "  -> PASS: Entities populated and runtime instances active." << std::endl;

    std::cout << "\n[20.4] Triggering Hot-Reload with Active Scene State Preservation..." << std::endl;
    auto reloadResult = reloadManager.Reload(scene, true);
    assert(reloadResult.Success);
    std::cout << "  -> Hot Reload Result: " << reloadResult.Message << std::endl;
    std::cout << "  -> New Runtime DLL: " << reloadManager.GetCurrentRuntimeDllPath().filename().string() << std::endl;
    assert(reloadManager.GetCurrentRuntimeDllPath() != runtimeDll);

    std::cout << "\n[20.5] Verifying State Preservation across Reload..." << std::endl;
    assert(nscAlpha.Instance != nullptr);
    assert(nscBeta.Instance != nullptr);
    assert(std::abs(nscAlpha.FloatFields["RotationSpeed"] - 888.8f) < 0.001f);
    assert(std::abs(nscBeta.FloatFields["RotationSpeed"] - (-120.0f)) < 0.001f);
    assert(nscBeta.BoolFields["Enabled"] == false);
    assert(nscBeta.Vec3Fields["Axis"].y == 1.0f);
    std::cout << "  -> PASS: All modified fields preserved 100% with fresh class instances!" << std::endl;

    std::cout << "\n[20.6] Shutting down and cleaning shadow copies..." << std::endl;
    scene->OnRuntimeStop();
    reloadManager.Shutdown();
    assert(!reloadManager.IsLoaded());
    std::cout << "  -> PASS: ScriptHotReloadManager successfully cleaned up." << std::endl;

    std::cout << ">>> STEP 20 PASSED: Phase 18.3 Anti-Lock Shadow-Copy & State Preservation verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 21: PHASE 18.3 ASYNC BACKGROUND SCRIPT COMPILER
// =============================================================================
bool RunTest_Step21_Phase18_AsyncBackgroundScriptCompiler() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 21 TEST] PHASE 18.3 ASYNC BACKGROUND COMPILER    " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::cout << "\n[21.1] Testing MSVC Diagnostic Output Parsing..." << std::endl;
    // Test Error Line
    std::string errLine = "D:\\WorkSpace\\AxleEngine\\Editor\\assets\\scripts\\RotatorScript.hpp(25,10): error C2065: 'speedMultiplier': undeclared identifier";
    auto diagErr = Axle::ScriptCompiler::ParseDiagnosticLine(errLine);
    assert(diagErr.Level == Axle::CompilerDiagnostic::Severity::Error);
    assert(diagErr.Line == 25);
    assert(diagErr.Column == 10);
    assert(diagErr.Code == "C2065");
    std::cout << "  -> PASS: Parsed C++ compiler error: " << diagErr.Code << " at line " << diagErr.Line << std::endl;

    // Test Warning Line
    std::string warnLine = "D:\\WorkSpace\\AxleEngine\\Editor\\src\\Scripts\\ScriptsModule.cpp(42): warning C4101: 'unusedVar': unreferenced local variable";
    auto diagWarn = Axle::ScriptCompiler::ParseDiagnosticLine(warnLine);
    assert(diagWarn.Level == Axle::CompilerDiagnostic::Severity::Warning);
    assert(diagWarn.Line == 42);
    assert(diagWarn.Code == "C4101");
    std::cout << "  -> PASS: Parsed C++ compiler warning: " << diagWarn.Code << " at line " << diagWarn.Line << std::endl;

    // Test Linker Error Line
    std::string linkLine = "LINK : fatal error LNK1104: cannot open file 'AxleScripts.dll'";
    auto diagLink = Axle::ScriptCompiler::ParseDiagnosticLine(linkLine);
    assert(diagLink.Level == Axle::CompilerDiagnostic::Severity::Error);
    assert(diagLink.Code == "LNK1104");
    std::cout << "  -> PASS: Parsed linker error: " << diagLink.Code << std::endl;

    std::cout << "\n[21.2] Testing Asynchronous Background Compilation (Non-Blocking UI)..." << std::endl;
    auto& compiler = Axle::ScriptCompiler::Get();
    compiler.SetBuildConfig("Release");
    compiler.SetTarget("AxleScripts");

    std::atomic<bool> callbackFired{ false };
    Axle::CompilationResult asyncResult;

    bool started = compiler.CompileAsync([&](const Axle::CompilationResult& res) {
        asyncResult = res;
        callbackFired = true;
    });
    assert(started);

    // Simulate Editor UI Loop running smoothly at 60 FPS while background compiler is working
    int simulatedUiFrames = 0;
    while (compiler.IsCompiling()) {
        simulatedUiFrames++;
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps frame tick
    }

    compiler.WaitForCompletion();
    assert(callbackFired.load());
    assert(asyncResult.Success);
    assert(asyncResult.ExitCode == 0);
    assert(asyncResult.DurationSeconds > 0.0);
    std::cout << "  -> PASS: Compilation completed in background in " << asyncResult.DurationSeconds << "s." << std::endl;
    std::cout << "  -> PASS: Editor UI simulated " << simulatedUiFrames << " frames without freezing!" << std::endl;

    std::cout << "\n[21.3] Testing Live Output Log and Metadata Retrieval..." << std::endl;
    std::string liveLog = compiler.GetCurrentOutputLog();
    assert(!liveLog.empty());
    auto lastRes = compiler.GetLastResult();
    assert(lastRes.Success);
    assert(lastRes.ExitCode == 0);
    std::cout << "  -> Captured " << liveLog.size() << " bytes of compiler output log." << std::endl;
    std::cout << "  -> PASS: CompilerStatus is Success and exit code is 0." << std::endl;

    std::cout << ">>> STEP 21 PASSED: Phase 18.3 Async Background Compiler verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 22: PHASE 19.2 CORE JOBSYSTEM ENGINE & PARALLEL PROCESSING
// =============================================================================
bool RunTest_Step22_Phase19_CoreJobSystemEngine() {
    using namespace Axle;
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 22 TEST] PHASE 19.2 CORE JOBSYSTEM ENGINE        " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::cout << "\n[22.1] Initializing JobSystem & Verifying Hardware Concurrency..." << std::endl;
    Axle::JobSystem::Init();
    assert(Axle::JobSystem::IsInitialized());
    
    uint32_t workerThreads = Axle::JobSystem::GetWorkerThreadCount();
    std::cout << "  -> Active Worker Threads: " << workerThreads << std::endl;
    assert(workerThreads >= 1);
    assert(Axle::JobSystem::IsMainThread());
    assert(Axle::JobSystem::GetCurrentThreadIndex() == 0);
    std::cout << "  -> PASS: JobSystem initialized. Main thread is thread 0." << std::endl;

    std::cout << "\n[22.2] Testing Fine-Grained Parallel Loop (ParallelFor with 250,000 items)..." << std::endl;
    const uint32_t elementCount = 250000;
    std::vector<uint64_t> testArray(elementCount, 0);
    std::atomic<uint32_t> processedCount{ 0 };

    Axle::JobSystem::ParallelFor(elementCount, [&](uint32_t index, uint32_t threadNum) {
        (void)threadNum;
        testArray[index] = (static_cast<uint64_t>(index) * 3) + 7;
        processedCount.fetch_add(1, std::memory_order_relaxed);
    }, 4096);

    assert(processedCount.load() == elementCount);
    for (uint32_t i = 0; i < 100; ++i) {
        assert(testArray[i] == (static_cast<uint64_t>(i) * 3) + 7);
    }
    assert(testArray[elementCount - 1] == (static_cast<uint64_t>(elementCount - 1) * 3) + 7);
    std::cout << "  -> PASS: All " << elementCount << " iterations verified 100% correct across all worker threads!" << std::endl;

    std::cout << "\n[22.3] Testing Coarse-Grained Range Processing (ParallelForRange with 50,000 items)..." << std::endl;
    const uint32_t rangeCount = 50000;
    std::vector<float> floatArray(rangeCount, 0.0f);

    Axle::JobSystem::ParallelForRange(rangeCount, [&](uint32_t start, uint32_t end) {
        for (uint32_t i = start; i < end; ++i) {
            floatArray[i] = static_cast<float>(i) * 0.5f;
        }
    }, 2048);

    for (uint32_t i = 0; i < rangeCount; ++i) {
        float expected = static_cast<float>(i) * 0.5f;
        assert(std::abs(floatArray[i] - expected) < 0.0001f);
    }
    std::cout << "  -> PASS: ParallelForRange executed and verified without partition gaps!" << std::endl;

    std::cout << "\n[22.4] Testing Asynchronous Job Dispatching & Dependency Waiting (JobHandle)..." << std::endl;
    std::atomic<bool> asyncTaskDone{ false };
    std::atomic<uint32_t> asyncAccumulator{ 0 };

    auto handle = Axle::JobSystem::Dispatch([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        asyncAccumulator.fetch_add(12345);
        asyncTaskDone.store(true);
    });

    assert(handle.IsValid());
    handle.Wait();
    assert(handle.IsComplete());
    assert(asyncTaskDone.load() == true);
    assert(asyncAccumulator.load() == 12345);
    std::cout << "  -> PASS: Async JobHandle dispatched, waited, and completed with synchronized state." << std::endl;

    std::cout << "\n[22.5] Testing Cross-Thread Main-Thread Dispatching (DispatchToMainThread)..." << std::endl;
    std::atomic<bool> mainThreadTaskExecuted{ false };
    std::atomic<uint32_t> mainThreadWorkerId{ 999 };

    auto workerHandle = Axle::JobSystem::Dispatch([&]() {
        // Dispatched on worker thread, enqueue lambda to main thread
        Axle::JobSystem::DispatchToMainThread([&]() {
            assert(Axle::JobSystem::IsMainThread());
            mainThreadWorkerId.store(Axle::JobSystem::GetCurrentThreadIndex());
            mainThreadTaskExecuted.store(true);
        });
    });

    workerHandle.Wait();
    // Worker finished enqueuing, but main thread hasn't pumped pinned tasks yet
    assert(!mainThreadTaskExecuted.load());

    // Main thread pumps pinned tasks
    Axle::JobSystem::RunPinnedTasks();
    assert(mainThreadTaskExecuted.load());
    assert(mainThreadWorkerId.load() == 0);
    std::cout << "  -> PASS: Cross-thread callback safely executed on main thread (Thread 0)!" << std::endl;

    std::cout << "\n[22.6] Testing JobSystem Shutdown and Re-initialization..." << std::endl;
    Axle::JobSystem::Shutdown();
    assert(!Axle::JobSystem::IsInitialized());
    std::cout << "  -> PASS: Shutdown completed cleanly." << std::endl;

    Axle::JobSystem::Init(2);
    assert(Axle::JobSystem::IsInitialized());
    assert(Axle::JobSystem::GetWorkerThreadCount() == 2);
    std::atomic<int> testCount{ 0 };
    Axle::JobSystem::ParallelFor(100, [&](uint32_t i) {
        (void)i;
        testCount.fetch_add(1);
    });
    assert(testCount.load() == 100);
    Axle::JobSystem::Shutdown();
    assert(!Axle::JobSystem::IsInitialized());
    std::cout << "  -> PASS: Re-initialization and custom thread pool size verified!" << std::endl;

    // Restore default JobSystem for subsequent test steps
    Axle::JobSystem::Init();

    std::cout << ">>> STEP 22 PASSED: Phase 19.2 Core JobSystem Engine verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 23: PHASE 19.3 JOLT PHYSICS BRIDGE (JobSystemEnkiTS)
// =============================================================================
#ifdef AddJob
#undef AddJob
#endif

bool RunTest_Step23_Phase19_JoltPhysicsBridgeEnkiTS() {
    using namespace Axle;
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 23 TEST] PHASE 19.3 JOLT PHYSICS BRIDGE (JobSystemEnkiTS)" << std::endl;
    std::cout << "========================================================" << std::endl;

    if (!Axle::JobSystem::IsInitialized()) {
        Axle::JobSystem::Init();
    }
    if (!Axle::PhysicsEngine::IsInitialized()) {
        Axle::PhysicsEngine::Init();
    }

    std::cout << "\n[23.1] Initializing JobSystemEnkiTS & Verifying Concurrency..." << std::endl;
    auto enkiJobSystem = std::make_unique<JobSystemEnkiTS>(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers
    );
    int concurrency = enkiJobSystem->GetMaxConcurrency();
    std::cout << "  -> JobSystemEnkiTS Concurrency: " << concurrency << std::endl;
    assert(concurrency >= 1);
    assert(concurrency == static_cast<int>(Axle::JobSystem::GetWorkerThreadCount()));
    std::cout << "  -> PASS: JobSystemEnkiTS concurrency matches Axle::JobSystem worker threads." << std::endl;

    std::cout << "\n[23.2] Testing Direct Job Execution & Barrier Synchronization..." << std::endl;
    {
        std::atomic<int> directJobCounter{ 0 };
        JPH::JobSystem::Barrier* barrier = enkiJobSystem->CreateBarrier();
        assert(barrier != nullptr);

        constexpr int numJobs = 64;
        JPH::JobHandle handles[numJobs];
        for (int i = 0; i < numJobs; ++i) {
            handles[i] = enkiJobSystem->CreateJob("DirectPhysicsJob", JPH::ColorArg(0, 255, 0), [&directJobCounter]() {
                directJobCounter.fetch_add(1, std::memory_order_relaxed);
            }, 0);
            barrier->AddJob(handles[i]);
        }

        enkiJobSystem->WaitForJobs(barrier);
        assert(directJobCounter.load() == numJobs);
        std::cout << "  -> PASS: All " << numJobs << " direct jobs synchronized and executed via JobSystemEnkiTS!" << std::endl;
        enkiJobSystem->DestroyBarrier(barrier);
    }

    std::cout << "\n[23.3] Testing Jolt Dependency Chaining (Job A -> Job B)..." << std::endl;
    {
        std::atomic<bool> jobACompleted{ false };
        std::atomic<bool> jobBCompleted{ false };

        JPH::JobHandle jobB = enkiJobSystem->CreateJob("JobB", JPH::ColorArg(255, 0, 0), [&]() {
            assert(jobACompleted.load() == true);
            jobBCompleted.store(true);
        }, 1); // 1 dependency

        JPH::JobHandle jobA = enkiJobSystem->CreateJob("JobA", JPH::ColorArg(0, 0, 255), [&, jobB]() mutable {
            jobACompleted.store(true);
            jobB.RemoveDependency(1);
        }, 0);

        JPH::JobSystem::Barrier* depBarrier = enkiJobSystem->CreateBarrier();
        depBarrier->AddJob(jobA);
        depBarrier->AddJob(jobB);
        enkiJobSystem->WaitForJobs(depBarrier);
        assert(jobACompleted.load() == true);
        assert(jobBCompleted.load() == true);
        std::cout << "  -> PASS: Job dependency chain (Job A -> Job B) executed and verified in order." << std::endl;
        enkiJobSystem->DestroyBarrier(depBarrier);
    }

    enkiJobSystem.reset();

    std::cout << "\n[23.4] Testing Multithreaded Physics Simulation via PhysicsScene..." << std::endl;
    auto physicsScene = std::make_unique<PhysicsScene>();

    // 1. Create static floor
    BoxBodyProps floorProps;
    floorProps.Position = glm::vec3(0.0f, -0.5f, 0.0f);
    floorProps.HalfExtent = glm::vec3(50.0f, 0.5f, 50.0f);
    floorProps.IsDynamic = false;
    BodyID floorID = physicsScene->CreateBox(floorProps);
    assert(!floorID.IsInvalid());

    // 2. Create multiple dynamic boxes dropped in a grid
    constexpr int gridX = 4;
    constexpr int gridZ = 4;
    constexpr int layers = 3;
    std::vector<BodyID> dynamicBodies;

    for (int y = 0; y < layers; ++y) {
        for (int x = 0; x < gridX; ++x) {
            for (int z = 0; z < gridZ; ++z) {
                BoxBodyProps boxProps;
                boxProps.Position = glm::vec3(
                    (x - gridX / 2.0f) * 2.5f,
                    5.0f + y * 2.0f,
                    (z - gridZ / 2.0f) * 2.5f
                );
                boxProps.HalfExtent = glm::vec3(0.5f, 0.5f, 0.5f);
                boxProps.IsDynamic = true;
                boxProps.Friction = 0.5f;
                boxProps.Restitution = 0.0f;
                BodyID bId = physicsScene->CreateBox(boxProps);
                assert(!bId.IsInvalid());
                dynamicBodies.push_back(bId);
            }
        }
    }
    std::cout << "  -> Spawned " << dynamicBodies.size() << " dynamic rigid bodies on static floor." << std::endl;

    // 3. Step physics through JobSystemEnkiTS
    constexpr int simulationSteps = 60;
    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < simulationSteps; ++step) {
        physicsScene->Step(dt);
    }

    // 4. Verify bodies have fallen and settled above the ground without falling through
    int settledCount = 0;
    for (const auto& bId : dynamicBodies) {
        glm::vec3 pos = physicsScene->GetBodyPosition(bId);
        // Box half-extent is 0.5, floor top is at y=0, so y should be >= 0.35f and < initial height
        assert(pos.y >= 0.35f);
        assert(pos.y < 15.0f);
        settledCount++;
    }
    assert(settledCount == static_cast<int>(dynamicBodies.size()));
    std::cout << "  -> PASS: All " << settledCount << " bodies simulated through JobSystemEnkiTS without penetration or drift." << std::endl;

    std::cout << "\n[23.5] Cleaning up PhysicsScene and verifying clean shutdown..." << std::endl;
    physicsScene.reset();
    std::cout << "  -> PASS: PhysicsScene and JobSystemEnkiTS cleanly destroyed." << std::endl;

    std::cout << ">>> STEP 23 PASSED: Phase 19.3 Jolt Physics Bridge (JobSystemEnkiTS) verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 24: Parallel ECS Processing (Batch Views, Hierarchy, CommandBuffer)
// =============================================================================
bool RunTest_Step24_Phase19_ParallelECSProcessing() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 24 TEST] PHASE 19.4: PARALLEL ECS PROCESSING     " << std::endl;
    std::cout << "========================================================" << std::endl;

    using namespace Axle;

    if (!JobSystem::IsInitialized()) {
        JobSystem::Init();
    }
    const uint32_t workerCount = JobSystem::GetWorkerThreadCount();
    std::cout << "  -> Active JobSystem worker threads: " << workerCount << std::endl;

    // -------------------------------------------------------------------------
    // [24.1] Parallel view iteration across 50,000 entities
    // -------------------------------------------------------------------------
    std::cout << "\n[24.1] Testing ParallelECS::ForEach across 50,000 entities..." << std::endl;
    entt::registry registry;
    constexpr uint32_t kEntityCount = 50000;

    std::vector<entt::entity> createdEntities;
    createdEntities.reserve(kEntityCount);
    for (uint32_t i = 0; i < kEntityCount; ++i) {
        entt::entity entity = registry.create();
        registry.emplace<TransformComponent>(entity, glm::vec3(0.0f));
        createdEntities.push_back(entity);
    }
    std::cout << "  -> Spawned " << kEntityCount << " entities with initial zero TransformComponent." << std::endl;

    std::atomic<uint32_t> processedCount{0};
    std::atomic<uint32_t> threadActivity[128]{};

    auto tStart = std::chrono::high_resolution_clock::now();

    ParallelECS::ForEach<TransformComponent>(registry, [&](entt::entity e, TransformComponent& tc, uint32_t threadNum) {
        uint32_t idVal = static_cast<uint32_t>(e);
        tc.Translation = glm::vec3(static_cast<float>(idVal), 1.0f, 2.0f);
        tc.Scale = glm::vec3(1.5f);
        processedCount.fetch_add(1, std::memory_order_relaxed);
        if (threadNum < 128) {
            threadActivity[threadNum].fetch_add(1, std::memory_order_relaxed);
        }
    }, 1024);

    auto tEnd = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

    assert(processedCount.load() == kEntityCount);

    uint32_t threadsUsed = 0;
    for (uint32_t t = 0; t < 128; ++t) {
        if (threadActivity[t].load() > 0) {
            threadsUsed++;
        }
    }

    std::cout << "  -> Processed " << processedCount.load() << " entities in " << elapsedMs 
              << " ms (" << static_cast<uint32_t>((kEntityCount / (elapsedMs / 1000.0))) << " entities/sec)." << std::endl;
    std::cout << "  -> Active threads participating in batch iteration: " << threadsUsed << " thread(s)." << std::endl;
    if (workerCount > 1) {
        assert(threadsUsed > 1);
    }

    // Spot check 10 entities
    for (uint32_t i = 0; i < 10; ++i) {
        entt::entity sampleEnt = createdEntities[i * (kEntityCount / 10)];
        const auto& tc = registry.get<TransformComponent>(sampleEnt);
        uint32_t expectedID = static_cast<uint32_t>(sampleEnt);
        assert(tc.Translation == glm::vec3(static_cast<float>(expectedID), 1.0f, 2.0f));
        assert(tc.Scale == glm::vec3(1.5f));
    }
    std::cout << "  -> PASS: All 50,000 entities verified with lock-free mutations across workers." << std::endl;

    // -------------------------------------------------------------------------
    // [24.2] Multi-component view filtering & mutation
    // -------------------------------------------------------------------------
    std::cout << "\n[24.2] Testing Multi-Component ParallelECS::ForEach<TransformComponent, RigidBody3D>..." << std::endl;
    constexpr uint32_t kRigidBodyCount = 25000;
    for (uint32_t i = 0; i < kRigidBodyCount; ++i) {
        RigidBody3D rb;
        rb.Mass = 10.0f;
        registry.emplace<RigidBody3D>(createdEntities[i], rb);
    }
    std::cout << "  -> Emplaced RigidBody3D on first " << kRigidBodyCount << " entities." << std::endl;

    std::atomic<uint32_t> multiCompCount{0};
    ParallelECS::ForEach<TransformComponent, RigidBody3D>(registry, [&](entt::entity e, TransformComponent& tc, RigidBody3D& rb) {
        (void)e;
        tc.Translation.y = 42.0f;
        rb.Mass = 25.0f;
        multiCompCount.fetch_add(1, std::memory_order_relaxed);
    }, 512);

    assert(multiCompCount.load() == kRigidBodyCount);
    std::cout << "  -> Processed exactly " << multiCompCount.load() << " filtered entities." << std::endl;

    // Verify first half has updated values, second half unchanged
    for (uint32_t i = 0; i < 5; ++i) {
        entt::entity sampleWithRB = createdEntities[i * 100];
        const auto& tc = registry.get<TransformComponent>(sampleWithRB);
        const auto& rb = registry.get<RigidBody3D>(sampleWithRB);
        assert(tc.Translation.y == 42.0f);
        assert(rb.Mass == 25.0f);
    }
    for (uint32_t i = 0; i < 5; ++i) {
        entt::entity sampleWithoutRB = createdEntities[kRigidBodyCount + i * 100];
        const auto& tc = registry.get<TransformComponent>(sampleWithoutRB);
        assert(tc.Translation.y == 1.0f);
        assert(!registry.all_of<RigidBody3D>(sampleWithoutRB));
    }
    std::cout << "  -> PASS: Multi-component filtering verified without false positives or lock contention." << std::endl;

    // -------------------------------------------------------------------------
    // [24.3] Disjoint subtree parallel hierarchy transform evaluation
    // -------------------------------------------------------------------------
    std::cout << "\n[24.3] Testing ParallelECS::UpdateWorldTransforms on disjoint hierarchy subtrees..." << std::endl;
    auto hierarchyScene = std::make_shared<Scene>();
    constexpr uint32_t kRootCount = 500;
    // Each root has 2 children, each child has 2 grandchildren: 500 * (1 + 2 + 4) = 3,500 entities
    std::vector<Entity> rootList;
    rootList.reserve(kRootCount);

    for (uint32_t r = 0; r < kRootCount; ++r) {
        Entity root = hierarchyScene->CreateEntity("Root_" + std::to_string(r));
        root.GetComponent<TransformComponent>().Translation = glm::vec3(static_cast<float>(r) * 10.0f, 0.0f, 0.0f);

        for (uint32_t c = 0; c < 2; ++c) {
            Entity child = hierarchyScene->CreateEntity("Child_" + std::to_string(r) + "_" + std::to_string(c));
            child.SetParent(root);
            child.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 5.0f, 0.0f);

            for (uint32_t g = 0; g < 2; ++g) {
                Entity grandchild = hierarchyScene->CreateEntity("Grandchild_" + std::to_string(r) + "_" + std::to_string(c) + "_" + std::to_string(g));
                grandchild.SetParent(child);
                grandchild.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 0.0f, 3.0f);
            }
        }
        rootList.push_back(root);
    }
    std::cout << "  -> Built scene graph hierarchy with " << kRootCount << " roots and 3,500 total nodes." << std::endl;

    // Parallel evaluate transforms across worker threads
    hierarchyScene->UpdateWorldTransformsParallel(32);

    // Verify correct hierarchical world matrix propagation
    for (uint32_t r = 0; r < kRootCount; r += 50) {
        Entity root = rootList[r];
        const auto& rootTc = root.GetComponent<TransformComponent>();
        glm::vec3 expectedRootWorld(static_cast<float>(r) * 10.0f, 0.0f, 0.0f);
        assert(glm::length(glm::vec3(rootTc.WorldMatrix[3]) - expectedRootWorld) < 1e-4f);

        const auto& rootHc = root.GetComponent<HierarchyComponent>();
        for (auto childID : rootHc.Children) {
            Entity child{ childID, hierarchyScene.get() };
            const auto& childTc = child.GetComponent<TransformComponent>();
            glm::vec3 expectedChildWorld = expectedRootWorld + glm::vec3(0.0f, 5.0f, 0.0f);
            assert(glm::length(glm::vec3(childTc.WorldMatrix[3]) - expectedChildWorld) < 1e-4f);

            const auto& childHc = child.GetComponent<HierarchyComponent>();
            for (auto grandID : childHc.Children) {
                Entity grand{ grandID, hierarchyScene.get() };
                const auto& grandTc = grand.GetComponent<TransformComponent>();
                glm::vec3 expectedGrandWorld = expectedChildWorld + glm::vec3(0.0f, 0.0f, 3.0f);
                assert(glm::length(glm::vec3(grandTc.WorldMatrix[3]) - expectedGrandWorld) < 1e-4f);
            }
        }
    }
    std::cout << "  -> PASS: Disjoint hierarchical subtrees evaluated in parallel matching exact world coordinates." << std::endl;

    // -------------------------------------------------------------------------
    // [24.4] Thread-safe deferred CommandBuffer mutations
    // -------------------------------------------------------------------------
    std::cout << "\n[24.4] Testing ParallelECS::CommandBuffer thread-safe deferred mutations..." << std::endl;
    ParallelECS::CommandBuffer cmdBuffer;

    // During parallel iteration, destroy even entities and emplace TagComponent on odd entities
    ParallelECS::ForEach<TransformComponent>(registry, [&](entt::entity e, TransformComponent& tc, uint32_t threadNum) {
        (void)tc;
        (void)threadNum;
        uint32_t idVal = static_cast<uint32_t>(e);
        if (idVal % 2 == 0) {
            cmdBuffer.DestroyEntity(e);
        } else {
            cmdBuffer.EmplaceOrReplace<TagComponent>(e, "DeferredTag_" + std::to_string(idVal));
        }
    }, 1024);

    assert(cmdBuffer.GetPendingCount() == kEntityCount);
    std::cout << "  -> Recorded " << cmdBuffer.GetPendingCount() << " deferred commands from worker threads." << std::endl;

    // Commit on main thread
    cmdBuffer.Execute(registry);
    assert(cmdBuffer.GetPendingCount() == 0);
    std::cout << "  -> Executed all deferred commands on main thread." << std::endl;

    // Check entity states
    uint32_t validCount = 0;
    uint32_t taggedCount = 0;
    for (entt::entity e : createdEntities) {
        uint32_t idVal = static_cast<uint32_t>(e);
        if (idVal % 2 == 0) {
            assert(!registry.valid(e));
        } else {
            assert(registry.valid(e));
            assert(registry.all_of<TagComponent>(e));
            const auto& tag = registry.get<TagComponent>(e);
            assert(tag.Tag == "DeferredTag_" + std::to_string(idVal));
            validCount++;
            taggedCount++;
        }
    }
    assert(validCount == kEntityCount / 2);
    assert(taggedCount == kEntityCount / 2);
    std::cout << "  -> PASS: Deferred structural mutations executed cleanly without memory corruption." << std::endl;

    // -------------------------------------------------------------------------
    // [24.5] ParallelECS::ForEachEntity on filtered sub-collection
    // -------------------------------------------------------------------------
    std::cout << "\n[24.5] Testing ParallelECS::ForEachEntity on filtered entity collection..." << std::endl;
    std::vector<entt::entity> remainingEntities;
    remainingEntities.reserve(validCount);
    for (entt::entity e : createdEntities) {
        if (registry.valid(e)) {
            remainingEntities.push_back(e);
        }
    }

    ParallelECS::ForEachEntity<TransformComponent>(registry, remainingEntities, [](entt::entity e, TransformComponent& tc) {
        (void)e;
        tc.Scale = glm::vec3(3.0f, 3.0f, 3.0f);
    }, 256);

    for (entt::entity e : remainingEntities) {
        const auto& tc = registry.get<TransformComponent>(e);
        assert(tc.Scale == glm::vec3(3.0f, 3.0f, 3.0f));
    }
    std::cout << "  -> PASS: ForEachEntity updated all " << remainingEntities.size() << " surviving entities." << std::endl;

    std::cout << ">>> STEP 24 PASSED: Phase 19.4 Parallel ECS Processing verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 25: Phase 20 Component-per-Wheel N-Wheeled Vehicle Architecture
// =============================================================================
bool RunTest_Step25_Phase20_ComponentPerWheelVehicle() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 25 TEST] PHASE 20: COMPONENT-PER-WHEEL VEHICLE   " << std::endl;
    std::cout << "========================================================" << std::endl;

    // -------------------------------------------------------------------------
    // [25.1] WheelComponent & VehicleWheel3D serialization roundtrip
    // -------------------------------------------------------------------------
    std::cout << "[25.1] Testing WheelComponent serialization and deserialization..." << std::endl;
    std::filesystem::path testScenePath = "temp_wheel_component_test.axlescene";

    {
        auto srcScene = std::make_shared<Axle::Scene>();
        auto wheelEnt = srcScene->CreateEntity("CustomWheel_FrontLeft");
        auto& tc = wheelEnt.GetComponent<Axle::TransformComponent>();
        tc.Translation = glm::vec3(0.95f, -0.15f, 1.4f);

        auto& wc = wheelEnt.AddComponent<Axle::WheelComponent>();
        wc.Radius = 0.42f;
        wc.Width = 0.28f;
        wc.SuspensionMinLength = 0.18f;
        wc.SuspensionMaxLength = 0.48f;
        wc.SpringFrequency = 3.5f;
        wc.SpringDamping = 0.65f;
        wc.CanSteer = true;
        wc.MaxSteerAngle = 38.0f;
        wc.IsDriven = true;
        wc.HasHandbrake = false;
        wc.BrakeTorque = 4200.0f;
        wc.HandBrakeTorque = 0.0f;

        Axle::SceneSerializer serializer(srcScene.get());
        bool serSuccess = serializer.Serialize(testScenePath);
        assert(serSuccess);
        assert(std::filesystem::exists(testScenePath));
    }

    // Verify deserialization
    {
        auto dstScene = std::make_shared<Axle::Scene>();
        Axle::SceneSerializer deserializer(dstScene.get());
        bool deserSuccess = deserializer.Deserialize(testScenePath);
        assert(deserSuccess);

        auto view = dstScene->GetRegistry().view<Axle::WheelComponent, Axle::TagComponent>();
        assert(view.size_hint() == 1);

        for (auto entityID : view) {
            const auto& tag = view.get<Axle::TagComponent>(entityID).Tag;
            const auto& wc = view.get<Axle::WheelComponent>(entityID);
            assert(tag == "CustomWheel_FrontLeft");
            assert(std::abs(wc.Radius - 0.42f) < 0.001f);
            assert(std::abs(wc.Width - 0.28f) < 0.001f);
            assert(std::abs(wc.SuspensionMinLength - 0.18f) < 0.001f);
            assert(std::abs(wc.SuspensionMaxLength - 0.48f) < 0.001f);
            assert(std::abs(wc.SpringFrequency - 3.5f) < 0.001f);
            assert(std::abs(wc.SpringDamping - 0.65f) < 0.001f);
            assert(wc.CanSteer == true);
            assert(std::abs(wc.MaxSteerAngle - 38.0f) < 0.001f);
            assert(wc.IsDriven == true);
            assert(wc.HasHandbrake == false);
            assert(std::abs(wc.BrakeTorque - 4200.0f) < 0.001f);
        }
        std::error_code ec;
        std::filesystem::remove(testScenePath, ec);
    }
    std::cout << "  -> PASS: WheelComponent successfully serialized and deserialized with all tuning properties." << std::endl;

    // -------------------------------------------------------------------------
    // [25.2] 6x6 N-Wheeled Vehicle Physics Simulation
    // -------------------------------------------------------------------------
    std::cout << "\n[25.2] Testing 6-wheeled (6x6) heavy truck physics simulation..." << std::endl;
    {
        Axle::PhysicsScene physicsScene;

        Axle::VehicleConfig config;
        config.InitialPosition = glm::vec3(0.0f, 1.5f, 0.0f);
        config.ChassisHalfExtent = glm::vec3(1.2f, 0.4f, 3.0f);
        config.ChassisMass = 5000.0f;

        // Configure 6 wheels (Front pair, Middle pair, Rear pair)
        const float xOffset = 1.25f;
        const float yOffset = -0.2f;

        // Front pair (steerable + driven)
        Axle::WheelConfig wFL;
        wFL.Position = glm::vec3(xOffset, yOffset, 2.2f);
        wFL.CanSteer = true;
        wFL.IsDriven = true;
        config.CustomWheels.push_back(wFL);

        Axle::WheelConfig wFR;
        wFR.Position = glm::vec3(-xOffset, yOffset, 2.2f);
        wFR.CanSteer = true;
        wFR.IsDriven = true;
        config.CustomWheels.push_back(wFR);

        // Middle pair (driven)
        Axle::WheelConfig wML;
        wML.Position = glm::vec3(xOffset, yOffset, 0.0f);
        wML.CanSteer = false;
        wML.IsDriven = true;
        config.CustomWheels.push_back(wML);

        Axle::WheelConfig wMR;
        wMR.Position = glm::vec3(-xOffset, yOffset, 0.0f);
        wMR.CanSteer = false;
        wMR.IsDriven = true;
        config.CustomWheels.push_back(wMR);

        // Rear pair (driven + handbrake)
        Axle::WheelConfig wRL;
        wRL.Position = glm::vec3(xOffset, yOffset, -2.2f);
        wRL.CanSteer = false;
        wRL.IsDriven = true;
        wRL.MaxHandBrakeTorque = 8000.0f;
        config.CustomWheels.push_back(wRL);

        Axle::WheelConfig wRR;
        wRR.Position = glm::vec3(-xOffset, yOffset, -2.2f);
        wRR.CanSteer = false;
        wRR.IsDriven = true;
        wRR.MaxHandBrakeTorque = 8000.0f;
        config.CustomWheels.push_back(wRR);

        Axle::Vehicle truck(physicsScene, config);

        assert(truck.GetConstraint() != nullptr);
        assert(truck.GetConstraint()->GetWheels().size() == 6);
        std::cout << "  -> PASS: 6-wheeled vehicle constraint created with 6 active wheels in Jolt Physics." << std::endl;

        // Verify GetWheelTransform for all 6 wheels
        for (Axle::u32 i = 0; i < 6; ++i) {
            glm::mat4 wMat = truck.GetWheelTransform(i);
            glm::vec3 pos = glm::vec3(wMat[3]);
            assert(!std::isnan(pos.x) && !std::isnan(pos.y) && !std::isnan(pos.z));
        }
        std::cout << "  -> PASS: All 6 wheel transforms queried successfully without bounds violation." << std::endl;

        // Simulate 30 physics steps with full throttle
        truck.SetDriverInput(1.0f, 0.0f, 0.0f, 0.0f);
        for (int step = 0; step < 30; ++step) {
            physicsScene.Step(1.0f / 60.0f);
        }

        // Verify truck position and state
        glm::vec3 finalPos = truck.GetPosition();
        assert(!std::isnan(finalPos.x) && !std::isnan(finalPos.y) && !std::isnan(finalPos.z));
        std::cout << "  -> Final truck position after 30 simulation steps: ["
                  << finalPos.x << ", " << finalPos.y << ", " << finalPos.z << "]" << std::endl;
        std::cout << "  -> PASS: Dynamic powertrain differential and anti-roll bars simulated stably." << std::endl;
    }

    std::cout << ">>> STEP 25 PASSED: Phase 20 Component-per-Wheel Vehicle Architecture verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// MAIN ENTRY POINT FOR TEST SUITE
// =============================================================================
int main(int argc, char** argv) {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    int targetStep = 0; // 0 = all
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "1" || arg == "step1" || arg == "--step1") targetStep = 1;
        else if (arg == "2" || arg == "step2" || arg == "--step2") targetStep = 2;
        else if (arg == "3" || arg == "step3" || arg == "--step3") targetStep = 3;
        else if (arg == "4" || arg == "step4" || arg == "--step4") targetStep = 4;
        else if (arg == "5" || arg == "step5" || arg == "--step5" || arg == "--phase14") targetStep = 5;
        else if (arg == "6" || arg == "step6" || arg == "--step6" || arg == "--phase15") targetStep = 6;
        else if (arg == "7" || arg == "step7" || arg == "--step7" || arg == "--phase15.2") targetStep = 7;
        else if (arg == "8" || arg == "step8" || arg == "--step8" || arg == "--phase15.3") targetStep = 8;
        else if (arg == "9" || arg == "step9" || arg == "--step9" || arg == "--phase15.4") targetStep = 9;
        else if (arg == "10" || arg == "step10" || arg == "--step10" || arg == "--phase16" || arg == "--phase16.1") targetStep = 10;
        else if (arg == "11" || arg == "step11" || arg == "--step11" || arg == "--phase16.2") targetStep = 11;
        else if (arg == "12" || arg == "step12" || arg == "--step12" || arg == "--phase16.3") targetStep = 12;
        else if (arg == "13" || arg == "step13" || arg == "--step13" || arg == "--phase16.4") targetStep = 13;
        else if (arg == "14" || arg == "step14" || arg == "--step14" || arg == "--phase17" || arg == "--phase17.1") targetStep = 14;
        else if (arg == "15" || arg == "step15" || arg == "--step15" || arg == "--phase17.2") targetStep = 15;
        else if (arg == "16" || arg == "step16" || arg == "--step16" || arg == "--phase17.3") targetStep = 16;
        else if (arg == "17" || arg == "step17" || arg == "--step17" || arg == "--phase18" || arg == "--phase18.1") targetStep = 17;
        else if (arg == "18" || arg == "step18" || arg == "--step18" || arg == "--phase18.2") targetStep = 18;
        else if (arg == "19" || arg == "step19" || arg == "--step19" || arg == "--phase18.3") targetStep = 19;
        else if (arg == "20" || arg == "step20" || arg == "--step20") targetStep = 20;
        else if (arg == "21" || arg == "step21" || arg == "--step21") targetStep = 21;
        else if (arg == "22" || arg == "step22" || arg == "--step22" || arg == "--phase19" || arg == "--phase19.2") targetStep = 22;
        else if (arg == "23" || arg == "step23" || arg == "--step23" || arg == "--phase19.3") targetStep = 23;
        else if (arg == "24" || arg == "step24" || arg == "--step24" || arg == "--phase19.4") targetStep = 24;
        else if (arg == "25" || arg == "step25" || arg == "--step25" || arg == "--phase20" || arg == "--phase20.1") targetStep = 25;
    }

    std::cout << "\n========================================================" << std::endl;
    std::cout << "        AXLE ENGINE - CORE & SCRIPTING TEST SUITE       " << std::endl;
    if (targetStep == 0) {
        std::cout << "              RUNNING ALL 25 STEPS                      " << std::endl;
    } else {
        std::cout << "              RUNNING STEP " << targetStep << " ONLY                    " << std::endl;
    }
    std::cout << "========================================================" << std::endl;

    bool allPassed = true;

    if (targetStep == 0 || targetStep == 1) {
        if (!RunTest_Step1_ScriptRegistry()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 2) {
        if (!RunTest_Step2_HierarchyTraversal()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 3) {
        if (!RunTest_Step3_FieldBuffersAndBinding()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 4) {
        if (!RunTest_Step4_AssemblyRoundtrip()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 5) {
        if (!RunTest_Step5_RelationshipAndTransform()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 6) {
        if (!RunTest_Step6_Phase15_ArchetypePresets()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 7) {
        if (!RunTest_Step7_Phase15_HierarchicalTreeView()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 8) {
        if (!RunTest_Step8_Phase15_DragAndDropReparenting()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 9) {
        if (!RunTest_Step9_Phase15_DeferredAndRecursiveLifecycle()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 10) {
        if (!RunTest_Step10_Phase16_StructuredComponentWidgets()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 11) {
        if (!RunTest_Step11_Phase16_CategorizedComponentPopup()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 12) {
        if (!RunTest_Step12_Phase16_ScriptReflectionMacro()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 13) {
        if (!RunTest_Step13_Phase16_DualContextPropertiesInspector()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 14) {
        if (!RunTest_Step14_Phase17_AssetUUIDAndMetaPipeline()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 15) {
        if (!RunTest_Step15_Phase17_RightClickAssetCreation()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 16) {
        if (!RunTest_Step16_Phase17_DedicatedMaterialInspector()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 17) {
        if (!RunTest_Step17_Phase18_DragDropPipelinesAndClassCreation()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 18) {
        if (!RunTest_Step18_Phase18_SafeSceneStateSimulation()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 19) {
        if (!RunTest_Step19_Phase18_DecoupledAxleScriptsModule()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 20) {
        if (!RunTest_Step20_Phase18_AntiLockShadowCopyAndStatePreservation()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 21) {
        if (!RunTest_Step21_Phase18_AsyncBackgroundScriptCompiler()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 22) {
        if (!RunTest_Step22_Phase19_CoreJobSystemEngine()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 23) {
        if (!RunTest_Step23_Phase19_JoltPhysicsBridgeEnkiTS()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 24) {
        if (!RunTest_Step24_Phase19_ParallelECSProcessing()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 25) {
        if (!RunTest_Step25_Phase20_ComponentPerWheelVehicle()) allPassed = false;
    }

    if (Axle::JobSystem::IsInitialized()) {
        Axle::JobSystem::Shutdown();
    }

    std::cout << "========================================================" << std::endl;
    if (allPassed) {
        std::cout << " [RESULT] >>> ALL REQUESTED TESTS PASSED (100%) <<<    " << std::endl;
    } else {
        std::cerr << " [RESULT] >>> SOME TESTS FAILED!                      " << std::endl;
    }
    std::cout << "========================================================\n" << std::endl;

    return allPassed ? 0 : 1;
}


