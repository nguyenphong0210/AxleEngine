#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Scene.hpp>
#include <Axle/AssemblySerializer.hpp>
#include "CarPlayerScript.hpp"
#include <iostream>
#include <string>
#include <filesystem>

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
    }

    std::cout << "\n========================================================" << std::endl;
    std::cout << "          AXLE ENGINE - PHASE 13 TEST SUITE             " << std::endl;
    if (targetStep == 0) {
        std::cout << "              RUNNING ALL 4 STEPS                      " << std::endl;
    } else {
        std::cout << "              RUNNING STEP " << targetStep << " ONLY                   " << std::endl;
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

    std::cout << "========================================================" << std::endl;
    if (allPassed) {
        std::cout << " [RESULT] >>> ALL REQUESTED TESTS PASSED (100%) <<<    " << std::endl;
    } else {
        std::cerr << " [RESULT] >>> SOME TESTS FAILED!                      " << std::endl;
    }
    std::cout << "========================================================\n" << std::endl;

    return allPassed ? 0 : 1;
}
