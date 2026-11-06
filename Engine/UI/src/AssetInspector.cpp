#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/AssetThumbnailManager.hpp>
#include <Axle/Components.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/PhysicsMaterial.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/Window.hpp>
#include <Axle/AudioEngine.hpp>
#include <Axle/AudioSource.hpp>
#include <Axle/AudioClip.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Application.hpp>
#include <Axle/ShaderPropertyReflection.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <stb_image.h>
#include <Extensions/NRIHelper.h>
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <functional>
#include <algorithm>
#include <cctype>

namespace Axle {

    void SceneHierarchyPanel::ClearTexturePreview() {
        if (m_TexturePreviewImGuiID || m_RHITexturePreview || m_GLTexturePreview) {
            // Selection may change while this frame's draw lists still reference the old image.
            // Keep both the descriptor and texture alive until UI submission and GPU completion.
            auto release = [layer = m_ImGuiLayer, handle = m_TexturePreviewImGuiID,
                            rhiTexture = std::move(m_RHITexturePreview),
                            glTexture = std::move(m_GLTexturePreview)] {
                if (handle && layer && layer->GetBackendType() != ImGuiBackendType::OpenGL)
                    layer->UnregisterTextureDescriptor(handle);
                // The captured shared owners are released with the retired callback.
            };
            if (m_ImGuiLayer) m_ImGuiLayer->DeferRelease(std::move(release));
            else release();
        }
        m_TexturePreviewImGuiID = 0;
        m_LoadedTexturePath.clear();
        m_LoadedTextureWidth = 0;
        m_LoadedTextureHeight = 0;
        m_LoadedTextureChannels = 0;
    }

    void SceneHierarchyPanel::LoadTexturePreview(const std::filesystem::path& path) {
        if (m_LoadedTexturePath == path && (m_TexturePreviewImGuiID != 0 || m_LoadedTextureWidth > 0)) {
            return;
        }

        ClearTexturePreview();
        m_LoadedTexturePath = path;

        if (!m_ImGuiLayer) {
            m_ImGuiLayer = dynamic_cast<ImGuiLayer*>(Application::Get().GetUIContext());
        }

        std::filesystem::path absPath = FileSystem::ResolveAssetPath(path);
        auto buffer = FileSystem::ReadBinaryFile(absPath);
        if (buffer.empty()) {
            AXLE_CORE_WARN("SceneHierarchyPanel: Failed to read texture file for preview '{0}'", absPath.string());
            return;
        }

        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load_thread(false);
        stbi_uc* pixels = stbi_load_from_memory(buffer.data(), static_cast<int>(buffer.size()), &width, &height, &channels, 4);
        if (!pixels) {
            AXLE_CORE_WARN("SceneHierarchyPanel: Failed to decode texture '{0}': {1}", absPath.string(), stbi_failure_reason());
            return;
        }

        m_LoadedTextureWidth = static_cast<uint32_t>(width);
        m_LoadedTextureHeight = static_cast<uint32_t>(height);
        m_LoadedTextureChannels = static_cast<uint32_t>(channels);

        bool isRHI = (m_ImGuiLayer && m_ImGuiLayer->GetBackendType() != ImGuiBackendType::OpenGL);
        if (isRHI) {
            auto& rhiDevice = RHI::RHIDevice::Get();
            if (rhiDevice.IsInitialized()) {
                m_RHITexturePreview = std::make_shared<RHI::RHITexture>();
                auto desc = RHI::RHITextureDesc::RenderTarget2D(m_LoadedTextureWidth, m_LoadedTextureHeight, RHI::RHITextureFormat::RGBA8_UNORM, "TexturePreview_" + path.filename().string());
                desc.Usage = RHI::RHITextureUsage::ShaderResource;
                if (m_RHITexturePreview->Init(desc)) {
                    const auto& helper = rhiDevice.GetNRIHelper();
                    nri::Queue* queue = rhiDevice.GetGraphicsQueue();
                    nri::Result uploadResult = nri::Result::FAILURE;
                    if (queue && helper.UploadData) {
                        nri::TextureSubresourceUploadDesc subres = {};
                        subres.slices = pixels;
                        subres.sliceNum = 1;
                        subres.rowPitch = m_LoadedTextureWidth * 4;
                        subres.slicePitch = m_LoadedTextureWidth * m_LoadedTextureHeight * 4;

                        nri::TextureUploadDesc upDesc = {};
                        upDesc.subresources = &subres;
                        upDesc.texture = m_RHITexturePreview->GetNative();
                        upDesc.after.access = nri::AccessBits::SHADER_RESOURCE;
                        upDesc.after.layout = nri::Layout::SHADER_RESOURCE;
                        upDesc.after.stages = nri::StageBits::ALL;
                        upDesc.planes = nri::PlaneBits::ALL;

                        uploadResult = helper.UploadData(*queue, &upDesc, 1, nullptr, 0);
                    }

                    if (uploadResult == nri::Result::SUCCESS) {
                        uint64_t nativeHandle = m_RHITexturePreview->GetNativeSRVHandle();
                        if (nativeHandle != 0 && m_ImGuiLayer)
                            m_TexturePreviewImGuiID = m_ImGuiLayer->RegisterTextureDescriptor(nativeHandle);
                    } else {
                        AXLE_CORE_WARN("SceneHierarchyPanel: Texture preview upload failed for '{0}' (result {1})",
                                       absPath.string(), static_cast<int>(uploadResult));
                    }
                }
            }
        } else {
            m_GLTexturePreview = Texture2D::Create(absPath.string());
            if (m_GLTexturePreview && m_GLTexturePreview->IsLoaded()) {
                m_TexturePreviewImGuiID = m_GLTexturePreview->GetRendererID();
            }
        }

        stbi_image_free(pixels);
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
        bool isMaterial = (ext == ".axleasset" && AssetDSL::PeekAssetKindFromFile(path.string()) == AssetKind::Material);
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
                }
            }
            DrawMaterialAssetProperties(path);
            return;
        }

        // 2. 2D Texture Asset: Render image preview with resolution metadata
        if (isTexture) {
            LoadTexturePreview(path);
            if (m_LoadedTextureWidth > 0 && m_LoadedTextureHeight > 0) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.11f, 0.14f, 1.0f));
                ImGui::BeginChild("TexturePreviewViewport", ImVec2(0.0f, 220.0f), true);
                {
                    float availW = ImGui::GetContentRegionAvail().x;
                    float availH = 200.0f;
                    float texW = (float)m_LoadedTextureWidth;
                    float texH = (float)m_LoadedTextureHeight;
                    float scale = std::min(availW / texW, availH / texH);
                    float dispW = texW * scale;
                    float dispH = texH * scale;

                    float padX = (availW - dispW) * 0.5f;
                    float padY = (availH - dispH) * 0.5f;
                    if (padX > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
                    if (padY > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padY);

                    ImVec2 uv0 = (m_ImGuiLayer && m_ImGuiLayer->GetBackendType() != ImGuiBackendType::OpenGL) ? ImVec2(0.0f, 0.0f) : ImVec2(0.0f, 1.0f);
                    ImVec2 uv1 = (m_ImGuiLayer && m_ImGuiLayer->GetBackendType() != ImGuiBackendType::OpenGL) ? ImVec2(1.0f, 1.0f) : ImVec2(1.0f, 0.0f);

                    if (m_TexturePreviewImGuiID != 0) {
                        ImGui::Image((ImTextureID)(intptr_t)m_TexturePreviewImGuiID, ImVec2(dispW, dispH), uv0, uv1);
                    } else {
                        ImGui::TextDisabled("Preview unavailable");
                    }
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
                    ImGui::Text("%u x %u px", m_LoadedTextureWidth, m_LoadedTextureHeight);
                    ImGui::NextColumn();

                    ImGui::TextDisabled("Aspect Ratio:");
                    ImGui::NextColumn();
                    ImGui::Text("%.2f:1", (float)m_LoadedTextureWidth / (float)std::max(1u, m_LoadedTextureHeight));
                    ImGui::NextColumn();

                    ImGui::TextDisabled("Channels:");
                    ImGui::NextColumn();
                    std::string chanStr = (m_LoadedTextureChannels == 4) ? "RGBA (4)" : ((m_LoadedTextureChannels == 3) ? "RGB (3)" : std::to_string(m_LoadedTextureChannels));
                    ImGui::Text("%s", chanStr.c_str());
                    ImGui::NextColumn();

                    ImGui::TextDisabled("Renderer ID:");
                    ImGui::NextColumn();
                    if (m_TexturePreviewImGuiID != 0) {
                        ImGui::Text("0x%llX", (unsigned long long)m_TexturePreviewImGuiID);
                    } else {
                        ImGui::TextDisabled("0");
                    }
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
        auto m = std::make_shared<Material>(path.stem().string());
        // Save through the same complete mapping used by entity materials.
        MaterialComponent snapshot = mat;
        snapshot.MaterialAsset = m;
        snapshot.SyncToAsset();
        return m->Save(path);
    }

    bool SceneHierarchyPanel::LoadMaterialAsset(const std::filesystem::path& path, MaterialComponent& mat) {
        auto m = std::make_shared<Material>(path.stem().string());
        if (!m->Load(path)) return false;
        mat.SetAsset(0, m);
        return true;
    }

    void SceneHierarchyPanel::DrawMaterialAssetProperties(const std::filesystem::path& path) {
        if (m_LoadedMaterialPath != path || !m_MaterialAssetLoaded) {
            m_ActiveMaterialAsset = MaterialComponent();
            LoadMaterialAsset(path, m_ActiveMaterialAsset);
            m_LoadedMaterialPath = path;
            m_MaterialAssetLoaded = true;
            // The renderer publishes reflected schema and diagnostics into this
            // material instance. Share it from the first frame, before any edit.
            if (m_AssetPreviewPanel) m_AssetPreviewPanel->UpdateMaterialPreview(m_ActiveMaterialAsset);
        }

        bool changed = false;

        changed |= m_CustomShaderPicker.Draw("MaterialAssetShader", m_ActiveMaterialAsset,
                                             path.stem().string(), m_OnOpenHLSLStudioCallback);

        changed |= m_MaterialInspectorControls.Draw(m_ActiveMaterialAsset);

        if (changed) m_ActiveMaterialAsset.SyncToAsset();

        // Live 3D Preview Synchronization
        if (changed && m_AssetPreviewPanel) {
            m_AssetPreviewPanel->UpdateMaterialPreview(m_ActiveMaterialAsset);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float availW = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.65f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.50f, 0.80f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.35f, 0.58f, 1.0f));
        if (ImGui::Button("Save Material Asset", ImVec2(availW, 26.0f)) || changed) {
            SaveMaterialAsset(path, m_ActiveMaterialAsset);
            AssetThumbnailManager::Invalidate(path);
            MaterialCache::ReloadMaterial(path);
        }
        ImGui::PopStyleColor(3);

        if (m_SelectedEntity && m_Scene && m_Scene->GetRegistry().valid((entt::entity)m_SelectedEntity)) {
            ImGui::Spacing();
            if (ImGui::Button("Apply to Selected Entity", ImVec2(availW, 24.0f))) {
                UUID handle = AssetManager::GetAssetUUID(path);
                auto matAsset = MaterialCache::GetMaterial(path);
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
