#include <Axle/SceneHierarchyPanel.hpp>
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
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <Jolt/Physics/Body/BodyLock.h>
#include <cstring>
#include <filesystem>

#include <Axle/VertexArray.hpp>
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

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text("%s", label.c_str());
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Reset All Axes")) {
                values = glm::vec3(resetValue);
            }
            ImGui::EndPopup();
        }
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

        float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

        // X Control - Red
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.80f, 0.15f, 0.15f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.95f, 0.25f, 0.25f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.70f, 0.10f, 0.10f, 1.0f });
        if (ImGui::Button("X", buttonSize)) {
            values.x = resetValue;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click to reset X to %.2f", resetValue);
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##X", &values.x, speed, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Y Control - Green
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.15f, 0.70f, 0.20f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.25f, 0.85f, 0.30f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.10f, 0.60f, 0.15f, 1.0f });
        if (ImGui::Button("Y", buttonSize)) {
            values.y = resetValue;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click to reset Y to %.2f", resetValue);
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Y", &values.y, speed, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Z Control - Blue
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.15f, 0.35f, 0.85f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.25f, 0.45f, 0.95f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.10f, 0.25f, 0.75f, 1.0f });
        if (ImGui::Button("Z", buttonSize)) {
            values.z = resetValue;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click to reset Z to %.2f", resetValue);
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Z", &values.z, speed, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }

    template<typename T, typename UIFunction>
    static void DrawComponent(
        const std::string& name,
        Entity entity,
        UIFunction uiFunction,
        bool canRemove = true,
        std::function<void(T&)> onRemove = nullptr
    ) {
        if (!entity.HasComponent<T>()) {
            return;
        }

        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen 
                                               | ImGuiTreeNodeFlags_Framed 
                                               | ImGuiTreeNodeFlags_SpanAvailWidth 
                                               | ImGuiTreeNodeFlags_AllowOverlap 
                                               | ImGuiTreeNodeFlags_FramePadding;

        auto& component = entity.GetComponent<T>();
        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4.0f, 4.0f });
        float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        ImGui::Separator();
        bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, "%s", name.c_str());
        ImGui::PopStyleVar();

        bool removeComponent = false;

        // Context menu on right click of the header
        if (ImGui::BeginPopupContextItem()) {
            if (canRemove) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeComponent = true;
                }
            } else {
                ImGui::TextDisabled("Cannot remove required component");
            }
            ImGui::EndPopup();
        }

        // Right-aligned settings button on header
        ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
        std::string btnId = "+##" + name;
        if (ImGui::Button(btnId.c_str(), ImVec2{ lineHeight, lineHeight })) {
            ImGui::OpenPopup("ComponentSettings");
        }

        if (ImGui::BeginPopup("ComponentSettings")) {
            if (canRemove) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeComponent = true;
                }
            } else {
                ImGui::TextDisabled("Cannot remove required component");
            }
            ImGui::EndPopup();
        }

        if (open) {
            uiFunction(component);
            ImGui::TreePop();
        }

        if (removeComponent) {
            if (onRemove) {
                onRemove(component);
            }
            entity.RemoveComponent<T>();
        }
        ImGui::Spacing();
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
                if (m_PlaneMesh) {
                    entity.AddComponent<MeshInstance3D>(
                        m_PlaneMesh, 0, 1.0f,
                        glm::vec4(0.35f, 0.38f, 0.42f, 1.0f),
                        glm::vec4(0.20f, 0.22f, 0.25f, 1.0f)
                    );
                }
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

        if (ImGui::MenuItem("Camera")) {
            auto camEntity = m_Scene->CreateEntity("Camera");
            camEntity.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 2.0f, 5.0f);
            camEntity.AddComponent<CameraComponent>(45.0f, 0.1f, 1000.0f, false);
            m_SelectedEntity = camEntity;
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
            if (ImGui::Begin("Scene Hierarchy", p_hierarchyOpen)) {
                // Header Bar: [+ Create] Button & Search Filter
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.30f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.68f, 0.38f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.45f, 0.22f, 1.0f));
                if (ImGui::Button("+ Create", ImVec2(75.0f, 24.0f))) {
                    ImGui::OpenPopup("CreateEntityPopup");
                }
                ImGui::PopStyleColor(3);

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
            if (ImGui::Begin("Properties", p_propertiesOpen)) {
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
                    ImGui::TextWrapped("Click any entity in Scene Hierarchy to inspect its components, or drag a 3D model (.obj) from Content Browser here to instantiate:");
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    float availW = ImGui::GetContentRegionAvail().x;
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.30f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.68f, 0.38f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.45f, 0.22f, 1.0f));
                    if (ImGui::Button("+ Create Entity", ImVec2(availW, 28.0f))) {
                        ImGui::OpenPopup("PropsCreateEntityPopup");
                    }
                    ImGui::PopStyleColor(3);

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

        bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "%s", tag.c_str());

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

    void SceneHierarchyPanel::DrawComponents(Entity entity) {
        if (!entity || !m_Scene || !m_Scene->GetRegistry().valid((entt::entity)entity)) {
            return;
        }

        // Tag Component
        if (entity.HasComponent<TagComponent>()) {
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            char buffer[256];
            std::memset(buffer, 0, sizeof(buffer));
            std::strncpy(buffer, tag.c_str(), sizeof(buffer) - 1);

            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##Tag", buffer, sizeof(buffer))) {
                tag = std::string(buffer);
            }
            ImGui::PopItemWidth();

            if (entity.HasComponent<IDComponent>()) {
                uint64_t uuid = (uint64_t)entity.GetComponent<IDComponent>().ID;
                ImGui::TextDisabled("UUID: %llu", uuid);
            }

            if (entity.HasComponent<HierarchyComponent>()) {
                auto& hc = entity.GetComponent<HierarchyComponent>();
                if (hc.Parent != entt::null && m_Scene->GetRegistry().valid(hc.Parent)) {
                    Entity parentEntity{ hc.Parent, m_Scene };
                    std::string parentTag = parentEntity.HasComponent<TagComponent>() ? parentEntity.GetComponent<TagComponent>().Tag : "Parent";
                    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Parent: %s", parentTag.c_str());
                    ImGui::SameLine();
                    ImGui::Checkbox("Inherit Transform", &hc.InheritTransform);
                    ImGui::SameLine();
                    ImGui::Checkbox("Inherit Scale", &hc.InheritScale);
                }
                if (!hc.Children.empty()) {
                    ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.55f, 1.0f), "Children: %zu attached", hc.Children.size());
                }
            }
            ImGui::Spacing();
        }

        // Assembly Component Indicator Banner
        if (entity.HasComponent<AssemblyComponent>()) {
            auto& ac = entity.GetComponent<AssemblyComponent>();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
            ImGui::Text("[Assembly Prefab]");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", std::filesystem::path(ac.AssetPath).filename().string().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Unpack")) {
                entity.RemoveComponent<AssemblyComponent>();
            }
            ImGui::Separator();
        }

        // Transform Component (Required - cannot be removed)
        DrawComponent<TransformComponent>("Transform", entity, [&](TransformComponent& tc) {
            glm::vec3 translation = tc.Translation;
            DrawVec3Control("Translation", translation);

            glm::vec3 rotationDegrees = glm::degrees(glm::eulerAngles(tc.Rotation));
            DrawVec3Control("Rotation", rotationDegrees);

            glm::vec3 scale = tc.Scale;
            DrawVec3Control("Scale", scale, 1.0f);

            // Check if user changed values
            bool changed = false;
            if (translation != tc.Translation) {
                tc.Translation = translation;
                changed = true;
            }
            if (rotationDegrees != glm::degrees(glm::eulerAngles(tc.Rotation))) {
                tc.Rotation = glm::quat(glm::radians(rotationDegrees));
                changed = true;
            }
            if (scale != tc.Scale) {
                tc.Scale = scale;
            }

            // If rigid body attached and user modified transform, sync to physics engine (using world transform)
            if (changed && m_PhysicsScene) {
                auto syncPhysicsBody = [&](Entity e) {
                    if (e.HasComponent<RigidBody3D>()) {
                        auto& rb = e.GetComponent<RigidBody3D>();
                        if (!rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                            glm::mat4 worldMat = e.GetWorldTransform();
                            glm::vec3 worldPos = glm::vec3(worldMat[3]);
                            glm::quat worldRot = glm::quat_cast(worldMat);
                            m_PhysicsScene->SetBodyPositionAndRotation(rb.RuntimeBodyID, worldPos, worldRot, true);
                        }
                    }
                };
                syncPhysicsBody(entity);
                entity.ForEachChild(syncPhysicsBody, true);
            }
        }, /*canRemove=*/ false);

        // MeshInstance3D Component
        DrawComponent<MeshInstance3D>("MeshInstance3D", entity, [&](MeshInstance3D& mc) {
            // 3D Model Asset Assignment via Drag & Drop
            ImGui::Text("Model Asset:");
            ImGui::SameLine();
            std::string assetDisplayName = mc.AssetPath.empty() 
                ? "[ Built-in Primitive (Drop .obj here) ]" 
                : std::filesystem::path(mc.AssetPath).filename().string();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.24f, 0.32f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.48f, 1.0f));
            ImGui::Button(assetDisplayName.c_str(), ImVec2(-1.0f, 26.0f));
            ImGui::PopStyleColor(2);

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* pathStr = static_cast<const char*>(payload->Data);
                    std::filesystem::path p(pathStr);
                    std::string ext = p.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                    if (ext == ".axlemesh" || ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                        mc.AssetPath = p.string();
                        if (ext == ".axlemesh" || ext == ".axle") {
                            auto loaded = AssetSerializer::LoadMesh(p);
                            if (loaded.Success) {
                                mc.VAO = loaded.VAO;
                                mc.PatternMode = loaded.SourceData.PatternMode;
                                mc.TileScale = loaded.SourceData.TileScale;
                                mc.Color1 = loaded.SourceData.ColorPrimary;
                                mc.Color2 = loaded.SourceData.ColorSecondary;
                            }
                        } else if (ext == ".obj") {
                            std::filesystem::path axlePath = p.parent_path() / (p.stem().string() + ".axlemesh");
                            if (!std::filesystem::exists(axlePath)) {
                                AssetSerializer::ConvertOBJToAxle(p, axlePath);
                            }
                            auto loaded = AssetSerializer::LoadMesh(axlePath);
                            if (loaded.Success) {
                                mc.VAO = loaded.VAO;
                                mc.PatternMode = loaded.SourceData.PatternMode;
                                mc.TileScale = loaded.SourceData.TileScale;
                                mc.Color1 = loaded.SourceData.ColorPrimary;
                                mc.Color2 = loaded.SourceData.ColorSecondary;
                                mc.AssetPath = axlePath.string();
                            } else {
                                auto loadedVao = PrimitiveMesh::LoadOBJ(p.string());
                                if (loadedVao) mc.VAO = loadedVao;
                            }
                        }
                        AXLE_INFO("Assigned 3D model '{0}' to Entity '{1}'", p.filename().string(), entity.GetComponent<TagComponent>().Tag);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Drag & Drop a 3D model (.obj) from Content Browser here to assign");
            }

            const char* patternNames[] = { "0: World XZ Caro (Floor)", "1: Local 3D Caro (Box/Cyl)", "2: Spherical Caro (Sphere)", "3: Solid Color (PBR Surface)" };
            int currentPattern = mc.PatternMode;
            if (ImGui::Combo("Pattern Mode", &currentPattern, patternNames, IM_ARRAYSIZE(patternNames))) {
                mc.PatternMode = currentPattern;
            }

            ImGui::DragFloat("Tile Scale", &mc.TileScale, 0.2f, 0.1f, 64.0f, "%.1f");

            ImGui::TextDisabled("Quick Palettes:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Titanium Silver")) {
                mc.Color1 = glm::vec4(0.85f, 0.88f, 0.92f, 1.0f);
                mc.Color2 = glm::vec4(0.65f, 0.70f, 0.76f, 1.0f);
                mc.TileScale = 8.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Slate Graphite")) {
                mc.Color1 = glm::vec4(0.35f, 0.38f, 0.42f, 1.0f);
                mc.Color2 = glm::vec4(0.20f, 0.22f, 0.26f, 1.0f);
                mc.TileScale = 8.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Arctic Navy")) {
                mc.Color1 = glm::vec4(0.22f, 0.28f, 0.38f, 1.0f);
                mc.Color2 = glm::vec4(0.14f, 0.18f, 0.25f, 1.0f);
                mc.TileScale = 8.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Ceramic White")) {
                mc.Color1 = glm::vec4(0.92f, 0.92f, 0.94f, 1.0f);
                mc.Color2 = glm::vec4(0.80f, 0.82f, 0.85f, 1.0f);
                mc.TileScale = 8.0f;
            }

            ImGui::ColorEdit4("Color Primary", glm::value_ptr(mc.Color1));
            ImGui::ColorEdit4("Color Secondary", glm::value_ptr(mc.Color2));

            if (entity.HasComponent<HierarchyComponent>() && !entity.GetComponent<HierarchyComponent>().Children.empty()) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.65f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.52f, 0.78f, 1.0f));
                if (ImGui::Button("Apply Material & Scale to All Child Parts", ImVec2(-1.0f, 26.0f))) {
                    auto children = entity.GetComponent<HierarchyComponent>().Children;
                    for (auto childId : children) {
                        if (m_Scene->GetRegistry().valid(childId) && m_Scene->GetRegistry().all_of<MeshInstance3D>(childId)) {
                            auto& childMc = m_Scene->GetRegistry().get<MeshInstance3D>(childId);
                            childMc.Color1 = mc.Color1;
                            childMc.Color2 = mc.Color2;
                            childMc.TileScale = mc.TileScale;
                            childMc.PatternMode = mc.PatternMode;
                        }
                    }
                }
                ImGui::PopStyleColor(2);
            }
        }, /*canRemove=*/ true);

        if (!entity.HasComponent<MeshInstance3D>()) {
            // Drop target when entity has no MeshInstance3D
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.18f, 0.24f, 0.65f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.30f, 0.42f, 0.90f));
            ImGui::Button("+ Drop 3D model (.obj) here to add Mesh", ImVec2(-1.0f, 28.0f));
            ImGui::PopStyleColor(2);

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* pathStr = static_cast<const char*>(payload->Data);
                    std::filesystem::path p(pathStr);
                    std::string ext = p.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                    if (ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                        int patternMode = 1;
                        float tileScale = 8.0f;
                        glm::vec4 color1(0.85f, 0.88f, 0.92f, 1.0f);
                        glm::vec4 color2(0.65f, 0.70f, 0.76f, 1.0f);
                        std::shared_ptr<VertexArray> loadedVao;

                        if (ext == ".axlemesh" || ext == ".axle") {
                            auto loaded = AssetSerializer::LoadMesh(p);
                            if (loaded.Success) {
                                loadedVao = loaded.VAO;
                                patternMode = loaded.SourceData.PatternMode;
                                tileScale = loaded.SourceData.TileScale;
                                color1 = loaded.SourceData.ColorPrimary;
                                color2 = loaded.SourceData.ColorSecondary;
                            }
                        } else if (ext == ".obj") {
                            std::filesystem::path axlePath = p.parent_path() / (p.stem().string() + ".axlemesh");
                            if (!std::filesystem::exists(axlePath)) {
                                AssetSerializer::ConvertOBJToAxle(p, axlePath);
                            }
                            auto loaded = AssetSerializer::LoadMesh(axlePath);
                            if (loaded.Success) {
                                loadedVao = loaded.VAO;
                                patternMode = loaded.SourceData.PatternMode;
                                tileScale = loaded.SourceData.TileScale;
                                color1 = loaded.SourceData.ColorPrimary;
                                color2 = loaded.SourceData.ColorSecondary;
                                p = axlePath;
                            } else {
                                loadedVao = PrimitiveMesh::LoadOBJ(p.string());
                            }
                        }

                        if (!loadedVao) loadedVao = m_CubeMesh;
                        entity.AddComponent<MeshInstance3D>(
                            loadedVao, p.string(), patternMode, tileScale,
                            color1, color2
                        );
                        AXLE_INFO("Added MeshInstance3D with 3D model '{0}' to Entity '{1}'", p.filename().string(), entity.GetComponent<TagComponent>().Tag);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::Spacing();
        }

        // Material Component (PBR Textures & Parameters)
        // Material Component (PBR Textures & Parameters)
        DrawComponent<MaterialComponent>("PBR Material", entity, [&](MaterialComponent& mat) {
            // 0. Material Asset Slot (Flyweight Handle & Drag-Drop)
            ImGui::Text("Material Asset:");
            ImGui::SameLine();
            if (mat.HasAsset()) {
                std::string matName = mat.MaterialAsset->GetName();
                if (matName.empty()) matName = "Material";
                std::string label = matName + " [#" + std::to_string((uint64_t)mat.MaterialHandle).substr(0, 8) + "...]";
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.65f, 0.85f));
                ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 65.0f, 24.0f));
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Shared Material Asset: %s\nUUID: %llu\n(Edits synchronize across all entities sharing this asset)",
                        matName.c_str(), (unsigned long long)mat.MaterialHandle);
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 0.85f));
                ImGui::Button("None (Local Instance)", ImVec2(ImGui::GetContentRegionAvail().x - 65.0f, 24.0f));
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Local Material: values are stored directly on this entity.\nDrag an .amat / .axlemat file here to bind a shared asset.");
                }
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* pathStr = static_cast<const char*>(payload->Data);
                    std::filesystem::path droppedPath(pathStr);
                    std::string ext = droppedPath.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                    if (ext == ".amat" || ext == ".axlemat" || ext == ".mat") {
                        UUID handle = AssetManager::GetAssetUUID(droppedPath);
                        auto asset = AssetManager::GetMaterial(droppedPath);
                        if (asset) {
                            mat.SetAsset(handle, asset);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (mat.HasAsset()) {
                if (ImGui::Button("Unlink", ImVec2(60.0f, 24.0f))) {
                    mat.MakeUnique();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Make Unique: detach from shared asset and create independent instance");
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::Button("Inline", ImVec2(60.0f, 24.0f));
                ImGui::EndDisabled();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 1. Material Classification & Summary Badges
            bool isMetal = (mat.Metallic >= 0.8f);
            const char* typeBadge = isMetal ? "Conductor (Metallic)" : "Dielectric (Non-Metal)";
            ImVec4 typeColor = isMetal ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f) : ImVec4(0.40f, 0.70f, 1.0f, 1.0f);

            const char* finishBadge = (mat.Roughness < 0.15f) ? "Mirror / Clearcoat" :
                                      (mat.Roughness < 0.40f) ? "Satin / Gloss" :
                                      (mat.Roughness < 0.70f) ? "Semi-Rough" : "Rough / Diffuse";

            ImGui::TextColored(typeColor, "[%s]", typeBadge);
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", finishBadge);
            if (mat.EmissionIntensity > 0.0f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "| Luminous (%.1f)", mat.EmissionIntensity);
            }
            ImGui::Spacing();

            // 2. Physical Material Presets Selector
            static const char* s_PresetNames[] = {
                "Choose Material Preset...",
                "Automotive Paint (Red Clearcoat)",
                "Automotive Paint (Metallic Blue)",
                "Automotive Paint (Metallic Silver)",
                "Chrome (Mirror Polished)",
                "24K Gold (Polished)",
                "Brushed Copper",
                "Rubber (Tire)",
                "Matte Plastic",
                "Cast Iron / Steel",
                "Polished Aluminum",
                "Obsidian (Black Gloss)",
                "Automotive Headlight (LED / Xenon)",
                "Automotive Taillight (LED Red)"
            };
            int selectedPreset = 0;
            if (ImGui::Combo("Physical Preset", &selectedPreset, s_PresetNames, IM_ARRAYSIZE(s_PresetNames))) {
                switch (selectedPreset) {
                    case 1: mat.SetPresetCarPaintRed(); break;
                    case 2: mat.SetPresetCarPaintBlue(); break;
                    case 3: mat.SetPresetCarPaintSilver(); break;
                    case 4: mat.SetPresetChrome(); break;
                    case 5: mat.SetPresetGold(); break;
                    case 6: mat.SetPresetCopper(); break;
                    case 7: mat.SetPresetRubber(); break;
                    case 8: mat.SetPresetPlastic(); break;
                    case 9: mat.SetPresetCastIron(); break;
                    case 10: mat.SetPresetAluminum(); break;
                    case 11: mat.SetPresetObsidian(); break;
                    case 12: mat.SetPresetHeadlightLED(); break;
                    case 13: mat.SetPresetTaillightLED(); break;
                    default: break;
                }
            }

            // Quick Preset Buttons Bar
            ImGui::TextDisabled("Quick Presets:");
            if (ImGui::SmallButton("Car Paint (Red)")) { mat.SetPresetCarPaintRed(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Car Paint (Blue)")) { mat.SetPresetCarPaintBlue(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Car Paint (Silver)")) { mat.SetPresetCarPaintSilver(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Chrome")) { mat.SetPresetChrome(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Gold")) { mat.SetPresetGold(); }

            if (ImGui::SmallButton("Copper")) { mat.SetPresetCopper(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Rubber Tire")) { mat.SetPresetRubber(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Plastic")) { mat.SetPresetPlastic(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Steel")) { mat.SetPresetCastIron(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Aluminum")) { mat.SetPresetAluminum(); }

            if (ImGui::SmallButton("Obsidian")) { mat.SetPresetObsidian(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("LED Headlight")) { mat.SetPresetHeadlightLED(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("LED Taillight")) { mat.SetPresetTaillightLED(); }

            ImGui::Separator();
            ImGui::Spacing();

            auto DrawTextureSlot = [&](const char* slotName, 
                                       std::shared_ptr<Texture2D>& tex, 
                                       std::string& path, 
                                       bool& enabled, 
                                       auto onSetPath) {
                ImGui::PushID(slotName);
                ImGui::TextDisabled("%s", slotName);

                // 44x44 thumbnail card
                ImTextureID texId = 0;
                if (tex && tex->IsLoaded()) {
                    texId = (ImTextureID)(intptr_t)tex->GetRendererID();
                }

                if (texId) {
                    ImGui::Image(texId, ImVec2(44.0f, 44.0f), ImVec2(0, 1), ImVec2(1, 0), ImVec4(1, 1, 1, 1), ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.15f, 0.20f, 0.85f));
                    ImGui::Button("Drop\nTex", ImVec2(44.0f, 44.0f));
                    ImGui::PopStyleColor();
                }

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = static_cast<const char*>(payload->Data);
                        std::filesystem::path p(pathStr);
                        std::string ext = p.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
                            onSetPath(p.string());
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Checkbox("Use Texture", &enabled);

                std::string displayName = path.empty() ? "[ Drag Texture Here ]" : std::filesystem::path(path).filename().string();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.20f, 0.26f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.32f, 0.44f, 1.0f));
                ImGui::Button(displayName.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 28.0f, 22.0f));
                ImGui::PopStyleColor(2);

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = static_cast<const char*>(payload->Data);
                        std::filesystem::path p(pathStr);
                        std::string ext = p.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
                            onSetPath(p.string());
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button("X", ImVec2(22.0f, 22.0f))) {
                    onSetPath("");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear texture");

                ImGui::EndGroup();
                ImGui::PopID();
                ImGui::Spacing();
            };

            // 1. Albedo
            DrawTextureSlot("Albedo Map (Base Color)", mat.AlbedoMap, mat.AlbedoPath, mat.UseAlbedoMap,
                [&](const std::string& p) { mat.SetAlbedo(p); });
            ImGui::ColorEdit4("Albedo Tint", glm::value_ptr(mat.AlbedoColor));
            ImGui::Spacing();

            // 2. Normal Map
            DrawTextureSlot("Normal Map (Cotangent Perturbation)", mat.NormalMap, mat.NormalPath, mat.UseNormalMap,
                [&](const std::string& p) { mat.SetNormal(p); });
            if (mat.UseNormalMap) {
                ImGui::SliderFloat("Normal Strength", &mat.NormalStrength, 0.0f, 3.0f, "%.2f");
            }
            ImGui::Spacing();

            // 3. Roughness
            DrawTextureSlot("Roughness Map", mat.RoughnessMap, mat.RoughnessPath, mat.UseRoughnessMap,
                [&](const std::string& p) { mat.SetRoughnessMap(p); });
            if (!mat.UseRoughnessMap) {
                ImGui::SliderFloat("Roughness", &mat.Roughness, 0.04f, 1.0f, "%.2f");
            } else {
                ImGui::TextDisabled("  [Active] Roughness sampled from texture red channel");
            }
            ImGui::Spacing();

            // 4. Metallic
            DrawTextureSlot("Metallic Map", mat.MetallicMap, mat.MetallicPath, mat.UseMetallicMap,
                [&](const std::string& p) { mat.SetMetallicMap(p); });
            if (!mat.UseMetallicMap) {
                ImGui::SliderFloat("Metallic", &mat.Metallic, 0.0f, 1.0f, "%.2f");
            } else {
                ImGui::TextDisabled("  [Active] Metallic conductivity sampled from texture red channel");
            }
            ImGui::Spacing();

            // 5. Emission (Automotive Lighting & Neon Glow)
            ImGui::ColorEdit3("Emission Color", glm::value_ptr(mat.EmissionColor));
            ImGui::DragFloat("Emission Intensity", &mat.EmissionIntensity, 0.1f, 0.0f, 25.0f, "%.1f");
            if (mat.EmissionIntensity > 0.0f) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##Emission")) {
                    mat.EmissionColor = glm::vec3(0.0f);
                    mat.EmissionIntensity = 0.0f;
                }
            }
            ImGui::Spacing();

            // 6. UV Tiling
            ImGui::DragFloat2("UV Scale", glm::value_ptr(mat.UVScale), 0.1f, 0.01f, 100.0f, "%.2f");
            ImGui::TextDisabled("UV Presets:");
            ImGui::SameLine();
            if (ImGui::SmallButton("1x1")) mat.UVScale = glm::vec2(1.0f);
            ImGui::SameLine();
            if (ImGui::SmallButton("2x2")) mat.UVScale = glm::vec2(2.0f);
            ImGui::SameLine();
            if (ImGui::SmallButton("4x4")) mat.UVScale = glm::vec2(4.0f);
            ImGui::SameLine();
            if (ImGui::SmallButton("8x8")) mat.UVScale = glm::vec2(8.0f);
            ImGui::SameLine();
            if (ImGui::SmallButton("16x16")) mat.UVScale = glm::vec2(16.0f);

            // Quick Texture Pipeline Actions
            ImGui::Spacing();
            ImGui::TextDisabled("Texture Setup Tools:");
            if (ImGui::SmallButton("Prototype Grid (512x512)")) {
                mat.SetAlbedo("textures/Prototype_Grid.png");
                mat.SetNormal("textures/Flat_Normal.png");
                mat.SetRoughnessMap("textures/Roughness_Gradient.png");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset All Textures")) {
                mat.ResetTextures();
                mat.Roughness = 0.5f;
                mat.Metallic = 0.0f;
                mat.NormalStrength = 1.0f;
                mat.EmissionColor = glm::vec3(0.0f);
                mat.EmissionIntensity = 0.0f;
            }
        }, /*canRemove=*/ true);

        // Collision Shape 3D Component
        DrawComponent<CollisionShape3D>("CollisionShape3D", entity, [&](CollisionShape3D& cs) {
            const char* shapeTypeNames[] = { "Box", "Sphere", "Cylinder", "Cone", "Plane", "Mesh" };
            int currentShape = static_cast<int>(cs.ShapeType);
            if (ImGui::Combo("Shape Type", &currentShape, shapeTypeNames, IM_ARRAYSIZE(shapeTypeNames))) {
                cs.ShapeType = static_cast<CollisionShapeType>(currentShape);
            }

            switch (cs.ShapeType) {
                case CollisionShapeType::Box:
                    ImGui::DragFloat3("Half Extents", glm::value_ptr(cs.HalfExtent), 0.05f, 0.01f, 100.0f);
                    break;
                case CollisionShapeType::Sphere:
                    ImGui::DragFloat("Radius", &cs.Radius, 0.05f, 0.01f, 100.0f);
                    break;
                case CollisionShapeType::Cylinder:
                    ImGui::DragFloat("Half Height", &cs.HalfHeight, 0.05f, 0.01f, 100.0f);
                    ImGui::DragFloat("Radius", &cs.Radius, 0.05f, 0.01f, 100.0f);
                    break;
                case CollisionShapeType::Cone:
                    ImGui::DragFloat("Half Height", &cs.HalfHeight, 0.05f, 0.01f, 100.0f);
                    ImGui::DragFloat("Radius", &cs.Radius, 0.05f, 0.01f, 100.0f);
                    break;
                case CollisionShapeType::Plane:
                    ImGui::DragFloat("Plane Half Extent", &cs.HalfExtent.x, 0.5f, 0.1f, 1000.0f);
                    cs.HalfExtent.z = cs.HalfExtent.x;
                    break;
                case CollisionShapeType::Mesh:
                    ImGui::TextDisabled("Mesh shape matches active MeshInstance3D");
                    break;
            }

            ImGui::DragFloat3("Offset", glm::value_ptr(cs.Offset), 0.05f);

            if (ImGui::SliderFloat("Friction", &cs.Friction, 0.0f, 2.0f, "%.2f")) {
                if (entity.HasComponent<RigidBody3D>() && m_PhysicsScene) {
                    auto& rb = entity.GetComponent<RigidBody3D>();
                    if (!rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                        JPH::BodyLockWrite lock(m_PhysicsScene->GetSystem().GetBodyLockInterface(), rb.RuntimeBodyID);
                        if (lock.Succeeded()) lock.GetBody().SetFriction(cs.Friction);
                    }
                }
            }

            if (ImGui::SliderFloat("Restitution", &cs.Restitution, 0.0f, 1.0f, "%.2f")) {
                if (entity.HasComponent<RigidBody3D>() && m_PhysicsScene) {
                    auto& rb = entity.GetComponent<RigidBody3D>();
                    if (!rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                        JPH::BodyLockWrite lock(m_PhysicsScene->GetSystem().GetBodyLockInterface(), rb.RuntimeBodyID);
                        if (lock.Succeeded()) lock.GetBody().SetRestitution(cs.Restitution);
                    }
                }
            }

            ImGui::Checkbox("Is Trigger (Sensor)", &cs.IsTrigger);
        }, /*canRemove=*/ true);

        // Rigid Body 3D Component
        DrawComponent<RigidBody3D>("RigidBody3D", entity, [&](RigidBody3D& rb) {
            ImGui::Text("Body ID: %u", rb.RuntimeBodyID.GetIndexAndSequenceNumber());

            const char* motionNames[] = { "Static", "Kinematic", "Dynamic" };
            int currentMotion = static_cast<int>(rb.MotionType);
            if (ImGui::Combo("Motion Type", &currentMotion, motionNames, IM_ARRAYSIZE(motionNames))) {
                rb.MotionType = static_cast<BodyMotionType>(currentMotion);
                rb.IsDynamic = (rb.MotionType == BodyMotionType::Dynamic);
                if (m_PhysicsScene && !rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                    JPH::EMotionType jType = JPH::EMotionType::Dynamic;
                    if (rb.MotionType == BodyMotionType::Static) jType = JPH::EMotionType::Static;
                    else if (rb.MotionType == BodyMotionType::Kinematic) jType = JPH::EMotionType::Kinematic;
                    JPH::EActivation act = (jType == JPH::EMotionType::Static) ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
                    m_PhysicsScene->GetBodyInterface().SetMotionType(rb.RuntimeBodyID, jType, act);
                }
            }

            ImGui::DragFloat("Mass (kg)", &rb.Mass, 1.0f, 0.01f, 100000.0f, "%.1f");
            ImGui::DragFloat("Linear Damping", &rb.LinearDamping, 0.01f, 0.0f, 10.0f, "%.2f");
            ImGui::DragFloat("Angular Damping", &rb.AngularDamping, 0.01f, 0.0f, 10.0f, "%.2f");
            ImGui::DragFloat("Gravity Scale", &rb.GravityFactor, 0.05f, -10.0f, 10.0f, "%.2f");

            // Warning if RigidBody3D has no CollisionShape3D
            if (!entity.HasComponent<CollisionShape3D>()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[!] No CollisionShape3D attached.");
                if (ImGui::Button("+ Add CollisionShape3D")) {
                    entity.AddComponent<CollisionShape3D>();
                }
            }

            if (m_PhysicsScene && !rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                const bool isActive = m_PhysicsScene->IsActive(rb.RuntimeBodyID);
                ImGui::Text("State: ");
                ImGui::SameLine();
                if (isActive) {
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "ACTIVE");
                } else {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "SLEEPING");
                }

                bool isDynamicInPhysics = false;
                if (m_PhysicsScene && !rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                    isDynamicInPhysics = (m_PhysicsScene->GetBodyInterface().GetMotionType(rb.RuntimeBodyID) == JPH::EMotionType::Dynamic);
                }

                if (isDynamicInPhysics) {
                    bool allowSleeping = true;
                    {
                        JPH::BodyLockRead lock(m_PhysicsScene->GetSystem().GetBodyLockInterface(), rb.RuntimeBodyID);
                        if (lock.Succeeded()) {
                            const JPH::Body& body = lock.GetBody();
                            if (!body.IsStatic() && body.GetMotionProperties() != nullptr) {
                                allowSleeping = body.GetAllowSleeping();
                            }
                        }
                    }

                    if (ImGui::Checkbox("Allow Sleeping", &allowSleeping)) {
                        JPH::BodyLockWrite lock(m_PhysicsScene->GetSystem().GetBodyLockInterface(), rb.RuntimeBodyID);
                        if (lock.Succeeded()) {
                            JPH::Body& body = lock.GetBody();
                            if (!body.IsStatic() && body.GetMotionProperties() != nullptr) {
                                body.SetAllowSleeping(allowSleeping);
                            }
                        }
                    }

                    if (ImGui::Button("Wake Up")) {
                        m_PhysicsScene->ActivateBody(rb.RuntimeBodyID);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Put to Sleep")) {
                        m_PhysicsScene->DeactivateBody(rb.RuntimeBodyID);
                    }

                    const glm::vec3 vel = m_PhysicsScene->GetLinearVelocity(rb.RuntimeBodyID);
                    ImGui::Text("Velocity: (%.2f, %.2f, %.2f) m/s", vel.x, vel.y, vel.z);
                    ImGui::Text("Linear Speed: %.2f m/s", glm::length(vel));

                    if (ImGui::Button("Zero Velocity")) {
                        m_PhysicsScene->SetLinearVelocity(rb.RuntimeBodyID, glm::vec3(0.0f));
                    }
                }
            }
        }, /*canRemove=*/ true, [&](RigidBody3D& rb) {
            if (m_PhysicsScene && !rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                m_PhysicsScene->DestroyBody(rb.RuntimeBodyID);
            }
        });

        // Vehicle Component (Phase 20.2: Vehicle Inspector Tuning & Quick-Setup Presets)
        DrawComponent<VehicleComponent>("Vehicle Dynamics", entity, [&](VehicleComponent& vc) {
            // Section 1: Dedicated Live Simulation Telemetry Cards (Active during Play mode)
            Vehicle* runtimeVehicle = m_Scene ? m_Scene->GetRuntimeVehicle(entity) : nullptr;
            bool isSimulating = (runtimeVehicle != nullptr) || vc.HasActivePhysics();

            if (isSimulating) {
                VehicleTelemetryData telemetry;
                if (runtimeVehicle) {
                    telemetry = runtimeVehicle->GetTelemetry();
                } else {
                    telemetry.SpeedKmh = vc.GetSpeedKmh();
                    telemetry.SpeedMps = vc.GetSpeedMps();
                    telemetry.EngineRPM = vc.GetRPM();
                    telemetry.CurrentGear = vc.GetCurrentGear();
                }

                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.75f, 0.5f, 0.8f));
                ImGui::BeginChild("##VehicleTelemetryCard", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoScrollbar);
                
                ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "● SIMULATION ACTIVE");
                ImGui::SameLine();
                ImGui::TextDisabled("| Live Vehicle Telemetry");
                ImGui::Separator();

                // 1. Digital Speedometer & Tachometer Card
                float speedKmh = telemetry.SpeedKmh;
                ImVec4 speedColor = (speedKmh > 160.0f) ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                                  : (speedKmh > 90.0f)  ? ImVec4(1.0f, 0.85f, 0.35f, 1.0f)
                                                        : ImVec4(0.35f, 0.95f, 0.75f, 1.0f);
                ImGui::TextColored(speedColor, "SPEED: %.1f km/h", speedKmh);
                ImGui::SameLine();
                ImGui::TextDisabled("(%.1f m/s)", telemetry.SpeedMps);

                float speedFraction = std::clamp(speedKmh / 240.0f, 0.0f, 1.0f);
                char speedBuf[64];
                snprintf(speedBuf, sizeof(speedBuf), "%.0f / 240 km/h", speedKmh);
                ImGui::ProgressBar(speedFraction, ImVec2(-1.0f, 18.0f), speedBuf);

                // Tachometer & Automatic Transmission
                float rpm = telemetry.EngineRPM;
                float maxRpm = (vc.MaxRPM > 0.0f) ? vc.MaxRPM : 7500.0f;
                float rpmFraction = std::clamp(rpm / maxRpm, 0.0f, 1.0f);
                ImVec4 rpmColor = (rpmFraction > 0.85f) ? ImVec4(1.0f, 0.25f, 0.25f, 1.0f) : ImVec4(0.4f, 0.8f, 1.0f, 1.0f);

                int gear = telemetry.CurrentGear;
                const char* gearStr = (gear == -1) ? "R (Reverse)" : (gear == 0) ? "N (Neutral)" : "D";
                if (gear > 0) {
                    ImGui::TextColored(rpmColor, "TACHOMETER: %.0f RPM | GEAR: %s %d", rpm, gearStr, gear);
                } else {
                    ImGui::TextColored(rpmColor, "TACHOMETER: %.0f RPM | GEAR: %s", rpm, gearStr);
                }

                char rpmBuf[64];
                snprintf(rpmBuf, sizeof(rpmBuf), "%.0f / %.0f RPM", rpm, maxRpm);
                ImGui::ProgressBar(rpmFraction, ImVec2(-1.0f, 18.0f), rpmBuf);

                // 2. Driver Input Card (ECS Real-time Feed)
                ImGui::Spacing();
                ImGui::TextDisabled("Driver Inputs (ECS):");
                ImGui::Columns(3, "##DriverInputsCols", false);
                ImGui::Text("Throttle: %.0f%%", vc.Throttle * 100.0f);
                ImGui::ProgressBar(std::clamp(vc.Throttle, 0.0f, 1.0f), ImVec2(-1.0f, 12.0f), "");
                ImGui::NextColumn();
                ImGui::Text("Steering: %+.0f%%", vc.Steering * 100.0f);
                ImGui::ProgressBar(std::clamp((vc.Steering + 1.0f) * 0.5f, 0.0f, 1.0f), ImVec2(-1.0f, 12.0f), "");
                ImGui::NextColumn();
                ImGui::Text("Brake: %.0f%%", vc.Brake * 100.0f);
                ImGui::ProgressBar(std::clamp(vc.Brake, 0.0f, 1.0f), ImVec2(-1.0f, 12.0f), "");
                ImGui::Columns(1);

                if (vc.Handbrake) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[P] HANDBRAKE ENGAGED");
                }

                // 3. Per-Wheel Suspension & Ground Contact Readouts
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Wheel Telemetry & Suspension Readouts", ImGuiTreeNodeFlags_DefaultOpen)) {
                    u32 wheelCount = runtimeVehicle ? runtimeVehicle->GetWheelCount() : 0;
                    if (wheelCount > 0) {
                        for (u32 w = 0; w < wheelCount; ++w) {
                            const auto& wheel = (w < 4) ? telemetry.Wheels[w] : runtimeVehicle->GetTelemetry().Wheels[0];
                            bool grounded = wheel.HasContact;
                            ImVec4 contactColor = grounded ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f) : ImVec4(0.95f, 0.7f, 0.2f, 1.0f);

                            ImGui::Text("Wheel [%u]:", w);
                            ImGui::SameLine();
                            ImGui::TextColored(contactColor, "[%s]", grounded ? "GROUND" : "AIR");
                            ImGui::SameLine();
                            ImGui::Text("Travel: %.3fm | Steer: %+.1f° | Slip: (%.2f, %.2f)",
                                wheel.SuspensionLength,
                                glm::degrees(wheel.SteerAngle),
                                wheel.LongitudinalSlip,
                                wheel.LateralSlip);
                        }
                    } else {
                        // Scan child entities with WheelComponent
                        int childIdx = 0;
                        for (auto child : entity.GetChildren()) {
                            if (child && child.HasComponent<WheelComponent>()) {
                                const auto& wc = child.GetComponent<WheelComponent>();
                                bool grounded = wc.IsGrounded;
                                ImVec4 contactColor = grounded ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f) : ImVec4(0.95f, 0.7f, 0.2f, 1.0f);
                                ImGui::Text("Wheel [%d] '%s':", childIdx++, child.GetName().c_str());
                                ImGui::SameLine();
                                ImGui::TextColored(contactColor, "[%s]", grounded ? "GROUND" : "AIR");
                                ImGui::SameLine();
                                ImGui::Text("Travel: %.3fm | AngVel: %.1f rad/s", wc.SuspensionLength, wc.AngularVelocity);
                            }
                        }
                    }
                    ImGui::TreePop();
                }

                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::Separator();
            } else {
                ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Status: Edit Mode (Press F5 to Simulate)");
                ImGui::Separator();
            }

            // Section 2: Chassis & Powertrain Tuning Sliders
            ImGui::TextDisabled("Chassis Weight & Distribution:");
            if (ImGui::DragFloat("Chassis Mass (kg)", &vc.ChassisMass, 10.0f, 100.0f, 25000.0f, "%.0f")) {
                if (entity.HasComponent<RigidBody3D>()) {
                    entity.GetComponent<RigidBody3D>().Mass = vc.ChassisMass;
                }
            }
            ImGui::DragFloat3("Center of Mass Offset", &vc.CenterOfMassOffset.x, 0.01f, -5.0f, 5.0f, "%.2f");
            ImGui::DragFloat("Anti-Roll Bar (N/m)", &vc.AntiRollBarStiffness, 25.0f, 0.0f, 10000.0f, "%.0f");

            // Driver Input Monitor (ECS Data Buffer)
            ImGui::Spacing();
            ImGui::TextDisabled("Driver Inputs (ECS State):");
            ImGui::Text("Throttle: %+.2f | Steer: %+.2f | Brake: %.2f | Handbrake: %s",
                vc.Throttle, vc.Steering, vc.Brake, vc.Handbrake ? "ENGAGED" : "OFF");

            ImGui::Spacing();
            ImGui::TextDisabled("Powertrain & Torque:");
            ImGui::DragFloat("Max Engine Torque (N.m)", &vc.MaxEngineTorque, 20.0f, 100.0f, 15000.0f, "%.0f");
            ImGui::DragFloat("Max RPM", &vc.MaxRPM, 50.0f, 2000.0f, 12000.0f, "%.0f");
            ImGui::DragFloat("Idle RPM", &vc.MinRPM, 50.0f, 500.0f, 3000.0f, "%.0f");
            ImGui::DragFloat("Max Brake Torque (N.m)", &vc.MaxBrakeTorque, 50.0f, 500.0f, 30000.0f, "%.0f");
            ImGui::DragFloat("Max Handbrake Torque (N.m)", &vc.MaxHandBrakeTorque, 50.0f, 500.0f, 30000.0f, "%.0f");
            ImGui::Checkbox("All-Wheel Drive (AWD)", &vc.FourWheelDrive);
        }, /*canRemove=*/ true);

        // Wheel Component (VehicleWheel3D)
        DrawComponent<WheelComponent>("Vehicle Wheel (VehicleWheel3D)", entity, [&](WheelComponent& wc) {
            ImGui::Checkbox("Auto-Sync Visual Mesh", &wc.AutoSyncMesh);
            ImGui::DragFloat("Radius (m)", &wc.Radius, 0.01f, 0.05f, 2.0f, "%.2f");
            ImGui::DragFloat("Width (m)", &wc.Width, 0.01f, 0.05f, 2.0f, "%.2f");
            ImGui::Separator();
            ImGui::TextDisabled("Suspension:");
            ImGui::DragFloat("Min Compression (m)", &wc.SuspensionMinLength, 0.01f, 0.02f, 1.0f, "%.2f");
            ImGui::DragFloat("Max Extension (m)", &wc.SuspensionMaxLength, 0.01f, 0.05f, 2.0f, "%.2f");
            ImGui::DragFloat("Spring Frequency (Hz)", &wc.SpringFrequency, 0.1f, 0.5f, 10.0f, "%.1f");
            ImGui::DragFloat("Spring Damping", &wc.SpringDamping, 0.05f, 0.0f, 2.0f, "%.2f");
            ImGui::Separator();
            ImGui::TextDisabled("Steering & Powertrain:");
            ImGui::Checkbox("Steerable (Steering Input)", &wc.CanSteer);
            if (wc.CanSteer) {
                ImGui::DragFloat("Max Steer Angle (deg)", &wc.MaxSteerAngle, 0.5f, 5.0f, 60.0f, "%.1f");
            }
            ImGui::Checkbox("Driven (Engine Traction)", &wc.IsDriven);
            ImGui::DragFloat("Brake Torque (N.m)", &wc.BrakeTorque, 50.0f, 0.0f, 20000.0f, "%.0f");
            ImGui::Checkbox("Handbrake", &wc.HasHandbrake);
            if (wc.HasHandbrake && wc.HandBrakeTorque == 0.0f) {
                wc.HandBrakeTorque = 6000.0f;
            } else if (!wc.HasHandbrake) {
                wc.HandBrakeTorque = 0.0f;
            }

            if (wc.IsGrounded || wc.SuspensionLength > 0.0f || std::abs(wc.AngularVelocity) > 0.01f) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "Live Wheel Telemetry:");
                ImVec4 contactCol = wc.IsGrounded ? ImVec4(0.2f, 0.9f, 0.4f, 1.0f) : ImVec4(0.95f, 0.7f, 0.2f, 1.0f);
                ImGui::TextColored(contactCol, "Contact: %s", wc.IsGrounded ? "GROUNDED" : "IN AIR");
                ImGui::SameLine();
                ImGui::Text("| Travel: %.3f m", wc.SuspensionLength);

                float maxLen = std::max(wc.SuspensionMaxLength, 0.01f);
                float compFraction = std::clamp(wc.SuspensionLength / maxLen, 0.0f, 1.0f);
                char compBuf[32];
                snprintf(compBuf, sizeof(compBuf), "%.1f %%", compFraction * 100.0f);
                ImGui::ProgressBar(compFraction, ImVec2(-1.0f, 14.0f), compBuf);

                ImGui::Text("Angular Vel: %.1f rad/s (%.0f RPM)", wc.AngularVelocity, (wc.AngularVelocity * 60.0f) / (2.0f * 3.14159f));
                ImGui::Text("Slip: Long=%.2f, Lat=%.2f | Angle=%.1f°", wc.LongitudinalSlip, wc.LateralSlip, wc.SlipAngle);
            }
        }, /*canRemove=*/ true);



        // Directional Light Component
        DrawComponent<DirectionalLightComponent>("Directional Sun Light", entity, [&](DirectionalLightComponent& dlc) {
            // Light Color
            ImGui::ColorEdit3("Light Color", glm::value_ptr(dlc.Color));

            // Intensity
            ImGui::SliderFloat("Intensity", &dlc.Intensity, 0.0f, 5.0f, "%.2f");

            // Ambient Intensity
            ImGui::SliderFloat("Ambient Fill", &dlc.AmbientIntensity, 0.0f, 1.0f, "%.2f");

            // Sun Angles
            ImGui::SliderFloat("Azimuth (Heading)", &dlc.Azimuth, 0.0f, 360.0f, "%.1f deg");
            ImGui::SliderFloat("Elevation (Altitude)", &dlc.Elevation, 5.0f, 89.0f, "%.1f deg");

            // Cast Shadows Toggle
            ImGui::Checkbox("Cast Real-Time Shadows", &dlc.CastShadows);

            if (dlc.CastShadows) {
                ImGui::Indent(15.0f);
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Soft Shadow Settings:");

                const char* shadowQualities[] = { "Low (1024x1024)", "High (2048x2048)", "Ultra (4096x4096)" };
                int currentQuality = glm::clamp(dlc.ShadowQuality, 0, 2);
                if (ImGui::Combo("Shadow Quality", &currentQuality, shadowQualities, IM_ARRAYSIZE(shadowQualities))) {
                    dlc.ShadowQuality = currentQuality;
                }

                ImGui::SliderFloat("Coverage (Ortho Size)", &dlc.ShadowOrthoSize, 5.0f, 100.0f, "%.1f m");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Orthographic projection extent. Smaller values yield denser texel resolution for close-ups.");
                }

                ImGui::SliderFloat("Penumbra Softness", &dlc.ShadowFilterRadius, 0.5f, 6.0f, "%.1f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("16-tap Vogel spiral PCF filter spread. Higher values produce smoother, softer penumbra edges.");
                }

                ImGui::DragFloat("Depth Bias", &dlc.ShadowBias, 0.0001f, 0.0001f, 0.01f, "%.5f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Adaptive depth bias to eliminate shadow acne and moiré artifacts.");
                }

                ImGui::Unindent(15.0f);
            }

            // Quick Atmosphere Presets
            ImGui::Spacing();
            ImGui::TextDisabled("Atmosphere Presets:");
            if (ImGui::SmallButton("Noon Daylight")) {
                dlc.Color = glm::vec3(1.0f, 0.98f, 0.92f);
                dlc.Intensity = 1.2f;
                dlc.AmbientIntensity = 0.28f;
                dlc.Azimuth = 45.0f;
                dlc.Elevation = 65.0f;
                dlc.CastShadows = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Golden Hour / Sunset")) {
                dlc.Color = glm::vec3(1.0f, 0.58f, 0.25f);
                dlc.Intensity = 1.35f;
                dlc.AmbientIntensity = 0.22f;
                dlc.Azimuth = 240.0f;
                dlc.Elevation = 15.0f;
                dlc.CastShadows = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Night Moonlight")) {
                dlc.Color = glm::vec3(0.45f, 0.60f, 0.95f);
                dlc.Intensity = 0.40f;
                dlc.AmbientIntensity = 0.15f;
                dlc.Azimuth = 135.0f;
                dlc.Elevation = 45.0f;
                dlc.CastShadows = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Overcast Studio")) {
                dlc.Color = glm::vec3(0.95f, 0.95f, 1.0f);
                dlc.Intensity = 1.0f;
                dlc.AmbientIntensity = 0.45f;
                dlc.Azimuth = 45.0f;
                dlc.Elevation = 70.0f;
                dlc.CastShadows = true;
            }

            // --- PHOTOMETRIC POST-PROCESSING & TONE MAPPING (Phase 11: Sub-step 3.2) ---
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "HDR Filmic Tone Mapping & Gamma:");

            ImGui::SliderFloat("Exposure", &dlc.Exposure, 0.1f, 5.0f, "%.2f");

            const char* toneMappingNames[] = {
                "0: ACES Filmic (Cinematic)",
                "1: Reinhard (Standard)",
                "2: Uncharted 2 (Hable)",
                "3: None (Linear Bypass)"
            };
            int currentTM = dlc.ToneMappingMode;
            if (ImGui::Combo("Tone Mapping", &currentTM, toneMappingNames, IM_ARRAYSIZE(toneMappingNames))) {
                dlc.ToneMappingMode = currentTM;
            }

            ImGui::SliderFloat("Display Gamma", &dlc.Gamma, 1.0f, 3.0f, "%.2f");

            ImGui::TextDisabled("Quick Looks:");
            ImGui::SameLine();
            if (ImGui::SmallButton("ACES Filmic")) {
                dlc.Exposure = 1.0f;
                dlc.ToneMappingMode = 0;
                dlc.Gamma = 2.2f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("High Exposure")) {
                dlc.Exposure = 1.6f;
                dlc.ToneMappingMode = 0;
                dlc.Gamma = 2.2f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reinhard")) {
                dlc.Exposure = 1.2f;
                dlc.ToneMappingMode = 1;
                dlc.Gamma = 2.2f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Raw Linear")) {
                dlc.Exposure = 1.0f;
                dlc.ToneMappingMode = 3;
                dlc.Gamma = 1.0f;
            }

            // Vector display
            glm::vec3 dir = dlc.GetDirection();
            ImGui::TextDisabled("Light Direction: (%.2f, %.2f, %.2f)", dir.x, dir.y, dir.z);
        }, /*canRemove=*/ true);

        // Skybox Component (Procedural Atmospheric vs HDR Cubemap IBL)
        DrawComponent<SkyboxComponent>("Skybox & Environment", entity, [&](SkyboxComponent& sbc) {
            ImGui::Checkbox("Enable Skybox", &sbc.Enabled);

            const char* skyboxTypes[] = { "Procedural Atmospheric", "HDR Cubemap (IBL)" };
            int currentType = (sbc.Type == SkyboxType::HDRCubemap) ? 1 : 0;
            if (ImGui::Combo("Skybox Type", &currentType, skyboxTypes, IM_ARRAYSIZE(skyboxTypes))) {
                sbc.Type = (currentType == 1) ? SkyboxType::HDRCubemap : SkyboxType::ProceduralAtmospheric;
            }

            ImGui::Spacing();

            if (sbc.Type == SkyboxType::HDRCubemap) {
                ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "HDR Environment Map (IBL):");

                char hdrBuf[260];
                strncpy(hdrBuf, sbc.HDRPath.c_str(), sizeof(hdrBuf));
                hdrBuf[sizeof(hdrBuf) - 1] = '\0';
                if (ImGui::InputText("##HDRPath", hdrBuf, sizeof(hdrBuf))) {
                    sbc.HDRPath = hdrBuf;
                }

                // Drag & Drop .hdr file from Content Browser
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = static_cast<const char*>(payload->Data);
                        std::filesystem::path p(pathStr);
                        std::string ext = p.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                        if (ext == ".hdr" || ext == ".exr") {
                            sbc.HDRPath = p.string();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button("Browse...")) {
                    std::string selected = FileDialogs::OpenFile("Radiance HDR (*.hdr)\0*.hdr\0All Files (*.*)\0*.*\0");
                    if (!selected.empty()) {
                        sbc.HDRPath = selected;
                    }
                }

                ImGui::SliderFloat("Radiance / Intensity", &sbc.Intensity, 0.0f, 5.0f, "%.2f");
                ImGui::SliderFloat("Exposure", &sbc.Exposure, 0.1f, 5.0f, "%.2f");

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Image-Based Lighting provides real-time diffuse irradiance and pre-filtered GGX specular reflections.");
                }
            } else {
                ImGui::Checkbox("Hemisphere Ambient Lighting", &sbc.HemisphereAmbient);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Compute realistic ambient irradiance gradients (Zenith, Horizon, Ground) from atmospheric sky.");
                }
                ImGui::Spacing();

                // Atmosphere Colors
                ImGui::ColorEdit3("Zenith Color", glm::value_ptr(sbc.ZenithColor));
                ImGui::ColorEdit3("Horizon Color", glm::value_ptr(sbc.HorizonColor));
                ImGui::ColorEdit3("Ground Color", glm::value_ptr(sbc.GroundColor));
                ImGui::Spacing();

                // Atmospheric Properties
                ImGui::SliderFloat("Turbidity (Haze)", &sbc.Turbidity, 0.2f, 4.0f, "%.2f");
                ImGui::SliderFloat("Sun Disk Size", &sbc.SunSize, 0.01f, 0.15f, "%.3f");
                ImGui::SliderFloat("Sun Glow (Corona)", &sbc.SunGlow, 0.02f, 0.80f, "%.2f");
                ImGui::Spacing();

                // Sun Alignment
                ImGui::Checkbox("Sync with Directional Sun Light", &sbc.SyncWithSun);
                if (!sbc.SyncWithSun) {
                    ImGui::SliderFloat("Custom Sun Azimuth", &sbc.CustomAzimuth, 0.0f, 360.0f, "%.1f deg");
                    ImGui::SliderFloat("Custom Sun Elevation", &sbc.CustomElevation, 1.0f, 89.0f, "%.1f deg");
                }
                ImGui::Spacing();

                // Atmosphere Presets
                ImGui::TextDisabled("Atmosphere Presets:");
                if (ImGui::SmallButton("Clear Noon")) {
                    sbc.SetPresetNoon();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Golden Sunset")) {
                    sbc.SetPresetSunset();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Starry Night")) {
                    sbc.SetPresetNight();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Overcast")) {
                    sbc.SetPresetOvercast();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Mars Sky")) {
                    sbc.SetPresetAlienMars();
                }
            }
        }, /*canRemove=*/ true);

        // Point Light Component
        DrawComponent<PointLightComponent>("Point Light", entity, [&](PointLightComponent& plc) {
            ImGui::ColorEdit3("Light Color", glm::value_ptr(plc.Color));
            ImGui::DragFloat("Intensity", &plc.Intensity, 0.1f, 0.0f, 100.0f, "%.2f");
            ImGui::DragFloat("Radius", &plc.Radius, 0.5f, 0.1f, 500.0f, "%.1f m");
            ImGui::DragFloat("Falloff", &plc.Falloff, 0.05f, 0.1f, 10.0f, "%.2f");
        }, /*canRemove=*/ true);

        // Spot Light Component
        DrawComponent<SpotLightComponent>("Spot Light", entity, [&](SpotLightComponent& slc) {
            ImGui::Checkbox("Enabled", &slc.Enabled);
            ImGui::ColorEdit3("Light Color", glm::value_ptr(slc.Color));
            ImGui::DragFloat("Intensity", &slc.Intensity, 0.1f, 0.0f, 100.0f, "%.2f");
            ImGui::DragFloat("Range", &slc.Range, 0.5f, 0.1f, 500.0f, "%.1f m");
            ImGui::DragFloat("Inner Cone Angle", &slc.InnerConeAngle, 0.5f, 1.0f, 89.0f, "%.1f deg");
            ImGui::DragFloat("Outer Cone Angle", &slc.OuterConeAngle, 0.5f, slc.InnerConeAngle, 89.9f, "%.1f deg");
        }, /*canRemove=*/ true);

        // Camera Component
        DrawComponent<CameraComponent>("Camera", entity, [&](CameraComponent& cam) {
            ImGui::DragFloat("Field of View", &cam.FOV, 0.5f, 10.0f, 150.0f, "%.1f deg");
            ImGui::DragFloat("Near Clip", &cam.NearClip, 0.01f, 0.001f, 10.0f, "%.3f m");
            ImGui::DragFloat("Far Clip", &cam.FarClip, 1.0f, 1.0f, 10000.0f, "%.1f m");
            ImGui::Checkbox("Primary Camera", &cam.Primary);
        }, /*canRemove=*/ true);

        // Native Script Component
        DrawComponent<NativeScriptComponent>("Native Script", entity, [&](NativeScriptComponent& nsc) {
            // Script Class Combo Dropdown
            const auto& registeredScripts = ScriptRegistry::GetScripts();
            std::vector<std::string> scriptNames;
            for (const auto& [name, _] : registeredScripts) {
                scriptNames.push_back(name);
            }

            ImGui::Text("Script Class:");
            ImGui::SameLine();
            std::string currentScript = nsc.ScriptName.empty() ? "[ Unbound ]" : nsc.ScriptName;
            if (ImGui::BeginCombo("##ScriptClassCombo", currentScript.c_str())) {
                for (const auto& name : scriptNames) {
                    bool isSelected = (nsc.ScriptName == name);
                    if (ImGui::Selectable(name.c_str(), isSelected)) {
                        nsc.Bind(name);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            // Drag & Drop Script Target Area
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.28f, 0.42f, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.38f, 0.58f, 0.90f));
            std::string dropBtnLabel = "Drop Script (.hpp) Here: " + (nsc.ScriptName.empty() ? "None" : nsc.ScriptName);
            ImGui::Button(dropBtnLabel.c_str(), ImVec2(-1.0f, 26.0f));
            ImGui::PopStyleColor(2);

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* pathStr = static_cast<const char*>(payload->Data);
                    std::filesystem::path p(pathStr);
                    std::string ext = p.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                    if (ext == ".hpp" || ext == ".h" || ext == ".cpp") {
                        std::string scriptName = p.stem().string();
                        nsc.Bind(scriptName);
                        AXLE_INFO("Bound Script '{0}' to Entity via Inspector drop", scriptName);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Dynamic Reflected Fields
            const auto* meta = ScriptRegistry::GetMetadata(nsc.ScriptName);
            if (meta && !meta->Fields.empty()) {
                ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.95f, 1.0f), "Exposed Fields (%zu):", meta->Fields.size());
                ImGui::Spacing();

                for (const auto& field : meta->Fields) {
                    std::string label = field.Name + "##" + field.Name;
                    switch (field.Type) {
                        case ScriptFieldType::Float: {
                            float val = nsc.FloatFields[field.Name];
                            bool changed = false;
                            if (field.Min < field.Max) {
                                changed = ImGui::SliderFloat(label.c_str(), &val, field.Min, field.Max, "%.2f");
                            } else {
                                changed = ImGui::DragFloat(label.c_str(), &val, 0.1f);
                            }
                            if (changed) {
                                nsc.FloatFields[field.Name] = val;
                                if (nsc.Instance) {
                                    field.SetValue(nsc.Instance, val);
                                }
                            }
                            if (!field.Tooltip.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", field.Tooltip.c_str());
                            }
                            break;
                        }
                        case ScriptFieldType::Int: {
                            int val = nsc.IntFields[field.Name];
                            bool changed = false;
                            if (field.Min < field.Max) {
                                changed = ImGui::SliderInt(label.c_str(), &val, (int)field.Min, (int)field.Max);
                            } else {
                                changed = ImGui::DragInt(label.c_str(), &val, 1.0f);
                            }
                            if (changed) {
                                nsc.IntFields[field.Name] = val;
                                if (nsc.Instance) {
                                    field.SetValue(nsc.Instance, val);
                                }
                            }
                            if (!field.Tooltip.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", field.Tooltip.c_str());
                            }
                            break;
                        }
                        case ScriptFieldType::Bool: {
                            bool val = nsc.BoolFields[field.Name];
                            if (ImGui::Checkbox(label.c_str(), &val)) {
                                nsc.BoolFields[field.Name] = val;
                                if (nsc.Instance) {
                                    field.SetValue(nsc.Instance, val);
                                }
                            }
                            if (!field.Tooltip.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", field.Tooltip.c_str());
                            }
                            break;
                        }
                        case ScriptFieldType::Vec3: {
                            glm::vec3 val = nsc.Vec3Fields[field.Name];
                            if (ImGui::DragFloat3(label.c_str(), glm::value_ptr(val), 0.1f)) {
                                nsc.Vec3Fields[field.Name] = val;
                                if (nsc.Instance) {
                                    field.SetValue(nsc.Instance, val);
                                }
                            }
                            if (!field.Tooltip.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", field.Tooltip.c_str());
                            }
                            break;
                        }
                        case ScriptFieldType::String: {
                            std::string& val = nsc.StringFields[field.Name];
                            char buf[256];
                            std::memset(buf, 0, sizeof(buf));
                            std::strncpy(buf, val.c_str(), sizeof(buf) - 1);
                            if (ImGui::InputText(label.c_str(), buf, sizeof(buf))) {
                                val = buf;
                                if (nsc.Instance) {
                                    field.SetValue(nsc.Instance, val);
                                }
                            }
                            if (!field.Tooltip.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", field.Tooltip.c_str());
                            }
                            break;
                        }
                        case ScriptFieldType::EntityRef: {
                            UUID targetUUID = nsc.EntityFields[field.Name];
                            std::string targetTag = "(None)";
                            if (m_Scene && (uint64_t)targetUUID != 0) {
                                auto targetEnt = m_Scene->GetEntityByUUID(targetUUID);
                                if (targetEnt) {
                                    targetTag = targetEnt.GetName() + " (" + std::to_string((uint64_t)targetUUID) + ")";
                                } else {
                                    targetTag = "Missing Entity (" + std::to_string((uint64_t)targetUUID) + ")";
                                }
                            }

                            ImGui::Columns(2);
                            ImGui::SetColumnWidth(0, 100.0f);
                            ImGui::Text("%s", label.c_str());
                            ImGui::NextColumn();

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
                            ImGui::Button(targetTag.c_str(), ImVec2(-25.0f, 0.0f));
                            ImGui::PopStyleColor();

                            // Drag & drop target accepting SCENE_HIERARCHY_ENTITY
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
                                    uint64_t droppedUUID = *(const uint64_t*)payload->Data;
                                    nsc.EntityFields[field.Name] = droppedUUID;
                                    if (nsc.Instance) {
                                        field.SetValue(nsc.Instance, UUID(droppedUUID));
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("X", ImVec2(20.0f, 0.0f))) {
                                nsc.EntityFields[field.Name] = 0;
                                if (nsc.Instance) {
                                    field.SetValue(nsc.Instance, UUID(0));
                                }
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Clear entity reference");
                            }

                            ImGui::Columns(1);

                            if (!field.Tooltip.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", field.Tooltip.c_str());
                            }
                            break;
                        }
                    }
                }

                ImGui::Spacing();
                if (ImGui::Button("Reset Defaults")) {
                    nsc.Bind(nsc.ScriptName);
                    if (nsc.Instance) {
                        nsc.SyncFieldsToInstance();
                    }
                }
            } else if (!nsc.ScriptName.empty()) {
                ImGui::TextDisabled("No reflected fields registered for '%s'.", nsc.ScriptName.c_str());
            }
        }, /*canRemove=*/ true, [&](NativeScriptComponent& nsc) {
            if (nsc.Instance && nsc.DestroyScript) {
                nsc.DestroyScript(&nsc);
            }
        });

        // Audio Listener Component
        DrawComponent<AudioListenerComponent>("Audio Listener", entity, [&](AudioListenerComponent& alc) {
            ImGui::Checkbox("Active Listener", &alc.Active);
            ImGui::SameLine();
            if (alc.Active) {
                ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.45f, 1.0f), "[ Listening ]");
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[ Muted ]");
            }
            ImGui::TextDisabled("Listens to 3D spatial sounds from active camera/entity perspective.");
        }, /*canRemove=*/ true);

        // Audio Source Component
        DrawComponent<AudioSourceComponent>("Audio Source", entity, [&](AudioSourceComponent& asc) {
            // 1. Audio Asset Slot with Drag & Drop
            ImGui::Text("Audio Clip:");
            std::string displayName = asc.AssetPath.empty() 
                ? "(None - Drop .wav / .mp3 here)" 
                : std::filesystem::path(asc.AssetPath).filename().string();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.28f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.32f, 0.42f, 0.95f));
            ImGui::Button(displayName.c_str(), ImVec2(-32.0f, 26.0f));
            ImGui::PopStyleColor(2);

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* pathStr = static_cast<const char*>(payload->Data);
                    std::filesystem::path p(pathStr);
                    std::string ext = p.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return (char)std::tolower(c); });
                    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
                        asc.AssetPath = p.string();
                        StopAudioPreview();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::Button("X##ClearAudio", ImVec2(24.0f, 26.0f))) {
                asc.AssetPath.clear();
                StopAudioPreview();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear Audio Clip");

            // 2. Interactive Preview Controls (Play, Pause, Stop)
            ImGui::Spacing();
            ImGui::TextDisabled("Editor Preview Controls:");
            bool isPreviewing = m_EditorPreviewAudioSource && m_EditorPreviewAudioSource->IsPlaying() && m_EditorPreviewPath == asc.AssetPath;

            if (isPreviewing) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.4f, 1.0f));
            }
            if (ImGui::Button(isPreviewing ? "Playing..." : "Play Preview", ImVec2(100.0f, 24.0f))) {
                if (!asc.AssetPath.empty()) {
                    if (!m_EditorPreviewAudioSource) {
                        m_EditorPreviewAudioSource = std::make_shared<AudioSource>();
                    }
                    if (m_EditorPreviewPath != asc.AssetPath) {
                        if (m_EditorPreviewAudioSource->Load(asc.AssetPath)) {
                            m_EditorPreviewPath = asc.AssetPath;
                        }
                    }
                    if (m_EditorPreviewAudioSource) {
                        m_EditorPreviewAudioSource->SetVolume(asc.Volume);
                        m_EditorPreviewAudioSource->SetPitch(asc.Pitch);
                        m_EditorPreviewAudioSource->SetLooping(asc.Loop);
                        m_EditorPreviewAudioSource->Play();
                    }
                }
            }
            if (isPreviewing) {
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            if (ImGui::Button("Pause", ImVec2(60.0f, 24.0f))) {
                if (m_EditorPreviewAudioSource) m_EditorPreviewAudioSource->Pause();
            }

            ImGui::SameLine();
            if (ImGui::Button("Stop", ImVec2(60.0f, 24.0f))) {
                StopAudioPreview();
            }

            // 3. Audio Parameters
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::SliderFloat("Volume", &asc.Volume, 0.0f, 1.0f, "%.2f")) {
                if (m_EditorPreviewAudioSource && m_EditorPreviewPath == asc.AssetPath) {
                    m_EditorPreviewAudioSource->SetVolume(asc.Volume);
                }
                if (asc.Source) {
                    asc.Source->SetVolume(asc.Volume);
                }
            }

            if (ImGui::SliderFloat("Pitch", &asc.Pitch, 0.1f, 3.0f, "%.2f")) {
                if (m_EditorPreviewAudioSource && m_EditorPreviewPath == asc.AssetPath) {
                    m_EditorPreviewAudioSource->SetPitch(asc.Pitch);
                }
                if (asc.Source) {
                    asc.Source->SetPitch(asc.Pitch);
                }
            }

            ImGui::Checkbox("Looping", &asc.Loop);
            ImGui::SameLine();
            ImGui::Checkbox("Play On Awake", &asc.PlayOnAwake);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 4. 3D Spatial Audio Settings
            ImGui::Checkbox("3D Spatialized", &asc.Spatialized);
            if (asc.Spatialized) {
                const char* attenuationModels[] = { "Inverse", "Linear", "Exponential" };
                int currentModel = static_cast<int>(asc.Attenuation);
                if (ImGui::Combo("Attenuation Model", &currentModel, attenuationModels, IM_ARRAYSIZE(attenuationModels))) {
                    asc.Attenuation = static_cast<AttenuationModel>(currentModel);
                    if (asc.Source) asc.Source->SetAttenuationModel(asc.Attenuation);
                }

                if (ImGui::DragFloat("Min Distance", &asc.MinDistance, 0.2f, 0.1f, 100.0f, "%.1f m")) {
                    asc.MinDistance = glm::max(0.1f, asc.MinDistance);
                    if (asc.Source) asc.Source->SetMinDistance(asc.MinDistance);
                }
                if (ImGui::DragFloat("Max Distance", &asc.MaxDistance, 1.0f, asc.MinDistance + 0.1f, 1000.0f, "%.1f m")) {
                    asc.MaxDistance = glm::max(asc.MinDistance + 0.1f, asc.MaxDistance);
                    if (asc.Source) asc.Source->SetMaxDistance(asc.MaxDistance);
                }
                if (ImGui::DragFloat("Roll-Off Factor", &asc.RollOff, 0.05f, 0.0f, 10.0f, "%.2f")) {
                    if (asc.Source) asc.Source->SetRollOff(asc.RollOff);
                }

                if (ImGui::CollapsingHeader("Directional Cone Settings")) {
                    if (ImGui::SliderFloat("Inner Angle", &asc.InnerAngle, 0.0f, 360.0f, "%.0f deg") ||
                        ImGui::SliderFloat("Outer Angle", &asc.OuterAngle, 0.0f, 360.0f, "%.0f deg") ||
                        ImGui::SliderFloat("Outer Gain", &asc.OuterGain, 0.0f, 1.0f, "%.2f")) {
                        if (asc.Source) {
                            asc.Source->SetCone(glm::radians(asc.InnerAngle), glm::radians(asc.OuterAngle), asc.OuterGain);
                        }
                    }
                    if (ImGui::DragFloat("Doppler Factor", &asc.DopplerFactor, 0.05f, 0.0f, 5.0f, "%.2f")) {
                        if (asc.Source) asc.Source->SetDopplerFactor(asc.DopplerFactor);
                    }
                }
            }
        }, /*canRemove=*/ true);

        // Vehicle Audio Component
        DrawComponent<VehicleAudioComponent>("Vehicle Audio Synthesizer", entity, [&](VehicleAudioComponent& vac) {
            ImGui::Checkbox("Synthesizer Enabled", &vac.Enabled);

            auto DrawAudioClipSlot = [&](const char* label, std::string& path, const char* hint) {
                ImGui::PushID(label);
                ImGui::Text("%s:", label);
                std::string disp = path.empty() ? std::string("(None - ") + hint + ")" : std::filesystem::path(path).filename().string();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.28f, 0.85f));
                ImGui::Button(disp.c_str(), ImVec2(-32.0f, 24.0f));
                ImGui::PopStyleColor();

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = static_cast<const char*>(payload->Data);
                        std::filesystem::path p(pathStr);
                        std::string ext = p.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return (char)std::tolower(c); });
                        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
                            path = p.string();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::Button("X", ImVec2(24.0f, 24.0f))) {
                    path.clear();
                }
                ImGui::PopID();
            };

            if (ImGui::CollapsingHeader("Audio Clips", ImGuiTreeNodeFlags_DefaultOpen)) {
                DrawAudioClipSlot("Engine Power Clip", vac.EnginePowerClipPath, "Throttle roar .wav/.mp3");
                DrawAudioClipSlot("Engine Coast Clip", vac.EngineCoastClipPath, "Off-throttle overrun");
                DrawAudioClipSlot("Tire Squeal Clip", vac.TireSquealClipPath, "Burnout/drift screech");
                DrawAudioClipSlot("Suspension Thud Clip", vac.SuspensionThudClipPath, "Hard landing thud");
                DrawAudioClipSlot("Collision Impact Clip", vac.CollisionImpactClipPath, "Metal crash impulse");
            }

            if (ImGui::CollapsingHeader("Engine RPM Synthesizer", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Master Engine Volume", &vac.EngineMasterVolume, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Min Pitch (Idle)", &vac.MinEnginePitch, 0.1f, 1.5f, "%.2fx");
                ImGui::SliderFloat("Max Pitch (Redline)", &vac.MaxEnginePitch, 1.5f, 4.0f, "%.2fx");
                ImGui::SliderFloat("Throttle Smoothing", &vac.ThrottleSmoothing, 1.0f, 20.0f, "%.1f");
            }

            if (ImGui::CollapsingHeader("Tire Slip Acoustics")) {
                ImGui::SliderFloat("Tire Squeal Volume", &vac.TireSquealMasterVolume, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Slip Threshold", &vac.SlipThreshold, 0.05f, 0.8f, "%.2f");
                ImGui::SliderFloat("Squeal Fade Speed", &vac.TireSquealFadeSpeed, 2.0f, 30.0f, "%.1f");
            }

            if (ImGui::CollapsingHeader("Suspension & Collision Impacts")) {
                ImGui::SliderFloat("Suspension Volume", &vac.SuspensionMasterVolume, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Suspension Velocity Threshold", &vac.SuspensionThudThreshold, 0.5f, 5.0f, "%.2f m/s");
                ImGui::SliderFloat("Collision Impact Volume", &vac.CollisionMasterVolume, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Collision Decel Threshold", &vac.CollisionDecelThreshold, 5.0f, 50.0f, "%.1f m/s²");
            }

            if (ImGui::CollapsingHeader("3D Spatial Settings")) {
                ImGui::Checkbox("Spatialized", &vac.Spatialized);
                ImGui::DragFloat("Min Distance", &vac.MinDistance, 0.2f, 0.5f, 50.0f, "%.1f m");
                ImGui::DragFloat("Max Distance", &vac.MaxDistance, 1.0f, 10.0f, 500.0f, "%.1f m");
            }

            // Live Runtime Synthesizer Telemetry Monitor
            if (vac.RuntimeAudio && vac.RuntimeAudio->IsPlaying()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.45f, 1.0f), "Live Audio Telemetry:");
                ImGui::Text("Engine Pitch: %.2fx", vac.RuntimeAudio->GetCurrentPitch());
                ImGui::Text("Engine Volume: %.2f", vac.RuntimeAudio->GetCurrentEngineVolume());
                ImGui::Text("Tire Squeal Volume: %.2f", vac.RuntimeAudio->GetCurrentTireVolume());
            }
        }, /*canRemove=*/ true);

        // --- ADD COMPONENT BUTTON ---
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.35f, 0.75f, 1.0f));
        if (ImGui::Button("+ Add Component", ImVec2(availWidth, 30.0f))) {
            ImGui::OpenPopup("AddComponentPopup");
            m_ComponentSearchFilter[0] = '\0';
        }
        ImGui::PopStyleColor(3);

        DrawAddComponentPopup(entity);
    }

    void SceneHierarchyPanel::DrawAddComponentPopup(Entity entity) {
        ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f));
        if (!ImGui::BeginPopup("AddComponentPopup")) {
            return;
        }

        // Search Input Bar
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(0);
        }
        ImGui::InputTextWithHint("##ComponentSearch", "Search components...", m_ComponentSearchFilter, sizeof(m_ComponentSearchFilter));
        ImGui::PopItemWidth();
        ImGui::Separator();

        std::string searchQuery = m_ComponentSearchFilter;
        bool isSearching = !searchQuery.empty();

        struct AvailableItem {
            std::string Category;
            std::string Name;
            std::string Description;
            bool AlreadyAdded = false;
            bool Enabled = true;
            std::function<void()> OnAdd;
        };

        std::vector<AvailableItem> items;

        // --- RENDERING CATEGORY ---
        items.push_back({
            "Rendering", "MeshInstance3D", "3D mesh instance rendering with vertex array or asset model",
            entity.HasComponent<MeshInstance3D>(), true,
            [&]() { entity.AddComponent<MeshInstance3D>(); }
        });
        items.push_back({
            "Rendering", "MaterialComponent", "PBR material properties (Albedo, Normal, Roughness, Metallic)",
            entity.HasComponent<MaterialComponent>(), true,
            [&]() { entity.AddComponent<MaterialComponent>(); }
        });
        items.push_back({
            "Rendering", "DirectionalLightComponent", "Global sunlight with shadow casting and photometric tone mapping",
            entity.HasComponent<DirectionalLightComponent>(), true,
            [&]() { entity.AddComponent<DirectionalLightComponent>(); }
        });
        items.push_back({
            "Rendering", "PointLightComponent", "Omnidirectional local point light source",
            entity.HasComponent<PointLightComponent>(), true,
            [&]() { entity.AddComponent<PointLightComponent>(); }
        });
        items.push_back({
            "Rendering", "SpotLightComponent", "Directional cone spot light source (e.g. vehicle headlights)",
            entity.HasComponent<SpotLightComponent>(), true,
            [&]() { entity.AddComponent<SpotLightComponent>(); }
        });
        items.push_back({
            "Rendering", "SkyboxComponent", "Procedural atmospheric Rayleigh/Mie sky dome",
            entity.HasComponent<SkyboxComponent>(), true,
            [&]() { entity.AddComponent<SkyboxComponent>(); }
        });
        items.push_back({
            "Rendering", "CameraComponent", "Perspective/Orthographic scene camera",
            entity.HasComponent<CameraComponent>(), true,
            [&]() { entity.AddComponent<CameraComponent>(); }
        });

        // --- PHYSICS CATEGORY ---
        items.push_back({
            "Physics", "CollisionShape3D", "Physics collision shape (Box, Sphere, Cylinder, Cone, Plane)",
            entity.HasComponent<CollisionShape3D>(), true,
            [&]() {
                CollisionShapeType shape = CollisionShapeType::Box;
                glm::vec3 halfExtent(0.5f);
                if (entity.HasComponent<TransformComponent>()) {
                    halfExtent = glm::max(entity.GetComponent<TransformComponent>().Scale * 0.5f, glm::vec3(0.01f));
                }
                if (entity.HasComponent<MeshInstance3D>()) {
                    auto& mc = entity.GetComponent<MeshInstance3D>();
                    if (mc.VAO == m_SphereMesh || mc.AssetPath == "builtin://sphere") shape = CollisionShapeType::Sphere;
                    else if (mc.VAO == m_CylinderMesh || mc.AssetPath == "builtin://cylinder") shape = CollisionShapeType::Cylinder;
                    else if (mc.VAO == m_ConeMesh || mc.AssetPath == "builtin://cone") shape = CollisionShapeType::Cone;
                    else if (mc.VAO == m_PlaneMesh || mc.AssetPath == "builtin://plane") shape = CollisionShapeType::Plane;
                }
                entity.AddComponent<CollisionShape3D>(shape, halfExtent);
            }
        });
        items.push_back({
            "Physics", "RigidBody3D", "Dynamic or static Jolt physics rigid body for collision & forces",
            entity.HasComponent<RigidBody3D>(), true,
            [&]() {
                if (m_PhysicsScene) {
                    auto& tc = entity.GetComponent<TransformComponent>();
                    if (!entity.HasComponent<CollisionShape3D>()) {
                        CollisionShapeType shape = CollisionShapeType::Box;
                        glm::vec3 halfExtent = glm::max(tc.Scale * 0.5f, glm::vec3(0.01f));
                        if (entity.HasComponent<MeshInstance3D>()) {
                            auto& mc = entity.GetComponent<MeshInstance3D>();
                            if (mc.VAO == m_SphereMesh || mc.AssetPath == "builtin://sphere") shape = CollisionShapeType::Sphere;
                            else if (mc.VAO == m_CylinderMesh || mc.AssetPath == "builtin://cylinder") shape = CollisionShapeType::Cylinder;
                            else if (mc.VAO == m_ConeMesh || mc.AssetPath == "builtin://cone") shape = CollisionShapeType::Cone;
                            else if (mc.VAO == m_PlaneMesh || mc.AssetPath == "builtin://plane") shape = CollisionShapeType::Plane;
                        }
                        entity.AddComponent<CollisionShape3D>(shape, halfExtent);
                    }

                    auto& cs = entity.GetComponent<CollisionShape3D>();
                    BodyID bodyID;
                    glm::vec3 spawnPos = tc.Translation + tc.Rotation * cs.Offset;
                    bool isDynamic = (cs.ShapeType != CollisionShapeType::Plane);
                    switch (cs.ShapeType) {
                        case CollisionShapeType::Sphere:
                            bodyID = m_PhysicsScene->CreateSphere(spawnPos, cs.Radius, isDynamic, cs.Friction, cs.Restitution);
                            break;
                        case CollisionShapeType::Cylinder:
                            bodyID = m_PhysicsScene->CreateCylinder(spawnPos, cs.HalfHeight, cs.Radius, isDynamic, cs.Friction, cs.Restitution);
                            break;
                        case CollisionShapeType::Cone:
                            bodyID = m_PhysicsScene->CreateCone(spawnPos, cs.HalfHeight, cs.Radius, isDynamic, cs.Friction, cs.Restitution);
                            break;
                        case CollisionShapeType::Plane:
                            bodyID = m_PhysicsScene->CreatePlane(spawnPos, glm::vec3(0, 1, 0), cs.HalfExtent.x, cs.Friction, cs.Restitution);
                            break;
                        case CollisionShapeType::Box:
                        default:
                            bodyID = m_PhysicsScene->CreateBox(spawnPos, cs.HalfExtent, isDynamic, cs.Friction, cs.Restitution);
                            break;
                    }

                    if (!bodyID.IsInvalid()) {
                        m_PhysicsScene->SetBodyPositionAndRotation(bodyID, spawnPos, tc.Rotation);
                    }
                    entity.AddComponent<RigidBody3D>(bodyID, isDynamic);
                }
            }
        });
        items.push_back({
            "Physics", "VehicleComponent", "Raycast/physics vehicle simulation controller",
            entity.HasComponent<VehicleComponent>(), true,
            [&]() {
                entity.AddComponent<VehicleComponent>();
                if (!entity.HasComponent<CollisionShape3D>()) {
                    glm::vec3 halfExtent{ 0.9f, 0.25f, 2.0f };
                    if (entity.HasComponent<TransformComponent>()) {
                        halfExtent = glm::max(entity.GetComponent<TransformComponent>().Scale * 0.5f, glm::vec3(0.2f));
                    }
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, halfExtent);
                }
                if (!entity.HasComponent<RigidBody3D>()) {
                    entity.AddComponent<RigidBody3D>();
                }
            }
        });
        items.push_back({
            "Physics", "VehicleWheel3D", "Raycast wheel dynamics controller for vehicle chassis",
            entity.HasComponent<WheelComponent>(), true,
            [&]() { entity.AddComponent<WheelComponent>(); }
        });

        // --- SCRIPTS CATEGORY ---
        items.push_back({
            "Scripts", "Native Script Component", "Generic C++ ScriptableEntity script container",
            entity.HasComponent<NativeScriptComponent>(), true,
            [&]() {
                auto& nsc = entity.AddComponent<NativeScriptComponent>();
                const auto& registered = ScriptRegistry::GetScripts();
                if (!registered.empty()) {
                    nsc.Bind(registered.begin()->first);
                }
            }
        });

        const auto& registeredScripts = ScriptRegistry::GetScripts();
        for (const auto& [scriptName, scriptMeta] : registeredScripts) {
            bool hasThisScript = entity.HasComponent<NativeScriptComponent>() &&
                                 entity.GetComponent<NativeScriptComponent>().ScriptName == scriptName;
            items.push_back({
                "Scripts", "Script: " + scriptName, "Directly attach registered script '" + scriptName + "'",
                hasThisScript, true,
                [&, sName = scriptName]() {
                    if (!entity.HasComponent<NativeScriptComponent>()) {
                        auto& nsc = entity.AddComponent<NativeScriptComponent>(sName);
                        nsc.Bind(sName);
                    } else {
                        auto& nsc = entity.GetComponent<NativeScriptComponent>();
                        nsc.Bind(sName);
                    }
                }
            });
        }

        // --- AUDIO / VFX CATEGORY ---
        items.push_back({
            "Audio/VFX", "AudioSourceComponent", "Positional 3D / 2D audio emitter with attenuation",
            entity.HasComponent<AudioSourceComponent>(), true,
            [&]() { entity.AddComponent<AudioSourceComponent>(); }
        });
        items.push_back({
            "Audio/VFX", "AudioListenerComponent", "3D microphone listener (attach to primary Camera)",
            entity.HasComponent<AudioListenerComponent>(), true,
            [&]() { entity.AddComponent<AudioListenerComponent>(true); }
        });
        items.push_back({
            "Audio/VFX", "VehicleAudioComponent", "Granular engine RPM synthesizer, slip acoustics & impact thuds",
            entity.HasComponent<VehicleAudioComponent>(), true,
            [&]() { entity.AddComponent<VehicleAudioComponent>(); }
        });
        items.push_back({
            "Audio/VFX", "ParticleEmitter3D (Phase 20)", "Dynamic particle system (Dust, smoke, sparks)",
            false, false, nullptr
        });

        // Rendering logic:
        if (isSearching) {
            int matchCount = 0;
            for (const auto& item : items) {
                std::string fullSearchString = item.Category + " " + item.Name + " " + item.Description;
                if (!ContainsIgnoreCase(fullSearchString, searchQuery)) {
                    continue;
                }
                matchCount++;

                std::string displayLabel = item.Name + " [" + item.Category + "]";
                if (item.AlreadyAdded) {
                    displayLabel += " (Already added)";
                } else if (!item.Enabled) {
                    displayLabel += " (Coming Soon)";
                }

                bool canClick = (!item.AlreadyAdded && item.Enabled && item.OnAdd != nullptr);
                if (!canClick) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::MenuItem(displayLabel.c_str())) {
                    if (item.OnAdd) item.OnAdd();
                    m_ComponentSearchFilter[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                if (!canClick) {
                    ImGui::EndDisabled();
                }
                if (!item.Description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", item.Description.c_str());
                }
            }

            if (matchCount == 0) {
                ImGui::TextDisabled("No components match '%s'", searchQuery.c_str());
            }
        } else {
            const std::vector<std::string> categories = { "Rendering", "Physics", "Scripts", "Audio/VFX" };
            for (const auto& cat : categories) {
                if (ImGui::BeginMenu(cat.c_str())) {
                    for (const auto& item : items) {
                        if (item.Category != cat) continue;

                        std::string displayLabel = item.Name;
                        if (item.AlreadyAdded) {
                            displayLabel += " (Already added)";
                        } else if (!item.Enabled) {
                            displayLabel += " (Coming Soon)";
                        }

                        bool canClick = (!item.AlreadyAdded && item.Enabled && item.OnAdd != nullptr);
                        if (!canClick) {
                            ImGui::BeginDisabled();
                        }
                        if (ImGui::MenuItem(displayLabel.c_str())) {
                            if (item.OnAdd) item.OnAdd();
                            m_ComponentSearchFilter[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                        if (!canClick) {
                            ImGui::EndDisabled();
                        }
                        if (!item.Description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("%s", item.Description.c_str());
                        }
                    }
                    ImGui::EndMenu();
                }
            }
        }

        ImGui::EndPopup();
    }

    void SceneHierarchyPanel::DrawAssetProperties(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            ImGui::Spacing();
            ImGui::TextDisabled("Selected asset no longer exists.");
            return;
        }

        std::filesystem::path relPath = path;
        try {
            relPath = std::filesystem::relative(path, FileSystem::GetAssetDirectory());
        } catch (...) {}

        std::string filenameStr = path.filename().string();
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        uintmax_t fileSize = 0;
        try {
            fileSize = std::filesystem::file_size(path);
        } catch (...) {}

        std::string sizeStr;
        if (fileSize < 1024) sizeStr = std::to_string(fileSize) + " B";
        else if (fileSize < 1024 * 1024) sizeStr = std::to_string(fileSize / 1024) + " KB";
        else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f MB", (double)fileSize / (1024.0 * 1024.0));
            sizeStr = buf;
        }

        bool isAssembly = (ext == ".axle");
        bool isMaterial = (ext == ".axlemat" || ext == ".amat" || ext == ".mat");
        bool isMesh = (ext == ".axlemesh" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx");
        bool isTexture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr");

        // Top Asset Header Card
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.15f, 0.20f, 0.85f));
        ImGui::BeginChild("AssetHeaderCard", ImVec2(0.0f, 66.0f), true);
        {
            if (isAssembly) {
                ImGui::TextColored(ImVec4(0.25f, 0.90f, 0.55f, 1.0f), "[MECHANICAL ASSEMBLY PREFAB]");
            } else if (isMaterial) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.20f, 1.0f), "[PBR MATERIAL ASSET]");
            } else if (isMesh) {
                ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "[3D MESH PACKAGE]");
            } else if (isTexture) {
                ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.50f, 1.0f), "[2D TEXTURE ASSET]");
            } else if (ext == ".axlescene" || ext == ".scene") {
                ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.30f, 1.0f), "[SCENE ASSET]");
            } else if (ext == ".physmat") {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f), "[PHYSICS MATERIAL ASSET]");
            } else {
                ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.85f, 1.0f), "[ASSET FILE]");
            }

            ImGui::TextUnformatted(filenameStr.c_str());
            ImGui::TextDisabled("%s | %s", relPath.string().c_str(), sizeStr.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // 1. Material Asset: Render 3D preview sphere turntable + interactive PBR controls
        if (isMaterial) {
            if (m_AssetPreviewPanel) {
                if (m_AssetPreviewPanel->GetCurrentAsset() != path) {
                    m_AssetPreviewPanel->SetAsset(path);
                }
                if (m_AssetPreviewPanel->HasAsset()) {
                    m_AssetPreviewPanel->RenderPreviewViewport();
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                }
            }
            DrawMaterialAssetProperties(path);
            return;
        }

        // 2. 2D Texture Asset: Render image preview with resolution metadata
        if (isTexture) {
            auto tex = Texture2D::Create(path.string());
            if (tex && tex->IsLoaded()) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.11f, 0.14f, 1.0f));
                ImGui::BeginChild("TexturePreviewViewport", ImVec2(0.0f, 220.0f), true);
                {
                    float availW = ImGui::GetContentRegionAvail().x;
                    float availH = 200.0f;
                    float texW = (float)tex->GetWidth();
                    float texH = (float)tex->GetHeight();
                    float scale = std::min(availW / texW, availH / texH);
                    float dispW = texW * scale;
                    float dispH = texH * scale;

                    float padX = (availW - dispW) * 0.5f;
                    float padY = (availH - dispH) * 0.5f;
                    if (padX > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
                    if (padY > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padY);

                    ImGui::Image((ImTextureID)(intptr_t)tex->GetRendererID(), ImVec2(dispW, dispH), ImVec2(0, 1), ImVec2(1, 0));
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::CollapsingHeader("Texture Metadata", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Columns(2, "TexMetaCols", false);
                    ImGui::SetColumnWidth(0, 120.0f);

                    ImGui::TextDisabled("Dimensions:");
                    ImGui::NextColumn();
                    ImGui::Text("%u x %u px", tex->GetWidth(), tex->GetHeight());
                    ImGui::NextColumn();

                    ImGui::TextDisabled("Aspect Ratio:");
                    ImGui::NextColumn();
                    ImGui::Text("%.2f:1", (float)tex->GetWidth() / (float)std::max(1u, tex->GetHeight()));
                    ImGui::NextColumn();

                    ImGui::TextDisabled("Renderer ID:");
                    ImGui::NextColumn();
                    ImGui::Text("%u", tex->GetRendererID());
                    ImGui::NextColumn();

                    ImGui::Columns(1);
                }
            }
            return;
        }

        // 3. Physics Material: Interactive slider controls
        if (ext == ".physmat") {
            PhysicsMaterial physMat;
            if (PhysicsMaterial::Load(path, physMat)) {
                bool changed = false;
                if (ImGui::CollapsingHeader("Physics Surface Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::SliderFloat("Friction (Grip)", &physMat.Friction, 0.0f, 1.0f, "%.2f")) changed = true;
                    if (ImGui::SliderFloat("Restitution (Bounciness)", &physMat.Restitution, 0.0f, 1.0f, "%.2f")) changed = true;
                    if (ImGui::DragFloat("Density (kg/m3)", &physMat.Density, 10.0f, 1.0f, 20000.0f, "%.1f")) changed = true;
                }
                if (changed) {
                    PhysicsMaterial::Save(path, physMat);
                }
            }
            return;
        }

        // 4. Embedded Interactive 3D Asset Preview Viewport (Mesh & Assembly)
        if (m_AssetPreviewPanel && (isAssembly || isMesh)) {
            if (m_AssetPreviewPanel->GetCurrentAsset() != path) {
                m_AssetPreviewPanel->SetAsset(path);
            }
            if (m_AssetPreviewPanel->HasAsset()) {
                m_AssetPreviewPanel->RenderPreviewViewport();
                ImGui::Spacing();
            }
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (isAssembly && m_AssetPreviewPanel && m_AssetPreviewPanel->HasAsset() && m_AssetPreviewPanel->IsAssembly()) {
            const auto& asmData = m_AssetPreviewPanel->GetAssemblyData();

            size_t totalParts = 0;
            float totalMass = 0.0f;

            std::function<void(const AssemblyNode&)> countRecursive = [&](const AssemblyNode& node) {
                totalParts++;
                if (node.HasRigidBody) {
                    totalMass += node.RigidBody.Mass;
                }
                for (const auto& child : node.Children) {
                    countRecursive(child);
                }
            };
            countRecursive(asmData.RootNode);

            if (ImGui::CollapsingHeader("Assembly Specifications", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Columns(2, "AsmSpecsColumns", false);
                ImGui::SetColumnWidth(0, 130.0f);

                ImGui::TextDisabled("Assembly Name:");
                ImGui::NextColumn();
                ImGui::Text("%s", asmData.Name.empty() ? filenameStr.c_str() : asmData.Name.c_str());
                ImGui::NextColumn();

                ImGui::TextDisabled("Root Part:");
                ImGui::NextColumn();
                ImGui::Text("%s", asmData.RootNode.Tag.c_str());
                ImGui::NextColumn();

                ImGui::TextDisabled("Total Parts:");
                ImGui::NextColumn();
                ImGui::Text("%zu parts", totalParts);
                ImGui::NextColumn();

                ImGui::TextDisabled("Total Mass:");
                ImGui::NextColumn();
                ImGui::Text("%.1f kg", totalMass);
                ImGui::NextColumn();

                ImGui::TextDisabled("Status:");
                ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.55f, 1.0f), "Valid Prefab");
                ImGui::NextColumn();

                ImGui::Columns(1);
            }

            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Part Structure Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) {
                std::function<void(const AssemblyNode&, int)> drawPartTree = [&](const AssemblyNode& node, int depth) {
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (node.Children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
                    else flags |= ImGuiTreeNodeFlags_DefaultOpen;

                    ImGui::PushID(&node);
                    bool opened = ImGui::TreeNodeEx("PartNode", flags, "%s", node.Tag.c_str());
                    if (opened) {
                        if (node.HasRigidBody) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(%.1fkg)", node.RigidBody.Mass);
                        }
                        if (node.HasMesh && !node.Mesh.AssetPath.empty()) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("[%s]", node.Mesh.AssetPath.c_str());
                        }
                        for (const auto& child : node.Children) {
                            drawPartTree(child, depth + 1);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                };
                drawPartTree(asmData.RootNode, 0);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Quick Actions
            float availW = ImGui::GetContentRegionAvail().x;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.68f, 0.38f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.45f, 0.22f, 1.0f));
            if (ImGui::Button("+ Spawn into Active Scene", ImVec2(availW, 30.0f))) {
                if (m_OnSpawnAssetCallback) {
                    m_OnSpawnAssetCallback(path);
                } else if (m_Scene) {
                    SceneSerializerContext ctx;
                    ctx.CubeMesh = m_CubeMesh;
                    ctx.SphereMesh = m_SphereMesh;
                    ctx.CylinderMesh = m_CylinderMesh;
                    ctx.ConeMesh = m_ConeMesh;
                    ctx.PlaneMesh = m_PlaneMesh;
                    auto ent = m_Scene->InstantiateAssembly(path, m_PhysicsScene, glm::vec3(0.0f, 0.0f, 0.0f), &ctx);
                    if (ent) m_SelectedEntity = ent;
                }
            }
            ImGui::PopStyleColor(3);

            ImGui::Spacing();
            if (ImGui::Button("Open in Assembly Editor", ImVec2(availW, 26.0f))) {
                if (m_OnOpenAssemblyEditorCallback) {
                    m_OnOpenAssemblyEditorCallback(path);
                }
            }
        } else if (isMesh && m_AssetPreviewPanel && m_AssetPreviewPanel->HasAsset()) {
            const auto& pkg = m_AssetPreviewPanel->GetPackageData();
            size_t totalVertices = m_AssetPreviewPanel->GetTotalVertices();
            size_t totalTriangles = m_AssetPreviewPanel->GetTotalTriangles();
            glm::vec3 boundsSize = m_AssetPreviewPanel->GetBoundsSize();

            if (ImGui::CollapsingHeader("Mesh Geometry & Bounds", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Columns(2, "MeshGeomColumns", false);
                ImGui::SetColumnWidth(0, 130.0f);

                ImGui::TextDisabled("Format:");
                ImGui::NextColumn();
                ImGui::Text("%s", pkg.IsPackage ? "Multi-Part Package" : "Single Mesh Asset");
                ImGui::NextColumn();

                ImGui::TextDisabled("Submeshes:");
                ImGui::NextColumn();
                ImGui::Text("%zu submesh%s", pkg.Parts.size(), pkg.Parts.size() == 1 ? "" : "es");
                ImGui::NextColumn();

                ImGui::TextDisabled("Vertices:");
                ImGui::NextColumn();
                ImGui::Text("%zu", totalVertices);
                ImGui::NextColumn();

                ImGui::TextDisabled("Triangles:");
                ImGui::NextColumn();
                ImGui::Text("%zu", totalTriangles);
                ImGui::NextColumn();

                ImGui::TextDisabled("Bounding Size:");
                ImGui::NextColumn();
                ImGui::Text("%.2fm x %.2fm x %.2fm", boundsSize.x, boundsSize.y, boundsSize.z);
                ImGui::NextColumn();

                ImGui::Columns(1);
            }

            if (!pkg.Parts.empty()) {
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Submesh Breakdown", ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (size_t i = 0; i < pkg.Parts.size(); ++i) {
                        const auto& part = pkg.Parts[i];
                        ImGui::BulletText("%s (%zu verts, %zu tris)", part.Name.c_str(), 
                            part.LoadedMesh.SourceData.Vertices.size(),
                            part.LoadedMesh.SourceData.Indices.size() / 3);
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float availW = ImGui::GetContentRegionAvail().x;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.78f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.58f, 0.90f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.40f, 0.68f, 1.0f));
            if (ImGui::Button("+ Spawn into Active Scene", ImVec2(availW, 30.0f))) {
                if (m_OnSpawnAssetCallback) {
                    m_OnSpawnAssetCallback(path);
                }
            }
            ImGui::PopStyleColor(3);
        } else if (isAssembly || isMesh) {
            ImGui::TextDisabled("Loading 3D asset metadata...");
        } else {
            ImGui::TextDisabled("File Type: %s", ext.c_str());
            ImGui::TextDisabled("File Size: %s", sizeStr.c_str());
            ImGui::Spacing();
            ImGui::TextWrapped("Click any entity in Scene Hierarchy to inspect its components.");
        }
    }

    bool SceneHierarchyPanel::SaveMaterialAsset(const std::filesystem::path& path, const MaterialComponent& mat) {
        std::ofstream fout(path);
        if (!fout.is_open()) return false;
        fout << "MaterialAsset:\n";
        fout << "  AlbedoColor: [ " << mat.AlbedoColor.r << " , " << mat.AlbedoColor.g << " , " << mat.AlbedoColor.b << " , " << mat.AlbedoColor.a << " ]\n";
        fout << "  AlbedoPath: " << mat.AlbedoPath << "\n";
        fout << "  UseAlbedoMap: " << (mat.UseAlbedoMap ? "true" : "false") << "\n";
        fout << "  NormalPath: " << mat.NormalPath << "\n";
        fout << "  UseNormalMap: " << (mat.UseNormalMap ? "true" : "false") << "\n";
        fout << "  NormalStrength: " << mat.NormalStrength << "\n";
        fout << "  Roughness: " << mat.Roughness << "\n";
        fout << "  RoughnessPath: " << mat.RoughnessPath << "\n";
        fout << "  UseRoughnessMap: " << (mat.UseRoughnessMap ? "true" : "false") << "\n";
        fout << "  Metallic: " << mat.Metallic << "\n";
        fout << "  MetallicPath: " << mat.MetallicPath << "\n";
        fout << "  UseMetallicMap: " << (mat.UseMetallicMap ? "true" : "false") << "\n";
        fout << "  EmissionColor: [ " << mat.EmissionColor.r << " , " << mat.EmissionColor.g << " , " << mat.EmissionColor.b << " ]\n";
        fout << "  EmissionIntensity: " << mat.EmissionIntensity << "\n";
        fout << "  UVScale: [ " << mat.UVScale.x << " , " << mat.UVScale.y << " ]\n";
        fout.close();
        return true;
    }

    bool SceneHierarchyPanel::LoadMaterialAsset(const std::filesystem::path& path, MaterialComponent& mat) {
        std::ifstream fin(path);
        if (!fin.is_open()) return false;
        std::string line;
        while (std::getline(fin, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            std::string trimmed = line.substr(start);

            if (trimmed.rfind("AlbedoColor:", 0) == 0) {
                float r = 1, g = 1, b = 1, a = 1;
                sscanf(trimmed.c_str(), "AlbedoColor: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                mat.AlbedoColor = glm::vec4(r, g, b, a);
            } else if (trimmed.rfind("AlbedoPath:", 0) == 0) {
                std::string p = trimmed.substr(11);
                size_t pStart = p.find_first_not_of(" \t");
                std::string pathStr = (pStart != std::string::npos ? p.substr(pStart) : "");
                mat.AlbedoPath = pathStr;
                if (!pathStr.empty() && Window::HasActiveContext()) {
                    mat.SetAlbedo(pathStr);
                }
            } else if (trimmed.rfind("UseAlbedoMap:", 0) == 0) {
                mat.UseAlbedoMap = (trimmed.find("true") != std::string::npos);
            } else if (trimmed.rfind("NormalPath:", 0) == 0) {
                std::string p = trimmed.substr(11);
                size_t pStart = p.find_first_not_of(" \t");
                std::string pathStr = (pStart != std::string::npos ? p.substr(pStart) : "");
                mat.NormalPath = pathStr;
                if (!pathStr.empty() && Window::HasActiveContext()) {
                    mat.SetNormal(pathStr);
                }
            } else if (trimmed.rfind("UseNormalMap:", 0) == 0) {
                mat.UseNormalMap = (trimmed.find("true") != std::string::npos);
            } else if (trimmed.rfind("NormalStrength:", 0) == 0) {
                mat.NormalStrength = std::stof(trimmed.substr(15));
            } else if (trimmed.rfind("Roughness:", 0) == 0) {
                mat.Roughness = std::stof(trimmed.substr(10));
            } else if (trimmed.rfind("RoughnessPath:", 0) == 0) {
                std::string p = trimmed.substr(14);
                size_t pStart = p.find_first_not_of(" \t");
                std::string pathStr = (pStart != std::string::npos ? p.substr(pStart) : "");
                mat.RoughnessPath = pathStr;
                if (!pathStr.empty() && Window::HasActiveContext()) {
                    mat.SetRoughnessMap(pathStr);
                }
            } else if (trimmed.rfind("UseRoughnessMap:", 0) == 0) {
                mat.UseRoughnessMap = (trimmed.find("true") != std::string::npos);
            } else if (trimmed.rfind("Metallic:", 0) == 0) {
                mat.Metallic = std::stof(trimmed.substr(9));
            } else if (trimmed.rfind("MetallicPath:", 0) == 0) {
                std::string p = trimmed.substr(13);
                size_t pStart = p.find_first_not_of(" \t");
                std::string pathStr = (pStart != std::string::npos ? p.substr(pStart) : "");
                mat.MetallicPath = pathStr;
                if (!pathStr.empty() && Window::HasActiveContext()) {
                    mat.SetMetallicMap(pathStr);
                }
            } else if (trimmed.rfind("UseMetallicMap:", 0) == 0) {
                mat.UseMetallicMap = (trimmed.find("true") != std::string::npos);
            } else if (trimmed.rfind("EmissionColor:", 0) == 0) {
                float r = 0, g = 0, b = 0;
                sscanf(trimmed.c_str(), "EmissionColor: [ %f , %f , %f ]", &r, &g, &b);
                mat.EmissionColor = glm::vec3(r, g, b);
            } else if (trimmed.rfind("EmissionIntensity:", 0) == 0) {
                mat.EmissionIntensity = std::stof(trimmed.substr(18));
            } else if (trimmed.rfind("UVScale:", 0) == 0) {
                float x = 1, y = 1;
                sscanf(trimmed.c_str(), "UVScale: [ %f , %f ]", &x, &y);
                mat.UVScale = glm::vec2(x, y);
            }
        }
        return true;
    }

    void SceneHierarchyPanel::DrawMaterialAssetProperties(const std::filesystem::path& path) {
        if (m_LoadedMaterialPath != path || !m_MaterialAssetLoaded) {
            m_ActiveMaterialAsset = MaterialComponent();
            LoadMaterialAsset(path, m_ActiveMaterialAsset);
            m_LoadedMaterialPath = path;
            m_MaterialAssetLoaded = true;
        }

        bool changed = false;

        // 1. Material Presets Card (12 Professional Presets)
        if (ImGui::CollapsingHeader("Material Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
            float btnW = (ImGui::GetContentRegionAvail().x - 12.0f) / 4.0f;
            
            // Row 1: Metals & Minerals
            if (ImGui::Button("Gold", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetGold(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Aluminum", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetAluminum(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Obsidian", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetObsidian(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Chrome", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetChrome(); changed = true; }

            // Row 2: Automotive & Industrial
            if (ImGui::Button("Rubber", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetRubber(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Plastic", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetPlastic(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Copper", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetCopper(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Cast Iron", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetCastIron(); changed = true; }

            // Row 3: Car Paint & Lighting
            if (ImGui::Button("Paint Red", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetCarPaintRed(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Paint Blue", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetCarPaintBlue(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Paint Silver", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetCarPaintSilver(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Headlight", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetHeadlightLED(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Taillight", ImVec2(btnW, 26.0f))) { m_ActiveMaterialAsset.SetPresetTaillightLED(); changed = true; }
        }

        ImGui::Spacing();

        // 2. Interactive PBR Surface Properties
        if (ImGui::CollapsingHeader("PBR Surface Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Albedo Color
            if (ImGui::ColorEdit4("Albedo Color", glm::value_ptr(m_ActiveMaterialAsset.AlbedoColor))) {
                changed = true;
            }

            // Albedo Texture Slot
            ImGui::TextDisabled("Albedo Map");
            if (m_ActiveMaterialAsset.UseAlbedoMap && m_ActiveMaterialAsset.AlbedoMap && m_ActiveMaterialAsset.AlbedoMap->IsLoaded()) {
                ImGui::Image((ImTextureID)(intptr_t)m_ActiveMaterialAsset.AlbedoMap->GetRendererID(), ImVec2(24.0f, 24.0f), ImVec2(0, 1), ImVec2(1, 0));
                ImGui::SameLine();
            }
            char albedoBuf[256];
            std::memset(albedoBuf, 0, sizeof(albedoBuf));
            std::strncpy(albedoBuf, m_ActiveMaterialAsset.AlbedoPath.c_str(), sizeof(albedoBuf) - 1);
            float clearBtnW = 24.0f;
            float inputW = ImGui::GetContentRegionAvail().x - clearBtnW - 6.0f;
            ImGui::SetNextItemWidth(inputW);
            if (ImGui::InputText("##AlbedoTexture", albedoBuf, sizeof(albedoBuf))) {
                m_ActiveMaterialAsset.SetAlbedo(albedoBuf);
                changed = true;
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    std::string droppedPath = static_cast<const char*>(payload->Data);
                    m_ActiveMaterialAsset.SetAlbedo(droppedPath);
                    changed = true;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::Button("X##ClearAlbedo", ImVec2(clearBtnW, 0.0f))) {
                m_ActiveMaterialAsset.SetAlbedo("");
                changed = true;
            }

            ImGui::Separator();

            // Roughness
            if (ImGui::SliderFloat("Roughness", &m_ActiveMaterialAsset.Roughness, 0.0f, 1.0f, "%.2f")) {
                changed = true;
            }
            ImGui::TextDisabled("Roughness Map");
            if (m_ActiveMaterialAsset.UseRoughnessMap && m_ActiveMaterialAsset.RoughnessMap && m_ActiveMaterialAsset.RoughnessMap->IsLoaded()) {
                ImGui::Image((ImTextureID)(intptr_t)m_ActiveMaterialAsset.RoughnessMap->GetRendererID(), ImVec2(24.0f, 24.0f), ImVec2(0, 1), ImVec2(1, 0));
                ImGui::SameLine();
            }
            char roughBuf[256];
            std::memset(roughBuf, 0, sizeof(roughBuf));
            std::strncpy(roughBuf, m_ActiveMaterialAsset.RoughnessPath.c_str(), sizeof(roughBuf) - 1);
            ImGui::SetNextItemWidth(inputW);
            if (ImGui::InputText("##RoughnessMap", roughBuf, sizeof(roughBuf))) {
                m_ActiveMaterialAsset.SetRoughnessMap(roughBuf);
                changed = true;
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    std::string droppedPath = static_cast<const char*>(payload->Data);
                    m_ActiveMaterialAsset.SetRoughnessMap(droppedPath);
                    changed = true;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::Button("X##ClearRoughness", ImVec2(clearBtnW, 0.0f))) {
                m_ActiveMaterialAsset.SetRoughnessMap("");
                changed = true;
            }

            ImGui::Separator();

            // Metallic
            if (ImGui::SliderFloat("Metallic", &m_ActiveMaterialAsset.Metallic, 0.0f, 1.0f, "%.2f")) {
                changed = true;
            }
            ImGui::TextDisabled("Metallic Map");
            if (m_ActiveMaterialAsset.UseMetallicMap && m_ActiveMaterialAsset.MetallicMap && m_ActiveMaterialAsset.MetallicMap->IsLoaded()) {
                ImGui::Image((ImTextureID)(intptr_t)m_ActiveMaterialAsset.MetallicMap->GetRendererID(), ImVec2(24.0f, 24.0f), ImVec2(0, 1), ImVec2(1, 0));
                ImGui::SameLine();
            }
            char metalBuf[256];
            std::memset(metalBuf, 0, sizeof(metalBuf));
            std::strncpy(metalBuf, m_ActiveMaterialAsset.MetallicPath.c_str(), sizeof(metalBuf) - 1);
            ImGui::SetNextItemWidth(inputW);
            if (ImGui::InputText("##MetallicMap", metalBuf, sizeof(metalBuf))) {
                m_ActiveMaterialAsset.SetMetallicMap(metalBuf);
                changed = true;
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    std::string droppedPath = static_cast<const char*>(payload->Data);
                    m_ActiveMaterialAsset.SetMetallicMap(droppedPath);
                    changed = true;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::Button("X##ClearMetallic", ImVec2(clearBtnW, 0.0f))) {
                m_ActiveMaterialAsset.SetMetallicMap("");
                changed = true;
            }

            ImGui::Separator();

            // Normal Map
            if (ImGui::SliderFloat("Normal Strength", &m_ActiveMaterialAsset.NormalStrength, 0.0f, 5.0f, "%.2f")) {
                changed = true;
            }
            ImGui::TextDisabled("Normal Map");
            if (m_ActiveMaterialAsset.UseNormalMap && m_ActiveMaterialAsset.NormalMap && m_ActiveMaterialAsset.NormalMap->IsLoaded()) {
                ImGui::Image((ImTextureID)(intptr_t)m_ActiveMaterialAsset.NormalMap->GetRendererID(), ImVec2(24.0f, 24.0f), ImVec2(0, 1), ImVec2(1, 0));
                ImGui::SameLine();
            }
            char normalBuf[256];
            std::memset(normalBuf, 0, sizeof(normalBuf));
            std::strncpy(normalBuf, m_ActiveMaterialAsset.NormalPath.c_str(), sizeof(normalBuf) - 1);
            ImGui::SetNextItemWidth(inputW);
            if (ImGui::InputText("##NormalMap", normalBuf, sizeof(normalBuf))) {
                m_ActiveMaterialAsset.SetNormal(normalBuf);
                changed = true;
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    std::string droppedPath = static_cast<const char*>(payload->Data);
                    m_ActiveMaterialAsset.SetNormal(droppedPath);
                    changed = true;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::Button("X##ClearNormal", ImVec2(clearBtnW, 0.0f))) {
                m_ActiveMaterialAsset.SetNormal("");
                changed = true;
            }

            ImGui::Separator();

            // Emission
            if (ImGui::ColorEdit3("Emission Color", glm::value_ptr(m_ActiveMaterialAsset.EmissionColor))) {
                changed = true;
            }
            if (ImGui::DragFloat("Emission Intensity", &m_ActiveMaterialAsset.EmissionIntensity, 0.1f, 0.0f, 50.0f, "%.1f")) {
                changed = true;
            }

            ImGui::Separator();

            // UV Tiling
            if (ImGui::DragFloat2("UV Scale", glm::value_ptr(m_ActiveMaterialAsset.UVScale), 0.1f, 0.01f, 100.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SmallButton("1x1")) { m_ActiveMaterialAsset.UVScale = glm::vec2(1.0f); changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("2x2")) { m_ActiveMaterialAsset.UVScale = glm::vec2(2.0f); changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("4x4")) { m_ActiveMaterialAsset.UVScale = glm::vec2(4.0f); changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("8x8")) { m_ActiveMaterialAsset.UVScale = glm::vec2(8.0f); changed = true; }
        }

        // 3. Live 3D Preview Synchronization
        if (changed && m_AssetPreviewPanel) {
            m_AssetPreviewPanel->UpdateMaterialPreview(m_ActiveMaterialAsset);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float availW = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.68f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.45f, 0.25f, 1.0f));
        if (ImGui::Button("Save Material Asset", ImVec2(availW, 30.0f)) || changed) {
            SaveMaterialAsset(path, m_ActiveMaterialAsset);
            AssetManager::ReloadMaterial(path);
        }
        ImGui::PopStyleColor(3);

        if (m_SelectedEntity && m_Scene && m_Scene->GetRegistry().valid((entt::entity)m_SelectedEntity)) {
            ImGui::Spacing();
            if (ImGui::Button("Apply to Selected Entity", ImVec2(availW, 26.0f))) {
                UUID handle = AssetManager::GetAssetUUID(path);
                auto matAsset = AssetManager::GetMaterial(path);
                if (m_SelectedEntity.HasComponent<MaterialComponent>()) {
                    auto& mc = m_SelectedEntity.GetComponent<MaterialComponent>();
                    if (matAsset) {
                        mc.SetAsset(handle, matAsset);
                    } else {
                        mc = m_ActiveMaterialAsset;
                    }
                } else {
                    auto& mc = m_SelectedEntity.AddComponent<MaterialComponent>();
                    if (matAsset) {
                        mc.SetAsset(handle, matAsset);
                    } else {
                        mc = m_ActiveMaterialAsset;
                    }
                }
            }
        }
    }

}
