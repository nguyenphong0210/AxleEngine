#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/SceneData.hpp>
#include <entt/entt.hpp>
#include <utility>
#include <vector>
#include <string>
#include <functional>
#include <stdexcept>
#include <glm/glm.hpp>

namespace Axle {

    class Scene;

    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, SceneData* scene)
            : m_EntityHandle(handle), m_Scene(scene), m_Lifetime(scene ? scene->GetLifetime() : nullptr),
              m_Epoch(scene ? scene->GetLifetime()->Epoch.load() : 0) {}
        Entity(entt::entity handle, Scene* scene);
        Entity(const Entity& other) = default;

        template<typename T, typename... Args>
        decltype(auto) AddComponent(Args&&... args) {
            if (!*this || !m_Scene->CanMutate()) throw std::logic_error("Component mutation requires an owner-thread scene boundary");
            if (HasComponent<T>()) m_Scene->BeforeComponentRemove(m_EntityHandle, entt::type_hash<T>::value());
            if constexpr (std::is_void_v<decltype(m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...))>) {
                m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
                m_Scene->AfterComponentAdd(m_EntityHandle, entt::type_hash<T>::value());
            } else {
                auto& component = m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
                m_Scene->AfterComponentAdd(m_EntityHandle, entt::type_hash<T>::value());
                return component;
            }
        }

        template<typename T, typename... Args>
        decltype(auto) AddOrReplaceComponent(Args&&... args) {
            return AddComponent<T>(std::forward<Args>(args)...);
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
            if (!*this || !m_Scene->CanMutate()) return;
            m_Scene->BeforeComponentRemove(m_EntityHandle, entt::type_hash<T>::value());
            m_Scene->m_Registry.remove<T>(m_EntityHandle);
        }

        operator bool() const {
            auto lifetime = m_Lifetime.lock();
            return lifetime && lifetime->Epoch.load() == m_Epoch && m_EntityHandle != entt::null &&
                   m_Scene != nullptr && m_Scene->m_Registry.valid(m_EntityHandle);
        }
        operator entt::entity() const { return m_EntityHandle; }
        operator u32() const { return static_cast<u32>(m_EntityHandle); }

        bool operator==(const Entity& other) const {
            return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene && m_Epoch == other.m_Epoch &&
                   !m_Lifetime.owner_before(other.m_Lifetime) && !other.m_Lifetime.owner_before(m_Lifetime);
        }

        bool operator!=(const Entity& other) const {
            return !(*this == other);
        }

        Scene* GetScene() const { return *this ? m_Scene->GetRuntimeScene() : nullptr; }
        SceneData* GetSceneData() const { return m_Scene; }

        void Destroy(class PhysicsScene* physicsScene = nullptr);

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
        glm::mat4 RecomputeWorldTransform();

    private:
        entt::entity m_EntityHandle{ entt::null };
        SceneData* m_Scene = nullptr;
        std::weak_ptr<SceneLifetime> m_Lifetime;
        uint64_t m_Epoch = 0;
    };

}
