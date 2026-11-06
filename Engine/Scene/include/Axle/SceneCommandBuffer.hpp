#pragma once

#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/Entity.hpp>
#include <Axle/SceneData.hpp>

#include <entt/entt.hpp>
#include <vector>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <tuple>
#include <utility>

namespace Axle {

    class Scene;

    /// High-performance thread-safe command buffer for deferred structural Scene mutations.
    /// Provides generation checking, conflict resolution (e.g. destroy-then-edit rejection),
    /// cross-scene boundary enforcement, and merges executed only on the main thread after
    /// parallel worker batches join.
    class SceneCommandBuffer {
    public:
        SceneCommandBuffer() = default;
        ~SceneCommandBuffer() = default;

        // Move semantics
        SceneCommandBuffer(SceneCommandBuffer&& other) noexcept;
        SceneCommandBuffer& operator=(SceneCommandBuffer&& other) noexcept;

        // Non-copyable
        SceneCommandBuffer(const SceneCommandBuffer&) = delete;
        SceneCommandBuffer& operator=(const SceneCommandBuffer&) = delete;

        // --- Recording APIs (Thread-Safe) ---

        /// Defer destruction of an entity through complete Scene lifecycle
        /// (script OnDestroy, physics body removal, recursive hierarchy teardown).
        void DestroyEntity(Entity entity);

        /// Defer entity reparenting with cycle rejection and optional world transform preservation.
        void SetParent(Entity child, Entity parent, bool keepWorldPosition = true);

        /// Defer entity creation.
        void CreateEntity(const std::string& name = "Entity", const std::function<void(Entity)>& onCreated = {});
        void CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity", const std::function<void(Entity)>& onCreated = {});

        /// Defer component addition / replacement.
        template<typename Component, typename... Args>
        void EmplaceOrReplace(Entity entity, Args&&... args) {
            auto tupleArgs = std::make_tuple(std::forward<Args>(args)...);
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Commands.push_back([entity, tupleArgs = std::move(tupleArgs)](SceneData& scene, ExecutionContext& ctx) mutable {
                if (!ctx.ValidateTarget(scene, entity)) return;
                std::apply([&scene, entity](auto&&... unpacked) {
                    scene.GetRegistry().template emplace_or_replace<Component>(
                        static_cast<entt::entity>(entity),
                        std::forward<decltype(unpacked)>(unpacked)...
                    );
                }, std::move(tupleArgs));
                ctx.ExecutedCount++;
            });
        }

        /// Defer component removal.
        template<typename Component>
        void Remove(Entity entity) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Commands.push_back([entity](SceneData& scene, ExecutionContext& ctx) {
                if (!ctx.ValidateTarget(scene, entity)) return;
                auto handle = static_cast<entt::entity>(entity);
                if (scene.GetRegistry().template all_of<Component>(handle)) {
                    scene.GetRegistry().template remove<Component>(handle);
                }
                ctx.ExecutedCount++;
            });
        }

        /// Defer custom scene mutation callback.
        void Custom(std::function<void(SceneData&)> command);

        // --- Merge & Execution APIs ---

        /// Merge another command buffer's commands into this buffer.
        /// Applied only after all relevant parallel worker batches join.
        void Merge(SceneCommandBuffer&& other);

        /// Execute all buffered commands against the target scene data.
        /// Enforces scene generation, entity generation, and conflict resolution rules.
        void Execute(SceneData& targetScene);
        void Execute(Scene& targetScene);

        // --- Status & Inspection ---

        size_t GetPendingCount() const;
        size_t GetLastExecutedCount() const { return m_LastExecutedCount; }
        size_t GetLastRejectedCount() const { return m_LastRejectedCount; }
        void Clear();

    private:
        struct ExecutionContext {
            std::unordered_set<entt::entity> DestroyedEntities;
            size_t ExecutedCount = 0;
            size_t RejectedCount = 0;

            bool ValidateTarget(SceneData& scene, Entity entity);
        };

        using CommandFn = std::function<void(SceneData&, ExecutionContext&)>;

        mutable std::mutex m_Mutex;
        std::vector<CommandFn> m_Commands;
        size_t m_LastExecutedCount = 0;
        size_t m_LastRejectedCount = 0;
    };

}
