#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/Shader.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/ScenePhase.hpp>
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
        void DestroyEntityWithPhysics(Entity entity, PhysicsScene* physicsScene) override { DestroyEntity(entity, physicsScene); }
        Scene* GetRuntimeScene() override { return this; }
        void BeforeComponentRemove(entt::entity entity, entt::id_type type) override;
        void AfterComponentAdd(entt::entity entity, entt::id_type type) override;
        void OnCommandsApplied() override;

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

        // Discrete Simulation Phase Execution (EJ-11)
        void ExecuteFrameScriptPhase(Timestep dt);
        void ExecuteFixedControlsPhase(Timestep fixedDt, PhysicsScene* physicsScene = nullptr);
        void ExecutePhysicsPhase(Timestep fixedDt, PhysicsScene* physicsScene = nullptr);
        void ExecutePostStepSyncPhase(PhysicsScene* physicsScene = nullptr);
        void ExecuteAnimationVFXPhase(Timestep fixedDt, PhysicsScene* physicsScene = nullptr);
        void ExecuteLatePresentationPhase(Timestep dt, PhysicsScene* physicsScene = nullptr);

        // Entire Fixed Tick (Controls -> Physics -> PostStepSync -> AnimationVFX)
        void ExecuteFixedTick(Timestep fixedDt, PhysicsScene* physicsScene = nullptr);

        // Full Simulation Step (FrameScripts -> FixedTick -> LatePresentation)
        void StepSimulation(Timestep dt, PhysicsScene* physicsScene = nullptr);

        // Simulation Phase Tracing & Telemetry
        void EnablePhaseTracing(bool enable) { m_PhaseTracingEnabled = enable; }
        bool IsPhaseTracingEnabled() const { return m_PhaseTracingEnabled; }
        void ClearPhaseTrace() { m_PhaseTrace.clear(); }
        const std::vector<SimulationPhase>& GetPhaseTrace() const { return m_PhaseTrace; }
        void RecordPhase(SimulationPhase phase) {
            if (m_PhaseTracingEnabled) {
                m_PhaseTrace.push_back(phase);
            }
        }

        PhysicsScene* GetPhysicsScene() const { return m_RuntimePhysicsScene; }
        bool IsRuntimeActive() const { return m_RuntimeActive; }
        bool IsSimulationFaulted() const { return m_SimulationFaulted; }
        void StopAfterScriptReloadFailure() { OnRuntimeStop(m_RuntimePhysicsScene); m_SimulationFaulted = true; }
        void StopAfterExtensionFailure() { OnRuntimeStop(m_RuntimePhysicsScene); m_SimulationFaulted = true; }

        // Script reload hooks (Script Bridge)
        void OnPreScriptReload();
        void OnPostScriptReload(bool isRuntimeActive = false);

        UIDataContext& GetUIDataContext() { return m_UIDataContext; }
        const UIDataContext& GetUIDataContext() const { return m_UIDataContext; }

    private:
        bool RunExtensionPhase(ExtensionPhase phase, float dt);
        PhysicsScene* m_RuntimePhysicsScene = nullptr;
        UIDataContext m_UIDataContext;
        bool m_PhaseTracingEnabled = false;
        bool m_RuntimeActive = false;
        bool m_SimulationFaulted = false;
        float m_ExtensionFixedDelta = 0;
        std::unordered_set<entt::entity> m_PendingPhysicsEntities;
        std::vector<SimulationPhase> m_PhaseTrace;

        friend class Entity;
    };

}
