#include <Axle/AssemblyEditorPanel.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/Log.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

namespace Axle {

    static const char* s_AsmVertSrc = R"(
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

    static const char* s_AsmFragSrc = R"(
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
        uniform int u_IsSelected;

        void main() {
            if (u_WireframeMode == 1) {
                o_Color = (u_IsSelected == 1) ? vec4(1.0, 0.75, 0.15, 1.0) : vec4(0.25, 0.88, 1.0, 1.0);
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
            } else if (u_PatternMode == 2) {
                vec3 nLocal = normalize(v_LocalPos);
                float u = atan(nLocal.z, nLocal.x) / (2.0 * 3.14159265359) + 0.5;
                float v = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265359 + 0.5;
                vec2 coord = floor(vec2(u * u_TileScale, v * (u_TileScale * 0.5)));
                pattern = mod(coord.x + coord.y, 2.0);
                normal = normalize((u_Transform * vec4(v_LocalPos, 0.0)).xyz);
            } else {
                vec3 dx = dFdx(v_WorldPos);
                vec3 dy = dFdy(v_WorldPos);
                normal = normalize(cross(dx, dy));
                if (dot(normal, normalize(-v_WorldPos)) < 0.0) normal = -normal;
                pattern = 0.0;
            }

            vec4 baseColor = mix(u_Color1, u_Color2, pattern);
            if (u_IsSelected == 1) {
                baseColor = mix(baseColor, vec4(1.0, 0.85, 0.3, 1.0), 0.25);
            }

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

    AssemblyEditorPanel::AssemblyEditorPanel() {
        FramebufferSpecification spec;
        spec.Width = m_ViewportWidth;
        spec.Height = m_ViewportHeight;
        m_Framebuffer = Framebuffer::Create(spec);

        m_Shader = std::make_unique<Shader>(s_AsmVertSrc, s_AsmFragSrc);
    }

    void AssemblyEditorPanel::SetSceneContext(Scene* scene, PhysicsScene* physicsScene, const SceneSerializerContext* serializerContext) {
        m_Scene = scene;
        m_PhysicsScene = physicsScene;
        m_SerializerContext = serializerContext;
    }

    void AssemblyEditorPanel::SetAssembly(const std::filesystem::path& filepath) {
        m_CurrentAssetPath = filepath;
        m_AssemblyData = AssemblySerializer::LoadAssemblyData(filepath, m_SerializerContext);
        m_HasAssembly = m_AssemblyData.IsValid;
        m_IsOpen = true;
        m_IsDirty = false;
        m_SelectedNodeIndex = 0;
        m_SelectedNode = m_HasAssembly ? &m_AssemblyData.RootNode : nullptr;

        m_Pitch = 22.0f;
        m_Yaw = 40.0f;
        m_Zoom = 1.0f;
        m_PanOffset = glm::vec3(0.0f);
    }

    void AssemblyEditorPanel::SaveAssembly() {
        if (!m_HasAssembly || m_CurrentAssetPath.empty()) return;

        if (AssemblySerializer::SaveAssemblyData(m_CurrentAssetPath, m_AssemblyData)) {
            m_IsDirty = false;
            m_StatusMessage = "Saved Assembly successfully!";
            m_StatusTimer = 3.5f;
            AXLE_INFO("Assembly Editor: Saved '{0}'", m_CurrentAssetPath.filename().string());
        }
    }

    void AssemblyEditorPanel::InstantiateIntoScene() {
        if (!m_HasAssembly || !m_Scene || m_CurrentAssetPath.empty()) return;

        // Auto-save before instantiating if modified
        if (m_IsDirty) {
            SaveAssembly();
        }

        m_Scene->InstantiateAssembly(m_CurrentAssetPath, m_PhysicsScene, glm::vec3(0.0f), m_SerializerContext);
        m_StatusMessage = "Added Assembly instance into active Scene!";
        m_StatusTimer = 3.5f;
    }

    static void RenderNodeRecursive(Shader& shader, AssemblyNode& node, const glm::mat4& parentTransform,
                                   int& currentIndex, int selectedIndex, const SceneSerializerContext* context, bool wireframe) {
        glm::mat4 localTransform = node.Transform.GetTransform();
        glm::mat4 worldTransform = parentTransform * localTransform;

        bool isSelected = (currentIndex == selectedIndex);

        if (node.HasMesh) {
            std::shared_ptr<VertexArray> vao = node.Mesh.VAO;
            if (!vao && context) {
                if (node.Mesh.AssetPath == "builtin://sphere") vao = context->SphereMesh;
                else if (node.Mesh.AssetPath == "builtin://cylinder") vao = context->CylinderMesh;
                else if (node.Mesh.AssetPath == "builtin://cone") vao = context->ConeMesh;
                else if (node.Mesh.AssetPath == "builtin://plane") vao = context->PlaneMesh;
                else if (!node.Mesh.AssetPath.empty() && node.Mesh.AssetPath != "builtin://cube") {
                    auto resolved = FileSystem::ResolveAssetPath(node.Mesh.AssetPath);
                    if (std::filesystem::exists(resolved)) {
                        auto loaded = AssetSerializer::LoadMesh(resolved);
                        if (loaded.Success) {
                            node.Mesh.VAO = loaded.VAO;
                            vao = loaded.VAO;
                        }
                    }
                }
                if (!vao) vao = context->CubeMesh;
            }

            if (vao) {
                shader.SetMat4("u_Transform", worldTransform);
                shader.SetInt("u_PatternMode", node.Mesh.PatternMode);
                shader.SetFloat("u_TileScale", node.Mesh.TileScale);
                shader.SetFloat4("u_Color1", node.Mesh.Color1);
                shader.SetFloat4("u_Color2", node.Mesh.Color2);
                shader.SetInt("u_IsSelected", isSelected ? 1 : 0);

                vao->Bind();
                if (wireframe) {
                    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                    glDrawElements(GL_TRIANGLES, (GLsizei)vao->GetIndexBuffer()->GetCount(), GL_UNSIGNED_INT, nullptr);
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                } else {
                    glDrawElements(GL_TRIANGLES, (GLsizei)vao->GetIndexBuffer()->GetCount(), GL_UNSIGNED_INT, nullptr);
                }
            }
        }

        currentIndex++;
        for (auto& child : node.Children) {
            RenderNodeRecursive(shader, child, worldTransform, currentIndex, selectedIndex, context, wireframe);
        }
    }

    void AssemblyEditorPanel::DrawPedestalGrid(const glm::mat4& viewProj, float radius) {
        if (!m_ShowPedestal || !m_SerializerContext || !m_SerializerContext->PlaneMesh) return;

        m_Shader->Bind();
        m_Shader->SetMat4("u_ViewProjection", viewProj);
        glm::mat4 pedModel = glm::scale(glm::mat4(1.0f), glm::vec3(radius, 1.0f, radius));
        m_Shader->SetMat4("u_Transform", pedModel);
        m_Shader->SetInt("u_PatternMode", 1);
        m_Shader->SetFloat("u_TileScale", 2.0f);
        m_Shader->SetFloat4("u_Color1", glm::vec4(0.14f, 0.17f, 0.22f, 1.0f));
        m_Shader->SetFloat4("u_Color2", glm::vec4(0.09f, 0.11f, 0.15f, 1.0f));
        m_Shader->SetInt("u_WireframeMode", 0);
        m_Shader->SetInt("u_IsSelected", 0);

        RenderCommand::DrawIndexed(m_SerializerContext->PlaneMesh);
    }

    void AssemblyEditorPanel::RenderPreview3D(float width, float height) {
        if (width < 32 || height < 32) return;

        u32 w = (u32)width;
        u32 h = (u32)height;
        if (w != m_ViewportWidth || h != m_ViewportHeight) {
            m_ViewportWidth = w;
            m_ViewportHeight = h;
            m_Framebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
        }

        m_Framebuffer->Bind();
        RenderCommand::SetViewport(0, 0, m_ViewportWidth, m_ViewportHeight);
        RenderCommand::SetClearColor({ 0.08f, 0.10f, 0.13f, 1.0f });
        RenderCommand::Clear();

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        if (m_TurntableActive) {
            m_Yaw += 30.0f * (1.0f / 60.0f) * m_TurntableSpeed;
            if (m_Yaw > 360.0f) m_Yaw -= 360.0f;
        }

        float fov = 45.0f;
        float aspect = (float)m_ViewportWidth / (float)m_ViewportHeight;
        glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, 0.1f, 100.0f);

        float distance = 4.0f * m_Zoom;
        float radYaw = glm::radians(m_Yaw);
        float radPitch = glm::radians(m_Pitch);

        glm::vec3 eye;
        eye.x = distance * cosf(radPitch) * sinf(radYaw);
        eye.y = distance * sinf(radPitch);
        eye.z = distance * cosf(radPitch) * cosf(radYaw);
        eye += m_PanOffset;

        glm::vec3 target = m_PanOffset;
        glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 viewProj = proj * view;

        DrawPedestalGrid(viewProj, 3.0f);

        if (m_HasAssembly) {
            m_Shader->Bind();
            m_Shader->SetMat4("u_ViewProjection", viewProj);
            m_Shader->SetInt("u_WireframeMode", m_WireframeMode ? 1 : 0);
            m_Shader->SetFloat3("u_LightDir", glm::vec3(0.5f, 0.8f, 0.6f));

            int traverseIndex = 0;
            RenderNodeRecursive(*m_Shader, m_AssemblyData.RootNode, glm::mat4(1.0f), traverseIndex,
                                m_SelectedNodeIndex, m_SerializerContext, m_WireframeMode);
        }

        m_Framebuffer->Unbind();
    }

    void AssemblyEditorPanel::DrawNodeHierarchy(AssemblyNode& node, int index, int& selectedId, AssemblyNode*& selectedNode) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (index == selectedId) {
            flags |= ImGuiTreeNodeFlags_Selected;
            selectedNode = &node;
        }

        if (node.Children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        std::string label = node.Tag + "##Node_" + std::to_string(index);
        bool opened = ImGui::TreeNodeEx((void*)(intptr_t)index, flags, "%s", node.Tag.c_str());

        if (ImGui::IsItemClicked()) {
            selectedId = index;
            selectedNode = &node;
        }

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("+ Add Child Part")) {
                AssemblyNode newChild;
                newChild.Tag = "Part_" + std::to_string(node.Children.size() + 1);
                newChild.HasMesh = true;
                newChild.Mesh.AssetPath = "builtin://cube";
                newChild.HasCollisionShape = true;
                node.Children.push_back(newChild);
                m_IsDirty = true;
            }
            ImGui::EndPopup();
        }

        if (opened && !node.Children.empty()) {
            static int runningIndex = 0;
            for (size_t i = 0; i < node.Children.size(); ++i) {
                int childId = index * 100 + (int)i + 1;
                DrawNodeHierarchy(node.Children[i], childId, selectedId, selectedNode);
            }
            ImGui::TreePop();
        }
    }

    static void DrawVec3(const char* label, glm::vec3& values, float resetValue = 0.0f) {
        ImGui::PushID(label);
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 95.0f);
        ImGui::Text("%s", label);
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::DragFloat("##X", &values.x, 0.05f);
        ImGui::SameLine();
        ImGui::DragFloat("##Y", &values.y, 0.05f);
        ImGui::SameLine();
        ImGui::DragFloat("##Z", &values.z, 0.05f);
        ImGui::PopItemWidth();
        ImGui::PopItemWidth();
        ImGui::PopItemWidth();

        ImGui::Columns(1);
        ImGui::PopID();
    }

    void AssemblyEditorPanel::DrawNodeInspector(AssemblyNode* node) {
        if (!node) {
            ImGui::TextDisabled("No part selected in hierarchy.");
            return;
        }

        char tagBuf[128];
        std::strncpy(tagBuf, node->Tag.c_str(), sizeof(tagBuf) - 1);
        tagBuf[sizeof(tagBuf) - 1] = '\0';
        ImGui::Text("Part Name:");
        ImGui::SameLine();
        if (ImGui::InputText("##PartName", tagBuf, sizeof(tagBuf))) {
            node->Tag = tagBuf;
            m_IsDirty = true;
        }
        ImGui::Separator();

        // 1. TransformComponent
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 translation = node->Transform.Translation;
            DrawVec3("Translation", translation);
            if (translation != node->Transform.Translation) {
                node->Transform.Translation = translation;
                m_IsDirty = true;
            }

            glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(node->Transform.Rotation));
            glm::vec3 oldEuler = eulerAngles;
            DrawVec3("Rotation", eulerAngles);
            if (eulerAngles != oldEuler) {
                node->Transform.Rotation = glm::quat(glm::radians(eulerAngles));
                m_IsDirty = true;
            }

            glm::vec3 scale = node->Transform.Scale;
            DrawVec3("Scale", scale, 1.0f);
            if (scale != node->Transform.Scale) {
                node->Transform.Scale = scale;
                m_IsDirty = true;
            }
        }

        // 2. MeshInstance3D
        if (ImGui::CollapsingHeader("Mesh Instance 3D", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Mesh", &node->HasMesh);
            if (node->HasMesh) {
                char pathBuf[256];
                std::strncpy(pathBuf, node->Mesh.AssetPath.c_str(), sizeof(pathBuf) - 1);
                pathBuf[sizeof(pathBuf) - 1] = '\0';
                ImGui::Text("Mesh Path:");
                if (ImGui::InputText("##MeshAssetPath", pathBuf, sizeof(pathBuf))) {
                    node->Mesh.AssetPath = pathBuf;
                    node->Mesh.VAO = nullptr; // force reload
                    m_IsDirty = true;
                }

                // Drop target for 3D model from Content Browser
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        std::string droppedPath = (const char*)payload->Data;
                        std::string ext = std::filesystem::path(droppedPath).extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb") {
                            node->Mesh.AssetPath = droppedPath;
                            node->Mesh.VAO = nullptr;
                            m_IsDirty = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                const char* patterns[] = { "0: World XZ Caro", "1: 3D Caro", "2: Spherical Caro", "3: Solid Color" };
                int currentPat = node->Mesh.PatternMode;
                if (ImGui::Combo("Pattern Mode", &currentPat, patterns, 4)) {
                    node->Mesh.PatternMode = currentPat;
                    m_IsDirty = true;
                }

                if (ImGui::SliderFloat("Tile Scale", &node->Mesh.TileScale, 0.2f, 32.0f, "%.1fx")) {
                    m_IsDirty = true;
                }

                if (ImGui::ColorEdit4("Color 1", glm::value_ptr(node->Mesh.Color1))) m_IsDirty = true;
                if (ImGui::ColorEdit4("Color 2", glm::value_ptr(node->Mesh.Color2))) m_IsDirty = true;
            }
        }

        // 3. CollisionShape3D
        if (ImGui::CollapsingHeader("Collision Shape 3D", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Collider", &node->HasCollisionShape);
            if (node->HasCollisionShape) {
                const char* shapeTypes[] = { "Box", "Sphere", "Cylinder", "Cone", "Plane", "Mesh" };
                int currentShape = (int)node->CollisionShape.ShapeType;
                if (ImGui::Combo("Shape Type", &currentShape, shapeTypes, 6)) {
                    node->CollisionShape.ShapeType = (CollisionShapeType)currentShape;
                    m_IsDirty = true;
                }

                if (node->CollisionShape.ShapeType == CollisionShapeType::Box || node->CollisionShape.ShapeType == CollisionShapeType::Plane) {
                    DrawVec3("Half Extent", node->CollisionShape.HalfExtent, 0.5f);
                } else if (node->CollisionShape.ShapeType == CollisionShapeType::Sphere) {
                    if (ImGui::SliderFloat("Radius", &node->CollisionShape.Radius, 0.05f, 10.0f)) m_IsDirty = true;
                } else if (node->CollisionShape.ShapeType == CollisionShapeType::Cylinder || node->CollisionShape.ShapeType == CollisionShapeType::Cone) {
                    if (ImGui::SliderFloat("Half Height", &node->CollisionShape.HalfHeight, 0.05f, 10.0f)) m_IsDirty = true;
                    if (ImGui::SliderFloat("Radius", &node->CollisionShape.Radius, 0.05f, 10.0f)) m_IsDirty = true;
                }

                DrawVec3("Collider Offset", node->CollisionShape.Offset, 0.0f);
                if (ImGui::SliderFloat("Friction", &node->CollisionShape.Friction, 0.0f, 1.0f)) m_IsDirty = true;
                if (ImGui::SliderFloat("Restitution", &node->CollisionShape.Restitution, 0.0f, 1.0f)) m_IsDirty = true;
                if (ImGui::Checkbox("Is Trigger", &node->CollisionShape.IsTrigger)) m_IsDirty = true;
            }
        }

        // 4. RigidBody3D
        if (ImGui::CollapsingHeader("Rigid Body 3D", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Rigid Body", &node->HasRigidBody);
            if (node->HasRigidBody) {
                const char* motions[] = { "Static", "Kinematic", "Dynamic" };
                int curMotion = (int)node->RigidBody.MotionType;
                if (ImGui::Combo("Motion Type", &curMotion, motions, 3)) {
                    node->RigidBody.MotionType = (BodyMotionType)curMotion;
                    m_IsDirty = true;
                }

                if (node->RigidBody.MotionType == BodyMotionType::Dynamic) {
                    if (ImGui::DragFloat("Mass (kg)", &node->RigidBody.Mass, 1.0f, 0.1f, 50000.0f)) m_IsDirty = true;
                    if (ImGui::SliderFloat("Linear Damping", &node->RigidBody.LinearDamping, 0.0f, 1.0f)) m_IsDirty = true;
                    if (ImGui::SliderFloat("Angular Damping", &node->RigidBody.AngularDamping, 0.0f, 1.0f)) m_IsDirty = true;
                    if (ImGui::SliderFloat("Gravity Factor", &node->RigidBody.GravityFactor, 0.0f, 5.0f)) m_IsDirty = true;
                }
            }
        }
    }

    void AssemblyEditorPanel::OnImGuiRender(bool* p_open) {
        if (p_open) {
            m_IsOpen = *p_open;
        }
        if (!m_IsOpen) return;

        ImGui::SetNextWindowSize(ImVec2(1050.0f, 650.0f), ImGuiCond_FirstUseEver);
        std::string winTitle = "Assembly Editor: " + (m_HasAssembly ? m_AssemblyData.Name : "No Assembly") +
                               (m_IsDirty ? " *###AssemblyEditor" : "###AssemblyEditor");

        if (ImGui::Begin(winTitle.c_str(), &m_IsOpen, ImGuiWindowFlags_MenuBar)) {
            // Check Ctrl+S inside window
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
                SaveAssembly();
            }

            // Top Menu Bar
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Save Assembly", "Ctrl+S", false, m_HasAssembly)) {
                        SaveAssembly();
                    }
                    if (ImGui::MenuItem("Add to Scene", nullptr, false, m_HasAssembly && m_Scene != nullptr)) {
                        InstantiateIntoScene();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Close")) {
                        m_IsOpen = false;
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Show Pedestal Grid", nullptr, &m_ShowPedestal);
                    ImGui::MenuItem("Wireframe Mode", nullptr, &m_WireframeMode);
                    ImGui::MenuItem("Turntable Auto-Orbit", nullptr, &m_TurntableActive);
                    ImGui::EndMenu();
                }

                ImGui::EndMenuBar();
            }

            // Status bar notification
            if (m_StatusTimer > 0.0f) {
                m_StatusTimer -= 1.0f / 60.0f;
                ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.0f), "%s", m_StatusMessage.c_str());
                ImGui::Separator();
            }

            // Layout 3 Columns: [Hierarchy Tree | 3D Viewport | Inspector]
            float totalWidth = ImGui::GetContentRegionAvail().x;
            float totalHeight = ImGui::GetContentRegionAvail().y - 38.0f; // leave room for action bar

            float hierarchyWidth = 220.0f;
            float inspectorWidth = 340.0f;
            float viewportWidth = std::max(totalWidth - hierarchyWidth - inspectorWidth - 24.0f, 200.0f);

            // Left Pane: Hierarchy
            ImGui::BeginChild("AsmHierarchyPane", ImVec2(hierarchyWidth, totalHeight), true);
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Assembly Parts");
            ImGui::Separator();
            if (m_HasAssembly) {
                int runningIndex = 0;
                DrawNodeHierarchy(m_AssemblyData.RootNode, 0, m_SelectedNodeIndex, m_SelectedNode);
            } else {
                ImGui::TextDisabled("No Assembly loaded.");
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Center Pane: 3D Viewport
            ImGui::BeginChild("AsmViewportPane", ImVec2(viewportWidth, totalHeight), true);
            {
                ImVec2 viewSize = ImGui::GetContentRegionAvail();
                RenderPreview3D(viewSize.x, viewSize.y);

                ImTextureID texID = (ImTextureID)(intptr_t)m_Framebuffer->GetColorAttachmentRendererID();
                ImGui::Image(texID, viewSize, ImVec2(0, 1), ImVec2(1, 0));

                m_IsViewportHovered = ImGui::IsItemHovered();
                if (m_IsViewportHovered) {
                    ImGuiIO& io = ImGui::GetIO();
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                        m_Yaw += io.MouseDelta.x * 0.45f;
                        m_Pitch -= io.MouseDelta.y * 0.45f;
                        m_Pitch = std::clamp(m_Pitch, -89.0f, 89.0f);
                    }
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                        m_PanOffset.x -= io.MouseDelta.x * 0.005f * m_Zoom;
                        m_PanOffset.y += io.MouseDelta.y * 0.005f * m_Zoom;
                    }
                    if (io.MouseWheel != 0.0f) {
                        m_Zoom *= (io.MouseWheel > 0 ? 0.88f : 1.14f);
                        m_Zoom = std::clamp(m_Zoom, 0.1f, 10.0f);
                    }
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Right Pane: Inspector
            ImGui::BeginChild("AsmInspectorPane", ImVec2(inspectorWidth, totalHeight), true);
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Part Inspector");
            ImGui::Separator();
            if (m_HasAssembly) {
                DrawNodeInspector(m_SelectedNode);
            } else {
                ImGui::TextDisabled("Select an entity.");
            }
            ImGui::EndChild();

            // Bottom Action Bar
            ImGui::Spacing();
            if (ImGui::Button("Save Assembly", ImVec2(140.0f, 28.0f))) {
                SaveAssembly();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add to Scene", ImVec2(140.0f, 28.0f))) {
                InstantiateIntoScene();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Location: %s", m_CurrentAssetPath.string().c_str());
        }
        ImGui::End();

        if (p_open) {
            *p_open = m_IsOpen;
        }
    }

}
