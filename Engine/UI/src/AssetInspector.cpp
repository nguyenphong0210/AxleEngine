#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/Components.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/PhysicsMaterial.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/Window.hpp>
#include <Axle/AudioEngine.hpp>
#include <Axle/AudioSource.hpp>
#include <Axle/AudioClip.hpp>
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <fstream>
#include <string>

namespace Axle {

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
            } else if (ext == ".physmat" || (ext == ".axleasset" && AssetDSL::PeekAssetKindFromFile(path.string()) == AssetKind::PhysicsMaterial)) {
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

        // 0. 3D Preview Mesh Selector (Sphere, Cube, Cylinder, Plane)
        if (ImGui::CollapsingHeader("Preview Mesh Selector", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Preview Model:");
            PreviewMeshType activeType = m_AssetPreviewPanel ? m_AssetPreviewPanel->GetPreviewMeshType() : PreviewMeshType::Sphere;
            float btnW = (ImGui::GetContentRegionAvail().x - 18.0f) / 4.0f;
            auto DrawMeshBtn = [&](const char* label, PreviewMeshType type) {
                bool isSel = (activeType == type);
                if (isSel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.50f, 0.85f, 0.90f));
                if (ImGui::Button(label, ImVec2(btnW, 26.0f))) {
                    if (m_AssetPreviewPanel) m_AssetPreviewPanel->SetPreviewMeshType(type);
                }
                if (isSel) ImGui::PopStyleColor();
            };
            DrawMeshBtn("Sphere", PreviewMeshType::Sphere);
            ImGui::SameLine();
            DrawMeshBtn("Cube", PreviewMeshType::Cube);
            ImGui::SameLine();
            DrawMeshBtn("Cylinder", PreviewMeshType::Cylinder);
            ImGui::SameLine();
            DrawMeshBtn("Plane", PreviewMeshType::Plane);
            ImGui::Spacing();
        }

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
