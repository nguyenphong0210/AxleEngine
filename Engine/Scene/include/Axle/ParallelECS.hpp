#pragma once

#include <Axle/Types.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Components.hpp>

#include <entt/entt.hpp>
#include <vector>
#include <functional>
#include <type_traits>
#include <mutex>

namespace Axle {

    /// High-performance multithreaded ECS utilities routing entt::registry batch operations
    /// into Axle::JobSystem worker threads with zero lock contention.
    class ParallelECS {
    public:
        // =====================================================================
        // PARALLEL VIEW ITERATION (ParallelFor on entt::view)
        // =====================================================================

        /// Iterates over all entities and components matching Components... across worker threads.
        /// Supports lambdas with signatures:
        ///   - func(entt::entity entity, Components&... comps, uint32_t threadIndex)
        ///   - func(entt::entity entity, Components&... comps)
        ///   - func(Components&... comps, uint32_t threadIndex)
        ///   - func(Components&... comps)
        ///   - func(entt::entity entity, uint32_t threadIndex)
        ///   - func(entt::entity entity)
        template<typename... Components, typename Func>
        static void ForEach(entt::registry& registry, Func&& func, uint32_t minBatchSize = 1024) {
            auto view = registry.view<Components...>();
            const auto* handle = view.handle();
            if (!handle || handle->empty()) return;
            const size_t entityCount = handle->size();

            // Sequential fallback if JobSystem is not initialized or count is smaller than batch grain
            if (!JobSystem::IsInitialized() || entityCount < minBatchSize || JobSystem::GetWorkerThreadCount() <= 1) {
                for (auto entity : view) {
                    InvokeEntity<Components...>(view, entity, func, 0);
                }
                return;
            }

            // In EnTT, view.handle() returns the leading storage pool containing contiguous entity IDs
            const entt::entity* entities = handle->data();
            const uint32_t totalEntities = static_cast<uint32_t>(entityCount);

            JobSystem::ParallelForRange(totalEntities, [&view, entities, &func](uint32_t start, uint32_t end, uint32_t threadNum) {
                for (uint32_t i = start; i < end; ++i) {
                    entt::entity entity = entities[i];
                    // If multi-component view, ensure entity belongs to all other pools
                    if constexpr (sizeof...(Components) > 1) {
                        if (!view.contains(entity)) {
                            continue;
                        }
                    }
                    InvokeEntity<Components...>(view, entity, func, threadNum);
                }
            }, minBatchSize);
        }

        /// Iterates in parallel over a predetermined collection of entities, passing components and threadNum.
        template<typename... Components, typename Func>
        static void ForEachEntity(entt::registry& registry, const std::vector<entt::entity>& entities, Func&& func, uint32_t minBatchSize = 512) {
            const size_t count = entities.size();
            if (count == 0) return;

            auto view = registry.view<Components...>();

            if (!JobSystem::IsInitialized() || count < minBatchSize || JobSystem::GetWorkerThreadCount() <= 1) {
                for (size_t i = 0; i < count; ++i) {
                    entt::entity entity = entities[i];
                    if (view.contains(entity)) {
                        InvokeEntity<Components...>(view, entity, func, 0);
                    }
                }
                return;
            }

            const uint32_t total = static_cast<uint32_t>(count);
            JobSystem::ParallelForRange(total, [&view, &entities, &func](uint32_t start, uint32_t end, uint32_t threadNum) {
                for (uint32_t i = start; i < end; ++i) {
                    entt::entity entity = entities[i];
                    if (view.contains(entity)) {
                        InvokeEntity<Components...>(view, entity, func, threadNum);
                    }
                }
            }, minBatchSize);
        }

        // =====================================================================
        // PARALLEL TRANSFORM HIERARCHY EVALUATION
        // =====================================================================

        /// Evaluates local and world transforms in parallel without lock contention.
        /// Identifies root entities and distributes disjoint subtrees across worker threads.
        static void UpdateWorldTransforms(entt::registry& registry, uint32_t minBatchSize = 64) {
            auto view = registry.view<TransformComponent>();
            if (view.empty()) return;

            // 1. Collect all root entities (entities without active parents)
            std::vector<entt::entity> rootEntities;
            rootEntities.reserve(view.size());

            for (auto entityID : view) {
                bool isRoot = true;
                if (registry.all_of<HierarchyComponent>(entityID)) {
                    const auto& hc = registry.get<HierarchyComponent>(entityID);
                    if (hc.Parent != entt::null && registry.valid(hc.Parent)) {
                        isRoot = false;
                    }
                }
                if (isRoot) {
                    rootEntities.push_back(entityID);
                }
            }

            const uint32_t rootCount = static_cast<uint32_t>(rootEntities.size());
            if (rootCount == 0) return;

            // 2. Sequential fallback if small or uninitialized
            if (!JobSystem::IsInitialized() || rootCount < minBatchSize || JobSystem::GetWorkerThreadCount() <= 1) {
                for (uint32_t i = 0; i < rootCount; ++i) {
                    UpdateEntitySubtree(registry, rootEntities[i], glm::mat4(1.0f));
                }
                return;
            }

            // 3. Parallel evaluation: Each thread processes its slice of root subtrees (disjoint memory)
            JobSystem::ParallelForRange(rootCount, [&registry, &rootEntities](uint32_t start, uint32_t end, uint32_t threadNum) {
                (void)threadNum;
                for (uint32_t i = start; i < end; ++i) {
                    UpdateEntitySubtree(registry, rootEntities[i], glm::mat4(1.0f));
                }
            }, minBatchSize);
        }

        // =====================================================================
        // PARALLEL COMMAND BUFFER (Thread-Safe Structural Mutations)
        // =====================================================================

        /// Command buffer allowing worker threads to record deferred entity/component mutations
        /// during parallel iterations and commit them on the main thread in a single batch.
        class CommandBuffer {
        public:
            void DestroyEntity(entt::entity entity) {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Commands.push_back([entity](entt::registry& reg) {
                    if (reg.valid(entity)) {
                        reg.destroy(entity);
                    }
                });
            }

            template<typename Component, typename... Args>
            void EmplaceOrReplace(entt::entity entity, Args&&... args) {
                // Capture arguments into deferred lambda
                auto tupleArgs = std::make_tuple(std::forward<Args>(args)...);
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Commands.push_back([entity, tupleArgs = std::move(tupleArgs)](entt::registry& reg) mutable {
                    if (reg.valid(entity)) {
                        std::apply([&reg, entity](auto&&... unpacked) {
                            reg.template emplace_or_replace<Component>(entity, std::forward<decltype(unpacked)>(unpacked)...);
                        }, std::move(tupleArgs));
                    }
                });
            }

            template<typename Component>
            void Remove(entt::entity entity) {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Commands.push_back([entity](entt::registry& reg) {
                    if (reg.valid(entity)) {
                        reg.template remove<Component>(entity);
                    }
                });
            }

            void Custom(std::function<void(entt::registry&)> command) {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Commands.push_back(std::move(command));
            }

            void Execute(entt::registry& registry) {
                std::vector<std::function<void(entt::registry&)>> toExecute;
                {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    toExecute.swap(m_Commands);
                }
                for (auto& cmd : toExecute) {
                    cmd(registry);
                }
            }

            size_t GetPendingCount() const {
                std::lock_guard<std::mutex> lock(m_Mutex);
                return m_Commands.size();
            }

            void Clear() {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Commands.clear();
            }

        private:
            mutable std::mutex m_Mutex;
            std::vector<std::function<void(entt::registry&)>> m_Commands;
        };

    private:
        template<typename...>
        struct always_false : std::false_type {};

        // Helper: Recursive transform update down an entity subtree
        static void UpdateEntitySubtree(entt::registry& registry, entt::entity entityID, const glm::mat4& parentWorld) {
            if (!registry.valid(entityID) || !registry.all_of<TransformComponent>(entityID)) {
                return;
            }

            auto& tc = registry.get<TransformComponent>(entityID);
            tc.LocalMatrix = tc.GetLocalTransform();
            tc.WorldMatrix = parentWorld * tc.LocalMatrix;

            if (registry.all_of<HierarchyComponent>(entityID)) {
                const auto& hc = registry.get<HierarchyComponent>(entityID);
                for (auto child : hc.Children) {
                    if (registry.valid(child)) {
                        glm::mat4 childParentWorld = tc.WorldMatrix;
                        if (registry.all_of<HierarchyComponent>(child)) {
                            const auto& childHc = registry.get<HierarchyComponent>(child);
                            if (!childHc.InheritTransform) {
                                childParentWorld = glm::mat4(1.0f);
                            } else if (!childHc.InheritScale) {
                                glm::vec3 pPos = glm::vec3(tc.WorldMatrix[3]);
                                glm::vec3 col0 = glm::vec3(tc.WorldMatrix[0]);
                                glm::vec3 col1 = glm::vec3(tc.WorldMatrix[1]);
                                glm::vec3 col2 = glm::vec3(tc.WorldMatrix[2]);
                                float l0 = glm::length(col0);
                                float l1 = glm::length(col1);
                                float l2 = glm::length(col2);
                                glm::mat3 rot(
                                    l0 > 1e-5f ? col0 / l0 : glm::vec3(1.0f, 0.0f, 0.0f),
                                    l1 > 1e-5f ? col1 / l1 : glm::vec3(0.0f, 1.0f, 0.0f),
                                    l2 > 1e-5f ? col2 / l2 : glm::vec3(0.0f, 0.0f, 1.0f)
                                );
                                glm::mat4 rigidParent = glm::mat4(rot);
                                rigidParent[3] = glm::vec4(pPos, 1.0f);
                                childParentWorld = rigidParent;
                            }
                        }
                        UpdateEntitySubtree(registry, child, childParentWorld);
                    }
                }
            }
        }

        // Helper: Invokes custom lambda with variable signatures based on SFINAE / is_invocable_v
        template<typename... Components, typename View, typename Func>
        static void InvokeEntity(View& view, entt::entity entity, Func&& func, uint32_t threadNum) {
            using DecayedFunc = std::decay_t<Func>;

            if constexpr (std::is_invocable_v<DecayedFunc, entt::entity, decltype(view.template get<Components>(entity))..., uint32_t>) {
                func(entity, view.template get<Components>(entity)..., threadNum);
            } else if constexpr (std::is_invocable_v<DecayedFunc, entt::entity, decltype(view.template get<Components>(entity))...>) {
                func(entity, view.template get<Components>(entity)...);
            } else if constexpr (std::is_invocable_v<DecayedFunc, decltype(view.template get<Components>(entity))..., uint32_t>) {
                func(view.template get<Components>(entity)..., threadNum);
            } else if constexpr (std::is_invocable_v<DecayedFunc, decltype(view.template get<Components>(entity))...>) {
                func(view.template get<Components>(entity)...);
            } else if constexpr (std::is_invocable_v<DecayedFunc, entt::entity, uint32_t>) {
                func(entity, threadNum);
            } else if constexpr (std::is_invocable_v<DecayedFunc, entt::entity>) {
                func(entity);
            } else {
                static_assert(always_false<DecayedFunc>::value, "Invalid lambda signature passed to ParallelECS::ForEach!");
            }
        }
    };

}
