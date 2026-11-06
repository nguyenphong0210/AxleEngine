#include <Axle/SceneData.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/ComponentCodec.hpp>
#include <Axle/JobSystem.hpp>

#include <iostream>
#include <cassert>
#include <string>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " - " << msg << std::endl; \
            return false; \
        } \
    } while(0)

namespace {

    bool TestSceneDataLifecycle() {
        std::cout << "[RUN] TestSceneDataLifecycle..." << std::endl;

        Axle::SceneData scene;
        TEST_ASSERT(scene.GetSceneGeneration() == 1, "Initial generation should be 1");
        TEST_ASSERT(scene.GetTransformRevision() == 1, "Initial transform revision should be 1");

        Axle::Entity e1 = scene.CreateEntity("Entity1");
        TEST_ASSERT(e1, "Entity1 must be valid");
        TEST_ASSERT(e1.GetName() == "Entity1", "Entity1 tag mismatch");
        TEST_ASSERT(e1.HasComponent<Axle::IDComponent>(), "IDComponent missing");
        TEST_ASSERT(e1.HasComponent<Axle::TagComponent>(), "TagComponent missing");
        TEST_ASSERT(e1.HasComponent<Axle::TransformComponent>(), "TransformComponent missing");
        TEST_ASSERT(e1.HasComponent<Axle::HierarchyComponent>(), "HierarchyComponent missing");

        Axle::UUID uuid1 = e1.GetUUID();
        TEST_ASSERT((uint64_t)uuid1 != 0, "UUID must be non-zero");
        TEST_ASSERT(scene.GetEntityByUUID(uuid1) == e1, "GetEntityByUUID lookup must return e1");

        Axle::Entity e2 = scene.CreateEntityWithUUID(Axle::UUID(9999), "Entity2");
        TEST_ASSERT(e2.GetUUID() == Axle::UUID(9999), "UUID mismatch for e2");
        TEST_ASSERT(scene.GetEntityByUUID(Axle::UUID(9999)) == e2, "Lookup for 9999 must succeed");

        uint32_t genBefore = scene.GetSceneGeneration();
        uint64_t revBefore = scene.GetTransformRevision();
        scene.DestroyEntity(e1);
        TEST_ASSERT(!e1, "e1 must be invalid after destruction");
        TEST_ASSERT(scene.GetSceneGeneration() > genBefore, "Scene generation must increase after destroy");
        TEST_ASSERT(scene.GetTransformRevision() > revBefore, "Transform revision must increase after destroy");

        scene.Clear();
        TEST_ASSERT(!scene.GetEntityByUUID(Axle::UUID(9999)), "All entities cleared");

        std::cout << "[PASS] TestSceneDataLifecycle" << std::endl;
        return true;
    }

    bool TestSceneDataHierarchyAndCycleRejection() {
        std::cout << "[RUN] TestSceneDataHierarchyAndCycleRejection..." << std::endl;

        Axle::SceneData scene;
        Axle::Entity root = scene.CreateEntity("Root");
        Axle::Entity child = scene.CreateEntity("Child");
        Axle::Entity grandchild = scene.CreateEntity("Grandchild");

        child.SetParent(root, false);
        grandchild.SetParent(child, false);

        TEST_ASSERT(child.GetParent() == root, "Child parent must be Root");
        TEST_ASSERT(grandchild.GetParent() == child, "Grandchild parent must be Child");
        TEST_ASSERT(grandchild.IsChildOf(root), "Grandchild must be child of Root");
        TEST_ASSERT(child.IsChildOf(root), "Child must be child of Root");
        TEST_ASSERT(!root.IsChildOf(child), "Root must not be child of Child");

        // Attempt illegal cycle: reparenting root to grandchild
        root.SetParent(grandchild, false);
        TEST_ASSERT(root.GetParent() != grandchild, "Cycle parenting must be rejected");
        TEST_ASSERT(!root.GetParent(), "Root should still have no parent");

        // Attempt self-parenting
        root.SetParent(root, false);
        TEST_ASSERT(root.GetParent() != root, "Self-parenting must be rejected");

        // Unparenting
        child.Unparent(false);
        TEST_ASSERT(!child.GetParent(), "Child must be unparented");
        TEST_ASSERT(!grandchild.IsChildOf(root), "Grandchild is no longer child of Root");

        std::cout << "[PASS] TestSceneDataHierarchyAndCycleRejection" << std::endl;
        return true;
    }

    bool TestSceneDataTransformEvaluation() {
        std::cout << "[RUN] TestSceneDataTransformEvaluation..." << std::endl;

        Axle::SceneData scene;
        Axle::Entity parent = scene.CreateEntity("Parent");
        Axle::Entity child = scene.CreateEntity("Child");
        child.SetParent(parent, false);

        parent.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(10.0f, 0.0f, 0.0f);
        child.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(0.0f, 5.0f, 0.0f);

        uint64_t rev0 = scene.GetTransformRevision();
        scene.UpdateWorldTransforms();
        uint64_t rev1 = scene.GetTransformRevision();
        TEST_ASSERT(rev1 > rev0, "Transform revision must increment after UpdateWorldTransforms");

        glm::mat4 childWorld = child.GetWorldTransform();
        glm::vec3 worldPos = glm::vec3(childWorld[3]);
        TEST_ASSERT(std::abs(worldPos.x - 10.0f) < 1e-4f, "Child world X must be 10");
        TEST_ASSERT(std::abs(worldPos.y - 5.0f) < 1e-4f, "Child world Y must be 5");

        // Recompute world transform specifically
        parent.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(20.0f, 0.0f, 0.0f);
        glm::mat4 recomputed = child.RecomputeWorldTransform();
        glm::vec3 recomputedPos = glm::vec3(recomputed[3]);
        TEST_ASSERT(std::abs(recomputedPos.x - 20.0f) < 1e-4f, "Child recomputed X must be 20");
        TEST_ASSERT(scene.GetTransformRevision() > rev1, "Transform revision must increment after RecomputeWorldTransform");

        std::cout << "[PASS] TestSceneDataTransformEvaluation" << std::endl;
        return true;
    }

    bool TestSceneDataComponentCodecsAndCopy() {
        std::cout << "[RUN] TestSceneDataComponentCodecsAndCopy..." << std::endl;

        auto srcScene = std::make_shared<Axle::SceneData>();
        Axle::Entity e = srcScene->CreateEntity("CustomEntity");
        e.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(1.0f, 2.0f, 3.0f);

        // Add DirectionalLightComponent
        auto& dlc = e.AddComponent<Axle::DirectionalLightComponent>();
        dlc.Color = glm::vec3(1.0f, 0.8f, 0.6f);
        dlc.Intensity = 5.0f;

        // Add RigidBody3D & CollisionShape3D
        auto& rb = e.AddComponent<Axle::RigidBody3D>();
        rb.Mass = 10.0f;
        rb.IsDynamic = true;

        auto& cs = e.AddComponent<Axle::CollisionShape3D>();
        cs.ShapeType = Axle::CollisionShapeType::Box;
        cs.HalfExtent = glm::vec3(0.5f);

        // Add NativeScriptComponent
        auto& nsc = e.AddComponent<Axle::NativeScriptComponent>("TestScript");
        nsc.FloatFields["Speed"] = 12.5f;

        // Perform Deep Copy via SceneData::Copy
        auto dstScene = Axle::SceneData::Copy(srcScene);
        TEST_ASSERT(dstScene != nullptr, "Copy must succeed");

        Axle::Entity copied = dstScene->GetEntityByUUID(e.GetUUID());
        TEST_ASSERT(copied, "Copied entity must exist with same UUID");
        TEST_ASSERT(copied.GetName() == "CustomEntity", "Tag must match");

        // Verify TransformComponent
        TEST_ASSERT(copied.HasComponent<Axle::TransformComponent>(), "Must have TransformComponent");
        glm::vec3 t = copied.GetComponent<Axle::TransformComponent>().Translation;
        TEST_ASSERT(std::abs(t.x - 1.0f) < 1e-4f && std::abs(t.y - 2.0f) < 1e-4f && std::abs(t.z - 3.0f) < 1e-4f, "Translation must match");

        // Verify DirectionalLightComponent
        TEST_ASSERT(copied.HasComponent<Axle::DirectionalLightComponent>(), "Must have DirectionalLightComponent");
        TEST_ASSERT(copied.GetComponent<Axle::DirectionalLightComponent>().Intensity == 5.0f, "Light intensity must match");

        // Verify RigidBody3D
        TEST_ASSERT(copied.HasComponent<Axle::RigidBody3D>(), "Must have RigidBody3D");
        TEST_ASSERT(copied.GetComponent<Axle::RigidBody3D>().Mass == 10.0f, "Mass must match");

        // Verify NativeScriptComponent
        TEST_ASSERT(copied.HasComponent<Axle::NativeScriptComponent>(), "Must have NativeScriptComponent");
        const auto& copiedNsc = copied.GetComponent<Axle::NativeScriptComponent>();
        TEST_ASSERT(copiedNsc.ScriptName == "TestScript", "Script name must match");
        TEST_ASSERT(copiedNsc.FloatFields.at("Speed") == 12.5f, "Script float field must match");
        TEST_ASSERT(copiedNsc.Instance == nullptr, "Runtime instance must be null in copied scene");

        std::cout << "[PASS] TestSceneDataComponentCodecsAndCopy" << std::endl;
        return true;
    }

    bool TestSceneDataPassiveExtensionPreservation() {
        std::cout << "[RUN] TestSceneDataPassiveExtensionPreservation..." << std::endl;

        auto srcScene = std::make_shared<Axle::SceneData>();
        Axle::Entity entity = srcScene->CreateEntity("VehicleCarrier");

        // Store arbitrary passive extension sections (e.g. unknown plugin or VehicleComponent)
        std::vector<std::string> lines = {
            "      Mass: 1500.0",
            "      MaxEngineTorque: 500.0",
            "      WheelCount: 4"
        };
        srcScene->StorePassiveSection(entity, "VehicleComponent", lines);
        TEST_ASSERT(srcScene->HasPassiveSections(entity), "Entity must report passive sections");

        const auto* sections = srcScene->GetPassiveSections(entity);
        TEST_ASSERT(sections != nullptr && sections->size() == 1, "Sections count must be 1");
        TEST_ASSERT(sections->at(0).SectionName == "VehicleComponent", "Section name mismatch");

        // 1. Serialize to string
        Axle::SceneSerializer serializer(srcScene.get());
        std::string serialized = serializer.SerializeToString();
        TEST_ASSERT(!serialized.empty(), "Serialized string must not be empty");
        TEST_ASSERT(serialized.find("VehicleComponent:") != std::string::npos, "Serialized output must contain VehicleComponent");
        TEST_ASSERT(serialized.find("Mass: 1500.0") != std::string::npos, "Serialized output must contain Vehicle lines");

        // 2. Deserialize into a clean new SceneData
        auto restoredScene = std::make_shared<Axle::SceneData>();
        Axle::SceneSerializer deserializer(restoredScene.get());
        bool deserialized = deserializer.DeserializeFromString(serialized);
        TEST_ASSERT(deserialized, "Deserialization must succeed");

        Axle::Entity restoredEntity = restoredScene->GetEntityByUUID(entity.GetUUID());
        TEST_ASSERT(restoredEntity, "Restored entity must be valid");
        TEST_ASSERT(restoredScene->HasPassiveSections(restoredEntity), "Restored entity must preserve passive section");

        const auto* restoredSections = restoredScene->GetPassiveSections(restoredEntity);
        TEST_ASSERT(restoredSections != nullptr && restoredSections->size() == 1, "Restored sections count must be 1");
        TEST_ASSERT(restoredSections->at(0).SectionName == "VehicleComponent", "Restored section name must match");
        TEST_ASSERT(restoredSections->at(0).Lines.size() == 3, "Restored lines count must be 3");

        // 3. Deep copy must also preserve passive sections
        auto copiedScene = Axle::SceneData::Copy(restoredScene);
        Axle::Entity copiedEntity = copiedScene->GetEntityByUUID(entity.GetUUID());
        TEST_ASSERT(copiedEntity, "Copied entity must be valid");
        TEST_ASSERT(copiedScene->HasPassiveSections(copiedEntity), "Copied entity must preserve passive section");

        std::cout << "[PASS] TestSceneDataPassiveExtensionPreservation" << std::endl;
        return true;
    }

    bool TestSceneFacadeParity() {
        std::cout << "[RUN] TestSceneFacadeParity..." << std::endl;

        // Verify that Scene correctly inherits SceneData and works interchangeably
        auto scene = std::make_shared<Axle::Scene>();
        Axle::SceneData* sceneData = scene.get();

        Axle::Entity e = sceneData->CreateEntity("FacadeEntity");
        TEST_ASSERT(e.GetScene() == scene.get(), "Entity GetScene must return Scene*");
        TEST_ASSERT(e.GetSceneData() == sceneData, "Entity GetSceneData must return SceneData*");

        e.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(5.0f, 10.0f, 15.0f);
        scene->UpdateWorldTransforms();

        glm::mat4 world = sceneData->GetWorldTransform((entt::entity)e);
        glm::vec3 pos = glm::vec3(world[3]);
        TEST_ASSERT(std::abs(pos.x - 5.0f) < 1e-4f && std::abs(pos.y - 10.0f) < 1e-4f && std::abs(pos.z - 15.0f) < 1e-4f, "Transform must match through SceneData");

        // Scene deep copy
        auto sceneCopy = Axle::Scene::Copy(scene);
        TEST_ASSERT(sceneCopy != nullptr, "Scene::Copy must succeed");
        Axle::Entity copiedEntity = sceneCopy->GetEntityByUUID(e.GetUUID());
        TEST_ASSERT(copiedEntity, "Entity must exist in SceneCopy");
        TEST_ASSERT(copiedEntity.GetName() == "FacadeEntity", "Name must match");

        std::cout << "[PASS] TestSceneFacadeParity" << std::endl;
        return true;
    }

}

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "  Axle SceneData & Bridge Boundaries Unit Tests  " << std::endl;
    std::cout << "=================================================" << std::endl;

    Axle::JobSystem::Init(4);

    bool allPassed = true;
    allPassed &= TestSceneDataLifecycle();
    allPassed &= TestSceneDataHierarchyAndCycleRejection();
    allPassed &= TestSceneDataTransformEvaluation();
    allPassed &= TestSceneDataComponentCodecsAndCopy();
    allPassed &= TestSceneDataPassiveExtensionPreservation();
    allPassed &= TestSceneFacadeParity();

    Axle::JobSystem::Shutdown();

    if (allPassed) {
        std::cout << "=================================================" << std::endl;
        std::cout << "  ALL SCENE DATA UNIT TESTS PASSED (100%)       " << std::endl;
        std::cout << "=================================================" << std::endl;
        return 0;
    } else {
        std::cerr << "SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
