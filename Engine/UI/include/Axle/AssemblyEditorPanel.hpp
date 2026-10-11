#pragma once
#include <Axle/Types.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Axle {

    class AssemblyEditorPanel {
    public:
        AssemblyEditorPanel();
        ~AssemblyEditorPanel() = default;

        void SetAssembly(const std::filesystem::path& filepath);
        const std::filesystem::path& GetCurrentAsset() const { return m_CurrentAssetPath; }

        void SetSceneContext(Scene* scene, PhysicsScene* physicsScene, const SceneSerializerContext* serializerContext);

        void OnImGuiRender(bool* p_open = nullptr);

        bool IsOpen() const { return m_IsOpen; }
        void SetOpen(bool open) { m_IsOpen = open; }

    private:
        void RenderPreview3D(float width, float height);
        void DrawPedestalGrid(const glm::mat4& viewProj, float radius);
        void DrawNodeHierarchy(AssemblyNode& node, int index, int& selectedId, AssemblyNode*& selectedNode);
        void DrawNodeInspector(AssemblyNode* node);
        void SaveAssembly();
        void InstantiateIntoScene();

    private:
        std::filesystem::path m_CurrentAssetPath;
        AssemblyData m_AssemblyData;
        bool m_HasAssembly = false;
        bool m_IsOpen = false;
        bool m_IsDirty = false;

        // Selection in hierarchy
        int m_SelectedNodeIndex = 0;
        AssemblyNode* m_SelectedNode = nullptr;

        // Scene context for instantiating & asset loading
        Scene* m_Scene = nullptr;
        PhysicsScene* m_PhysicsScene = nullptr;
        const SceneSerializerContext* m_SerializerContext = nullptr;

        // 3D Viewport & Rendering
        std::shared_ptr<Framebuffer> m_Framebuffer;
        std::unique_ptr<Shader> m_Shader;
        u32 m_ViewportWidth = 480;
        u32 m_ViewportHeight = 360;

        // Orbit Camera Controls
        float m_Pitch = 22.0f;
        float m_Yaw = 40.0f;
        float m_Zoom = 1.0f;
        glm::vec3 m_PanOffset{ 0.0f };

        // Interaction States
        bool m_TurntableActive = false;
        float m_TurntableSpeed = 1.0f;
        bool m_WireframeMode = false;
        bool m_ShowPedestal = true;
        bool m_ShowCollisionWireframes = true;
        bool m_IsViewportHovered = false;

        // Status Notification
        std::string m_StatusMessage;
        float m_StatusTimer = 0.0f;
    };

}
