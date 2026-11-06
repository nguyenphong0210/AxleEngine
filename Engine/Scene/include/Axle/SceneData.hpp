#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/ComponentCodec.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Axle {

    class Entity;
    class SceneCommandBuffer;

    class SceneData {
    public:
        SceneData();
        virtual ~SceneData();

        // Entity lifecycle (pure data operations - no physics scene required)
        Entity CreateEntity(const std::string& name = "Entity");
        Entity CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity");
        virtual void DestroyEntity(Entity entity);
        virtual Entity DuplicateEntity(Entity entity);
        virtual void Clear();

        Entity GetEntityByUUID(UUID uuid);

        // Registry queries
        entt::registry& GetRegistry() { return m_Registry; }
        const entt::registry& GetRegistry() const { return m_Registry; }

        // Hierarchical world transform calculation (propagating parent transforms - EJ-10)
        glm::mat4 GetWorldTransform(entt::entity entityID) const;
        glm::mat4 RecomputeWorldTransform(entt::entity entityID);
        void UpdateWorldTransforms();
        void UpdateWorldTransformsParallel(u32 minBatchSize = 64);
        uint64_t GetTransformRevision() const { return m_TransformRevision; }

        // Reparenting API with cycle check and World Position Stays logic
        void SetParent(Entity child, Entity parent, bool keepWorldPosition = true);

        // Scene Command Buffer & Mutation Boundaries (EJ-09)
        SceneCommandBuffer& GetCommandBuffer();
        void ExecuteCommandBuffer();
        u32 GetSceneGeneration() const { return m_SceneGeneration; }

        // Deep copy entire generic scene data with UUID remapping
        static std::shared_ptr<SceneData> Copy(const std::shared_ptr<SceneData>& other);

        // Passive extension storage API (unknown/plugin sections preserved across serialization/duplication)
        void StorePassiveSection(Entity entity, const std::string& sectionName, const std::vector<std::string>& lines);
        const std::vector<UnknownComponentSection>* GetPassiveSections(Entity entity) const;
        bool HasPassiveSections(Entity entity) const;

    protected:
        void UpdateEntityTransformRecursive(entt::entity entity, const glm::mat4& parentWorld);

        entt::registry m_Registry;
        std::unique_ptr<SceneCommandBuffer> m_CommandBuffer;
        u32 m_SceneGeneration = 1;
        uint64_t m_TransformRevision = 1;

        friend class Entity;
    };

}
