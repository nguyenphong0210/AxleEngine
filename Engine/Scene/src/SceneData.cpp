#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ParallelECS.hpp>
#include <Axle/SceneCommandBuffer.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>

namespace Axle {

    SceneData::SceneData() {
        m_CommandBuffer = std::make_unique<SceneCommandBuffer>();
    }

    SceneData::~SceneData() {
        Clear();
    }

    Entity SceneData::CreateEntity(const std::string& name) {
        return CreateEntityWithUUID(UUID(), name);
    }

    Entity SceneData::CreateEntityWithUUID(UUID uuid, const std::string& name) {
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<HierarchyComponent>();
        return entity;
    }

    void SceneData::Clear() {
        if (m_CommandBuffer) {
            m_CommandBuffer->Clear();
        }
        m_SceneGeneration++;
        m_TransformRevision++;

        // Clean up any instantiated native scripts
        auto scriptView = m_Registry.view<NativeScriptComponent>();
        for (auto entityID : scriptView) {
            auto& nsc = scriptView.get<NativeScriptComponent>(entityID);
            if (nsc.Instance && nsc.DestroyScript) {
                nsc.Instance->OnDestroy();
                nsc.DestroyScript(&nsc);
                nsc.Instance = nullptr;
            }
        }

        m_Registry.clear();
    }

    Entity SceneData::GetEntityByUUID(UUID uuid) {
        auto view = m_Registry.view<IDComponent>();
        for (auto entity : view) {
            if (view.get<IDComponent>(entity).ID == uuid) {
                return { entity, this };
            }
        }
        return {};
    }

    void SceneData::DestroyEntity(Entity entity) {
        if (!entity || !m_Registry.valid(entity)) return;

        entt::entity entityID = (entt::entity)entity;

        // 1. Recursive child destruction: make a safe copy of children list before recursion
        if (entity.HasComponent<HierarchyComponent>()) {
            auto childrenCopy = entity.GetComponent<HierarchyComponent>().Children;
            for (auto childID : childrenCopy) {
                if (m_Registry.valid(childID)) {
                    DestroyEntity(Entity{ childID, this });
                }
            }
        }

        // 2. Unlink from parent's children vector if parent exists
        if (entity.HasComponent<HierarchyComponent>()) {
            entt::entity parentID = entity.GetComponent<HierarchyComponent>().Parent;
            if (parentID != entt::null && m_Registry.valid(parentID)) {
                if (m_Registry.all_of<HierarchyComponent>(parentID)) {
                    auto& parentHC = m_Registry.get<HierarchyComponent>(parentID);
                    auto it = std::find(parentHC.Children.begin(), parentHC.Children.end(), entityID);
                    if (it != parentHC.Children.end()) {
                        parentHC.Children.erase(it);
                    }
                }
            }
        }

        // 3. Script lifecycle cleanup
        if (entity.HasComponent<NativeScriptComponent>()) {
            auto& nsc = entity.GetComponent<NativeScriptComponent>();
            if (nsc.Instance && nsc.DestroyScript) {
                nsc.Instance->OnDestroy();
                nsc.DestroyScript(&nsc);
                nsc.Instance = nullptr;
            }
        }

        m_Registry.destroy(entityID);
        m_SceneGeneration++;
        m_TransformRevision++;
    }

    Entity SceneData::DuplicateEntity(Entity entity) {
        if (!entity || !m_Registry.valid(entity)) return {};

        std::string name = "Entity";
        if (entity.HasComponent<TagComponent>()) {
            name = entity.GetComponent<TagComponent>().Tag + " (Copy)";
        }

        Entity newEntity = CreateEntity(name);
        entt::entity srcID = (entt::entity)entity;
        entt::entity dstID = (entt::entity)newEntity;

        std::unordered_map<UUID, entt::entity> uuidMap;
        uuidMap[entity.GetUUID()] = dstID;

        ComponentCopyContext ctx{
            m_Registry,
            dstID,
            m_Registry,
            srcID,
            uuidMap
        };

        // Deep copy components via ComponentCodecRegistry
        ComponentCodecRegistry::Get().CopyAllComponents(ctx);

        // Preserve parent link if entity is parented
        if (entity.HasComponent<HierarchyComponent>()) {
            const auto& srcHC = entity.GetComponent<HierarchyComponent>();
            if (srcHC.Parent != entt::null && m_Registry.valid(srcHC.Parent)) {
                Entity parentEntity{ srcHC.Parent, this };
                newEntity.SetParent(parentEntity, false);
                auto& dstHC = newEntity.GetComponent<HierarchyComponent>();
                dstHC.InheritTransform = srcHC.InheritTransform;
                dstHC.InheritScale = srcHC.InheritScale;
            }
        }

        m_SceneGeneration++;
        m_TransformRevision++;
        return newEntity;
    }

    glm::mat4 SceneData::GetWorldTransform(entt::entity entityID) const {
        if (!m_Registry.valid(entityID)) return glm::mat4(1.0f);
        if (!m_Registry.all_of<TransformComponent>(entityID)) return glm::mat4(1.0f);

        return m_Registry.get<TransformComponent>(entityID).WorldMatrix;
    }

    glm::mat4 SceneData::RecomputeWorldTransform(entt::entity entityID) {
        if (!m_Registry.valid(entityID)) return glm::mat4(1.0f);
        if (!m_Registry.all_of<TransformComponent>(entityID)) return glm::mat4(1.0f);

        std::vector<entt::entity> chain;
        entt::entity current = entityID;

        // Traverse upwards towards the root with cycle guard
        while (current != entt::null && m_Registry.valid(current)) {
            chain.push_back(current);
            if (chain.size() > 128) break;

            if (m_Registry.all_of<HierarchyComponent>(current)) {
                current = m_Registry.get<HierarchyComponent>(current).Parent;
            } else {
                break;
            }
        }

        // Evaluate transforms downwards from the root
        glm::mat4 parentWorld = glm::mat4(1.0f);
        for (int i = (int)chain.size() - 1; i >= 0; --i) {
            entt::entity e = chain[i];
            if (!m_Registry.all_of<TransformComponent>(e)) continue;

            auto& tc = m_Registry.get<TransformComponent>(e);
            tc.LocalMatrix = tc.GetTransform();

            if (i == (int)chain.size() - 1 || !m_Registry.all_of<HierarchyComponent>(e)) {
                tc.WorldMatrix = tc.LocalMatrix;
            } else {
                const auto& hc = m_Registry.get<HierarchyComponent>(e);
                if (hc.InheritTransform) {
                    if (hc.InheritScale) {
                        tc.WorldMatrix = parentWorld * tc.LocalMatrix;
                    } else {
                        glm::vec3 parentTrans = glm::vec3(parentWorld[3]);
                        glm::quat parentRot = glm::quat_cast(parentWorld);
                        glm::mat4 rigidParent = glm::translate(glm::mat4(1.0f), parentTrans) * glm::mat4_cast(parentRot);
                        tc.WorldMatrix = rigidParent * tc.LocalMatrix;
                    }
                } else {
                    tc.WorldMatrix = tc.LocalMatrix;
                }
            }
            parentWorld = tc.WorldMatrix;
        }

        m_TransformRevision++;
        return m_Registry.get<TransformComponent>(entityID).WorldMatrix;
    }

    void SceneData::UpdateEntityTransformRecursive(entt::entity entity, const glm::mat4& parentWorld) {
        if (!m_Registry.valid(entity) || !m_Registry.all_of<TransformComponent>(entity)) return;

        auto& tc = m_Registry.get<TransformComponent>(entity);
        tc.LocalMatrix = tc.GetTransform();

        if (m_Registry.all_of<HierarchyComponent>(entity)) {
            const auto& hc = m_Registry.get<HierarchyComponent>(entity);
            if (hc.InheritTransform) {
                if (hc.InheritScale) {
                    tc.WorldMatrix = parentWorld * tc.LocalMatrix;
                } else {
                    glm::vec3 parentTrans = glm::vec3(parentWorld[3]);
                    glm::quat parentRot = glm::quat_cast(parentWorld);
                    glm::mat4 rigidParent = glm::translate(glm::mat4(1.0f), parentTrans) * glm::mat4_cast(parentRot);
                    tc.WorldMatrix = rigidParent * tc.LocalMatrix;
                }
            } else {
                tc.WorldMatrix = tc.LocalMatrix;
            }

            auto childrenCopy = hc.Children;
            for (auto child : childrenCopy) {
                UpdateEntityTransformRecursive(child, tc.WorldMatrix);
            }
        } else {
            tc.WorldMatrix = parentWorld * tc.LocalMatrix;
        }
    }

    void SceneData::UpdateWorldTransforms() {
        UpdateWorldTransformsParallel(64);
    }

    void SceneData::UpdateWorldTransformsParallel(u32 minBatchSize) {
        ParallelECS::UpdateWorldTransforms(m_Registry, minBatchSize);
        m_TransformRevision++;
    }

    void SceneData::SetParent(Entity child, Entity parent, bool keepWorldPosition) {
        if (!child || !m_Registry.valid(child)) return;

        entt::entity childID = (entt::entity)child;
        entt::entity parentID = parent ? (entt::entity)parent : entt::null;

        if (childID == parentID) return;

        // Prevent cross-scene parenting
        if (parent && parent.GetSceneData() != this) return;

        // Prevent parenting to a descendant (cycle detection)
        if (parent && parent.IsChildOf(child)) return;

        if (!m_Registry.all_of<HierarchyComponent>(childID)) {
            m_Registry.emplace<HierarchyComponent>(childID);
        }

        auto& childHC = m_Registry.get<HierarchyComponent>(childID);

        // Keep World Position Stays: cache current world transform before modifying hierarchy
        glm::mat4 currentWorld = glm::mat4(1.0f);
        if (keepWorldPosition) {
            currentWorld = RecomputeWorldTransform(childID);
        }

        // Unlink from old parent if any
        if (childHC.Parent != entt::null && m_Registry.valid(childHC.Parent)) {
            if (m_Registry.all_of<HierarchyComponent>(childHC.Parent)) {
                auto& oldParentHC = m_Registry.get<HierarchyComponent>(childHC.Parent);
                auto it = std::find(oldParentHC.Children.begin(), oldParentHC.Children.end(), childID);
                if (it != oldParentHC.Children.end()) {
                    oldParentHC.Children.erase(it);
                }
            }
        }

        childHC.Parent = parentID;

        // Link into new parent's children vector
        if (parentID != entt::null && m_Registry.valid(parentID)) {
            if (!m_Registry.all_of<HierarchyComponent>(parentID)) {
                m_Registry.emplace<HierarchyComponent>(parentID);
            }
            auto& newParentHC = m_Registry.get<HierarchyComponent>(parentID);
            if (std::find(newParentHC.Children.begin(), newParentHC.Children.end(), childID) == newParentHC.Children.end()) {
                newParentHC.Children.push_back(childID);
            }
        }

        // Apply World Position Stays: compute new local transform from currentWorld and parent's world
        if (keepWorldPosition && m_Registry.all_of<TransformComponent>(childID)) {
            auto& childTC = m_Registry.get<TransformComponent>(childID);
            if (parentID != entt::null && m_Registry.valid(parentID)) {
                glm::mat4 parentWorld = RecomputeWorldTransform(parentID);
                glm::mat4 invParentWorld = glm::inverse(parentWorld);
                glm::mat4 newLocal = invParentWorld * currentWorld;

                glm::vec3 skew;
                glm::vec4 perspective;
                glm::decompose(newLocal, childTC.Scale, childTC.Rotation, childTC.Translation, skew, perspective);
            } else {
                glm::vec3 skew;
                glm::vec4 perspective;
                glm::decompose(currentWorld, childTC.Scale, childTC.Rotation, childTC.Translation, skew, perspective);
            }
            childTC.LocalMatrix = childTC.GetTransform();
        }

        m_TransformRevision++;
    }

    SceneCommandBuffer& SceneData::GetCommandBuffer() {
        if (!m_CommandBuffer) {
            m_CommandBuffer = std::make_unique<SceneCommandBuffer>();
        }
        return *m_CommandBuffer;
    }

    void SceneData::ExecuteCommandBuffer() {
        if (m_CommandBuffer) {
            m_CommandBuffer->Execute(*this);
        }
    }

    std::shared_ptr<SceneData> SceneData::Copy(const std::shared_ptr<SceneData>& other) {
        if (!other) return nullptr;

        std::shared_ptr<SceneData> newScene = std::make_shared<SceneData>();

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

        newScene->UpdateWorldTransforms();
        return newScene;
    }

    void SceneData::StorePassiveSection(Entity entity, const std::string& sectionName, const std::vector<std::string>& lines) {
        if (!entity || !m_Registry.valid(entity)) return;

        auto& pec = entity.HasComponent<PassiveExtensionComponent>()
            ? entity.GetComponent<PassiveExtensionComponent>()
            : entity.AddComponent<PassiveExtensionComponent>();

        pec.Sections.push_back({ sectionName, lines });
    }

    const std::vector<UnknownComponentSection>* SceneData::GetPassiveSections(Entity entity) const {
        if (!entity || !m_Registry.valid(entity)) return nullptr;
        if (!m_Registry.all_of<PassiveExtensionComponent>((entt::entity)entity)) return nullptr;

        return &m_Registry.get<PassiveExtensionComponent>((entt::entity)entity).Sections;
    }

    bool SceneData::HasPassiveSections(Entity entity) const {
        if (!entity || !m_Registry.valid(entity)) return false;
        if (!m_Registry.all_of<PassiveExtensionComponent>((entt::entity)entity)) return false;

        return !m_Registry.get<PassiveExtensionComponent>((entt::entity)entity).Sections.empty();
    }

}
