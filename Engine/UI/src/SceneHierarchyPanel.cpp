#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/AssetSerializer.hpp>
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
                    entity.AddComponent<MeshComponent>(
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
                    entity.AddComponent<RigidBodyComponent>(bodyID, true);
                }
                break;
            }
            case 1: { // 3D Sphere
                entity = m_Scene->CreateEntity("Dynamic Sphere");
                entity.AddComponent<TransformComponent>(position, glm::vec3(0.5f, 0.5f, 0.5f));
                if (m_SphereMesh) {
                    entity.AddComponent<MeshComponent>(
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
                    entity.AddComponent<RigidBodyComponent>(bodyID, true);
                }
                break;
            }
            case 2: { // 3D Cylinder
                entity = m_Scene->CreateEntity("Dynamic Cylinder");
                entity.AddComponent<TransformComponent>(position, glm::vec3(0.5f, 0.75f, 0.5f));
                if (m_CylinderMesh) {
                    entity.AddComponent<MeshComponent>(
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
                    entity.AddComponent<RigidBodyComponent>(bodyID, true);
                }
                break;
            }
            case 3: { // 3D Cone
                entity = m_Scene->CreateEntity("Dynamic Cone");
                entity.AddComponent<TransformComponent>(position, glm::vec3(0.5f, 0.75f, 0.5f));
                if (m_ConeMesh) {
                    entity.AddComponent<MeshComponent>(
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
                    entity.AddComponent<RigidBodyComponent>(bodyID, true);
                }
                break;
            }
            case 4: { // Static Floor Plane
                entity = m_Scene->CreateEntity("Static Floor Plane");
                entity.AddComponent<TransformComponent>(position, glm::vec3(50.0f, 1.0f, 50.0f));
                if (m_PlaneMesh) {
                    entity.AddComponent<MeshComponent>(
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
                    entity.AddComponent<RigidBodyComponent>(bodyID, false);
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
        if (ImGui::MenuItem("Empty Entity")) {
            m_SelectedEntity = m_Scene->CreateEntity("Empty Entity");
            m_SelectedEntity.AddComponent<TransformComponent>();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("3D Cube (Dynamic Box)")) {
            CreatePrimitive(0);
        }
        if (ImGui::MenuItem("3D Sphere (Dynamic)")) {
            CreatePrimitive(1);
        }
        if (ImGui::MenuItem("3D Cylinder (Dynamic)")) {
            CreatePrimitive(2);
        }
        if (ImGui::MenuItem("3D Cone (Dynamic)")) {
            CreatePrimitive(3);
        }
        if (ImGui::MenuItem("Static Floor Plane")) {
            CreatePrimitive(4);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Directional Sun Light")) {
            auto lightEntity = m_Scene->CreateEntity("Sun Light");
            lightEntity.AddComponent<TransformComponent>(glm::vec3(0.0f, 25.0f, 0.0f));
            lightEntity.AddComponent<DirectionalLightComponent>();
            m_SelectedEntity = lightEntity;
        }
    }

    void SceneHierarchyPanel::OnImGuiRender(bool* p_hierarchyOpen, bool* p_propertiesOpen) {
        if (!m_Scene) return;

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

                    // Filter search
                    if (m_SearchFilter[0] != '\0') {
                        const auto& tag = entity.GetComponent<TagComponent>().Tag;
                        if (!ContainsIgnoreCase(tag, m_SearchFilter)) {
                            continue;
                        }
                    }

                    // If entity is a child, it will be rendered under its parent node
                    if (entity.HasComponent<HierarchyComponent>()) {
                        auto& hc = entity.GetComponent<HierarchyComponent>();
                        if (hc.Parent != entt::null && m_Scene->GetRegistry().valid(hc.Parent)) {
                            continue;
                        }
                    }
                    DrawEntityNode(entity);
                }

                // Click on blank space to deselect
                if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
                    m_SelectedEntity = {};
                }

                // Right click blank space context menu
                if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                    DrawCreateMenu();
                    ImGui::EndPopup();
                }

                // Drag & Drop to instantiate 3D model in Hierarchy
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = static_cast<const char*>(payload->Data);
                        std::filesystem::path p(pathStr);
                        std::string ext = p.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                        if (ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
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
                                        rootEntity.AddComponent<RigidBodyComponent>(bodyId, true);
                                    }

                                    for (const auto& part : pkg.Parts) {
                                        auto childEntity = m_Scene->CreateEntity(part.Name);
                                        auto& ct = childEntity.AddComponent<TransformComponent>(part.LocalTranslation, part.LocalScale);
                                        ct.Rotation = part.LocalRotation;

                                        auto partFullPath = p.parent_path() / part.RelativePath;
                                        childEntity.AddComponent<MeshComponent>(
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
                                    newEnt.AddComponent<MeshComponent>(
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
                                        newEnt.AddComponent<RigidBodyComponent>(bodyId, true);
                                    }
                                    m_SelectedEntity = newEnt;
                                    AXLE_INFO("Instantiated 3D model '{0}' via Scene Hierarchy drop", p.filename().string());
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::End();
        }

        // 2. Properties / Inspector Window
        if (!p_propertiesOpen || *p_propertiesOpen) {
            ImGui::SetNextWindowSize(ImVec2(340.0f, 500.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Properties", p_propertiesOpen)) {
                if (m_SelectedEntity) {
                    DrawComponents(m_SelectedEntity);
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
                                            rootEntity.AddComponent<RigidBodyComponent>(bodyId, true);
                                        }

                                        for (const auto& part : pkg.Parts) {
                                            auto childEntity = m_Scene->CreateEntity(part.Name);
                                            auto& ct = childEntity.AddComponent<TransformComponent>(part.LocalTranslation, part.LocalScale);
                                            ct.Rotation = part.LocalRotation;

                                            auto partFullPath = p.parent_path() / part.RelativePath;
                                            childEntity.AddComponent<MeshComponent>(
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
                                        newEnt.AddComponent<MeshComponent>(
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
                                            newEnt.AddComponent<RigidBodyComponent>(bodyId, true);
                                        }
                                        m_SelectedEntity = newEnt;
                                        AXLE_INFO("Instantiated 3D model '{0}' via Properties drop", p.filename().string());
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
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
        auto& tag = entity.GetComponent<TagComponent>().Tag;

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

        if (ImGui::IsItemClicked()) {
            m_SelectedEntity = entity;
        }

        bool entityDeleted = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete Entity")) {
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
            if (m_SelectedEntity == entity) {
                m_SelectedEntity = {};
            }
            m_Scene->DestroyEntity(entity);
        }
    }

    void SceneHierarchyPanel::DrawComponents(Entity entity) {
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
                    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Parent: %s", parentEntity.GetComponent<TagComponent>().Tag.c_str());
                    ImGui::SameLine();
                    ImGui::Checkbox("Inherit Transform", &hc.InheritTransform);
                }
                if (!hc.Children.empty()) {
                    ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.55f, 1.0f), "Children: %zu attached", hc.Children.size());
                }
            }
            ImGui::Spacing();
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

                // If rigid body attached and user modified transform, sync to physics engine
                if (changed && entity.HasComponent<RigidBodyComponent>() && m_PhysicsScene) {
                    auto& rb = entity.GetComponent<RigidBodyComponent>();
                    if (!rb.RuntimeBodyID.IsInvalid()) {
                        m_PhysicsScene->SetBodyPositionAndRotation(rb.RuntimeBodyID, tc.Translation, tc.Rotation, true);
                    }
                }

                ImGui::TreePop();
            }
            ImGui::Spacing();
        }

        // Mesh Component
        if (entity.HasComponent<MeshComponent>()) {
            bool removeMesh = false;
            bool meshOpen = ImGui::TreeNodeEx((void*)typeid(MeshComponent).hash_code(), treeNodeFlags, "Mesh & Material");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeMesh = true;
                }
                ImGui::EndPopup();
            }

            if (meshOpen) {
                auto& mc = entity.GetComponent<MeshComponent>();

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
                            if (m_Scene->GetRegistry().valid(childId) && m_Scene->GetRegistry().all_of<MeshComponent>(childId)) {
                                auto& childMc = m_Scene->GetRegistry().get<MeshComponent>(childId);
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
                entity.RemoveComponent<MeshComponent>();
            }
            ImGui::Spacing();
        } else {
            // Drop target when entity has no MeshComponent
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
                        entity.AddComponent<MeshComponent>(
                            loadedVao, p.string(), patternMode, tileScale,
                            color1, color2
                        );
                        AXLE_INFO("Added MeshComponent with 3D model '{0}' to Entity '{1}'", p.filename().string(), entity.GetComponent<TagComponent>().Tag);
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
                ImGui::Spacing();

                // 3. Roughness
                DrawTextureSlot("Roughness Map", mat.RoughnessMap, mat.RoughnessPath, mat.UseRoughnessMap,
                    [&](const std::string& p) { mat.SetRoughnessMap(p); });
                if (!mat.UseRoughnessMap) {
                    ImGui::SliderFloat("Roughness", &mat.Roughness, 0.04f, 1.0f, "%.2f");
                }
                ImGui::Spacing();

                // 4. Metallic
                DrawTextureSlot("Metallic Map", mat.MetallicMap, mat.MetallicPath, mat.UseMetallicMap,
                    [&](const std::string& p) { mat.SetMetallicMap(p); });
                if (!mat.UseMetallicMap) {
                    ImGui::SliderFloat("Metallic", &mat.Metallic, 0.0f, 1.0f, "%.2f");
                }
                ImGui::Spacing();

                // 5. UV Tiling
                ImGui::DragFloat2("UV Scale", glm::value_ptr(mat.UVScale), 0.1f, 0.01f, 100.0f, "%.2f");

                // Quick Presets
                ImGui::Spacing();
                ImGui::TextDisabled("Quick Texture Presets:");
                if (ImGui::SmallButton("Prototype Grid")) {
                    mat.SetAlbedo("textures/Prototype_Grid.png");
                    mat.SetNormal("textures/Flat_Normal.png");
                    mat.SetRoughnessMap("textures/Roughness_Gradient.png");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear Textures")) {
                    mat.SetAlbedo("");
                    mat.SetNormal("");
                    mat.SetRoughnessMap("");
                    mat.SetMetallicMap("");
                    mat.Roughness = 0.5f;
                    mat.Metallic = 0.0f;
                }

                ImGui::TreePop();
            }

            if (removeMat) {
                entity.RemoveComponent<MaterialComponent>();
            }
            ImGui::Spacing();
        }

        // Rigid Body Component
        if (entity.HasComponent<RigidBodyComponent>()) {
            bool removeRB = false;
            bool rbOpen = ImGui::TreeNodeEx((void*)typeid(RigidBodyComponent).hash_code(), treeNodeFlags, "Jolt Rigid Body");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeRB = true;
                }
                ImGui::EndPopup();
            }

            if (rbOpen) {
                auto& rb = entity.GetComponent<RigidBodyComponent>();

                ImGui::Text("Body ID: %u", rb.RuntimeBodyID.GetIndexAndSequenceNumber());
                ImGui::Text("Motion Type: %s", rb.IsDynamic ? "Dynamic" : "Static");

                if (m_PhysicsScene && !rb.RuntimeBodyID.IsInvalid()) {
                    const bool isActive = m_PhysicsScene->IsActive(rb.RuntimeBodyID);
                    ImGui::Text("State: ");
                    ImGui::SameLine();
                    if (isActive) {
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "ACTIVE");
                    } else {
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "SLEEPING");
                    }

                    // Friction & Restitution live tuning
                    JPH::BodyLockWrite lock(m_PhysicsScene->GetSystem().GetBodyLockInterface(), rb.RuntimeBodyID);
                    if (lock.Succeeded()) {
                        JPH::Body& body = lock.GetBody();

                        float friction = body.GetFriction();
                        if (ImGui::SliderFloat("Friction", &friction, 0.0f, 2.0f, "%.2f")) {
                            body.SetFriction(friction);
                        }

                        float restitution = body.GetRestitution();
                        if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f, "%.2f")) {
                            body.SetRestitution(restitution);
                        }

                        if (rb.IsDynamic) {
                            bool allowSleeping = body.GetAllowSleeping();
                            if (ImGui::Checkbox("Allow Sleeping", &allowSleeping)) {
                                body.SetAllowSleeping(allowSleeping);
                            }
                        }
                    }

                    if (rb.IsDynamic) {
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
                auto& rb = entity.GetComponent<RigidBodyComponent>();
                if (m_PhysicsScene && !rb.RuntimeBodyID.IsInvalid()) {
                    m_PhysicsScene->DestroyBody(rb.RuntimeBodyID);
                }
                entity.RemoveComponent<RigidBodyComponent>();
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

        // Native Script Component
        if (entity.HasComponent<NativeScriptComponent>()) {
            bool removeNSC = false;
            bool nscOpen = ImGui::TreeNodeEx((void*)typeid(NativeScriptComponent).hash_code(), treeNodeFlags, "C++ Native Script");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeNSC = true;
                }
                ImGui::EndPopup();
            }

            if (nscOpen) {
                auto& nsc = entity.GetComponent<NativeScriptComponent>();
                ImGui::Text("Class: %s", nsc.ScriptName.c_str());
                ImGui::Text("Status: %s", nsc.Instance ? "Active (Running)" : "Bound (Ready)");
                ImGui::TreePop();
            }

            if (removeNSC) {
                auto& nsc = entity.GetComponent<NativeScriptComponent>();
                if (nsc.Instance && nsc.DestroyScript) {
                    nsc.Instance->OnDestroy();
                    nsc.DestroyScript(&nsc);
                }
                entity.RemoveComponent<NativeScriptComponent>();
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
            if (!entity.HasComponent<MeshComponent>()) {
                if (ImGui::MenuItem("Mesh Component")) {
                    entity.AddComponent<MeshComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<RigidBodyComponent>()) {
                if (ImGui::MenuItem("RigidBody (Box Dynamic)")) {
                    if (m_PhysicsScene) {
                        auto& tc = entity.GetComponent<TransformComponent>();
                        Axle::BoxBodyProps bProps;
                        bProps.Position = tc.Translation;
                        bProps.Rotation = tc.Rotation;
                        bProps.HalfExtent = tc.Scale * 0.5f;
                        bProps.IsDynamic = true;
                        auto bodyID = m_PhysicsScene->CreateBox(bProps);
                        entity.AddComponent<RigidBodyComponent>(bodyID, true);
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("RigidBody (Box Static)")) {
                    if (m_PhysicsScene) {
                        auto& tc = entity.GetComponent<TransformComponent>();
                        Axle::BoxBodyProps bProps;
                        bProps.Position = tc.Translation;
                        bProps.Rotation = tc.Rotation;
                        bProps.HalfExtent = tc.Scale * 0.5f;
                        bProps.IsDynamic = false;
                        auto bodyID = m_PhysicsScene->CreateBox(bProps);
                        entity.AddComponent<RigidBodyComponent>(bodyID, false);
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!entity.HasComponent<NativeScriptComponent>()) {
                if (ImGui::MenuItem("Native Script Component")) {
                    entity.AddComponent<NativeScriptComponent>();
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
            if (!entity.HasComponent<MaterialComponent>()) {
                if (ImGui::MenuItem("Material Component")) {
                    entity.AddComponent<MaterialComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

}
