#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/RenderCommand.hpp>

namespace Axle {

    Scene::Scene() {
    }

    Entity Scene::CreateEntity(const std::string& name) {
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        m_Registry.destroy(entity);
    }

    void Scene::OnUpdate(Timestep ts, PhysicsScene* physicsScene, const PerspectiveCamera& camera, Shader& shader) {
        (void)ts;

        // 1. Synchronize physics simulation transforms into TransformComponents
        if (physicsScene) {
            auto physicsView = m_Registry.view<RigidBodyComponent, TransformComponent>();
            for (auto entity : physicsView) {
                auto [rigidBody, transform] = physicsView.get<RigidBodyComponent, TransformComponent>(entity);
                if (!rigidBody.RuntimeBodyID.IsInvalid()) {
                    transform.Translation = physicsScene->GetBodyPosition(rigidBody.RuntimeBodyID);
                    transform.Rotation = physicsScene->GetBodyRotation(rigidBody.RuntimeBodyID);
                }
            }
        }

        // 2. Render all entities with MeshComponent and TransformComponent
        shader.Bind();
        shader.SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
        shader.SetFloat3("u_LightDir", glm::vec3(0.45f, 1.0f, 0.65f));

        auto renderView = m_Registry.view<MeshComponent, TransformComponent>();
        for (auto entity : renderView) {
            auto [mesh, transform] = renderView.get<MeshComponent, TransformComponent>(entity);
            if (mesh.VAO) {
                shader.SetMat4("u_Transform", transform.GetTransform());
                shader.SetInt("u_PatternMode", mesh.PatternMode);
                shader.SetFloat("u_TileScale", mesh.TileScale);
                shader.SetFloat4("u_Color1", mesh.Color1);
                shader.SetFloat4("u_Color2", mesh.Color2);
                RenderCommand::DrawIndexed(mesh.VAO);
            }
        }
    }

}
