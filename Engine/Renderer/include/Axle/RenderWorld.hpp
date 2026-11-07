#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/BuiltinMesh.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Axle {
    class VertexArray;
    class Material;
    namespace RHI {
        class RHIMesh;
        class RHIMaterial;
    }

    enum class RenderLightType : uint8_t {
        Directional,
        Point,
        Spot
    };

    struct RenderCamera {
        glm::vec3 Position{ 0.0f, 0.0f, 5.0f };
        glm::mat4 ViewMatrix{ 1.0f };
        glm::mat4 ProjectionMatrix{ 1.0f };
        glm::mat4 ViewProjectionMatrix{ 1.0f };
        float NearClip = 0.1f;
        float FarClip = 1000.0f;
        float FOV = 45.0f;
        float AspectRatio = 1.777778f;
    };

    struct RenderMaterialData {
        UUID MaterialUUID = 0;
        std::string MaterialPath;
        std::shared_ptr<Material> MaterialAsset;
        std::shared_ptr<RHI::RHIMaterial> RHIMaterialAsset;
        glm::vec4 AlbedoColor{ 1.0f };
        glm::vec4 SecondaryColor{ 1.0f };
        glm::vec2 TileScale{ 1.0f, 1.0f };
        int PatternMode = 0;
        float Roughness = 0.5f;
        float Metallic = 0.0f;
        float Emission = 0.0f;
        std::string AlbedoMapPath;
        std::string NormalMapPath;
        std::string MetallicRoughnessMapPath;
        std::string EmissiveMapPath;
        bool UseCustomShader = false;
        std::string CustomShaderPath;
    };

    struct RenderMeshInstance {
        uint32_t EntityID = 0;
        UUID StableUUID = 0;
        std::string Tag;
        std::string MeshURI;
        UUID MeshSourceUUID = 0;
        std::optional<BuiltinMeshId> BuiltinId;
        std::shared_ptr<VertexArray> GLMesh;
        std::shared_ptr<RHI::RHIMesh> NativeMesh;
        glm::mat4 WorldTransform{ 1.0f };
        RenderMaterialData Material;
        bool CastsShadows = true;
        bool Selected = false;
    };

    struct RenderDirectionalLight {
        glm::vec3 Direction{ 0.45f, 1.0f, 0.65f };
        glm::vec3 Color{ 1.0f, 0.98f, 0.92f };
        float Intensity = 1.0f;
        float AmbientIntensity = 0.28f;
        bool CastsShadows = true;
    };

    struct RenderPointLight {
        uint32_t EntityID = 0;
        glm::vec3 Position{ 0.0f };
        glm::vec3 Color{ 1.0f };
        float Intensity = 1.0f;
        float Radius = 10.0f;
        float Falloff = 1.0f;
    };

    struct RenderSpotLight {
        uint32_t EntityID = 0;
        glm::vec3 Position{ 0.0f };
        glm::vec3 Direction{ 0.0f, -1.0f, 0.0f };
        glm::vec3 Color{ 1.0f, 0.98f, 0.92f };
        float Intensity = 8.0f;
        float Range = 35.0f;
        float InnerAngle = 18.0f; // degrees
        float OuterAngle = 32.0f; // degrees
    };

    struct RenderSkyboxData {
        bool Enabled = true;
        int Type = 0; // 0: Atmospheric Procedural, 1: Cubemap / HDRI
        glm::vec4 SunDirection{ 0.45f, 1.0f, 0.65f, 0.0f };
        glm::vec4 SunColor{ 1.0f, 0.98f, 0.92f, 1.0f };
        glm::vec4 ZenithColor{ 0.2f, 0.35f, 0.6f, 1.0f };
        glm::vec4 HorizonColor{ 0.6f, 0.7f, 0.8f, 1.0f };
        glm::vec4 GroundColor{ 0.15f, 0.12f, 0.1f, 1.0f };
        std::string HDRPath;
        float Intensity = 1.0f;
        float Exposure = 1.0f;
    };

    // Immutable extracted snapshot of a scene ready for rendering.
    // Retains zero mutable references to the ECS registry or Scene.
    class RenderWorld {
    public:
        RenderWorld() = default;
        ~RenderWorld() = default;

        RenderWorld(const RenderWorld&) = default;
        RenderWorld& operator=(const RenderWorld&) = default;
        RenderWorld(RenderWorld&&) noexcept = default;
        RenderWorld& operator=(RenderWorld&&) noexcept = default;

        void Clear() {
            Meshes.clear();
            DirectionalLight.reset();
            PointLights.clear();
            SpotLights.clear();
            EntityToStableUUID.clear();
            Camera = RenderCamera{};
            Skybox = RenderSkyboxData{};
            SceneRevision = 0;
            TotalEntities = 0;
        }

        bool IsEmpty() const { return Meshes.empty() && PointLights.empty() && SpotLights.empty() && !DirectionalLight.has_value(); }
        size_t GetMeshCount() const { return Meshes.size(); }
        size_t GetPointLightCount() const { return PointLights.size(); }
        size_t GetSpotLightCount() const { return SpotLights.size(); }

        std::optional<UUID> FindStableUUID(uint32_t entityID) const {
            auto it = EntityToStableUUID.find(entityID);
            if (it != EntityToStableUUID.end()) return it->second;
            return std::nullopt;
        }

    public:
        RenderCamera Camera;
        std::vector<RenderMeshInstance> Meshes;
        std::optional<RenderDirectionalLight> DirectionalLight;
        std::vector<RenderPointLight> PointLights;
        std::vector<RenderSpotLight> SpotLights;
        RenderSkyboxData Skybox;

        std::unordered_map<uint32_t, UUID> EntityToStableUUID;
        uint64_t SceneRevision = 0;
        uint32_t TotalEntities = 0;
    };
}
