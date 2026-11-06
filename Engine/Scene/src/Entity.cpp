#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/Scene.hpp>
#include <Axle/ScenePhysicsBridge.hpp>
#include <algorithm>

namespace Axle {

    const std::string& Entity::GetName() const {
        static const std::string s_Empty = "";
        if (!*this) return s_Empty;
        if (HasComponent<TagComponent>()) {
            return GetComponent<TagComponent>().Tag;
        }
        return s_Empty;
    }

    UUID Entity::GetUUID() const {
        if (!*this) return 0;
        if (HasComponent<IDComponent>()) {
            return GetComponent<IDComponent>().ID;
        }
        return 0;
    }

    Entity Entity::GetParent() const {
        if (!*this) return {};
        if (!HasComponent<HierarchyComponent>()) return {};

        const auto& hc = GetComponent<HierarchyComponent>();
        if (hc.Parent == entt::null || !m_Scene || !m_Scene->GetRegistry().valid(hc.Parent)) {
            return {};
        }

        return Entity(hc.Parent, m_Scene);
    }

    void Entity::Destroy(PhysicsScene* physicsScene) {
        if (m_Scene && *this) {
            if (physicsScene) {
                ScenePhysicsBridge::TeardownEntityPhysics(*m_Scene, m_EntityHandle, physicsScene);
            }
            m_Scene->DestroyEntity(*this);
            m_EntityHandle = entt::null;
        }
    }

    void Entity::SetParent(Entity parent, bool keepWorldPosition) {
        if (!*this || !m_Scene) return;

        // Prevent cross-scene parenting
        if (parent && parent.m_Scene != m_Scene) {
            return;
        }

        // Prevent self-parenting or parenting to a descendant
        if (parent && (parent == *this || parent.IsChildOf(*this))) {
            return;
        }

        m_Scene->SetParent(*this, parent, keepWorldPosition);
    }

    std::vector<Entity> Entity::GetChildren() const {
        std::vector<Entity> result;
        if (!*this || !HasComponent<HierarchyComponent>()) return result;

        const auto& hc = GetComponent<HierarchyComponent>();
        result.reserve(hc.Children.size());

        for (auto childHandle : hc.Children) {
            if (m_Scene && m_Scene->GetRegistry().valid(childHandle)) {
                result.emplace_back(childHandle, m_Scene);
            }
        }

        return result;
    }

    size_t Entity::GetChildCount() const {
        if (!*this || !HasComponent<HierarchyComponent>()) return 0;
        return GetComponent<HierarchyComponent>().Children.size();
    }

    Entity Entity::FindChildByTag(const std::string& tag, bool recursive) const {
        if (!*this || !HasComponent<HierarchyComponent>()) return {};

        const auto& hc = GetComponent<HierarchyComponent>();
        for (auto childHandle : hc.Children) {
            if (!m_Scene || !m_Scene->GetRegistry().valid(childHandle)) continue;

            Entity child(childHandle, m_Scene);
            if (child.GetName() == tag) {
                return child;
            }

            if (recursive) {
                Entity found = child.FindChildByTag(tag, true);
                if (found) return found;
            }
        }

        return {};
    }

    void Entity::ForEachChild(const std::function<void(Entity)>& func, bool recursive) const {
        if (!*this || !HasComponent<HierarchyComponent>()) return;

        auto childrenCopy = GetComponent<HierarchyComponent>().Children;
        for (auto childHandle : childrenCopy) {
            if (!m_Scene || !m_Scene->GetRegistry().valid(childHandle)) continue;

            Entity child(childHandle, m_Scene);
            func(child);

            if (recursive && child) {
                child.ForEachChild(func, true);
            }
        }
    }

    bool Entity::IsChildOf(Entity potentialParent) const {
        if (!*this || !potentialParent) return false;

        Entity current = GetParent();
        while (current) {
            if (current == potentialParent) return true;
            current = current.GetParent();
        }

        return false;
    }

    glm::mat4 Entity::GetWorldTransform() const {
        if (!*this || !m_Scene) return glm::mat4(1.0f);
        return m_Scene->GetWorldTransform(m_EntityHandle);
    }

    glm::mat4 Entity::RecomputeWorldTransform() {
        if (!*this || !m_Scene) return glm::mat4(1.0f);
        return m_Scene->RecomputeWorldTransform(m_EntityHandle);
    }

    Entity UICanvasComponent::GetDataSourceEntity(Scene* scene, Entity currentEntity) const {
        if (DataSourceEntityID == 0) {
            return currentEntity;
        }
        if (scene) {
            Entity target = scene->GetEntityByUUID(DataSourceEntityID);
            if (target) {
                return target;
            }
        }
        return currentEntity;
    }

}
