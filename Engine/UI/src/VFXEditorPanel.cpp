#include <Axle/VFXEditorPanel.hpp>
#include <Axle/Components.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Scene.hpp>
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
        m_PreviewSystem = std::make_unique<ParticleSystem>(m_Config);

        if (Window::HasActiveContext()) {
            // Initialize GPU Framebuffer for 3D Viewport preview
            FramebufferSpecification spec;
            spec.Width = m_ViewportWidth;
            spec.Height = m_ViewportHeight;
            m_Framebuffer = Framebuffer::Create(spec);

            // Initialize GPU Particle Renderer
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

    void VFXEditorPanel::InitGridShader() {
        if (!glad_glGenVertexArrays || !glad_glGenBuffers || !glad_glBindVertexArray || !glad_glBindBuffer || !glad_glBufferData) return;

        m_GridShader = std::make_unique<Shader>(s_GridVertSrc, s_GridFragSrc);

        // Create Grid Mesh
        std::vector<float> gridVertices;
        const int gridLines = 20;
        const float spacing = 0.5f;
        const float extent = (gridLines * spacing) * 0.5f;

        for (int i = 0; i <= gridLines; ++i) {
            float coord = -extent + i * spacing;

            // X-parallel lines (along Z)
            glm::vec4 colorZ = (i == gridLines / 2) ? glm::vec4(0.2f, 0.4f, 0.9f, 0.8f) : glm::vec4(0.25f, 0.28f, 0.35f, 0.4f);
            gridVertices.insert(gridVertices.end(), { -extent, 0.0f, coord, colorZ.r, colorZ.g, colorZ.b, colorZ.a });
            gridVertices.insert(gridVertices.end(), {  extent, 0.0f, coord, colorZ.r, colorZ.g, colorZ.b, colorZ.a });

            // Z-parallel lines (along X)
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

    void VFXEditorPanel::SetEmitterConfig(const ParticleEmitterConfig& config) {
        m_Config = config;
        ResetPreview();
    }

    void VFXEditorPanel::SetSelectedEntity(Entity entity) {
        m_SelectedEntity = entity;
        if (m_SelectedEntity && m_SelectedEntity.HasComponent<ParticleEmitterComponent>()) {
            m_Config = m_SelectedEntity.GetComponent<ParticleEmitterComponent>().Config;
            ResetPreview();
        }
    }

    void VFXEditorPanel::ResetPreview() {
        m_PreviewSystem = std::make_unique<ParticleSystem>(m_Config);
        if (m_IsPlaying) {
            m_PreviewSystem->SetEmitting(true);
        }
    }

    void VFXEditorPanel::PlayPreview() {
        m_IsPlaying = true;
        if (m_PreviewSystem) m_PreviewSystem->SetEmitting(true);
    }

    void VFXEditorPanel::PausePreview() {
        m_IsPlaying = false;
        if (m_PreviewSystem) m_PreviewSystem->SetEmitting(false);
    }

    void VFXEditorPanel::StopPreview() {
        m_IsPlaying = false;
        if (m_PreviewSystem) {
            m_PreviewSystem->SetEmitting(false);
            m_PreviewSystem->Reset();
        }
    }

    void VFXEditorPanel::TriggerBurst(uint32_t count) {
        if (!m_PreviewSystem) ResetPreview();
        m_PreviewSystem->EmitBurst(count);
    }

    void VFXEditorPanel::OnUpdate(Timestep ts) {
        if (!m_IsOpen) return;

        if (m_IsPlaying && m_PreviewSystem) {
            m_PreviewSystem->Update(ts.GetSeconds(), glm::vec3(0.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
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

        // Orbit Camera View & Projection
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

        // 2. Render particle system via GPU Instancing
        if (m_PreviewSystem && m_ParticleRenderer) {
            m_ParticleRenderer->Begin(camera, m_Framebuffer, 0.1f, 1000.0f);
            m_ParticleRenderer->Render(*m_PreviewSystem, nullptr, m_Config.SoftnessDistance);
            m_ParticleRenderer->End();
        }

        m_Framebuffer->Unbind();
    }

    void VFXEditorPanel::DrawPresetSelector() {
        if (ImGui::CollapsingHeader("VFX Presets (Quick Setup)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Load built-in physical particle effect configurations:");
            ImGui::Spacing();

            auto PresetBtn = [&](const char* label, const ParticleEmitterConfig& cfg) {
                if (ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x * 0.48f, 26.0f))) {
                    m_Config = cfg;
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

    void VFXEditorPanel::DrawLifecycleAndSpawnControls() {
        if (ImGui::CollapsingHeader("Lifecycle & Kinematics", ImGuiTreeNodeFlags_DefaultOpen)) {
            char nameBuf[128];
            strncpy_s(nameBuf, m_Config.Name.c_str(), sizeof(nameBuf));
            if (ImGui::InputText("Emitter Name", nameBuf, sizeof(nameBuf))) {
                m_Config.Name = nameBuf;
            }

            const char* spaceModes[] = { "World Space", "Local Space" };
            int currentSpace = (m_Config.SimulationSpace == ParticleSimulationSpace::Local) ? 1 : 0;
            if (ImGui::Combo("Simulation Space", &currentSpace, spaceModes, IM_ARRAYSIZE(spaceModes))) {
                m_Config.SimulationSpace = (currentSpace == 1) ? ParticleSimulationSpace::Local : ParticleSimulationSpace::World;
                ResetPreview();
            }

            int maxP = static_cast<int>(m_Config.MaxParticles);
            if (ImGui::DragInt("Max Particles (Pool)", &maxP, 50, 10, 50000)) {
                m_Config.MaxParticles = static_cast<uint32_t>(std::max(10, maxP));
                ResetPreview();
            }

            ImGui::DragFloat("Emission Rate (P/sec)", &m_Config.EmissionRate, 1.0f, 0.0f, 2000.0f, "%.1f");

            ImGui::DragFloatRange2("Lifetime Range (sec)", &m_Config.Lifetime.Min, &m_Config.Lifetime.Max, 0.05f, 0.05f, 30.0f, "Min: %.2f", "Max: %.2f");
            ImGui::DragFloatRange2("Speed Range (m/s)", &m_Config.InitialSpeed.Min, &m_Config.InitialSpeed.Max, 0.1f, 0.0f, 100.0f, "Min: %.1f", "Max: %.1f");

            ImGui::DragFloat("Gravity Modifier", &m_Config.GravityModifier, 0.05f, -10.0f, 10.0f, "%.2fx");
            ImGui::SliderFloat("Air Drag Damping", &m_Config.Drag, 0.0f, 1.0f, "%.2f");
        }
    }

    void VFXEditorPanel::DrawShapeControls() {
        if (ImGui::CollapsingHeader("Emitter Geometry Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* shapeTypes[] = { "Point", "Sphere", "Hemisphere", "Box", "Cone", "Line" };
            int currentShape = static_cast<int>(m_Config.Shape.Type);
            if (ImGui::Combo("Shape Type", &currentShape, shapeTypes, IM_ARRAYSIZE(shapeTypes))) {
                m_Config.Shape.Type = static_cast<ParticleEmitterShapeType>(currentShape);
            }

            switch (m_Config.Shape.Type) {
                case ParticleEmitterShapeType::Sphere:
                case ParticleEmitterShapeType::Hemisphere:
                    ImGui::DragFloat("Sphere Radius", &m_Config.Shape.Radius, 0.05f, 0.01f, 50.0f, "%.2f m");
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
        if (ImGui::CollapsingHeader("Dynamic Curves (Size & Color/Alpha)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Size over Lifetime:");
            ImGui::DragFloatRange2("Start Size Range", &m_Config.StartSize.Min, &m_Config.StartSize.Max, 0.02f, 0.01f, 20.0f, "Min: %.2f", "Max: %.2f");
            ImGui::DragFloatRange2("End Size Range", &m_Config.EndSize.Min, &m_Config.EndSize.Max, 0.02f, 0.01f, 20.0f, "Min: %.2f", "Max: %.2f");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Color & Alpha Gradient over Lifetime:");
            ImGui::ColorEdit4("Start Color & Alpha", glm::value_ptr(m_Config.ColorOverLifetime.StartColor), ImGuiColorEditFlags_AlphaBar);
            ImGui::ColorEdit4("End Color & Alpha", glm::value_ptr(m_Config.ColorOverLifetime.EndColor), ImGuiColorEditFlags_AlphaBar);

            ImGui::DragFloatRange2("Angular Velocity (rad/s)", &m_Config.AngularVelocity.Min, &m_Config.AngularVelocity.Max, 0.1f, -20.0f, 20.0f, "Min: %.2f", "Max: %.2f");
        }
    }

    void VFXEditorPanel::DrawTextureFlipbookControls() {
        if (ImGui::CollapsingHeader("Texture & Flipbook Animations")) {
            int rows = static_cast<int>(m_Config.TextureRows);
            int cols = static_cast<int>(m_Config.TextureCols);
            if (ImGui::DragInt("Flipbook Rows (N)", &rows, 1, 1, 16)) {
                m_Config.TextureRows = static_cast<uint32_t>(std::max(1, rows));
                m_Config.TotalFrames = m_Config.TextureRows * m_Config.TextureCols;
            }
            if (ImGui::DragInt("Flipbook Columns (M)", &cols, 1, 1, 16)) {
                m_Config.TextureCols = static_cast<uint32_t>(std::max(1, cols));
                m_Config.TotalFrames = m_Config.TextureRows * m_Config.TextureCols;
            }
            ImGui::Checkbox("Sub-Frame Smooth Blending", &m_Config.SubFrameBlending);
        }
    }

    void VFXEditorPanel::DrawDynamicsAndForceFields() {
        if (ImGui::CollapsingHeader("Simulation Dynamics & Fields")) {
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "Curl Noise Fluid Turbulence:");
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
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.35f, 1.0f), "Particle Ground Collisions:");
            ImGui::Checkbox("Enable Collisions", &m_Config.Collision.Enabled);
            if (m_Config.Collision.Enabled) {
                ImGui::SliderFloat("Restitution (Bounciness)", &m_Config.Collision.Restitution, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Friction (Tangential)", &m_Config.Collision.Friction, 0.0f, 1.0f, "%.2f");
                ImGui::Checkbox("Kill on Collision", &m_Config.Collision.KillOnCollision);
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 1.0f, 1.0f), "Environmental Forces:");
            ImGui::DragFloat3("Directional Wind", glm::value_ptr(m_Config.Wind), 0.1f, -50.0f, 50.0f, "%.1f m/s");
            ImGui::DragFloat("Thermal Buoyancy (Updraft)", &m_Config.Buoyancy, 0.1f, -20.0f, 20.0f, "%.1f m/s²");
        }
    }

    void VFXEditorPanel::DrawShadingAndBillboarding() {
        if (ImGui::CollapsingHeader("Shading & Billboarding Pipeline")) {
            const char* orientations[] = { "Spherical (Camera-Facing)", "Cylindrical (Axial-Y)", "Velocity-Aligned (Stretched)" };
            int currentOrient = static_cast<int>(m_Config.Orientation);
            if (ImGui::Combo("Orientation Mode", &currentOrient, orientations, IM_ARRAYSIZE(orientations))) {
                m_Config.Orientation = static_cast<ParticleOrientation>(currentOrient);
            }

            const char* blendModes[] = { "Additive (Fire, Sparks)", "Alpha Blending (Smoke, Dust)", "Premultiplied Alpha" };
            int currentBlend = static_cast<int>(m_Config.BlendMode);
            if (ImGui::Combo("Blend Mode", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                m_Config.BlendMode = static_cast<ParticleBlendMode>(currentBlend);
            }

            ImGui::DragFloat("Softness Distance", &m_Config.SoftnessDistance, 0.05f, 0.05f, 5.0f, "%.2f m");
        }
    }

    void VFXEditorPanel::DrawSceneActions() {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (m_SelectedEntity && m_SelectedEntity.HasComponent<ParticleEmitterComponent>()) {
            if (ImGui::Button("💾 Apply to Selected Entity", ImVec2(-1.0f, 30.0f))) {
                auto& pec = m_SelectedEntity.GetComponent<ParticleEmitterComponent>();
                pec.Config = m_Config;
                pec.Initialize();
                pec.Play();
            }
        }

        if (m_Scene) {
            if (ImGui::Button("➕ Create New VFX Entity in Scene", ImVec2(-1.0f, 28.0f))) {
                std::string entName = m_Config.Name.empty() ? "VFX_Emitter" : m_Config.Name;
                Entity newEnt = m_Scene->CreateEntity(entName);
                newEnt.AddComponent<TransformComponent>(glm::vec3(0.0f, 1.0f, 0.0f));
                auto& pec = newEnt.AddComponent<ParticleEmitterComponent>(m_Config);
                pec.Initialize();
                pec.Play();
                SetSelectedEntity(newEnt);
            }
        }
    }

    void VFXEditorPanel::OnImGuiRender(bool* p_open) {
        if (p_open) m_IsOpen = *p_open;
        if (!m_IsOpen) return;

        ImGui::SetNextWindowSize(ImVec2(1050, 720), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Visual FX (VFX) Editor", &m_IsOpen, ImGuiWindowFlags_NoScrollbar)) {
            // Top Toolbar
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
            if (ImGui::Button(m_IsPlaying ? "⏸ Pause" : "▶ Play", ImVec2(75, 26))) {
                if (m_IsPlaying) PausePreview(); else PlayPreview();
            }
            ImGui::SameLine();
            if (ImGui::Button("⏹ Stop", ImVec2(65, 26))) {
                StopPreview();
            }
            ImGui::SameLine();
            if (ImGui::Button("🔄 Reset", ImVec2(65, 26))) {
                ResetPreview();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            if (ImGui::Button("💥 Burst 20", ImVec2(85, 26))) {
                TriggerBurst(20);
            }
            ImGui::SameLine();
            if (ImGui::Button("💥 Burst 100", ImVec2(90, 26))) {
                TriggerBurst(100);
            }
            ImGui::SameLine();
            if (ImGui::Button("💥 Burst 500", ImVec2(90, 26))) {
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
            ImGui::PopStyleVar();

            ImGui::Separator();

            // Main Two-Column Layout (Left: 3D Viewport, Right: Parameters)
            float totalWidth = ImGui::GetContentRegionAvail().x;
            float viewportWidth = totalWidth * 0.52f;
            float inspectorWidth = totalWidth - viewportWidth - 10.0f;
            float viewHeight = ImGui::GetContentRegionAvail().y;

            // --- LEFT COLUMN: 3D PREVIEW VIEWPORT ---
            ImGui::BeginChild("VFX_Viewport_Child", ImVec2(viewportWidth, viewHeight), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                RenderPreview3D(avail.x, avail.y);

                if (m_Framebuffer) {
                    ImTextureID texID = (ImTextureID)(intptr_t)m_Framebuffer->GetColorAttachmentRendererID();
                    ImGui::Image(texID, avail, ImVec2(0, 1), ImVec2(1, 0));
                } else {
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    ImVec2 p1 = ImVec2(p0.x + avail.x, p0.y + avail.y);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(p0, p1, IM_COL32(20, 22, 26, 255));
                    drawList->AddRect(p0, p1, IM_COL32(40, 44, 52, 255));
                    const char* txt = "VFX preview requires OpenGL context";
                    ImVec2 txtSize = ImGui::CalcTextSize(txt);
                    ImVec2 txtPos = ImVec2(p0.x + (avail.x - txtSize.x) * 0.5f, p0.y + (avail.y - txtSize.y) * 0.5f);
                    drawList->AddText(txtPos, IM_COL32(130, 135, 145, 255), txt);
                    ImGui::Dummy(avail);
                }

                m_ViewportHovered = ImGui::IsItemHovered();
                m_ViewportFocused = ImGui::IsItemFocused();

                // Interactive Orbit Camera Handling
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
                ImGui::BeginChild("TelemetryOverlay", ImVec2(240, 105), true);
                uint32_t activeCount = m_PreviewSystem ? m_PreviewSystem->GetActiveParticleCount() : 0;
                uint32_t poolCap = m_PreviewSystem ? m_PreviewSystem->GetPool().GetCapacity() : 0;
                uint32_t collisions = m_PreviewSystem ? m_PreviewSystem->GetCollisionCount() : 0;
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Live VFX Telemetry:");
                ImGui::Text("Active Particles: %u / %u", activeCount, poolCap);
                ImGui::Text("Collisions Bounced: %u", collisions);
                ImGui::Text("Draw Calls: 1 (GPU Instanced)");
                ImGui::Text("Camera Dist: %.1f m", m_CameraDistance);
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // --- RIGHT COLUMN: PARAMETER INSPECTOR ---
            ImGui::BeginChild("VFX_Inspector_Child", ImVec2(inspectorWidth, viewHeight), true);
            {
                DrawPresetSelector();
                DrawLifecycleAndSpawnControls();
                DrawShapeControls();
                DrawCurvesControls();
                DrawTextureFlipbookControls();
                DrawDynamicsAndForceFields();
                DrawShadingAndBillboarding();
                DrawSceneActions();
            }
            ImGui::EndChild();
        }
        ImGui::End();

        if (p_open) *p_open = m_IsOpen;
    }

}
