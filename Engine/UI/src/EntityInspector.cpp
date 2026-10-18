#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
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


}
