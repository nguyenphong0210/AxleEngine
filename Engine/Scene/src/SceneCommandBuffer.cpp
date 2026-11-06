#include <Axle/SceneCommandBuffer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>

namespace Axle {

    SceneCommandBuffer::SceneCommandBuffer(SceneCommandBuffer&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.m_Mutex);
        m_Commands = std::move(other.m_Commands);
        m_LastExecutedCount = other.m_LastExecutedCount;
        m_LastRejectedCount = other.m_LastRejectedCount;
    }

    SceneCommandBuffer& SceneCommandBuffer::operator=(SceneCommandBuffer&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(m_Mutex, other.m_Mutex);
            m_Commands = std::move(other.m_Commands);
            m_LastExecutedCount = other.m_LastExecutedCount;
            m_LastRejectedCount = other.m_LastRejectedCount;
        }
        return *this;
    }

    void SceneCommandBuffer::DestroyEntity(Entity entity) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Commands.push_back([entity](Scene& scene, ExecutionContext& ctx) {
            if (!ctx.ValidateTarget(scene, entity)) {
                return;
            }

            auto handle = static_cast<entt::entity>(entity);

            // Mark handle and all descendant children as destroyed in this batch
            std::function<void(entt::entity)> markDescendants = [&](entt::entity h) {
                ctx.DestroyedEntities.insert(h);
                if (scene.GetRegistry().valid(h) && scene.GetRegistry().template all_of<HierarchyComponent>(h)) {
                    auto children = scene.GetRegistry().template get<HierarchyComponent>(h).Children;
                    for (auto c : children) {
                        markDescendants(c);
                    }
                }
            };
            markDescendants(handle);

            // Perform full scene destroy with script, physics, and hierarchy teardown
            scene.DestroyEntity(entity);
            ctx.ExecutedCount++;
        });
    }

    void SceneCommandBuffer::SetParent(Entity child, Entity parent, bool keepWorldPosition) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Commands.push_back([child, parent, keepWorldPosition](Scene& scene, ExecutionContext& ctx) {
            if (!ctx.ValidateTarget(scene, child)) {
                return;
            }

            if (parent) {
                // Validate parent target
                if (parent.GetScene() != &scene) {
                    ctx.RejectedCount++;
                    return;
                }
                auto parentHandle = static_cast<entt::entity>(parent);
                if (ctx.DestroyedEntities.find(parentHandle) != ctx.DestroyedEntities.end()) {
                    ctx.RejectedCount++;
                    return;
                }
                if (!scene.GetRegistry().valid(parentHandle)) {
                    ctx.RejectedCount++;
                    return;
                }
            }

            scene.SetParent(child, parent, keepWorldPosition);
            ctx.ExecutedCount++;
        });
    }

    void SceneCommandBuffer::CreateEntity(const std::string& name, const std::function<void(Entity)>& onCreated) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Commands.push_back([name, onCreated](Scene& scene, ExecutionContext& ctx) {
            Entity entity = scene.CreateEntity(name);
            if (onCreated) {
                onCreated(entity);
            }
            ctx.ExecutedCount++;
        });
    }

    void SceneCommandBuffer::CreateEntityWithUUID(UUID uuid, const std::string& name, const std::function<void(Entity)>& onCreated) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Commands.push_back([uuid, name, onCreated](Scene& scene, ExecutionContext& ctx) {
            Entity entity = scene.CreateEntityWithUUID(uuid, name);
            if (onCreated) {
                onCreated(entity);
            }
            ctx.ExecutedCount++;
        });
    }

    void SceneCommandBuffer::Custom(std::function<void(Scene&)> command) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Commands.push_back([command = std::move(command)](Scene& scene, ExecutionContext& ctx) {
            if (command) {
                command(scene);
                ctx.ExecutedCount++;
            }
        });
    }

    void SceneCommandBuffer::Merge(SceneCommandBuffer&& other) {
        if (this == &other) return;
        std::scoped_lock lock(m_Mutex, other.m_Mutex);
        m_Commands.reserve(m_Commands.size() + other.m_Commands.size());
        for (auto& cmd : other.m_Commands) {
            m_Commands.push_back(std::move(cmd));
        }
        other.m_Commands.clear();
    }

    void SceneCommandBuffer::Execute(Scene& targetScene) {
        std::vector<CommandFn> toExecute;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            toExecute.swap(m_Commands);
        }

        ExecutionContext ctx;
        for (auto& cmd : toExecute) {
            if (cmd) {
                cmd(targetScene, ctx);
            }
        }

        m_LastExecutedCount = ctx.ExecutedCount;
        m_LastRejectedCount = ctx.RejectedCount;
    }

    size_t SceneCommandBuffer::GetPendingCount() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Commands.size();
    }

    void SceneCommandBuffer::Clear() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Commands.clear();
        m_LastExecutedCount = 0;
        m_LastRejectedCount = 0;
    }

    bool SceneCommandBuffer::ExecutionContext::ValidateTarget(Scene& scene, Entity entity) {
        if (!entity) {
            RejectedCount++;
            return false;
        }

        // Wrong-scene boundary check
        if (entity.GetScene() != &scene) {
            RejectedCount++;
            return false;
        }

        auto handle = static_cast<entt::entity>(entity);

        // Destroy-then-edit conflict check
        if (DestroyedEntities.find(handle) != DestroyedEntities.end()) {
            RejectedCount++;
            return false;
        }

        // Registry validity check
        if (!scene.GetRegistry().valid(handle)) {
            RejectedCount++;
            return false;
        }

        return true;
    }

}
