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
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <Jolt/Physics/Body/BodyLock.h>
#include <cstring>
#include <filesystem>

#include <Axle/VertexArray.hpp>
#include <algorithm>
#include <cctype>

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

    static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f) {
        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text("%s", label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

        float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

        // X Control
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.15f, 0.15f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.95f, 0.25f, 0.25f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.7f, 0.1f, 0.1f, 1.0f });
        if (ImGui::Button("X", buttonSize)) {
            values.x = resetValue;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Y Control
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.15f, 0.7f, 0.2f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.25f, 0.85f, 0.3f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.6f, 0.15f, 1.0f });
        if (ImGui::Button("Y", buttonSize)) {
            values.y = resetValue;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Z Control
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.15f, 0.35f, 0.85f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.25f, 0.45f, 0.95f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.75f, 1.0f });
        if (ImGui::Button("Z", buttonSize)) {
            values.z = resetValue;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();
        ImGui::Columns(1);
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
        m_SelectedEntity = entity;
        m_SelectedAsset.clear();
        if (m_AssetPreviewPanel) {
            m_AssetPreviewPanel->SetAsset({});
        }
    }

    void SceneHierarchyPanel::SetSelectedAsset(const std::filesystem::path& assetPath) {
        m_SelectedAsset = assetPath;
        m_SelectedEntity = {};
        if (m_AssetPreviewPanel) {
            m_AssetPreviewPanel->SetAsset(assetPath);
        }
    }

    void SceneHierarchyPanel::ClearSelectedAsset() {
        m_SelectedAsset.clear();
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

    Entity SceneHierarchyPanel::CreatePrimitive(int primitiveType, const glm::vec3& position) {
        if (!m_Scene) return {};

        Entity entity{};
        switch (primitiveType) {
            case 0: { // 3D Cube
                entity = m_Scene->CreateEntity("Dynamic Box");
                entity.AddComponent<TransformComponent>(position, glm::vec3(1.0f, 1.0f, 1.0f));
                if (m_CubeMesh) {
                    entity.AddComponent<MeshInstance3D>(
                        m_CubeMesh, 1, 4.0f,
                        glm::vec4(0.96f, 0.52f, 0.12f, 1.0f),
                        glm::vec4(0.12f, 0.13f, 0.15f, 1.0f)
                    );
                }
                if (m_PhysicsScene) {
                    BoxBodyProps bProps;
                    bProps.Position = position;
                    bProps.HalfExtent = glm::vec3(0.5f, 0.5f, 0.5f);
                    bProps.IsDynamic = true;
                    auto bodyID = m_PhysicsScene->CreateBox(bProps);
                    entity.AddComponent<RigidBody3D>(bodyID, true);
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, glm::vec3(0.5f));
                }
                break;
            }
            case 1: { // 3D Sphere
                entity = m_Scene->CreateEntity("Dynamic Sphere");
                entity.AddComponent<TransformComponent>(position, glm::vec3(0.5f, 0.5f, 0.5f));
                if (m_SphereMesh) {
                    entity.AddComponent<MeshInstance3D>(
                        m_SphereMesh, 2, 16.0f,
                        glm::vec4(0.0f, 0.85f, 0.95f, 1.0f),
                        glm::vec4(0.05f, 0.12f, 0.24f, 1.0f)
                    );
                }
                if (m_PhysicsScene) {
                    SphereBodyProps sProps;
                    sProps.Position = position;
                    sProps.Radius = 0.5f;
                    sProps.IsDynamic = true;
                    auto bodyID = m_PhysicsScene->CreateSphere(sProps);
                    entity.AddComponent<RigidBody3D>(bodyID, true);
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Sphere, 0.5f);
                }
                break;
            }
            case 2: { // 3D Cylinder
                entity = m_Scene->CreateEntity("Dynamic Cylinder");
                entity.AddComponent<TransformComponent>(position, glm::vec3(0.5f, 0.75f, 0.5f));
                if (m_CylinderMesh) {
                    entity.AddComponent<MeshInstance3D>(
                        m_CylinderMesh, 1, 4.0f,
                        glm::vec4(0.20f, 0.80f, 0.40f, 1.0f),
                        glm::vec4(0.08f, 0.18f, 0.10f, 1.0f)
                    );
                }
                if (m_PhysicsScene) {
                    CylinderBodyProps cProps;
                    cProps.Position = position;
                    cProps.HalfHeight = 0.75f;
                    cProps.Radius = 0.5f;
                    cProps.IsDynamic = true;
                    auto bodyID = m_PhysicsScene->CreateCylinder(cProps);
                    entity.AddComponent<RigidBody3D>(bodyID, true);
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Cylinder, 0.75f, 0.5f);
                }
                break;
            }
            case 3: { // 3D Cone
                entity = m_Scene->CreateEntity("Dynamic Cone");
                entity.AddComponent<TransformComponent>(position, glm::vec3(0.5f, 0.75f, 0.5f));
                if (m_ConeMesh) {
                    entity.AddComponent<MeshInstance3D>(
                        m_ConeMesh, 1, 4.0f,
                        glm::vec4(0.95f, 0.65f, 0.10f, 1.0f),
                        glm::vec4(0.18f, 0.12f, 0.05f, 1.0f)
                    );
                }
                if (m_PhysicsScene) {
                    CylinderBodyProps cProps;
                    cProps.Position = position;
                    cProps.HalfHeight = 0.75f;
                    cProps.Radius = 0.5f;
                    cProps.IsDynamic = true;
                    auto bodyID = m_PhysicsScene->CreateCylinder(cProps);
                    entity.AddComponent<RigidBody3D>(bodyID, true);
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Cone, 0.75f, 0.5f);
                }
                break;
            }
            case 4: { // Static Floor Plane
                entity = m_Scene->CreateEntity("Static Floor Plane");
                entity.AddComponent<TransformComponent>(position, glm::vec3(50.0f, 1.0f, 50.0f));
                if (m_PlaneMesh) {
                    entity.AddComponent<MeshInstance3D>(
                        m_PlaneMesh, 0, 1.0f,
                        glm::vec4(0.25f, 0.28f, 0.32f, 1.0f),
                        glm::vec4(0.15f, 0.17f, 0.20f, 1.0f)
                    );
                }
                if (m_PhysicsScene) {
                    BoxBodyProps bProps;
                    bProps.Position = position;
                    bProps.HalfExtent = glm::vec3(25.0f, 0.5f, 25.0f);
                    bProps.IsDynamic = false;
                    auto bodyID = m_PhysicsScene->CreateBox(bProps);
                    entity.AddComponent<RigidBody3D>(bodyID, false);
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Plane, glm::vec3(25.0f, 0.5f, 25.0f));
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
                        if (ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                            if (ext == ".axle" && AssemblySerializer::IsAssemblyFile(p)) {
                                SceneSerializerContext ctx;
                                ctx.CubeMesh = m_CubeMesh;
                                ctx.SphereMesh = m_SphereMesh;
                                ctx.CylinderMesh = m_CylinderMesh;
                                ctx.ConeMesh = m_ConeMesh;
                                ctx.PlaneMesh = m_PlaneMesh;
                                auto spawned = m_Scene->InstantiateAssembly(p, m_PhysicsScene, glm::vec3(0.0f, 1.0f, 0.0f), &ctx);
                                if (spawned) {
                                    m_SelectedEntity = spawned;
                                }
                            } else {
                                if (ext != ".axle") {
                                std::filesystem::path axlePath = p.parent_path() / (p.stem().string() + ".axle");
                                if (!std::filesystem::exists(axlePath)) {
                                    AssetSerializer::AutoConvertModel(p, axlePath, true);
                                }
                                p = axlePath;
                            }

                            auto pkg = AssetSerializer::LoadPackage(p);
                            if (pkg.Success) {
                                glm::vec3 spawnPos(0.0f, pkg.HalfExtent.y, 0.0f);
                                if (pkg.IsPackage) {
                                    std::string rootTag = pkg.Name.empty() ? p.stem().string() : pkg.Name;
                                    auto rootEntity = m_Scene->CreateEntity(rootTag);
                                    rootEntity.AddComponent<TransformComponent>(spawnPos, glm::vec3(1.0f));
                                    auto& rootHierarchy = rootEntity.AddComponent<HierarchyComponent>();

                                    if (m_PhysicsScene) {
                                        BoxBodyProps bProps;
                                        bProps.Position = spawnPos;
                                        bProps.HalfExtent = pkg.HalfExtent;
                                        bProps.IsDynamic = true;
                                        bProps.Friction = 0.70f;
                                        bProps.Restitution = 0.15f;
                                        auto bodyId = m_PhysicsScene->CreateBox(bProps);
                                        rootEntity.AddComponent<RigidBody3D>(bodyId, true);
                                        rootEntity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, pkg.HalfExtent, 0.70f, 0.15f);
                                    }

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
                                    if (m_PhysicsScene) {
                                        BoxBodyProps bProps;
                                        bProps.Position = spawnPos;
                                        bProps.HalfExtent = pkg.HalfExtent;
                                        bProps.IsDynamic = true;
                                        bProps.Friction = 0.70f;
                                        bProps.Restitution = 0.15f;
                                        auto bodyId = m_PhysicsScene->CreateBox(bProps);
                                        newEnt.AddComponent<RigidBody3D>(bodyId, true);
                                        newEnt.AddComponent<CollisionShape3D>(CollisionShapeType::Box, pkg.HalfExtent, 0.70f, 0.15f);
                                    }
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
                if (m_SelectedEntity && m_Scene->GetRegistry().valid((entt::entity)m_SelectedEntity)) {
                    DrawComponents(m_SelectedEntity);
                } else if (!m_SelectedAsset.empty() && std::filesystem::exists(m_SelectedAsset)) {
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
                            if (ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                                if (ext == ".axle" && AssemblySerializer::IsAssemblyFile(p)) {
                                    SceneSerializerContext ctx;
                                    ctx.CubeMesh = m_CubeMesh;
                                    ctx.SphereMesh = m_SphereMesh;
                                    ctx.CylinderMesh = m_CylinderMesh;
                                    ctx.ConeMesh = m_ConeMesh;
                                    ctx.PlaneMesh = m_PlaneMesh;
                                    auto spawned = m_Scene->InstantiateAssembly(p, m_PhysicsScene, glm::vec3(0.0f, 1.0f, 0.0f), &ctx);
                                    if (spawned) {
                                        m_SelectedEntity = spawned;
                                    }
                                } else {
                                    if (ext != ".axle") {
                                    std::filesystem::path axlePath = p.parent_path() / (p.stem().string() + ".axle");
                                    if (!std::filesystem::exists(axlePath)) {
                                        AssetSerializer::AutoConvertModel(p, axlePath, true);
                                    }
                                    p = axlePath;
                                }

                                auto pkg = AssetSerializer::LoadPackage(p);
                                if (pkg.Success) {
                                    glm::vec3 spawnPos(0.0f, pkg.HalfExtent.y, 0.0f);
                                    if (pkg.IsPackage) {
                                        std::string rootTag = pkg.Name.empty() ? p.stem().string() : pkg.Name;
                                        auto rootEntity = m_Scene->CreateEntity(rootTag);
                                        rootEntity.AddComponent<TransformComponent>(spawnPos, glm::vec3(1.0f));
                                        auto& rootHierarchy = rootEntity.AddComponent<HierarchyComponent>();

                                        if (m_PhysicsScene) {
                                            BoxBodyProps bProps;
                                            bProps.Position = spawnPos;
                                            bProps.HalfExtent = pkg.HalfExtent;
                                            bProps.IsDynamic = true;
                                            bProps.Friction = 0.70f;
                                            bProps.Restitution = 0.15f;
                                            auto bodyId = m_PhysicsScene->CreateBox(bProps);
                                            rootEntity.AddComponent<RigidBody3D>(bodyId, true);
                                            rootEntity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, pkg.HalfExtent, 0.70f, 0.15f);
                                        }

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
                                        if (m_PhysicsScene) {
                                            BoxBodyProps bProps;
                                            bProps.Position = spawnPos;
                                            bProps.HalfExtent = pkg.HalfExtent;
                                            bProps.IsDynamic = true;
                                            bProps.Friction = 0.70f;
                                            bProps.Restitution = 0.15f;
                                            auto bodyId = m_PhysicsScene->CreateBox(bProps);
                                            newEnt.AddComponent<RigidBody3D>(bodyId, true);
                                            newEnt.AddComponent<CollisionShape3D>(CollisionShapeType::Box, pkg.HalfExtent, 0.70f, 0.15f);
                                        }
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

            // 2. Attach Script from Content Browser
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

            if (entity.HasComponent<HierarchyComponent>()) {
                auto& hc = entity.GetComponent<HierarchyComponent>();
                if (hc.Parent != entt::null && m_Scene->GetRegistry().valid(hc.Parent)) {
                    Entity parentEntity{ hc.Parent, m_Scene };
                    std::string parentTag = parentEntity.HasComponent<TagComponent>() ? parentEntity.GetComponent<TagComponent>().Tag : "Parent";
                    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Parent: %s", parentTag.c_str());
                    ImGui::SameLine();
                    ImGui::Checkbox("Inherit Transform", &hc.InheritTransform);
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

        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen 
                                               | ImGuiTreeNodeFlags_Framed 
                                               | ImGuiTreeNodeFlags_SpanAvailWidth;

        // Transform Component
        if (entity.HasComponent<TransformComponent>()) {
            if (ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), treeNodeFlags, "Transform")) {
                auto& tc = entity.GetComponent<TransformComponent>();

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


                ImGui::TreePop();
            }
            ImGui::Spacing();
        }

        // MeshInstance3D Component
        if (entity.HasComponent<MeshInstance3D>()) {
            bool removeMesh = false;
            bool meshOpen = ImGui::TreeNodeEx((void*)typeid(MeshInstance3D).hash_code(), treeNodeFlags, "MeshInstance3D");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeMesh = true;
                }
                ImGui::EndPopup();
            }

            if (meshOpen) {
                auto& mc = entity.GetComponent<MeshInstance3D>();

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
                        if (ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                            mc.AssetPath = p.string();
                            if (ext == ".axle") {
                                auto loaded = AssetSerializer::LoadMesh(p);
                                if (loaded.Success) {
                                    mc.VAO = loaded.VAO;
                                    mc.PatternMode = loaded.SourceData.PatternMode;
                                    mc.TileScale = loaded.SourceData.TileScale;
                                    mc.Color1 = loaded.SourceData.ColorPrimary;
                                    mc.Color2 = loaded.SourceData.ColorSecondary;
                                }
                            } else if (ext == ".obj") {
                                std::filesystem::path axlePath = p.parent_path() / (p.stem().string() + ".axle");
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

                const char* patternNames[] = { "0: World XZ Caro (Floor)", "1: Local 3D Caro (Box/Cyl)", "2: Spherical Caro (Sphere)" };
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

                ImGui::TreePop();
            }

            if (removeMesh) {
                entity.RemoveComponent<MeshInstance3D>();
            }
            ImGui::Spacing();
        } else {
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

                        if (ext == ".axle") {
                            auto loaded = AssetSerializer::LoadMesh(p);
                            if (loaded.Success) {
                                loadedVao = loaded.VAO;
                                patternMode = loaded.SourceData.PatternMode;
                                tileScale = loaded.SourceData.TileScale;
                                color1 = loaded.SourceData.ColorPrimary;
                                color2 = loaded.SourceData.ColorSecondary;
                            }
                        } else if (ext == ".obj") {
                            std::filesystem::path axlePath = p.parent_path() / (p.stem().string() + ".axle");
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
        if (entity.HasComponent<MaterialComponent>()) {
            bool removeMat = false;
            bool matOpen = ImGui::TreeNodeEx((void*)typeid(MaterialComponent).hash_code(), treeNodeFlags, "PBR Material");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeMat = true;
                }
                ImGui::EndPopup();
            }

            if (matOpen) {
                auto& mat = entity.GetComponent<MaterialComponent>();

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
                        case 3: mat.SetPresetChrome(); break;
                        case 4: mat.SetPresetGold(); break;
                        case 5: mat.SetPresetCopper(); break;
                        case 6: mat.SetPresetRubber(); break;
                        case 7: mat.SetPresetPlastic(); break;
                        case 8: mat.SetPresetCastIron(); break;
                        case 9: mat.SetPresetAluminum(); break;
                        case 10: mat.SetPresetObsidian(); break;
                        case 11: mat.SetPresetHeadlightLED(); break;
                        case 12: mat.SetPresetTaillightLED(); break;
                        default: break;
                    }
                }

                // Quick Preset Buttons Bar
                ImGui::TextDisabled("Quick Presets:");
                if (ImGui::SmallButton("Car Paint (Red)")) { mat.SetPresetCarPaintRed(); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Car Paint (Blue)")) { mat.SetPresetCarPaintBlue(); }
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

                ImGui::TreePop();
            }

            if (removeMat) {
                entity.RemoveComponent<MaterialComponent>();
            }
            ImGui::Spacing();
        }

        // Collision Shape 3D Component
        if (entity.HasComponent<CollisionShape3D>()) {
            bool removeCS = false;
            bool csOpen = ImGui::TreeNodeEx((void*)typeid(CollisionShape3D).hash_code(), treeNodeFlags, "CollisionShape3D");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeCS = true;
                }
                ImGui::EndPopup();
            }

            if (csOpen) {
                auto& cs = entity.GetComponent<CollisionShape3D>();

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

                ImGui::TreePop();
            }

            if (removeCS) {
                entity.RemoveComponent<CollisionShape3D>();
            }
            ImGui::Spacing();
        }

        // Rigid Body 3D Component
        if (entity.HasComponent<RigidBody3D>()) {
            bool removeRB = false;
            bool rbOpen = ImGui::TreeNodeEx((void*)typeid(RigidBody3D).hash_code(), treeNodeFlags, "RigidBody3D");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeRB = true;
                }
                ImGui::EndPopup();
            }

            if (rbOpen) {
                auto& rb = entity.GetComponent<RigidBody3D>();

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

                ImGui::TreePop();
            }

            if (removeRB) {
                auto& rb = entity.GetComponent<RigidBody3D>();
                if (m_PhysicsScene && !rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                    m_PhysicsScene->DestroyBody(rb.RuntimeBodyID);
                }
                entity.RemoveComponent<RigidBody3D>();
            }
            ImGui::Spacing();
        }

        // Vehicle Component
        if (entity.HasComponent<VehicleComponent>()) {
            bool removeVC = false;
            bool vcOpen = ImGui::TreeNodeEx((void*)typeid(VehicleComponent).hash_code(), treeNodeFlags, "Vehicle Dynamics");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeVC = true;
                }
                ImGui::EndPopup();
            }

            if (vcOpen) {
                auto& vc = entity.GetComponent<VehicleComponent>();
                if (vc.VehicleInstance) {
                    auto telemetry = vc.VehicleInstance->GetTelemetry();
                    ImGui::Text("Speed: %.1f km/h (%.1f m/s)", telemetry.SpeedKmh, telemetry.SpeedMps);
                    ImGui::Text("Engine RPM: %.0f", telemetry.EngineRPM);
                    ImGui::Text("Current Gear: %d", telemetry.CurrentGear);
                    ImGui::Text("Chassis Mass: %.0f kg", vc.VehicleInstance->GetConfig().ChassisMass);
                } else {
                    ImGui::TextDisabled("Vehicle controller not attached.");
                }
                ImGui::TreePop();
            }

            if (removeVC) {
                entity.RemoveComponent<VehicleComponent>();
            }
            ImGui::Spacing();
        }



        // Directional Light Component
        if (entity.HasComponent<DirectionalLightComponent>()) {
            bool removeDLC = false;
            bool dlcOpen = ImGui::TreeNodeEx((void*)typeid(DirectionalLightComponent).hash_code(), treeNodeFlags, "Directional Sun Light");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeDLC = true;
                }
                ImGui::EndPopup();
            }

            if (dlcOpen) {
                auto& dlc = entity.GetComponent<DirectionalLightComponent>();

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

                ImGui::TreePop();
            }

            if (removeDLC) {
                entity.RemoveComponent<DirectionalLightComponent>();
            }
            ImGui::Spacing();
        }

        // Atmospheric Skybox Component (Phase 11: Sub-step 4.2)
        if (entity.HasComponent<SkyboxComponent>()) {
            bool removeSBC = false;
            bool sbcOpen = ImGui::TreeNodeEx((void*)typeid(SkyboxComponent).hash_code(), treeNodeFlags, "Atmospheric Skybox");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeSBC = true;
                }
                ImGui::EndPopup();
            }

            if (sbcOpen) {
                auto& sbc = entity.GetComponent<SkyboxComponent>();

                ImGui::Checkbox("Enable Skybox", &sbc.Enabled);
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

                ImGui::TreePop();
            }

            if (removeSBC) {
                entity.RemoveComponent<SkyboxComponent>();
            }
            ImGui::Spacing();
        }

        // Point Light Component
        if (entity.HasComponent<PointLightComponent>()) {
            bool removePLC = false;
            bool plcOpen = ImGui::TreeNodeEx((void*)typeid(PointLightComponent).hash_code(), treeNodeFlags, "Point Light");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removePLC = true;
                }
                ImGui::EndPopup();
            }

            if (plcOpen) {
                auto& plc = entity.GetComponent<PointLightComponent>();
                ImGui::ColorEdit3("Light Color", glm::value_ptr(plc.Color));
                ImGui::DragFloat("Intensity", &plc.Intensity, 0.1f, 0.0f, 100.0f, "%.2f");
                ImGui::DragFloat("Radius", &plc.Radius, 0.5f, 0.1f, 500.0f, "%.1f m");
                ImGui::DragFloat("Falloff", &plc.Falloff, 0.05f, 0.1f, 10.0f, "%.2f");
                ImGui::TreePop();
            }

            if (removePLC) {
                entity.RemoveComponent<PointLightComponent>();
            }
            ImGui::Spacing();
        }

        // Camera Component
        if (entity.HasComponent<CameraComponent>()) {
            bool removeCam = false;
            bool camOpen = ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code(), treeNodeFlags, "Camera");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeCam = true;
                }
                ImGui::EndPopup();
            }

            if (camOpen) {
                auto& cam = entity.GetComponent<CameraComponent>();
                ImGui::DragFloat("Field of View", &cam.FOV, 0.5f, 10.0f, 150.0f, "%.1f deg");
                ImGui::DragFloat("Near Clip", &cam.NearClip, 0.01f, 0.001f, 10.0f, "%.3f m");
                ImGui::DragFloat("Far Clip", &cam.FarClip, 1.0f, 1.0f, 10000.0f, "%.1f m");
                ImGui::Checkbox("Primary Camera", &cam.Primary);
                ImGui::TreePop();
            }

            if (removeCam) {
                entity.RemoveComponent<CameraComponent>();
            }
            ImGui::Spacing();
        }


        // Native Script Component
        if (entity.HasComponent<NativeScriptComponent>()) {
            auto& nsc = entity.GetComponent<NativeScriptComponent>();
            bool removeScript = false;
            bool scriptOpen = ImGui::TreeNodeEx((void*)typeid(NativeScriptComponent).hash_code(), treeNodeFlags, "Native Script");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeScript = true;
                }
                ImGui::EndPopup();
            }

            if (scriptOpen) {

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

                ImGui::TreePop();
            }

            if (removeScript) {
                if (nsc.Instance && nsc.DestroyScript) {
                    nsc.DestroyScript(&nsc);
                }
                entity.RemoveComponent<NativeScriptComponent>();
            }
            ImGui::Spacing();
        }

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
        }
        ImGui::PopStyleColor(3);

        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (!entity.HasComponent<MeshInstance3D>()) {
                if (ImGui::MenuItem("MeshInstance3D")) {
                    entity.AddComponent<MeshInstance3D>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<CollisionShape3D>()) {
                if (ImGui::MenuItem("CollisionShape3D")) {
                    glm::vec3 halfExtent(0.5f);
                    if (entity.HasComponent<TransformComponent>()) {
                        halfExtent = entity.GetComponent<TransformComponent>().Scale * 0.5f;
                    }
                    entity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, halfExtent);
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<RigidBody3D>()) {
                if (ImGui::MenuItem("RigidBody3D (Dynamic Box)")) {
                    if (m_PhysicsScene) {
                        auto& tc = entity.GetComponent<TransformComponent>();
                        Axle::BoxBodyProps bProps;
                        bProps.Position = tc.Translation;
                        bProps.Rotation = tc.Rotation;
                        bProps.HalfExtent = tc.Scale * 0.5f;
                        bProps.IsDynamic = true;
                        auto bodyID = m_PhysicsScene->CreateBox(bProps);
                        entity.AddComponent<RigidBody3D>(bodyID, true);
                        if (!entity.HasComponent<CollisionShape3D>()) {
                            entity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, tc.Scale * 0.5f);
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("RigidBody3D (Static Box)")) {
                    if (m_PhysicsScene) {
                        auto& tc = entity.GetComponent<TransformComponent>();
                        Axle::BoxBodyProps bProps;
                        bProps.Position = tc.Translation;
                        bProps.Rotation = tc.Rotation;
                        bProps.HalfExtent = tc.Scale * 0.5f;
                        bProps.IsDynamic = false;
                        auto bodyID = m_PhysicsScene->CreateBox(bProps);
                        entity.AddComponent<RigidBody3D>(bodyID, false);
                        if (!entity.HasComponent<CollisionShape3D>()) {
                            entity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, tc.Scale * 0.5f);
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<NativeScriptComponent>()) {
                if (ImGui::MenuItem("Native Script Component")) {
                    auto& nsc = entity.AddComponent<NativeScriptComponent>();
                    const auto& registered = ScriptRegistry::GetScripts();
                    if (!registered.empty()) {
                        nsc.Bind(registered.begin()->first);
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<VehicleComponent>()) {
                if (ImGui::MenuItem("Vehicle Component")) {
                    entity.AddComponent<VehicleComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<DirectionalLightComponent>()) {
                if (ImGui::MenuItem("Directional Light Component")) {
                    entity.AddComponent<DirectionalLightComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<SkyboxComponent>()) {
                if (ImGui::MenuItem("Atmospheric Skybox Component")) {
                    entity.AddComponent<SkyboxComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<MaterialComponent>()) {
                if (ImGui::MenuItem("Material Component")) {
                    entity.AddComponent<MaterialComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<PointLightComponent>()) {
                if (ImGui::MenuItem("Point Light Component")) {
                    entity.AddComponent<PointLightComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<CameraComponent>()) {
                if (ImGui::MenuItem("Camera Component")) {
                    entity.AddComponent<CameraComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();

        }
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

        bool isAssembly = (ext == ".axle" && AssemblySerializer::IsAssemblyFile(path));
        bool isMesh = (ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") && !isAssembly;

        // Top Asset Header Card
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.15f, 0.20f, 0.85f));
        ImGui::BeginChild("AssetHeaderCard", ImVec2(0.0f, 66.0f), true);
        {
            if (isAssembly) {
                ImGui::TextColored(ImVec4(0.25f, 0.90f, 0.55f, 1.0f), "[MECHANICAL ASSEMBLY PREFAB]");
            } else if (isMesh) {
                ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "[3D MESH PACKAGE]");
            } else if (ext == ".axlescene" || ext == ".scene") {
                ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.30f, 1.0f), "[SCENE ASSET]");
            } else {
                ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.85f, 1.0f), "[ASSET FILE]");
            }

            ImGui::TextUnformatted(filenameStr.c_str());
            ImGui::TextDisabled("%s | %s", relPath.string().c_str(), sizeStr.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Embedded Interactive 3D Asset Preview Viewport (Unified Inspector paradigm)
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
                    auto ent = m_Scene->InstantiateAssembly(path, m_PhysicsScene, glm::vec3(0.0f, 1.0f, 0.0f), &ctx);
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

}
