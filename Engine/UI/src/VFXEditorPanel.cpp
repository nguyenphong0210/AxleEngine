#include <Axle/Path.hpp>
#include <Axle/VFXEditorPanel.hpp>
#include <Axle/CurveEditorWidget.hpp>
#include <Axle/Components.hpp>
#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/RenderService.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Scene.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/ParticleEmitterSerializer.hpp>
#include <Axle/ParticleMeshSampler.hpp>
#include <Axle/ShaderDragDropDecoder.hpp>
#include <Axle/AssetDSL.hpp>
#include <imgui.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>
#include <limits>

namespace Axle {

    static const char* s_GridVertSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;

        uniform mat4 u_ViewProjection;
        out vec4 v_Color;

        void main() {
            v_Color = a_Color;
            gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
        }
    )";

    static const char* s_GridFragSrc = R"(
        #version 450 core
        in vec4 v_Color;
        layout(location = 0) out vec4 o_Color;

        void main() {
            o_Color = v_Color;
        }
    )";

    VFXEditorPanel::VFXEditorPanel() {
        m_Config = ParticleEmitterConfig::CreateNeutral();
        SyncStacksFromDocument();
        const auto path = PathToUtf8(FileSystem::GetAssetDirectory() / "VFX" / "NewEffect.axleasset");
        strncpy_s(m_FilePath, path.c_str(), sizeof(m_FilePath) - 1);

        if (Window::HasActiveContext()) {
            FramebufferSpecification spec;
            spec.Width = m_ViewportWidth;
            spec.Height = m_ViewportHeight;
            m_Framebuffer = Framebuffer::Create(spec);

            m_ParticleRenderer = std::make_unique<ParticleRenderer>();
            InitGridShader();
        }
    }

    VFXEditorPanel::~VFXEditorPanel() {
        ShutdownRHI();
        if (Window::HasActiveContext()) {
            if (glad_glDeleteVertexArrays && m_GridVAO) {
                glDeleteVertexArrays(1, &m_GridVAO);
                m_GridVAO = 0;
            }
            if (glad_glDeleteBuffers && m_GridVBO) {
                glDeleteBuffers(1, &m_GridVBO);
                m_GridVBO = 0;
            }
        }
    }

    void VFXEditorPanel::ShutdownRHI() {
        m_PreviewGrid.clear();
        m_PreviewEntity = {};
        m_PreviewScene.reset();
        m_RHIViewport.reset();
        m_ImGuiLayer = nullptr;
    }

    void VFXEditorPanel::InitRHI(ImGuiLayer* imguiLayer) {
        m_ImGuiLayer = imguiLayer;
        m_DeferredRelease=imguiLayer?imguiLayer->GetDeferredReleaser():GuiDeferredReleaser{};
        if (!m_ImGuiLayer) return;

        if (RHI::RHIDevice::Get().IsNativeReady()) {
            if (!m_RHIViewport) {
                m_RHIViewport = std::make_unique<RHIViewportDescriptor>();
            }
            m_RHIViewport->Init(m_ImGuiLayer, m_ViewportWidth, m_ViewportHeight, RHI::RHITextureFormat::RGBA8_UNORM, true);

        }
    }

    void VFXEditorPanel::InitGridShader() {
        if (!glad_glGenVertexArrays || !glad_glGenBuffers || !glad_glBindVertexArray || !glad_glBindBuffer || !glad_glBufferData) return;

        m_GridShader = std::make_unique<Shader>(s_GridVertSrc, s_GridFragSrc);

        std::vector<float> gridVertices;
        const int gridLines = 20;
        const float spacing = 0.5f;
        const float extent = (gridLines * spacing) * 0.5f;

        for (int i = 0; i <= gridLines; ++i) {
            float coord = -extent + i * spacing;

            glm::vec4 colorZ = (i == gridLines / 2) ? glm::vec4(0.2f, 0.4f, 0.9f, 0.8f) : glm::vec4(0.25f, 0.28f, 0.35f, 0.4f);
            gridVertices.insert(gridVertices.end(), { -extent, 0.0f, coord, colorZ.r, colorZ.g, colorZ.b, colorZ.a });
            gridVertices.insert(gridVertices.end(), {  extent, 0.0f, coord, colorZ.r, colorZ.g, colorZ.b, colorZ.a });

            glm::vec4 colorX = (i == gridLines / 2) ? glm::vec4(0.9f, 0.2f, 0.3f, 0.8f) : glm::vec4(0.25f, 0.28f, 0.35f, 0.4f);
            gridVertices.insert(gridVertices.end(), { coord, 0.0f, -extent, colorX.r, colorX.g, colorX.b, colorX.a });
            gridVertices.insert(gridVertices.end(), { coord, 0.0f,  extent, colorX.r, colorX.g, colorX.b, colorX.a });
        }

        glGenVertexArrays(1, &m_GridVAO);
        glBindVertexArray(m_GridVAO);

        glGenBuffers(1, &m_GridVBO);
        glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
        glBufferData(GL_ARRAY_BUFFER, gridVertices.size() * sizeof(float), gridVertices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));

        glBindVertexArray(0);
    }

    void VFXEditorPanel::SyncStacksFromDocument() {
        const auto& def = m_Document.GetDefinition();
        const auto selectedId = m_Document.GetSelectedEmitterId();
        m_EmitterStacks.clear();
        m_SelectedStackIndex = 0;
        for (const auto& emitter : def.Emitters) {
            if (emitter.Id == selectedId)
                m_SelectedStackIndex = m_EmitterStacks.size();
            m_EmitterStacks.emplace_back(emitter);
        }
        m_Config = m_EmitterStacks.empty() ? ParticleEmitterConfig::CreateNeutral()
                                          : m_EmitterStacks[m_SelectedStackIndex].Config;
        ResetPreview();
    }

    void VFXEditorPanel::SyncDocumentFromStacks() {
        auto definition = m_Document.GetDefinition();
        definition.Emitters.clear();
        for (const auto &stack : m_EmitterStacks)
            definition.Emitters.push_back(stack.GetDefinition());
        m_Document.ReplaceDefinition(definition);
    }

    void VFXEditorPanel::NewDocument(const std::string& name) {
        FinishEmitterEdit();
        if (m_Document.IsDirty() && !m_DiscardApproved) {
            m_PendingAction = 1;
            m_PendingPath = name;
            return;
        }
        m_DiscardApproved = false;
        m_Document.NewEffect(name);
        m_AssetDocumentMode = true;
        SyncStacksFromDocument();
    }
    bool VFXEditorPanel::OpenDocument(const std::string& filePath) {
        FinishEmitterEdit();
        if (m_Document.IsDirty() && !m_DiscardApproved) {
            m_PendingAction = 2;
            m_PendingPath = filePath;
            return false;
        }
        m_DiscardApproved = false;
        m_Diagnostics.clear();
        if (!m_Document.OpenEffect(filePath, m_Diagnostics))
            return false;
        m_AssetDocumentMode = true;
        SyncStacksFromDocument();
        return true;
    }
    bool VFXEditorPanel::SaveDocument() {
        SyncCurrentStackConfig();
        m_Document.CommitTransaction("Edit Emitter Modules");
        m_Diagnostics.clear();
        return m_Document.Save(m_Diagnostics);
    }
    void VFXEditorPanel::RelocateCleanDocument(const std::filesystem::path& path) {
        if (m_Document.IsDirty()) return;
        const auto text = PathToUtf8(path); m_Document.RelocateCleanDocument(text);
        strncpy_s(m_FilePath, text.c_str(), sizeof(m_FilePath) - 1);
    }
    void VFXEditorPanel::DiscardDocument() {
        m_Document.DiscardChanges(); SyncStacksFromDocument();
    }
    bool VFXEditorPanel::SaveDocumentAs(const std::string& filePath) {
        SyncCurrentStackConfig();
        m_Document.CommitTransaction("Edit Emitter Modules");
        m_Diagnostics.clear();
        return m_Document.SaveAs(filePath, m_Diagnostics);
    }
    ParticleEmitterStack* VFXEditorPanel::GetCurrentEmitterStack() {
        return m_SelectedStackIndex < m_EmitterStacks.size() ? &m_EmitterStacks[m_SelectedStackIndex]
                                                             : nullptr;
    }
    void VFXEditorPanel::SelectEmitterStack(size_t index) {
        if (index >= m_EmitterStacks.size())
            return;
        FinishEmitterEdit();
        m_SelectedStackIndex = index;
        m_Config = m_EmitterStacks[index].Config;
        m_Document.SelectEmitter(m_EmitterStacks[index].Id);
    }
    void VFXEditorPanel::AddEmitterStack(const std::string& name, const ParticleEmitterConfig& config) {
        FinishEmitterEdit();
        m_Document.AddEmitter(name, config);
        SyncStacksFromDocument();
    }
    void VFXEditorPanel::RemoveEmitterStack(size_t index) {
        if (index >= m_EmitterStacks.size())
            return;
        FinishEmitterEdit();
        m_Diagnostics.clear();
        if (m_Document.RemoveEmitter(m_EmitterStacks[index].Id, m_Diagnostics))
            SyncStacksFromDocument();
    }
    void VFXEditorPanel::FinishEmitterEdit() {
        SyncCurrentStackConfig();
        m_Document.CommitTransaction("Edit Emitter Modules");
    }

    bool VFXEditorPanel::DuplicateEmitterStack(size_t index) {
        if (index >= m_EmitterStacks.size()) return false;
        FinishEmitterEdit();
        if (m_Document.DuplicateEmitter(m_EmitterStacks[index].Id).empty()) return false;
        SyncStacksFromDocument();
        return true;
    }

    bool VFXEditorPanel::ReorderEmitterStack(size_t from, size_t to) {
        FinishEmitterEdit();
        if (!m_Document.ReorderEmitter(from, to)) return false;
        SyncStacksFromDocument();
        return true;
    }

    bool VFXEditorPanel::SetSubEmitterTarget(size_t linkIndex, const std::string& targetId) {
        auto* stack = GetCurrentEmitterStack();
        if (!stack || linkIndex > m_Config.SubEmitters.size()) return false;
        auto candidate = m_Config;
        if (linkIndex == candidate.SubEmitters.size())
            candidate.SubEmitters.emplace_back(SubEmitterTrigger::Collision, targetId, 1, 1.0f);
        else candidate.SubEmitters[linkIndex].TargetEmitterName = targetId;
        auto effect = m_Document.GetDefinition();
        auto* emitter = effect.FindEmitter(stack->Id);
        if (!emitter) return false;
        emitter->Config = candidate;
        std::vector<std::string> errors;
        m_Diagnostics.clear();
        if (!effect.HasEmitter(targetId) || !ValidateSubEmitterGraph(effect, errors)) {
            for (const auto& error : errors)
                m_Diagnostics.push_back({VFXDiagnosticSeverity::Error, "INVALID_SUB_EMITTER", stack->Id, error});
            if (m_Diagnostics.empty()) m_Diagnostics.push_back({VFXDiagnosticSeverity::Error,
                "INVALID_SUB_EMITTER", stack->Id, "Choose an existing emitter in this effect."});
            return false;
        }
        m_Config = std::move(candidate);
        SyncCurrentStackConfig();
        return true;
    }

    bool VFXEditorPanel::ApplyToSelectedEntity() {
        if (!m_SelectedEntity || !m_SelectedEntity.HasComponent<ParticleEmitterComponent>()) return false;
        FinishEmitterEdit();
        std::vector<std::string> errors;
        if (!ValidateParticleEffectDefinition(m_Document.GetDefinition(), errors)) {
            m_Diagnostics.clear();
            for (const auto& error : errors) m_Diagnostics.push_back({VFXDiagnosticSeverity::Error, "INVALID_EFFECT", "", error});
            return false;
        }
        auto& component = m_SelectedEntity.GetComponent<ParticleEmitterComponent>();
        component.SetEffectDefinition(m_Document.GetDefinition());
        component.AssetPath.clear();
        return true;
    }

    Entity VFXEditorPanel::CreateSceneInstance(const glm::vec3& position) {
        if (!m_Scene || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return {};
        FinishEmitterEdit();
        std::vector<std::string> errors;
        if (!ValidateParticleEffectDefinition(m_Document.GetDefinition(), errors)) {
            m_Diagnostics.clear();
            for (const auto& error : errors) m_Diagnostics.push_back({VFXDiagnosticSeverity::Error, "INVALID_EFFECT", "", error});
            return {};
        }
        auto entity = m_Scene->CreateEntity(m_Document.GetDefinition().Name);
        entity.GetComponent<TransformComponent>().Translation = position;
        auto& component = entity.AddComponent<ParticleEmitterComponent>();
        component.SetEffectDefinition(m_Document.GetDefinition());
        if (!m_Document.IsDirty()) component.AssetPath = m_Document.GetFilePath();
        m_SelectedEntity = entity;
        return entity;
    }

    bool VFXEditorPanel::GetPreviewBounds(glm::vec3& minimum, glm::vec3& maximum) const {
        minimum = glm::vec3(std::numeric_limits<float>::max());
        maximum = glm::vec3(std::numeric_limits<float>::lowest());
        bool found = false;
        for (const auto& stack : m_EmitterStacks) {
            if (!stack.Visible || !stack.Enabled) continue;
            const auto transform = stack.GetWorldTransform();
            const auto* system = stack.System.get();
            if (!system || system->GetActiveParticleCount() == 0) {
                minimum = glm::min(minimum, transform.Translation - glm::vec3(0.1f));
                maximum = glm::max(maximum, transform.Translation + glm::vec3(0.1f));
                found = true;
                continue;
            }
            const auto& pool = system->GetPool();
            for (uint32_t i = 0; i < pool.GetActiveCount(); ++i) {
                auto position = pool.GetPositions()[i];
                auto velocity = pool.GetVelocities()[i];
                float radius = std::abs(pool.GetSizes()[i]) * 0.5f;
                if (stack.Config.SimulationSpace == ParticleSimulationSpace::Local) {
                    position = transform.Translation + transform.Rotation * (position * transform.Scale);
                    radius *= std::max({transform.Scale.x, transform.Scale.y, transform.Scale.z});
                    velocity = transform.Rotation * (velocity * transform.Scale);
                }
                const float stretch = stack.Config.Orientation == ParticleOrientation::VelocityAligned
                                          ? std::max(1.0f, glm::length(velocity) * 0.25f) : 1.0f;
                radius *= std::sqrt(1 + stretch * stretch); // Conservative rotated quad, including velocity stretch.
                if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(radius)) continue;
                minimum = glm::min(minimum, position - glm::vec3(radius));
                maximum = glm::max(maximum, position + glm::vec3(radius));
                found = true;
            }
        }
        return found;
    }

    void VFXEditorPanel::FramePreview() {
        glm::vec3 minimum, maximum;
        if (!GetPreviewBounds(minimum, maximum)) {
            m_CameraTarget = glm::vec3(0);
            m_CameraDistance = 5;
            return;
        }
        m_CameraTarget = (minimum + maximum) * 0.5f;
        const float aspect = float(m_ViewportWidth) / float(std::max(1u, m_ViewportHeight));
        const float halfFov = std::atan(std::tan(glm::radians(27.5f)) * std::min(1.0f, aspect));
        m_CameraDistance = std::clamp(glm::length(maximum - minimum) * 0.6f / std::sin(halfFov), 0.5f, 500.0f);
    }

    PerspectiveCamera VFXEditorPanel::CreatePreviewCamera(float width, float height) const {
        PerspectiveCamera camera(55.0f, std::max(1.0f, width) / std::max(1.0f, height), 0.1f, 1000.0f);
        const float yaw = glm::radians(m_CameraYaw), pitch = glm::radians(m_CameraPitch);
        const glm::vec3 position = m_CameraTarget + m_CameraDistance * glm::vec3(
            std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
        camera.SetViewMatrix(glm::lookAt(position, m_CameraTarget, glm::vec3(0, 1, 0)), position);
        return camera;
    }

    bool VFXEditorPanel::Undo() {
        SyncCurrentStackConfig();
        m_Document.CommitTransaction("Edit Emitter Modules");
        if (!m_Document.Undo())
            return false;
        SyncStacksFromDocument();
        return true;
    }
    bool VFXEditorPanel::Redo() {
        SyncCurrentStackConfig();
        m_Document.CommitTransaction("Edit Emitter Modules");
        if (!m_Document.Redo())
            return false;
        SyncStacksFromDocument();
        return true;
    }
    void VFXEditorPanel::SyncCurrentStackConfig() {
        if (auto *stack = GetCurrentEmitterStack()) {
            if (!ParticleEmitterConfigsEqual(stack->Config, m_Config)) {
                stack->Config = m_Config;
                if (stack->System)
                    stack->System->SetConfig(m_Config);
            }
            SyncDocumentFromStacks();
        }
    }

    void VFXEditorPanel::SetEmitterConfig(const ParticleEmitterConfig& config) {
        m_Config = config;
        SyncCurrentStackConfig();
        ResetPreview();
    }

    void VFXEditorPanel::OpenEntityEffect(Entity entity) {
        if (!entity || !entity.HasComponent<ParticleEmitterComponent>())
            return;
        if (m_Document.IsDirty()) {
            m_Diagnostics = {{VFXDiagnosticSeverity::Warning, "UNSAVED_EFFECT", "",
                              "Save or discard the working effect before opening a scene emitter."}};
            return;
        }
        m_AssetDocumentMode = false;
        m_SelectedEntity = {};
        SetSelectedEntity(entity);
    }

    void VFXEditorPanel::SetSelectedEntity(Entity entity) {
        // 1. Guard against re-binding the same entity repeatedly (fixes frame-by-frame selection and preview reset)
        if (m_SelectedEntity == entity && entity) {
            if (entity.HasComponent<ParticleEmitterComponent>()) {
                return;
            }
        }

        // 2. If entity is invalid or does not have ParticleEmitterComponent, record binding and preserve working state
        if (!entity || !entity.HasComponent<ParticleEmitterComponent>()) {
            m_SelectedEntity = entity;
            return;
        }

        m_SelectedEntity = entity;
        if (m_Document.IsDirty() || m_AssetDocumentMode)
            return;
        auto& pec = m_SelectedEntity.GetComponent<ParticleEmitterComponent>();
        m_Document.LoadDefinition(pec.GetEffectDefinition());
        SyncStacksFromDocument();
    }

    void VFXEditorPanel::ResetPreview() {
        m_PlaybackScrubberTime = 0.0f;
        m_Clock.Tick = 0;
        m_Clock.Accumulator = 0.0;
        m_IsSeeking = false;
        for (auto& stack : m_EmitterStacks) {
            stack.Initialize(m_Document.GetDefinition().Seed);
            if (stack.System) {
                stack.System->Reset();
                stack.System->SetEmitting(stack.Enabled && !stack.TriggerOnly);
            }
        }
    }

    void VFXEditorPanel::PlayPreview() {
        m_IsPlaying = true;
        for (auto& stack : m_EmitterStacks) {
            if (stack.System)
                stack.System->SetEmitting(stack.Enabled && !stack.TriggerOnly);
        }
    }

    void VFXEditorPanel::PausePreview() {
        m_IsPlaying = false; // Pause the clock, not the authored emission state used by Step/Seek.
    }

    void VFXEditorPanel::StopPreview() {
        m_IsPlaying = false;
        m_PlaybackScrubberTime = 0.0f;
        m_Clock.Tick = 0;
        m_Clock.Accumulator = 0.0;
        m_IsSeeking = false;
        for (auto& stack : m_EmitterStacks) {
            if (stack.System) {
                stack.System->SetEmitting(false);
                stack.System->Reset();
            }
        }
    }

    void VFXEditorPanel::Step(float stepTime) {
        if (stepTime <= 0.0f) stepTime = static_cast<float>(m_Clock.StepSeconds);
        m_Clock.Tick++;

        for (auto& stack : m_EmitterStacks) {
            if (!stack.System)
                continue;
            const auto t = stack.GetWorldTransform();
            stack.System->SetEmitterTransform(t.Translation, t.Rotation, t.Scale);
            stack.System->GetConfig().Collision.WorldRaycast = {};
            if (m_PhysicsScene &&
                stack.Config.Collision.CollisionMode == ParticleCollisionMode::ScenePhysics) {
                stack.System->GetConfig().Collision.WorldRaycast =
                    [this](const glm::vec3 &origin, const glm::vec3 &direction, float distance,
                           glm::vec3 &point, glm::vec3 &normal) {
                        RayCastHit hit;
                        if (!m_PhysicsScene->CastRay(origin, direction, distance, hit))
                            return false;
                        point = hit.Position;
                        normal = hit.Normal;
                        return true;
                    };
            }
            stack.System->SetEmitting(stack.Enabled && !stack.TriggerOnly);
            stack.System->GetConfig().Looping = m_IsLooping && stack.Config.Looping;
            if (stack.Enabled)
                stack.System->Update(stepTime, t.Translation, t.Rotation, t.Scale);
        }
        DispatchEffectEvents(m_EmitterStacks);
    }

    void VFXEditorPanel::SeekTo(float targetTime) {
        if (targetTime < 0.0f) targetTime = 0.0f;
        if (!std::isfinite(targetTime))
            return;
        float maxDuration = 0.0f;
        for (const auto &stack : m_EmitterStacks)
            maxDuration = std::max(maxDuration, stack.Config.Duration);
        targetTime = std::min(targetTime, std::max(maxDuration, 1.0f));

        uint64_t targetTick = static_cast<uint64_t>(std::round(targetTime / m_Clock.StepSeconds));
        if (targetTick < m_Clock.Tick) {
            ResetPreview();
            m_Clock.Tick = 0;
            m_Clock.Accumulator = 0.0;
        }

        m_TargetSeekTick = targetTick;
        m_IsSeeking = true;

        uint64_t immediateTicks = std::min(m_TargetSeekTick - m_Clock.Tick, static_cast<uint64_t>(300));
        for (uint64_t i = 0; i < immediateTicks; ++i) {
            Step(static_cast<float>(m_Clock.StepSeconds));
        }
        m_PlaybackScrubberTime = static_cast<float>(m_Clock.Tick * m_Clock.StepSeconds);
        if (m_Clock.Tick >= m_TargetSeekTick) {
            m_IsSeeking = false;
        }
    }

    void VFXEditorPanel::TriggerBurst(uint32_t count) {
        if (!m_EmitterStacks.empty() && m_EmitterStacks[m_SelectedStackIndex].System) {
            auto &stack = m_EmitterStacks[m_SelectedStackIndex];
            const auto t = stack.GetWorldTransform();
            stack.System->EmitBurst(count, t.Translation, t.Rotation, t.Scale);
        }
    }

    void VFXEditorPanel::OnUpdate(Timestep ts) {
        if (!m_IsOpen) return;

        if (m_IsSeeking) {
            const uint64_t MAX_SEEK_TICKS_PER_PUMP = 120;
            uint64_t ticksRemaining = (m_TargetSeekTick > m_Clock.Tick) ? (m_TargetSeekTick - m_Clock.Tick) : 0;
            uint64_t ticksToPump = std::min(ticksRemaining, MAX_SEEK_TICKS_PER_PUMP);
            for (uint64_t i = 0; i < ticksToPump; ++i) {
                Step(static_cast<float>(m_Clock.StepSeconds));
            }
            m_PlaybackScrubberTime = static_cast<float>(m_Clock.Tick * m_Clock.StepSeconds);
            if (m_Clock.Tick >= m_TargetSeekTick) {
                m_IsSeeking = false;
            }
            return;
        }

        if (m_IsPlaying) {
            float dt = ts.GetSeconds() * m_Clock.PlaybackSpeed;
            m_Clock.Accumulator += dt;
            unsigned steps = 0;
            while (m_Clock.Accumulator >= m_Clock.StepSeconds && steps++ < 120) {
                m_Clock.Accumulator -= m_Clock.StepSeconds;
                Step(static_cast<float>(m_Clock.StepSeconds));
            }
            m_PlaybackScrubberTime = static_cast<float>(m_Clock.Tick * m_Clock.StepSeconds);
        }
    }

    void VFXEditorPanel::DrawGrid(const glm::mat4& viewProj, float radius) {
        if (!m_ShowGroundGrid || !m_GridShader || m_GridVAO == 0 || !glad_glBindVertexArray || !glad_glDrawArrays) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        m_GridShader->Bind();
        m_GridShader->SetMat4("u_ViewProjection", viewProj);

        glBindVertexArray(m_GridVAO);
        glDrawArrays(GL_LINES, 0, (20 + 1) * 4);
        glBindVertexArray(0);

        glEnable(GL_DEPTH_TEST);
    }

    void VFXEditorPanel::RenderPreview(float width, float height) {
        if (width <= 0 || height <= 0) return;

        u32 reqW = static_cast<u32>(width);
        u32 reqH = static_cast<u32>(height);
        if (reqW > 0 && reqH > 0 && (reqW != m_ViewportWidth || reqH != m_ViewportHeight)) {
            m_ViewportWidth = reqW;
            m_ViewportHeight = reqH;
            if (m_RHIViewport && m_RHIViewport->IsValid()) {
                m_RHIViewport->Resize(m_ViewportWidth, m_ViewportHeight);
            }
            if (m_Framebuffer) {
                m_Framebuffer=Framebuffer::Create({m_ViewportWidth,m_ViewportHeight,1,false});
            }
        }

        if (!m_PreviewScene) {
            m_PreviewScene = std::make_shared<Scene>();
            auto studio=m_PreviewScene->CreateEntity("Preview studio light");
            studio.AddComponent<DirectionalLightComponent>().CastShadows=false;
            m_PreviewEntity = m_PreviewScene->CreateEntity("VFX Preview");
            m_PreviewEntity.AddComponent<ParticleEmitterComponent>();
            for (int axis = 0; axis < 2; ++axis)
                for (int line = -10; line <= 10; ++line) {
                    auto entity = m_PreviewScene->CreateEntity("Preview Grid");
                    auto &tc = entity.GetComponent<TransformComponent>();
                    tc.Translation = axis == 0 ? glm::vec3(line * 0.5f, 0, 0) : glm::vec3(0, 0, line * 0.5f);
                    tc.Scale = axis == 0 ? glm::vec3(0.006f, 0.004f, 10) : glm::vec3(10, 0.004f, 0.006f);
                    auto &material = entity.AddComponent<MaterialComponent>();
                    material.AlbedoColor = line == 0 ? (axis == 0 ? glm::vec4(0.12f, 0.18f, 0.35f, 1)
                                                                  : glm::vec4(0.35f, 0.12f, 0.12f, 1))
                                                     : glm::vec4(0.08f, 0.09f, 0.1f, 1);
                    material.EnableSpecularHighlights = false;
                    material.EnableEnvironmentReflections = false;
                    m_PreviewGrid.push_back(entity);
                }
        }
        for (auto entity : m_PreviewGrid) {
            if (m_ShowGroundGrid && !entity.HasComponent<MeshInstance3D>()) {
                auto& mi = entity.AddComponent<MeshInstance3D>();
                mi.RHIMeshAsset = RHIMeshRegistry::Get().GetCube();
                mi.AssetPath = "builtin://cube";
            }
            if (!m_ShowGroundGrid && entity.HasComponent<MeshInstance3D>())
                entity.RemoveComponent<MeshInstance3D>();
        }
        auto &component = m_PreviewEntity.GetComponent<ParticleEmitterComponent>();
        component.EmitterStacks.clear();
        // This private scene renders the panel-owned systems. It is never ticked and never shares user scene
        // systems.
        for (const auto &stack : m_EmitterStacks) {
            ParticleEmitterStack renderStack(stack.GetDefinition());
            renderStack.Visible = stack.Visible;
            renderStack.System = stack.System;
            renderStack.Texture=stack.Texture;renderStack.NormalMap=stack.NormalMap;
            component.EmitterStacks.push_back(std::move(renderStack));
        }
        component.SyncPrimary();

        auto camera = CreatePreviewCamera(width, height);

        SceneRenderSettings settings;
        settings.EnableShadows = false;
        settings.EnableSkybox = false;
        settings.EnableStudioLightLobes = false;
        settings.EnableParticles = true;
        settings.ClearColor = m_PreviewBackground;

        ViewRequest request;
        request.Kind = RenderViewKind::VFXPreview;
        request.Width = m_ViewportWidth;
        request.Height = m_ViewportHeight;
        request.Camera = camera;
        request.Settings = settings;
        request.Name = "VFXPreview";

        if (RHI::RHIDevice::Get().IsNativeReady() && m_RHIViewport && m_RHIViewport->IsValid()) {
            if (!m_RHIViewport->SetRenderOptions(settings)) return;
            request.ColorTarget = m_RHIViewport->GetColorTexture();
            request.DepthTarget = m_RHIViewport->GetDepthTexture();
            request.ViewTargets = m_RHIViewport->GetTargets();
        } else if (m_Framebuffer) {
            request.GLTargetFBO = m_Framebuffer;
        }

        RenderService::Get().RenderView(*m_PreviewScene, request);
    }

    void VFXEditorPanel::DrawDocumentDialogs() {
            if (m_RequestFileDialog) {
                m_RequestFileDialog = false;
                m_EffectSearch[0] = 0;
                m_EffectFiles.clear();
                std::error_code error;
                const auto root = FileSystem::GetAssetDirectory();
                for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
                     !error && it != end; it.increment(error)) {
                    if (!it->is_regular_file(error)) continue;
                    const auto path = it->path();
                    const auto extension = PathToUtf8(path.extension());
                    if (extension == ".axle_vfx" || (extension == ".axleasset" && AssetDSL::PeekAssetKindFromFile(PathToUtf8(path)) == AssetKind::ParticleEmitter))
                        m_EffectFiles.push_back(path);
                }
                std::sort(m_EffectFiles.begin(), m_EffectFiles.end());
                if (!m_Document.GetFilePath().empty()) strncpy_s(m_FilePath, m_Document.GetFilePath().c_str(), sizeof(m_FilePath) - 1);
                ImGui::OpenPopup("Effect File");
            }
            if (ImGui::BeginPopup("Effect File")) {
                ImGui::SetNextItemWidth(440);
                ImGui::InputText("Asset path", m_FilePath, sizeof(m_FilePath));
                if (m_FileAction == 1) {
                    ImGui::SetNextItemWidth(440);
                    ImGui::InputTextWithHint("##EffectSearch", "Search project effects...", m_EffectSearch, sizeof(m_EffectSearch));
                    ImGuiTextFilter filter(m_EffectSearch);
                    ImGui::BeginChild("Project Effects", ImVec2(440, 220), true);
                    for (const auto& path : m_EffectFiles) {
                        std::error_code error;
                        const auto relative = std::filesystem::relative(path, FileSystem::GetAssetDirectory(), error);
                        const auto label = error ? PathToUtf8(path) : PathToUtf8(relative);
                        if (filter.PassFilter(label.c_str()) && ImGui::Selectable(label.c_str()))
                            strncpy_s(m_FilePath, PathToUtf8(path).c_str(), sizeof(m_FilePath) - 1);
                    }
                    if (m_EffectFiles.empty()) ImGui::TextDisabled("No particle effect assets in this project.");
                    ImGui::EndChild();
                }
                if (ImGui::Button(m_FileAction == 1 ? "Open" : "Save As")) {
                    bool ok = m_FileAction == 1 ? OpenDocument(m_FilePath) : SaveDocumentAs(m_FilePath);
                    if (ok || m_PendingAction)
                        ImGui::CloseCurrentPopup();
                }
                for (const auto& diagnostic : m_Diagnostics) ImGui::TextWrapped("%s", diagnostic.Message.c_str());
                ImGui::EndPopup();
            }
            if (m_PendingAction)
                ImGui::OpenPopup("Unsaved Effect");
            if (ImGui::BeginPopupModal("Unsaved Effect", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Save or discard the current effect before continuing.");
                ImGui::SetNextItemWidth(440);
                ImGui::InputText("Save path", m_FilePath, sizeof(m_FilePath));
                auto proceed = [&]() {
                    const auto action = m_PendingAction;
                    const auto path = m_PendingPath;
                    m_PendingAction = 0;
                    m_DiscardApproved = true;
                    if (action == 1)
                        NewDocument(path);
                    else if (action == 2)
                        OpenDocument(path);
                    else if (action == 3) {
                        m_Document.DiscardChanges();
                        SyncStacksFromDocument();
                        m_DiscardApproved = true;
                        m_IsOpen = false;
                    }
                    ImGui::CloseCurrentPopup();
                };
                if (ImGui::Button("Save and Continue")) {
                    bool saved =
                        m_Document.GetFilePath().empty() ? SaveDocumentAs(m_FilePath) : SaveDocument();
                    if (saved)
                        proceed();
                }
                ImGui::SameLine();
                if (ImGui::Button("Discard"))
                    proceed();
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    m_PendingAction = 0;
                    ImGui::CloseCurrentPopup();
                }
                for (const auto& diagnostic : m_Diagnostics) ImGui::TextWrapped("%s", diagnostic.Message.c_str());
                ImGui::EndPopup();
            }
    }

    void VFXEditorPanel::DrawEmitterStackColumn(float width, float height) {
        ImGui::BeginChild("VFX_EmitterStack_Column", ImVec2(width, height), true);
        {
            std::string docTitle = m_Document.GetDefinition().Name;
            if (m_Document.IsDirty()) docTitle += " *";
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", docTitle.c_str());

            if (ImGui::Button("New", ImVec2(38, 20))) {
                NewDocument();
            }
            ImGui::SameLine();
            if (ImGui::Button("Save", ImVec2(40, 20))) {
                if (m_Document.GetFilePath().empty()) {
                    m_FileAction = 2;
                    m_RequestFileDialog = true;
                } else
                    SaveDocument();
            }
            ImGui::SameLine();
            if (ImGui::Button("Undo", ImVec2(40, 20))) {
                Undo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Redo", ImVec2(40, 20))) {
                Redo();
            }

            if (ImGui::Button("Open...")) {
                m_FileAction = 1;
                m_RequestFileDialog = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save As...")) {
                m_FileAction = 2;
                m_RequestFileDialog = true;
            }
            for (const auto &diag : m_Diagnostics)
                ImGui::TextWrapped("%s", diag.Message.c_str());

            char effectName[256]{};
            strncpy_s(effectName, m_Document.GetDefinition().Name.c_str(), sizeof(effectName) - 1);
            if (ImGui::InputText("Effect", effectName, sizeof(effectName)))
                m_Document.SetEffectName(effectName);
            int seed = static_cast<int>(m_Document.GetDefinition().Seed);
            if (ImGui::InputInt("Seed", &seed)) {
                auto definition = m_Document.GetDefinition();
                definition.Seed = static_cast<uint32_t>(seed);
                m_Document.ReplaceDefinition(definition, "Change Effect Seed");
                SyncStacksFromDocument();
            }
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Emitter Stacks");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24.0f);
            if (ImGui::Button("+", ImVec2(22.0f, 20.0f))) {
                std::string newName = "Emitter " + std::to_string(m_EmitterStacks.size() + 1);
                AddEmitterStack(newName, ParticleEmitterConfig::CreateNeutral());
            }

            ImGui::Separator();
            ImGui::Spacing();

            for (size_t i = 0; i < m_EmitterStacks.size(); ++i) {
                ImGui::PushID(m_EmitterStacks[i].Id.c_str());
                ImGui::BeginGroup();
                bool isSelected = (i == m_SelectedStackIndex);
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.45f, 0.7f, 0.8f));
                }

                // Enabled toggle
                if (ImGui::Checkbox("##Enabled", &m_EmitterStacks[i].Enabled)) {
                    if (m_EmitterStacks[i].System)
                        m_EmitterStacks[i].System->SetEmitting(m_EmitterStacks[i].Enabled &&
                                                               !m_EmitterStacks[i].TriggerOnly);
                    SyncDocumentFromStacks();
                }
                ImGui::SameLine();

                // Selectable stack row
                std::string label = m_EmitterStacks[i].Name.empty() ? ("Emitter " + std::to_string(i)) : m_EmitterStacks[i].Name;
                if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(ImGui::GetContentRegionAvail().x - 30.0f, 22.0f))) {
                    SelectEmitterStack(i);
                }

                if (isSelected) {
                    ImGui::PopStyleColor();
                }

                // Visibility toggle
                ImGui::SameLine();
                bool vis = m_EmitterStacks[i].Visible;
                if (ImGui::SmallButton(vis ? "V" : "H")) {
                    m_EmitterStacks[i].Visible = !vis;
                }

                ImGui::EndGroup();

                // Right click context menu on row
                if (ImGui::BeginPopupContextItem("Emitter Actions")) {
                    if (ImGui::MenuItem("Duplicate Emitter")) {
                        DuplicateEmitterStack(i);
                    }
                    if (ImGui::MenuItem("Move Up", nullptr, false, i > 0)) {
                        ReorderEmitterStack(i, i - 1);
                    }
                    if (ImGui::MenuItem("Move Down", nullptr, false, i + 1 < m_EmitterStacks.size())) {
                        ReorderEmitterStack(i, i + 1);
                    }
                    if (ImGui::MenuItem("Solo Preview"))
                        for (size_t j = 0; j < m_EmitterStacks.size(); ++j)
                            m_EmitterStacks[j].Visible = j == i;
                    if (ImGui::MenuItem("Show All"))
                        for (auto &stack : m_EmitterStacks)
                            stack.Visible = true;
                    if (ImGui::MenuItem("Delete Emitter")) {
                        RemoveEmitterStack(i);
                        ImGui::EndPopup();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Stack statistics
            uint32_t totalParticles = 0;
            for (const auto& s : m_EmitterStacks) {
                if (s.System) totalParticles += s.System->GetActiveParticleCount();
            }
            ImGui::TextDisabled("Total VFX Stats:");
            ImGui::Text("Emitters: %zu", m_EmitterStacks.size());
            ImGui::Text("Total P: %u", totalParticles);
        }
        ImGui::EndChild();
    }

    void VFXEditorPanel::DrawTransportBar() {
        const auto next = [](float width) {
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <
                ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
        };
        if (ImGui::Button(m_IsPlaying ? "Pause" : "Play")) {
            if (m_IsPlaying) PausePreview(); else PlayPreview();
        }
        next(45);
        if (ImGui::Button("Stop")) StopPreview();
        next(50);
        if (ImGui::Button("Restart")) ResetPreview();
        next(45);
        if (ImGui::Button("Step")) {
            PausePreview(); Step();
            m_PlaybackScrubberTime = static_cast<float>(m_Clock.Tick * m_Clock.StepSeconds);
        }
        next(100);
        ImGui::SetNextItemWidth(80);
        ImGui::SliderFloat("##Speed", &m_Clock.PlaybackSpeed, 0.1f, 3, "%.1fx");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview playback speed");
        next(100);
        ImGui::SetNextItemWidth(90);
        ImGui::InputInt("##BurstCount", &m_PreviewBurstCount);
        m_PreviewBurstCount = std::clamp(m_PreviewBurstCount, 1, 20000);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Manual preview burst count; this does not edit Emission");
        next(45);
        ImGui::BeginDisabled(m_EmitterStacks.empty());
        if (ImGui::Button("Emit")) TriggerBurst(static_cast<uint32_t>(m_PreviewBurstCount));
        ImGui::EndDisabled();
        next(120);
        if (ImGui::Button("Preview Settings")) ImGui::OpenPopup("Preview Settings");
        if (ImGui::BeginPopup("Preview Settings")) {
            if (ImGui::Checkbox("Allow authored looping", &m_IsLooping)) ResetPreview();
            ImGui::Checkbox("Ground grid", &m_ShowGroundGrid);
            ImGui::Checkbox("Live bounds", &m_ShowBounds);
            ImGui::ColorEdit3("Background", glm::value_ptr(m_PreviewBackground));
            if (ImGui::Button("Frame Effect")) FramePreview();
            ImGui::SameLine();
            if (ImGui::Button("Reset Camera")) {
                m_CameraDistance = 5; m_CameraPitch = 20; m_CameraYaw = 45;
                m_CameraTarget = glm::vec3(0);
            }
            ImGui::TextDisabled("Preview settings do not modify the asset.");
            ImGui::EndPopup();
        }
        float duration = 1;
        uint32_t count = 0;
        for (const auto& stack : m_EmitterStacks) {
            duration = std::max(duration, stack.Config.Duration);
            if (stack.System) count += stack.System->GetActiveParticleCount();
        }
        ImGui::SetNextItemWidth(-1);
        float time = m_PlaybackScrubberTime;
        if (ImGui::SliderFloat("##Timeline", &time, 0, duration, "%.2f s")) SeekTo(time);
        ImGui::TextDisabled("%u particles | %.2f s%s", count, m_PlaybackScrubberTime, m_IsSeeking ? " | Seeking..." : "");
    }

    bool VFXEditorPanel::BeginModule(ParticleModule module) {
        const char* name = GetParticleModuleName(module);
        bool* enabled = GetParticleModuleToggle(m_Config, module);
        ImGuiTextFilter filter(m_ModuleSearch);
        if (!filter.PassFilter(name) || (m_EnabledModulesOnly && enabled && !*enabled)) return false;
        ImGui::PushID(static_cast<int>(module));
        if (enabled) {
            ImGui::Checkbox("##Enabled", enabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable this module without losing its authored values");
            ImGui::SameLine();
        }
        const bool open = ImGui::CollapsingHeader(name, module == ParticleModule::Main ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (ImGui::BeginPopupContextItem("Module Actions")) {
            if (ImGui::MenuItem("Reset Module")) {
                ResetParticleModule(m_Config, module);
                if (module == ParticleModule::Renderer) if (auto* stack = GetCurrentEmitterStack()) stack->MaterialPath.clear();
                SyncCurrentStackConfig();
                ResetPreview();
            }
            if (ImGui::MenuItem("Copy Module")) {
                m_ModuleClipboard = m_Config;
                if (module == ParticleModule::Renderer) if (auto* stack = GetCurrentEmitterStack()) m_ModuleClipboard.MaterialPath = stack->MaterialPath;
                m_CopiedModule = module;
                m_HasModuleClipboard = true;
            }
            if (ImGui::MenuItem("Paste Module", nullptr, false, m_HasModuleClipboard && m_CopiedModule == module)) {
                auto candidate = m_Config;
                CopyParticleModule(candidate, m_ModuleClipboard, module);
                auto effect = m_Document.GetDefinition();
                if (auto* stack = GetCurrentEmitterStack()) if (auto* emitter = effect.FindEmitter(stack->Id)) emitter->Config = candidate;
                std::vector<std::string> errors;
                if (ValidateSubEmitterGraph(effect, errors)) {
                    m_Config = std::move(candidate);
                    if (module == ParticleModule::Renderer) if (auto* stack = GetCurrentEmitterStack()) stack->MaterialPath = m_Config.MaterialPath;
                    SyncCurrentStackConfig();
                    ResetPreview();
                } else {
                    m_Diagnostics.clear();
                    for (const auto& error : errors) m_Diagnostics.push_back({VFXDiagnosticSeverity::Error, "INVALID_MODULE_PASTE", "", error});
                }
            }
            ImGui::EndPopup();
        }
        if (!open) { ImGui::PopID(); return false; }
        ImGui::BeginDisabled(enabled && !*enabled);
        return true;
    }

    void VFXEditorPanel::EndModule() {
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    void VFXEditorPanel::DrawMainModule() {
        if (BeginModule(ParticleModule::Main)) {
            if (auto *stack = GetCurrentEmitterStack()) {
                ImGui::Checkbox("Trigger Only", &stack->TriggerOnly);
                ImGui::DragFloat3("Emitter Offset", glm::value_ptr(stack->LocalTransform.Translation), 0.01f);
                glm::vec3 rotation = glm::degrees(glm::eulerAngles(stack->LocalTransform.Rotation));
                if (ImGui::DragFloat3("Emitter Rotation", glm::value_ptr(rotation), 0.5f))
                    stack->LocalTransform.Rotation = glm::quat(glm::radians(rotation));
                ImGui::DragFloat3("Emitter Scale", glm::value_ptr(stack->LocalTransform.Scale), 0.01f, 0.001f,
                                  100.0f);
            }
            if (auto* stack = GetCurrentEmitterStack()) {
                char nameBuf[128]{};
                strncpy_s(nameBuf, stack->Name.c_str(), sizeof(nameBuf) - 1);
                if (ImGui::InputText("Emitter Name", nameBuf, sizeof(nameBuf))) {
                    SyncCurrentStackConfig();
                    m_Document.RenameEmitter(stack->Id, nameBuf);
                    // Preserve live systems and migrate any legacy name-based links before the rename.
                    for (auto& source : m_EmitterStacks) {
                        const auto* definition = m_Document.GetDefinition().FindEmitter(source.Id);
                        if (!definition) continue;
                        source.Name = definition->Name;
                        source.Config.Name = definition->Config.Name;
                        source.Config.SubEmitters = definition->Config.SubEmitters;
                        if (source.System) source.System->SetConfig(source.Config);
                    }
                    m_Config = GetCurrentEmitterStack()->Config;
                }
            }

            ImGui::DragFloat("Duration (sec)", &m_Config.Duration, 0.1f, 0.1f, 60.0f, "%.1f s");
            ImGui::Checkbox("Looping", &m_Config.Looping);
            ImGui::SameLine();
            ImGui::Checkbox("Play on Awake", &m_Config.PlayOnAwake);

            const char* spaceModes[] = { "World Space", "Local Space" };
            int currentSpace = (m_Config.SimulationSpace == ParticleSimulationSpace::Local) ? 1 : 0;
            if (ImGui::Combo("Simulation Space", &currentSpace, spaceModes, IM_ARRAYSIZE(spaceModes))) {
                m_Config.SimulationSpace = (currentSpace == 1) ? ParticleSimulationSpace::Local : ParticleSimulationSpace::World;
                SyncCurrentStackConfig();
                ResetPreview();
            }

            int maxP = static_cast<int>(m_Config.MaxParticles);
            if (ImGui::DragInt("Max Particles (Pool)", &maxP, 50, 10, 50000)) {
                m_Config.MaxParticles = static_cast<uint32_t>(std::max(10, maxP));
                SyncCurrentStackConfig();
                ResetPreview();
            }

            ImGui::DragFloatRange2("Start Lifetime", &m_Config.Lifetime.Min, &m_Config.Lifetime.Max, 0.05f, 0.05f, 30.0f, "Min: %.2f", "Max: %.2f");
            ImGui::DragFloatRange2("Start Speed", &m_Config.InitialSpeed.Min, &m_Config.InitialSpeed.Max, 0.1f, 0.0f, 100.0f, "Min: %.1f", "Max: %.1f");

            ImGui::DragFloatRange2("Start Size", &m_Config.StartSize.Min, &m_Config.StartSize.Max, 0.02f, 0.001f, 100);
            ImGui::DragFloatRange2("Start Rotation (rad)", &m_Config.StartRotation.Min, &m_Config.StartRotation.Max, 0.02f, -100, 100);
            ImGui::ColorEdit4("Start Color", glm::value_ptr(m_Config.StartColor));
            ImGui::DragFloat("Gravity Modifier", &m_Config.GravityModifier, 0.05f, -10.0f, 10.0f, "%.2fx");
            ImGui::SliderFloat("Air Drag Damping", &m_Config.Drag, 0.0f, 1.0f, "%.2f");
            EndModule();
        }
    }

    void VFXEditorPanel::DrawEmissionAndBurstsModule() {
        if (BeginModule(ParticleModule::Emission)) {
            ImGui::DragFloat("Rate over Time (P/sec)", &m_Config.EmissionRate, 1.0f, 0.0f, 2000.0f, "%.1f");

            int periodicCount = static_cast<int>(m_Config.BurstCount);
            if (ImGui::DragInt("Periodic Count", &periodicCount, 1, 0, 20000)) m_Config.BurstCount = std::max(0, periodicCount);
            ImGui::DragFloat("Periodic Interval", &m_Config.BurstInterval, 0.01f, 0, 100);
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Bursts (One-shot & Periodic):");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f);
            if (ImGui::SmallButton("+ Add Burst")) {
                m_Config.Bursts.emplace_back(0.0f, 1, 1, 1, 0.0f, 1.0f);
                SyncCurrentStackConfig();
            }

            for (size_t b = 0; b < m_Config.Bursts.size(); ++b) {
                ImGui::PushID(static_cast<int>(b));
                auto& burst = m_Config.Bursts[b];

                ImGui::Separator();
                ImGui::DragFloat("Time", &burst.Time, 0.05f, 0.0f, m_Config.Duration, "%.2fs");
                int minC = static_cast<int>(burst.MinCount);
                int maxC = static_cast<int>(burst.MaxCount);
                if (ImGui::DragIntRange2("Count (Min/Max)", &minC, &maxC, 1, 1, 1000)) {
                    burst.MinCount = static_cast<uint32_t>(std::max(1, minC));
                    burst.MaxCount = static_cast<uint32_t>(std::max(minC, maxC));
                }
                int cycles = static_cast<int>(burst.Cycles);
                if (ImGui::DragInt("Cycles (0=Loop)", &cycles, 1, 0, 100)) {
                    burst.Cycles = static_cast<uint32_t>(std::max(0, cycles));
                }
                ImGui::DragFloat("Interval", &burst.Interval, 0.05f, 0.0f, 10.0f, "%.2fs");
                ImGui::SliderFloat("Probability", &burst.Probability, 0.0f, 1.0f, "%.2f");

                if (ImGui::SmallButton("Delete Burst")) {
                    m_Config.Bursts.erase(m_Config.Bursts.begin() + b);
                    SyncCurrentStackConfig();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            EndModule();
        }
    }

    void VFXEditorPanel::DrawShapeControls() {
        if (BeginModule(ParticleModule::Shape)) {
            const char* shapeTypes[] = { "Point", "Sphere", "Hemisphere", "Box", "Cone", "Line", "Circle", "MeshSurface" };
            int currentShape = static_cast<int>(m_Config.Shape.Type);
            if (ImGui::Combo("Shape Type", &currentShape, shapeTypes, IM_ARRAYSIZE(shapeTypes))) {
                m_Config.Shape.Type = static_cast<ParticleEmitterShapeType>(currentShape);
                SyncCurrentStackConfig();
            }

            ImGui::DragFloat3("Emission Direction", glm::value_ptr(m_Config.Shape.Direction), 0.01f, -1, 1);
            switch (m_Config.Shape.Type) {
                case ParticleEmitterShapeType::Sphere:
                case ParticleEmitterShapeType::Hemisphere:
                case ParticleEmitterShapeType::Circle:
                    ImGui::DragFloat("Radius", &m_Config.Shape.Radius, 0.05f, 0.01f, 50.0f, "%.2f m");
                    ImGui::Checkbox("Surface Shell Only", &m_Config.Shape.SurfaceOnly);
                    break;
                case ParticleEmitterShapeType::Box:
                    ImGui::DragFloat3("Box Half-Extents", glm::value_ptr(m_Config.Shape.BoxExtents), 0.05f, 0.01f, 50.0f, "%.2f m");
                    ImGui::Checkbox("Surface Shell Only", &m_Config.Shape.SurfaceOnly);
                    break;
                case ParticleEmitterShapeType::Cone:
                    ImGui::DragFloat("Cone Angle (Rad)", &m_Config.Shape.ConeAngle, 0.02f, 0.01f, 1.57f, "%.2f rad");
                    ImGui::DragFloat("Cone Base Radius", &m_Config.Shape.Radius, 0.05f, 0.0f, 20.0f, "%.2f m");
                    break;
                case ParticleEmitterShapeType::Line:
                    ImGui::DragFloat("Line Length", &m_Config.Shape.LineLength, 0.05f, 0.01f, 50.0f, "%.2f m");
                    break;
                case ParticleEmitterShapeType::MeshSurface: {
                    if (DrawResourcePicker("Emitter Mesh (.obj)", m_Config.Shape.MeshPath, true)) {
                        m_Config.Shape.MeshSampler.reset();
                        if (!m_Config.Shape.MeshPath.empty()) {
                            m_Config.Shape.MeshSampler = std::make_shared<ParticleMeshSampler>();
                            m_Config.Shape.MeshSampler->LoadFromOBJ(
                                FileSystem::ResolveAssetPath(m_Config.Shape.MeshPath).string());
                        }
                        SyncCurrentStackConfig();
                    }
                    if (m_Config.Shape.MeshSampler && m_Config.Shape.MeshSampler->IsValid()) {
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Mesh Loaded: %zu triangles, Surface Area: %.2f",
                            m_Config.Shape.MeshSampler->GetTriangleCount(),
                            m_Config.Shape.MeshSampler->GetTotalArea());
                    } else if (!m_Config.Shape.MeshPath.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "Warning: Mesh failed to load or has degenerate geometry; falling back to origin point.");
                    } else {
                        ImGui::TextDisabled("No mesh assigned (fallback: emits at origin)");
                    }
                    break;
                }
                default:
                    break;
            }
            EndModule();
        }
    }

    void VFXEditorPanel::DrawCurvesControls() {
        if (BeginModule(ParticleModule::Size)) {
            ImGui::DragFloatRange2("End Size", &m_Config.EndSize.Min, &m_Config.EndSize.Max, 0.02f, 0.001f, 100);
            CurveEditorWidget::DrawCurveEditor("Size Curve", m_Config.SizeOverLifetime, 90);
            EndModule();
        }
        if (BeginModule(ParticleModule::Speed)) {
            CurveEditorWidget::DrawCurveEditor("Speed Multiplier", m_Config.SpeedOverLifetime, 90);
            EndModule();
        }
        if (BeginModule(ParticleModule::Color)) {
            CurveEditorWidget::DrawGradientEditor("Color Gradient", m_Config.ColorOverLifetime, 26);
            EndModule();
        }
        if (BeginModule(ParticleModule::Alpha)) {
            CurveEditorWidget::DrawCurveEditor("Alpha Multiplier", m_Config.AlphaOverLifetime, 90);
            EndModule();
        }
        if (BeginModule(ParticleModule::Rotation)) {
            ImGui::DragFloatRange2("Angular Velocity (rad/s)", &m_Config.AngularVelocity.Min, &m_Config.AngularVelocity.Max, 0.1f, -100, 100);
            EndModule();
        }
    }

    bool VFXEditorPanel::DrawResourcePicker(const char *label, std::string &path, bool mesh) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        bool changed = false;
        auto accepts = [&](const std::filesystem::path &candidate) {
            auto extension = PathToUtf8(candidate.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return mesh ? extension == ".obj"
                        : extension == ".axleasset" &&
                              AssetDSL::PeekAssetKindFromFile(PathToUtf8(candidate)) == AssetKind::Material;
        };
        auto assign = [&](const std::filesystem::path &candidate) {
            const auto resolved = FileSystem::ResolveAssetPath(candidate);
            if (!std::filesystem::is_regular_file(resolved) || !accepts(resolved)) {
                m_Diagnostics = {{VFXDiagnosticSeverity::Error, "RESOURCE_TYPE", label,
                                  "Choose a compatible project resource."}};
                return;
            }
            std::error_code error;
            auto relative = std::filesystem::relative(resolved, FileSystem::GetAssetDirectory(), error);
            path = !error && !relative.empty() && *relative.begin() != ".." ? PathToUtf8(relative)
                                                                            : PathToUtf8(resolved);
            changed = true;
        };
        const auto text = path.empty() ? std::string("Select resource...")
                                       : std::filesystem::path(path).filename().string();
        if (ImGui::Button(text.c_str(), ImVec2(-1, 0)))
            ImGui::OpenPopup("ResourcePicker");
        if (ImGui::BeginDragDropTarget()) {
            if (const auto *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const auto decoded = DecodeAssetPayload(payload->Data, payload->DataSize);
                if (decoded.Success)
                    assign(decoded.Path);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("ResourceActions")) {
            if (ImGui::MenuItem("Clear")) {
                path.clear();
                changed = true;
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("ResourcePicker")) {
            if (ImGui::IsWindowAppearing()) {
                m_ResourceFiles.clear();
                m_ResourceSearch[0] = 0;
                std::error_code ec;
                std::filesystem::recursive_directory_iterator it(
                    FileSystem::GetAssetDirectory(),
                    std::filesystem::directory_options::skip_permission_denied, ec);
                for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                    if (it->is_regular_file(ec) && accepts(it->path()))
                        m_ResourceFiles.push_back(it->path());
                std::sort(m_ResourceFiles.begin(), m_ResourceFiles.end());
            }
            ImGui::InputTextWithHint("##Search", "Search resources...", m_ResourceSearch,
                                     sizeof(m_ResourceSearch));
            ImGui::BeginChild("Results", ImVec2(320, 240));
            for (const auto &file : m_ResourceFiles) {
                if (!m_ResourceSearch[0] || PathToUtf8(file).find(m_ResourceSearch) != std::string::npos)
                    if (ImGui::Selectable(PathToUtf8(file.filename()).c_str())) {
                        assign(file);
                        ImGui::CloseCurrentPopup();
                    }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

    void VFXEditorPanel::DrawTextureFlipbookControls() {
        if (ImGuiTextFilter(m_ModuleSearch).PassFilter("Textures") && ImGui::CollapsingHeader("Textures")) {
            auto* currentStack = GetCurrentEmitterStack();
            if (currentStack) {
                ImGui::TextUnformatted("Albedo Texture");
                m_TexturePicker.TextureSlot("ParticleAlbedo", currentStack->TexturePath,
                                            currentStack->UseTexture, [&](const std::string &path) {
                                                currentStack->TexturePath = path;
                                                currentStack->Texture.reset();
                                            });
                ImGui::TextUnformatted("Normal Map");
                m_TexturePicker.TextureSlot("ParticleNormal", currentStack->NormalMapPath,
                                            currentStack->UseNormalMap, [&](const std::string &path) {
                                                currentStack->NormalMapPath = path;
                                                currentStack->NormalMap.reset();
                                            });
                ImGui::Separator();
            }

        }
        if (BeginModule(ParticleModule::TextureSheet)) {
            int rows = static_cast<int>(m_Config.TextureRows);
            int cols = static_cast<int>(m_Config.TextureCols);
            if (ImGui::DragInt("Flipbook Rows (N)", &rows, 1, 1, 16)) {
                m_Config.TextureRows = static_cast<uint32_t>(std::max(1, rows));
                m_Config.TotalFrames = m_Config.TextureRows * m_Config.TextureCols;
                SyncCurrentStackConfig();
            }
            if (ImGui::DragInt("Flipbook Columns (M)", &cols, 1, 1, 16)) {
                m_Config.TextureCols = static_cast<uint32_t>(std::max(1, cols));
                m_Config.TotalFrames = m_Config.TextureRows * m_Config.TextureCols;
                SyncCurrentStackConfig();
            }
            int frames = static_cast<int>(m_Config.TotalFrames);
            if (ImGui::DragInt("Frame Count", &frames, 1, 1, int(m_Config.TextureRows * m_Config.TextureCols)))
                m_Config.TotalFrames = std::clamp(frames, 1, int(m_Config.TextureRows * m_Config.TextureCols));
            ImGui::DragFloat("Animation Cycles", &m_Config.AnimationCycles, 0.05f, 0.001f, 100);
            if (ImGui::Checkbox("Sub-Frame Smooth Blending", &m_Config.SubFrameBlending)) {
                SyncCurrentStackConfig();
            }
            EndModule();
        }
    }

    void VFXEditorPanel::DrawDynamicsAndForceFields() {
        if (BeginModule(ParticleModule::Noise)) {
            if (m_Config.CurlNoise.Enabled) {
                ImGui::DragFloat("Curl Frequency", &m_Config.CurlNoise.Frequency, 0.05f, 0.05f, 10.0f, "%.2f");
                ImGui::DragFloat("Curl Strength", &m_Config.CurlNoise.Strength, 0.1f, 0.0f, 20.0f, "%.2f m/s²");
                ImGui::DragFloat("Noise Lacunarity", &m_Config.CurlNoise.Lacunarity, 0.01f, 0.01f, 8);
                ImGui::SliderFloat("Noise Persistence", &m_Config.CurlNoise.Persistence, 0, 1);
                ImGui::DragFloat3("Noise Scroll", glm::value_ptr(m_Config.CurlNoise.ScrollSpeed), 0.01f);
                int oct = static_cast<int>(m_Config.CurlNoise.Octaves);
                if (ImGui::SliderInt("Noise Octaves", &oct, 1, 4)) {
                    m_Config.CurlNoise.Octaves = static_cast<uint32_t>(oct);
                }
            }

            EndModule();
        }
        if (BeginModule(ParticleModule::Collision)) {
            if (m_Config.Collision.Enabled) {
                const char* collisionModes[] = { "Planes & Boxes", "Floor Plane", "Scene Physics" };
                int currentMode = static_cast<int>(m_Config.Collision.CollisionMode);
                if (ImGui::Combo("Collision Mode", &currentMode, collisionModes, IM_ARRAYSIZE(collisionModes))) {
                    m_Config.Collision.CollisionMode = static_cast<ParticleCollisionMode>(currentMode);
                    SyncCurrentStackConfig();
                }

                if (m_Config.Collision.CollisionMode == ParticleCollisionMode::Floor) {
                    if (ImGui::DragFloat("Floor Height (Y)", &m_Config.Collision.FloorHeight, 0.05f, -100.0f, 100.0f, "%.2f m")) {
                        SyncCurrentStackConfig();
                    }
                    if (ImGui::Checkbox("Kill on Floor Impact", &m_Config.Collision.FloorKill)) {
                        SyncCurrentStackConfig();
                    }
                } else if (m_Config.Collision.CollisionMode == ParticleCollisionMode::ScenePhysics) {
                    ImGui::TextWrapped("%s", m_PhysicsScene ? "Scene physics queries use the current scene "
                                                              "origin; preview does not advance physics."
                                                            : "No scene physics service bound. Choose Floor "
                                                              "or Planes and Boxes for standalone preview.");
                }

                if (ImGui::SliderFloat("Restitution (Bounce)", &m_Config.Collision.Restitution, 0.0f, 1.0f, "%.2f")) {
                    SyncCurrentStackConfig();
                }
                if (ImGui::SliderFloat("Friction (Tangential)", &m_Config.Collision.Friction, 0.0f, 1.0f, "%.2f")) {
                    SyncCurrentStackConfig();
                }
                if (ImGui::DragFloat("Collision Radius Margin", &m_Config.Collision.ParticleRadius, 0.01f, 0.001f, 1.0f, "%.3f m")) {
                    SyncCurrentStackConfig();
                }
                ImGui::DragFloat("Minimum Bounce Speed", &m_Config.Collision.MinBounceSpeed, 0.01f, 0, 100);
                if (ImGui::Checkbox("Kill on Collision (Global)", &m_Config.Collision.KillOnCollision)) {
                    SyncCurrentStackConfig();
                }

                if (m_Config.Collision.CollisionMode == ParticleCollisionMode::PlanesAndBoxes) {
                    ImGui::Separator();
                    ImGui::Text("Planes (%zu):", m_Config.Collision.Planes.size());
                    ImGui::SameLine();
                    if (ImGui::Button("+ Add Plane")) {
                        m_Config.Collision.Planes.emplace_back(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
                        SyncCurrentStackConfig();
                    }
                    for (size_t p = 0; p < m_Config.Collision.Planes.size(); ++p) {
                        ImGui::PushID(static_cast<int>(p));
                        auto& plane = m_Config.Collision.Planes[p];
                        ImGui::Text("Plane %zu", p + 1);
                        ImGui::SameLine();
                        if (ImGui::Button("Remove")) {
                            m_Config.Collision.Planes.erase(m_Config.Collision.Planes.begin() + p);
                            SyncCurrentStackConfig();
                            ImGui::PopID();
                            break;
                        }
                        if (ImGui::DragFloat3("Normal", glm::value_ptr(plane.Normal), 0.02f, -1.0f, 1.0f)) {
                            if (glm::length(plane.Normal) > 0.0001f) plane.Normal = glm::normalize(plane.Normal);
                            SyncCurrentStackConfig();
                        }
                        if (ImGui::DragFloat("Distance", &plane.Distance, 0.05f)) SyncCurrentStackConfig();
                        ImGui::SliderFloat("Plane Restitution", &plane.Restitution, 0, 1);
                        ImGui::SliderFloat("Plane Friction", &plane.Friction, 0, 1);
                        if (ImGui::Checkbox("Per-Plane Kill", &plane.KillOnCollision)) SyncCurrentStackConfig();
                        ImGui::PopID();
                    }
                }
            }

            ImGui::Spacing();
            if (m_Config.Collision.Enabled &&
                m_Config.Collision.CollisionMode == ParticleCollisionMode::PlanesAndBoxes) {
                ImGui::TextUnformatted("Collision Boxes");
                if (ImGui::Button("Add Box"))
                    m_Config.Collision.Boxes.emplace_back();
                for (size_t i = 0; i < m_Config.Collision.Boxes.size(); ++i) {
                    ImGui::PushID(("CollisionBox" + std::to_string(i)).c_str());
                    auto &box = m_Config.Collision.Boxes[i];
                    ImGui::DragFloat3("Minimum", glm::value_ptr(box.Min), 0.01f);
                    ImGui::DragFloat3("Maximum", glm::value_ptr(box.Max), 0.01f);
                    box.Max = glm::max(box.Max, box.Min + glm::vec3(0.001f));
                    ImGui::Checkbox("Contain Particles", &box.CollideInside);
                    ImGui::SliderFloat("Box Restitution", &box.Restitution, 0, 1);
                    ImGui::SliderFloat("Box Friction", &box.Friction, 0, 1);
                    ImGui::Checkbox("Kill on Box", &box.KillOnCollision);
                    bool remove = ImGui::SmallButton("Remove Box");
                    ImGui::PopID();
                    if (remove) {
                        m_Config.Collision.Boxes.erase(m_Config.Collision.Boxes.begin() + i);
                        break;
                    }
                }
            }
            EndModule();
        }
        if (BeginModule(ParticleModule::Forces)) {
            ImGui::DragFloat3("Constant Acceleration", glm::value_ptr(m_Config.ConstantAcceleration), 0.1f);
            ImGui::DragFloat3("Directional Wind", glm::value_ptr(m_Config.Wind), 0.1f, -50.0f, 50.0f, "%.1f m/s");
            ImGui::DragFloat("Thermal Buoyancy", &m_Config.Buoyancy, 0.1f, -20.0f, 20.0f, "%.1f m/s²");
            ImGui::SliderFloat("Wind Influence", &m_Config.WindInfluence, 0, 1);
            if (ImGui::Button("Add Force Field"))
                m_Config.ForceFields.emplace_back();
            for (size_t i = 0; i < m_Config.ForceFields.size(); ++i) {
                ImGui::PushID(("ForceField" + std::to_string(i)).c_str());
                auto &field = m_Config.ForceFields[i];
                const char *types[] = {"Directional Wind", "Buoyancy", "Point Attractor", "Vortex"};
                int type = static_cast<int>(field.Type);
                if (ImGui::Combo("Field Type", &type, types, 4))
                    field.Type = static_cast<ForceFieldType>(type);
                ImGui::DragFloat3("Position", glm::value_ptr(field.Position), 0.01f);
                ImGui::DragFloat3("Direction / Axis", glm::value_ptr(field.Direction), 0.01f, -1, 1);
                ImGui::DragFloat("Strength", &field.Strength, 0.1f);
                ImGui::DragFloat("Radius", &field.Radius, 0.1f, 0.001f, 10000);
                const char *falloffs[] = {"None", "Linear", "Inverse Square"};
                int falloff = static_cast<int>(field.Falloff);
                if (ImGui::Combo("Falloff", &falloff, falloffs, 3))
                    field.Falloff = static_cast<ForceFieldFalloff>(falloff);
                ImGui::DragFloat("Falloff Exponent", &field.FalloffExponent, 0.01f, 0, 10);
                if (field.Type == ForceFieldType::Vortex) {
                    ImGui::DragFloat("Radial Suction", &field.RadialSuction, 0.01f);
                    ImGui::DragFloat("Axial Lift", &field.AxialLift, 0.01f);
                }
                if (field.Type == ForceFieldType::DirectionalWind) {
                    ImGui::SliderFloat("Field Wind Influence", &field.WindInfluence, 0, 1);
                    ImGui::DragFloat("Gust Frequency", &field.GustFrequency, 0.01f, 0, 100);
                    ImGui::DragFloat("Gust Amplitude", &field.GustAmplitude, 0.01f, 0, 100);
                }
                bool remove = ImGui::SmallButton("Remove Field");
                ImGui::PopID();
                if (remove) {
                    m_Config.ForceFields.erase(m_Config.ForceFields.begin() + i);
                    break;
                }
            }
            EndModule();
        }
    }

    void VFXEditorPanel::DrawSubEmittersModule() {
        if (BeginModule(ParticleModule::SubEmitters)) {
            ImGui::TextDisabled("Spawn child particle systems on particle events:");
            if (ImGui::Button("Add Link...")) ImGui::OpenPopup("Choose Child Emitter");
            if (ImGui::BeginPopup("Choose Child Emitter")) {
                bool hasCandidate = false;
                for (size_t i = 0; i < m_EmitterStacks.size(); ++i) {
                    if (i == m_SelectedStackIndex) continue;
                    hasCandidate = true;
                    if (ImGui::Selectable((m_EmitterStacks[i].Name + "##" + m_EmitterStacks[i].Id).c_str()))
                        SetSubEmitterTarget(m_Config.SubEmitters.size(), m_EmitterStacks[i].Id);
                }
                if (!hasCandidate) ImGui::TextDisabled("Add another emitter first.");
                ImGui::EndPopup();
            }

            for (size_t s = 0; s < m_Config.SubEmitters.size(); ++s) {
                ImGui::PushID(static_cast<int>(s));
                auto& sub = m_Config.SubEmitters[s];

                ImGui::Separator();
                const char* triggerNames[] = { "Birth", "Collision", "Death" };
                int currentTrig = static_cast<int>(sub.Trigger);
                if (ImGui::Combo("Event Trigger", &currentTrig, triggerNames, IM_ARRAYSIZE(triggerNames))) {
                    sub.Trigger = static_cast<SubEmitterTrigger>(currentTrig);
                    SyncCurrentStackConfig();
                }

                // Target Emitter Selection Combo
                const std::string currentTarget = sub.TargetEmitterName;
                const auto* target = m_Document.GetDefinition().FindEmitter(currentTarget);
                if (ImGui::BeginCombo("Target Emitter", target ? target->Name.c_str() : "<Missing Target>")) {
                    for (size_t i = 0; i < m_EmitterStacks.size(); ++i) {
                        if (i == m_SelectedStackIndex) continue; // Exclude self
                        const auto& targetName = m_EmitterStacks[i].Name;
                        bool isSelected = (currentTarget == m_EmitterStacks[i].Id);
                        if (ImGui::Selectable((targetName + "##" + m_EmitterStacks[i].Id).c_str(), isSelected)) {
                            // Keep the reference to this vector element valid until the rest of the row is drawn.
                            auto candidate = m_Document.GetDefinition();
                            candidate.FindEmitter(GetCurrentEmitterStack()->Id)->Config = m_Config;
                            candidate.FindEmitter(GetCurrentEmitterStack()->Id)->Config.SubEmitters[s].TargetEmitterName = m_EmitterStacks[i].Id;
                            std::vector<std::string> errors;
                            if (ValidateSubEmitterGraph(candidate, errors)) sub.TargetEmitterName = m_EmitterStacks[i].Id;
                            else {
                                m_Diagnostics.clear();
                                for (const auto& error : errors) m_Diagnostics.push_back({VFXDiagnosticSeverity::Error, "INVALID_SUB_EMITTER", "", error});
                            }
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                // Check for invalid target or self-reference
                auto* curStack = GetCurrentEmitterStack();
                if (curStack && sub.TargetEmitterName == curStack->Id) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: Self-referencing sub-emitter target!");
                } else if (!sub.TargetEmitterName.empty()) {
                    bool targetExists = false;
                    for (const auto& stack : m_EmitterStacks) {
                        if (stack.Id == sub.TargetEmitterName || stack.Name == sub.TargetEmitterName) {
                            targetExists = true;
                            break;
                        }
                    }
                    if (!targetExists) {
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Warning: Target emitter not found!");
                    }
                }

                int count = static_cast<int>(sub.EmitCount);
                if (ImGui::DragInt("Spawn Count", &count, 1, 1, 200)) {
                    sub.EmitCount = static_cast<uint32_t>(std::max(1, count));
                    SyncCurrentStackConfig();
                }
                if (ImGui::SliderFloat("Trigger Probability", &sub.Probability, 0.0f, 1.0f, "%.2f")) {
                    SyncCurrentStackConfig();
                }

                if (ImGui::Checkbox("Inherit Color", &sub.InheritColor)) {
                    SyncCurrentStackConfig();
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Inherit Velocity", &sub.InheritVelocity)) {
                    SyncCurrentStackConfig();
                }

                if (ImGui::SmallButton("Delete Sub-Emitter")) {
                    m_Config.SubEmitters.erase(m_Config.SubEmitters.begin() + s);
                    SyncCurrentStackConfig();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            EndModule();
        }
    }

    void VFXEditorPanel::DrawShadingAndBillboarding() {
        if (BeginModule(ParticleModule::Renderer)) {
            const char* orientations[] = { "Spherical (Camera-Facing)", "Cylindrical (Axial-Y)", "Velocity-Aligned (Stretched)" };
            int currentOrient = static_cast<int>(m_Config.Orientation);
            if (ImGui::Combo("Orientation Mode", &currentOrient, orientations, IM_ARRAYSIZE(orientations))) {
                m_Config.Orientation = static_cast<ParticleOrientation>(currentOrient);
            }

            const char* blendModes[] = { "Additive", "Alpha Blend", "Multiply", "Cutout (Alpha Tested)" };
            int currentBlend = static_cast<int>(m_Config.BlendMode);
            if (ImGui::Combo("Blend Mode", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                m_Config.BlendMode = static_cast<ParticleBlendMode>(currentBlend);
            }

            ImGui::Checkbox("Lighting", &m_Config.LightingEnabled);
            if (m_Config.LightingEnabled) {
                ImGui::SliderFloat("Wrap Lighting", &m_Config.WrapLighting, 0, 1);
                ImGui::SliderFloat("Scattering", &m_Config.ScatteringFactor, 0, 1);
                ImGui::SliderFloat("Normal Strength", &m_Config.NormalMapStrength, 0, 2);
            }
            ImGui::Checkbox("Near Fade", &m_Config.NearFadeEnabled);
            if (m_Config.NearFadeEnabled)
                ImGui::DragFloatRange2("Near Fade Range", &m_Config.NearFadeStart, &m_Config.NearFadeEnd,
                                       0.01f, 0, 1000);
            ImGui::Checkbox("Far Fade", &m_Config.FarFadeEnabled);
            if (m_Config.FarFadeEnabled)
                ImGui::DragFloatRange2("Far Fade Range", &m_Config.FarFadeStart, &m_Config.FarFadeEnd, 0.1f,
                                       0, 10000);
            ImGui::DragFloat("Softness Distance", &m_Config.SoftnessDistance, 0.05f, 0.0f, 5.0f, "%.2f m");

            if (auto *stack = GetCurrentEmitterStack()) {
                if (DrawResourcePicker("Material", stack->MaterialPath, false))
                    m_Config.MaterialPath = stack->MaterialPath;
                const auto settings =
                    ParticleMaterialAdapter::Resolve(m_Config, stack->GetDefinition().Resources);
                for (const auto &diagnostic : settings.Diagnostics)
                    ImGui::TextWrapped("%s", diagnostic.Message.c_str());
            }
            EndModule();
        }
    }

    void VFXEditorPanel::DrawSceneActions() {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (m_SelectedEntity && m_SelectedEntity.HasComponent<ParticleEmitterComponent>()) {
            auto& pec = m_SelectedEntity.GetComponent<ParticleEmitterComponent>();
            if (!pec.AssetPath.empty()) {
                if (ImGui::Button("Make Unique (Detach Shared Asset)", ImVec2(-1.0f, 26.0f))) {
                    pec.AssetPath.clear();
                }
            }
            if (ImGui::Button("Apply Working Copy", ImVec2(-1, 28))) ApplyToSelectedEntity();
        }
        if (m_Scene) {
            ImGui::DragFloat3("Scene Position", glm::value_ptr(m_SpawnPosition), 0.1f);
            if (ImGui::Button("Create Scene Instance", ImVec2(-1, 28))) CreateSceneInstance(m_SpawnPosition);
        }
    }

    void VFXEditorPanel::DrawShurikenInspector(float width, float height) {
        ImGui::BeginChild("VFX_Shuriken_Inspector", ImVec2(width, height), true);
        {
            if (m_EmitterStacks.empty()) {
                ImGui::TextWrapped("This effect has no emitters. Add an emitter to start authoring.");
                if (ImGui::Button("Add Emitter", ImVec2(-1, 28))) AddEmitterStack("Emitter");
                DrawSceneActions();
                ImGui::EndChild();
                return;
            }
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##ModuleSearch", "Search modules...", m_ModuleSearch, sizeof(m_ModuleSearch));
            ImGui::Checkbox("Enabled modules only", &m_EnabledModulesOnly);
            m_Document.BeginTransaction();
            ImGui::PushID(GetCurrentEmitterStack() ? GetCurrentEmitterStack()->Id.c_str() : "NoEmitter");
            DrawMainModule();
            DrawEmissionAndBurstsModule();
            DrawShapeControls();
            DrawCurvesControls();
            DrawTextureFlipbookControls();
            DrawDynamicsAndForceFields();
            DrawSubEmittersModule();
            DrawShadingAndBillboarding();
            DrawSceneActions();
            SyncCurrentStackConfig();
            ImGui::PopID();
            if (!ImGui::IsAnyItemActive())
                m_Document.CommitTransaction("Edit Emitter Modules");
        }
        ImGui::EndChild();
    }

    void VFXEditorPanel::DrawPreviewViewport(float width, float height) {
            ImGui::BeginChild("VFX_Viewport_Child", ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                avail.x = std::max(1.0f, avail.x); avail.y = std::max(1.0f, avail.y);
                const ImVec2 origin = ImGui::GetCursorScreenPos();

                uint64_t rhiTexID = 0;
                if (RHI::RHIDevice::Get().IsNativeReady() && m_RHIViewport && m_RHIViewport->IsValid()) {
                    uint32_t reqW = (uint32_t)avail.x;
                    uint32_t reqH = (uint32_t)avail.y;
                    if (reqW > 0 && reqH > 0 && (reqW != m_ViewportWidth || reqH != m_ViewportHeight)) {
                        m_ViewportWidth = reqW;
                        m_ViewportHeight = reqH;
                        m_RHIViewport->Resize(m_ViewportWidth, m_ViewportHeight);
                    }
                    RenderPreview(avail.x, avail.y);
                    rhiTexID = m_RHIViewport->GetImTextureID();
                } else if (m_Framebuffer) {
                    uint32_t reqW = (uint32_t)avail.x;
                    uint32_t reqH = (uint32_t)avail.y;
                    if (reqW > 0 && reqH > 0 && (reqW != m_ViewportWidth || reqH != m_ViewportHeight)) {
                        m_ViewportWidth = reqW;
                        m_ViewportHeight = reqH;
                        m_Framebuffer=Framebuffer::Create({m_ViewportWidth,m_ViewportHeight,1,false});
                    }
                    RenderPreview(avail.x, avail.y);
                }

                auto* draw = ImGui::GetWindowDrawList();
                const ImVec2 end(origin.x + avail.x, origin.y + avail.y);
                if (rhiTexID != 0) {
                    glm::vec2 uv0 = m_RHIViewport->GetUV0();
                    glm::vec2 uv1 = m_RHIViewport->GetUV1();
                    draw->AddImage((ImTextureID)(intptr_t)rhiTexID, origin, end, ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
                } else if (m_Framebuffer && m_Framebuffer->GetColorAttachmentRendererID() != 0) {
                    ImTextureID texID = (ImTextureID)(intptr_t)m_Framebuffer->GetColorAttachmentRendererID();
                    if(m_DeferredRelease)m_DeferredRelease([owner=m_Framebuffer]{});
                    draw->AddImage(texID, origin, end, ImVec2(0, 1), ImVec2(1, 0));
                } else {
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    ImVec2 p1 = ImVec2(p0.x + avail.x, p0.y + avail.y);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(p0, p1, IM_COL32(18, 20, 24, 255));
                    drawList->AddRect(p0, p1, IM_COL32(40, 44, 52, 255));

                    const char *txt = "VFX preview renderer unavailable";
                    ImVec2 txtSize = ImGui::CalcTextSize(txt);
                    ImVec2 txtPos = ImVec2(p0.x + (avail.x - txtSize.x) * 0.5f, p0.y + 12.0f);
                    drawList->AddText(txtPos, IM_COL32(100, 200, 255, 255), txt);
                }

                // Own preview drags so ImGui does not treat the image as window background.
                // Use the same input surface for both renderers and the unavailable fallback.
                ImGui::InvisibleButton("##PreviewInput", avail, ImGuiButtonFlags_MouseButtonLeft
                    | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
                m_ViewportHovered = ImGui::IsItemHovered();
                m_ViewportFocused = ImGui::IsItemFocused();
                if (ImGui::IsItemActivated()) {
                    for (int button = 0; button < 3; ++button) {
                        if (ImGui::IsMouseClicked(button)) { m_PreviewDragButton = button; break; }
                    }
                }
                if (!ImGui::IsItemActive()) m_PreviewDragButton = -1;

                auto& io = ImGui::GetIO();
                if (m_ViewportHovered) {
                    if (io.MouseWheel != 0) m_CameraDistance = glm::clamp(m_CameraDistance * std::pow(0.9f, io.MouseWheel), 0.1f, 500.0f);
                    if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.WantTextInput) FramePreview();
                }
                if (m_PreviewDragButton >= 0) {
                    if (!ImGui::IsMouseDown(m_PreviewDragButton)) m_PreviewDragButton = -1;
                    else if (m_PreviewDragButton == ImGuiMouseButton_Left) {
                        m_CameraYaw += io.MouseDelta.x * 0.5f;
                        m_CameraPitch = glm::clamp(m_CameraPitch - io.MouseDelta.y * 0.5f, -89.0f, 89.0f);
                    } else {
                        const auto camera = CreatePreviewCamera(avail.x, avail.y);
                        const auto inverseView = glm::inverse(camera.GetViewMatrix());
                        const float unitsPerPixel = 2 * m_CameraDistance * std::tan(glm::radians(27.5f)) / avail.y;
                        m_CameraTarget += (-glm::vec3(inverseView[0]) * io.MouseDelta.x + glm::vec3(inverseView[1]) * io.MouseDelta.y) * unitsPerPixel;
                    }
                }
                if (m_ShowBounds) {
                    glm::vec3 minimum, maximum;
                    if (GetPreviewBounds(minimum, maximum)) {
                        const auto matrix = CreatePreviewCamera(avail.x, avail.y).GetViewProjectionMatrix();
                        glm::vec4 corners[8];
                        for (int i = 0; i < 8; ++i) corners[i] = matrix * glm::vec4(
                            (i & 1) ? maximum.x : minimum.x, (i & 2) ? maximum.y : minimum.y,
                            (i & 4) ? maximum.z : minimum.z, 1);
                        const auto project = [&](const glm::vec4& point) {
                            return ImVec2(origin.x + (point.x / point.w + 1) * avail.x * 0.5f,
                                          origin.y + (1 - point.y / point.w) * avail.y * 0.5f);
                        };
                        for (int i = 0; i < 8; ++i) for (int bit = 1; bit <= 4; bit *= 2) {
                            const int j = i ^ bit;
                            if (i < j && corners[i].w > 0.01f && corners[j].w > 0.01f)
                                draw->AddLine(project(corners[i]), project(corners[j]), IM_COL32(100, 205, 240, 220));
                        }
                    }
                }
                draw->AddText(ImVec2(origin.x + 8, origin.y + avail.y - ImGui::GetTextLineHeight() - 6),
                              IM_COL32(180, 185, 190, 220), "LMB orbit | RMB/MMB pan | Wheel zoom | F frame");
            }
            ImGui::EndChild();
    }

    void VFXEditorPanel::OnImGuiRender(bool* p_open) {
        if (p_open && !*p_open && m_IsOpen) {
            FinishEmitterEdit();
            if (m_Document.IsDirty()) { *p_open = true; m_PendingAction = 3; }
        }
        if (p_open) m_IsOpen = *p_open;
        if (!m_IsOpen) return;
        m_DiscardApproved = false;

        m_ViewportHovered = false;
        ImGui::SetNextWindowSize(ImVec2(1280, 780), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(420, 360), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::Begin("VFX Editor", &m_IsOpen, ImGuiWindowFlags_NoScrollbar)) {
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                const auto& io = ImGui::GetIO();
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                    if (m_Document.GetFilePath().empty() || io.KeyShift) {
                        m_FileAction = 2; m_RequestFileDialog = true;
                    } else SaveDocument();
                }
                if (io.KeyCtrl && !io.WantTextInput) {
                    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) Redo(); else Undo(); }
                    if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
                    if (ImGui::IsKeyPressed(ImGuiKey_N, false)) NewDocument();
                }
            }
            DrawTransportBar();
            ImGui::Separator();
            const float width = ImGui::GetContentRegionAvail().x;
            const float height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
            if (width >= 1050) {
                if (ImGui::BeginTable("VFX Workspace", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                    ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthFixed, 240);
                    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 1);
                    ImGui::TableSetupColumn("Modules", ImGuiTableColumnFlags_WidthStretch, 1);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); DrawEmitterStackColumn(0, height);
                    ImGui::TableSetColumnIndex(1); DrawPreviewViewport(0, height);
                    ImGui::TableSetColumnIndex(2); DrawShurikenInspector(0, height);
                    ImGui::EndTable();
                }
            } else if (ImGui::BeginTabBar("VFX Workspace Tabs")) {
                if (ImGui::BeginTabItem("Effect")) {
                    DrawEmitterStackColumn(0, std::max(1.0f, ImGui::GetContentRegionAvail().y)); ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Preview")) {
                    DrawPreviewViewport(0, std::max(1.0f, ImGui::GetContentRegionAvail().y)); ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Modules")) {
                    DrawShurikenInspector(0, std::max(1.0f, ImGui::GetContentRegionAvail().y)); ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            DrawDocumentDialogs();
        }
        ImGui::End();

        if (!m_IsOpen) { FinishEmitterEdit(); m_PreviewDragButton = -1; }
        if (!m_IsOpen && m_Document.IsDirty() && !m_DiscardApproved && m_PendingAction != 3) {
            m_IsOpen = true;
            m_PendingAction = 3;
        }
        if (p_open) *p_open = m_IsOpen;
    }

}
