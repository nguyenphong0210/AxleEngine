#pragma once
#include <Axle/Types.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <filesystem>
#include <memory>
#include <string>

namespace Axle {

    class AssetPreviewPanel {
    public:
        AssetPreviewPanel();
        ~AssetPreviewPanel() = default;

        void SetAsset(const std::filesystem::path& assetPath);
        const std::filesystem::path& GetCurrentAsset() const { return m_CurrentAssetPath; }
        bool HasAsset() const { return m_HasAsset; }

        void SetSceneContext(Scene* scene, PhysicsScene* physicsScene, std::shared_ptr<VertexArray> fallbackMesh, const SceneSerializerContext* serializerContext = nullptr);

        void RenderPreviewViewport();
        void OnImGuiRender(bool* p_open = nullptr);

    private:
        void RenderPreview3D(float width, float height);
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
        bool m_IsViewportHovered = false;

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
    };

}
