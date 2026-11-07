#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/DataComponents.hpp>
#include <Axle/ParallelECS.hpp>
#include <Axle/SceneCommandBuffer.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <Axle/Log.hpp>

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
        if (!CanMutate()) return {};
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<HierarchyComponent>();
        return entity;
    }

    void SceneData::Clear() {
        if (!CanMutate()) return;
        m_Lifetime->Epoch.fetch_add(1);
        if (m_CommandBuffer) {
            m_CommandBuffer->Clear();
        }
        m_SceneGeneration++;
        m_TransformRevision++;

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
        if (!CanMutate() || !entity || entity.GetSceneData() != this || !m_Destroying.insert(entity).second) return;

        entt::entity entityID = (entt::entity)entity;

        // 1. Recursive child destruction: make a safe copy of children list before recursion
        if (entity.HasComponent<HierarchyComponent>()) {
            auto childrenCopy = entity.GetComponent<HierarchyComponent>().Children;
            for (auto childID : childrenCopy) {
                if (m_Registry.valid(childID) && m_Registry.all_of<HierarchyComponent>(childID) && m_Registry.get<HierarchyComponent>(childID).Parent == entityID) {
                    DestroyEntity(Entity{ childID, this });
                }
            }
        }

        DestroyEntityData(entity);
        m_Destroying.erase(entityID);
    }

    void SceneData::DestroyEntityData(Entity entity) {
        const auto entityID = static_cast<entt::entity>(entity);

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

        m_Registry.destroy(entityID);
        m_SceneGeneration++;
        m_TransformRevision++;
    }

    Entity SceneData::DuplicateEntity(Entity entity) {
        if (!CanMutate() || !entity || entity.GetSceneData() != this) return {};

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
        if (!CanMutate()) return GetWorldTransform(entityID);
        // A single-entity query/reparent must not evaluate and enqueue the whole
        // scene. Publish only its ancestor path; phase boundaries publish the
        // complete forest after a batch of authored edits.
        std::vector<entt::entity> path;
        std::unordered_set<entt::entity> visited;
        auto current = entityID;
        while (m_Registry.valid(current) && m_Registry.all_of<TransformComponent>(current)) {
            path.push_back(current);
            visited.insert(current);
            if (!m_Registry.all_of<HierarchyComponent>(current)) break;
            auto& hierarchy = m_Registry.get<HierarchyComponent>(current);
            const auto parent = hierarchy.Parent;
            if (parent == entt::null) break;
            if (!m_Registry.valid(parent) || !m_Registry.all_of<TransformComponent>(parent) || visited.count(parent)) {
                hierarchy.Parent = entt::null;
                break;
            }
            current = parent;
        }
        glm::mat4 world(1.0f);
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            if (m_Registry.all_of<HierarchyComponent>(*it)) {
                world = InheritedParentTransform(world, m_Registry.get<HierarchyComponent>(*it));
            }
            auto& transform = m_Registry.get<TransformComponent>(*it);
            transform.LocalMatrix = transform.GetLocalTransform();
            transform.WorldMatrix = world * transform.LocalMatrix;
            world = transform.WorldMatrix;
        }
        if (!path.empty()) ++m_TransformRevision;
        return GetWorldTransform(entityID);
    }

    void SceneData::UpdateWorldTransforms() {
        UpdateWorldTransformsParallel(64);
    }

    void SceneData::UpdateWorldTransformsParallel(u32 minBatchSize) {
        if (!CanMutate()) return;
        ParallelECS::UpdateWorldTransforms(m_Registry, minBatchSize);
        m_TransformRevision++;
    }

    void SceneData::SetParent(Entity child, Entity parent, bool keepWorldPosition) {
        if (!CanMutate() || !child || child.GetSceneData() != this) return;

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

        // Keep World Position Stays: cache current world transform before modifying hierarchy
        glm::mat4 currentWorld = glm::mat4(1.0f);
        if (keepWorldPosition) {
            currentWorld = RecomputeWorldTransform(childID);
        }
        auto& childHC = m_Registry.get<HierarchyComponent>(childID);

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
                glm::mat4 invParentWorld = glm::inverse(InheritedParentTransform(parentWorld, m_Registry.get<HierarchyComponent>(childID)));
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

        RecomputeWorldTransform(childID);
    }

    SceneCommandBuffer& SceneData::GetCommandBuffer() {
        if (!m_CommandBuffer) {
            m_CommandBuffer = std::make_unique<SceneCommandBuffer>();
        }
        return *m_CommandBuffer;
    }

    void SceneData::ExecuteCommandBuffer() {
        if (!CanMutate()) return;
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
        if (!CanMutate() || !entity || entity.GetSceneData() != this) return;

        auto& pec = entity.HasComponent<PassiveExtensionComponent>()
            ? entity.GetComponent<PassiveExtensionComponent>()
            : entity.AddComponent<PassiveExtensionComponent>();

        pec.Sections.push_back({ sectionName, lines });
    }

    const std::vector<UnknownComponentSection>* SceneData::GetPassiveSections(Entity entity) const {
        if (!entity || entity.GetSceneData() != this) return nullptr;
        if (!m_Registry.all_of<PassiveExtensionComponent>((entt::entity)entity)) return nullptr;

        return &m_Registry.get<PassiveExtensionComponent>((entt::entity)entity).Sections;
    }

    bool SceneData::HasPassiveSections(Entity entity) const {
        if (!entity || !m_Registry.valid(entity)) return false;
        if (!m_Registry.all_of<PassiveExtensionComponent>((entt::entity)entity)) return false;

        return !m_Registry.get<PassiveExtensionComponent>((entt::entity)entity).Sections.empty();
    }

    bool SceneData::CanMutate() const {
        if (std::this_thread::get_id() == m_OwnerThread && JobSystem::GetActiveTaskDepth() == 0 && m_IterationDepth == 0) return true;
        AXLE_CORE_ERROR("Scene mutation rejected outside an owner-thread boundary; record a scene command instead");
        return false;
    }

    void SceneData::DestroyEntityWithPhysics(Entity entity, PhysicsScene*) {
        DestroyEntity(entity);
    }

}
