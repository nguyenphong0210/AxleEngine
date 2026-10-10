#pragma once
#include <Axle/Types.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/PhysicsScene.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <memory>

namespace Axle {

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

        TransformComponent() = default;
        TransformComponent(const glm::vec3& translation)
            : Translation(translation) {}
        TransformComponent(const glm::vec3& translation, const glm::vec3& scale)
            : Translation(translation), Scale(scale) {}
        TransformComponent(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
            : Translation(translation), Rotation(rotation), Scale(scale) {}

        glm::mat4 GetTransform() const {
            return glm::translate(glm::mat4(1.0f), Translation)
                 * glm::mat4_cast(Rotation)
                 * glm::scale(glm::mat4(1.0f), Scale);
        }
    };

    struct MeshComponent {
        std::shared_ptr<VertexArray> VAO;

        // Visual Caro (Checkerboard) material properties
        int PatternMode = 0; // 0: World XZ Caro, 1: 3D Caro, 2: Spherical Caro
        float TileScale = 1.0f;
        glm::vec4 Color1{ 1.0f };
        glm::vec4 Color2{ 0.0f };

        MeshComponent() = default;
        MeshComponent(const std::shared_ptr<VertexArray>& vao, int patternMode, float tileScale, const glm::vec4& c1, const glm::vec4& c2)
            : VAO(vao), PatternMode(patternMode), TileScale(tileScale), Color1(c1), Color2(c2) {}
    };

    struct RigidBodyComponent {
        BodyID RuntimeBodyID;
        bool IsDynamic = true;

        RigidBodyComponent() = default;
        RigidBodyComponent(BodyID bodyId, bool isDynamic = true)
            : RuntimeBodyID(bodyId), IsDynamic(isDynamic) {}
    };

    struct HierarchyComponent {
        entt::entity Parent{ entt::null };
        std::vector<entt::entity> Children;

        HierarchyComponent() = default;
        HierarchyComponent(entt::entity parent)
            : Parent(parent) {}
    };

    class ScriptableEntity;

    struct NativeScriptComponent {
        ScriptableEntity* Instance = nullptr;
        std::string ScriptName = "NativeScript";

        ScriptableEntity* (*InstantiateScript)() = nullptr;
        void (*DestroyScript)(NativeScriptComponent*) = nullptr;

        template<typename T>
        void Bind(const std::string& name = "") {
            ScriptName = name.empty() ? typeid(T).name() : name;
            InstantiateScript = []() -> ScriptableEntity* { return static_cast<ScriptableEntity*>(new T()); };
            DestroyScript = [](NativeScriptComponent* nsc) {
                delete nsc->Instance;
                nsc->Instance = nullptr;
            };
        }
    };

    class Vehicle;

    struct VehicleComponent {
        Vehicle* VehicleInstance = nullptr;

        VehicleComponent() = default;
        VehicleComponent(Vehicle* vehicle)
            : VehicleInstance(vehicle) {}
    };

}
