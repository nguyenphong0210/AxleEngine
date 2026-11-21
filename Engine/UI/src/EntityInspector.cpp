#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/PhysicsMaterial.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderBackendPolicy.hpp>
#include <Axle/RenderService.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Window.hpp>
#include <Axle/PlatformUtils.hpp>
#include <Axle/AudioEngine.hpp>
#include <Axle/AudioSource.hpp>
#include <Axle/AudioClip.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <Axle/ParticleSystem.hpp>
#include <Axle/CharacterVFX.hpp>
#include <Axle/UI/UIParser.hpp>
#include <Axle/UIVectorIcons.hpp>
#include <Axle/ShaderPropertyReflection.hpp>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <Jolt/Physics/Body/BodyLock.h>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <functional>

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



    static std::vector<std::string> DiscoverAxleUIAssets() {
        std::vector<std::string> result;
        const std::vector<std::filesystem::path> searchDirs = {
            FileSystem::GetAssetDirectory()
        };
        std::unordered_set<std::string> seen;
        for (const auto& dir : searchDirs) {
            if (!std::filesystem::exists(dir)) continue;
            try {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                    if (entry.is_regular_file() && (entry.path().extension() == ".axleui" || entry.path().extension() == ".axleasset")) {
                        std::string rel = entry.path().lexically_normal().generic_string();
                        if (seen.insert(rel).second) {
                            result.push_back(rel);
                        }
                    }
                }
            } catch (...) {}
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    template<typename T, typename UIFunction>
    static void DrawComponent(
        const std::string& name,
        Entity entity,
        UIFunction uiFunction,
        bool canRemove = true,
        std::function<void(T&)> onRemove = nullptr,
        bool canReset = false,
        std::function<void(T&)> onReset = nullptr
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
        float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4.0f, 4.0f });
        ImGui::Separator();

        ImVec2 rowStart = ImGui::GetCursorScreenPos();
        bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, "    %s", name.c_str());

        // Draw component vector icon
        float iconSize = 13.0f;
        float textOffsetX = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 3.0f;
        ImVec2 iconPos(rowStart.x + textOffsetX, rowStart.y + (lineHeight - iconSize) * 0.5f);
        UIIcons::DrawComponentVectorIcon(ImGui::GetWindowDrawList(), iconPos, iconSize, name);

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

        // Right-aligned settings button on header (Unity vertical ellipsis ⋮)
        ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.7f);
        std::string btnId = "...##" + name;
        if (ImGui::Button(btnId.c_str(), ImVec2{ lineHeight * 0.9f, lineHeight })) {
            ImGui::OpenPopup("ComponentSettings");
        }

        if (ImGui::BeginPopup("ComponentSettings")) {
            if (canReset) {
                if (ImGui::MenuItem("Reset")) {
                    if (onReset) onReset(component);
                    else component = T{};
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::MenuItem("Reset");
                ImGui::EndDisabled();
            }
            ImGui::Separator();
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

        // Tag Component (Unity Signature Entity Inspector Header)
        if (entity.HasComponent<TagComponent>()) {
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            char buffer[256];
            std::memset(buffer, 0, sizeof(buffer));
            std::strncpy(buffer, tag.c_str(), sizeof(buffer) - 1);

            // Active Checkbox + Large Entity Name
            static bool s_EntityActive = true;
            ImGui::Checkbox("##Active", &s_EntityActive);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable / Disable Entity");
            ImGui::SameLine();

            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##Tag", buffer, sizeof(buffer))) {
                tag = std::string(buffer);
            }
            ImGui::PopItemWidth();

            // Unity Subheader: Tag | Layer | Static
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
            ImGui::TextDisabled("Tag:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::BeginCombo("##EntityTagCombo", "Untagged")) {
                if (ImGui::Selectable("Untagged")) {}
                if (ImGui::Selectable("MainCamera")) {}
                if (ImGui::Selectable("Player")) {}
                if (ImGui::Selectable("Prop")) {}
                if (ImGui::Selectable("FinishLine")) {}
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::TextDisabled("Layer:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(85.0f);
            if (ImGui::BeginCombo("##EntityLayerCombo", "Default")) {
                if (ImGui::Selectable("Default")) {}
                if (ImGui::Selectable("TransparentFX")) {}
                if (ImGui::Selectable("Ignore Raycast")) {}
                if (ImGui::Selectable("Water")) {}
                if (ImGui::Selectable("UI")) {}
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            static bool s_Static = false;
            ImGui::Checkbox("Static", &s_Static);
            ImGui::PopStyleVar();

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
                changed = true;
            }

            if (changed) {
                entity.RecomputeWorldTransform();
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
                                mc.VAO = CreateVertexArrayFromMeshSource(loaded.SourceData);
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
                                mc.VAO = CreateVertexArrayFromMeshSource(loaded.SourceData);
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
                                loadedVao = CreateVertexArrayFromMeshSource(loaded.SourceData);
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
                                loadedVao = CreateVertexArrayFromMeshSource(loaded.SourceData);
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
                    ImGui::SetTooltip("Local Material: values are stored directly on this entity.\nDrag an .axleasset Material here to bind a shared asset.");
                }
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* pathStr = static_cast<const char*>(payload->Data);
                    std::filesystem::path droppedPath(pathStr);
                    std::string ext = droppedPath.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                    if (ext == ".axleasset" && AssetDSL::PeekAssetKindFromFile(droppedPath.string()) == AssetKind::Material) {
                        UUID handle = AssetManager::GetAssetUUID(droppedPath);
                        auto asset = MaterialCache::GetMaterial(droppedPath);
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

            const char* surfBadge = (mat.SurfaceType == Axle::SurfaceType::Opaque) ? "Opaque" :
                                    (mat.SurfaceType == Axle::SurfaceType::Cutout) ? "Cutout" :
                                    (mat.SurfaceType == Axle::SurfaceType::Transparent) ? "Transparent" : "Additive";

            ImGui::TextColored(typeColor, "[%s]", typeBadge);
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", finishBadge);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.6f, 1.0f), "| %s", surfBadge);
            if (mat.EmissionIntensity > 0.0f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "| Luminous (%.1f)", mat.EmissionIntensity);
            }
            ImGui::Spacing();

            std::string shaderName = mat.AssetPath.empty() ? "CustomShader" : std::filesystem::path(mat.AssetPath).stem().string();
            if (m_CustomShaderPicker.Draw("EntityMaterialShader", mat, shaderName, m_OnOpenHLSLStudioCallback)) {
                if (mat.HasAsset()) mat.SyncToAsset();
                mat.SyncToRHI();
            }

            if (m_MaterialInspectorControls.Draw(mat, m_HasActiveRenderSettings ? &m_ActiveRenderSettings : nullptr)) {
                if (mat.HasAsset()) mat.SyncToAsset();
                mat.SyncToRHI();
            }
        }, /*canRemove=*/ true, /*onRemove=*/ nullptr, /*canReset=*/ true, [](MaterialComponent& mat) {
            mat.ResetToDefault();
            if (mat.HasAsset()) mat.SyncToAsset();
        });

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


        // Directional Light Component
        DrawComponent<DirectionalLightComponent>("Directional Sun Light", entity, [&](DirectionalLightComponent& dlc) {
            const auto backendPolicy = RenderBackendPolicy::FromDevice(RHI::RHIDevice::Get().GetCapabilities());
            const auto resolvedView = backendPolicy.ResolveSettings(
                m_HasActiveRenderSettings ? m_ActiveRenderSettings : SceneRenderSettings{});
            auto tooltip = [](const char* text) {
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
            };
            // Light Color
            if (ImGui::ColorEdit3("Light Color", glm::value_ptr(dlc.Color))) {
                dlc.Color = glm::max(dlc.Color, glm::vec3(0.0f));
            }
            tooltip("Linear RGB directional light color.");

            // Intensity
            if (ImGui::SliderFloat("Intensity", &dlc.Intensity, 0.0f, 10.0f, "%.2f")) {
                dlc.Intensity = std::isfinite(dlc.Intensity) ? std::clamp(dlc.Intensity, 0.0f, 100.0f) : 1.0f;
            }
            tooltip("Sun illuminance multiplier [0..100]; 1.0 corresponds to nominal daylight reference.");

            // Ambient Intensity
            if (ImGui::SliderFloat("Ambient Fill", &dlc.AmbientIntensity, 0.0f, 1.0f, "%.2f")) {
                dlc.AmbientIntensity = std::isfinite(dlc.AmbientIntensity) ? std::clamp(dlc.AmbientIntensity, 0.0f, 1.0f) : 0.28f;
            }
            tooltip("Fallback indirect fill radiance weight [0..1] when IBL/GI is inactive.");

            // Sun Angles
            if (ImGui::SliderFloat("Azimuth (Heading)", &dlc.Azimuth, 0.0f, 360.0f, "%.1f deg")) {
                dlc.Azimuth = std::isfinite(dlc.Azimuth) ? std::clamp(dlc.Azimuth, 0.0f, 360.0f) : 45.0f;
            }
            tooltip("Horizontal compass bearing around world up (degrees).");
            if (ImGui::SliderFloat("Elevation (Altitude)", &dlc.Elevation, 5.0f, 89.0f, "%.1f deg")) {
                dlc.Elevation = std::isfinite(dlc.Elevation) ? std::clamp(dlc.Elevation, 5.0f, 89.0f) : 50.0f;
            }
            tooltip("Vertical angle above the horizon (degrees).");

            // Cast Shadows Toggle
            ImGui::Checkbox("Cast Real-Time Shadows", &dlc.CastShadows);
            tooltip("Enable 4-cascade directional shadow map generation.");
            if (dlc.CastShadows && !resolvedView.EnableShadows) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "Viewport Render FX currently has Real-Time Shadows disabled.");
            } else if (dlc.CastShadows && backendPolicy.GetQualityTier() == BackendQualityTier::Compatibility) {
                ImGui::TextDisabled("OpenGL Compatibility: Cascaded sun shadows active (local point/spot shadows omitted).");
            }

            if (dlc.CastShadows) {
                ImGui::Indent(15.0f);
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Cascaded Shadow Settings (4 Splits):");

                const char* shadowQualities[] = { "Low (1024x1024)", "High (2048x2048)", "Ultra (4096x4096)" };
                int currentQuality = glm::clamp(dlc.ShadowQuality, 0, 2);
                if (ImGui::Combo("Shadow Quality", &currentQuality, shadowQualities, IM_ARRAYSIZE(shadowQualities))) {
                    dlc.ShadowQuality = currentQuality;
                }
                tooltip("Cascade tile resolution for each of the 4 directional splits.");

                if (ImGui::SliderFloat("Max Shadow Distance", &dlc.MaxShadowDistance, 20.0f, 500.0f, "%.1f m")) {
                    dlc.MaxShadowDistance = std::isfinite(dlc.MaxShadowDistance) ? std::clamp(dlc.MaxShadowDistance, 20.0f, 500.0f) : 150.0f;
                }
                tooltip("Maximum view-space distance across all 4 cascades.");

                if (ImGui::SliderFloat("Cascade Split Lambda", &dlc.CascadeSplitLambda, 0.50f, 0.95f, "%.2f")) {
                    dlc.CascadeSplitLambda = std::isfinite(dlc.CascadeSplitLambda) ? std::clamp(dlc.CascadeSplitLambda, 0.50f, 0.95f) : 0.85f;
                }
                tooltip("PSSM practical split weight (logarithmic vs linear distribution). Higher values give denser detail near the camera.");

                if (ImGui::SliderFloat("PCF 5x5 Softness", &dlc.ShadowFilterRadius, 0.5f, 3.0f, "%.2f")) {
                    dlc.ShadowFilterRadius = std::isfinite(dlc.ShadowFilterRadius) ? std::clamp(dlc.ShadowFilterRadius, 0.5f, 3.0f) : 2.0f;
                }
                tooltip("5x5 Percentage-Closer Filtering (25 hardware bilinear taps) spread multiplier.");

                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Anti-Acne Depth Biasing:");

                if (ImGui::DragFloat("Base Depth Bias", &dlc.ShadowBias, 0.0001f, 0.0001f, 0.0100f, "%.5f")) {
                    dlc.ShadowBias = std::isfinite(dlc.ShadowBias) ? std::clamp(dlc.ShadowBias, 0.0001f, 0.0100f) : 0.0012f;
                }
                tooltip("Constant depth bias offset.");

                if (ImGui::DragFloat("Slope-Scaled Bias", &dlc.ShadowSlopeBias, 0.0001f, 0.0001f, 0.0200f, "%.5f")) {
                    dlc.ShadowSlopeBias = std::isfinite(dlc.ShadowSlopeBias) ? std::clamp(dlc.ShadowSlopeBias, 0.0001f, 0.0200f) : 0.0020f;
                }
                tooltip("Slope-scaled depth bias proportional to tan(theta) to eliminate shadow acne on grazing angles.");

                if (ImGui::DragFloat("Normal Offset Bias", &dlc.ShadowNormalBias, 0.001f, 0.001f, 0.050f, "%.4f")) {
                    dlc.ShadowNormalBias = std::isfinite(dlc.ShadowNormalBias) ? std::clamp(dlc.ShadowNormalBias, 0.001f, 0.050f) : 0.0150f;
                }
                tooltip("World-space normal offset bias to prevent self-shadow acne on curved surfaces without Peter Panning.");

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

            if (ImGui::SliderFloat("Exposure", &dlc.Exposure, 0.1f, 5.0f, "%.2f")) {
                dlc.Exposure = std::isfinite(dlc.Exposure) ? std::clamp(dlc.Exposure, 0.1f, 5.0f) : 1.0f;
            }
            tooltip("Manual HDR exposure multiplier [0.1..5.0] applied prior to tone mapping when Auto-Exposure is off.");

            const char* toneMappingNames[] = {
                "0: ACES Filmic (Cinematic)",
                "1: Reinhard (Standard)",
                "2: Uncharted 2 (Hable)",
                "3: None (Linear Bypass)"
            };
            int currentTM = std::clamp(dlc.ToneMappingMode, 0, 3);
            if (ImGui::Combo("Tone Mapping", &currentTM, toneMappingNames, IM_ARRAYSIZE(toneMappingNames))) {
                dlc.ToneMappingMode = currentTM;
            }
            tooltip("Select HDR-to-display tone mapping curve.");

            if (ImGui::SliderFloat("Display Gamma", &dlc.Gamma, 1.0f, 3.0f, "%.2f")) {
                dlc.Gamma = std::isfinite(dlc.Gamma) ? std::clamp(dlc.Gamma, 1.0f, 3.0f) : 2.2f;
            }
            tooltip("Display transfer gamma exponent (standard sRGB approximation: 2.2).");
            if (backendPolicy.GetQualityTier() == BackendQualityTier::Compatibility) {
                ImGui::TextDisabled("OpenGL Compatibility: Tone mapping passes use standard LDR output.");
            }

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
        }, /*canRemove=*/ true, nullptr, /*canReset=*/ true);

        // Skybox Component (Procedural Atmospheric vs HDR Cubemap IBL)
        DrawComponent<SkyboxComponent>("Skybox & Environment", entity, [&](SkyboxComponent& sbc) {
            auto tooltip = [](const char* text) {
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
            };
            ImGui::Checkbox("Enable Skybox", &sbc.Enabled);
            tooltip("Render this sky in the background.");
            ImGui::Checkbox("Environment Lighting", &sbc.EnvironmentLighting);
            tooltip("Allow this sky to provide ambient and image-based lighting to scene geometry.");
            if (!sbc.EnvironmentLighting) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                    "Background only: metallic surfaces need Environment Lighting or a direct light.");
            }

            const char* skyboxTypes[] = { "Procedural Atmospheric", "HDR Cubemap (IBL)" };
            int currentType = (sbc.Type == SkyboxType::HDRCubemap) ? 1 : 0;
            if (ImGui::Combo("Skybox Type", &currentType, skyboxTypes, IM_ARRAYSIZE(skyboxTypes))) {
                sbc.Type = (currentType == 1) ? SkyboxType::HDRCubemap : SkyboxType::ProceduralAtmospheric;
            }
            tooltip("Select procedural atmospheric sky or authored HDR equirectangular cubemap.");

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

                if (ImGui::SliderFloat("Radiance / Intensity", &sbc.Intensity, 0.0f, 5.0f, "%.2f")) {
                    sbc.Intensity = std::isfinite(sbc.Intensity) ? std::clamp(sbc.Intensity, 0.0f, 20.0f) : 1.0f;
                }
                tooltip("Scales both background luminance and baked IBL radiance.");
                if (ImGui::SliderFloat("Exposure", &sbc.Exposure, 0.1f, 5.0f, "%.2f")) {
                    sbc.Exposure = std::isfinite(sbc.Exposure) ? std::clamp(sbc.Exposure, 0.1f, 10.0f) : 1.0f;
                }
                tooltip("Pre-baked exposure multiplier applied when decoding the HDR environment.");
                if (ImGui::SliderFloat("Environment Rotation", &sbc.EnvironmentRotation, -360.0f, 360.0f, "%.1f deg")) {
                    sbc.EnvironmentRotation = std::isfinite(sbc.EnvironmentRotation) ? std::clamp(sbc.EnvironmentRotation, -360.0f, 360.0f) : 0.0f;
                }
                tooltip("Rotate authored HDRI around world up; procedural sun azimuth/elevation remain independent.");
            } else {
                ImGui::Checkbox("Hemisphere Ambient Lighting", &sbc.HemisphereAmbient);
                tooltip("Compute realistic ambient irradiance gradients (Zenith, Horizon, Ground) from atmospheric sky.");
                ImGui::Spacing();

                // Atmosphere Colors
                if (ImGui::ColorEdit3("Zenith Color", glm::value_ptr(sbc.ZenithColor))) {
                    sbc.ZenithColor = glm::max(sbc.ZenithColor, glm::vec3(0.0f));
                }
                tooltip("Upper hemisphere sky color at zenith.");
                if (ImGui::ColorEdit3("Horizon Color", glm::value_ptr(sbc.HorizonColor))) {
                    sbc.HorizonColor = glm::max(sbc.HorizonColor, glm::vec3(0.0f));
                }
                tooltip("Atmospheric horizon scatter tint.");
                if (ImGui::ColorEdit3("Ground Color", glm::value_ptr(sbc.GroundColor))) {
                    sbc.GroundColor = glm::max(sbc.GroundColor, glm::vec3(0.0f));
                }
                tooltip("Lower hemisphere ground bounce tint.");
                ImGui::Spacing();

                // Atmospheric Properties
                if (ImGui::SliderFloat("Turbidity (Haze)", &sbc.Turbidity, 0.2f, 4.0f, "%.2f")) {
                    sbc.Turbidity = std::isfinite(sbc.Turbidity) ? std::clamp(sbc.Turbidity, 0.2f, 4.0f) : 1.0f;
                }
                tooltip("Atmospheric scattering density / haze strength.");
                if (ImGui::SliderFloat("Sun Disk Size", &sbc.SunSize, 0.01f, 0.15f, "%.3f")) {
                    sbc.SunSize = std::isfinite(sbc.SunSize) ? std::clamp(sbc.SunSize, 0.01f, 0.15f) : 0.04f;
                }
                tooltip("Angular radius of the procedural solar disk.");
                if (ImGui::SliderFloat("Sun Glow (Corona)", &sbc.SunGlow, 0.02f, 0.80f, "%.2f")) {
                    sbc.SunGlow = std::isfinite(sbc.SunGlow) ? std::clamp(sbc.SunGlow, 0.02f, 0.80f) : 0.2f;
                }
                tooltip("Forward-scattering atmospheric halo around the sun.");
                ImGui::Spacing();

                // Sun Alignment
                ImGui::Checkbox("Sync with Directional Sun Light", &sbc.SyncWithSun);
                tooltip("Follow the primary DirectionalLightComponent sun vector automatically.");
                if (!sbc.SyncWithSun) {
                    if (ImGui::SliderFloat("Custom Sun Azimuth", &sbc.CustomAzimuth, 0.0f, 360.0f, "%.1f deg")) {
                        sbc.CustomAzimuth = std::isfinite(sbc.CustomAzimuth) ? std::clamp(sbc.CustomAzimuth, 0.0f, 360.0f) : 45.0f;
                    }
                    if (ImGui::SliderFloat("Custom Sun Elevation", &sbc.CustomElevation, 1.0f, 89.0f, "%.1f deg")) {
                        sbc.CustomElevation = std::isfinite(sbc.CustomElevation) ? std::clamp(sbc.CustomElevation, 1.0f, 89.0f) : 50.0f;
                    }
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
        }, /*canRemove=*/ true, nullptr, /*canReset=*/ true);

        const auto lightPolicy = RenderBackendPolicy::FromDevice(RHI::RHIDevice::Get().GetCapabilities());
        const auto resolvedViewSettings = lightPolicy.ResolveSettings(
            m_HasActiveRenderSettings ? m_ActiveRenderSettings : SceneRenderSettings{});
        auto inspectTooltip = [](const char* text) {
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
        };

        // Point Light Component
        auto drawLocalShadows = [&](LocalShadowSettings& s) {
            ImGui::Checkbox("Cast Shadows", &s.CastShadows);
            inspectTooltip("Enable real-time local shadow map atlas allocation on D3D12/Vulkan.");
            if (lightPolicy.GetQualityTier() == BackendQualityTier::Compatibility) {
                ImGui::TextDisabled("Local shadows require D3D12 or Vulkan.");
            } else if (s.CastShadows && !resolvedViewSettings.EnableShadows) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "Viewport Render FX currently has Real-Time Shadows disabled.");
            } else if (s.CastShadows) {
                ImGui::TextDisabled("Native local shadow budget: max %u Spot + %u Point lights within %.0f m.",
                    static_cast<unsigned>(resolvedViewSettings.MaxShadowedSpotLights),
                    static_cast<unsigned>(resolvedViewSettings.MaxShadowedPointLights),
                    resolvedViewSettings.LocalShadowDistance);
            }
            if (!s.CastShadows) return;
            if (ImGui::DragFloat("Shadow Bias (m)", &s.Bias, .0001f, 0.f, 1.f, "%.4f")) {
                s.Bias = std::isfinite(s.Bias) ? std::clamp(s.Bias, 0.0f, 1.0f) : 0.002f;
            }
            inspectTooltip("Constant world-space receiver depth offset against shadow acne.");
            if (ImGui::DragFloat("Shadow Slope Bias (m)", &s.SlopeBias, .0001f, 0.f, 1.f, "%.4f")) {
                s.SlopeBias = std::isfinite(s.SlopeBias) ? std::clamp(s.SlopeBias, 0.0f, 1.0f) : 0.005f;
            }
            inspectTooltip("Grazing-angle slope depth bias against shadow acne.");
            if (ImGui::DragFloat("Shadow Normal Bias (m)", &s.NormalBias, .0001f, 0.f, 1.f, "%.4f")) {
                s.NormalBias = std::isfinite(s.NormalBias) ? std::clamp(s.NormalBias, 0.0f, 1.0f) : 0.01f;
            }
            inspectTooltip("World-space surface normal offset applied when sampling local shadows.");
            if (ImGui::SliderFloat("Shadow Filter Radius", &s.FilterRadius, 0.f, 2.f)) {
                s.FilterRadius = std::isfinite(s.FilterRadius) ? std::clamp(s.FilterRadius, 0.0f, 2.0f) : 1.0f;
            }
            inspectTooltip("PCF filter kernel softness multiplier [0..2].");
            if (ImGui::DragFloat("Shadow Near Clip (m)", &s.NearPlane, .005f, .001f, 1.f, "%.3f")) {
                s.NearPlane = std::isfinite(s.NearPlane) ? std::clamp(s.NearPlane, 0.001f, 1.0f) : 0.05f;
            }
            inspectTooltip("Shadow projection near plane distance in meters.");
        };
        DrawComponent<PointLightComponent>("Point Light", entity, [&](PointLightComponent& plc) {
            if (ImGui::ColorEdit3("Light Color", glm::value_ptr(plc.Color))) {
                plc.Color = glm::max(plc.Color, glm::vec3(0.0f));
            }
            inspectTooltip("Linear RGB omnidirectional light color.");
            if (ImGui::DragFloat("Intensity", &plc.Intensity, 0.1f, 0.0f, 100.0f, "%.2f")) {
                plc.Intensity = std::isfinite(plc.Intensity) ? std::clamp(plc.Intensity, 0.0f, 1000.0f) : 1.0f;
            }
            inspectTooltip("Luminous intensity multiplier (>= 0).");
            if (ImGui::DragFloat("Radius", &plc.Radius, 0.5f, 0.1f, 500.0f, "%.1f m")) {
                plc.Radius = std::isfinite(plc.Radius) ? std::clamp(plc.Radius, 0.1f, 500.0f) : 10.0f;
            }
            inspectTooltip("Effective attenuation cutoff radius in meters.");
            if (ImGui::DragFloat("Falloff", &plc.Falloff, 0.05f, 0.1f, 10.0f, "%.2f")) {
                plc.Falloff = std::isfinite(plc.Falloff) ? std::clamp(plc.Falloff, 0.1f, 10.0f) : 1.0f;
            }
            inspectTooltip("Distance attenuation shaping exponent [0.1..10].");
            drawLocalShadows(plc.Shadows);
        }, /*canRemove=*/ true, nullptr, /*canReset=*/ true);

        // Spot Light Component
        DrawComponent<SpotLightComponent>("Spot Light", entity, [&](SpotLightComponent& slc) {
            ImGui::Checkbox("Enabled", &slc.Enabled);
            if (ImGui::ColorEdit3("Light Color", glm::value_ptr(slc.Color))) {
                slc.Color = glm::max(slc.Color, glm::vec3(0.0f));
            }
            inspectTooltip("Linear RGB spotlight cone color.");
            if (ImGui::DragFloat("Intensity", &slc.Intensity, 0.1f, 0.0f, 100.0f, "%.2f")) {
                slc.Intensity = std::isfinite(slc.Intensity) ? std::clamp(slc.Intensity, 0.0f, 1000.0f) : 8.0f;
            }
            inspectTooltip("Luminous intensity multiplier (>= 0).");
            if (ImGui::DragFloat("Range", &slc.Range, 0.5f, 0.1f, 500.0f, "%.1f m")) {
                slc.Range = std::isfinite(slc.Range) ? std::clamp(slc.Range, 0.1f, 500.0f) : 35.0f;
            }
            inspectTooltip("Spotlight cutoff distance in meters.");
            if (ImGui::DragFloat("Inner Cone Angle", &slc.InnerConeAngle, 0.5f, 1.0f, 89.0f, "%.1f deg")) {
                slc.InnerConeAngle = std::isfinite(slc.InnerConeAngle) ? std::clamp(slc.InnerConeAngle, 1.0f, 89.0f) : 18.0f;
                slc.OuterConeAngle = std::max(slc.OuterConeAngle, slc.InnerConeAngle);
            }
            inspectTooltip("Full-brightness inner cone half-angle in degrees.");
            if (ImGui::DragFloat("Outer Cone Angle", &slc.OuterConeAngle, 0.5f, slc.InnerConeAngle, 89.0f, "%.1f deg")) {
                slc.OuterConeAngle = std::isfinite(slc.OuterConeAngle) ? std::clamp(slc.OuterConeAngle, slc.InnerConeAngle, 89.0f) : 32.0f;
            }
            inspectTooltip("Zero-falloff outer cone half-angle (>= Inner Cone Angle).");
            drawLocalShadows(slc.Shadows);
        }, /*canRemove=*/ true, nullptr, /*canReset=*/ true);

        // Rect / Area Light Component (LTC)
        DrawComponent<RectLightComponent>("Area Light (LTC)", entity, [&](RectLightComponent& rlc) {
            ImGui::Checkbox("Enabled", &rlc.Enabled);
            const char* shapeLabels[] = { "Rectangle", "Disk" };
            int shapeIdx = (rlc.Shape == AreaLightShape::Disk) ? 1 : 0;
            if (ImGui::Combo("Shape", &shapeIdx, shapeLabels, 2)) {
                rlc.Shape = (shapeIdx == 1) ? AreaLightShape::Disk : AreaLightShape::Rectangle;
            }
            inspectTooltip("Analytic Linearly Transformed Cosines (LTC) emitter geometry.");
            if (ImGui::ColorEdit3("Light Color", glm::value_ptr(rlc.Color))) {
                rlc.Color = glm::max(rlc.Color, glm::vec3(0.0f));
            }
            inspectTooltip("Linear RGB area emitter color.");
            if (ImGui::DragFloat("Intensity", &rlc.Intensity, 0.1f, 0.0f, 100.0f, "%.2f")) {
                rlc.Intensity = std::isfinite(rlc.Intensity) ? std::clamp(rlc.Intensity, 0.0f, 1000.0f) : 10.0f;
            }
            inspectTooltip("Radiant intensity multiplier (>= 0).");
            if (ImGui::DragFloat("Width", &rlc.Width, 0.05f, 0.01f, 100.0f, "%.2f m")) {
                rlc.Width = std::isfinite(rlc.Width) ? std::clamp(rlc.Width, 0.01f, 100.0f) : 2.0f;
            }
            inspectTooltip("Emitter full width in meters.");
            if (ImGui::DragFloat("Height", &rlc.Height, 0.05f, 0.01f, 100.0f, "%.2f m")) {
                rlc.Height = std::isfinite(rlc.Height) ? std::clamp(rlc.Height, 0.01f, 100.0f) : 1.0f;
            }
            inspectTooltip("Emitter full height in meters (Disk uses Width/Height ellipsoid axes).");
            if (ImGui::DragFloat("Range", &rlc.Range, 0.5f, 0.1f, 500.0f, "%.1f m")) {
                rlc.Range = std::isfinite(rlc.Range) ? std::clamp(rlc.Range, 0.1f, 500.0f) : 20.0f;
            }
            inspectTooltip("Smooth distance cutoff range in meters.");
            ImGui::Checkbox("Two-Sided", &rlc.TwoSided);
            inspectTooltip("Emit radiance from both front and back faces of the area emitter.");
            ImGui::TextDisabled("Shadow Policy: Unshadowed Analytic LTC");
            if (lightPolicy.GetQualityTier() == BackendQualityTier::Compatibility) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "OpenGL Compatibility: Area lights (LTC) execute on D3D12 or Vulkan.");
            }
        }, /*canRemove=*/ true, nullptr, /*canReset=*/ true);

        // Planar Reflection Component (M15-U5-46)
        DrawComponent<PlanarReflectionComponent>("Planar Reflection", entity, [&](PlanarReflectionComponent& prc) {
            ImGui::Checkbox("Enabled", &prc.Enabled);
            if (lightPolicy.GetQualityTier() == BackendQualityTier::Compatibility) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "OpenGL Compatibility: Planar reflections require D3D12 or Vulkan.");
            } else if (resolvedViewSettings.MSAASamples > 1) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "Single-sample pass: Planar reflection resolve is disabled while viewport MSAA > 1x.");
            } else if (!resolvedViewSettings.EnablePlanarReflections) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "Planar Reflections toggle is currently disabled in viewport Render FX.");
            }
            if (ImGui::DragFloat2("Half Extents (m)", glm::value_ptr(prc.HalfExtents), 0.1f, 0.05f, 500.0f, "%.2f")) {
                prc.HalfExtents = glm::clamp(glm::vec2(
                    std::isfinite(prc.HalfExtents.x) ? prc.HalfExtents.x : 5.0f,
                    std::isfinite(prc.HalfExtents.y) ? prc.HalfExtents.y : 5.0f), glm::vec2(0.05f), glm::vec2(500.0f));
            }
            inspectTooltip("Planar reflector plane half-width and half-length in meters.");
            if (ImGui::SliderFloat("Resolution Scale", &prc.ResolutionScale,
                    RHI::RHIViewTargets::MinPlanarResolutionScale, RHI::RHIViewTargets::MaxPlanarResolutionScale, "%.2f")) {
                prc.ResolutionScale = std::isfinite(prc.ResolutionScale)
                    ? std::clamp(prc.ResolutionScale, RHI::RHIViewTargets::MinPlanarResolutionScale, RHI::RHIViewTargets::MaxPlanarResolutionScale)
                    : 0.5f;
            }
            inspectTooltip("Offscreen reflection capture scale relative to main viewport [0.25..1.0].");
            if (ImGui::DragFloat("Clip Plane Offset", &prc.ClipPlaneOffset, 0.005f, -1.0f, 1.0f, "%.3f")) {
                prc.ClipPlaneOffset = std::isfinite(prc.ClipPlaneOffset) ? std::clamp(prc.ClipPlaneOffset, -1.0f, 1.0f) : 0.0f;
            }
            inspectTooltip("Oblique near-plane offset along reflector normal to avoid seam clipping artifacts.");
            if (ImGui::SliderFloat("Edge Fade (m)", &prc.EdgeFadeDistance, 0.0f, 5.0f, "%.2f")) {
                prc.EdgeFadeDistance = std::isfinite(prc.EdgeFadeDistance) ? std::clamp(prc.EdgeFadeDistance, 0.0f, 5.0f) : 0.15f;
            }
            inspectTooltip("Distance from extent border over which planar reflection fades into SSR/probe/sky fallback.");
            if (ImGui::SliderFloat("Max Roughness", &prc.MaxRoughness, 0.02f, 1.0f, "%.2f")) {
                prc.MaxRoughness = std::isfinite(prc.MaxRoughness) ? std::clamp(prc.MaxRoughness, 0.02f, 1.0f) : 0.45f;
            }
            inspectTooltip("Surface roughness cutoff above which planar reflections blend toward probe/sky fallback.");
            if (ImGui::SliderFloat("Intensity", &prc.Intensity, 0.0f, 2.0f, "%.2f")) {
                prc.Intensity = std::isfinite(prc.Intensity) ? std::clamp(prc.Intensity, 0.0f, 2.0f) : 1.0f;
            }
            inspectTooltip("Planar reflection radiance weight [0..2].");
            if (ImGui::DragFloat("Capture Near", &prc.CaptureNearPlane, 0.01f, 0.01f, 10.0f, "%.3f")) {
                prc.CaptureNearPlane = std::isfinite(prc.CaptureNearPlane) ? std::clamp(prc.CaptureNearPlane, 0.01f, 10.0f) : 0.1f;
                prc.CaptureFarPlane = std::max(prc.CaptureFarPlane, prc.CaptureNearPlane + 0.1f);
            }
            if (ImGui::DragFloat("Capture Far", &prc.CaptureFarPlane, 1.0f, 1.0f, 10000.0f, "%.1f")) {
                prc.CaptureFarPlane = std::isfinite(prc.CaptureFarPlane)
                    ? std::clamp(prc.CaptureFarPlane, std::max(1.0f, prc.CaptureNearPlane + 0.1f), 10000.0f)
                    : 1000.0f;
            }
            ImGui::Checkbox("Capture Shadows", &prc.CaptureShadows);
            ImGui::Checkbox("Capture Skybox", &prc.CaptureSkybox);
            ImGui::Checkbox("Clip To Extents", &prc.ClipToExtents);
            inspectTooltip("Restrict planar reflection influence to the authored reflector box extents.");

            const char* modeItems[] = { "Automatic", "Manual" };
            int modeIdx = (prc.CaptureMode == PlanarReflectionCaptureMode::Manual) ? 1 : 0;
            if (ImGui::Combo("Capture Mode", &modeIdx, modeItems, 2)) {
                prc.CaptureMode = (modeIdx == 1)
                    ? PlanarReflectionCaptureMode::Manual
                    : PlanarReflectionCaptureMode::Automatic;
            }
            inspectTooltip("Automatic recaptures when scene/camera changes; Manual updates only on Capture Now.");

            const uint64_t entityKeyID = static_cast<uint64_t>(static_cast<uint32_t>(entity));
            const UUID entityUUID = entity.GetUUID();
            auto& renderService = RenderService::Get();
            if (const auto status = renderService.GetPlanarReflectionStatus(entityKeyID, entityUUID)) {
                const char* stateLabel = "Idle";
                switch (status->State) {
                    case PlanarCaptureLifecycleState::Idle: stateLabel = "Idle"; break;
                    case PlanarCaptureLifecycleState::Ready: stateLabel = "Ready"; break;
                    case PlanarCaptureLifecycleState::Stale: stateLabel = "Stale"; break;
                    case PlanarCaptureLifecycleState::Failed: stateLabel = "Failed"; break;
                }
                ImGui::TextDisabled("Status: %s | Gen %llu | %ux%u",
                    stateLabel,
                    static_cast<unsigned long long>(status->ActiveGenerationID),
                    status->TargetWidth,
                    status->TargetHeight);
                if (status->PreservedPreviousGeneration) {
                    ImGui::TextDisabled("Fallback: Last-good capture preserved");
                }
                if (!status->LastDiagnostic.empty()) {
                    ImGui::TextDisabled("Note: %s", status->LastDiagnostic.c_str());
                }
            } else {
                ImGui::TextDisabled("Status: Idle (no active render status yet)");
            }

            if (ImGui::Button("Capture Now")) {
                renderService.RequestPlanarReflectionCapture(entityKeyID, entityUUID);
            }
            ImGui::SameLine();
            if (ImGui::Button("Invalidate")) {
                renderService.InvalidatePlanarReflection(entityKeyID, entityUUID, "inspector_manual_invalidate");
            }
        }, /*canRemove=*/ true, nullptr, /*canReset=*/ true);

        // Reflection Probe Component (M15-U5 / M15-U10-02.1..02.3)
        DrawComponent<ReflectionProbeComponent>("Reflection Probe", entity, [&](ReflectionProbeComponent& rpc) {
            ImGui::Checkbox("Enabled", &rpc.Enabled);
            inspectTooltip("Enables this reflection probe for local IBL specular and diffuse fallback.");

            if (lightPolicy.GetQualityTier() == BackendQualityTier::Compatibility) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "OpenGL Compatibility: Local reflection probes and dynamic probe captures execute on D3D12 or Vulkan.");
            } else if (rpc.DynamicCapture && !rpc.CubemapPath.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "Static CubemapPath is assigned: Dynamic Capture stays idle until CubemapPath is cleared.");
            }

            const char* shapeItems[] = { "Box", "Sphere" };
            int shapeIdx = (rpc.Shape == ProbeShape::Sphere) ? 1 : 0;
            if (ImGui::Combo("Volume Shape", &shapeIdx, shapeItems, 2)) {
                rpc.Shape = (shapeIdx == 1) ? ProbeShape::Sphere : ProbeShape::Box;
            }
            inspectTooltip("Box uses oriented box extents + parallax proxy bounds; Sphere uses radial distance influence.");

            if (rpc.Shape == ProbeShape::Box) {
                if (ImGui::DragFloat3("Box Extents (m)", glm::value_ptr(rpc.BoxExtents), 0.1f, 0.1f, 5000.0f, "%.2f")) {
                    rpc.BoxExtents = glm::clamp(glm::vec3(
                        std::isfinite(rpc.BoxExtents.x) ? rpc.BoxExtents.x : 10.0f,
                        std::isfinite(rpc.BoxExtents.y) ? rpc.BoxExtents.y : 10.0f,
                        std::isfinite(rpc.BoxExtents.z) ? rpc.BoxExtents.z : 10.0f), glm::vec3(0.1f), glm::vec3(5000.0f));
                }
                inspectTooltip("Full axis extents (width, height, depth) of the probe influence volume in meters.");
            } else {
                if (ImGui::DragFloat("Radius (m)", &rpc.Radius, 0.1f, 0.1f, 5000.0f, "%.2f")) {
                    rpc.Radius = std::isfinite(rpc.Radius) ? std::clamp(rpc.Radius, 0.1f, 5000.0f) : 10.0f;
                }
                inspectTooltip("Inner core radius of the spherical probe influence volume in meters.");
            }

            if (ImGui::DragFloat3("Local Offset (m)", glm::value_ptr(rpc.BoxOffset), 0.05f, -5000.0f, 5000.0f, "%.2f")) {
                rpc.BoxOffset = glm::clamp(glm::vec3(
                    std::isfinite(rpc.BoxOffset.x) ? rpc.BoxOffset.x : 0.0f,
                    std::isfinite(rpc.BoxOffset.y) ? rpc.BoxOffset.y : 0.0f,
                    std::isfinite(rpc.BoxOffset.z) ? rpc.BoxOffset.z : 0.0f), glm::vec3(-5000.0f), glm::vec3(5000.0f));
            }
            inspectTooltip("Local-space offset of the capture origin and influence bounds relative to transform.");

            if (ImGui::DragFloat("Blend Distance (m)", &rpc.BlendDistance, 0.05f, 0.0f, 500.0f, "%.2f")) {
                rpc.BlendDistance = std::isfinite(rpc.BlendDistance) ? std::clamp(rpc.BlendDistance, 0.0f, 500.0f) : 0.0f;
            }
            inspectTooltip("Outer smoothstep falloff margin into neighboring probes/skybox (0 = auto margin).");

            if (ImGui::SliderFloat("Intensity", &rpc.Intensity, 0.0f, 20.0f, "%.2f")) {
                rpc.Intensity = std::isfinite(rpc.Intensity) ? std::clamp(rpc.Intensity, 0.0f, 20.0f) : 1.0f;
            }
            inspectTooltip("Probe radiance intensity multiplier [0..20].");

            ImGui::Checkbox("Parallax Corrected", &rpc.ParallaxCorrected);
            inspectTooltip("Reprojects specular sample directions against the probe bounds for localized reflections.");

            ImGui::Separator();
            std::string cubemapDisplay = rpc.CubemapPath.empty() ? "(None - Use Dynamic / Procedural)" : rpc.CubemapPath;
            ImGui::Text("Baked Cubemap");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.19f, 0.22f, 1.0f));
            ImGui::Button((cubemapDisplay + "##ProbeCubemapSlot").c_str(), ImVec2(170.0f, 0.0f));
            ImGui::PopStyleColor();
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
                    const char* pathStr = static_cast<const char*>(payload->Data);
                    std::filesystem::path droppedPath(pathStr ? pathStr : "");
                    std::string ext = droppedPath.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".hdr" || ext == ".exr" || ext == ".png" || ext == ".jpg") {
                        auto assetDir = FileSystem::GetAssetDirectory();
                        if (!assetDir.empty()) {
                            std::error_code ec;
                            auto rel = std::filesystem::relative(droppedPath, assetDir, ec);
                            rpc.CubemapPath = (!ec && !rel.empty()) ? PathToUtf8(rel) : PathToUtf8(droppedPath);
                        } else {
                            rpc.CubemapPath = PathToUtf8(droppedPath);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::Button("...##BrowseProbeCubemap")) {
                std::string selected = FileDialogs::OpenFile("Radiance HDR / Image (*.hdr;*.exr;*.png;*.jpg)\0*.hdr;*.exr;*.png;*.jpg\0All Files (*.*)\0*.*\0");
                if (!selected.empty()) {
                    auto assetDir = FileSystem::GetAssetDirectory();
                    auto chosen = PathFromUtf8(selected);
                    std::error_code ec;
                    auto rel = !assetDir.empty() ? std::filesystem::relative(chosen, assetDir, ec) : std::filesystem::path{};
                    rpc.CubemapPath = (!ec && !rel.empty() && rel.native().rfind(L"..", 0) != 0)
                        ? PathToUtf8(rel) : selected;
                }
            }
            if (!rpc.CubemapPath.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear##ClearProbeCubemap")) {
                    rpc.CubemapPath.clear();
                }
            }

            ImGui::Checkbox("Dynamic Capture", &rpc.DynamicCapture);
            inspectTooltip("Captures the live scene into a 6-face cubemap with GGX prefiltered mip levels.");

            if (rpc.DynamicCapture) {
                static constexpr uint32_t kResValues[] = { 32u, 64u, 128u, 256u, 512u };
                const char* resLabels[] = { "32 x 32", "64 x 64", "128 x 128", "256 x 256", "512 x 512" };
                int resIdx = 2;
                for (int i = 0; i < 5; ++i) {
                    if (rpc.CaptureResolution <= kResValues[i]) { resIdx = i; break; }
                    resIdx = 4;
                }
                if (ImGui::Combo("Capture Resolution", &resIdx, resLabels, 5)) {
                    rpc.CaptureResolution = kResValues[resIdx];
                }
                inspectTooltip("Per-face dynamic cubemap capture resolution [32..512].");

                ImGui::Checkbox("Capture Shadows", &rpc.CaptureShadows);
                inspectTooltip("Includes shadowed direct lighting while rendering probe faces.");

                if (ImGui::DragFloat("Capture Near", &rpc.CaptureNearPlane, 0.01f, 0.01f, 10.0f, "%.3f")) {
                    rpc.CaptureNearPlane = std::isfinite(rpc.CaptureNearPlane)
                        ? std::clamp(rpc.CaptureNearPlane, 0.01f, 10.0f) : 0.1f;
                    rpc.CaptureFarPlane = std::max(rpc.CaptureFarPlane, rpc.CaptureNearPlane + 0.1f);
                }
                if (ImGui::DragFloat("Capture Far", &rpc.CaptureFarPlane, 1.0f, 1.0f, 10000.0f, "%.1f")) {
                    rpc.CaptureFarPlane = std::isfinite(rpc.CaptureFarPlane)
                        ? std::clamp(rpc.CaptureFarPlane, std::max(1.0f, rpc.CaptureNearPlane + 0.1f), 10000.0f)
                        : 100.0f;
                }

                const char* probeModeItems[] = { "Automatic", "Manual" };
                int probeModeIdx = (rpc.CaptureMode == ReflectionProbeCaptureMode::Manual) ? 1 : 0;
                if (ImGui::Combo("Capture Mode", &probeModeIdx, probeModeItems, 2)) {
                    rpc.CaptureMode = (probeModeIdx == 1)
                        ? ReflectionProbeCaptureMode::Manual
                        : ReflectionProbeCaptureMode::Automatic;
                }
                inspectTooltip("Automatic schedules incremental recaptures on scene edits; Manual updates only on Capture Now.");

                const uint64_t entityKeyID = static_cast<uint64_t>(static_cast<uint32_t>(entity));
                const UUID entityUUID = entity.GetUUID();
                auto& renderService = RenderService::Get();
                const auto probeStatus = renderService.GetDynamicReflectionProbeStatus(entityKeyID, entityUUID);
                const auto cacheStats = renderService.GetDynamicReflectionCaptureCacheStats();

                if (probeStatus) {
                    const char* stateLabel = "Idle";
                    ImVec4 stateColor(0.70f, 0.72f, 0.76f, 1.0f);
                    switch (probeStatus->State) {
                        case DynamicCaptureLifecycleState::Idle:
                            stateLabel = "Idle";
                            break;
                        case DynamicCaptureLifecycleState::Pending:
                            stateLabel = "Pending";
                            stateColor = ImVec4(0.38f, 0.80f, 0.98f, 1.0f);
                            break;
                        case DynamicCaptureLifecycleState::Ready:
                            stateLabel = "Ready";
                            stateColor = ImVec4(0.35f, 0.90f, 0.45f, 1.0f);
                            break;
                        case DynamicCaptureLifecycleState::Stale:
                            stateLabel = "Stale";
                            stateColor = ImVec4(0.95f, 0.72f, 0.28f, 1.0f);
                            break;
                        case DynamicCaptureLifecycleState::Failed:
                            stateLabel = "Failed";
                            stateColor = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
                            break;
                    }
                    ImGui::TextColored(stateColor, "Status: %s (%u/%u faces) | Gen %llu | %.2f MB",
                        stateLabel,
                        probeStatus->CompletedFaces,
                        probeStatus->TotalFaces,
                        static_cast<unsigned long long>(probeStatus->ActiveGenerationID),
                        static_cast<double>(probeStatus->CachedBytes) / (1024.0 * 1024.0));

                    if (probeStatus->PendingInvalidation.Any()) {
                        std::string reasons;
                        auto appendReason = [&](bool flag, const char* label) {
                            if (!flag) return;
                            if (!reasons.empty()) reasons += ", ";
                            reasons += label;
                        };
                        appendReason(probeStatus->PendingInvalidation.TransformChanged, "Transform");
                        appendReason(probeStatus->PendingInvalidation.SceneRevisionChanged, "SceneRev");
                        appendReason(probeStatus->PendingInvalidation.GeometryChanged, "Geometry");
                        appendReason(probeStatus->PendingInvalidation.MaterialChanged, "Material");
                        appendReason(probeStatus->PendingInvalidation.LightingChanged, "Lighting");
                        appendReason(probeStatus->PendingInvalidation.AssetChanged, "Asset");
                        appendReason(probeStatus->PendingInvalidation.SettingsChanged, "Settings");
                        appendReason(probeStatus->PendingInvalidation.DeviceEpochChanged, "DeviceEpoch");
                        appendReason(probeStatus->PendingInvalidation.ManualRequested, "Manual");
                        ImGui::TextDisabled("Dirty Domains: %s", reasons.c_str());
                    }
                    if (probeStatus->ManualRefreshRequired) {
                        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                            "Manual refresh pending — click Capture Now to recapture.");
                    }
                    if (probeStatus->Failed) {
                        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                            "Capture Failed (%u failure(s)): %s",
                            probeStatus->FailureCount,
                            probeStatus->LastDiagnostic.empty() ? "unknown capture failure" : probeStatus->LastDiagnostic.c_str());
                    } else if (!probeStatus->LastDiagnostic.empty()) {
                        ImGui::TextDisabled("Diagnostic: %s", probeStatus->LastDiagnostic.c_str());
                    }
                } else {
                    ImGui::TextDisabled("Status: Idle (render a frame or click Capture Now)");
                }

                ImGui::TextDisabled("Scheduler Cache: %u probes | %.1f / %.1f MB | Frame: %u probe(s), %u face(s) | Evictions: %llu",
                    cacheStats.CachedProbeCount,
                    static_cast<double>(cacheStats.UsedBytes) / (1024.0 * 1024.0),
                    static_cast<double>(cacheStats.BudgetBytes) / (1024.0 * 1024.0),
                    cacheStats.LastScheduledProbes,
                    cacheStats.LastScheduledFaces,
                    static_cast<unsigned long long>(cacheStats.EvictionCount));
                if (m_HasActiveRenderSettings) {
                    ImGui::TextDisabled("Frame Budget: %u probes/frame | %u faces/frame (Render FX / Project Settings)",
                        resolvedViewSettings.ReflectionCaptureMaxProbesPerFrame,
                        resolvedViewSettings.ReflectionCaptureMaxFacesPerFrame);
                }

                if (ImGui::Button("Capture Now##ProbeCaptureNow")) {
                    renderService.RequestDynamicReflectionCapture(entityKeyID, entityUUID);
                }
                ImGui::SameLine();
                if (ImGui::Button("Invalidate##ProbeInvalidate")) {
                    renderService.InvalidateDynamicReflectionProbe(entityKeyID, entityUUID, "inspector_manual_invalidate");
                }
                if (probeStatus && probeStatus->Failed) {
                    ImGui::SameLine();
                    if (ImGui::Button("Retry Capture##ProbeRetry")) {
                        renderService.RequestDynamicReflectionCapture(entityKeyID, entityUUID);
                    }
                }
            }
        }, /*canRemove=*/ true, nullptr, /*canReset=*/ true);

        // Camera Component
        DrawComponent<CameraComponent>("Camera", entity, [&](CameraComponent& cam) {
            if (ImGui::DragFloat("Field of View", &cam.FOV, 0.5f, 10.0f, 150.0f, "%.1f deg")) {
                cam.FOV = std::isfinite(cam.FOV) ? std::clamp(cam.FOV, 10.0f, 150.0f) : 45.0f;
            }
            if (ImGui::DragFloat("Near Clip", &cam.NearClip, 0.01f, 0.001f, 10.0f, "%.3f m")) {
                cam.NearClip = std::isfinite(cam.NearClip) ? std::clamp(cam.NearClip, 0.001f, 10.0f) : 0.1f;
                cam.FarClip = std::max(cam.FarClip, cam.NearClip + 0.1f);
            }
            if (ImGui::DragFloat("Far Clip", &cam.FarClip, 1.0f, 1.0f, 10000.0f, "%.1f m")) {
                cam.FarClip = std::isfinite(cam.FarClip) ? std::clamp(cam.FarClip, std::max(1.0f, cam.NearClip + 0.1f), 10000.0f) : 1000.0f;
            }
            ImGui::Checkbox("Primary Camera", &cam.Primary);
        }, /*canRemove=*/ true, nullptr, /*canReset=*/ true);

        // Volumetric Fog Component (3D Froxel Atmospheric Media)
        DrawComponent<VolumetricFogComponent>("Volumetric Fog", entity, [&](VolumetricFogComponent& vfc) {
            ImGui::Checkbox("Enabled", &vfc.Enabled);
            if (!lightPolicy.Supports(BackendFeature::Compute) ||
                !lightPolicy.Supports(BackendFeature::StorageTextures) ||
                !lightPolicy.Supports(BackendFeature::HDRFormats)) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "3D Froxel Volumetric Fog requires D3D12 or Vulkan with compute & linear HDR targets.");
            } else if (resolvedViewSettings.MSAASamples > 1) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "Single-sample pass: Volumetric Fog is disabled while viewport MSAA > 1x.");
            } else if (m_HasActiveRenderSettings && !resolvedViewSettings.EnableVolumetricFog) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "Viewport Render FX override currently disables Volumetric Fog.");
            }
            if (ImGui::DragFloat("Density", &vfc.Density, 0.002f, 0.0f, 5.0f, "%.4f 1/m")) {
                vfc.Density = std::isfinite(vfc.Density) ? std::clamp(vfc.Density, 0.0f, 5.0f) : 0.02f;
            }
            inspectTooltip("Base extinction density [0..5] in inverse meters.");
            if (ImGui::DragFloat("Height Falloff", &vfc.HeightFalloff, 0.005f, 0.0f, 5.0f, "%.3f 1/m")) {
                vfc.HeightFalloff = std::isfinite(vfc.HeightFalloff) ? std::clamp(vfc.HeightFalloff, 0.0f, 5.0f) : 0.1f;
            }
            inspectTooltip("Exponential vertical density decay above Y = 0.");
            if (ImGui::SliderFloat("Anisotropy (g)", &vfc.ScatteringAsymmetry, -0.95f, 0.95f, "%.2f")) {
                vfc.ScatteringAsymmetry = std::isfinite(vfc.ScatteringAsymmetry) ? std::clamp(vfc.ScatteringAsymmetry, -0.95f, 0.95f) : 0.7f;
            }
            inspectTooltip("Henyey-Greenstein phase asymmetry (-0.95=backscatter, 0=isotropic, +0.95=forward Mie).");
            if (ImGui::ColorEdit3("Albedo", glm::value_ptr(vfc.Color))) {
                vfc.Color = glm::clamp(vfc.Color, glm::vec3(0.0f), glm::vec3(1.0f));
            }
            inspectTooltip("Single-scattering particle albedo [0..1].");
            if (ImGui::DragFloat("Max Distance", &vfc.MaxDistance, 1.0f, 1.0f, 1000.0f, "%.1f m")) {
                vfc.MaxDistance = std::isfinite(vfc.MaxDistance) ? std::clamp(vfc.MaxDistance, 1.0f, 1000.0f) : 100.0f;
            }
            inspectTooltip("Far depth bound of the 3D froxel frustum grid in meters [1..1000].");
            if (ImGui::DragFloat("Intensity", &vfc.Intensity, 0.05f, 0.0f, 10.0f, "%.2f")) {
                vfc.Intensity = std::isfinite(vfc.Intensity) ? std::clamp(vfc.Intensity, 0.0f, 10.0f) : 1.0f;
            }
            inspectTooltip("Scales the density of the scene fog. The Editor viewport can override its visibility.");
        }, /*canRemove=*/ true, nullptr, /*canReset=*/ true);

        // DDGI Probe Grid Component (Experimental Dynamic Diffuse GI)
        DrawComponent<DDGIProbeGridComponent>("DDGI Probe Grid (Experimental)", entity, [&](DDGIProbeGridComponent& ddgi) {
            ImGui::Checkbox("Enabled", &ddgi.Enabled);
            if (!lightPolicy.Supports(BackendFeature::Compute) ||
                !lightPolicy.Supports(BackendFeature::StorageTextures)) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "Experimental DDGI requires D3D12 or Vulkan with compute & storage textures.");
            } else if (resolvedViewSettings.MSAASamples > 1) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                    "Single-sample pass: DDGI diffuse GI is disabled while viewport MSAA > 1x.");
            } else if (m_HasActiveRenderSettings && !resolvedViewSettings.EnableDDGI) {
                ImGui::TextDisabled("Experimental DDGI is disabled by current viewport Render FX / preset.");
            }
            if (ImGui::DragInt3("Grid Dimensions", glm::value_ptr(ddgi.GridDimensions), 1.0f, 1, 64)) {
                ddgi.GridDimensions = glm::clamp(ddgi.GridDimensions, glm::ivec3(1), glm::ivec3(64));
            }
            inspectTooltip("Probe count along X, Y, Z axes (capped by 16384 total probes).");
            if (ImGui::DragFloat3("Grid Spacing", glm::value_ptr(ddgi.GridSpacing), 0.1f, 0.05f, 100.0f, "%.2f m")) {
                ddgi.GridSpacing = glm::clamp(ddgi.GridSpacing, glm::vec3(0.05f), glm::vec3(100.0f));
            }
            inspectTooltip("World-space interval between neighboring probes in meters.");
            ImGui::DragFloat3("Grid Origin", glm::value_ptr(ddgi.GridOrigin), 0.1f, -5000.0f, 5000.0f, "%.2f m");
            inspectTooltip("Local origin offset of probe (0,0,0) relative to this entity's transform.");
            if (ImGui::DragFloat("Energy Multiplier", &ddgi.EnergyMultiplier, 0.05f, 0.0f, 10.0f, "%.2f")) {
                ddgi.EnergyMultiplier = std::isfinite(ddgi.EnergyMultiplier) ? std::clamp(ddgi.EnergyMultiplier, 0.0f, 10.0f) : 1.0f;
            }
            inspectTooltip("Multi-bounce diffuse irradiance intensity scale [0..10].");
            if (ImGui::SliderFloat("Hysteresis", &ddgi.Hysteresis, 0.0f, 0.99f, "%.2f")) {
                ddgi.Hysteresis = std::isfinite(ddgi.Hysteresis) ? std::clamp(ddgi.Hysteresis, 0.0f, 0.99f) : 0.9f;
            }
            inspectTooltip("Temporal blend weight [0..0.99] suppressing irradiance flicker across recaptures.");
            if (ImGui::SliderFloat("Update Fraction", &ddgi.UpdateFraction, 0.01f, 1.0f, "%.2f")) {
                ddgi.UpdateFraction = std::isfinite(ddgi.UpdateFraction) ? std::clamp(ddgi.UpdateFraction, 0.01f, 1.0f) : 0.25f;
            }
            inspectTooltip("Target fraction of grid probes scheduled per update batch.");
            ddgi.MaxProbesPerFrame = std::clamp(
                ddgi.MaxProbesPerFrame, 1, static_cast<int>(DDGIContract::HardMaxProbesPerFrame));
            ImGui::SliderInt("Max Probes / Frame", &ddgi.MaxProbesPerFrame, 1,
                static_cast<int>(DDGIContract::HardMaxProbesPerFrame));
            inspectTooltip("Strict per-frame ceiling on recaptured probes.");
            if (ImGui::DragFloat("View Bias", &ddgi.ViewBias, 0.01f, 0.0f, 2.0f, "%.3f m")) {
                ddgi.ViewBias = std::isfinite(ddgi.ViewBias) ? std::clamp(ddgi.ViewBias, 0.0f, 2.0f) : 0.15f;
            }
            inspectTooltip("Sampling offset toward camera to mitigate light leaking through thin walls.");
            if (ImGui::DragFloat("Normal Bias", &ddgi.NormalBias, 0.01f, 0.0f, 2.0f, "%.3f m")) {
                ddgi.NormalBias = std::isfinite(ddgi.NormalBias) ? std::clamp(ddgi.NormalBias, 0.0f, 2.0f) : 0.08f;
            }
            inspectTooltip("Sampling offset along surface normal before Chebyshev visibility test.");
            const auto budget = DDGIContract::EstimateMemoryBudget(
                ddgi.GridDimensions, static_cast<uint32_t>(ddgi.MaxProbesPerFrame), false);
            if (budget.Extent.Valid) {
                const double mib = static_cast<double>(budget.TotalLogicalBytes) / (1024.0 * 1024.0);
                ImGui::TextDisabled("Probes: %u | Irradiance: %ux%u | Distance: %ux%u | Est: %.2f MiB",
                    budget.Extent.ProbeCount,
                    budget.Extent.IrradianceWidth, budget.Extent.IrradianceHeight,
                    budget.Extent.DistanceWidth, budget.Extent.DistanceHeight,
                    mib);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                    "Invalid grid dimensions (max %u probes, max %u atlas extent).",
                    DDGIContract::MaxProbeCount, DDGIContract::MaxAtlasDimension);
            }
        }, /*canRemove=*/ true, nullptr, /*canReset=*/ true);

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

        // Particle Emitter Component (VFX)
        DrawComponent<ParticleEmitterComponent>(
            "Particle Emitter (VFX)", entity,
            [&](ParticleEmitterComponent &pec) {
                const auto previousConfig = pec.Config;
                const auto previousTexture = pec.TexturePath, previousNormal = pec.NormalMapPath;
                // Open in VFX Editor Button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.35f, 0.15f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.45f, 0.25f, 1.0f));
                if (ImGui::Button("Open in VFX Editor", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                    if (m_OnOpenVFXEditorCallback) {
                        m_OnOpenVFXEditorCallback(entity);
                    }
                }
                ImGui::PopStyleColor(2);
                ImGui::Spacing();
                // Live Playback & Testing Bar
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
                if (ImGui::Button(pec.Active ? "Pause Emitter" : "Play Emitter", ImVec2(100.0f, 24.0f))) {
                    if (pec.Active)
                        pec.Stop();
                    else
                        pec.Play();
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Button("Reset", ImVec2(60.0f, 24.0f))) {
                    pec.Reset();
                }
                ImGui::SameLine();
                if (ImGui::Button("Burst 25", ImVec2(75.0f, 24.0f))) {
                    pec.EmitBurst(25);
                }
                ImGui::SameLine();
                if (ImGui::Button("Burst 100", ImVec2(75.0f, 24.0f))) {
                    pec.EmitBurst(100);
                }

                if (ImGui::Checkbox("Active", &pec.Active)) {
                    if (pec.Active)
                        pec.Play();
                    else
                        pec.Stop();
                }
                ImGui::SameLine();
                ImGui::Checkbox("Auto Play on Start", &pec.AutoPlay);
                if (!pec.AssetPath.empty()) {
                    ImGui::TextWrapped("Shared effect: %s", pec.AssetPath.c_str());
                    if (ImGui::Button("Make Unique"))
                        pec.AssetPath.clear();
                    ImGui::SameLine();
                    if (ImGui::Button("Reload Shared Effect")) {
                        const auto source = pec.AssetPath;
                        pec.LoadFromAsset(source);
                    }
                }
                ImGui::BeginDisabled(!pec.AssetPath.empty());

                const char *spaceModes[] = {"World Space", "Local Space"};
                int currentSpace = (pec.SimulationSpace == ParticleSimulationSpace::Local) ? 1 : 0;
                if (ImGui::Combo("Simulation Space", &currentSpace, spaceModes, IM_ARRAYSIZE(spaceModes))) {
                    pec.SetSimulationSpace((currentSpace == 1) ? ParticleSimulationSpace::Local
                                                               : ParticleSimulationSpace::World);
                }

                // Presets quick loader
                if (ImGui::TreeNodeEx("Presets (Quick Setup)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto PresetBtn = [&](const char *label, const ParticleEmitterConfig &cfg) {
                        if (ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x * 0.48f, 24.0f))) {
                            pec.Config = cfg;
                            pec.Initialize();
                            pec.Play();
                        }
                    };
                    PresetBtn("Fire", ParticleEmitterConfig::CreateFire());
                    ImGui::SameLine();
                    PresetBtn("Smoke", ParticleEmitterConfig::CreateSmoke());
                    PresetBtn("Sparks", ParticleEmitterConfig::CreateSparks());
                    ImGui::SameLine();
                    PresetBtn("Tire Burnout", ParticleEmitterConfig::CreateTireBurnout());
                    PresetBtn("Dirt Rooster", ParticleEmitterConfig::CreateDustTrail());
                    ImGui::SameLine();
                    PresetBtn("Backfire", ParticleEmitterConfig::CreateExhaustBackfire());
                    ImGui::TreePop();
                }

                // Spawn & Kinematics
                if (ImGui::TreeNodeEx("Lifecycle & Spawning", ImGuiTreeNodeFlags_DefaultOpen)) {
                    int maxP = static_cast<int>(pec.Config.MaxParticles);
                    if (ImGui::DragInt("Max Particles (Pool)", &maxP, 50, 10, 50000)) {
                        pec.Config.MaxParticles = static_cast<uint32_t>(std::max(10, maxP));
                        pec.Initialize();
                    }
                    ImGui::DragFloat("Emission Rate (P/sec)", &pec.Config.EmissionRate, 1.0f, 0.0f, 2000.0f,
                                     "%.1f");
                    ImGui::DragFloatRange2("Lifetime (sec)", &pec.Config.Lifetime.Min,
                                           &pec.Config.Lifetime.Max, 0.05f, 0.05f, 30.0f, "Min: %.2f",
                                           "Max: %.2f");
                    ImGui::DragFloatRange2("Speed (m/s)", &pec.Config.InitialSpeed.Min,
                                           &pec.Config.InitialSpeed.Max, 0.1f, 0.0f, 100.0f, "Min: %.1f",
                                           "Max: %.1f");
                    ImGui::DragFloat("Gravity Modifier", &pec.Config.GravityModifier, 0.05f, -10.0f, 10.0f,
                                     "%.2fx");
                    ImGui::SliderFloat("Drag", &pec.Config.Drag, 0.0f, 1.0f, "%.2f");
                    ImGui::TreePop();
                }

                // Shape
                if (ImGui::TreeNodeEx("Emitter Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const char *shapeTypes[] = {"Point", "Sphere", "Box", "Cone", "Directional"};
                    int currentShape = static_cast<int>(pec.Config.Shape.Type);
                    if (ImGui::Combo("Shape Type", &currentShape, shapeTypes, IM_ARRAYSIZE(shapeTypes))) {
                        pec.Config.Shape.Type = static_cast<ParticleEmitterShapeType>(currentShape);
                    }
                    switch (pec.Config.Shape.Type) {
                    case ParticleEmitterShapeType::Sphere:
                        ImGui::DragFloat("Sphere Radius", &pec.Config.Shape.Radius, 0.05f, 0.01f, 50.0f, "%.2f m");
                        ImGui::Checkbox("Surface Shell Only", &pec.Config.Shape.SurfaceOnly);
                        break;
                    case ParticleEmitterShapeType::Box:
                        ImGui::DragFloat3("Box Half-Extents", glm::value_ptr(pec.Config.Shape.BoxExtents), 0.05f, 0.01f, 50.0f, "%.2f m");
                        ImGui::Checkbox("Surface Shell Only", &pec.Config.Shape.SurfaceOnly);
                        break;
                    case ParticleEmitterShapeType::Cone:
                        ImGui::DragFloat("Cone Angle (Rad)", &pec.Config.Shape.ConeAngle, 0.02f, 0.01f, 1.57f, "%.2f rad");
                        ImGui::DragFloat("Cone Base Radius", &pec.Config.Shape.Radius, 0.05f, 0.0f, 20.0f, "%.2f m");
                        break;
                    case ParticleEmitterShapeType::Line:
                        ImGui::DragFloat("Line Length", &pec.Config.Shape.LineLength, 0.05f, 0.01f, 50.0f, "%.2f m");
                        break;
                    default:
                        break;
                    }
                    ImGui::TreePop();
                }

                // Size & Color Curves
                if (ImGui::TreeNodeEx("Curves (Size & Color/Alpha)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::DragFloatRange2("Start Size Range", &pec.Config.StartSize.Min,
                                           &pec.Config.StartSize.Max, 0.02f, 0.01f, 20.0f, "Min: %.2f",
                                           "Max: %.2f");
                    ImGui::DragFloatRange2("End Size Range", &pec.Config.EndSize.Min, &pec.Config.EndSize.Max,
                                           0.02f, 0.01f, 20.0f, "Min: %.2f", "Max: %.2f");
                    ImGui::ColorEdit4("Start Color & Alpha",
                                      glm::value_ptr(pec.Config.ColorOverLifetime.StartColor),
                                      ImGuiColorEditFlags_AlphaBar);
                    ImGui::ColorEdit4("End Color & Alpha",
                                      glm::value_ptr(pec.Config.ColorOverLifetime.EndColor),
                                      ImGuiColorEditFlags_AlphaBar);
                    ImGui::DragFloatRange2("Angular Velocity", &pec.Config.AngularVelocity.Min,
                                           &pec.Config.AngularVelocity.Max, 0.1f, -20.0f, 20.0f, "Min: %.2f",
                                           "Max: %.2f");
                    ImGui::TreePop();
                }

                // Texture & Flipbook
                if (ImGui::TreeNodeEx("Texture & Flipbook")) {
                    std::string disp = pec.TexturePath.empty()
                                           ? "(None - Procedural Dot)"
                                           : std::filesystem::path(pec.TexturePath).filename().string();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.28f, 0.85f));
                    ImGui::Button(disp.c_str(), ImVec2(-32.0f, 24.0f));
                    ImGui::PopStyleColor();

                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload *payload =
                                ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            const char *pathStr = static_cast<const char *>(payload->Data);
                            std::filesystem::path p(pathStr);
                            std::string ext = p.extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(),
                                           [](char c) { return (char)std::tolower(c); });
                            if (ext == ".png" || ext == ".jpg" || ext == ".tga" || ext == ".bmp") {
                                pec.TexturePath = p.string();
                                pec.Texture =
                                    Window::HasActiveContext() ? Texture2D::Create(p.string()) : nullptr;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("X##ClearPecTex", ImVec2(24.0f, 24.0f))) {
                        pec.TexturePath.clear();
                        pec.Texture.reset();
                    }

                    int rows = static_cast<int>(pec.Config.TextureRows);
                    int cols = static_cast<int>(pec.Config.TextureCols);
                    if (ImGui::DragInt("Flipbook Rows", &rows, 1, 1, 16)) {
                        pec.Config.TextureRows = static_cast<uint32_t>(std::max(1, rows));
                        pec.Config.TotalFrames = pec.Config.TextureRows * pec.Config.TextureCols;
                    }
                    if (ImGui::DragInt("Flipbook Columns", &cols, 1, 1, 16)) {
                        pec.Config.TextureCols = static_cast<uint32_t>(std::max(1, cols));
                        pec.Config.TotalFrames = pec.Config.TextureRows * pec.Config.TextureCols;
                    }
                    ImGui::Checkbox("Subframe Smooth Blending", &pec.Config.SubFrameBlending);
                    ImGui::TreePop();
                }

                // Dynamics, Collisions & Forces
                if (ImGui::TreeNodeEx("Dynamics & Forces")) {
                    ImGui::Checkbox("Curl Noise Turbulence", &pec.Config.CurlNoise.Enabled);
                    if (pec.Config.CurlNoise.Enabled) {
                        ImGui::DragFloat("Curl Frequency", &pec.Config.CurlNoise.Frequency, 0.05f, 0.05f,
                                         10.0f, "%.2f");
                        ImGui::DragFloat("Curl Strength", &pec.Config.CurlNoise.Strength, 0.1f, 0.0f, 20.0f,
                                         "%.2f m/s²");
                    }
                    ImGui::Checkbox("Ground Collisions", &pec.Config.Collision.Enabled);
                    if (pec.Config.Collision.Enabled) {
                        ImGui::SliderFloat("Restitution (Bounce)", &pec.Config.Collision.Restitution, 0.0f,
                                           1.0f, "%.2f");
                        ImGui::SliderFloat("Friction", &pec.Config.Collision.Friction, 0.0f, 1.0f, "%.2f");
                        ImGui::Checkbox("Kill on Collision", &pec.Config.Collision.KillOnCollision);
                    }
                    ImGui::DragFloat3("Wind Vector", glm::value_ptr(pec.Config.Wind), 0.1f, -50.0f, 50.0f,
                                      "%.1f m/s");
                    ImGui::DragFloat("Thermal Buoyancy", &pec.Config.Buoyancy, 0.1f, -20.0f, 20.0f,
                                     "%.1f m/s²");
                    ImGui::TreePop();
                }

                // Shading & Billboarding
                if (ImGui::TreeNodeEx("Shading & Billboarding")) {
                    const char *orientations[] = {"Spherical (Camera-Facing)", "Cylindrical (Axial-Y)",
                                                  "Velocity-Aligned"};
                    int currentOrient = static_cast<int>(pec.Config.Orientation);
                    if (ImGui::Combo("Orientation", &currentOrient, orientations,
                                     IM_ARRAYSIZE(orientations))) {
                        pec.Config.Orientation = static_cast<ParticleOrientation>(currentOrient);
                    }

                    const char *blendModes[] = {"Additive", "Alpha Blend", "Premultiplied Alpha"};
                    int currentBlend = static_cast<int>(pec.Config.BlendMode);
                    if (ImGui::Combo("Blend Mode", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                        pec.Config.BlendMode = static_cast<ParticleBlendMode>(currentBlend);
                    }
                    ImGui::DragFloat("Softness Distance", &pec.Config.SoftnessDistance, 0.05f, 0.05f, 5.0f,
                                     "%.2f m");
                    ImGui::TreePop();
                }

                ImGui::EndDisabled();
                if (pec.AssetPath.empty() &&
                    (!ParticleEmitterConfigsEqual(previousConfig, pec.Config) ||
                     previousTexture != pec.TexturePath || previousNormal != pec.NormalMapPath)) {
                    pec.AssetPath.clear();
                    if (!pec.EmitterStacks.empty()) {
                        auto &primary = pec.EmitterStacks.front();
                        primary.Config = pec.Config;
                        primary.TexturePath = pec.TexturePath;
                        primary.NormalMapPath = pec.NormalMapPath;
                        primary.UseTexture = !pec.TexturePath.empty();
                        primary.UseNormalMap = !pec.NormalMapPath.empty();
                        if (primary.System)
                            primary.System->SetConfig(primary.Config);
                        pec.SyncPrimary();
                    }
                }

                // Live Telemetry
                if (pec.System) {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Live Particle Telemetry:");
                    ImGui::Text("Active Particles: %u / %u", pec.System->GetActiveParticleCount(),
                                pec.System->GetPool().GetCapacity());
                    ImGui::Text("Collisions Registered: %u", pec.System->GetCollisionCount());
                }
            },
            /*canRemove=*/true);

        // Character VFX Component
        DrawComponent<CharacterVFXComponent>("Character Visual Effects (VFX)", entity, [&](CharacterVFXComponent& cvfx) {
            ImGui::Checkbox("VFX Enabled", &cvfx.Enabled);

            ImGui::SliderFloat("Stride Length", &cvfx.Config.StrideLength, 0.5f, 3.0f, "%.2f m");
            ImGui::SliderFloat("Footstep Min Speed", &cvfx.Config.FootstepMinSpeed, 0.1f, 2.0f, "%.2f m/s");
            ImGui::SliderFloat("Landing Impact Threshold", &cvfx.Config.LandingImpactThreshold, 1.0f, 10.0f, "%.1f m/s");
            ImGui::SliderFloat("Heavy Landing Threshold", &cvfx.Config.HeavyLandingThreshold, 4.0f, 20.0f, "%.1f m/s");

            // Live Runtime Telemetry
            if (cvfx.RuntimeVFX) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Live Character VFX Telemetry:");
                uint32_t footsteps = cvfx.RuntimeVFX->GetFootstepSystem() ? cvfx.RuntimeVFX->GetFootstepSystem()->GetActiveParticleCount() : 0;
                uint32_t shockwave = cvfx.RuntimeVFX->GetLandingShockwaveSystem() ? cvfx.RuntimeVFX->GetLandingShockwaveSystem()->GetActiveParticleCount() : 0;

                ImGui::Text("Footstep Dust Puffs: %u particles", footsteps);
                ImGui::Text("Landing Shockwave: %u particles", shockwave);
            }
        }, /*canRemove=*/ true);

        // UICanvasComponent (Phase 34 Step 7 & 8 - UI Canvas, Searchable Picker & Scoped MVVM Binding)
        DrawComponent<UICanvasComponent>("UI Canvas Component", entity, [&](UICanvasComponent& uic) {
            // 1. Searchable Asset Path Picker
            std::string previewText = uic.AssetPath.empty() ? "[ None (Select .axleui) ]" : uic.AssetPath;
            if (ImGui::BeginCombo("Asset Path", previewText.c_str())) {
                static char filterBuf[64] = "";
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##FilterAxleUI", "Search .axleui...", filterBuf, sizeof(filterBuf));
                ImGui::Separator();

                if (ImGui::Selectable("[ None / Clear ]", uic.AssetPath.empty())) {
                    uic.AssetPath = "";
                    filterBuf[0] = '\0';
                }

                auto availableAssets = DiscoverAxleUIAssets();
                for (const auto& assetPath : availableAssets) {
                    if (filterBuf[0] != '\0' && !ContainsIgnoreCase(assetPath, filterBuf)) {
                        continue;
                    }
                    bool isSelected = (uic.AssetPath == assetPath);
                    if (ImGui::Selectable(assetPath.c_str(), isSelected)) {
                        uic.AssetPath = assetPath;
                        filterBuf[0] = '\0';
                        if (!uic.Document) {
                            uic.Document = std::make_shared<UIDocument>();
                        }
                        std::filesystem::path fullPath = assetPath;
                        if (!std::filesystem::exists(fullPath)) {
                            fullPath = FileSystem::GetAssetDirectory() / assetPath;
                        }
                        if (std::filesystem::exists(fullPath)) {
                            uic.Document->LoadFromFile(fullPath.string());
                        }
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            // Drag-drop .axleui item into AssetPath
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* pathStr = static_cast<const char*>(payload->Data);
                    std::filesystem::path p(pathStr);
                    if (p.extension() == ".axleui" || p.extension() == ".axleasset") {
                        uic.AssetPath = p.generic_string();
                        if (!uic.Document) {
                            uic.Document = std::make_shared<UIDocument>();
                        }
                        uic.Document->LoadFromFile(p.string());
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // 2. Scoped MVVM Data Source (Convention over Configuration + Retargeting)
            std::string dsLabel = "[ Self (This Entity) ]";
            Entity currentTarget = uic.GetDataSourceEntity(entity.GetScene(), entity);

            if (uic.DataSourceEntityID == 0) {
                if (entity.HasComponent<NativeScriptComponent>()) {
                    dsLabel = "[ Self (NativeScript: " + entity.GetComponent<NativeScriptComponent>().ScriptName + ") ]";
                } else {
                    dsLabel = "[ Self (This Entity) ]";
                }
            } else {
                if (currentTarget && currentTarget != entity) {
                    std::string targetTag = currentTarget.GetName();
                    if (currentTarget.HasComponent<NativeScriptComponent>()) {
                        dsLabel = "[ " + targetTag + " (" + currentTarget.GetComponent<NativeScriptComponent>().ScriptName + ") ]";
                    } else {
                        dsLabel = "[ " + targetTag + " ]";
                    }
                } else {
                    dsLabel = "[ Entity: " + std::to_string(static_cast<uint64_t>(uic.DataSourceEntityID)) + " ]";
                }
            }

            float availX = ImGui::GetContentRegionAvail().x;
            float btnW = 28.0f;
            float inputW = (uic.DataSourceEntityID != 0) ? (availX - btnW - 6.0f) : availX;

            ImGui::SetNextItemWidth(inputW);
            ImGui::PushStyleColor(ImGuiCol_Text, (uic.DataSourceEntityID == 0) ? ImVec4(0.4f, 0.85f, 1.0f, 1.0f) : ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
            ImGui::InputText("Data Source", const_cast<char*>(dsLabel.c_str()), dsLabel.length(), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Drag & Drop an Entity from Scene Hierarchy to retarget the UI Data Source.\nDefault is 'Self' (auto-binds to script co-located on this Entity).");
            }

            // Accept Drag & Drop of Entity from Scene Hierarchy
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
                    uint32_t draggedID = *(const uint32_t*)payload->Data;
                    if (entity.GetScene()) {
                        Entity draggedEntity{ (entt::entity)draggedID, entity.GetScene() };
                        if (draggedEntity) {
                            if (draggedEntity == entity) {
                                uic.DataSourceEntityID = 0;
                            } else {
                                uic.DataSourceEntityID = draggedEntity.GetUUID();
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (uic.DataSourceEntityID != 0) {
                ImGui::SameLine();
                if (ImGui::Button("x##ClearDS", ImVec2(btnW, 0))) {
                    uic.DataSourceEntityID = 0; // Reset to Self (Auto)
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset Data Source back to Self (Auto)");
                }
            }

            ImGui::DragInt("Sort Order", &uic.SortOrder, 1, -100, 100);
            ImGui::Checkbox("Visible", &uic.Visible);

            ImGui::Spacing();
            ImGui::Separator();

            if (uic.Document) {
                bool isDirty = uic.Document->IsDirty();
                if (isDirty) {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "[*] Document has unsaved visual modifications");
                } else {
                    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "[OK] Document is clean & synchronized");
                }

                ImGui::Text("Nodes in DOM: %zu", uic.Document->GetNodeCount());
                if (auto* rootCanvas = dynamic_cast<UICanvasNode*>(uic.Document->GetRootNode())) {
                    ImGui::Text("Reference Resolution: %.0f x %.0f",
                        rootCanvas->ReferenceResolution.x, rootCanvas->ReferenceResolution.y);
                }

                ImGui::Spacing();
                if (ImGui::Button("Save .axleui Document")) {
                    std::string savePath = uic.AssetPath;
                    if (savePath.empty()) {
                        savePath = "assets/ui/" + entity.GetName() + ".axleui";
                        uic.AssetPath = savePath;
                    }
                    std::filesystem::path fullPath = savePath;
                    if (!std::filesystem::exists(fullPath)) {
                        fullPath = FileSystem::GetAssetDirectory() / savePath;
                    }
                    if (fullPath.has_parent_path() && !std::filesystem::exists(fullPath.parent_path())) {
                        std::filesystem::create_directories(fullPath.parent_path());
                    }
                    uic.Document->SaveToFile(fullPath.string());
                    AXLE_INFO("Saved UI Document '{0}'", fullPath.string());
                }

                ImGui::SameLine();
                if (ImGui::Button("Reload from Disk")) {
                    std::filesystem::path fullPath = uic.AssetPath;
                    if (!std::filesystem::exists(fullPath)) {
                        fullPath = FileSystem::GetAssetDirectory() / uic.AssetPath;
                    }
                    if (std::filesystem::exists(fullPath)) {
                        uic.Document->LoadFromFile(fullPath.string());
                        AXLE_INFO("Reloaded UI Document '{0}'", fullPath.string());
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "No UIDocument attached.");
                if (ImGui::Button("Initialize Empty Canvas Document")) {
                    uic.Document = std::make_shared<UIDocument>();
                    uic.Document->CreateNode<UICanvasNode>(nullptr);
                }
            }
        }, /*canRemove=*/ true);

        // Custom Plugin Components
        if (m_CustomComponentDrawer) {
            m_CustomComponentDrawer(entity);
        }

        // --- UNITY STYLE ADD COMPONENT BUTTON ---
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float availWidth = ImGui::GetContentRegionAvail().x;
        float btnWidth = std::min(230.0f, availWidth * 0.85f);
        float offsetX = (availWidth - btnWidth) * 0.5f;
        if (offsetX > 0.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.5f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.24f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.32f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("Add Component", ImVec2(btnWidth, 28.0f))) {
            ImGui::OpenPopup("AddComponentPopup");
            m_ComponentSearchFilter[0] = '\0';
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

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
            "Rendering", "SpotLightComponent", "Directional cone spot light source",
            entity.HasComponent<SpotLightComponent>(), true,
            [&]() { entity.AddComponent<SpotLightComponent>(); }
        });
        items.push_back({
            "Rendering", "RectLightComponent", "LTC rectangular or disk area light source",
            entity.HasComponent<RectLightComponent>(), true,
            [&]() { entity.AddComponent<RectLightComponent>(); }
        });
        items.push_back({
            "Rendering", "PlanarReflectionComponent", "Single-sided real-time planar mirror reflection plane",
            entity.HasComponent<PlanarReflectionComponent>(), true,
            [&]() { entity.AddComponent<PlanarReflectionComponent>(); }
        });
        items.push_back({
            "Rendering", "ReflectionProbeComponent", "Local box/sphere cubemap reflection volume with dynamic or baked capture",
            entity.HasComponent<ReflectionProbeComponent>(), true,
            [&]() {
                auto& probe = entity.AddComponent<ReflectionProbeComponent>();
                probe.DynamicCapture = true;
            }
        });
        items.push_back({
            "Rendering", "SkyboxComponent", "Procedural atmospheric Rayleigh/Mie sky dome",
            entity.HasComponent<SkyboxComponent>(), true,
            [&]() {
                auto& skybox = entity.AddComponent<SkyboxComponent>();
                // An explicit Editor authoring action opts the new environment into
                // ambient fallback so metallic materials are immediately readable.
                skybox.EnvironmentLighting = true;
            }
        });
        items.push_back({
            "Rendering", "CameraComponent", "Perspective/Orthographic scene camera",
            entity.HasComponent<CameraComponent>(), true,
            [&]() { entity.AddComponent<CameraComponent>(); }
        });
        items.push_back({
            "Rendering", "VolumetricFogComponent", "Exponential height-falloff 3D froxel volumetric fog & scattering media",
            entity.HasComponent<VolumetricFogComponent>(), true,
            [&]() { entity.AddComponent<VolumetricFogComponent>(); }
        });
        items.push_back({
            "Rendering", "DDGIProbeGridComponent", "Experimental dynamic diffuse GI probe lattice & octahedral atlas",
            entity.HasComponent<DDGIProbeGridComponent>(), true,
            [&]() { entity.AddComponent<DDGIProbeGridComponent>(); }
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
            "Audio/VFX", "ParticleEmitterComponent", "GPU-instanced particle emitter (Smoke, Fire, Sparks, Dust, Flipbooks)",
            entity.HasComponent<ParticleEmitterComponent>(), true,
            [&]() {
                auto& pec = entity.AddComponent<ParticleEmitterComponent>(ParticleEmitterConfig::CreateFire());
                pec.Initialize();
                pec.Play();
            }
        });
        items.push_back({
            "Audio/VFX", "CharacterVFXComponent", "Gameplay VFX: Ground footstep dust puffs & landing shockwave ripples",
            entity.HasComponent<CharacterVFXComponent>(), true,
            [&]() { entity.AddComponent<CharacterVFXComponent>(); }
        });

        // --- USER INTERFACE CATEGORY ---
        items.push_back({
            "User Interface", "UICanvasComponent", "Hardware-accelerated 2D UI canvas with responsive layout & .axleui bindings",
            entity.HasComponent<UICanvasComponent>(), true,
            [&]() {
                auto& uic = entity.AddComponent<UICanvasComponent>();
                uic.Document = std::make_shared<UIDocument>();
                uic.Document->CreateNode<UICanvasNode>(nullptr);
            }
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
            const std::vector<std::string> categories = { "Rendering", "Physics", "Scripts", "Audio/VFX", "User Interface" };
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

        if (m_CustomAddComponentDrawer) {
            ImGui::Separator();
            m_CustomAddComponentDrawer(entity);
        }

        ImGui::EndPopup();
    }


}
