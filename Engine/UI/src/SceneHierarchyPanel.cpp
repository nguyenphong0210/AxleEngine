#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/Components.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace Axle {

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

    void SceneHierarchyPanel::OnImGuiRender(bool* p_hierarchyOpen, bool* p_propertiesOpen) {
        if (!m_Scene) return;

        // 1. Scene Hierarchy Window
        if (!p_hierarchyOpen || *p_hierarchyOpen) {
            ImGui::SetNextWindowSize(ImVec2(280.0f, 400.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Scene Hierarchy", p_hierarchyOpen)) {
                auto view = m_Scene->GetRegistry().view<TagComponent>();
                for (auto entityID : view) {
                    Entity entity{ entityID, m_Scene };
                    DrawEntityNode(entity);
                }

                // Click on blank space to deselect
                if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
                    m_SelectedEntity = {};
                }

                // Right click blank space context menu
                if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                    if (ImGui::MenuItem("Create Empty Entity")) {
                        m_SelectedEntity = m_Scene->CreateEntity("Empty Entity");
                        m_SelectedEntity.AddComponent<TransformComponent>();
                    }
                    ImGui::EndPopup();
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
                    ImGui::TextDisabled("No entity selected.");
                    ImGui::TextWrapped("Click any entity in the Scene Hierarchy to view and edit its components.");
                }
            }
            ImGui::End();
        }
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
        auto& tag = entity.GetComponent<TagComponent>().Tag;

        ImGuiTreeNodeFlags flags = ((m_SelectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0) 
                                 | ImGuiTreeNodeFlags_OpenOnArrow 
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;

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
            if (ImGui::TreeNodeEx((void*)typeid(MeshComponent).hash_code(), treeNodeFlags, "Mesh & Material")) {
                auto& mc = entity.GetComponent<MeshComponent>();

                const char* patternNames[] = { "0: World XZ Caro (Floor)", "1: Local 3D Caro (Box/Cyl)", "2: Spherical Caro (Sphere)" };
                int currentPattern = mc.PatternMode;
                if (ImGui::Combo("Pattern Mode", &currentPattern, patternNames, IM_ARRAYSIZE(patternNames))) {
                    mc.PatternMode = currentPattern;
                }

                ImGui::DragFloat("Tile Scale", &mc.TileScale, 0.1f, 0.1f, 64.0f, "%.1f");

                ImGui::ColorEdit4("Color Primary", glm::value_ptr(mc.Color1));
                ImGui::ColorEdit4("Color Secondary", glm::value_ptr(mc.Color2));

                ImGui::TreePop();
            }
            ImGui::Spacing();
        }

        // Rigid Body Component
        if (entity.HasComponent<RigidBodyComponent>()) {
            if (ImGui::TreeNodeEx((void*)typeid(RigidBodyComponent).hash_code(), treeNodeFlags, "Jolt Rigid Body")) {
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
            ImGui::Spacing();
        }
    }

}
