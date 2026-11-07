#pragma once
#include <Axle/Types.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <Axle/ParticleSystem.hpp>
#include <Axle/ParticleModules.hpp>
#include <Axle/ParticleRenderer.hpp>
#include <Axle/VFXEditorDocument.hpp>
#include <Axle/MaterialInspectorControls.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/Entity.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/Components.hpp>
#include <Axle/RenderService.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace Axle {

    struct VFXPreviewClock {
        uint64_t Tick = 0;
        double Accumulator = 0.0;
        double StepSeconds = 1.0 / 60.0;
        float PlaybackSpeed = 1.0f;
    };

    class Scene;

    class VFXEditorPanel {
    public:
        VFXEditorPanel();
        ~VFXEditorPanel();

        void OnUpdate(Timestep ts);
        void OnImGuiRender(bool* p_open = nullptr);

        void SetSceneContext(Scene* scene) {
            // Compare raw ownership before consulting an entity registry that may already have been
            // destroyed.
            if (m_SelectedEntity.GetScene() != scene)
                m_SelectedEntity = {};
            if (m_Scene != scene) {
                m_Scene = scene;
                m_PhysicsScene = nullptr;
            }
        }
        void SetSelectedEntity(Entity entity);
        void OpenEntityEffect(Entity entity);
        void SetPhysicsScene(PhysicsScene *scene) {
            m_PhysicsScene = scene;
        }
        Entity GetSelectedEntity() const { return m_SelectedEntity; }
        void SetEmitterConfig(const ParticleEmitterConfig& config);

        void ShutdownRHI();
        void InitRHI(class ImGuiLayer* imguiLayer);
        RHIViewportDescriptor* GetRHIViewport() const { return m_RHIViewport.get(); }
        std::shared_ptr<Framebuffer> GetGLFramebuffer() const { return m_Framebuffer; }
        bool HasRHIViewport() const { return m_RHIViewport != nullptr && m_RHIViewport->IsValid(); }

        bool IsOpen() const { return m_IsOpen; }
        void SetOpen(bool open) { m_IsOpen = open; }

        void TriggerBurst(uint32_t count);
        void ResetPreview();
        void PlayPreview();
        void PausePreview();
        void StopPreview();
        void Step(float stepTime = -1.0f);
        void SeekTo(float targetTime);

        float GetPlaybackSpeed() const { return m_Clock.PlaybackSpeed; }
        void SetPlaybackSpeed(float speed) { m_Clock.PlaybackSpeed = speed; }
        float GetPlaybackScrubberTime() const { return m_PlaybackScrubberTime; }
        void SetPlaybackScrubberTime(float time) { SeekTo(time); }

        ParticleEmitterConfig& GetConfig() { return m_Config; }
        const ParticleEmitterConfig& GetConfig() const { return m_Config; }
        ParticleSystem* GetPreviewSystem() const { return m_EmitterStacks.empty() ? nullptr : m_EmitterStacks[0].System.get(); }

        // Document Model API
        VFXEditorDocument& GetDocument() { return m_Document; }
        const VFXEditorDocument& GetDocument() const { return m_Document; }
        void NewDocument(const std::string& name = "NewEffect");
        bool OpenDocument(const std::string& filePath);
        bool SaveDocument();
        bool DuplicateEmitterStack(size_t index);
        bool ReorderEmitterStack(size_t from, size_t to);
        bool SetSubEmitterTarget(size_t linkIndex, const std::string& targetId);
        bool ApplyToSelectedEntity();
        Entity CreateSceneInstance(const glm::vec3& position = glm::vec3(0));
        bool GetPreviewBounds(glm::vec3& minimum, glm::vec3& maximum) const;
        void FramePreview();
        bool Undo();
        bool Redo();
        bool SaveDocumentAs(const std::string& filePath);
        void SyncStacksFromDocument();
        void SyncDocumentFromStacks();

        // Emitter Stack API
        size_t GetEmitterStackCount() const { return m_EmitterStacks.size(); }
        size_t GetSelectedStackIndex() const { return m_SelectedStackIndex; }
        ParticleEmitterStack* GetCurrentEmitterStack();
        void AddEmitterStack(const std::string& name, const ParticleEmitterConfig& config = ParticleEmitterConfig::CreateNeutral());
        void RemoveEmitterStack(size_t index);
        void SelectEmitterStack(size_t index);

    private:
        bool DrawResourcePicker(const char *label, std::string &path, bool mesh);
        MaterialInspectorControls m_TexturePicker;
        char m_ResourceSearch[128]{};
        std::vector<std::filesystem::path> m_ResourceFiles;
        void RenderPreview(float width, float height);
        void DrawGrid(const glm::mat4& viewProj, float radius = 5.0f);
        
        // Adaptive emitter, preview and module workspace
        void DrawEmitterStackColumn(float width, float height);
        void DrawTransportBar();
        void DrawDocumentDialogs();
        void DrawPreviewViewport(float width, float height);
        PerspectiveCamera CreatePreviewCamera(float width, float height) const;
        void FinishEmitterEdit();
        void DrawShurikenInspector(float width, float height);

        // Searchable module inspector
        bool BeginModule(ParticleModule module);
        void EndModule();
        char m_ModuleSearch[128]{};
        bool m_EnabledModulesOnly = false;
        bool m_HasModuleClipboard = false;
        ParticleModule m_CopiedModule = ParticleModule::Main;
        ParticleEmitterConfig m_ModuleClipboard;
        void DrawMainModule();
        void DrawEmissionAndBurstsModule();
        void DrawShapeControls();
        void DrawCurvesControls();
        void DrawTextureFlipbookControls();
        void DrawDynamicsAndForceFields();
        void DrawSubEmittersModule();
        void DrawShadingAndBillboarding();
        void DrawSceneActions();

        void SyncCurrentStackConfig();

    private:
        VFXEditorDocument m_Document;
        ParticleEmitterConfig m_Config;
        std::vector<ParticleEmitterStack> m_EmitterStacks;
        size_t m_SelectedStackIndex = 0;


        std::unique_ptr<ParticleRenderer> m_ParticleRenderer;
        std::shared_ptr<Scene> m_PreviewScene;
        Entity m_PreviewEntity;
        std::vector<Entity> m_PreviewGrid;
        std::shared_ptr<Framebuffer> m_Framebuffer;
        std::unique_ptr<RHIViewportDescriptor> m_RHIViewport;
        class ImGuiLayer* m_ImGuiLayer = nullptr;
        GuiDeferredReleaser m_DeferredRelease;
        
        bool m_IsOpen = false;
        bool m_IsPlaying = true;
        bool m_IsLooping = true;
        bool m_ShowGroundGrid = true;
        bool m_ShowBounds = false;
        int m_PreviewBurstCount = 1;
        glm::vec4 m_PreviewBackground{0.07f, 0.08f, 0.10f, 1};
        glm::vec3 m_SpawnPosition{0};
        float m_PlaybackScrubberTime = 0.0f;
        VFXPreviewClock m_Clock;
        std::vector<VFXDiagnostic> m_Diagnostics;
        int m_PendingAction = 0, m_FileAction = 0;
        bool m_RequestFileDialog = false;
        char m_EffectSearch[128]{};
        std::vector<std::filesystem::path> m_EffectFiles;
        bool m_DiscardApproved = false;
        std::string m_PendingPath;
        char m_FilePath[512]{};
        bool m_IsSeeking = false;
        uint64_t m_TargetSeekTick = 0;

        // Viewport resolution & Orbit Camera
        u32 m_ViewportWidth = 640;
        u32 m_ViewportHeight = 480;
        float m_CameraDistance = 5.0f;
        float m_CameraPitch = 20.0f;
        float m_CameraYaw = 45.0f;
        glm::vec3 m_CameraTarget = glm::vec3(0.0f, 1.0f, 0.0f);
        bool m_ViewportHovered = false;
        bool m_ViewportFocused = false;
        int m_PreviewDragButton = -1;

        // Scene linkage
        Scene* m_Scene = nullptr;
        PhysicsScene *m_PhysicsScene = nullptr;
        Entity m_SelectedEntity{};
        bool m_AssetDocumentMode = false;

        // Simple line drawing shader for reference ground grid
        u32 m_GridVAO = 0;
        u32 m_GridVBO = 0;
        std::unique_ptr<class Shader> m_GridShader;
        void InitGridShader();
    };

}
