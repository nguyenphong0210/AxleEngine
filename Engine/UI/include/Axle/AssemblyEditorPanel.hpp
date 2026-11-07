#pragma once
#include <Axle/Types.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/RenderService.hpp>
#if defined(AXLE_BUILD_RHI)
#include <Axle/RHIViewportDescriptor.hpp>
#endif
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

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

        // Preview scene and data inspection
        Scene* GetPreviewScene() const { return m_PreviewScene.get(); }
        const AssemblyData& GetAssemblyData() const { return m_AssemblyData; }
        void SyncPreviewScene();

    private:
        void RenderPreview(float width, float height);
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
        std::unordered_map<int, uint32_t> m_NodeIndexToEntityID;

        // Scene context for instantiating & asset loading
        Scene* m_Scene = nullptr;
        PhysicsScene* m_PhysicsScene = nullptr;
        const SceneSerializerContext* m_SerializerContext = nullptr;

        // Private Preview Scene
        std::unique_ptr<Scene> m_PreviewScene;

        // Viewport & Rendering Targets
#if defined(AXLE_BUILD_RHI)
        RHIViewportDescriptor m_RHIViewport;
#endif
        std::shared_ptr<Framebuffer> m_Framebuffer;
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

        // Status & Diagnostic Notification
        std::string m_StatusMessage;
        float m_StatusTimer = 0.0f;
        std::string m_DiagnosticMessage;
    };

}
