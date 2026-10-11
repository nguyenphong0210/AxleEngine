#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/PhysicsScene.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <memory>

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
        std::string AssetPath;

        // Visual Caro (Checkerboard) material properties
        int PatternMode = 0; // 0: World XZ Caro, 1: 3D Caro, 2: Spherical Caro
        float TileScale = 1.0f;
        glm::vec4 Color1{ 1.0f };
        glm::vec4 Color2{ 0.0f };

        MeshComponent() = default;
        MeshComponent(const std::shared_ptr<VertexArray>& vao, int patternMode, float tileScale, const glm::vec4& c1, const glm::vec4& c2)
            : VAO(vao), PatternMode(patternMode), TileScale(tileScale), Color1(c1), Color2(c2) {}
        MeshComponent(const std::shared_ptr<VertexArray>& vao, const std::string& assetPath, int patternMode, float tileScale, const glm::vec4& c1, const glm::vec4& c2)
            : VAO(vao), AssetPath(assetPath), PatternMode(patternMode), TileScale(tileScale), Color1(c1), Color2(c2) {}
    };

    struct MaterialComponent {
        std::string AlbedoPath;
        std::shared_ptr<Texture2D> AlbedoMap;
        bool UseAlbedoMap = false;
        glm::vec4 AlbedoColor{ 1.0f, 1.0f, 1.0f, 1.0f };

        std::string NormalPath;
        std::shared_ptr<Texture2D> NormalMap;
        bool UseNormalMap = false;

        std::string RoughnessPath;
        std::shared_ptr<Texture2D> RoughnessMap;
        bool UseRoughnessMap = false;
        float Roughness = 0.5f;

        std::string MetallicPath;
        std::shared_ptr<Texture2D> MetallicMap;
        bool UseMetallicMap = false;
        float Metallic = 0.0f;

        glm::vec2 UVScale{ 1.0f, 1.0f };

        MaterialComponent() = default;

        void SetAlbedo(const std::string& path) {
            AlbedoPath = path;
            if (!path.empty()) {
                auto resolved = FileSystem::ResolveAssetPath(path);
                AlbedoMap = Texture2D::Create(resolved.string());
                UseAlbedoMap = (AlbedoMap && AlbedoMap->IsLoaded());
            } else {
                AlbedoMap = nullptr;
                UseAlbedoMap = false;
            }
        }

        void SetNormal(const std::string& path) {
            NormalPath = path;
            if (!path.empty()) {
                auto resolved = FileSystem::ResolveAssetPath(path);
                NormalMap = Texture2D::Create(resolved.string());
                UseNormalMap = (NormalMap && NormalMap->IsLoaded());
            } else {
                NormalMap = nullptr;
                UseNormalMap = false;
            }
        }

        void SetRoughnessMap(const std::string& path) {
            RoughnessPath = path;
            if (!path.empty()) {
                auto resolved = FileSystem::ResolveAssetPath(path);
                RoughnessMap = Texture2D::Create(resolved.string());
                UseRoughnessMap = (RoughnessMap && RoughnessMap->IsLoaded());
            } else {
                RoughnessMap = nullptr;
                UseRoughnessMap = false;
            }
        }

        void SetMetallicMap(const std::string& path) {
            MetallicPath = path;
            if (!path.empty()) {
                auto resolved = FileSystem::ResolveAssetPath(path);
                MetallicMap = Texture2D::Create(resolved.string());
                UseMetallicMap = (MetallicMap && MetallicMap->IsLoaded());
            } else {
                MetallicMap = nullptr;
                UseMetallicMap = false;
            }
        }
    };

    enum class BodyShapeType {
        Box = 0,
        Sphere = 1,
        Cylinder = 2,
        Cone = 3,
        Plane = 4,
        Mesh = 5
    };

    struct RigidBodyComponent {
        BodyID RuntimeBodyID;
        bool IsDynamic = true;
        BodyShapeType ShapeType = BodyShapeType::Box;
        glm::vec3 HalfExtent{ 0.5f };
        float Radius = 0.5f;
        float HalfHeight = 0.5f;
        float Friction = 0.5f;
        float Restitution = 0.0f;

        RigidBodyComponent() = default;
        RigidBodyComponent(BodyID bodyId, bool isDynamic = true)
            : RuntimeBodyID(bodyId), IsDynamic(isDynamic) {}
        RigidBodyComponent(BodyID bodyId, bool isDynamic, BodyShapeType shape, const glm::vec3& halfExtent, float friction = 0.5f, float restitution = 0.0f)
            : RuntimeBodyID(bodyId), IsDynamic(isDynamic), ShapeType(shape), HalfExtent(halfExtent), Friction(friction), Restitution(restitution) {}
    };

    struct HierarchyComponent {
        entt::entity Parent{ entt::null };
        std::vector<entt::entity> Children;
        bool InheritTransform = true;

        HierarchyComponent() = default;
        HierarchyComponent(entt::entity parent, bool inheritTransform = true)
            : Parent(parent), InheritTransform(inheritTransform) {}
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

    struct DirectionalLightComponent {
        glm::vec3 Color{ 1.0f, 0.98f, 0.92f }; // Sunlight warm tint
        float Intensity = 1.0f;
        float AmbientIntensity = 0.28f;
        float Azimuth = 45.0f;    // Compass angle (0..360 deg)
        float Elevation = 50.0f;  // Sun altitude angle (5..90 deg)
        bool CastShadows = true;

        // Post-Processing & Photometric Tone Mapping (Phase 11: Sub-step 3.2)
        float Exposure = 1.0f;
        int ToneMappingMode = 0;  // 0: ACES Filmic, 1: Reinhard, 2: Uncharted 2, 3: Linear Bypass
        float Gamma = 2.2f;

        DirectionalLightComponent() = default;
        DirectionalLightComponent(const glm::vec3& color, float intensity = 1.0f, float ambient = 0.28f, float azimuth = 45.0f, float elevation = 50.0f, bool castShadows = true,
                                  float exposure = 1.0f, int toneMapping = 0, float gamma = 2.2f)
            : Color(color), Intensity(intensity), AmbientIntensity(ambient), Azimuth(azimuth), Elevation(elevation), CastShadows(castShadows),
              Exposure(exposure), ToneMappingMode(toneMapping), Gamma(gamma) {}

        // Calculate unit vector pointing toward the sun light source
        glm::vec3 GetDirection() const {
            float radAz = glm::radians(Azimuth);
            float radEl = glm::radians(std::clamp(Elevation, 1.0f, 89.9f));
            return glm::normalize(glm::vec3(
                std::cos(radEl) * std::sin(radAz),
                std::sin(radEl),
                std::cos(radEl) * std::cos(radAz)
            ));
        }
    };

}

