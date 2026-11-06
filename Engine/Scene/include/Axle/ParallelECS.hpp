#pragma once

#include <Axle/Types.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Components.hpp>

#include <entt/entt.hpp>
#include <vector>
#include <unordered_set>
#include <functional>
#include <type_traits>
#include <mutex>

namespace Axle {

    /// High-performance multithreaded ECS utilities routing entt::registry batch operations
    /// into Axle::JobSystem worker threads with zero lock contention.
    class ParallelECS {
    public:
        // Helper to query size or size_hint across EnTT storage/multi-component views
        template<typename View>
        static size_t GetViewSizeHint(const View& view) {
            if constexpr (requires { view.size_hint(); }) {
                return static_cast<size_t>(view.size_hint());
            } else if constexpr (requires { view.size(); }) {
                return static_cast<size_t>(view.size());
            } else {
                return 0;
            }
        }

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
            if (view.begin() == view.end()) return;

            const size_t estimatedCount = GetViewSizeHint(view);

            // Sequential fallback if JobSystem is not initialized or count is smaller than batch grain or single-threaded
            if (!JobSystem::IsInitialized() || estimatedCount < minBatchSize || JobSystem::GetTotalThreadCount() <= 1) {
                for (auto entity : view) {
                    InvokeEntity<Components...>(view, entity, func, 0);
                }
                return;
            }

            // In EnTT, iterating `for (auto entity : view)` guarantees:
            // 1. Skips entt::tombstone (in_place_delete / stable storage)
            // 2. Checks view.contains(entity) across multi-component views
            // 3. Produces only valid, active entities in the view
            std::vector<entt::entity> entities;
            entities.reserve(estimatedCount);
            for (auto entity : view) {
                entities.push_back(entity);
            }

            if (entities.empty()) return;

            const uint32_t totalEntities = static_cast<uint32_t>(entities.size());
            JobSystem::ParallelForRange(totalEntities, [&view, &entities, &func](uint32_t start, uint32_t end, uint32_t threadNum) {
                for (uint32_t i = start; i < end; ++i) {
                    entt::entity entity = entities[i];
                    InvokeEntity<Components...>(view, entity, func, threadNum);
                }
            }, minBatchSize);
        }

        /// Iterates in parallel over a predetermined collection of entities, passing components and threadNum.
        template<typename... Components, typename Func>
        static void ForEachEntity(entt::registry& registry, const std::vector<entt::entity>& entities, Func&& func, uint32_t minBatchSize = 512) {
            if (entities.empty()) return;

            auto view = registry.view<Components...>();

            // Deduplicate and filter entities: must be valid in registry, not tombstone, and contained in view
            std::vector<entt::entity> validEntities;
            validEntities.reserve(entities.size());
            std::unordered_set<entt::entity> seen;
            seen.reserve(entities.size());

            for (entt::entity e : entities) {
                if (e != entt::null && e != entt::tombstone && registry.valid(e) && view.contains(e)) {
                    if (seen.insert(e).second) {
                        validEntities.push_back(e);
                    }
                }
            }

            const size_t count = validEntities.size();
            if (count == 0) return;

            if (!JobSystem::IsInitialized() || count < minBatchSize || JobSystem::GetTotalThreadCount() <= 1) {
                for (size_t i = 0; i < count; ++i) {
                    InvokeEntity<Components...>(view, validEntities[i], func, 0);
                }
                return;
            }

            const uint32_t total = static_cast<uint32_t>(count);
            JobSystem::ParallelForRange(total, [&view, &validEntities, &func](uint32_t start, uint32_t end, uint32_t threadNum) {
                for (uint32_t i = start; i < end; ++i) {
                    InvokeEntity<Components...>(view, validEntities[i], func, threadNum);
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
            if (view.begin() == view.end()) return;

            // 1. Detect and repair cycles/self-parenting in hierarchy links
            for (auto entityID : view) {
                if (registry.all_of<HierarchyComponent>(entityID)) {
                    auto& hc = registry.get<HierarchyComponent>(entityID);
                    if (hc.Parent != entt::null) {
                        if (!registry.valid(hc.Parent) || !registry.all_of<TransformComponent>(hc.Parent) || hc.Parent == entityID) {
                            hc.Parent = entt::null;
                        } else {
                            // Cycle detection: walk up parent chain with Floyd's Tortoise-and-Hare algorithm
                            entt::entity slow = hc.Parent;
                            entt::entity fast = hc.Parent;
                            bool hasCycle = false;
                            while (fast != entt::null && registry.valid(fast) && registry.all_of<HierarchyComponent>(fast)) {
                                fast = registry.get<HierarchyComponent>(fast).Parent;
                                if (fast != entt::null && registry.valid(fast) && registry.all_of<HierarchyComponent>(fast)) {
                                    fast = registry.get<HierarchyComponent>(fast).Parent;
                                    slow = registry.get<HierarchyComponent>(slow).Parent;
                                    if (slow == fast || slow == entityID || fast == entityID) {
                                        hasCycle = true;
                                        break;
                                    }
                                }
                            }
                            if (hasCycle) {
                                hc.Parent = entt::null; // Break cycle
                            }
                        }
                    }
                }
            }

            // 2. Collect all root entities (entities without active parents)
            std::vector<entt::entity> rootEntities;
            rootEntities.reserve(GetViewSizeHint(view));

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

            // 3. Sequential fallback if small, single thread, or JobSystem uninitialized
            if (!JobSystem::IsInitialized() || rootCount < minBatchSize || JobSystem::GetTotalThreadCount() <= 1) {
                for (uint32_t i = 0; i < rootCount; ++i) {
                    UpdateEntitySubtree(registry, rootEntities[i], glm::mat4(1.0f), 0);
                }
                return;
            }

            // 4. Parallel evaluation: Each thread processes its slice of root subtrees (disjoint memory)
            JobSystem::ParallelForRange(rootCount, [&registry, &rootEntities](uint32_t start, uint32_t end, uint32_t threadNum) {
                (void)threadNum;
                for (uint32_t i = start; i < end; ++i) {
                    UpdateEntitySubtree(registry, rootEntities[i], glm::mat4(1.0f), 0);
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

        // Helper: Recursive transform update down an entity subtree with depth protection
        static void UpdateEntitySubtree(entt::registry& registry, entt::entity entityID, const glm::mat4& parentWorld, uint32_t depth = 0) {
            if (depth > 128) {
                return; // Prevent stack overflow on pathological graph or cycle
            }
            if (!registry.valid(entityID) || !registry.all_of<TransformComponent>(entityID)) {
                return;
            }

            auto& tc = registry.get<TransformComponent>(entityID);
            tc.LocalMatrix = tc.GetLocalTransform();
            tc.WorldMatrix = parentWorld * tc.LocalMatrix;

            if (registry.all_of<HierarchyComponent>(entityID)) {
                const auto& hc = registry.get<HierarchyComponent>(entityID);
                auto children = hc.Children; // Copy list to prevent iterator invalidation
                for (auto child : children) {
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
                        UpdateEntitySubtree(registry, child, childParentWorld, depth + 1);
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
