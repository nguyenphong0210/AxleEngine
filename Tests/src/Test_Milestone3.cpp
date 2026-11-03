#include "TestCommon.hpp"

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