#pragma once
#include <Axle/Layer.hpp>
#include <Axle/PerspectiveCameraController.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/ShadowMap.hpp>
#include <Axle/Skybox.hpp>
#include <Axle/DebugPanel.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/ConsolePanel.hpp>
#include <Axle/ContentBrowserPanel.hpp>
#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/AssemblyEditorPanel.hpp>
#include <Axle/VFXEditorPanel.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsDebugRenderer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneRenderer.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/Entity.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/ScriptCompiler.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/RHI/RHIBarrier.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <filesystem>

class EditorLayer : public Axle::Layer {
public:
    enum class SceneState {
        Edit = 0,
        Play = 1,
        Pause = 2
    };

    EditorLayer();
    ~EditorLayer() override = default;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(Axle::Timestep ts) override;
    void OnImGuiRender() override;
    void OnWindowResize(Axle::u32 width, Axle::u32 height) override;
    void OnFileDrop(const std::vector<std::string>& filepaths) override;

    void OnScenePlay();
    void OnScenePause();
    void OnSceneStop();
    void CompileAndHotReloadAssets(bool forceRebakeAll = false);

    void TriggerScriptCompilationAndReload();
    void OpenInVisualStudio();
    void OpenScriptsDirectory();

    void NewProject();
    void OpenProject(const std::filesystem::path& filepath = {});
    void SaveProject();
    void SaveProjectAs();

    void NewScene();
    void OpenScene(const std::filesystem::path& filepath = {});
    void SaveScene(const std::filesystem::path& filepath = {});
    void SaveSceneAs();
    void UpdateWindowTitle();
    const std::filesystem::path& GetCurrentScenePath() const { return m_CurrentScenePath; }
    const std::filesystem::path& GetCurrentProjectPath() const { return m_CurrentProjectPath; }
    Axle::RHIViewportDescriptor* GetRHIViewport() const { return m_RHIViewport.get(); }
    Axle::RHISceneRenderer* GetRHISceneRenderer() const { return m_RHISceneRenderer.get(); }

private:
    void ResetSimulation();
    void RenderToolbar();
    void UnlockCursor();
    void ClearRHIViewport();
    bool IsCursorLocked() const;

private:
    std::unique_ptr<Axle::PerspectiveCameraController> m_CameraController;
    std::unique_ptr<Axle::PhysicsScene> m_PhysicsScene;
    std::unique_ptr<Axle::PhysicsDebugRenderer> m_PhysicsDebugRenderer;
    Axle::PhysicsDrawSettings m_PhysicsDrawSettings;
    bool m_ShowPhysicsDebug = false;
    bool m_F1KeyWasPressed = false;
    bool m_F9KeyWasPressed = false;
    bool m_F11KeyWasPressed = false;
    bool m_BKeyWasPressed = false;
    float m_CompileNotificationTimer = 0.0f;
    std::string m_CompileNotificationMessage;
    float m_ScriptCompileStatusTimer = 0.0f;
    std::string m_ScriptCompileStatusText;
    ImVec4 m_ScriptCompileStatusColor = ImVec4(0.35f, 0.95f, 0.55f, 1.0f);
    std::filesystem::path m_CurrentScenePath;
    std::filesystem::path m_CurrentProjectPath;

    // Viewport & Offscreen Framebuffer (OpenGL) / RHI Viewport Descriptor (D3D12/Vulkan)
    std::shared_ptr<Axle::Framebuffer> m_Framebuffer;
    std::unique_ptr<Axle::RHIViewportDescriptor> m_RHIViewport;
    Axle::u32 m_ViewportWidth = 1280;
    Axle::u32 m_ViewportHeight = 720;
    bool m_ViewportFocused = false;
    bool m_ViewportHovered = false;
    bool m_CursorCaptured = false;
    glm::vec2 m_PreCaptureMousePos{ 0.0f, 0.0f };

    // Simulation Execution Control
    SceneState m_SceneState = SceneState::Edit;
    bool m_StepSingleFrame = false;
    Axle::f32 m_SimulationSpeed = 1.0f;
    bool m_F5KeyWasPressed = false;
    bool m_F6KeyWasPressed = false;
    bool m_F7KeyWasPressed = false;
    bool m_OKeyWasPressed = false;
    bool m_CtrlOKeyWasPressed = false;
    bool m_EscKeyWasPressed = false;
    bool m_RKeyWasPressed = false;
    bool m_SKeyWasPressed = false;
    bool m_NKeyWasPressed = false;

    // 3D Transform Gizmo & Viewport Interaction (Phase 6)
    int m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
    bool m_GizmoModeLocal = false;
    bool m_SnapEnabled = false;
    float m_SnapTranslation = 0.5f;
    float m_SnapRotation = 45.0f;
    float m_SnapScale = 0.5f;

    // Camera Viewport Mode (Phase 20.4: Game View vs Eject Free-Fly)
    bool m_UseEditorCameraInPlay = false;
    bool m_ActiveCameraIsSceneCamera = false;
    bool m_F2KeyWasPressed = false;

    // Scene & Entities (Phase 18.2: Safe Scene State Simulation)
    std::shared_ptr<Axle::Scene> m_Scene;        // Active Scene pointer
    std::shared_ptr<Axle::Scene> m_EditorScene;  // Pristine Edit Mode Scene
    std::shared_ptr<Axle::Scene> m_RuntimeScene; // Ephemeral Simulation Scene

    // Primitive Meshes & Shaders
    std::shared_ptr<Axle::VertexArray> m_CubeMesh;
    std::shared_ptr<Axle::VertexArray> m_SphereMesh;
    std::shared_ptr<Axle::VertexArray> m_CylinderMesh;
    std::shared_ptr<Axle::VertexArray> m_ConeMesh;
    std::shared_ptr<Axle::VertexArray> m_PlaneMesh;
    std::unique_ptr<Axle::SceneRenderer> m_SceneRenderer;
    std::unique_ptr<Axle::RHISceneRenderer> m_RHISceneRenderer;
    Axle::SceneSerializerContext m_SerializerContext;

    // Editor Panels
    Axle::SceneHierarchyPanel m_SceneHierarchyPanel;
    Axle::ConsolePanel m_ConsolePanel;
    Axle::ContentBrowserPanel m_ContentBrowserPanel;
    Axle::AssetPreviewPanel m_AssetPreviewPanel;
    Axle::AssemblyEditorPanel m_AssemblyEditorPanel;
    Axle::VFXEditorPanel m_VFXEditorPanel;
    Axle::DebugPanel m_DebugPanel;

    // Panel Visibility Flags
    bool m_ShowSceneHierarchy = true;
    bool m_ShowProperties = true;
    bool m_ShowConsole = true;
    bool m_ShowContentBrowser = true;
    bool m_ShowAssemblyEditor = false;
    bool m_ShowVFXEditor = false;
    bool m_ShowTelemetry = false;
    bool m_ResetLayout = true;

    Axle::f32 m_LastDeltaTime = 0.0f;

    // Dynamic Simulation State Cache
    struct InitialBodyState {
        glm::vec3 Position{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    };
    std::unordered_map<Axle::u32, InitialBodyState> m_InitialDynamicStates;

    // 3D Model Asset Ingestion & Conversion Modal
    struct ModelImportModalData {
        std::filesystem::path SourcePath;
        std::filesystem::path TargetFolder;
        char OutputFileName[256] = "";
        bool SplitSubmeshes = true;
        int PatternMode = 1;
        float TileScale = 8.0f;
        glm::vec4 ColorPrimary{ 0.85f, 0.88f, 0.92f, 1.0f };
        glm::vec4 ColorSecondary{ 0.65f, 0.70f, 0.76f, 1.0f };
        std::string ErrorMessage;
    };

    bool m_ShowImportModal = false;
    bool m_OpenImportModalPopup = false;
    ModelImportModalData m_ImportModalData;
    std::vector<std::filesystem::path> m_PendingImportQueue;

    void SetupNextImportModal();
    void RenderImportModal();

    // Standalone Game Packaging & Exporting (Phase 23)
    bool m_ShowExportGameModal = false;
    char m_ExportDirectoryBuffer[260] = "Export";
    bool m_ExportBakeMeshes = true;
    bool m_ExportIncludeShaders = true;
    bool m_ExportCopyExecutables = true;
    std::string m_ExportStatusMessage;
    bool m_ExportStatusSuccess = false;
    std::string m_ExportLastOutputDir;

    void RenderExportGameModal();
};
