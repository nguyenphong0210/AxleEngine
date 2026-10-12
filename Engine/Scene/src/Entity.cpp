#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
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

    Entity Entity::GetParent() const {
        if (!*this) return {};
        if (!HasComponent<HierarchyComponent>()) return {};

        const auto& hc = GetComponent<HierarchyComponent>();
        if (hc.Parent == entt::null || !m_Scene || !m_Scene->m_Registry.valid(hc.Parent)) {
            return {};
        }

        return Entity(hc.Parent, m_Scene);
    }

    void Entity::SetParent(Entity parent) {
        if (!*this) return;

        // Prevent self-parenting or parenting to a descendant (cycle prevention)
        if (parent && (parent == *this || parent.IsChildOf(*this))) {
            return;
        }

        // 1. Detach from current parent if attached
        if (HasComponent<HierarchyComponent>()) {
            auto& currentHc = GetComponent<HierarchyComponent>();
            if (currentHc.Parent != entt::null && m_Scene && m_Scene->m_Registry.valid(currentHc.Parent)) {
                if (m_Scene->m_Registry.all_of<HierarchyComponent>(currentHc.Parent)) {
                    auto& parentHc = m_Scene->m_Registry.get<HierarchyComponent>(currentHc.Parent);
                    auto it = std::remove(parentHc.Children.begin(), parentHc.Children.end(), m_EntityHandle);
                    parentHc.Children.erase(it, parentHc.Children.end());
                }
            }
            currentHc.Parent = entt::null;
        }

        // 2. Attach to new parent if valid
        if (parent) {
            if (!HasComponent<HierarchyComponent>()) {
                AddComponent<HierarchyComponent>();
            }
            GetComponent<HierarchyComponent>().Parent = parent.m_EntityHandle;

            if (!parent.HasComponent<HierarchyComponent>()) {
                parent.AddComponent<HierarchyComponent>();
            }
            auto& parentHc = parent.GetComponent<HierarchyComponent>();
            if (std::find(parentHc.Children.begin(), parentHc.Children.end(), m_EntityHandle) == parentHc.Children.end()) {
                parentHc.Children.push_back(m_EntityHandle);
            }
        }
    }

    std::vector<Entity> Entity::GetChildren() const {
        std::vector<Entity> result;
        if (!*this || !HasComponent<HierarchyComponent>()) return result;

        const auto& hc = GetComponent<HierarchyComponent>();
        result.reserve(hc.Children.size());
        for (auto childHandle : hc.Children) {
            if (m_Scene && m_Scene->m_Registry.valid(childHandle)) {
                result.emplace_back(childHandle, m_Scene);
            }
        }
        return result;
    }

    size_t Entity::GetChildCount() const {
        if (!*this || !HasComponent<HierarchyComponent>()) return 0;
        const auto& hc = GetComponent<HierarchyComponent>();
        size_t count = 0;
        for (auto childHandle : hc.Children) {
            if (m_Scene && m_Scene->m_Registry.valid(childHandle)) {
                count++;
            }
        }
        return count;
    }

    Entity Entity::FindChildByTag(const std::string& tag, bool recursive) const {
        if (!*this || !HasComponent<HierarchyComponent>()) return {};

        const auto& hc = GetComponent<HierarchyComponent>();
        for (auto childHandle : hc.Children) {
            if (m_Scene && m_Scene->m_Registry.valid(childHandle)) {
                Entity child(childHandle, m_Scene);
                if (child.HasComponent<TagComponent>() && child.GetComponent<TagComponent>().Tag == tag) {
                    return child;
                }
                if (recursive) {
                    Entity found = child.FindChildByTag(tag, true);
                    if (found) return found;
                }
            }
        }
        return {};
    }

    void Entity::ForEachChild(const std::function<void(Entity)>& func, bool recursive) const {
        if (!*this || !HasComponent<HierarchyComponent>()) return;

        const auto& hc = GetComponent<HierarchyComponent>();
        auto childrenCopy = hc.Children; // Copy to protect against iterator invalidation
        for (auto childHandle : childrenCopy) {
            if (m_Scene && m_Scene->m_Registry.valid(childHandle)) {
                Entity child(childHandle, m_Scene);
                func(child);
                if (recursive) {
                    child.ForEachChild(func, true);
                }
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
        if (!*this) return glm::mat4(1.0f);
        return m_Scene->GetWorldTransform(m_EntityHandle);
    }

}
