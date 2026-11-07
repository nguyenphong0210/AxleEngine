#pragma once
#include <Axle/Types.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/RenderService.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace Axle {

    namespace RHI { class RHITexture; }
    class Texture2D;

    enum class PreviewMeshType {
        Sphere = 0,
        Cube = 1,
        Cylinder = 2,
        Plane = 3,
        Custom = 4
    };

    struct IconTextureRegion {
        glm::vec2 UV0{0.0f}, UV1{1.0f};
        glm::vec2 ContentSize{1.0f};
    };

    class AssetPreviewPanel {
    public:
        AssetPreviewPanel();
        ~AssetPreviewPanel();

        void SetAsset(const std::filesystem::path& assetPath);
        const std::filesystem::path& GetCurrentAsset() const { return m_CurrentAssetPath; }
        bool HasAsset() const { return m_HasAsset; }
        bool IsAssembly() const { return m_IsAssembly; }
        const AssemblyData& GetAssemblyData() const { return m_CurrentAssembly; }
        const AxleLoadedPackage& GetPackageData() const { return m_CurrentPackage; }
        const glm::vec3& GetBoundsSize() const { return m_Size; }
        size_t GetTotalVertices() const { return m_TotalVertices; }
        size_t GetTotalTriangles() const { return m_TotalTriangles; }

        void SetSceneContext(Scene* scene, PhysicsScene* physicsScene, std::shared_ptr<VertexArray> fallbackMesh, const SceneSerializerContext* serializerContext = nullptr);
        void InitRHI(ImGuiLayer* imguiLayer);
        PreviewMeshType GetPreviewMeshType() const { return m_PreviewMeshType; }
        void SetPreviewMeshType(PreviewMeshType type);
        void LoadCustomPreviewMesh(const std::filesystem::path& meshPath);
        float GetSunAzimuth() const { return m_SunAzimuth; }
        void SetSunAzimuth(float deg);
        float GetSunElevation() const { return m_SunElevation; }
        void SetSunElevation(float deg);
        RHIViewportDescriptor* GetRHIViewport() const { return m_RHIViewport.get(); }
        const std::shared_ptr<Framebuffer>& GetGLFramebuffer() const { return m_Framebuffer; }
        Scene* GetPreviewScene() const { return m_PreviewScene.get(); }
        const std::string& GetRenderDiagnostic() const { return m_RenderDiagnostic; }
        void UpdateMaterialPreview(const MaterialComponent& mat);

        void RenderPreviewViewport();
        void OnImGuiRender(bool* p_open = nullptr);

        // UI Icon Texture Cache
        uint64_t GetOrCreateIconTexture(const std::string& iconFilename);
        IconTextureRegion GetIconRegion(const std::string& filename) const {
            auto found = m_IconRegions.find(filename);
            return found != m_IconRegions.end() ? found->second : IconTextureRegion{};
        }

    private:
        std::string m_RenderDiagnostic;
        void RenderPreview(float width, float height);
        void RebuildPreviewScene();
        bool IsMaterialPreview() const;
        glm::quat GetPreviewRotation() const;
        float GetPreviewScale() const;
        glm::mat4 GetPreviewTransform() const;
        void DrawPedestalGrid(const glm::mat4& viewProj, float radius);
        void SpawnCurrentAssetIntoScene();

    private:
        std::filesystem::path m_CurrentAssetPath;
        AxleLoadedPackage m_CurrentPackage;
        std::vector<std::shared_ptr<VertexArray>> m_PartVAOs;
        bool m_IsAssembly = false;
        AssemblyData m_CurrentAssembly;
        bool m_HasAsset = false;

        // Viewport & Rendering
        std::shared_ptr<Framebuffer> m_Framebuffer;
        std::unique_ptr<RHIViewportDescriptor> m_RHIViewport;
        ImGuiLayer* m_ImGuiLayer = nullptr;
        GuiTextureRegistrar m_TextureRegistrar;
        GuiDeferredReleaser m_DeferredRelease;
        u32 m_ViewportWidth = 380;
        u32 m_ViewportHeight = 320;

        // Fixed camera framing; mouse gestures transform the preview object.
        float m_Pitch = 22.0f;
        float m_Yaw = 40.0f;
        float m_Zoom = 1.0f;
        glm::vec3 m_PanOffset{ 0.0f };

        // Interaction States
        bool m_TurntableActive = false;
        float m_TurntableSpeed = 1.0f;
        bool m_WireframeMode = false;
        bool m_ShowPedestal = true;
        PreviewMeshType m_PreviewMeshType = PreviewMeshType::Sphere;
        bool m_IsViewportHovered = false;
        float m_SunAzimuth = 45.0f;
        float m_SunElevation = 35.264f;
        std::filesystem::path m_CustomMeshPath;
        std::shared_ptr<VertexArray> m_CustomMeshVAO;

        // Cached Bounds
        glm::vec3 m_Center{ 0.0f };
        glm::vec3 m_Size{ 1.0f };
        float m_MaxDim = 1.0f;
        size_t m_TotalVertices = 0;
        size_t m_TotalTriangles = 0;

        // Scene linkage for spawning
        Scene* m_Scene = nullptr;
        PhysicsScene* m_PhysicsScene = nullptr;
        std::shared_ptr<VertexArray> m_FallbackMesh;
        const SceneSerializerContext* m_SerializerContext = nullptr;

        // Multi-Backend Scene & Lighting Preview
        std::shared_ptr<Scene> m_PreviewScene;
        Entity m_PreviewEntity;
        Entity m_PreviewLight;
        Entity m_PreviewFillLight;
        float m_ObjectYaw = 0.0f;
        float m_ObjectPitch = 0.0f;
        MaterialComponent m_ActiveMaterial;
        bool m_SceneNeedsRebuild = true;

        // Studio Light controls (Godot parity)
        bool m_EnableKeyLight = true;
        bool m_EnableSecondaryLight = true;
        void UpdatePreviewLighting();

        // UI Icon Texture Cache
        void FreeIconTextures();
        std::unordered_map<std::string, IconTextureRegion> m_IconRegions;
        std::unordered_map<std::string, std::pair<std::shared_ptr<RHI::RHITexture>, GuiTextureLease>> m_IconCache;
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_GLIconCache;
    };

}
