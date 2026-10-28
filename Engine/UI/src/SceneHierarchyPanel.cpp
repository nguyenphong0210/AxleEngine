#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/UIPalettePanel.hpp>
#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/PhysicsMaterial.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/Window.hpp>
#include <Axle/PlatformUtils.hpp>
#include <Axle/AudioEngine.hpp>
#include <Axle/AudioSource.hpp>
#include <Axle/AudioClip.hpp>
#include <Axle/VehicleAudio.hpp>
#include <Axle/UIVectorIcons.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <Jolt/Physics/Body/BodyLock.h>
#include <cstring>
#include <filesystem>

#include <Axle/VertexArray.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <GLFW/glfw3.h>

namespace Axle {

    static bool ContainsIgnoreCase(const std::string& str, const std::string& query) {
        if (query.empty()) return true;
        auto it = std::search(
            str.begin(), str.end(),
            query.begin(), query.end(),
            [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
        );
        return it != str.end();
    }

    void SceneHierarchyPanel::DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue, float columnWidth, float speed) {
        ImGui::PushID(label.c_str());

        // Unity Inspector Row Layout with clean horizontal alignment
        float totalWidth = ImGui::GetContentRegionAvail().x;
        float labelWidth = std::min(columnWidth, totalWidth * 0.32f);
        float controlsWidth = totalWidth - labelWidth;

        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label.c_str());
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Reset All Axes")) {
                values = glm::vec3(resetValue);
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine(labelWidth);

        // 3 Axes: X, Y, Z with subtle spacing and Unity pastel badges
        float spacing = 4.0f;
        float itemWidth = (controlsWidth - spacing * 2.0f) / 3.0f;
        float badgeWidth = 16.0f;
        float dragWidth = std::max(20.0f, itemWidth - badgeWidth);
        float lineHeight = ImGui::GetFrameHeight();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0.0f, 0.0f });

        auto DrawAxis = [&](const char* axisLabel, float& val, const ImVec4& color, const ImVec4& hoverColor, const char* id) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

            if (ImGui::Button(axisLabel, ImVec2(badgeWidth, lineHeight))) {
                val = resetValue;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to reset %s to %.2f (or drag to scrub)", axisLabel, resetValue);
            }
            ImGui::PopStyleColor(4);

            ImGui::SameLine();
            ImGui::SetNextItemWidth(dragWidth);
            ImGui::DragFloat(id, &val, speed, 0.0f, 0.0f, "%.2f");
            ImGui::PopStyleVar();
        };

        // Axis X - Unity Coral Red
        DrawAxis("X", values.x, ImVec4(0.85f, 0.26f, 0.26f, 1.0f), ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "##X");

        ImGui::SameLine(0.0f, spacing);

        // Axis Y - Unity Emerald Green
        DrawAxis("Y", values.y, ImVec4(0.30f, 0.68f, 0.31f, 1.0f), ImVec4(0.38f, 0.78f, 0.40f, 1.0f), "##Y");

        ImGui::SameLine(0.0f, spacing);

        // Axis Z - Unity Royal Blue
        DrawAxis("Z", values.z, ImVec4(0.20f, 0.52f, 0.88f, 1.0f), ImVec4(0.28f, 0.62f, 0.98f, 1.0f), "##Z");

        ImGui::PopStyleVar();
        ImGui::PopID();
    }


    SceneHierarchyPanel::SceneHierarchyPanel(Scene* scene, PhysicsScene* physicsScene) {
        SetContext(scene, physicsScene);
    }

    void SceneHierarchyPanel::SetContext(Scene* scene, PhysicsScene* physicsScene) {
        m_Scene = scene;
        m_PhysicsScene = physicsScene;
        m_SelectedEntity = {};
        m_SelectedAsset.clear();
        if (m_AssetPreviewPanel) m_AssetPreviewPanel->SetAsset({});
    }

    void SceneHierarchyPanel::SetSelectedEntity(Entity entity) {
        if (m_SelectedEntity != entity) {
            StopAudioPreview();
        }
        m_SelectedEntity = entity;
        m_InspectorMode = InspectorMode::Entity;
    }

    void SceneHierarchyPanel::StopAudioPreview() {
        if (m_EditorPreviewAudioSource) {
            m_EditorPreviewAudioSource->Stop();
            m_EditorPreviewAudioSource->Rewind();
        }
    }

    void SceneHierarchyPanel::SetSelectedAsset(const std::filesystem::path& assetPath) {
        if (assetPath.empty()) {
            ClearSelectedAsset();
            return;
        }
        m_SelectedAsset = assetPath;
        m_InspectorMode = InspectorMode::Asset;
        if (m_AssetPreviewPanel) {
            m_AssetPreviewPanel->SetAsset(assetPath);
        }
    }

    void SceneHierarchyPanel::ClearSelectedAsset() {
        m_SelectedAsset.clear();
        m_InspectorMode = InspectorMode::Entity;
        if (m_AssetPreviewPanel) {
            m_AssetPreviewPanel->SetAsset({});
        }
    }

    void SceneHierarchyPanel::SetMeshes(
        std::shared_ptr<VertexArray> cube,
        std::shared_ptr<VertexArray> sphere,
        std::shared_ptr<VertexArray> cylinder,
        std::shared_ptr<VertexArray> cone,
        std::shared_ptr<VertexArray> plane
    ) {
        m_CubeMesh = cube;
        m_SphereMesh = sphere;
        m_CylinderMesh = cylinder;
        m_ConeMesh = cone;
        m_PlaneMesh = plane;
    }

    Entity SceneHierarchyPanel::CreatePrimitive(int primitiveType, const glm::vec3& position, bool isDynamic) {
        if (!m_Scene) return {};

        Entity entity{};
        const glm::vec4 defaultPrimaryColor{ 0.88f, 0.89f, 0.92f, 1.0f };   // Studio Light Silver
        const glm::vec4 defaultSecondaryColor{ 0.45f, 0.48f, 0.52f, 1.0f }; // Subtle Slate Accent

        switch (primitiveType) {
            case 0: { // 3D Cube
                std::string name = isDynamic ? "Dynamic Box" : "Cube";
                entity = m_Scene->CreateEntity(name);
                entity.AddComponent<TransformComponent>(position, glm::vec3(1.0f, 1.0f, 1.0f));
                if (m_CubeMesh) {
                    entity.AddComponent<MeshInstance3D>(
                        m_CubeMesh, 1, 4.0f,
                        defaultPrimaryColor,
                        defaultSecondaryColor
                    );
                }
                if (isDynamic) {
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, glm::vec3(0.5f));
                    if (m_PhysicsScene) {
                        BoxBodyProps bProps;
                        bProps.Position = position;
                        bProps.HalfExtent = glm::vec3(0.5f, 0.5f, 0.5f);
                        bProps.IsDynamic = true;
                        auto bodyID = m_PhysicsScene->CreateBox(bProps);
                        entity.AddComponent<RigidBody3D>(bodyID, true);
                    }
                }
                break;
            }
            case 1: { // 3D Sphere
                std::string name = isDynamic ? "Dynamic Sphere" : "Sphere";
                entity = m_Scene->CreateEntity(name);
                entity.AddComponent<TransformComponent>(position, glm::vec3(1.0f, 1.0f, 1.0f));
                if (m_SphereMesh) {
                    entity.AddComponent<MeshInstance3D>(
                        m_SphereMesh, 2, 4.0f,
                        defaultPrimaryColor,
                        defaultSecondaryColor
                    );
                }
                if (isDynamic) {
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Sphere, 0.5f);
                    if (m_PhysicsScene) {
                        SphereBodyProps sProps;
                        sProps.Position = position;
                        sProps.Radius = 0.5f;
                        sProps.IsDynamic = true;
                        auto bodyID = m_PhysicsScene->CreateSphere(sProps);
                        entity.AddComponent<RigidBody3D>(bodyID, true);
                    }
                }
                break;
            }
            case 2: { // 3D Cylinder
                std::string name = isDynamic ? "Dynamic Cylinder" : "Cylinder";
                entity = m_Scene->CreateEntity(name);
                entity.AddComponent<TransformComponent>(position, glm::vec3(1.0f, 1.0f, 1.0f));
                if (m_CylinderMesh) {
                    entity.AddComponent<MeshInstance3D>(
                        m_CylinderMesh, 1, 4.0f,
                        defaultPrimaryColor,
                        defaultSecondaryColor
                    );
                }
                if (isDynamic) {
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Cylinder, 0.5f, 0.5f);
                    if (m_PhysicsScene) {
                        CylinderBodyProps cProps;
                        cProps.Position = position;
                        cProps.HalfHeight = 0.5f;
                        cProps.Radius = 0.5f;
                        cProps.IsDynamic = true;
                        auto bodyID = m_PhysicsScene->CreateCylinder(cProps);
                        entity.AddComponent<RigidBody3D>(bodyID, true);
                    }
                }
                break;
            }
            case 3: { // 3D Cone
                std::string name = isDynamic ? "Dynamic Cone" : "Cone";
                entity = m_Scene->CreateEntity(name);
                entity.AddComponent<TransformComponent>(position, glm::vec3(1.0f, 1.0f, 1.0f));
                if (m_ConeMesh) {
                    entity.AddComponent<MeshInstance3D>(
                        m_ConeMesh, 1, 4.0f,
                        defaultPrimaryColor,
                        defaultSecondaryColor
                    );
                }
                if (isDynamic) {
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Cone, 0.5f, 0.5f);
                    if (m_PhysicsScene) {
                        ConeBodyProps cProps;
                        cProps.Position = position;
                        cProps.HalfHeight = 0.5f;
                        cProps.Radius = 0.5f;
                        cProps.IsDynamic = true;
                        auto bodyID = m_PhysicsScene->CreateCone(cProps);
                        entity.AddComponent<RigidBody3D>(bodyID, true);
                    }
                }
                break;
            }
            case 4: { // 3D Plane
                entity = m_Scene->CreateEntity("Plane");
                entity.AddComponent<TransformComponent>(position, glm::vec3(20.0f, 1.0f, 20.0f));
                auto& mc = entity.AddComponent<MeshInstance3D>(
                    m_PlaneMesh, 1, 8.0f,
                    glm::vec4(0.35f, 0.38f, 0.42f, 1.0f),
                    glm::vec4(0.20f, 0.22f, 0.25f, 1.0f)
                );
                mc.AssetPath = "builtin://plane";
                mc.RHIMeshAsset = RHIMeshRegistry::Get().GetPlane();
                break;
            }
            default:
                break;
        }

        m_SelectedEntity = entity;
        return entity;
    }

    void SceneHierarchyPanel::DrawCreateMenu() {
        bool hasSelected = m_SelectedEntity && m_Scene->GetRegistry().valid((entt::entity)m_SelectedEntity);

        if (ImGui::MenuItem("Create Empty")) {
            m_SelectedEntity = m_Scene->CreateEntity("GameObject");
        }
        if (ImGui::MenuItem("Create Empty Child", nullptr, false, hasSelected)) {
            if (hasSelected) {
                auto child = m_Scene->CreateEntity("Child");
                child.SetParent(m_SelectedEntity);
                m_SelectedEntity = child;
            }
        }
        ImGui::Separator();

        if (ImGui::BeginMenu("3D Object")) {
            if (ImGui::MenuItem("Cube")) {
                CreatePrimitive(0);
            }
            if (ImGui::MenuItem("Sphere")) {
                CreatePrimitive(1);
            }
            if (ImGui::MenuItem("Cylinder")) {
                CreatePrimitive(2);
            }
            if (ImGui::MenuItem("Cone")) {
                CreatePrimitive(3);
            }
            if (ImGui::MenuItem("Plane")) {
                CreatePrimitive(4);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Physics Templates")) {
            if (ImGui::MenuItem("Dynamic Box (Falling)")) {
                CreatePrimitive(0, glm::vec3(0.0f, 2.0f, 0.0f), true);
            }
            if (ImGui::MenuItem("Dynamic Sphere (Falling)")) {
                CreatePrimitive(1, glm::vec3(0.0f, 2.0f, 0.0f), true);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Light")) {
            if (ImGui::MenuItem("Directional Light")) {
                auto lightEntity = m_Scene->CreateEntity("Directional Light");
                lightEntity.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 25.0f, 0.0f);
                lightEntity.AddComponent<DirectionalLightComponent>();
                m_SelectedEntity = lightEntity;
            }
            if (ImGui::MenuItem("Point Light")) {
                auto pointLight = m_Scene->CreateEntity("Point Light");
                pointLight.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 3.0f, 0.0f);
                pointLight.AddComponent<PointLightComponent>();
                m_SelectedEntity = pointLight;
            }
            if (ImGui::MenuItem("Spot Light")) {
                auto spotLight = m_Scene->CreateEntity("Spot Light");
                spotLight.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 4.0f, 0.0f);
                spotLight.AddComponent<SpotLightComponent>();
                m_SelectedEntity = spotLight;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("UI")) {
            if (ImGui::MenuItem("Canvas")) {
                Entity canvasEntity = UIPalettePanel::GetOrCreateCanvasEntity(m_Scene);
                m_SelectedEntity = canvasEntity;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Panel")) {
                Entity canvas = UIPalettePanel::GetOrCreateCanvasEntity(m_Scene);
                if (canvas && canvas.HasComponent<UICanvasComponent>()) {
                    UIPalettePanel::SpawnWidget(canvas.GetComponent<UICanvasComponent>().Document.get(), UIWidgetType::Panel);
                    m_SelectedEntity = canvas;
                }
            }
            if (ImGui::MenuItem("Text")) {
                Entity canvas = UIPalettePanel::GetOrCreateCanvasEntity(m_Scene);
                if (canvas && canvas.HasComponent<UICanvasComponent>()) {
                    UIPalettePanel::SpawnWidget(canvas.GetComponent<UICanvasComponent>().Document.get(), UIWidgetType::Text);
                    m_SelectedEntity = canvas;
                }
            }
            if (ImGui::MenuItem("Image")) {
                Entity canvas = UIPalettePanel::GetOrCreateCanvasEntity(m_Scene);
                if (canvas && canvas.HasComponent<UICanvasComponent>()) {
                    UIPalettePanel::SpawnWidget(canvas.GetComponent<UICanvasComponent>().Document.get(), UIWidgetType::Image);
                    m_SelectedEntity = canvas;
                }
            }
            if (ImGui::MenuItem("Button")) {
                Entity canvas = UIPalettePanel::GetOrCreateCanvasEntity(m_Scene);
                if (canvas && canvas.HasComponent<UICanvasComponent>()) {
                    UIPalettePanel::SpawnWidget(canvas.GetComponent<UICanvasComponent>().Document.get(), UIWidgetType::Button);
                    m_SelectedEntity = canvas;
                }
            }
            if (ImGui::MenuItem("ProgressBar")) {
                Entity canvas = UIPalettePanel::GetOrCreateCanvasEntity(m_Scene);
                if (canvas && canvas.HasComponent<UICanvasComponent>()) {
                    UIPalettePanel::SpawnWidget(canvas.GetComponent<UICanvasComponent>().Document.get(), UIWidgetType::ProgressBar);
                    m_SelectedEntity = canvas;
                }
            }
            if (ImGui::MenuItem("RadialGauge")) {
                Entity canvas = UIPalettePanel::GetOrCreateCanvasEntity(m_Scene);
                if (canvas && canvas.HasComponent<UICanvasComponent>()) {
                    UIPalettePanel::SpawnWidget(canvas.GetComponent<UICanvasComponent>().Document.get(), UIWidgetType::RadialGauge);
                    m_SelectedEntity = canvas;
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Camera")) {
            auto camEntity = m_Scene->CreateEntity("Camera");
            camEntity.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 2.0f, 5.0f);
            camEntity.AddComponent<CameraComponent>(45.0f, 0.1f, 1000.0f, false);
            m_SelectedEntity = camEntity;
        }

        if (ImGui::BeginMenu("Effects")) {
            if (ImGui::MenuItem("Particle Emitter")) {
                auto vfxEntity = m_Scene->CreateEntity("Particle Emitter");
                vfxEntity.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 1.0f, 0.0f);
                vfxEntity.AddComponent<ParticleEmitterComponent>();
                m_SelectedEntity = vfxEntity;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Environment")) {
            if (ImGui::MenuItem("Atmospheric Skybox")) {
                auto skyEntity = m_Scene->CreateEntity("Atmospheric Skybox");
                skyEntity.AddComponent<SkyboxComponent>();
                m_SelectedEntity = skyEntity;
            }
            ImGui::EndMenu();
        }
    }


    void SceneHierarchyPanel::OnImGuiRender(bool* p_hierarchyOpen, bool* p_propertiesOpen) {
        if (!m_Scene) return;

        if (m_SelectedEntity && !m_Scene->GetRegistry().valid((entt::entity)m_SelectedEntity)) {
            m_SelectedEntity = {};
        }

        // 1. Scene Hierarchy Window
        if (!p_hierarchyOpen || *p_hierarchyOpen) {
            ImGui::SetNextWindowSize(ImVec2(280.0f, 400.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Hierarchy", nullptr)) {
                // Header Bar: [+ ▾] Unity Style Create Button & Search Filter
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                if (ImGui::Button("+ ▾", ImVec2(38.0f, 24.0f))) {
                    ImGui::OpenPopup("CreateEntityPopup");
                }
                ImGui::PopStyleVar();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Create New GameObject / Primitive / Light / UI Component");
                }

                if (ImGui::BeginPopup("CreateEntityPopup")) {
                    DrawCreateMenu();
                    ImGui::EndPopup();
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::InputTextWithHint("##SearchFilter", "Search...", m_SearchFilter, sizeof(m_SearchFilter));

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                auto view = m_Scene->GetRegistry().view<TagComponent>();
                for (auto entityID : view) {
                    Entity entity{ entityID, m_Scene };

                    // Filter search: display all matching entities directly
                    if (m_SearchFilter[0] != '\0') {
                        const auto& tag = entity.GetComponent<TagComponent>().Tag;
                        if (!ContainsIgnoreCase(tag, m_SearchFilter)) {
                            continue;
                        }
                        DrawEntityNode(entity);
                        continue;
                    }

                    // Strict hierarchical tree view: render only root entities (no parent) at top level
                    if (entity.HasComponent<HierarchyComponent>()) {
                        auto& hc = entity.GetComponent<HierarchyComponent>();
                        if (hc.Parent != entt::null && m_Scene->GetRegistry().valid(hc.Parent)) {
                            continue;
                        }
                    }
                    DrawEntityNode(entity);
                }

                // Right click blank space context menu
                if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                    DrawCreateMenu();
                    ImGui::EndPopup();
                }

                // Drag & Drop to instantiate 3D model or unparent to Root in Hierarchy
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
                        uint32_t draggedID = *(const uint32_t*)payload->Data;
                        Entity draggedEntity{ (entt::entity)draggedID, m_Scene };
                        if (draggedEntity) {
                            draggedEntity.Unparent();
                            m_SelectedEntity = draggedEntity;
                            AXLE_INFO("Unparented Entity '{0}' to Root via Tree Drag & Drop", draggedEntity.GetName());
                        }
                    }

                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UIPalettePanel::DRAG_DROP_PAYLOAD_ID)) {
                        const char* widgetTypeName = static_cast<const char*>(payload->Data);
                        auto canvasEntity = UIPalettePanel::GetOrCreateCanvasEntity(m_Scene);
                        if (canvasEntity && canvasEntity.HasComponent<UICanvasComponent>()) {
                            auto& uic = canvasEntity.GetComponent<UICanvasComponent>();
                            if (!uic.Document) {
                                uic.Document = std::make_shared<UIDocument>();
                                uic.Document->CreateNode<UICanvasNode>(nullptr);
                            }
                            UIPalettePanel::SpawnWidgetByName(uic.Document.get(), widgetTypeName);
                            m_SelectedEntity = canvasEntity;
                            AXLE_INFO("Spawned UI Widget '{0}' under Root Canvas via Hierarchy Drag & Drop", widgetTypeName);
                        }
                    }

                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = static_cast<const char*>(payload->Data);
                        std::filesystem::path p(pathStr);
                        std::string ext = p.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                        if (ext == ".axlemesh" || ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                            if (ext == ".axle") {
                                SceneSerializerContext ctx;
                                ctx.CubeMesh = m_CubeMesh;
                                ctx.SphereMesh = m_SphereMesh;
                                ctx.CylinderMesh = m_CylinderMesh;
                                ctx.ConeMesh = m_ConeMesh;
                                ctx.PlaneMesh = m_PlaneMesh;
                                auto spawned = m_Scene->InstantiateAssembly(p, m_PhysicsScene, glm::vec3(0.0f, 0.0f, 0.0f), &ctx);
                                if (spawned) {
                                    m_SelectedEntity = spawned;
                                }
                            } else {
                                if (ext != ".axlemesh" && ext != ".axle") {
                                    std::filesystem::path axlePath = p.parent_path() / (p.stem().string() + ".axlemesh");
                                    if (!std::filesystem::exists(axlePath)) {
                                        AssetSerializer::AutoConvertModel(p, axlePath, true);
                                    }
                                    p = axlePath;
                                }

                            auto pkg = AssetSerializer::LoadPackage(p);
                            if (pkg.Success) {
                                glm::vec3 spawnPos(0.0f, 0.0f, 0.0f);
                                if (pkg.IsPackage) {
                                    std::string rootTag = pkg.Name.empty() ? p.stem().string() : pkg.Name;
                                    auto rootEntity = m_Scene->CreateEntity(rootTag);
                                    rootEntity.AddComponent<TransformComponent>(spawnPos, glm::vec3(1.0f));
                                    auto& rootHierarchy = rootEntity.AddComponent<HierarchyComponent>();

                                    for (const auto& part : pkg.Parts) {
                                        auto childEntity = m_Scene->CreateEntity(part.Name);
                                        auto& ct = childEntity.AddComponent<TransformComponent>(part.LocalTranslation, part.LocalScale);
                                        ct.Rotation = part.LocalRotation;

                                        auto partFullPath = p.parent_path() / part.RelativePath;
                                        childEntity.AddComponent<MeshInstance3D>(
                                            part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_CubeMesh,
                                            partFullPath.string(),
                                            part.LoadedMesh.SourceData.PatternMode,
                                            part.LoadedMesh.SourceData.TileScale,
                                            part.LoadedMesh.SourceData.ColorPrimary,
                                            part.LoadedMesh.SourceData.ColorSecondary
                                        );

                                        childEntity.AddComponent<HierarchyComponent>((entt::entity)rootEntity);
                                        rootHierarchy.Children.push_back((entt::entity)childEntity);
                                    }
                                    m_SelectedEntity = rootEntity;
                                    AXLE_INFO("Instantiated Model Package '{0}' ({1} submeshes) via Scene Hierarchy drop", 
                                        rootTag, pkg.Parts.size());
                                } else if (!pkg.Parts.empty()) {
                                    const auto& part = pkg.Parts[0];
                                    std::string entityTag = pkg.Name.empty() ? p.stem().string() : pkg.Name;
                                    auto newEnt = m_Scene->CreateEntity(entityTag);
                                    newEnt.AddComponent<TransformComponent>(spawnPos, glm::vec3(1.0f));
                                    newEnt.AddComponent<MeshInstance3D>(
                                        part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_CubeMesh,
                                        p.string(),
                                        part.LoadedMesh.SourceData.PatternMode,
                                        part.LoadedMesh.SourceData.TileScale,
                                        part.LoadedMesh.SourceData.ColorPrimary,
                                        part.LoadedMesh.SourceData.ColorSecondary
                                    );
                                    m_SelectedEntity = newEnt;
                                    AXLE_INFO("Instantiated 3D model '{0}' via Scene Hierarchy drop", p.filename().string());
                                }
                            }
                        }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                // Blank space at bottom of Hierarchy: accept drops to Root & click to deselect
                ImVec2 avail = ImGui::GetContentRegionAvail();
                if (avail.y > 10.0f) {
                    ImGui::Dummy(avail);
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                        SetSelectedEntity({});
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
                            uint32_t draggedID = *(const uint32_t*)payload->Data;
                            Entity draggedEntity{ (entt::entity)draggedID, m_Scene };
                            if (draggedEntity) {
                                draggedEntity.Unparent();
                                m_SelectedEntity = draggedEntity;
                                AXLE_INFO("Unparented Entity '{0}' to Root via Tree Drag & Drop", draggedEntity.GetName());
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                } else {
                    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
                        SetSelectedEntity({});
                    }
                }
            }
            ImGui::End();
        }

        // 2. Properties / Inspector Window
        if (!p_propertiesOpen || *p_propertiesOpen) {
            ImGui::SetNextWindowSize(ImVec2(340.0f, 500.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Inspector", nullptr)) {
                bool hasEntity = (m_SelectedEntity && m_Scene && m_Scene->GetRegistry().valid((entt::entity)m_SelectedEntity));
                bool hasAsset = (!m_SelectedAsset.empty() && std::filesystem::exists(m_SelectedAsset));

                // Dual-Context Mode Switcher Tab Bar
                if (hasEntity && hasAsset) {
                    float tabWidth = (ImGui::GetContentRegionAvail().x - 4.0f) * 0.5f;
                    bool isEntityMode = (m_InspectorMode == InspectorMode::Entity);
                    bool isAssetMode = (m_InspectorMode == InspectorMode::Asset);

                    if (isEntityMode) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
                    if (ImGui::Button("Scene Entity", ImVec2(tabWidth, 24.0f))) {
                        m_InspectorMode = InspectorMode::Entity;
                    }
                    if (isEntityMode) ImGui::PopStyleColor();

                    ImGui::SameLine();

                    if (isAssetMode) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
                    if (ImGui::Button("Asset File", ImVec2(tabWidth, 24.0f))) {
                        m_InspectorMode = InspectorMode::Asset;
                    }
                    if (isAssetMode) ImGui::PopStyleColor();

                    ImGui::Separator();
                    ImGui::Spacing();
                }

                if (m_InspectorMode == InspectorMode::Entity && hasEntity) {
                    DrawComponents(m_SelectedEntity);
                } else if (m_InspectorMode == InspectorMode::Asset && hasAsset) {
                    DrawAssetProperties(m_SelectedAsset);
                } else if (hasEntity) {
                    DrawComponents(m_SelectedEntity);
                } else if (hasAsset) {
                    DrawAssetProperties(m_SelectedAsset);
                } else {
                    ImGui::Spacing();
                    ImGui::TextDisabled("No entity selected.");
                    ImGui::TextWrapped("Select an entity in Hierarchy to inspect its components, or drag a 3D model (.obj) from Project to instantiate.");
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    float availW = ImGui::GetContentRegionAvail().x;
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.5f);
                    if (ImGui::Button("+ Create Entity", ImVec2(availW, 26.0f))) {
                        ImGui::OpenPopup("PropsCreateEntityPopup");
                    }
                    ImGui::PopStyleVar();

                    if (ImGui::BeginPopup("PropsCreateEntityPopup")) {
                        DrawCreateMenu();
                        ImGui::EndPopup();
                    }

                    // Drag & Drop to instantiate when no entity selected
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            const char* pathStr = static_cast<const char*>(payload->Data);
                            std::filesystem::path p(pathStr);
                            std::string ext = p.extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                            if (ext == ".axlemesh" || ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                                if (ext == ".axle") {
                                    SceneSerializerContext ctx;
                                    ctx.CubeMesh = m_CubeMesh;
                                    ctx.SphereMesh = m_SphereMesh;
                                    ctx.CylinderMesh = m_CylinderMesh;
                                    ctx.ConeMesh = m_ConeMesh;
                                    ctx.PlaneMesh = m_PlaneMesh;
                                    auto spawned = m_Scene->InstantiateAssembly(p, m_PhysicsScene, glm::vec3(0.0f, 0.0f, 0.0f), &ctx);
                                    if (spawned) {
                                        m_SelectedEntity = spawned;
                                    }
                                } else {
                                    if (ext != ".axlemesh" && ext != ".axle") {
                                        std::filesystem::path axlePath = p.parent_path() / (p.stem().string() + ".axlemesh");
                                        if (!std::filesystem::exists(axlePath)) {
                                            AssetSerializer::AutoConvertModel(p, axlePath, true);
                                        }
                                        p = axlePath;
                                    }

                                auto pkg = AssetSerializer::LoadPackage(p);
                                if (pkg.Success) {
                                    glm::vec3 spawnPos(0.0f, 0.0f, 0.0f);
                                    if (pkg.IsPackage) {
                                        std::string rootTag = pkg.Name.empty() ? p.stem().string() : pkg.Name;
                                        auto rootEntity = m_Scene->CreateEntity(rootTag);
                                        rootEntity.AddComponent<TransformComponent>(spawnPos, glm::vec3(1.0f));
                                        auto& rootHierarchy = rootEntity.AddComponent<HierarchyComponent>();

                                        for (const auto& part : pkg.Parts) {
                                            auto childEntity = m_Scene->CreateEntity(part.Name);
                                            auto& ct = childEntity.AddComponent<TransformComponent>(part.LocalTranslation, part.LocalScale);
                                            ct.Rotation = part.LocalRotation;

                                            auto partFullPath = p.parent_path() / part.RelativePath;
                                            childEntity.AddComponent<MeshInstance3D>(
                                                part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_CubeMesh,
                                                partFullPath.string(),
                                                part.LoadedMesh.SourceData.PatternMode,
                                                part.LoadedMesh.SourceData.TileScale,
                                                part.LoadedMesh.SourceData.ColorPrimary,
                                                part.LoadedMesh.SourceData.ColorSecondary
                                            );

                                            childEntity.AddComponent<HierarchyComponent>((entt::entity)rootEntity);
                                            rootHierarchy.Children.push_back((entt::entity)childEntity);
                                        }
                                        m_SelectedEntity = rootEntity;
                                        AXLE_INFO("Instantiated Model Package '{0}' ({1} submeshes) via Properties drop", 
                                            rootTag, pkg.Parts.size());
                                    } else if (!pkg.Parts.empty()) {
                                        const auto& part = pkg.Parts[0];
                                        std::string entityTag = pkg.Name.empty() ? p.stem().string() : pkg.Name;
                                        auto newEnt = m_Scene->CreateEntity(entityTag);
                                        newEnt.AddComponent<TransformComponent>(spawnPos, glm::vec3(1.0f));
                                        newEnt.AddComponent<MeshInstance3D>(
                                            part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_CubeMesh,
                                            p.string(),
                                            part.LoadedMesh.SourceData.PatternMode,
                                            part.LoadedMesh.SourceData.TileScale,
                                            part.LoadedMesh.SourceData.ColorPrimary,
                                            part.LoadedMesh.SourceData.ColorSecondary
                                        );
                                        m_SelectedEntity = newEnt;
                                        AXLE_INFO("Instantiated 3D model '{0}' via Properties drop", p.filename().string());
                                    }
                                }
                            }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
            }
            ImGui::End();
        }

        // Global Scene Hierarchy / Inspector Shortcuts (when not typing in an InputText)
        if (!ImGui::GetIO().WantTextInput) {
            // Ctrl+D: Deep recursive duplication of selected entity
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
                if (m_SelectedEntity && m_Scene && m_Scene->GetRegistry().valid((entt::entity)m_SelectedEntity)) {
                    Entity duplicated = m_Scene->DuplicateEntity(m_SelectedEntity, m_PhysicsScene);
                    if (duplicated) {
                        m_SelectedEntity = duplicated;
                        AXLE_INFO("Duplicated Entity '{0}' via Ctrl+D", duplicated.GetName());
                    }
                }
            }

            // Delete key: Deferred deletion of selected entity
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                if (m_SelectedEntity && m_Scene && m_Scene->GetRegistry().valid((entt::entity)m_SelectedEntity)) {
                    m_EntitiesToDestroy.push_back(m_SelectedEntity);
                }
            }
        }

        // Process end-of-frame deferred deletion queue
        for (auto entityToDestroy : m_EntitiesToDestroy) {
            if (m_Scene && entityToDestroy && m_Scene->GetRegistry().valid((entt::entity)entityToDestroy)) {
                if (m_SelectedEntity == entityToDestroy || (m_SelectedEntity && m_SelectedEntity.IsChildOf(entityToDestroy))) {
                    m_SelectedEntity = {};
                }
                m_Scene->DestroyEntity(entityToDestroy, m_PhysicsScene);
            }
        }
        m_EntitiesToDestroy.clear();
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
        std::string tag = entity.HasComponent<TagComponent>() ? entity.GetComponent<TagComponent>().Tag : "Entity";

        bool hasChildren = false;
        if (entity.HasComponent<HierarchyComponent>()) {
            hasChildren = !entity.GetComponent<HierarchyComponent>().Children.empty();
        }

        ImGuiTreeNodeFlags flags = ((m_SelectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0) 
                                 | ImGuiTreeNodeFlags_OpenOnArrow 
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;

        if (!hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        } else {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }

        // Unity GameObject Tree Node: leave space for crisp vector icon
        ImVec2 rowStart = ImGui::GetCursorScreenPos();
        bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "    %s", tag.c_str());

        // Draw crisp vector icon in the reserved pocket
        float iconSize = 13.0f;
        float textOffsetX = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImVec2 iconPos(rowStart.x + textOffsetX, rowStart.y + (ImGui::GetTextLineHeight() - iconSize) * 0.5f);
        UIIcons::DrawEntityVectorIcon(ImGui::GetWindowDrawList(), iconPos, iconSize, entity);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_SelectedEntity = entity;
            m_SelectedAsset.clear();
        }

        // Drag Source: Allow dragging this entity to reparent
        if (ImGui::BeginDragDropSource()) {
            uint32_t entityID = (uint32_t)entity;
            ImGui::SetDragDropPayload("SCENE_HIERARCHY_ENTITY", &entityID, sizeof(uint32_t));
            ImGui::Text("Reparent '%s'", tag.c_str());
            ImGui::EndDragDropSource();
        }

        // Drag & Drop payload onto Entity node
        if (ImGui::BeginDragDropTarget()) {
            // 1. Reparent dragged entity under this entity
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
                uint32_t draggedID = *(const uint32_t*)payload->Data;
                Entity draggedEntity{ (entt::entity)draggedID, m_Scene };
                if (draggedEntity && draggedEntity != entity && !entity.IsChildOf(draggedEntity)) {
                    draggedEntity.SetParent(entity);
                    m_SelectedEntity = draggedEntity;
                    AXLE_INFO("Reparented Entity '{0}' under '{1}' via Tree Drag & Drop", draggedEntity.GetName(), entity.GetName());
                }
            }

            // 2. Attach Script or Apply Material from Content Browser
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* pathStr = static_cast<const char*>(payload->Data);
                std::filesystem::path p(pathStr);
                std::string ext = p.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                if (ext == ".hpp" || ext == ".h" || ext == ".cpp") {
                    std::string scriptName = p.stem().string();
                    if (!entity.HasComponent<NativeScriptComponent>()) {
                        entity.AddComponent<NativeScriptComponent>(scriptName);
                    } else {
                        entity.GetComponent<NativeScriptComponent>().Bind(scriptName);
                    }
                    m_SelectedEntity = entity;
                    AXLE_INFO("Attached Native Script '{0}' to Entity '{1}' via Tree Drag & Drop", scriptName, tag);
                } else if (ext == ".amat" || ext == ".axlemat" || ext == ".mat") {
                    MaterialComponent matComp;
                    if (LoadMaterialAsset(p, matComp)) {
                        if (entity.HasComponent<MaterialComponent>()) {
                            entity.GetComponent<MaterialComponent>() = matComp;
                        } else {
                            entity.AddComponent<MaterialComponent>(matComp);
                        }
                        m_SelectedEntity = entity;
                        AXLE_INFO("Applied Material '{0}' to Entity '{1}' via Tree Drag & Drop", p.stem().string(), tag);
                    }
                }
            }

            // 3. Drop Stock UI Widget onto Entity (Phase 34 Step 7.4)
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UIPalettePanel::DRAG_DROP_PAYLOAD_ID)) {
                const char* widgetTypeName = static_cast<const char*>(payload->Data);
                if (!entity.HasComponent<UICanvasComponent>()) {
                    auto& uic = entity.AddComponent<UICanvasComponent>();
                    uic.Document = std::make_shared<UIDocument>();
                    uic.Document->CreateNode<UICanvasNode>(nullptr);
                }
                auto& uic = entity.GetComponent<UICanvasComponent>();
                if (!uic.Document) {
                    uic.Document = std::make_shared<UIDocument>();
                    uic.Document->CreateNode<UICanvasNode>(nullptr);
                }
                UIPalettePanel::SpawnWidgetByName(uic.Document.get(), widgetTypeName);
                m_SelectedEntity = entity;
                AXLE_INFO("Spawned UI Widget '{0}' under Entity '{1}' via Tree Drag & Drop", widgetTypeName, tag);
            }
            ImGui::EndDragDropTarget();
        }

        bool entityDeleted = false;
        if (ImGui::BeginPopupContextItem()) {
            m_SelectedEntity = entity;
            m_SelectedAsset.clear();

            if (ImGui::MenuItem("Create Empty Child")) {
                auto child = m_Scene->CreateEntity("Child");
                child.SetParent(entity);
                m_SelectedEntity = child;
            }

            if (ImGui::BeginMenu("3D Object")) {
                if (ImGui::MenuItem("Cube")) {
                    auto prim = CreatePrimitive(0);
                    prim.SetParent(entity);
                    m_SelectedEntity = prim;
                }
                if (ImGui::MenuItem("Sphere")) {
                    auto prim = CreatePrimitive(1);
                    prim.SetParent(entity);
                    m_SelectedEntity = prim;
                }
                if (ImGui::MenuItem("Cylinder")) {
                    auto prim = CreatePrimitive(2);
                    prim.SetParent(entity);
                    m_SelectedEntity = prim;
                }
                if (ImGui::MenuItem("Cone")) {
                    auto prim = CreatePrimitive(3);
                    prim.SetParent(entity);
                    m_SelectedEntity = prim;
                }
                if (ImGui::MenuItem("Plane")) {
                    auto prim = CreatePrimitive(4);
                    prim.SetParent(entity);
                    m_SelectedEntity = prim;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Physics Templates")) {
                if (ImGui::MenuItem("Dynamic Box (Falling)")) {
                    auto prim = CreatePrimitive(0, glm::vec3(0.0f, 2.0f, 0.0f), true);
                    prim.SetParent(entity);
                    m_SelectedEntity = prim;
                }
                if (ImGui::MenuItem("Dynamic Sphere (Falling)")) {
                    auto prim = CreatePrimitive(1, glm::vec3(0.0f, 2.0f, 0.0f), true);
                    prim.SetParent(entity);
                    m_SelectedEntity = prim;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Light")) {
                if (ImGui::MenuItem("Point Light")) {
                    auto light = m_Scene->CreateEntity("Point Light");
                    light.SetParent(entity);
                    light.AddComponent<PointLightComponent>();
                    m_SelectedEntity = light;
                }
                if (ImGui::MenuItem("Spot Light")) {
                    auto light = m_Scene->CreateEntity("Spot Light");
                    light.SetParent(entity);
                    light.AddComponent<SpotLightComponent>();
                    m_SelectedEntity = light;
                }
                ImGui::EndMenu();
            }

            if (entity.HasComponent<HierarchyComponent>()) {
                auto& hc = entity.GetComponent<HierarchyComponent>();
                if (hc.Parent != entt::null && m_Scene->GetRegistry().valid(hc.Parent)) {
                    if (ImGui::MenuItem("Detach from Parent")) {
                        entity.Unparent();
                    }
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Duplicate Entity", "Ctrl+D")) {
                Entity duplicated = m_Scene->DuplicateEntity(entity, m_PhysicsScene);
                if (duplicated) {
                    m_SelectedEntity = duplicated;
                    AXLE_INFO("Duplicated Entity '{0}' via Context Menu", duplicated.GetName());
                }
            }

            if (ImGui::MenuItem("Save as Assembly...")) {
                std::string entityTag = entity.HasComponent<TagComponent>() ? entity.GetComponent<TagComponent>().Tag : "Assembly";
                std::filesystem::path outPath = FileSystem::GetAssetDirectory() / (entityTag + ".axle");
                int counter = 1;
                while (std::filesystem::exists(outPath)) {
                    outPath = FileSystem::GetAssetDirectory() / (entityTag + "_" + std::to_string(counter++) + ".axle");
                }
                AssemblySerializer::Serialize(outPath, entity, m_PhysicsScene);
            }
            if (entity.HasComponent<AssemblyComponent>()) {
                if (ImGui::MenuItem("Unpack Assembly")) {
                    entity.RemoveComponent<AssemblyComponent>();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Entity", "Del")) {
                entityDeleted = true;
            }
            ImGui::EndPopup();
        }

        if (opened) {
            if (hasChildren) {
                // Copy children list to avoid iterator invalidation upon modification
                auto children = entity.GetComponent<HierarchyComponent>().Children;
                for (auto childID : children) {
                    if (m_Scene->GetRegistry().valid(childID)) {
                        Entity childEntity{ childID, m_Scene };
                        DrawEntityNode(childEntity);
                    }
                }
            }
            ImGui::TreePop();
        }

        if (entityDeleted) {
            m_EntitiesToDestroy.push_back(entity);
        }
    }


}
