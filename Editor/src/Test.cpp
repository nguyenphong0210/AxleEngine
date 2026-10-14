#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Scene.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include "CarPlayerScript.hpp"
#include <iostream>
#include <string>
#include <filesystem>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

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
// MAIN ENTRY POINT FOR TEST SUITE
// =============================================================================
int main(int argc, char** argv) {
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
    }

    std::cout << "\n========================================================" << std::endl;
    std::cout << "        AXLE ENGINE - CORE & SCRIPTING TEST SUITE       " << std::endl;
    if (targetStep == 0) {
        std::cout << "              RUNNING ALL 8 STEPS                       " << std::endl;
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

    std::cout << "========================================================" << std::endl;
    if (allPassed) {
        std::cout << " [RESULT] >>> ALL REQUESTED TESTS PASSED (100%) <<<    " << std::endl;
    } else {
        std::cerr << " [RESULT] >>> SOME TESTS FAILED!                      " << std::endl;
    }
    std::cout << "========================================================\n" << std::endl;

    return allPassed ? 0 : 1;
}


