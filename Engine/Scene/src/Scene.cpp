#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/RenderCommand.hpp>
#include <algorithm>

namespace Axle {

    Scene::Scene() {
    }

    Entity Scene::CreateEntity(const std::string& name) {
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        if (entity.HasComponent<NativeScriptComponent>()) {
            auto& nsc = entity.GetComponent<NativeScriptComponent>();
            if (nsc.Instance && nsc.DestroyScript) {
                nsc.Instance->OnDestroy();
                nsc.DestroyScript(&nsc);
            }
        }

        if (entity.HasComponent<HierarchyComponent>()) {
            auto& hc = entity.GetComponent<HierarchyComponent>();
            auto children = hc.Children; // Copy list to avoid iterator invalidation
            for (auto child : children) {
                if (m_Registry.valid(child)) {
                    DestroyEntity({ child, this });
                }
            }
            if (hc.Parent != entt::null && m_Registry.valid(hc.Parent)) {
                if (m_Registry.all_of<HierarchyComponent>(hc.Parent)) {
                    auto& parentHc = m_Registry.get<HierarchyComponent>(hc.Parent);
                    auto it = std::remove(parentHc.Children.begin(), parentHc.Children.end(), static_cast<entt::entity>(entity));
                    parentHc.Children.erase(it, parentHc.Children.end());
                }
            }
        }
        m_Registry.destroy(entity);
    }

    Entity Scene::FindEntityByBodyID(BodyID bodyID) {
        if (bodyID.IsInvalid()) return {};

        auto view = m_Registry.view<RigidBodyComponent>();
        for (auto entityID : view) {
            const auto& rb = view.get<RigidBodyComponent>(entityID);
            if (rb.RuntimeBodyID == bodyID) {
                return { entityID, this };
            }
        }
        return {};
    }

    glm::mat4 Scene::GetWorldTransform(entt::entity entityID) const {
        if (!m_Registry.valid(entityID) || !m_Registry.all_of<TransformComponent>(entityID)) {
            return glm::mat4(1.0f);
        }

        glm::mat4 transform = m_Registry.get<TransformComponent>(entityID).GetTransform();
        if (m_Registry.all_of<HierarchyComponent>(entityID)) {
            const auto& hc = m_Registry.get<HierarchyComponent>(entityID);
            if (hc.InheritTransform && hc.Parent != entt::null && m_Registry.valid(hc.Parent) && m_Registry.all_of<TransformComponent>(hc.Parent)) {
                transform = GetWorldTransform(hc.Parent) * transform;
            }
        }
        return transform;
    }

    void Scene::OnUpdate(Timestep ts, PhysicsScene* physicsScene, const PerspectiveCamera& camera, Shader& shader) {
        (void)ts;

        // 1. Synchronize physics simulation transforms into TransformComponents
        if (physicsScene) {
            auto physicsView = m_Registry.view<RigidBodyComponent, TransformComponent>();
            for (auto entity : physicsView) {
                auto& tc = m_Registry.get<TransformComponent>(entity);
                const auto& rb = m_Registry.get<RigidBodyComponent>(entity);
                if (!rb.RuntimeBodyID.IsInvalid()) {
                    tc.Translation = physicsScene->GetBodyPosition(rb.RuntimeBodyID);
                    tc.Rotation = physicsScene->GetBodyRotation(rb.RuntimeBodyID);
                }
            }
        }

        // 2. Render all entities with MeshComponent and TransformComponent (with hierarchy propagation)
        shader.Bind();
        shader.SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
        shader.SetFloat3("u_LightDir", glm::vec3(0.45f, 1.0f, 0.65f));

        auto renderView = m_Registry.view<MeshComponent, TransformComponent>();
        for (auto entity : renderView) {
            auto& mesh = m_Registry.get<MeshComponent>(entity);
            if (mesh.VAO) {
                glm::mat4 worldTransform = GetWorldTransform(entity);
                shader.SetMat4("u_Transform", worldTransform);
                shader.SetInt("u_PatternMode", mesh.PatternMode);
                shader.SetFloat("u_TileScale", mesh.TileScale);
                shader.SetFloat4("u_Color1", mesh.Color1);
                shader.SetFloat4("u_Color2", mesh.Color2);
                RenderCommand::DrawIndexed(mesh.VAO);
            }
        }
    }

    void Scene::OnRuntimeStart() {
        auto view = m_Registry.view<NativeScriptComponent>();
        for (auto entity : view) {
            auto& nsc = view.get<NativeScriptComponent>(entity);
            if (!nsc.Instance && nsc.InstantiateScript) {
                nsc.Instance = nsc.InstantiateScript();
                nsc.Instance->m_Entity = Entity{ entity, this };
                nsc.Instance->OnCreate();
            }
        }
    }

    void Scene::OnUpdateRuntime(Timestep ts) {
        auto view = m_Registry.view<NativeScriptComponent>();
        for (auto entity : view) {
            auto& nsc = view.get<NativeScriptComponent>(entity);
            if (!nsc.Instance && nsc.InstantiateScript) {
                nsc.Instance = nsc.InstantiateScript();
                nsc.Instance->m_Entity = Entity{ entity, this };
                nsc.Instance->OnCreate();
            }

            if (nsc.Instance) {
                nsc.Instance->OnUpdate(ts);
            }
        }
    }

    void Scene::OnRuntimeStop() {
        auto view = m_Registry.view<NativeScriptComponent>();
        for (auto entity : view) {
            auto& nsc = view.get<NativeScriptComponent>(entity);
            if (nsc.Instance && nsc.DestroyScript) {
                nsc.Instance->OnDestroy();
                nsc.DestroyScript(&nsc);
            }
        }
    }

}

