#include "TestCommon.hpp"

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

    auto primaryCam = scene->GetPrimaryCameraEntity();
    if (!primaryCam || primaryCam != camera) {
        std::cerr << "FAIL: GetPrimaryCameraEntity() did not return the primary camera entity!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: PointLightComponent, CameraComponent, and GetPrimaryCameraEntity validated." << std::endl;

    // Test PerspectiveCamera::SetWorldTransform when camera is rotated 180 degrees (behind vehicle)
    {
        Axle::PerspectiveCamera testCam(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
        // Camera at (0, 5, -8) looking towards target at (0, 2, 0) -> Yaw = 180 deg, Pitch = -20 deg
        glm::vec3 camPos(0.0f, 5.0f, -8.0f);
        glm::quat camRot = glm::quat(glm::vec3(glm::radians(-20.0f), glm::radians(180.0f), 0.0f));
        glm::mat4 worldMat = glm::translate(glm::mat4(1.0f), camPos) * glm::mat4_cast(camRot);
        testCam.SetWorldTransform(worldMat);

        // In view space, world up (0, 1, 0) should have positive Y (upright camera, not inverted)
        glm::vec4 viewUp = testCam.GetViewMatrix() * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        if (viewUp.y <= 0.0f) {
            std::cerr << "FAIL: Camera view matrix is inverted upside-down when rotated behind vehicle! viewUp.y = " << viewUp.y << std::endl;
            return false;
        }

        // Target at (0, 2, 0) in front of camera should have negative Z in view space (OpenGL convention)
        glm::vec4 viewTarget = testCam.GetViewMatrix() * glm::vec4(0.0f, 2.0f, 0.0f, 1.0f);
        if (viewTarget.z >= 0.0f) {
            std::cerr << "FAIL: Target point is not in front of camera in view space! viewTarget.z = " << viewTarget.z << std::endl;
            return false;
        }
        std::cout << "  -> PASS: PerspectiveCamera::SetWorldTransform behind-vehicle orientation validated." << std::endl;
    }

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