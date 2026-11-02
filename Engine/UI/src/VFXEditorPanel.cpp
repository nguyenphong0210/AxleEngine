#include <Axle/VFXEditorPanel.hpp>
#include <Axle/CurveEditorWidget.hpp>
#include <Axle/Components.hpp>
#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Scene.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/ParticleEmitterSerializer.hpp>
#include <imgui.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>

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
        // Initialize default emitter preset: Fire
        m_Config = ParticleEmitterConfig::CreateFire();
        m_Config.MaxParticles = 1000;

        ParticleEmitterStack baseStack("Fire Base", m_Config);
        baseStack.Initialize();
        m_EmitterStacks.push_back(std::move(baseStack));
        m_SelectedStackIndex = 0;


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

    void VFXEditorPanel::InitRHI(ImGuiLayer* imguiLayer) {
        m_ImGuiLayer = imguiLayer;
        if (!m_ImGuiLayer) return;

        if (RHI::RHIDevice::Get().IsInitialized()) {
            if (!m_RHIViewport) {
                m_RHIViewport = std::make_unique<RHIViewportDescriptor>();
            }
            m_RHIViewport->Init(m_ImGuiLayer, m_ViewportWidth, m_ViewportHeight, RHI::RHITextureFormat::RGBA8_UNORM, true);

            if (!m_RHIParticleRenderer) {
                m_RHIParticleRenderer = std::make_unique<RHIParticleRenderer>();
                m_RHIParticleRenderer->Init(RHI::RHIDevice::Get());
            }
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
        m_Config.Name = def.Name;
        m_EmitterStacks.clear();
        for (const auto& emitter : def.Emitters) {
            ParticleEmitterStack stack(emitter.Name, emitter.Config);
            stack.Enabled = emitter.Enabled;
            stack.Visible = true;
            stack.TexturePath = emitter.Resources.TexturePath;
            stack.NormalMapPath = emitter.Resources.NormalMapPath;
            stack.MaterialPath = emitter.Resources.MaterialPath;
            stack.Initialize();
            m_EmitterStacks.push_back(std::move(stack));
        }
        if (m_EmitterStacks.empty()) {
            ParticleEmitterStack base("Base Emitter", m_Config);
            base.Initialize();
            m_EmitterStacks.push_back(std::move(base));
        }
        m_SelectedStackIndex = 0;
        m_Config = m_EmitterStacks[0].Config;
        ResetPreview();
    }

    void VFXEditorPanel::SyncDocumentFromStacks() {
        SyncCurrentStackConfig();
        auto& def = m_Document.GetDefinition();
        def.Name = m_Config.Name.empty() ? "ParticleEffect" : m_Config.Name;
        def.Emitters.clear();
        for (size_t i = 0; i < m_EmitterStacks.size(); ++i) {
            const auto& stack = m_EmitterStacks[i];
            EmitterDefinition edef;
            edef.Id = "emitter-" + std::to_string(i + 1);
            edef.Name = stack.Name;
            edef.Enabled = stack.Enabled;
            edef.Config = stack.Config;
            edef.Resources.TexturePath = stack.TexturePath;
            edef.Resources.NormalMapPath = stack.NormalMapPath;
            edef.Resources.MaterialPath = stack.MaterialPath;
            edef.Resources.UseTexture = !stack.TexturePath.empty();
            edef.Resources.UseNormalMap = !stack.NormalMapPath.empty();
            def.Emitters.push_back(std::move(edef));
        }
    }

    void VFXEditorPanel::NewDocument(const std::string& name) {
        m_Document.NewEffect(name);
        SyncStacksFromDocument();
    }

    bool VFXEditorPanel::OpenDocument(const std::string& filePath) {
        std::vector<VFXDiagnostic> diags;
        if (m_Document.OpenEffect(filePath, diags)) {
            SyncStacksFromDocument();
            return true;
        }
        return false;
    }

    bool VFXEditorPanel::SaveDocument() {
        SyncDocumentFromStacks();
        std::vector<VFXDiagnostic> diags;
        return m_Document.Save(diags);
    }

    bool VFXEditorPanel::SaveDocumentAs(const std::string& filePath) {
        SyncDocumentFromStacks();
        std::vector<VFXDiagnostic> diags;
        return m_Document.SaveAs(filePath, diags);
    }

    ParticleEmitterStack* VFXEditorPanel::GetCurrentEmitterStack() {
        if (m_SelectedStackIndex < m_EmitterStacks.size()) {
            return &m_EmitterStacks[m_SelectedStackIndex];
        }
        return nullptr;
    }

    void VFXEditorPanel::SelectEmitterStack(size_t index) {
        if (index < m_EmitterStacks.size()) {
            SyncCurrentStackConfig();
            m_SelectedStackIndex = index;
            m_Config = m_EmitterStacks[index].Config;
        }
    }

    void VFXEditorPanel::AddEmitterStack(const std::string& name, const ParticleEmitterConfig& config) {
        ParticleEmitterStack stack(name, config);
        stack.Initialize();
        m_EmitterStacks.push_back(std::move(stack));
        SelectEmitterStack(m_EmitterStacks.size() - 1);
        ResetPreview();
    }

    void VFXEditorPanel::RemoveEmitterStack(size_t index) {
        if (m_EmitterStacks.size() > 1 && index < m_EmitterStacks.size()) {
            m_EmitterStacks.erase(m_EmitterStacks.begin() + index);
            if (m_SelectedStackIndex >= m_EmitterStacks.size()) {
                m_SelectedStackIndex = m_EmitterStacks.size() - 1;
            }
            SelectEmitterStack(m_SelectedStackIndex);
            ResetPreview();
        }
    }

    void VFXEditorPanel::SyncCurrentStackConfig() {
        if (m_SelectedStackIndex < m_EmitterStacks.size()) {
            m_EmitterStacks[m_SelectedStackIndex].Config = m_Config;
            if (m_EmitterStacks[m_SelectedStackIndex].System) {
                m_EmitterStacks[m_SelectedStackIndex].System->SetConfig(m_Config);
            }
        }
    }

    void VFXEditorPanel::SetEmitterConfig(const ParticleEmitterConfig& config) {
        m_Config = config;
        SyncCurrentStackConfig();
        ResetPreview();
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
        auto& pec = m_SelectedEntity.GetComponent<ParticleEmitterComponent>();
        m_Config = pec.Config;
        m_EmitterStacks.clear();
        if (!pec.EmitterStacks.empty()) {
            for (const auto& stack : pec.EmitterStacks) {
                ParticleEmitterStack previewStack(stack.Name, stack.Config);
                previewStack.Enabled = stack.Enabled;
                previewStack.Visible = stack.Visible;
                previewStack.TexturePath = stack.TexturePath;
                previewStack.NormalMapPath = stack.NormalMapPath;
                previewStack.MaterialPath = stack.MaterialPath;
                m_EmitterStacks.push_back(std::move(previewStack));
            }
            m_SelectedStackIndex = 0;
        } else {
            ParticleEmitterStack baseStack(m_Config.Name.empty() ? "Base Emitter" : m_Config.Name, m_Config);
            baseStack.Initialize();
            m_EmitterStacks.push_back(std::move(baseStack));
            m_SelectedStackIndex = 0;
        }
        ResetPreview();
    }

    void VFXEditorPanel::ResetPreview() {
        m_PlaybackScrubberTime = 0.0f;
        m_Clock.Tick = 0;
        m_Clock.Accumulator = 0.0;
        m_IsSeeking = false;
        for (auto& stack : m_EmitterStacks) {
            stack.Initialize();
            if (stack.System) {
                stack.System->Reset();
                if (m_IsPlaying && stack.Enabled) stack.System->SetEmitting(true);
            }
        }
    }

    void VFXEditorPanel::PlayPreview() {
        m_IsPlaying = true;
        for (auto& stack : m_EmitterStacks) {
            if (stack.System && stack.Enabled) stack.System->SetEmitting(true);
        }
    }

    void VFXEditorPanel::PausePreview() {
        m_IsPlaying = false;
        for (auto& stack : m_EmitterStacks) {
            if (stack.System) stack.System->SetEmitting(false);
        }
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

        float maxDuration = m_Config.Duration > 0.0f ? m_Config.Duration : 5.0f;
        for (const auto& stack : m_EmitterStacks) {
            if (stack.Config.Duration > maxDuration) {
                maxDuration = stack.Config.Duration;
            }
        }

        float currentTime = static_cast<float>(m_Clock.Tick * m_Clock.StepSeconds);
        if (m_IsLooping && maxDuration > 0.0f && currentTime >= maxDuration) {
            m_Clock.Tick = 0;
        }

        for (auto& stack : m_EmitterStacks) {
            if (stack.Enabled && stack.System) {
                stack.System->Update(stepTime, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            }
        }
        DispatchEffectEvents(m_EmitterStacks);
    }

    void VFXEditorPanel::SeekTo(float targetTime) {
        if (targetTime < 0.0f) targetTime = 0.0f;
        float maxDuration = m_Config.Duration > 0.0f ? m_Config.Duration : 10.0f;
        if (targetTime > maxDuration) targetTime = maxDuration;

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
            m_EmitterStacks[m_SelectedStackIndex].System->EmitBurst(count);
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
            while (m_Clock.Accumulator >= m_Clock.StepSeconds) {
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

    void VFXEditorPanel::RenderPreview3D(float width, float height) {
        if (width <= 0 || height <= 0 || !m_Framebuffer || !glad_glBindFramebuffer || !glad_glClear) return;

        u32 w = static_cast<u32>(width);
        u32 h = static_cast<u32>(height);
        if (w != m_ViewportWidth || h != m_ViewportHeight) {
            m_ViewportWidth = w;
            m_ViewportHeight = h;
            m_Framebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
        }

        m_Framebuffer->Bind();
        glViewport(0, 0, m_ViewportWidth, m_ViewportHeight);
        glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);
        PerspectiveCamera camera(55.0f, aspect, 0.1f, 1000.0f);

        float yawRad = glm::radians(m_CameraYaw);
        float pitchRad = glm::radians(m_CameraPitch);
        glm::vec3 camPos;
        camPos.x = m_CameraTarget.x + m_CameraDistance * std::cos(pitchRad) * std::sin(yawRad);
        camPos.y = m_CameraTarget.y + m_CameraDistance * std::sin(pitchRad);
        camPos.z = m_CameraTarget.z + m_CameraDistance * std::cos(pitchRad) * std::cos(yawRad);

        glm::mat4 view = glm::lookAt(camPos, m_CameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        camera.SetViewMatrix(view, camPos);

        glm::mat4 viewProj = camera.GetViewProjectionMatrix();

        // 1. Draw ground grid & reference axes
        DrawGrid(viewProj);

        // 2. Render all visible emitter stacks
        if (m_ParticleRenderer) {
            m_ParticleRenderer->Begin(camera, m_Framebuffer, 0.1f, 1000.0f);
            for (auto& stack : m_EmitterStacks) {
                if (stack.Visible && stack.Enabled && stack.System) {
                    if (!stack.Texture && !stack.TexturePath.empty()) {
                        auto resolved = FileSystem::ResolveAssetPath(stack.TexturePath);
                        if (std::filesystem::exists(resolved)) {
                            stack.Texture = Texture2D::Create(resolved.string());
                        }
                    }
                    if (!stack.NormalMap && !stack.NormalMapPath.empty()) {
                        auto resolved = FileSystem::ResolveAssetPath(stack.NormalMapPath);
                        if (std::filesystem::exists(resolved)) {
                            stack.NormalMap = Texture2D::Create(resolved.string());
                        }
                    }
                    m_ParticleRenderer->Render(*stack.System, stack.Texture, stack.NormalMap, stack.Config.SoftnessDistance);
                }
            }
            m_ParticleRenderer->End();
        }

        m_Framebuffer->Unbind();
    }

    void VFXEditorPanel::RenderPreviewRHI(float width, float height) {
        if (!m_RHIViewport || !m_RHIViewport->IsValid()) return;
        if (!RHI::RHIDevice::Get().IsInitialized()) return;

        if (!m_RHIParticleRenderer) {
            m_RHIParticleRenderer = std::make_unique<RHIParticleRenderer>();
            m_RHIParticleRenderer->Init(RHI::RHIDevice::Get());
        }

        u32 reqW = static_cast<u32>(width);
        u32 reqH = static_cast<u32>(height);
        if (reqW > 0 && reqH > 0 && (reqW != m_ViewportWidth || reqH != m_ViewportHeight)) {
            m_ViewportWidth = reqW;
            m_ViewportHeight = reqH;
            m_RHIViewport->Resize(m_ViewportWidth, m_ViewportHeight);
        }

        float aspect = static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);
        PerspectiveCamera camera(55.0f, aspect, 0.1f, 1000.0f);

        float yawRad = glm::radians(m_CameraYaw);
        float pitchRad = glm::radians(m_CameraPitch);
        glm::vec3 camPos;
        camPos.x = m_CameraTarget.x + m_CameraDistance * std::cos(pitchRad) * std::sin(yawRad);
        camPos.y = m_CameraTarget.y + m_CameraDistance * std::sin(pitchRad);
        camPos.z = m_CameraTarget.z + m_CameraDistance * std::cos(pitchRad) * std::cos(yawRad);

        glm::mat4 view = glm::lookAt(camPos, m_CameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        camera.SetViewMatrix(view, camPos);

        // Render preview passes into offscreen RHI viewport
        RenderPreview3D(width, height);
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
                SaveDocument();
            }
            ImGui::SameLine();
            if (ImGui::Button("Undo", ImVec2(40, 20))) {
                if (m_Document.Undo()) SyncStacksFromDocument();
            }
            ImGui::SameLine();
            if (ImGui::Button("Redo", ImVec2(40, 20))) {
                if (m_Document.Redo()) SyncStacksFromDocument();
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Emitter Stacks");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24.0f);
            if (ImGui::Button("+", ImVec2(22.0f, 20.0f))) {
                std::string newName = "Emitter " + std::to_string(m_EmitterStacks.size() + 1);
                AddEmitterStack(newName, m_Config);
            }

            ImGui::Separator();
            ImGui::Spacing();

            for (size_t i = 0; i < m_EmitterStacks.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));

                bool isSelected = (i == m_SelectedStackIndex);
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.45f, 0.7f, 0.8f));
                }

                // Enabled toggle
                ImGui::Checkbox("##Enabled", &m_EmitterStacks[i].Enabled);
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

                // Right click context menu on row
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Duplicate Emitter")) {
                        std::string dupName = m_EmitterStacks[i].Name + " (Copy)";
                        AddEmitterStack(dupName, m_EmitterStacks[i].Config);
                    }
                    if (m_EmitterStacks.size() > 1 && ImGui::MenuItem("Delete Emitter")) {
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
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
        if (ImGui::Button(m_IsPlaying ? "Pause" : "Play", ImVec2(65, 26))) {
            if (m_IsPlaying) PausePreview(); else PlayPreview();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(55, 26))) {
            StopPreview();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2(55, 26))) {
            ResetPreview();
        }
        ImGui::SameLine();
        if (ImGui::Button("Step", ImVec2(50, 26))) {
            PausePreview();
            Step();
            m_PlaybackScrubberTime = static_cast<float>(m_Clock.Tick * m_Clock.StepSeconds);
        }
        ImGui::SameLine();
        ImGui::PushItemWidth(70.0f);
        ImGui::SliderFloat("##Speed", &m_Clock.PlaybackSpeed, 0.1f, 3.0f, "%.1fx");
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (ImGui::Button("Burst 20", ImVec2(75, 26))) {
            TriggerBurst(20);
        }
        ImGui::SameLine();
        if (ImGui::Button("Burst 100", ImVec2(80, 26))) {
            TriggerBurst(100);
        }
        ImGui::SameLine();
        if (ImGui::Button("Burst 500", ImVec2(80, 26))) {
            TriggerBurst(500);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGui::Checkbox("Ground Grid", &m_ShowGroundGrid);
        ImGui::SameLine();
        if (ImGui::Button("Reset Camera", ImVec2(95, 26))) {
            m_CameraDistance = 5.0f;
            m_CameraPitch = 20.0f;
            m_CameraYaw = 45.0f;
            m_CameraTarget = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // Timeline scrubber
        ImGui::SameLine();
        ImGui::PushItemWidth(140.0f);
        float maxDuration = m_Config.Duration > 0.0f ? m_Config.Duration : 5.0f;
        for (const auto& s : m_EmitterStacks) {
            if (s.Config.Duration > maxDuration) maxDuration = s.Config.Duration;
        }
        float scrubber = m_PlaybackScrubberTime;
        if (ImGui::SliderFloat("##Scrubber", &scrubber, 0.0f, maxDuration, "%.2fs")) {
            SeekTo(scrubber);
        }
        ImGui::PopItemWidth();

        if (m_IsSeeking) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(Seeking...)");
        }

        ImGui::PopStyleVar();
    }

    void VFXEditorPanel::DrawPresetSelector() {
        if (ImGui::CollapsingHeader("⚡ VFX Presets (Quick Setup)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Load built-in physical particle presets:");
            ImGui::Spacing();

            auto PresetBtn = [&](const char* label, const ParticleEmitterConfig& cfg) {
                if (ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x * 0.48f, 26.0f))) {
                    m_Config = cfg;
                    SyncCurrentStackConfig();
                    ResetPreview();
                }
            };

            PresetBtn("🔥 Fire (Campfire / Torch)", ParticleEmitterConfig::CreateFire());
            ImGui::SameLine();
            PresetBtn("💨 Smoke (Volumetric Plume)", ParticleEmitterConfig::CreateSmoke());

            PresetBtn("✨ Sparks (Bouncing Embers)", ParticleEmitterConfig::CreateSparks());
            ImGui::SameLine();
            PresetBtn("🚗 Tire Burnout (Dense Smoke)", ParticleEmitterConfig::CreateTireBurnout());

            PresetBtn("🏜️ Dirt Rooster (Mud / Gravel)", ParticleEmitterConfig::CreateDustTrail());
            ImGui::SameLine();
            PresetBtn("💥 Exhaust Backfire (Pop & Spark)", ParticleEmitterConfig::CreateExhaustBackfire());
        }
    }

    void VFXEditorPanel::DrawMainModule() {
        if (ImGui::CollapsingHeader("📦 Main Module", ImGuiTreeNodeFlags_DefaultOpen)) {
            char nameBuf[128];
            strncpy_s(nameBuf, m_Config.Name.c_str(), sizeof(nameBuf));
            if (ImGui::InputText("Emitter Name", nameBuf, sizeof(nameBuf))) {
                m_Config.Name = nameBuf;
                if (m_SelectedStackIndex < m_EmitterStacks.size()) {
                    m_EmitterStacks[m_SelectedStackIndex].Name = nameBuf;
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

            ImGui::DragFloat("Gravity Modifier", &m_Config.GravityModifier, 0.05f, -10.0f, 10.0f, "%.2fx");
            ImGui::SliderFloat("Air Drag Damping", &m_Config.Drag, 0.0f, 1.0f, "%.2f");
        }
    }

    void VFXEditorPanel::DrawEmissionAndBurstsModule() {
        if (ImGui::CollapsingHeader("💥 Emission & Bursts", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Rate over Time (P/sec)", &m_Config.EmissionRate, 1.0f, 0.0f, 2000.0f, "%.1f");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Bursts (One-shot & Periodic):");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f);
            if (ImGui::SmallButton("+ Add Burst")) {
                m_Config.Bursts.emplace_back(0.0f, 30, 50, 1, 0.0f, 1.0f);
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
        }
    }

    void VFXEditorPanel::DrawShapeControls() {
        if (ImGui::CollapsingHeader("📐 Shape Module", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* shapeTypes[] = { "Point", "Sphere", "Hemisphere", "Box", "Cone", "Line", "Circle", "MeshSurface" };
            int currentShape = static_cast<int>(m_Config.Shape.Type);
            if (ImGui::Combo("Shape Type", &currentShape, shapeTypes, IM_ARRAYSIZE(shapeTypes))) {
                m_Config.Shape.Type = static_cast<ParticleEmitterShapeType>(currentShape);
                SyncCurrentStackConfig();
            }

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
                default:
                    break;
            }
        }
    }

    void VFXEditorPanel::DrawCurvesControls() {
        if (ImGui::CollapsingHeader("📈 Size & Velocity over Lifetime", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloatRange2("Start Size Range", &m_Config.StartSize.Min, &m_Config.StartSize.Max, 0.02f, 0.01f, 20.0f, "Min: %.2f", "Max: %.2f");
            ImGui::DragFloatRange2("End Size Range", &m_Config.EndSize.Min, &m_Config.EndSize.Max, 0.02f, 0.01f, 20.0f, "Min: %.2f", "Max: %.2f");

            // Interactive Bezier Curve Editors
            if (CurveEditorWidget::DrawCurveEditor("Size over Lifetime Curve", m_Config.SizeOverLifetime, 75.0f)) {
                SyncCurrentStackConfig();
            }

            ImGui::Spacing();
            if (CurveEditorWidget::DrawCurveEditor("Speed over Lifetime Curve", m_Config.SpeedOverLifetime, 75.0f)) {
                SyncCurrentStackConfig();
            }

            ImGui::Spacing();
            ImGui::DragFloatRange2("Angular Velocity (rad/s)", &m_Config.AngularVelocity.Min, &m_Config.AngularVelocity.Max, 0.1f, -20.0f, 20.0f, "Min: %.2f", "Max: %.2f");
        }

        if (ImGui::CollapsingHeader("🎨 Color & Alpha over Lifetime", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (CurveEditorWidget::DrawGradientEditor("Color Gradient Ramp", m_Config.ColorOverLifetime, 26.0f)) {
                SyncCurrentStackConfig();
            }

            ImGui::Spacing();
            if (CurveEditorWidget::DrawCurveEditor("Alpha over Lifetime Curve", m_Config.AlphaOverLifetime, 70.0f)) {
                SyncCurrentStackConfig();
            }
        }
    }

    void VFXEditorPanel::DrawTextureFlipbookControls() {
        if (ImGui::CollapsingHeader("🖼️ Texture & Flipbook Animation")) {
            auto* currentStack = GetCurrentEmitterStack();
            if (currentStack) {
                char texBuf[256];
                strncpy_s(texBuf, currentStack->TexturePath.c_str(), sizeof(texBuf));
                if (ImGui::InputText("Texture Path", texBuf, sizeof(texBuf))) {
                    currentStack->TexturePath = texBuf;
                    currentStack->Texture.reset();
                    SyncDocumentFromStacks();
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear##Tex")) {
                    currentStack->TexturePath.clear();
                    currentStack->Texture.reset();
                    SyncDocumentFromStacks();
                }

                char normBuf[256];
                strncpy_s(normBuf, currentStack->NormalMapPath.c_str(), sizeof(normBuf));
                if (ImGui::InputText("Normal Map", normBuf, sizeof(normBuf))) {
                    currentStack->NormalMapPath = normBuf;
                    currentStack->NormalMap.reset();
                    SyncDocumentFromStacks();
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear##Norm")) {
                    currentStack->NormalMapPath.clear();
                    currentStack->NormalMap.reset();
                    SyncDocumentFromStacks();
                }
                ImGui::Separator();
            }

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
            if (ImGui::Checkbox("Sub-Frame Smooth Blending", &m_Config.SubFrameBlending)) {
                SyncCurrentStackConfig();
            }
        }
    }

    void VFXEditorPanel::DrawDynamicsAndForceFields() {
        if (ImGui::CollapsingHeader("🌪️ Physics, Noise & Collisions")) {
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "Curl Noise Turbulence:");
            ImGui::Checkbox("Enable Curl Noise", &m_Config.CurlNoise.Enabled);
            if (m_Config.CurlNoise.Enabled) {
                ImGui::DragFloat("Curl Frequency", &m_Config.CurlNoise.Frequency, 0.05f, 0.05f, 10.0f, "%.2f");
                ImGui::DragFloat("Curl Strength", &m_Config.CurlNoise.Strength, 0.1f, 0.0f, 20.0f, "%.2f m/s²");
                int oct = static_cast<int>(m_Config.CurlNoise.Octaves);
                if (ImGui::SliderInt("Noise Octaves", &oct, 1, 4)) {
                    m_Config.CurlNoise.Octaves = static_cast<uint32_t>(oct);
                }
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.35f, 1.0f), "Ground / Surface Collisions:");
            ImGui::Checkbox("Enable Collisions", &m_Config.Collision.Enabled);
            if (m_Config.Collision.Enabled) {
                ImGui::SliderFloat("Restitution (Bounce)", &m_Config.Collision.Restitution, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Friction (Tangential)", &m_Config.Collision.Friction, 0.0f, 1.0f, "%.2f");
                ImGui::Checkbox("Kill on Collision", &m_Config.Collision.KillOnCollision);
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 1.0f, 1.0f), "Environmental Forces:");
            ImGui::DragFloat3("Directional Wind", glm::value_ptr(m_Config.Wind), 0.1f, -50.0f, 50.0f, "%.1f m/s");
            ImGui::DragFloat("Thermal Buoyancy", &m_Config.Buoyancy, 0.1f, -20.0f, 20.0f, "%.1f m/s²");
        }
    }

    void VFXEditorPanel::DrawSubEmittersModule() {
        if (ImGui::CollapsingHeader("🧬 Sub-Emitters Module")) {
            ImGui::TextDisabled("Spawn child particle systems on particle events:");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f);
            if (ImGui::SmallButton("+ Add Sub")) {
                std::string defaultTarget = "Sparks";
                for (size_t i = 0; i < m_EmitterStacks.size(); ++i) {
                    if (i != m_SelectedStackIndex) {
                        defaultTarget = m_EmitterStacks[i].Name;
                        break;
                    }
                }
                m_Config.SubEmitters.emplace_back(SubEmitterTrigger::Collision, defaultTarget, 10, 1.0f);
                SyncCurrentStackConfig();
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
                std::string currentTarget = sub.TargetEmitterName;
                if (ImGui::BeginCombo("Target Emitter", currentTarget.empty() ? "<Select Target>" : currentTarget.c_str())) {
                    for (size_t i = 0; i < m_EmitterStacks.size(); ++i) {
                        if (i == m_SelectedStackIndex) continue; // Exclude self
                        const auto& targetName = m_EmitterStacks[i].Name;
                        bool isSelected = (currentTarget == targetName);
                        if (ImGui::Selectable(targetName.c_str(), isSelected)) {
                            sub.TargetEmitterName = targetName;
                            SyncCurrentStackConfig();
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                // Check for invalid target or self-reference
                auto* curStack = GetCurrentEmitterStack();
                if (curStack && sub.TargetEmitterName == curStack->Name) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: Self-referencing sub-emitter target!");
                } else if (!sub.TargetEmitterName.empty()) {
                    bool targetExists = false;
                    for (const auto& stack : m_EmitterStacks) {
                        if (stack.Name == sub.TargetEmitterName) {
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
        }
    }

    void VFXEditorPanel::DrawShadingAndBillboarding() {
        if (ImGui::CollapsingHeader("💡 Renderer & Shading Pipeline")) {
            const char* orientations[] = { "Spherical (Camera-Facing)", "Cylindrical (Axial-Y)", "Velocity-Aligned (Stretched)" };
            int currentOrient = static_cast<int>(m_Config.Orientation);
            if (ImGui::Combo("Orientation Mode", &currentOrient, orientations, IM_ARRAYSIZE(orientations))) {
                m_Config.Orientation = static_cast<ParticleOrientation>(currentOrient);
            }

            const char* blendModes[] = { "Additive (Fire, Sparks)", "Alpha Blending (Smoke, Dust)", "Multiply", "Cutout (Alpha Tested)" };
            int currentBlend = static_cast<int>(m_Config.BlendMode);
            if (ImGui::Combo("Blend Mode", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                m_Config.BlendMode = static_cast<ParticleBlendMode>(currentBlend);
            }

            ImGui::DragFloat("Softness Distance", &m_Config.SoftnessDistance, 0.05f, 0.05f, 5.0f, "%.2f m");

            char matBuf[256];
            strncpy_s(matBuf, m_Config.MaterialPath.c_str(), sizeof(matBuf));
            if (ImGui::InputText("Material Path", matBuf, sizeof(matBuf))) {
                m_Config.MaterialPath = matBuf;
                SyncCurrentStackConfig();
            }
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
            if (ImGui::Button("Apply Stacks to Selected Entity", ImVec2(-1.0f, 30.0f))) {
                auto& pec = m_SelectedEntity.GetComponent<ParticleEmitterComponent>();
                SyncCurrentStackConfig();
                pec.EmitterStacks.clear();
                for (const auto& stack : m_EmitterStacks) {
                    ParticleEmitterStack sceneStack(stack.Name, stack.Config);
                    sceneStack.Enabled = stack.Enabled;
                    sceneStack.Visible = stack.Visible;
                    sceneStack.TexturePath = stack.TexturePath;
                    sceneStack.NormalMapPath = stack.NormalMapPath;
                    sceneStack.MaterialPath = stack.MaterialPath;
                    pec.EmitterStacks.push_back(std::move(sceneStack));
                }
                pec.SyncPrimary();
                pec.Initialize();
                if (pec.Active) {
                    pec.Play();
                }
            }
        }

        if (m_Scene) {
            if (ImGui::Button("Create New VFX Entity in Scene", ImVec2(-1.0f, 28.0f))) {
                SyncCurrentStackConfig();
                std::string entName = m_EmitterStacks.empty() ? "VFX_Emitter" : m_EmitterStacks[0].Name;
                Entity newEnt = m_Scene->CreateEntity(entName);
                newEnt.AddComponent<TransformComponent>(glm::vec3(0.0f, 1.0f, 0.0f));
                auto primaryConfig = m_EmitterStacks.empty() ? m_Config : m_EmitterStacks[0].Config;
                auto& pec = newEnt.AddComponent<ParticleEmitterComponent>(primaryConfig);
                pec.EmitterStacks.clear();
                for (const auto& stack : m_EmitterStacks) {
                    ParticleEmitterStack sceneStack(stack.Name, stack.Config);
                    sceneStack.Enabled = stack.Enabled;
                    sceneStack.Visible = stack.Visible;
                    sceneStack.TexturePath = stack.TexturePath;
                    sceneStack.NormalMapPath = stack.NormalMapPath;
                    sceneStack.MaterialPath = stack.MaterialPath;
                    pec.EmitterStacks.push_back(std::move(sceneStack));
                }
                pec.SyncPrimary();
                pec.Initialize();
                pec.Play();
                SetSelectedEntity(newEnt);
            }
        }
    }

    void VFXEditorPanel::DrawShurikenInspector(float width, float height) {
        ImGui::BeginChild("VFX_Shuriken_Inspector", ImVec2(width, height), true);
        {
            DrawPresetSelector();
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
        }
        ImGui::EndChild();
    }

    void VFXEditorPanel::OnImGuiRender(bool* p_open) {
        if (p_open) m_IsOpen = *p_open;
        if (!m_IsOpen) return;

        ImGui::SetNextWindowSize(ImVec2(1200, 760), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Visual Particle & VFX Studio (Unity Shuriken)", &m_IsOpen, ImGuiWindowFlags_NoScrollbar)) {
            // Top Transport Bar
            DrawTransportBar();
            ImGui::Separator();

            // 3-Column Layout:
            // Column 1 (Left): Emitter Stack Manager (~200px)
            // Column 2 (Middle): 3D Viewport (~50% of remainder)
            // Column 3 (Right): Shuriken Modules Inspector
            float totalWidth = ImGui::GetContentRegionAvail().x;
            float totalHeight = ImGui::GetContentRegionAvail().y;

            float stackWidth = 210.0f;
            float remainderWidth = totalWidth - stackWidth - 12.0f;
            float viewportWidth = remainderWidth * 0.50f;
            float inspectorWidth = remainderWidth - viewportWidth - 8.0f;

            // --- 1. LEFT COLUMN: EMITTER STACKS ---
            DrawEmitterStackColumn(stackWidth, totalHeight);
            ImGui::SameLine();

            // --- 2. MIDDLE COLUMN: 3D PREVIEW VIEWPORT ---
            ImGui::BeginChild("VFX_Viewport_Child", ImVec2(viewportWidth, totalHeight), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();

                uint64_t rhiTexID = 0;
                if (m_RHIViewport && m_RHIViewport->IsValid()) {
                    uint32_t reqW = (uint32_t)avail.x;
                    uint32_t reqH = (uint32_t)avail.y;
                    if (reqW > 0 && reqH > 0 && (reqW != m_ViewportWidth || reqH != m_ViewportHeight)) {
                        m_ViewportWidth = reqW;
                        m_ViewportHeight = reqH;
                        m_RHIViewport->Resize(m_ViewportWidth, m_ViewportHeight);
                    }
                    RenderPreviewRHI(avail.x, avail.y);
                    rhiTexID = m_RHIViewport->GetImTextureID();
                } else {
                    RenderPreview3D(avail.x, avail.y);
                }

                if (rhiTexID != 0) {
                    glm::vec2 uv0 = m_RHIViewport->GetUV0();
                    glm::vec2 uv1 = m_RHIViewport->GetUV1();
                    ImGui::Image((ImTextureID)(intptr_t)rhiTexID, avail, ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
                } else if (m_Framebuffer && m_Framebuffer->GetColorAttachmentRendererID() != 0) {
                    ImTextureID texID = (ImTextureID)(intptr_t)m_Framebuffer->GetColorAttachmentRendererID();
                    ImGui::Image(texID, avail, ImVec2(0, 1), ImVec2(1, 0));
                } else {
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    ImVec2 p1 = ImVec2(p0.x + avail.x, p0.y + avail.y);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(p0, p1, IM_COL32(18, 20, 24, 255));
                    drawList->AddRect(p0, p1, IM_COL32(40, 44, 52, 255));

                    ImVec2 center = ImVec2(p0.x + avail.x * 0.5f, p0.y + avail.y * 0.55f);
                    drawList->AddCircle(center, 24.0f, IM_COL32(255, 120, 40, 160), 24, 1.5f);
                    for (int i = 0; i < 8; ++i) {
                        float angle = (i * 0.785f);
                        float dist = 28.0f + (i % 3) * 15.0f;
                        ImVec2 sparkPos = ImVec2(center.x + std::cos(angle) * dist, center.y - std::sin(angle) * dist * 1.4f);
                        drawList->AddCircleFilled(sparkPos, 3.0f, IM_COL32(255, 180, 50, 220));
                    }

                    const char* txt = "Multi-Backend RHI Viewport Active";
                    ImVec2 txtSize = ImGui::CalcTextSize(txt);
                    ImVec2 txtPos = ImVec2(p0.x + (avail.x - txtSize.x) * 0.5f, p0.y + 12.0f);
                    drawList->AddText(txtPos, IM_COL32(100, 200, 255, 255), txt);
                    ImGui::Dummy(avail);
                }

                m_ViewportHovered = ImGui::IsItemHovered();
                m_ViewportFocused = ImGui::IsItemFocused();

                // Interactive Orbit Camera
                if (m_ViewportHovered) {
                    ImGuiIO& io = ImGui::GetIO();
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                        m_CameraYaw += io.MouseDelta.x * 0.5f;
                        m_CameraPitch -= io.MouseDelta.y * 0.5f;
                        m_CameraPitch = glm::clamp(m_CameraPitch, -89.0f, 89.0f);
                    }
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                        float panSpeed = 0.005f * m_CameraDistance;
                        m_CameraTarget.y += io.MouseDelta.y * panSpeed;
                    }
                    if (io.MouseWheel != 0.0f) {
                        m_CameraDistance = glm::clamp(m_CameraDistance - io.MouseWheel * 0.5f, 0.5f, 80.0f);
                    }
                }

                // Overlay Telemetry
                ImGui::SetCursorPos(ImVec2(12, 12));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.65f));
                ImGui::BeginChild("TelemetryOverlay", ImVec2(240, 115), true);
                uint32_t activeStackP = (m_SelectedStackIndex < m_EmitterStacks.size() && m_EmitterStacks[m_SelectedStackIndex].System) 
                                        ? m_EmitterStacks[m_SelectedStackIndex].System->GetActiveParticleCount() : 0;
                uint32_t poolCap = (m_SelectedStackIndex < m_EmitterStacks.size() && m_EmitterStacks[m_SelectedStackIndex].System)
                                   ? m_EmitterStacks[m_SelectedStackIndex].System->GetPool().GetCapacity() : 0;
                uint32_t collisions = (m_SelectedStackIndex < m_EmitterStacks.size() && m_EmitterStacks[m_SelectedStackIndex].System)
                                      ? m_EmitterStacks[m_SelectedStackIndex].System->GetCollisionCount() : 0;
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Live VFX Telemetry:");
                ImGui::Text("Active Emitter: %s", (m_SelectedStackIndex < m_EmitterStacks.size()) ? m_EmitterStacks[m_SelectedStackIndex].Name.c_str() : "None");
                ImGui::Text("Particles: %u / %u", activeStackP, poolCap);
                ImGui::Text("Collisions Bounced: %u", collisions);
                ImGui::Text("Sim Time: %.2f s", m_PlaybackScrubberTime);
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // --- 3. RIGHT COLUMN: SHURIKEN MODULES INSPECTOR ---
            DrawShurikenInspector(inspectorWidth, totalHeight);
        }
        ImGui::End();

        if (p_open) *p_open = m_IsOpen;
    }

}
