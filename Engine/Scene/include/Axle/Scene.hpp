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
    class Vehicle;
    struct DirectionalLightComponent;
    struct SkyboxComponent;

    struct RuntimeVehicleData {
        entt::entity ChassisEntityID = entt::null;
        std::unique_ptr<Vehicle> VehicleInstance;
        std::vector<entt::entity> WheelEntityIDs;
    };

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
        void UpdateWorldTransformsParallel(u32 minBatchSize = 64);

        // Reparenting API with cycle check and World Position Stays logic
        void SetParent(Entity child, Entity parent, bool keepWorldPosition = true);

        // Query active directional sun light in the scene (returns true if light found)
        bool GetPrimaryDirectionalLight(DirectionalLightComponent& outLight) const;

        // Query active atmospheric skybox in the scene (returns true if skybox found)
        bool GetPrimarySkybox(SkyboxComponent& outSkybox) const;

        // Query primary camera entity in the scene (returns invalid Entity if none found)
        Entity GetPrimaryCameraEntity();

        // Synchronize physics simulation transforms into TransformComponents & propagate hierarchy
        void SynchronizePhysicsTransforms(PhysicsScene* physicsScene);

        // Render geometry pass with active lighting, materials & shadow sampling
        void RenderGeometryPass(const PerspectiveCamera& camera, Shader& shader,
                                const glm::mat4& lightSpaceMatrix = glm::mat4(1.0f),
                                const glm::vec3& lightDir = glm::vec3(0.45f, 1.0f, 0.65f),
                                int shadowMapSlot = 1);

        // Update physics synchronization and render all mesh components (legacy/convenience wrapper)
        void OnUpdate(Timestep ts, PhysicsScene* physicsScene, const PerspectiveCamera& camera, Shader& shader,
                      const glm::mat4& lightSpaceMatrix = glm::mat4(1.0f),
                      const glm::vec3& lightDir = glm::vec3(0.45f, 1.0f, 0.65f),
                      int shadowMapSlot = 1);

        // Render shadow depth pass from light's perspective
        void RenderShadowPass(Shader& shadowShader, const glm::mat4& lightSpaceMatrix);

        // Simulation runtime lifecycle & dynamic vehicle assembly (Phase 20.3 Stage 2)
        void OnRuntimeStart(PhysicsScene* physicsScene = nullptr);
        void OnUpdateRuntime(Timestep ts, PhysicsScene* physicsScene = nullptr);
        void OnImGuiRender();
        void OnRuntimeStop(PhysicsScene* physicsScene = nullptr);

        // Dynamic vehicle query API
        Vehicle* GetRuntimeVehicle(entt::entity chassisEntityID) const;
        Vehicle* GetRuntimeVehicle(Entity chassisEntity) const;
        Vehicle* GetPrimaryRuntimeVehicle() const;
        const std::vector<RuntimeVehicleData>& GetRuntimeVehicles() const { return m_RuntimeVehicles; }
        PhysicsScene* GetPhysicsScene() const { return m_RuntimePhysicsScene; }

        // Hot Reload Lifecycle & Field State Preservation
        void OnPreScriptReload();
        void OnPostScriptReload(bool isRuntimeActive = false);

        entt::registry& GetRegistry() { return m_Registry; }
        const entt::registry& GetRegistry() const { return m_Registry; }

    private:
        void UpdateEntityTransformRecursive(entt::entity entity, const glm::mat4& parentWorld);

        entt::registry m_Registry;
        std::vector<RuntimeVehicleData> m_RuntimeVehicles;
        PhysicsScene* m_RuntimePhysicsScene = nullptr;

        friend class Entity;
    };

}
