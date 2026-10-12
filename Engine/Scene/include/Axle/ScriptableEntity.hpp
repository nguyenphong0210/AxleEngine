#pragma once
#include <Axle/Entity.hpp>
#include <Axle/Timestep.hpp>

namespace Axle {

    class ScriptableEntity {
    public:
        virtual ~ScriptableEntity() = default;

        template<typename T>
        T& GetComponent() {
            return m_Entity.GetComponent<T>();
        }

        template<typename T>
        const T& GetComponent() const {
            return m_Entity.GetComponent<T>();
        }

        template<typename T>
        bool HasComponent() const {
            return m_Entity.HasComponent<T>();
        }

        Entity GetEntity() const { return m_Entity; }

        // Hierarchy Traversal & Relationship Helpers
        Entity GetParent() const { return m_Entity.GetParent(); }
        void SetParent(Entity parent) { m_Entity.SetParent(parent); }
        std::vector<Entity> GetChildren() const { return m_Entity.GetChildren(); }
        size_t GetChildCount() const { return m_Entity.GetChildCount(); }
        Entity FindChildByTag(const std::string& tag, bool recursive = true) const { return m_Entity.FindChildByTag(tag, recursive); }
        void ForEachChild(const std::function<void(Entity)>& func, bool recursive = true) const { m_Entity.ForEachChild(func, recursive); }
        bool IsChildOf(Entity potentialParent) const { return m_Entity.IsChildOf(potentialParent); }
        const std::string& GetName() const { return m_Entity.GetName(); }
        glm::mat4 GetWorldTransform() const { return m_Entity.GetWorldTransform(); }

        virtual void OnCreate() {}
        virtual void OnUpdate(Timestep ts) {}
        virtual void OnDestroy() {}

    private:
        Entity m_Entity;
        friend class Scene;
    };

}
