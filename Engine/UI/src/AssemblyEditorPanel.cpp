#include <Axle/AssemblyEditorPanel.hpp>
#include <Axle/Window.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/MeshResourceResolver.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/Log.hpp>
#include <Axle/Path.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/RHI/RHIDevice.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <sstream>

namespace Axle {
    void AssemblyEditorPanel::InitRHI(ImGuiLayer* layer) {
        m_ImGuiLayer=layer;
        m_DeferredRelease=layer?layer->GetDeferredReleaser():GuiDeferredReleaser{};
        if (RHI::RHIDevice::Get().IsNativeReady() && layer)
            m_RHIViewport.Init(layer,m_ViewportWidth,m_ViewportHeight,RHI::RHITextureFormat::RGBA8_UNORM,true);
    }

    AssemblyEditorPanel::AssemblyEditorPanel() {
        m_PreviewScene = std::make_unique<Scene>();
        SyncPreviewScene();
    }

    void AssemblyEditorPanel::SetSceneContext(Scene* scene, PhysicsScene* physicsScene, const SceneSerializerContext* serializerContext) {
        m_Scene = scene;
        m_PhysicsScene = physicsScene;
        m_SerializerContext = serializerContext;
        SyncPreviewScene();
    }

    void AssemblyEditorPanel::SetAssembly(const std::filesystem::path& filepath) {
        auto candidate = AssemblySerializer::LoadAssemblyData(filepath, m_SerializerContext);
        if (!candidate.IsValid) { m_StatusMessage = "Could not open Assembly: " + PathToUtf8(filepath); return; }
        m_CurrentAssetPath = filepath;
        m_AssemblyData = std::move(candidate);
        m_NodeIndexToEntityID.clear();
        m_HasAssembly = m_AssemblyData.IsValid;
        m_IsOpen = true;
        m_IsDirty = false;
        m_SelectedNodeIndex = 0;
        m_SelectedNode = m_HasAssembly ? &m_AssemblyData.RootNode : nullptr;

        m_Pitch = 22.0f;
        m_Yaw = 40.0f;
        m_Zoom = 1.0f;
        m_PanOffset = glm::vec3(0.0f);

        SyncPreviewScene();
        if (m_HasAssembly) m_SelectedNodeIndex = m_AssemblyData.RootNode.AuthoredUUID;
    }

    bool AssemblyEditorPanel::CapturePreviewData() {
        if (!m_PreviewScene || !m_HasAssembly || m_NodeIndexToEntityID.empty()) return true;
        std::function<bool(AssemblyNode&)> capture = [&](AssemblyNode& node) {
            const auto found = m_NodeIndexToEntityID.find(node.AuthoredUUID);
            if (found != m_NodeIndexToEntityID.end()) {
                Entity entity{static_cast<entt::entity>(found->second), m_PreviewScene.get()};
                if (!entity) return false;
                std::vector<UnknownComponentSection> sections;
                if (const auto* passive = m_PreviewScene->GetPassiveSections(entity)) sections = *passive;
                std::ostringstream encoded;
                if (!m_PreviewScene->SerializeExtensions(entity, encoded)) return false;
                UnknownComponentSection current;
                const auto flush = [&] {
                    if (current.SectionName.empty()) return true;
                    if (std::any_of(sections.begin(), sections.end(), [&](const auto& other) { return other.SectionName == current.SectionName; })) return false;
                    sections.push_back(std::move(current)); current = {}; return true;
                };
                std::istringstream input(encoded.str()); std::string line;
                while (std::getline(input, line)) {
                    if (line.starts_with("    ") && !line.starts_with("     ") && line.ends_with(':')) {
                        if (!flush()) return false;
                        current.SectionName = line.substr(4, line.size() - 5);
                    } else if (!current.SectionName.empty()) current.Lines.push_back(line);
                }
                if (!flush()) return false;
                node.PassiveSections = std::move(sections); node.AuthoredUUID = entity.GetUUID();
            }
            for (auto& child : node.Children) if (!capture(child)) return false;
            return true;
        };
        return capture(m_AssemblyData.RootNode);
    }
    bool AssemblyEditorPanel::SaveDocument() { return SaveAssembly(); }
    void AssemblyEditorPanel::DiscardChanges() { if (!m_CurrentAssetPath.empty()) SetAssembly(m_CurrentAssetPath); }
    void AssemblyEditorPanel::ShutdownPreview() {
        m_ComponentDrawer = {}; m_AddComponentDrawer = {};
        if (m_PreviewScene) m_PreviewScene->Clear();
        m_PreviewScene.reset(); m_NodeIndexToEntityID.clear();
    }
    bool AssemblyEditorPanel::SaveAssembly() {
        if (!m_HasAssembly || m_CurrentAssetPath.empty()) return false;
        if (!CapturePreviewData()) { m_StatusMessage = "Could not save plugin data in the Assembly preview."; return false; }

        if (AssemblySerializer::SaveAssemblyData(m_CurrentAssetPath, m_AssemblyData)) {
            m_IsDirty = false;
            m_StatusMessage = "Saved Assembly successfully!";
            m_StatusTimer = 3.5f;
            AXLE_INFO("Assembly Editor: Saved '{0}'", PathToUtf8(m_CurrentAssetPath.filename()));
            return true;
        }
        m_StatusMessage = "Could not save Assembly. Changes are still pending.";
        return false;
    }

    void AssemblyEditorPanel::InstantiateIntoScene() {
        if (!m_HasAssembly || !m_Scene || m_CurrentAssetPath.empty()) return;

        // Auto-save before instantiating if modified
        if (m_IsDirty && !SaveAssembly()) return;

        m_Scene->InstantiateAssembly(m_CurrentAssetPath, m_PhysicsScene, glm::vec3(0.0f), m_SerializerContext);
        m_StatusMessage = "Added Assembly instance into active Scene!";
        m_StatusTimer = 3.5f;
    }

    void AssemblyEditorPanel::SyncPreviewScene() {
        if (!CapturePreviewData()) { m_StatusMessage = "Could not preserve plugin edits while rebuilding the preview."; return; }
        m_PreviewScene = std::make_unique<Scene>();
        m_NodeIndexToEntityID.clear();

        // 1. Studio Lighting: Primary Key Light
        Entity fillLight = m_PreviewScene->CreateEntity("FillLight");
        auto& dlc2 = fillLight.AddComponent<DirectionalLightComponent>();
        dlc2.Azimuth = 225.0f;
        dlc2.Elevation = 30.0f;
        dlc2.Color = glm::vec3(0.85f, 0.90f, 1.0f);
        dlc2.Intensity = 0.4f;
        dlc2.AmbientIntensity = 0.0f;
        dlc2.CastShadows = false;

        Entity keyLight = m_PreviewScene->CreateEntity("KeyLight");
        auto& dlc1 = keyLight.AddComponent<DirectionalLightComponent>();
        dlc1.Azimuth = 45.0f;
        dlc1.Elevation = 50.0f;
        dlc1.Color = glm::vec3(1.0f);
        dlc1.Intensity = 1.0f;
        dlc1.AmbientIntensity = 0.32f;
        dlc1.CastShadows = false;

        // 2. Pedestal Grid
        if (m_ShowPedestal) {
            Entity pedestal = m_PreviewScene->CreateEntity("Pedestal");
            auto& tc = pedestal.GetComponent<TransformComponent>();
            tc.Translation = glm::vec3(0.0f, -0.01f, 0.0f);
            tc.Scale = glm::vec3(6.0f, 1.0f, 6.0f);

            auto& mesh = pedestal.AddComponent<MeshInstance3D>();
            mesh.AssetPath = "builtin://plane";
            mesh.PatternMode = 1; // 3D Caro
            mesh.TileScale = 2.0f;
            mesh.Color1 = glm::vec4(0.14f, 0.17f, 0.22f, 1.0f);
            mesh.Color2 = glm::vec4(0.09f, 0.11f, 0.15f, 1.0f);
            ResolveMeshResources(mesh, m_SerializerContext);
        }

        // 3. Recursive Assembly Hierarchy
        if (m_HasAssembly) {
            std::function<void(AssemblyNode&, Entity)> buildNode = [&](AssemblyNode& node, Entity parent) {
                Entity ent = node.AuthoredUUID ? m_PreviewScene->CreateEntityWithUUID(UUID(node.AuthoredUUID), node.Tag.empty() ? "Part" : node.Tag)
                                              : m_PreviewScene->CreateEntity(node.Tag.empty() ? "Part" : node.Tag);
                if (parent) {
                    ent.SetParent(parent, false);
                }
                ent.GetComponent<TransformComponent>() = node.Transform;
                for (const auto& section : node.PassiveSections) {
                    m_PreviewScene->StorePassiveSection(ent, section.SectionName, section.Lines);
                }

                if (node.HasMesh) {
                    auto& mesh = ent.AddComponent<MeshInstance3D>();
                    mesh = node.Mesh;
                    ResolveMeshResources(mesh, m_SerializerContext);
                }

                if (node.HasMaterial) {
                    auto& mat = ent.AddComponent<MaterialComponent>();
                    mat = node.Material;
                }

                if (node.HasSkeletalMesh) {
                    auto& skm = ent.AddComponent<SkeletalMeshComponent>();
                    skm = node.SkeletalMesh;
                }

                if (node.HasCollisionShape) {
                    auto& col = ent.AddComponent<CollisionShape3D>();
                    col = node.CollisionShape;
                }

                if (node.HasRigidBody) {
                    auto& rb = ent.AddComponent<RigidBody3D>();
                    rb = node.RigidBody;
                }

                node.AuthoredUUID = ent.GetUUID();
                m_NodeIndexToEntityID[node.AuthoredUUID] = static_cast<uint32_t>((entt::entity)ent);

                for (size_t i = 0; i < node.Children.size(); ++i) {
                    buildNode(node.Children[i], ent);
                }
            };

            buildNode(m_AssemblyData.RootNode, Entity{});
        }

        m_PreviewScene->UpdateWorldTransforms();
    }

    void AssemblyEditorPanel::RenderPreview(float width, float height) {
        if (width < 32.0f || height < 32.0f) return;

        u32 w = static_cast<u32>(width);
        u32 h = static_cast<u32>(height);
        m_ViewportWidth = w;
        m_ViewportHeight = h;

        if (!m_PreviewScene) {
            SyncPreviewScene();
        }

        if (m_TurntableActive) {
            m_Yaw += 30.0f * (1.0f / 60.0f) * m_TurntableSpeed;
            if (m_Yaw > 360.0f) m_Yaw -= 360.0f;
        }

        float fov = 45.0f;
        float aspect = static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);
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

        PerspectiveCamera cam(fov, aspect, 0.1f, 100.0f);
        cam.SetViewMatrix(view, eye);

        ViewRequest request;
        request.Kind = RenderViewKind::AssemblyPreview;
        request.Width = m_ViewportWidth;
        request.Height = m_ViewportHeight;
        request.Camera = cam;
        request.Settings.Wireframe = m_WireframeMode;
        request.Settings.ClearColor = glm::vec4(0.08f, 0.10f, 0.13f, 1.0f);
        request.Settings.EnableShadows = false;
        request.Settings.EnableSkybox = false;
        request.Name = "AssemblyPreviewView";

        if (RHI::RHIDevice::Get().IsNativeReady()) {
            if (!m_RHIViewport.IsValid() && m_ImGuiLayer) InitRHI(m_ImGuiLayer);
            if (!m_RHIViewport.IsValid() || m_RHIViewport.GetWidth() != m_ViewportWidth || m_RHIViewport.GetHeight() != m_ViewportHeight) {
                m_RHIViewport.Resize(m_ViewportWidth, m_ViewportHeight);
            }
            request.ColorTarget = m_RHIViewport.GetColorTexture();
            request.DepthTarget = m_RHIViewport.GetDepthTexture();
            request.ViewTargets = m_RHIViewport.GetTargets();
        } else
        {
            if (Window::HasActiveContext()) {
                if (!m_Framebuffer || m_Framebuffer->GetSpecification().Width != m_ViewportWidth || m_Framebuffer->GetSpecification().Height != m_ViewportHeight) {
                    FramebufferSpecification spec;
                    spec.Width = m_ViewportWidth;
                    spec.Height = m_ViewportHeight;
                    m_Framebuffer = Framebuffer::Create(spec);
                }
                request.GLTargetFBO = m_Framebuffer;
            }
        }

        std::unordered_set<uint32_t> selectedEntities;
        auto it = m_NodeIndexToEntityID.find(m_SelectedNodeIndex);
        if (it != m_NodeIndexToEntityID.end()) {
            selectedEntities.insert(it->second);
        }

        SceneExtractionOptions options;
        options.SelectedEntities = &selectedEntities;
        options.ResolveResources = true;

        RenderWorld world = ExtractRenderWorld(*m_PreviewScene, options);
        RenderResult result = RenderService::Get().RenderView(world, request);

        if (!result.Success || !result.ErrorMessage.empty()) {
            m_DiagnosticMessage = result.ErrorMessage;
        } else {
            m_DiagnosticMessage.clear();
        }
    }

    void AssemblyEditorPanel::DrawNodeHierarchy(AssemblyNode& node, std::uint64_t index, std::uint64_t& selectedId, AssemblyNode*& selectedNode) {
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
                SyncPreviewScene();
            }
            ImGui::EndPopup();
        }

        if (opened && !node.Children.empty()) {
            for (size_t i = 0; i < node.Children.size(); ++i) {
                auto childId = node.Children[i].AuthoredUUID;
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
            SyncPreviewScene();
        }
        ImGui::Separator();

        // 1. TransformComponent
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 translation = node->Transform.Translation;
            DrawVec3("Translation", translation);
            if (translation != node->Transform.Translation) {
                node->Transform.Translation = translation;
                m_IsDirty = true;
                SyncPreviewScene();
            }

            glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(node->Transform.Rotation));
            glm::vec3 oldEuler = eulerAngles;
            DrawVec3("Rotation", eulerAngles);
            if (eulerAngles != oldEuler) {
                node->Transform.Rotation = glm::quat(glm::radians(eulerAngles));
                m_IsDirty = true;
                SyncPreviewScene();
            }

            glm::vec3 scale = node->Transform.Scale;
            DrawVec3("Scale", scale, 1.0f);
            if (scale != node->Transform.Scale) {
                node->Transform.Scale = scale;
                m_IsDirty = true;
                SyncPreviewScene();
            }
        }

        // 2. MeshInstance3D
        if (ImGui::CollapsingHeader("Mesh Instance 3D", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Mesh", &node->HasMesh)) {
                m_IsDirty = true;
                SyncPreviewScene();
            }
            if (node->HasMesh) {
                char pathBuf[256];
                std::strncpy(pathBuf, node->Mesh.AssetPath.c_str(), sizeof(pathBuf) - 1);
                pathBuf[sizeof(pathBuf) - 1] = '\0';
                ImGui::Text("Mesh Path:");
                if (ImGui::InputText("##MeshAssetPath", pathBuf, sizeof(pathBuf))) {
                    node->Mesh.AssetPath = pathBuf;
                    node->Mesh.VAO = nullptr; // force reload
                    node->Mesh.RHIMeshAsset = nullptr;
                    m_IsDirty = true;
                    SyncPreviewScene();
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
                            node->Mesh.RHIMeshAsset = nullptr;
                            m_IsDirty = true;
                            SyncPreviewScene();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                const char* patterns[] = { "0: World XZ Caro", "1: 3D Caro", "2: Spherical Caro", "3: Solid Color" };
                int currentPat = node->Mesh.PatternMode;
                if (ImGui::Combo("Pattern Mode", &currentPat, patterns, 4)) {
                    node->Mesh.PatternMode = currentPat;
                    m_IsDirty = true;
                    SyncPreviewScene();
                }

                if (ImGui::SliderFloat("Tile Scale", &node->Mesh.TileScale, 0.2f, 32.0f, "%.1fx")) {
                    m_IsDirty = true;
                    SyncPreviewScene();
                }

                if (ImGui::ColorEdit4("Color 1", glm::value_ptr(node->Mesh.Color1))) {
                    m_IsDirty = true;
                    SyncPreviewScene();
                }
                if (ImGui::ColorEdit4("Color 2", glm::value_ptr(node->Mesh.Color2))) {
                    m_IsDirty = true;
                    SyncPreviewScene();
                }
            }
        }

        // 3. CollisionShape3D
        if (ImGui::CollapsingHeader("Collision Shape 3D", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Collider", &node->HasCollisionShape)) {
                m_IsDirty = true;
                SyncPreviewScene();
            }
            if (node->HasCollisionShape) {
                const char* shapeTypes[] = { "Box", "Sphere", "Cylinder", "Cone", "Plane", "Mesh" };
                int currentShape = (int)node->CollisionShape.ShapeType;
                if (ImGui::Combo("Shape Type", &currentShape, shapeTypes, 6)) {
                    node->CollisionShape.ShapeType = (CollisionShapeType)currentShape;
                    m_IsDirty = true;
                    SyncPreviewScene();
                }

                if (node->CollisionShape.ShapeType == CollisionShapeType::Box || node->CollisionShape.ShapeType == CollisionShapeType::Plane) {
                    DrawVec3("Half Extent", node->CollisionShape.HalfExtent, 0.5f);
                } else if (node->CollisionShape.ShapeType == CollisionShapeType::Sphere) {
                    if (ImGui::SliderFloat("Radius", &node->CollisionShape.Radius, 0.05f, 10.0f)) {
                        m_IsDirty = true;
                        SyncPreviewScene();
                    }
                } else if (node->CollisionShape.ShapeType == CollisionShapeType::Cylinder || node->CollisionShape.ShapeType == CollisionShapeType::Cone) {
                    if (ImGui::SliderFloat("Half Height", &node->CollisionShape.HalfHeight, 0.05f, 10.0f)) {
                        m_IsDirty = true;
                        SyncPreviewScene();
                    }
                    if (ImGui::SliderFloat("Radius", &node->CollisionShape.Radius, 0.05f, 10.0f)) {
                        m_IsDirty = true;
                        SyncPreviewScene();
                    }
                }

                DrawVec3("Collider Offset", node->CollisionShape.Offset, 0.0f);
                if (ImGui::SliderFloat("Friction", &node->CollisionShape.Friction, 0.0f, 1.0f)) m_IsDirty = true;
                if (ImGui::SliderFloat("Restitution", &node->CollisionShape.Restitution, 0.0f, 1.0f)) m_IsDirty = true;
                if (ImGui::Checkbox("Is Trigger", &node->CollisionShape.IsTrigger)) m_IsDirty = true;
            }
        }

        // 4. RigidBody3D
        if (ImGui::CollapsingHeader("Rigid Body 3D", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Rigid Body", &node->HasRigidBody)) {
                m_IsDirty = true;
                SyncPreviewScene();
            }
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
                    if (ImGui::MenuItem("Show Pedestal Grid", nullptr, &m_ShowPedestal)) {
                        SyncPreviewScene();
                    }
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
                DrawNodeHierarchy(m_AssemblyData.RootNode, m_AssemblyData.RootNode.AuthoredUUID, m_SelectedNodeIndex, m_SelectedNode);
            } else {
                ImGui::TextDisabled("No Assembly loaded.");
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Center Pane: 3D Viewport
            ImGui::BeginChild("AsmViewportPane", ImVec2(viewportWidth, totalHeight), true);
            {
                ImVec2 viewSize = ImGui::GetContentRegionAvail();
                RenderPreview(viewSize.x, viewSize.y);

                bool imageDrawn = false;
                if (RHI::RHIDevice::Get().IsNativeReady() && m_RHIViewport.GetImTextureID()) {
                    ImGui::Image((ImTextureID)m_RHIViewport.GetImTextureID(), viewSize);
                    imageDrawn = true;
                }
                if (!imageDrawn && m_Framebuffer) {
                    ImTextureID texID = (ImTextureID)(intptr_t)m_Framebuffer->GetColorAttachmentRendererID();
                    if(m_DeferredRelease)m_DeferredRelease([owner=m_Framebuffer]{});
                    ImGui::Image(texID, viewSize, ImVec2(0, 1), ImVec2(1, 0));
                    imageDrawn = true;
                }

                if (!imageDrawn) {
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    ImVec2 p1 = ImVec2(p0.x + viewSize.x, p0.y + viewSize.y);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(p0, p1, IM_COL32(20, 22, 26, 255));
                    drawList->AddRect(p0, p1, IM_COL32(40, 44, 52, 255));
                    const char* txt = "Assembly preview requires an active render backend";
                    ImVec2 txtSize = ImGui::CalcTextSize(txt);
                    ImVec2 txtPos = ImVec2(p0.x + (viewSize.x - txtSize.x) * 0.5f, p0.y + (viewSize.y - txtSize.y) * 0.5f);
                    drawList->AddText(txtPos, IM_COL32(130, 135, 145, 255), txt);
                    ImGui::Dummy(viewSize);
                }

                // Show diagnostic warning banner if feature not supported (e.g. Wireframe on RHI)
                if (!m_DiagnosticMessage.empty()) {
                    ImVec2 badgePos = ImGui::GetItemRectMin();
                    badgePos.x += 10.0f;
                    badgePos.y += 10.0f;
                    ImGui::GetWindowDrawList()->AddRectFilled(badgePos, ImVec2(badgePos.x + 360.0f, badgePos.y + 24.0f), IM_COL32(30, 30, 35, 220), 4.0f);
                    ImGui::GetWindowDrawList()->AddText(ImVec2(badgePos.x + 8.0f, badgePos.y + 4.0f), IM_COL32(255, 190, 60, 255), m_DiagnosticMessage.c_str());
                }

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
                const auto found = m_NodeIndexToEntityID.find(m_SelectedNodeIndex);
                if (m_PreviewScene && found != m_NodeIndexToEntityID.end()) {
                    Entity selected{static_cast<entt::entity>(found->second), m_PreviewScene.get()};
                    if (selected && m_ComponentDrawer && m_ComponentDrawer(selected)) m_IsDirty = true;
                    if (selected && m_AddComponentDrawer) {
                        if (ImGui::Button("Add plugin component")) ImGui::OpenPopup("AssemblyPluginComponents");
                        if (ImGui::BeginPopup("AssemblyPluginComponents")) {
                            if (m_AddComponentDrawer(selected)) m_IsDirty = true;
                            ImGui::EndPopup();
                        }
                    }
                }
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
            ImGui::TextDisabled("Location: %s", PathToUtf8(m_CurrentAssetPath).c_str());
        }
        ImGui::End();

        if (p_open) {
            *p_open = m_IsOpen;
        }
    }

}
