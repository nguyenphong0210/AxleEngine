#include <Axle/SceneData.hpp>
#include <Axle/SceneDataDocument.hpp>
#include <Axle/DataComponents.hpp>
#include <Axle/Entity.hpp>
#include <Axle/SceneCommandBuffer.hpp>
#include <Axle/ParallelECS.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <iostream>
#include <thread>

struct EmptyTag {};
int main() {
    Axle::Log::Init();
    Axle::JobSystem::Init(2);
    int failures = 0;
    const auto check = [&](bool value) { if (!value) ++failures; };
    auto scene = std::make_shared<Axle::SceneData>();
    const std::string document = "Scene: Test\nEntities:\n  - Entity: 10\n    TransformComponent:\n      Translation: [ 1 , 2 , 3 ]\n    PhysicsPlugin:\n      Nested:\n        Value: 42\n";
    check(Axle::SceneDataDocument::Deserialize(document, *scene));
    auto entity = scene->GetEntityByUUID(Axle::UUID(10));
    check(entity.GetWorldTransform()[3].x == 1.0f);
    check(entity.GetScene() == nullptr);
    check(Axle::SceneDataDocument::Serialize(*scene).find("        Value: 42") != std::string::npos);
    auto copy = Axle::SceneData::Copy(scene);
    check(copy->GetPassiveSections(copy->GetEntityByUUID(Axle::UUID(10)))->size() == 1);
    Axle::SceneCommandBuffer oldCommands;
    oldCommands.DestroyEntity(entity);
    scene->Clear();
    auto replacement = scene->CreateEntity("Replacement");
    oldCommands.Execute(*scene);
    check(bool(replacement));
    check(!entity);
    check(oldCommands.GetLastRejectedCount() == 1);
    scene->GetCommandBuffer().DestroyEntity(replacement);
    std::thread worker([&] { scene->ExecuteCommandBuffer(); scene->DestroyEntity(replacement); });
    worker.join();
    check(bool(replacement));
    scene->ExecuteCommandBuffer();
    check(!replacement);
    auto root = scene->CreateEntity("Root");
    auto child = scene->CreateEntity("Child");
    child.SetParent(root, false);
    auto otherRoot = scene->CreateEntity("Other");
    otherRoot.GetComponent<Axle::HierarchyComponent>().Children = {child, child};
    scene->UpdateWorldTransformsParallel(1);
    check(otherRoot.GetComponent<Axle::HierarchyComponent>().Children.empty());
    root.AddComponent<EmptyTag>();
    child.AddComponent<EmptyTag>();
    std::atomic<int> count = 0;
    Axle::ParallelECS::ForEach<EmptyTag, const Axle::TransformComponent>(scene->GetRegistry(), [&](entt::entity, const Axle::TransformComponent&) { ++count; }, 1);
    check(count == 2);
    auto previous = root;
    for (int depth = 0; depth < 200; ++depth) {
        auto next = scene->CreateEntity();
        next.GetComponent<Axle::TransformComponent>().Translation.x = 1.0f;
        next.SetParent(previous, false);
        previous = next;
    }
    scene->UpdateWorldTransforms();
    check(previous.GetWorldTransform()[3].x == 200.0f);
    // Targeted recompute reaches deep ancestors without evaluating unrelated
    // trees on every authored reparent (large scene construction stays cheap).
    otherRoot.GetComponent<Axle::TransformComponent>().WorldMatrix[3].x = 123.0f;
    root.GetComponent<Axle::TransformComponent>().Translation.x = 5.0f;
    check(previous.RecomputeWorldTransform()[3].x == 205.0f);
    check(otherRoot.GetWorldTransform()[3].x == 123.0f);
    scene->UpdateWorldTransforms();
    check(previous.GetWorldTransform()[3].x == 205.0f);
    check(otherRoot.GetWorldTransform()[3].x == 0.0f);
    Axle::Entity expired;
    { Axle::SceneData temporary; expired = temporary.CreateEntity(); }
    check(!expired);
    scene.reset();
    copy.reset();
    Axle::JobSystem::Shutdown();
    std::cout << "SCENE_DATA_BOUNDARY failures=" << failures << '\n';
    return failures ? 1 : 0;
}
