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

namespace Axle {

    Scene::Scene()
        : SceneData() {
        MaterialCache::SetUUIDPathResolver([](UUID id) { return AssetManager::GetAssetPath(id); });
        MaterialCache::SetPathUUIDResolver([](const std::filesystem::path& p) { return AssetManager::GetAssetUUID(p); });
    }

    Scene::~Scene() {
        Clear();
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
        if (!entity || !m_Registry.valid(entity)) return;

        entt::entity entityID = (entt::entity)entity;
        PhysicsScene* pScene = physicsScene ? physicsScene : m_RuntimePhysicsScene;

        // 1. Recursive child destruction with physics scene
        if (entity.HasComponent<HierarchyComponent>()) {
            auto childrenCopy = entity.GetComponent<HierarchyComponent>().Children;
            for (auto childID : childrenCopy) {
                if (m_Registry.valid(childID)) {
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
        SceneData::DestroyEntity(entity);
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
        m_RuntimePhysicsScene = physicsScene;
        SceneScriptBridge::OnRuntimeStart(*this);
        SceneCharacterBridge::OnRuntimeStart(*this, physicsScene);
        SceneAudioBridge::OnRuntimeStart(*this);
    }

    void Scene::OnUpdateRuntime(Timestep ts, PhysicsScene* physicsScene) {
        if (physicsScene) m_RuntimePhysicsScene = physicsScene;

        // 1. C++ Native Scripting
        SceneScriptBridge::OnUpdate(*this, ts);

        // 2. Kinematic Character Controllers, Animator & VFX
        SceneCharacterBridge::OnUpdate(*this, ts, m_RuntimePhysicsScene);

        // 3. Propagate scene graph hierarchy transforms
        UpdateWorldTransformsParallel(64);

        // 4. Audio Listener & 3D Spatial Audio Synchronization
        SceneAudioBridge::OnUpdate(*this, ts, m_RuntimePhysicsScene);
    }

    void Scene::OnImGuiRender() {
        auto scriptView = m_Registry.view<NativeScriptComponent>();
        for (auto entity : scriptView) {
            auto& nsc = scriptView.get<NativeScriptComponent>(entity);
            if (nsc.Instance) {
                nsc.Instance->OnImGuiRender();
            }
        }
    }

    void Scene::OnRuntimeStop(PhysicsScene* /*physicsScene*/) {
        SceneAudioBridge::OnRuntimeStop(*this);
        SceneCharacterBridge::OnRuntimeStop(*this);
        SceneScriptBridge::OnRuntimeStop(*this);
        m_RuntimePhysicsScene = nullptr;
    }

    void Scene::OnPreScriptReload() {
        SceneScriptBridge::OnPreScriptReload(*this);
    }

    void Scene::OnPostScriptReload(bool isRuntimeActive) {
        SceneScriptBridge::OnPostScriptReload(*this, isRuntimeActive);
    }

}
