#pragma once
#include <Axle/Types.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <Axle/ParticleSystem.hpp>
#include <Axle/ParticleRenderer.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/Entity.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/Components.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace Axle {

    class Scene;

    class VFXEditorPanel {
    public:
        VFXEditorPanel();
        ~VFXEditorPanel();

        void OnUpdate(Timestep ts);
        void OnImGuiRender(bool* p_open = nullptr);

        void SetSceneContext(Scene* scene) { m_Scene = scene; }
        void SetSelectedEntity(Entity entity);
        void SetEmitterConfig(const ParticleEmitterConfig& config);

        void InitRHI(class ImGuiLayer* imguiLayer);
        RHIViewportDescriptor* GetRHIViewport() const { return m_RHIViewport.get(); }
        bool HasRHIViewport() const { return m_RHIViewport != nullptr && m_RHIViewport->IsValid(); }

        bool IsOpen() const { return m_IsOpen; }
        void SetOpen(bool open) { m_IsOpen = open; }

        void TriggerBurst(uint32_t count);
        void ResetPreview();
        void PlayPreview();
        void PausePreview();
        void StopPreview();

        ParticleEmitterConfig& GetConfig() { return m_Config; }
        const ParticleEmitterConfig& GetConfig() const { return m_Config; }
        ParticleSystem* GetPreviewSystem() const { return m_PreviewSystem.get(); }

        // Emitter Stack API
        size_t GetEmitterStackCount() const { return m_EmitterStacks.size(); }
        ParticleEmitterStack* GetCurrentEmitterStack();
        void AddEmitterStack(const std::string& name, const ParticleEmitterConfig& config = {});
        void RemoveEmitterStack(size_t index);
        void SelectEmitterStack(size_t index);

    private:
        void RenderPreview3D(float width, float height);
        void DrawGrid(const glm::mat4& viewProj, float radius = 5.0f);
        
        // 3-Column Unity Shuriken Layout
        void DrawEmitterStackColumn(float width, float height);
        void DrawTransportBar();
        void DrawShurikenInspector(float width, float height);

        // Modular Shuriken Group Accordions
        void DrawPresetSelector();
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
        ParticleEmitterConfig m_Config;
        std::vector<ParticleEmitterStack> m_EmitterStacks;
        size_t m_SelectedStackIndex = 0;

        std::unique_ptr<ParticleSystem> m_PreviewSystem;
        std::unique_ptr<ParticleRenderer> m_ParticleRenderer;
        std::shared_ptr<Framebuffer> m_Framebuffer;
        std::unique_ptr<RHIViewportDescriptor> m_RHIViewport;
        class ImGuiLayer* m_ImGuiLayer = nullptr;
        
        bool m_IsOpen = false;
        bool m_IsPlaying = true;
        bool m_IsLooping = true;
        bool m_ShowGroundGrid = true;
        bool m_ShowBounds = false;
        float m_PlaybackScrubberTime = 0.0f;

        // Viewport resolution & Orbit Camera
        u32 m_ViewportWidth = 640;
        u32 m_ViewportHeight = 480;
        float m_CameraDistance = 5.0f;
        float m_CameraPitch = 20.0f;
        float m_CameraYaw = 45.0f;
        glm::vec3 m_CameraTarget = glm::vec3(0.0f, 1.0f, 0.0f);
        bool m_ViewportHovered = false;
        bool m_ViewportFocused = false;

        // Scene linkage
        Scene* m_Scene = nullptr;
        Entity m_SelectedEntity{};
        bool m_AutoSyncWithSelectedEntity = false;

        // Simple line drawing shader for reference ground grid
        u32 m_GridVAO = 0;
        u32 m_GridVBO = 0;
        std::unique_ptr<class Shader> m_GridShader;
        void InitGridShader();
    };

}
