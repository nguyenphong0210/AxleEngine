#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace Axle {
    struct IDComponent {
        UUID ID;

        IDComponent() = default;
        IDComponent(const UUID& id)
            : ID(id) {}
    };

    struct TagComponent {
        std::string Tag;

        TagComponent() = default;
        TagComponent(const std::string& tag)
            : Tag(tag) {}
    };

    struct TransformComponent {
        glm::vec3 Translation{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };

        glm::mat4 LocalMatrix{ 1.0f };
        glm::mat4 WorldMatrix{ 1.0f };

        TransformComponent() = default;
        TransformComponent(const glm::vec3& translation)
            : Translation(translation) {}
        TransformComponent(const glm::vec3& translation, const glm::vec3& scale)
            : Translation(translation), Scale(scale) {}
        TransformComponent(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
            : Translation(translation), Rotation(rotation), Scale(scale) {}

        glm::mat4 GetLocalTransform() const {
            return glm::translate(glm::mat4(1.0f), Translation)
                * glm::mat4_cast(Rotation)
                * glm::scale(glm::mat4(1.0f), Scale);
        }

        glm::mat4 GetTransform() const {
            return GetLocalTransform();
        }
    };

    struct HierarchyComponent {
        entt::entity Parent{ entt::null };
        std::vector<entt::entity> Children;
        bool InheritTransform = true;
        bool InheritScale = true;

        HierarchyComponent() = default;
        HierarchyComponent(entt::entity parent, bool inheritTransform = true, bool inheritScale = true)
            : Parent(parent), InheritTransform(inheritTransform), InheritScale(inheritScale) {}
    };
    using RelationshipComponent = HierarchyComponent; // Relationship / Scene Graph alias


    inline glm::mat4 InheritedParentTransform(const glm::mat4& parent, const HierarchyComponent& hierarchy) {
        if (!hierarchy.InheritTransform) return glm::mat4(1.0f);
        if (hierarchy.InheritScale) return parent;
        glm::mat4 rigid(1.0f);
        for (int axis = 0; axis < 3; ++axis) {
            const auto column = glm::vec3(parent[axis]);
            const float length = glm::length(column);
            rigid[axis] = glm::vec4(length > 1e-6f ? column / length : glm::vec3(axis == 0, axis == 1, axis == 2), 0.0f);
        }
        rigid[3] = parent[3];
        return rigid;
    }
}
