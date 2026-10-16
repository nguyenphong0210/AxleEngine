#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/Scene.hpp>
#include <entt/entt.hpp>
#include <utility>
#include <vector>
#include <string>
#include <functional>
#include <glm/glm.hpp>

namespace Axle {

    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene)
            : m_EntityHandle(handle), m_Scene(scene) {}
        Entity(const Entity& other) = default;

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args) {
            return m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        T& AddOrReplaceComponent(Args&&... args) {
            return m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
        }

        template<typename T>
        T& GetComponent() {
            return m_Scene->m_Registry.get<T>(m_EntityHandle);
        }

        template<typename T>
        const T& GetComponent() const {
            return m_Scene->m_Registry.get<T>(m_EntityHandle);
        }

        template<typename T>
        bool HasComponent() const {
            if (!*this) return false;
            return m_Scene->m_Registry.all_of<T>(m_EntityHandle);
        }

        template<typename T>
        void RemoveComponent() {
            if (!*this) return;
            m_Scene->m_Registry.remove<T>(m_EntityHandle);
        }

        operator bool() const {
            return m_EntityHandle != entt::null && m_Scene != nullptr && m_Scene->m_Registry.valid(m_EntityHandle);
        }
        operator entt::entity() const { return m_EntityHandle; }
        operator u32() const { return static_cast<u32>(m_EntityHandle); }

        bool operator==(const Entity& other) const {
            return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
        }

        bool operator!=(const Entity& other) const {
            return !(*this == other);
        }

        Scene* GetScene() const { return m_Scene; }

        // Hierarchy Traversal & Management
        Entity GetParent() const;
        void SetParent(Entity parent, bool keepWorldPosition = true);
        void Unparent(bool keepWorldPosition = true) { SetParent({}, keepWorldPosition); }
        std::vector<Entity> GetChildren() const;
        size_t GetChildCount() const;
        Entity FindChildByTag(const std::string& tag, bool recursive = true) const;
        void ForEachChild(const std::function<void(Entity)>& func, bool recursive = true) const;
        bool IsChildOf(Entity potentialParent) const;
        const std::string& GetName() const;
        UUID GetUUID() const;
        glm::mat4 GetWorldTransform() const;

    private:
        entt::entity m_EntityHandle{ entt::null };
        Scene* m_Scene = nullptr;
    };

}
