#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/ScenePhysicsBridge.hpp>
#include <Axle/SceneAudioBridge.hpp>
#include <Axle/SceneScriptBridge.hpp>
#include <Axle/SceneCharacterBridge.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/SceneCommandBuffer.hpp>
#include <Axle/ScriptHotReloadManager.hpp>

namespace Axle {

    Scene::Scene()
        : SceneData() {
        ScriptHotReloadManager::Get().RegisterScene(this);
        RegisterRuntimeComponentCodecs();
        MaterialCache::SetUUIDPathResolver([](UUID id) { return AssetManager::GetAssetPath(id); });
        MaterialCache::SetPathUUIDResolver([](const std::filesystem::path& p) { return AssetManager::GetAssetUUID(p); });
    }

    Scene::~Scene() {
        Clear();
        ScriptHotReloadManager::Get().UnregisterScene(this);
    }

    Entity Scene::InstantiateAssembly(const std::filesystem::path& filepath,
                                      PhysicsScene* physicsScene,
                                      const glm::vec3& spawnPosition,
                                      const SceneSerializerContext* context) {
        return AssemblySerializer::Deserialize(filepath, this, physicsScene, spawnPosition, context);
    }

    void Scene::DestroyEntity(Entity entity) {
        DestroyEntity(entity, m_RuntimePhysicsScene);
    }

    void Scene::DestroyEntity(Entity entity, PhysicsScene* physicsScene) {
        if (!CanMutate() || !entity || entity.GetSceneData() != this || !m_Destroying.insert(entity).second) return;

        entt::entity entityID = (entt::entity)entity;
        PhysicsScene* pScene = physicsScene ? physicsScene : m_RuntimePhysicsScene;

        // 1. Recursive child destruction with physics scene
        if (entity.HasComponent<HierarchyComponent>()) {
            auto childrenCopy = entity.GetComponent<HierarchyComponent>().Children;
            for (auto childID : childrenCopy) {
                if (m_Registry.valid(childID) && m_Registry.all_of<HierarchyComponent>(childID) && m_Registry.get<HierarchyComponent>(childID).Parent == entityID) {
                    DestroyEntity(Entity{ childID, this }, pScene);
                }
            }
        }

        // 2. Concrete subsystem teardown
        ScenePhysicsBridge::TeardownEntityPhysics(*this, entityID, pScene);
        SceneAudioBridge::TeardownEntityAudio(*this, entityID);
        SceneCharacterBridge::TeardownEntityCharacter(*this, entityID);
        SceneScriptBridge::TeardownEntityScript(*this, entityID);

        // 3. Delegate to generic SceneData data destruction
        DestroyEntityData(entity);
        m_Destroying.erase(entityID);
    }

    Entity Scene::DuplicateEntity(Entity entity) {
        return DuplicateEntity(entity, m_RuntimePhysicsScene);
    }

    Entity Scene::DuplicateEntity(Entity entity, PhysicsScene* physicsScene) {
        Entity newEntity = SceneData::DuplicateEntity(entity);
        if (!newEntity) return {};

        PhysicsScene* pScene = physicsScene ? physicsScene : m_RuntimePhysicsScene;
        if (pScene) {
            ScenePhysicsBridge::CreateBodyForEntity(*this, (entt::entity)newEntity, pScene);
        }

        // Recursive duplication for children
        if (entity.HasComponent<HierarchyComponent>()) {
            auto children = entity.GetComponent<HierarchyComponent>().Children;
            for (auto childID : children) {
                if (m_Registry.valid(childID)) {
                    Entity childEntity{ childID, this };
                    Entity duplicatedChild = DuplicateEntity(childEntity, pScene);
                    if (duplicatedChild) {
                        duplicatedChild.SetParent(newEntity, false);
                    }
                }
            }
        }

        return newEntity;
    }

    void Scene::Clear() {
        if (!CanMutate()) return;
        m_RuntimeActive = false;
        if (m_RuntimePhysicsScene) {
            for (auto entity : m_Registry.view<RigidBody3D>()) ScenePhysicsBridge::TeardownEntityPhysics(*this, entity, m_RuntimePhysicsScene);
        }
        SceneScriptBridge::OnRuntimeStop(*this);
        SceneAudioBridge::OnRuntimeStop(*this);
        SceneCharacterBridge::OnRuntimeStop(*this);
        m_RuntimePhysicsScene = nullptr;

        SceneData::Clear();
    }

    Entity Scene::FindEntityByBodyID(BodyID bodyID) {
        return ScenePhysicsBridge::FindEntityByBodyID(*this, bodyID);
    }

    void Scene::RestorePhysicsBodies(PhysicsScene* physicsScene) {
        ScenePhysicsBridge::RestoreBodies(*this, physicsScene ? physicsScene : m_RuntimePhysicsScene);
    }

    void Scene::SynchronizePhysicsTransforms(PhysicsScene* physicsScene) {
        ScenePhysicsBridge::SynchronizeTransforms(*this, physicsScene ? physicsScene : m_RuntimePhysicsScene);
    }

    std::shared_ptr<Scene> Scene::Copy(std::shared_ptr<Scene> other) {
        if (!other) return nullptr;

        std::shared_ptr<Scene> newScene = std::make_shared<Scene>();

        // Map old UUID to new Entity handle
        std::unordered_map<UUID, entt::entity> entMap;

        // Pass 1: Clone all entities preserving exact UUID and Tag
        auto idView = other->m_Registry.view<IDComponent>();
        for (auto srcEntityID : idView) {
            UUID uuid = other->m_Registry.get<IDComponent>(srcEntityID).ID;
            std::string name = "Entity";
            if (other->m_Registry.all_of<TagComponent>(srcEntityID)) {
                name = other->m_Registry.get<TagComponent>(srcEntityID).Tag;
            }

            Entity dstEntity = newScene->CreateEntityWithUUID(uuid, name);
            entMap[uuid] = (entt::entity)dstEntity;
        }

        // Pass 2: Deep copy all components via ComponentCodecRegistry
        for (auto srcEntityID : idView) {
            UUID uuid = other->m_Registry.get<IDComponent>(srcEntityID).ID;
            entt::entity dstEntityID = entMap[uuid];

            ComponentCopyContext ctx{
                newScene->m_Registry,
                dstEntityID,
                other->m_Registry,
                srcEntityID,
                entMap
            };

            ComponentCodecRegistry::Get().CopyAllComponents(ctx);
        }

        // Pass 3: Remap Hierarchy Relationships using UUID mapping
        for (auto srcEntityID : idView) {
            UUID uuid = other->m_Registry.get<IDComponent>(srcEntityID).ID;
            Entity dstEntity{ entMap[uuid], newScene.get() };

            if (other->m_Registry.all_of<HierarchyComponent>(srcEntityID)) {
                const auto& srcHc = other->m_Registry.get<HierarchyComponent>(srcEntityID);
                if (srcHc.Parent != entt::null && other->m_Registry.valid(srcHc.Parent)) {
                    UUID parentUUID = other->m_Registry.get<IDComponent>(srcHc.Parent).ID;
                    auto it = entMap.find(parentUUID);
                    if (it != entMap.end()) {
                        dstEntity.SetParent(Entity{ it->second, newScene.get() }, false);
                        if (dstEntity.HasComponent<HierarchyComponent>()) {
                            dstEntity.GetComponent<HierarchyComponent>().InheritTransform = srcHc.InheritTransform;
                            dstEntity.GetComponent<HierarchyComponent>().InheritScale = srcHc.InheritScale;
                        }
                    }
                }
            }
        }

        newScene->m_UIDataContext = other->m_UIDataContext;
        newScene->UpdateWorldTransforms();
        return newScene;
    }

    bool Scene::GetPrimaryDirectionalLight(DirectionalLightComponent& outLight) const {
        auto view = m_Registry.view<DirectionalLightComponent>();
        for (auto entity : view) {
            outLight = view.get<DirectionalLightComponent>(entity);
            return true;
        }
        return false;
    }

    bool Scene::GetPrimarySkybox(SkyboxComponent& outSkybox) const {
        auto view = m_Registry.view<SkyboxComponent>();
        for (auto entity : view) {
            outSkybox = view.get<SkyboxComponent>(entity);
            return true;
        }
        return false;
    }

    Entity Scene::GetPrimaryCameraEntity() {
        auto view = m_Registry.view<CameraComponent>();
        for (auto entityID : view) {
            const auto& cam = view.get<CameraComponent>(entityID);
            if (cam.Primary) {
                return Entity{ entityID, this };
            }
        }
        for (auto entityID : view) {
            return Entity{ entityID, this };
        }
        return Entity{};
    }

    void Scene::RenderGeometryPass(const PerspectiveCamera& camera, Shader& shader,
                                   const glm::mat4& lightSpaceMatrix, const glm::vec3& lightDir, int shadowMapSlot,
                                   const std::vector<glm::mat4>& cascadeMatrices,
                                   const std::vector<float>& cascadeSplitDistances) {
        SceneRenderBridge::RenderGeometryPass(*this, camera, shader, lightSpaceMatrix, lightDir, shadowMapSlot, cascadeMatrices, cascadeSplitDistances);
    }

    void Scene::OnUpdate(Timestep ts, PhysicsScene* physicsScene, const PerspectiveCamera& camera, Shader& shader,
                         const glm::mat4& lightSpaceMatrix, const glm::vec3& lightDir, int shadowMapSlot) {
        (void)ts;
        SynchronizePhysicsTransforms(physicsScene);
        RenderGeometryPass(camera, shader, lightSpaceMatrix, lightDir, shadowMapSlot);
    }

    void Scene::RenderShadowPass(Shader& shadowShader, const glm::mat4& lightSpaceMatrix) {
        SceneRenderBridge::RenderShadowPass(*this, shadowShader, lightSpaceMatrix);
    }

    void Scene::RenderSkeletalMeshes(SkinningPipeline& skinningPipeline, const PerspectiveCamera& camera,
                                     const glm::mat4& lightSpaceMatrix, const glm::vec3& lightDir, bool shadowPass) {
        SceneRenderBridge::RenderSkeletalMeshes(*this, skinningPipeline, camera, lightSpaceMatrix, lightDir, shadowPass);
    }

    void Scene::RenderParticles(ParticleRenderer& particleRenderer, const PerspectiveCamera& camera,
                                const std::shared_ptr<class Framebuffer>& targetFBO) {
        SceneRenderBridge::RenderParticles(*this, particleRenderer, camera, targetFBO);
    }

    void Scene::RenderParticles(RHIParticleRenderer& particleRenderer, const PerspectiveCamera& camera,
                                RHI::RHICommandBuffer& cmd, MaterialTextureCache* textures) {
        SceneRenderBridge::RenderParticles(*this, particleRenderer, camera, cmd, textures);
    }

    void Scene::OnRuntimeStart(PhysicsScene* physicsScene) {
        if (!CanMutate()) return;
        m_SimulationFaulted = false;
        UpdateWorldTransforms();
        m_RuntimeActive = true;
        m_RuntimePhysicsScene = physicsScene;
        if (physicsScene) {
            for (auto entity : m_Registry.view<RigidBody3D>()) ScenePhysicsBridge::CreateBodyForEntity(*this, entity, physicsScene);
        }
        SceneScriptBridge::OnRuntimeStart(*this);
        SceneCharacterBridge::OnRuntimeStart(*this, physicsScene);
        SceneAudioBridge::OnRuntimeStart(*this);
        OnCommandsApplied();
    }

    void Scene::ExecuteFrameScriptPhase(Timestep dt) {
        if (!CanMutate()) return;
        RecordPhase(SimulationPhase::FrameScripts);
        SceneScriptBridge::OnUpdate(*this, dt);
    }

    void Scene::ExecuteFixedControlsPhase(Timestep fixedDt, PhysicsScene* physicsScene) {
        if (!CanMutate()) return;
        RecordPhase(SimulationPhase::FixedControls);
        auto* ps = physicsScene ? physicsScene : m_RuntimePhysicsScene;
        GetCommandBuffer().Execute(*this);
        SceneScriptBridge::OnFixedUpdate(*this, fixedDt);
        SceneCharacterBridge::OnFixedUpdate(*this, fixedDt, ps);
    }

    void Scene::ExecutePhysicsPhase(Timestep fixedDt, PhysicsScene* physicsScene) {
        if (!CanMutate()) return;
        RecordPhase(SimulationPhase::Physics);
        auto* ps = physicsScene ? physicsScene : m_RuntimePhysicsScene;
        if (ps) {
            ps->Step(fixedDt.GetSeconds());
        }
    }

    void Scene::ExecutePostStepSyncPhase(PhysicsScene* physicsScene) {
        if (!CanMutate()) return;
        RecordPhase(SimulationPhase::PostStepSync);
        auto* ps = physicsScene ? physicsScene : m_RuntimePhysicsScene;
        ScenePhysicsBridge::SynchronizeTransforms(*this, ps);
        UpdateWorldTransformsParallel(64);
        SceneCharacterBridge::OnPostStepSync(*this, ps);
    }

    void Scene::ExecuteAnimationVFXPhase(Timestep fixedDt, PhysicsScene* physicsScene) {
        if (!CanMutate()) return;
        RecordPhase(SimulationPhase::AnimationVFX);
        auto* ps = physicsScene ? physicsScene : m_RuntimePhysicsScene;
        SceneCharacterBridge::UpdateAnimationAndVFX(*this, fixedDt, ps);
        UpdateWorldTransformsParallel(64);
    }

    void Scene::ExecuteLatePresentationPhase(Timestep dt, PhysicsScene* physicsScene) {
        if (!CanMutate()) return;
        RecordPhase(SimulationPhase::LatePresentation);
        auto* ps = physicsScene ? physicsScene : m_RuntimePhysicsScene;
        SceneScriptBridge::OnLateUpdate(*this, dt);
        UpdateWorldTransformsParallel(64);
        SceneAudioBridge::OnUpdate(*this, dt, ps);
    }

    void Scene::ExecuteFixedTick(Timestep fixedDt, PhysicsScene* physicsScene) {
        ExecuteFixedControlsPhase(fixedDt, physicsScene);
        ExecutePhysicsPhase(fixedDt, physicsScene);
        ExecutePostStepSyncPhase(physicsScene);
        ExecuteAnimationVFXPhase(fixedDt, physicsScene);
    }

    void Scene::StepSimulation(Timestep dt, PhysicsScene* physicsScene) {
        if (!CanMutate()) return;
        if (physicsScene) m_RuntimePhysicsScene = physicsScene;
        auto* ps = m_RuntimePhysicsScene;

        ExecuteFrameScriptPhase(dt);
        ExecuteFixedTick(dt, ps);
        ExecuteLatePresentationPhase(dt, ps);
    }

    void Scene::OnUpdateRuntime(Timestep ts, PhysicsScene* physicsScene) {
        if (!CanMutate()) return;
        if (physicsScene) m_RuntimePhysicsScene = physicsScene;
        auto* ps = m_RuntimePhysicsScene;

        ExecuteFrameScriptPhase(ts);
        ExecuteFixedControlsPhase(ts, ps);
        ExecutePostStepSyncPhase(ps);
        ExecuteAnimationVFXPhase(ts, ps);
        ExecuteLatePresentationPhase(ts, ps);
    }

    void Scene::OnImGuiRender() {
        SceneData::IterationScope iteration(*this);
        auto scriptView = m_Registry.view<NativeScriptComponent>();
        for (auto entity : scriptView) {
            auto& nsc = scriptView.get<NativeScriptComponent>(entity);
            if (nsc.Instance) {
                nsc.Instance->OnImGuiRender();
            }
        }
    }

    void Scene::OnRuntimeStop(PhysicsScene* /*physicsScene*/) {
        if (!CanMutate()) return;
        m_RuntimeActive = false;
        SceneAudioBridge::OnRuntimeStop(*this);
        SceneCharacterBridge::OnRuntimeStop(*this);
        SceneScriptBridge::OnRuntimeStop(*this);
        m_RuntimePhysicsScene = nullptr;
    }

    void Scene::OnPreScriptReload() {
        if (!CanMutate()) return;
        GetCommandBuffer().Clear();
        SceneScriptBridge::OnPreScriptReload(*this);
        GetCommandBuffer().Clear();
    }

    void Scene::OnPostScriptReload(bool isRuntimeActive) {
        if (!CanMutate()) return;
        SceneScriptBridge::OnPostScriptReload(*this, isRuntimeActive);
        m_SimulationFaulted = false;
    }

    void Scene::BeforeComponentRemove(entt::entity entity, entt::id_type type) {
        if (type == entt::type_hash<NativeScriptComponent>::value()) SceneScriptBridge::TeardownEntityScript(*this, entity);
        if (type == entt::type_hash<RigidBody3D>::value() || type == entt::type_hash<CollisionShape3D>::value())
            ScenePhysicsBridge::TeardownEntityPhysics(*this, entity, m_RuntimePhysicsScene);
        if (type == entt::type_hash<CharacterControllerComponent>::value() || type == entt::type_hash<ParticleEmitterComponent>::value() || type == entt::type_hash<CharacterVFXComponent>::value())
            SceneCharacterBridge::TeardownEntityCharacter(*this, entity);
        if (type == entt::type_hash<AudioSourceComponent>::value()) SceneAudioBridge::TeardownEntityAudio(*this, entity);
    }

    void Scene::AfterComponentAdd(entt::entity entity, entt::id_type type) {
        if (m_RuntimePhysicsScene && (type == entt::type_hash<RigidBody3D>::value() || type == entt::type_hash<CollisionShape3D>::value()))
            m_PendingPhysicsEntities.insert(entity);
    }

    void Scene::OnCommandsApplied() {
        auto pending = std::move(m_PendingPhysicsEntities);
        m_PendingPhysicsEntities.clear();
        for (auto entity : pending) ScenePhysicsBridge::CreateBodyForEntity(*this, entity, m_RuntimePhysicsScene);
    }

}
