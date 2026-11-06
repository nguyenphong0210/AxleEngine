#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/Shader.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/UI/UIDataContext.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace Axle {

    class Entity;
    class SkinningPipeline;
    class ParticleRenderer;
    class RHIParticleRenderer;
    class MaterialTextureCache;
    namespace RHI { class RHICommandBuffer; }
    struct DirectionalLightComponent;
    struct SkyboxComponent;

    class Scene : public SceneData {
    public:
        Scene();
        virtual ~Scene() override;

        // Assembly instantiation (with physics body creation)
        Entity InstantiateAssembly(const std::filesystem::path& filepath,
                                  PhysicsScene* physicsScene = nullptr,
                                  const glm::vec3& spawnPosition = glm::vec3(0.0f),
                                  const struct SceneSerializerContext* context = nullptr);

        // Entity destruction with optional PhysicsScene cleanup
        void DestroyEntity(Entity entity, PhysicsScene* physicsScene);
        void DestroyEntity(Entity entity) override;

        // Entity duplication with optional PhysicsScene creation
        Entity DuplicateEntity(Entity entity, PhysicsScene* physicsScene);
        Entity DuplicateEntity(Entity entity) override;

        virtual void Clear() override;

        // Physics body query and restoration (Physics Bridge)
        Entity FindEntityByBodyID(BodyID bodyID);
        void RestorePhysicsBodies(PhysicsScene* physicsScene);
        void SynchronizePhysicsTransforms(PhysicsScene* physicsScene);

        // Deep copy entire scene (Phase 18.2)
        static std::shared_ptr<Scene> Copy(std::shared_ptr<Scene> other);

        // Primary lighting / skybox / camera queries
        bool GetPrimaryDirectionalLight(DirectionalLightComponent& outLight) const;
        bool GetPrimarySkybox(SkyboxComponent& outSkybox) const;
        Entity GetPrimaryCameraEntity();

        // Render Bridge forwarding
        void RenderGeometryPass(const PerspectiveCamera& camera, Shader& shader,
                                const glm::mat4& lightSpaceMatrix = glm::mat4(1.0f),
                                const glm::vec3& lightDir = glm::vec3(0.45f, 1.0f, 0.65f),
                                int shadowMapSlot = 1,
                                const std::vector<glm::mat4>& cascadeMatrices = {},
                                const std::vector<float>& cascadeSplitDistances = {});

        void OnUpdate(Timestep ts, PhysicsScene* physicsScene, const PerspectiveCamera& camera, Shader& shader,
                      const glm::mat4& lightSpaceMatrix = glm::mat4(1.0f),
                      const glm::vec3& lightDir = glm::vec3(0.45f, 1.0f, 0.65f),
                      int shadowMapSlot = 1);

        void RenderShadowPass(Shader& shadowShader, const glm::mat4& lightSpaceMatrix);

        void RenderSkeletalMeshes(SkinningPipeline& skinningPipeline, const PerspectiveCamera& camera,
                                 const glm::mat4& lightSpaceMatrix = glm::mat4(1.0f),
                                 const glm::vec3& lightDir = glm::vec3(0.45f, 1.0f, 0.65f),
                                 bool shadowPass = false);

        void RenderParticles(ParticleRenderer& particleRenderer, const PerspectiveCamera& camera,
                             const std::shared_ptr<class Framebuffer>& targetFBO = nullptr);
        void RenderParticles(RHIParticleRenderer& particleRenderer, const PerspectiveCamera& camera,
                             RHI::RHICommandBuffer& cmd, MaterialTextureCache* textures = nullptr);

        // Simulation runtime lifecycle
        void OnRuntimeStart(PhysicsScene* physicsScene = nullptr);
        void OnUpdateRuntime(Timestep ts, PhysicsScene* physicsScene = nullptr);
        void OnImGuiRender();
        void OnRuntimeStop(PhysicsScene* physicsScene = nullptr);

        PhysicsScene* GetPhysicsScene() const { return m_RuntimePhysicsScene; }

        // Script reload hooks (Script Bridge)
        void OnPreScriptReload();
        void OnPostScriptReload(bool isRuntimeActive = false);

        UIDataContext& GetUIDataContext() { return m_UIDataContext; }
        const UIDataContext& GetUIDataContext() const { return m_UIDataContext; }

    private:
        PhysicsScene* m_RuntimePhysicsScene = nullptr;
        UIDataContext m_UIDataContext;

        friend class Entity;
    };

}
