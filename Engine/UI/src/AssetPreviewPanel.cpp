#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <imgui.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <functional>

namespace Axle {

    static const char* s_PreviewVertSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;

        out vec3 v_WorldPos;
        out vec3 v_LocalPos;

        void main() {
            v_LocalPos = a_Position;
            vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
            v_WorldPos = worldPos.xyz;
            gl_Position = u_ViewProjection * worldPos;
        }
    )";

    static const char* s_PreviewFragSrc = R"(
        #version 450 core
        layout(location = 0) out vec4 o_Color;

        in vec3 v_WorldPos;
        in vec3 v_LocalPos;

        uniform int u_PatternMode;
        uniform float u_TileScale;
        uniform vec4 u_Color1;
        uniform vec4 u_Color2;
        uniform vec3 u_LightDir;
        uniform mat4 u_Transform;
        uniform int u_WireframeMode;

        void main() {
            if (u_WireframeMode == 1) {
                o_Color = vec4(0.25, 0.88, 1.0, 1.0);
                return;
            }

            float pattern = 0.0;
            vec3 normal = vec3(0.0, 1.0, 0.0);
            if (u_PatternMode == 0) {
                vec2 coord = floor(v_WorldPos.xz * u_TileScale);
                pattern = mod(coord.x + coord.y, 2.0);
            } else if (u_PatternMode == 1) {
                vec3 coord = floor((v_LocalPos + vec3(0.5)) * u_TileScale + vec3(0.0001));
                pattern = mod(coord.x + coord.y + coord.z, 2.0);
                vec3 dx = dFdx(v_WorldPos);
                vec3 dy = dFdy(v_WorldPos);
                normal = normalize(cross(dx, dy));
                if (dot(normal, normalize(-v_WorldPos)) < 0.0) normal = -normal;
            } else {
                vec3 nLocal = normalize(v_LocalPos);
                float u = atan(nLocal.z, nLocal.x) / (2.0 * 3.14159265359) + 0.5;
                float v = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265359 + 0.5;
                vec2 coord = floor(vec2(u * u_TileScale, v * (u_TileScale * 0.5)));
                pattern = mod(coord.x + coord.y, 2.0);
                normal = normalize((u_Transform * vec4(v_LocalPos, 0.0)).xyz);
            }

            vec4 baseColor = mix(u_Color1, u_Color2, pattern);
            vec3 keyLight = normalize(u_LightDir);
            vec3 fillLight = normalize(vec3(-0.45, 0.45, -0.65));

            float NdotKey = max(dot(normal, keyLight), 0.0);
            float NdotFill = max(dot(normal, fillLight), 0.0) * 0.38;
            float ambient = 0.32;
            float lighting = ambient + 0.58 * NdotKey + NdotFill;

            vec3 viewDir = normalize(-v_WorldPos);
            vec3 halfDir = normalize(keyLight + viewDir);
            float spec = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.25;

            o_Color = vec4(baseColor.rgb * lighting + vec3(spec), baseColor.a);
        }
    )";

    AssetPreviewPanel::AssetPreviewPanel() {
        FramebufferSpecification spec;
        spec.Width = m_ViewportWidth;
        spec.Height = m_ViewportHeight;
        m_Framebuffer = Framebuffer::Create(spec);

        m_Shader = std::make_unique<Shader>(s_PreviewVertSrc, s_PreviewFragSrc);
    }

    void AssetPreviewPanel::SetSceneContext(Scene* scene, PhysicsScene* physicsScene, std::shared_ptr<VertexArray> fallbackMesh, const SceneSerializerContext* serializerContext) {
        m_Scene = scene;
        m_PhysicsScene = physicsScene;
        m_FallbackMesh = fallbackMesh;
        m_SerializerContext = serializerContext;

        if (m_HasAsset && m_IsAssembly) {
            std::function<void(AssemblyNode&)> preloadMeshVAOs = [&](AssemblyNode& node) {
                if (node.HasMesh && !node.Mesh.VAO) {
                    if (m_SerializerContext) {
                        if (node.Mesh.AssetPath == "builtin://sphere") node.Mesh.VAO = m_SerializerContext->SphereMesh;
                        else if (node.Mesh.AssetPath == "builtin://cylinder") node.Mesh.VAO = m_SerializerContext->CylinderMesh;
                        else if (node.Mesh.AssetPath == "builtin://cone") node.Mesh.VAO = m_SerializerContext->ConeMesh;
                        else if (node.Mesh.AssetPath == "builtin://plane") node.Mesh.VAO = m_SerializerContext->PlaneMesh;
                        else if (!node.Mesh.AssetPath.empty() && node.Mesh.AssetPath != "builtin://cube") {
                            auto resolved = FileSystem::ResolveAssetPath(node.Mesh.AssetPath);
                            if (std::filesystem::exists(resolved)) {
                                auto loaded = AssetSerializer::LoadMesh(resolved);
                                if (loaded.Success) node.Mesh.VAO = loaded.VAO;
                            }
                        }
                        if (!node.Mesh.VAO) node.Mesh.VAO = m_SerializerContext->CubeMesh;
                    }
                    if (!node.Mesh.VAO) node.Mesh.VAO = m_FallbackMesh;
                }
                for (auto& child : node.Children) {
                    preloadMeshVAOs(child);
                }
            };
            preloadMeshVAOs(m_CurrentAssembly.RootNode);
        }
    }

    void AssetPreviewPanel::SetAsset(const std::filesystem::path& assetPath) {
        m_CurrentAssetPath = assetPath;
        if (!std::filesystem::exists(assetPath)) {
            m_HasAsset = false;
            return;
        }

        std::string ext = assetPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".axle" && AssemblySerializer::IsAssemblyFile(assetPath)) {
            m_IsAssembly = true;
            m_CurrentAssembly = AssemblySerializer::LoadAssemblyData(assetPath, m_SerializerContext);
            m_HasAsset = m_CurrentAssembly.IsValid;
            if (!m_HasAsset) return;

            // Preload and cache all mesh VAOs ONCE so RenderPreview3D doesn't reload from disk/cache every frame
            std::function<void(AssemblyNode&)> preloadMeshVAOs = [&](AssemblyNode& node) {
                if (node.HasMesh && !node.Mesh.VAO) {
                    if (m_SerializerContext) {
                        if (node.Mesh.AssetPath == "builtin://sphere") node.Mesh.VAO = m_SerializerContext->SphereMesh;
                        else if (node.Mesh.AssetPath == "builtin://cylinder") node.Mesh.VAO = m_SerializerContext->CylinderMesh;
                        else if (node.Mesh.AssetPath == "builtin://cone") node.Mesh.VAO = m_SerializerContext->ConeMesh;
                        else if (node.Mesh.AssetPath == "builtin://plane") node.Mesh.VAO = m_SerializerContext->PlaneMesh;
                        else if (!node.Mesh.AssetPath.empty() && node.Mesh.AssetPath != "builtin://cube") {
                            auto resolved = FileSystem::ResolveAssetPath(node.Mesh.AssetPath);
                            if (std::filesystem::exists(resolved)) {
                                auto loaded = AssetSerializer::LoadMesh(resolved);
                                if (loaded.Success) node.Mesh.VAO = loaded.VAO;
                            }
                        }
                        if (!node.Mesh.VAO) node.Mesh.VAO = m_SerializerContext->CubeMesh;
                    }
                    if (!node.Mesh.VAO) node.Mesh.VAO = m_FallbackMesh;
                }
                for (auto& child : node.Children) {
                    preloadMeshVAOs(child);
                }
            };
            preloadMeshVAOs(m_CurrentAssembly.RootNode);

            // Compute aggregate bounding box from assembly
            glm::vec3 minBound(1e9f);
            glm::vec3 maxBound(-1e9f);
            size_t totalVertices = 0;
            size_t totalIndices = 0;

            std::function<void(const AssemblyNode&, const glm::mat4&)> computeBounds = [&](const AssemblyNode& node, const glm::mat4& parentMat) {
                glm::mat4 localMat = node.Transform.GetTransform();
                glm::mat4 worldMat = parentMat * localMat;
                glm::vec3 pos = glm::vec3(worldMat[3]);
                glm::vec3 halfExt = node.HasCollisionShape ? node.CollisionShape.HalfExtent : glm::vec3(0.5f);
                minBound = glm::min(minBound, pos - halfExt);
                maxBound = glm::max(maxBound, pos + halfExt);
                totalVertices += 24;
                totalIndices += 36;
                for (const auto& child : node.Children) {
                    computeBounds(child, worldMat);
                }
            };
            computeBounds(m_CurrentAssembly.RootNode, glm::mat4(1.0f));

            if (minBound.x > maxBound.x) {
                minBound = glm::vec3(-1.0f);
                maxBound = glm::vec3(1.0f);
            }

            m_Center = (minBound + maxBound) * 0.5f;
            m_Size = maxBound - minBound;
            m_MaxDim = std::max({ m_Size.x, m_Size.y, m_Size.z, 0.2f });
            m_TotalVertices = totalVertices;
            m_TotalTriangles = totalIndices / 3;

            // Reset camera framing
            m_Pitch = 20.0f;
            m_Yaw = 35.0f;
            m_Zoom = 1.0f;
            m_PanOffset = glm::vec3(0.0f);
            return;
        }

        m_IsAssembly = false;
        m_CurrentPackage = AssetSerializer::LoadPackage(assetPath);
        if (!m_CurrentPackage.Success || m_CurrentPackage.Parts.empty()) {
            m_HasAsset = false;
            return;
        }

        m_HasAsset = true;

        // Compute aggregate bounding box
        glm::vec3 minBound(1e9f);
        glm::vec3 maxBound(-1e9f);
        size_t totalVertices = 0;
        size_t totalIndices = 0;

        for (const auto& part : m_CurrentPackage.Parts) {
            if (!part.LoadedMesh.Success || !part.LoadedMesh.VAO) continue;
            const auto& verts = part.LoadedMesh.SourceData.Vertices;
            glm::mat4 localMat = glm::translate(glm::mat4(1.0f), part.LocalTranslation)
                               * glm::mat4_cast(part.LocalRotation)
                               * glm::scale(glm::mat4(1.0f), part.LocalScale);

            for (const auto& v : verts) {
                glm::vec3 worldV = glm::vec3(localMat * glm::vec4(v, 1.0f));
                minBound = glm::min(minBound, worldV);
                maxBound = glm::max(maxBound, worldV);
            }
            totalVertices += verts.size();
            totalIndices += part.LoadedMesh.SourceData.Indices.size();
        }

        if (minBound.x > maxBound.x) {
            minBound = glm::vec3(-0.5f);
            maxBound = glm::vec3(0.5f);
        }

        m_Center = (minBound + maxBound) * 0.5f;
        m_Size = maxBound - minBound;
        m_MaxDim = std::max({ m_Size.x, m_Size.y, m_Size.z, 0.05f });
        m_TotalVertices = totalVertices;
        m_TotalTriangles = totalIndices / 3;

        // Reset camera framing
        m_Pitch = 20.0f;
        m_Yaw = 35.0f;
        m_Zoom = 1.0f;
        m_PanOffset = glm::vec3(0.0f);
    }

    void AssetPreviewPanel::RenderPreview3D(float width, float height) {
        u32 w = static_cast<u32>(std::max(width, 64.0f));
        u32 h = static_cast<u32>(std::max(height, 64.0f));

        if (w != m_ViewportWidth || h != m_ViewportHeight) {
            m_ViewportWidth = w;
            m_ViewportHeight = h;
            m_Framebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
        }

        m_Framebuffer->Bind();
        glViewport(0, 0, m_ViewportWidth, m_ViewportHeight);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.10f, 0.12f, 0.16f, 1.0f); // Sleek studio charcoal
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!m_HasAsset || (!m_IsAssembly && m_CurrentPackage.Parts.empty())) {
            m_Framebuffer->Unbind();
            return;
        }

        // Turntable update
        if (m_TurntableActive && !m_IsViewportHovered) {
            m_Yaw += ImGui::GetIO().DeltaTime * m_TurntableSpeed * 30.0f;
            if (m_Yaw > 360.0f) m_Yaw -= 360.0f;
        }

        // Camera setup
        float pitchRad = glm::radians(m_Pitch);
        float yawRad = glm::radians(m_Yaw);
        float distance = m_MaxDim * 1.95f * m_Zoom;

        glm::vec3 camPos = m_PanOffset + glm::vec3(
            distance * std::cos(pitchRad) * std::sin(yawRad),
            distance * std::sin(pitchRad),
            distance * std::cos(pitchRad) * std::cos(yawRad)
        );

        glm::mat4 view = glm::lookAt(camPos, m_PanOffset, glm::vec3(0.0f, 1.0f, 0.0f));
        float aspect = static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.01f, distance * 8.0f);
        glm::mat4 viewProj = proj * view;

        m_Shader->Bind();
        m_Shader->SetMat4("u_ViewProjection", viewProj);
        m_Shader->SetFloat3("u_LightDir", glm::vec3(0.5f, 0.85f, 0.6f));

        if (m_WireframeMode) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            m_Shader->SetInt("u_WireframeMode", 1);
        } else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            m_Shader->SetInt("u_WireframeMode", 0);
        }

        if (m_IsAssembly) {
            std::function<void(const AssemblyNode&, const glm::mat4&)> renderNodeRec = [&](const AssemblyNode& node, const glm::mat4& parentMat) {
                glm::mat4 localMat = node.Transform.GetTransform();
                glm::mat4 worldMat = parentMat * localMat;
                glm::mat4 model = glm::translate(glm::mat4(1.0f), -m_Center) * worldMat;

                if (node.HasMesh) {
                    std::shared_ptr<VertexArray> vao = node.Mesh.VAO;
                    if (!vao) vao = m_FallbackMesh;

                    if (vao) {
                        m_Shader->SetMat4("u_Transform", model);
                        m_Shader->SetInt("u_PatternMode", node.Mesh.PatternMode);
                        m_Shader->SetFloat("u_TileScale", node.Mesh.TileScale);
                        m_Shader->SetFloat4("u_Color1", node.Mesh.Color1);
                        m_Shader->SetFloat4("u_Color2", node.Mesh.Color2);
                        RenderCommand::DrawIndexed(vao);
                    }
                }

                for (const auto& child : node.Children) {
                    renderNodeRec(child, worldMat);
                }
            };
            renderNodeRec(m_CurrentAssembly.RootNode, glm::mat4(1.0f));
        } else {
            // Render all subparts with center offset
            for (const auto& part : m_CurrentPackage.Parts) {
                if (!part.LoadedMesh.VAO) continue;
                glm::mat4 localMat = glm::translate(glm::mat4(1.0f), part.LocalTranslation)
                                   * glm::mat4_cast(part.LocalRotation)
                                   * glm::scale(glm::mat4(1.0f), part.LocalScale);
                glm::mat4 model = glm::translate(glm::mat4(1.0f), -m_Center) * localMat;

                m_Shader->SetMat4("u_Transform", model);
                m_Shader->SetInt("u_PatternMode", part.LoadedMesh.SourceData.PatternMode);
                m_Shader->SetFloat("u_TileScale", part.LoadedMesh.SourceData.TileScale);
                m_Shader->SetFloat4("u_Color1", part.LoadedMesh.SourceData.ColorPrimary);
                m_Shader->SetFloat4("u_Color2", part.LoadedMesh.SourceData.ColorSecondary);

                RenderCommand::DrawIndexed(part.LoadedMesh.VAO);
            }
        }

        if (m_WireframeMode) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        m_Framebuffer->Unbind();
    }

    void AssetPreviewPanel::SpawnCurrentAssetIntoScene() {
        if (!m_Scene || !m_HasAsset) return;

        if (m_IsAssembly) {
            auto spawned = m_Scene->InstantiateAssembly(m_CurrentAssetPath, m_PhysicsScene, glm::vec3(0.0f, 1.0f, 0.0f), m_SerializerContext);
            AXLE_INFO("AssetPreviewPanel: Instantiated Assembly '{0}'", m_CurrentAssetPath.filename().string());
            return;
        }

        if (m_CurrentPackage.Parts.empty()) return;

        glm::vec3 spawnPos(0.0f, m_CurrentPackage.HalfExtent.y, 0.0f);
        if (m_CurrentPackage.IsPackage) {
            std::string rootTag = m_CurrentPackage.Name.empty() ? m_CurrentAssetPath.stem().string() : m_CurrentPackage.Name;
            auto rootEntity = m_Scene->CreateEntity(rootTag);
            rootEntity.AddComponent<TransformComponent>(spawnPos, glm::vec3(1.0f));
            auto& rootHierarchy = rootEntity.AddComponent<HierarchyComponent>();

            if (m_PhysicsScene) {
                BoxBodyProps bProps;
                bProps.Position = spawnPos;
                bProps.HalfExtent = m_CurrentPackage.HalfExtent;
                bProps.IsDynamic = true;
                bProps.Friction = 0.70f;
                bProps.Restitution = 0.15f;
                auto bodyId = m_PhysicsScene->CreateBox(bProps);
                rootEntity.AddComponent<RigidBody3D>(bodyId, true);
                rootEntity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, m_CurrentPackage.HalfExtent, 0.70f, 0.15f);
            }

            for (const auto& part : m_CurrentPackage.Parts) {
                auto childEntity = m_Scene->CreateEntity(part.Name);
                auto& ct = childEntity.AddComponent<TransformComponent>(part.LocalTranslation, part.LocalScale);
                ct.Rotation = part.LocalRotation;

                auto partFullPath = m_CurrentAssetPath.parent_path() / part.RelativePath;
                childEntity.AddComponent<MeshInstance3D>(
                    part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_FallbackMesh,
                    partFullPath.string(),
                    part.LoadedMesh.SourceData.PatternMode,
                    part.LoadedMesh.SourceData.TileScale,
                    part.LoadedMesh.SourceData.ColorPrimary,
                    part.LoadedMesh.SourceData.ColorSecondary
                );

                childEntity.AddComponent<HierarchyComponent>(static_cast<entt::entity>(rootEntity));
                rootHierarchy.Children.push_back(static_cast<entt::entity>(childEntity));
            }
            AXLE_INFO("AssetPreviewPanel: Instantiated package '{0}' ({1} subparts)", rootTag, m_CurrentPackage.Parts.size());
        } else {
            const auto& part = m_CurrentPackage.Parts[0];
            std::string tag = m_CurrentPackage.Name.empty() ? m_CurrentAssetPath.stem().string() : m_CurrentPackage.Name;
            auto entity = m_Scene->CreateEntity(tag);
            entity.AddComponent<TransformComponent>(spawnPos, part.LocalScale);
            entity.AddComponent<MeshInstance3D>(
                part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_FallbackMesh,
                m_CurrentAssetPath.string(),
                part.LoadedMesh.SourceData.PatternMode,
                part.LoadedMesh.SourceData.TileScale,
                part.LoadedMesh.SourceData.ColorPrimary,
                part.LoadedMesh.SourceData.ColorSecondary
            );

            if (m_PhysicsScene) {
                BoxBodyProps bProps;
                bProps.Position = spawnPos;
                bProps.HalfExtent = part.LoadedMesh.SourceData.HalfExtent;
                bProps.IsDynamic = true;
                auto bodyId = m_PhysicsScene->CreateBox(bProps);
                entity.AddComponent<RigidBody3D>(bodyId, true);
                entity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, part.LoadedMesh.SourceData.HalfExtent, 0.70f, 0.15f);
            }
            AXLE_INFO("AssetPreviewPanel: Instantiated single mesh '{0}'", tag);
        }
    }

    void AssetPreviewPanel::RenderPreviewViewport() {
        if (!m_HasAsset) return;

        // 1. Controls Toolbar
        {
            if (ImGui::Button(m_TurntableActive ? "Pause Orbit" : "Play Orbit")) {
                m_TurntableActive = !m_TurntableActive;
            }
            ImGui::SameLine();
            if (ImGui::Button(m_WireframeMode ? "Shaded" : "Wireframe")) {
                m_WireframeMode = !m_WireframeMode;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset View")) {
                m_Pitch = 20.0f;
                m_Yaw = 35.0f;
                m_Zoom = 1.0f;
                m_PanOffset = glm::vec3(0.0f);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.0f);
            ImGui::SliderFloat("Speed", &m_TurntableSpeed, 0.1f, 3.0f, "%.1fx");
        }

        ImGui::Spacing();

        // 2. Interactive 3D Viewport
        float availW = ImGui::GetContentRegionAvail().x;
        float viewportH = std::clamp(availW * 0.72f, 180.0f, 360.0f);

        RenderPreview3D(availW, viewportH);

        u32 texID = m_Framebuffer->GetColorAttachmentRendererID();
        ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(availW, viewportH), ImVec2(0, 1), ImVec2(1, 0));

        m_IsViewportHovered = ImGui::IsItemHovered();

        // Mouse orbit controls on viewport
        if (m_IsViewportHovered) {
            // Scroll zoom
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                m_Zoom -= wheel * 0.12f;
                m_Zoom = std::clamp(m_Zoom, 0.25f, 4.0f);
            }

            // Left drag orbit
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                m_Yaw -= delta.x * 0.75f;
                m_Pitch += delta.y * 0.75f;
                m_Pitch = std::clamp(m_Pitch, -85.0f, 85.0f);
            }

            // Middle drag pan
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                m_PanOffset.x -= delta.x * 0.005f * m_MaxDim;
                m_PanOffset.y += delta.y * 0.005f * m_MaxDim;
            }
        }

        // Drag & Drop Source directly from 3D Viewport
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            std::string pathStr = m_CurrentAssetPath.string();
            ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", pathStr.c_str(), pathStr.size() + 1);
            ImGui::Text("Spawn: %s", m_CurrentAssetPath.filename().string().c_str());
            ImGui::EndDragDropSource();
        }
    }

    void AssetPreviewPanel::OnImGuiRender(bool* p_open) {
        if (p_open && !*p_open) return;

        ImGui::Begin("Asset Preview", p_open);

        if (!m_HasAsset) {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.45f, 0.70f, 0.95f, 1.0f), "  No 3D Asset Selected");
            ImGui::Spacing();
            ImGui::TextDisabled("  Click any 3D model asset (.axle, .glb, .obj)\n  in the Content Browser to inspect it in 3D.");
            ImGui::End();
            return;
        }

        RenderPreviewViewport();

        ImGui::Spacing();

        // Action Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.78f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.58f, 0.90f, 1.0f));
        if (ImGui::Button("+ Instantiate into Scene", ImVec2(-1.0f, 28.0f))) {
            SpawnCurrentAssetIntoScene();
        }
        ImGui::PopStyleColor(2);

        ImGui::End();
    }

}
