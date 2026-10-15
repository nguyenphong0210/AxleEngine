#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/Shader.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/PhysicsScene.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <filesystem>

namespace Axle {

    class Entity;
    struct DirectionalLightComponent;
    struct SkyboxComponent;

    class Scene {
    public:
        Scene();
        ~Scene() = default;

        Entity CreateEntity(const std::string& name = "Entity");
        Entity CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity");
        Entity InstantiateAssembly(const std::filesystem::path& filepath,
                                  PhysicsScene* physicsScene = nullptr,
                                  const glm::vec3& spawnPosition = glm::vec3(0.0f),
                                  const struct SceneSerializerContext* context = nullptr);
        void DestroyEntity(Entity entity, PhysicsScene* physicsScene = nullptr);
        Entity DuplicateEntity(Entity entity, PhysicsScene* physicsScene = nullptr);
        void Clear();
        Entity FindEntityByBodyID(BodyID bodyID);
        Entity GetEntityByUUID(UUID uuid);

        // Deep copy entire scene with UUID remapping for Play/Stop simulation (Phase 18.2)
        static std::shared_ptr<Scene> Copy(std::shared_ptr<Scene> other);

        // Zero-drift physics restoration for pristine scene reset
        void RestorePhysicsBodies(PhysicsScene* physicsScene);

        // Hierarchical world transform calculation (propagating parent transforms)
        glm::mat4 GetWorldTransform(entt::entity entityID) const;
        void UpdateWorldTransforms();

        // Reparenting API with cycle check and World Position Stays logic
        void SetParent(Entity child, Entity parent);

        // Query active directional sun light in the scene (returns true if light found)
        bool GetPrimaryDirectionalLight(DirectionalLightComponent& outLight) const;

        // Query active atmospheric skybox in the scene (returns true if skybox found)
        bool GetPrimarySkybox(SkyboxComponent& outSkybox) const;

        // Update physics synchronization and render all mesh components
        void OnUpdate(Timestep ts, PhysicsScene* physicsScene, const PerspectiveCamera& camera, Shader& shader,
                      const glm::mat4& lightSpaceMatrix = glm::mat4(1.0f),
                      const glm::vec3& lightDir = glm::vec3(0.45f, 1.0f, 0.65f),
                      int shadowMapSlot = 1);

        // Render shadow depth pass from light's perspective
        void RenderShadowPass(Shader& shadowShader, const glm::mat4& lightSpaceMatrix);

        // Scripting runtime lifecycle
        void OnRuntimeStart();
        void OnUpdateRuntime(Timestep ts);
        void OnRuntimeStop();

        entt::registry& GetRegistry() { return m_Registry; }
        const entt::registry& GetRegistry() const { return m_Registry; }

    private:
        void UpdateEntityTransformRecursive(entt::entity entity, const glm::mat4& parentWorld);

        entt::registry m_Registry;

        friend class Entity;
    };

}
