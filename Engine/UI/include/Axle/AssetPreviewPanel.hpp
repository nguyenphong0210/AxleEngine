#pragma once
#include <Axle/Types.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <filesystem>
#include <memory>
#include <string>

namespace Axle {

    class RHISceneRenderer;

    enum class PreviewMeshType {
        Sphere = 0,
        Cube = 1,
        Cylinder = 2,
        Plane = 3,
        Custom = 4
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
        void UpdateMaterialPreview(const MaterialComponent& mat);

        void RenderPreviewViewport();
        void OnImGuiRender(bool* p_open = nullptr);

    private:
        void RenderPreview3D(float width, float height);
        void RenderPreviewRHI(float width, float height);
        void RebuildPreviewScene();
        void DrawPedestalGrid(const glm::mat4& viewProj, float radius);
        void SpawnCurrentAssetIntoScene();

    private:
        std::filesystem::path m_CurrentAssetPath;
        AxleLoadedPackage m_CurrentPackage;
        bool m_IsAssembly = false;
        AssemblyData m_CurrentAssembly;
        bool m_HasAsset = false;

        // Viewport & Rendering
        std::shared_ptr<Framebuffer> m_Framebuffer;
        std::unique_ptr<Shader> m_Shader;
        std::unique_ptr<RHIViewportDescriptor> m_RHIViewport;
        ImGuiLayer* m_ImGuiLayer = nullptr;
        u32 m_ViewportWidth = 380;
        u32 m_ViewportHeight = 320;

        // Orbit Camera Controls
        float m_Pitch = 22.0f;
        float m_Yaw = 40.0f;
        float m_Zoom = 1.0f;
        glm::vec3 m_PanOffset{ 0.0f };

        // Interaction States
        bool m_TurntableActive = true;
        float m_TurntableSpeed = 1.0f;
        bool m_WireframeMode = false;
        bool m_ShowPedestal = true;
        PreviewMeshType m_PreviewMeshType = PreviewMeshType::Sphere;
        bool m_IsViewportHovered = false;
        float m_SunAzimuth = 45.0f;
        float m_SunElevation = 50.0f;
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

        // RHI Multi-Backend Scene & Lighting Preview
        std::unique_ptr<RHISceneRenderer> m_RHISceneRenderer;
        std::shared_ptr<Scene> m_PreviewScene;
        Entity m_PreviewEntity;
        Entity m_PreviewLight;
        MaterialComponent m_ActiveMaterial;
        bool m_SceneNeedsRebuild = true;
    };

}
