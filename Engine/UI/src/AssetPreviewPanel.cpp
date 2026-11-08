#include <Axle/Path.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/RenderService.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/Texture2D.hpp>
#include <Extensions/NRIHelper.h>
#include <Axle/ImageSource.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/Log.hpp>
#include <imgui.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <vector>
#include <functional>

namespace Axle {

    AssetPreviewPanel::AssetPreviewPanel() {
        if (Window::HasActiveContext()) {
            FramebufferSpecification spec;
            spec.Width = m_ViewportWidth;
            spec.Height = m_ViewportHeight;
            m_Framebuffer = Framebuffer::Create(spec);
        }
    }

    AssetPreviewPanel::~AssetPreviewPanel() {
        FreeIconTextures();
        ShutdownPreview();
    }
    void AssetPreviewPanel::ShutdownPreview() {
        if (m_PreviewScene) {
            m_PreviewScene->Clear();
            m_PreviewScene.reset();
        }
    }

    uint64_t AssetPreviewPanel::GetOrCreateIconTexture(const std::string& iconFilename) {
        auto path = FileSystem::ResolveAssetPath(std::filesystem::path("Icons") / iconFilename);
        if (!FileSystem::Exists(path)) {
#ifdef _WIN32
            wchar_t executable[32768]{};
            const auto length = GetModuleFileNameW(nullptr, executable, 32768);
            if (length && length < 32768) path = std::filesystem::path(executable).parent_path() / "Resources" / "Icons" / iconFilename;
#endif
        }
        ImageDecodeOptions options; options.GenerateMips = options.AlphaWeighted = true;
        const auto source = LoadImageSource(path, options);
        if (!source.Image || source.Image->HDR) return 0;
        const auto& mip = source.Image->Mips.front();
        const int width = mip.Width, height = mip.Height;
        int left = width, top = height, right = -1, bottom = -1;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            if (mip.Pixels[(static_cast<size_t>(y) * width + x) * 4 + 3] <= 16) continue;
            left = (std::min)(left, x); top = (std::min)(top, y); right = (std::max)(right, x); bottom = (std::max)(bottom, y);
        }
        if (right >= left && bottom >= top) {
            left = (std::max)(0, left - 1); top = (std::max)(0, top - 1);
            right = (std::min)(width, right + 2); bottom = (std::min)(height, bottom + 2);
            m_IconRegions[iconFilename] = {glm::vec2(left, top) / glm::vec2(width, height),
                glm::vec2(right, bottom) / glm::vec2(width, height), glm::vec2(right - left, bottom - top)};
        }
        if (RHI::RHIDevice::Get().IsNativeReady()) {
            auto texture = ImageTextureCache::GetNative(source); if (!texture) return 0;
            auto old = m_IconCache.find(iconFilename);
            if (old != m_IconCache.end() && old->second.first == texture && old->second.second.IsValid()) return old->second.second.GetID();
            auto token = m_TextureRegistrar ? m_TextureRegistrar(*texture) : GuiTextureLease{};
            if (!token.IsValid()) return 0;
            const auto id = token.GetID(); m_IconCache[iconFilename] = {std::move(texture), std::move(token)}; return id;
        }
        TextureSpecification spec; spec.FlipVertically = false; spec.WrapS = spec.WrapT = TextureWrap::ClampToEdge;
        auto texture = ImageTextureCache::GetGL(source, spec); if (!texture) return 0;
        auto old = m_GLIconCache.find(iconFilename);
        if (old != m_GLIconCache.end() && old->second != texture && m_DeferredRelease) m_DeferredRelease([retired = std::move(old->second)] {});
        const auto id = texture->GetRendererID(); m_GLIconCache[iconFilename] = std::move(texture); return id;
    }

    void AssetPreviewPanel::FreeIconTextures() {
        m_IconCache.clear();
        if (!m_GLIconCache.empty()) {
            auto release = [textures = std::move(m_GLIconCache)] {};
            m_GLIconCache.clear();
            if (m_DeferredRelease) m_DeferredRelease(std::move(release)); else release();
        }
        m_IconRegions.clear();
    }

    void AssetPreviewPanel::UpdatePreviewLighting() {
        if (m_PreviewLight && m_PreviewLight.HasComponent<DirectionalLightComponent>()) {
            auto& dlc = m_PreviewLight.GetComponent<DirectionalLightComponent>();
            dlc.Intensity = m_EnableKeyLight ? 1.0f : 0.0f;
            dlc.AmbientIntensity = m_EnableKeyLight ? 0.65f : 0.25f;
        }
        if (m_PreviewFillLight && m_PreviewFillLight.HasComponent<DirectionalLightComponent>()) {
            auto& flc = m_PreviewFillLight.GetComponent<DirectionalLightComponent>();
            flc.Intensity = m_EnableSecondaryLight ? 1.0f : 0.0f;
        }
    }

    void AssetPreviewPanel::InitRHI(ImGuiLayer* imguiLayer) {
        m_ImGuiLayer = imguiLayer;
        m_TextureRegistrar = imguiLayer ? imguiLayer->GetTextureRegistrar() : GuiTextureRegistrar{};
        m_DeferredRelease = imguiLayer ? imguiLayer->GetDeferredReleaser() : GuiDeferredReleaser{};
        if (!m_ImGuiLayer) return;

        if (RHI::RHIDevice::Get().IsNativeReady()) {
            if (!m_RHIViewport) {
                m_RHIViewport = std::make_unique<RHIViewportDescriptor>();
            }
            RHIViewportDesc desc;
            desc.Width = m_ViewportWidth;
            desc.Height = m_ViewportHeight;
            desc.ColorFormat = RHI::RHITextureFormat::RGBA8_UNORM;
            desc.EnableDepth = true;
            desc.DepthFormat = RHI::RHITextureFormat::D32_FLOAT;
            desc.DebugName = "AssetPreviewViewport";
            m_RHIViewport->Init(m_ImGuiLayer, desc);
            m_SceneNeedsRebuild = true;
        }
    }

    void AssetPreviewPanel::SetSceneContext(Scene* scene, PhysicsScene* physicsScene, std::shared_ptr<VertexArray> fallbackMesh, const SceneSerializerContext* serializerContext) {
        m_Scene = scene;
        m_PhysicsScene = physicsScene;
        m_FallbackMesh = fallbackMesh;
        m_SerializerContext = serializerContext;

        if (m_HasAsset) m_SceneNeedsRebuild=true;
    }

    void AssetPreviewPanel::SetAsset(const std::filesystem::path& assetPath) {
        m_CurrentAssetPath = assetPath;
        if (assetPath.empty() || !std::filesystem::exists(assetPath)) {
            m_HasAsset = false;
            m_IsAssembly = false;
            m_CurrentPackage = {};
            m_CurrentAssembly = {};
            return;
        }

        std::string ext = PathToUtf8(assetPath.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool isMaterial = (ext == ".axleasset" && AssetDSL::PeekAssetKindFromFile(PathToUtf8(assetPath)) == AssetKind::Material);
        bool is3D = (ext == ".axlemesh" || ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || isMaterial ||
            (ext==".axleasset"&&AssetDSL::PeekAssetKindFromFile(PathToUtf8(assetPath))==AssetKind::Mesh));
        if (!is3D) {
            m_HasAsset = false;
            m_IsAssembly = false;
            m_CurrentPackage = {};
            m_CurrentAssembly = {};
            return;
        }

        if (isMaterial) {
            MaterialComponent mat;
            if (SceneHierarchyPanel::LoadMaterialAsset(assetPath, mat)) {
                m_IsAssembly = false;
                m_CurrentPackage = {};
                m_CurrentPackage.Name = PathToUtf8(assetPath.stem());
                m_CurrentPackage.BoundingSize = glm::vec3(1.0f);
                m_CurrentPackage.HalfExtent = glm::vec3(0.5f);
                m_CurrentPackage.Success = true;

                AxlePackagePart previewPart;
                previewPart.LoadedMesh.Success = true;
                std::shared_ptr<VertexArray> partVao = nullptr;

                switch (m_PreviewMeshType) {
                    case PreviewMeshType::Cube:
                        previewPart.Name = "MaterialCube";
                        partVao = PrimitiveMesh::CreateCube();
                        m_TotalVertices = 36;
                        m_TotalTriangles = 12;
                        break;
                    case PreviewMeshType::Cylinder:
                        previewPart.Name = "MaterialCylinder";
                        partVao = PrimitiveMesh::CreateCylinder(32);
                        m_TotalVertices = 192;
                        m_TotalTriangles = 96;
                        break;
                    case PreviewMeshType::Plane:
                        previewPart.Name = "MaterialPlane";
                        partVao = PrimitiveMesh::CreatePlane(1, 1);
                        m_TotalVertices = 4;
                        m_TotalTriangles = 2;
                        break;
                    case PreviewMeshType::Sphere:
                    default:
                        previewPart.Name = "MaterialSphere";
                        partVao = (m_SerializerContext && m_SerializerContext->SphereMesh) ? m_SerializerContext->SphereMesh : m_FallbackMesh;
                        if (!partVao) partVao = PrimitiveMesh::CreateSphere(64, 96);
                        m_TotalVertices = 768;
                        m_TotalTriangles = 512;
                        break;
                }

                previewPart.LoadedMesh.SourceData.ColorPrimary = mat.AlbedoColor;
                previewPart.LoadedMesh.SourceData.ColorSecondary = mat.AlbedoColor * 0.85f;
                previewPart.LoadedMesh.SourceData.PatternMode = 2; // Spherical PBR
                previewPart.LoadedMesh.SourceData.TileScale = 1.0f;
                previewPart.LoadedMesh.SourceData.Vertices = { glm::vec3(-0.5f), glm::vec3(0.5f) };
                previewPart.LoadedMesh.SourceData.Indices = { 0, 1, 0 };
                m_CurrentPackage.Parts.push_back(previewPart);
                m_PartVAOs = { partVao };

                m_HasAsset = true;
                m_Center = glm::vec3(0.0f);
                m_Size = glm::vec3(1.0f);
                m_MaxDim = 1.0f;
                m_Pitch = 0.0f;
                m_Yaw = 0.0f;
                m_ObjectYaw = 30.0f;
                m_ObjectPitch = -15.0f;
                m_Zoom = 1.0f;
                m_PanOffset = glm::vec3(0.0f);
                m_ActiveMaterial = mat;
                m_SceneNeedsRebuild = true;
                return;
            }
        }

        if (ext == ".axle" && AssemblySerializer::IsAssemblyFile(assetPath)) {
            m_IsAssembly = true;
            m_CurrentAssembly = AssemblySerializer::LoadAssemblyData(assetPath, m_SerializerContext);
            m_HasAsset = m_CurrentAssembly.IsValid;
            if (!m_HasAsset) return;

            // Compute aggregate bounding box from assembly
            glm::vec3 minBound(1e9f);
            glm::vec3 maxBound(-1e9f);
            size_t totalVertices = 0;
            size_t totalIndices = 0;

            std::function<void(const AssemblyNode&, const glm::mat4&)> computeBounds = [&](const AssemblyNode& node, const glm::mat4& parentMat) {
                glm::mat4 localMat = node.Transform.GetTransform();
                glm::mat4 worldMat = parentMat * localMat;
                if (node.HasMesh) {
                    const auto& source=node.Mesh.Source;
                    if (source && !source->Vertices.empty()) {
                        for (const auto& vertex : source->Vertices) {
                            const auto position=glm::vec3(worldMat*glm::vec4(vertex,1));
                            minBound=glm::min(minBound,position);maxBound=glm::max(maxBound,position);
                        }
                        totalVertices+=source->Vertices.size();totalIndices+=source->Indices.size();
                    } else {
                        auto half=glm::vec3(0.5f);
                        if (auto builtin=ParseBuiltinMesh(node.Mesh.AssetPath)) {
                            const auto* definition=GetBuiltinMeshDefinition(*builtin);
                            if (definition->Radius>0) half={definition->Radius,definition->Height>0?definition->Height*0.5f:definition->Radius,definition->Radius};
                        } else {
                            const auto loaded=AssetSerializer::LoadMesh(FileSystem::ResolveAssetPath(node.Mesh.AssetPath));
                            if (loaded.Success) half=loaded.SourceData.HalfExtent;
                        }
                        for (int x : {-1,1}) for (int y : {-1,1}) for (int z : {-1,1}) {
                            const auto position=glm::vec3(worldMat*glm::vec4(half*glm::vec3(x,y,z),1));
                            minBound=glm::min(minBound,position);maxBound=glm::max(maxBound,position);
                        }
                        totalVertices+=24;totalIndices+=36;
                    }
                }
                for (const auto& child : node.Children) {
                    computeBounds(child, worldMat);
                }
            };
            computeBounds(m_CurrentAssembly.RootNode, glm::mat4(1.0f));

            if (minBound.x > maxBound.x) {
                minBound = glm::vec3(-1.0f);
                maxBound = glm::vec3(1.0f);
            }

            m_Center = (minBound + maxBound) * 0.5f;
            m_Size = maxBound - minBound;
            m_MaxDim = std::max({ m_Size.x, m_Size.y, m_Size.z, 0.2f });
            m_TotalVertices = totalVertices;
            m_TotalTriangles = totalIndices / 3;

            // Reset camera framing
            m_Pitch = 20.0f;
            m_Yaw = 35.0f;
            m_ObjectYaw = 0.0f;
            m_ObjectPitch = 0.0f;
            m_Zoom = 1.0f;
            m_PanOffset = glm::vec3(0.0f);
            m_SceneNeedsRebuild = true;
            return;
        }

        m_IsAssembly = false;
        m_CurrentPackage = AssetSerializer::LoadPackage(assetPath);
        if (!m_CurrentPackage.Success || m_CurrentPackage.Parts.empty()) {
            m_HasAsset = false;
            return;
        }

        m_PartVAOs.clear();
        for (const auto& part : m_CurrentPackage.Parts) {
            m_PartVAOs.push_back(CreateVertexArrayFromMeshSource(part.LoadedMesh.SourceData));
        }

        m_HasAsset = true;

        // Compute aggregate bounding box
        glm::vec3 minBound(1e9f);
        glm::vec3 maxBound(-1e9f);
        size_t totalVertices = 0;
        size_t totalIndices = 0;

        for (const auto& part : m_CurrentPackage.Parts) {
            if (!part.LoadedMesh.Success) continue;
            const auto& verts = part.LoadedMesh.SourceData.Vertices;
            glm::mat4 localMat = glm::translate(glm::mat4(1.0f), part.LocalTranslation)
                               * glm::mat4_cast(part.LocalRotation)
                               * glm::scale(glm::mat4(1.0f), part.LocalScale);

            for (const auto& v : verts) {
                glm::vec3 worldV = glm::vec3(localMat * glm::vec4(v, 1.0f));
                minBound = glm::min(minBound, worldV);
                maxBound = glm::max(maxBound, worldV);
            }
            totalVertices += verts.size();
            totalIndices += part.LoadedMesh.SourceData.Indices.size();
        }

        if (minBound.x > maxBound.x) {
            minBound = glm::vec3(-0.5f);
            maxBound = glm::vec3(0.5f);
        }

        m_Center = (minBound + maxBound) * 0.5f;
        m_Size = maxBound - minBound;
        m_MaxDim = std::max({ m_Size.x, m_Size.y, m_Size.z, 0.05f });
        m_TotalVertices = totalVertices;
        m_TotalTriangles = totalIndices / 3;

        // Reset camera framing
        m_Pitch = 20.0f;
        m_Yaw = 35.0f;
        m_ObjectYaw = 0.0f;
        m_ObjectPitch = 0.0f;
        m_Zoom = 1.0f;
        m_PanOffset = glm::vec3(0.0f);
        m_SceneNeedsRebuild = true;
    }

    bool AssetPreviewPanel::IsMaterialPreview() const {
        return !m_CurrentAssetPath.empty() && m_CurrentAssetPath.extension() == ".axleasset"
            && AssetDSL::PeekAssetKindFromFile(PathToUtf8(m_CurrentAssetPath)) == AssetKind::Material;
    }

    glm::quat AssetPreviewPanel::GetPreviewRotation() const {
        float cameraYaw = glm::radians(m_Yaw);
        glm::vec3 cameraRight(std::cos(cameraYaw), 0.0f, -std::sin(cameraYaw));
        glm::quat rotation = glm::angleAxis(glm::radians(m_ObjectPitch), cameraRight)
             * glm::angleAxis(glm::radians(m_ObjectYaw), glm::vec3(0.0f, 1.0f, 0.0f));
        // The built-in plane lies on XZ; face it toward the fixed material camera.
        if (IsMaterialPreview() && m_PreviewMeshType == PreviewMeshType::Plane)
            rotation *= glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        return rotation;
    }

    float AssetPreviewPanel::GetPreviewScale() const {
        float meshScale = IsMaterialPreview() && m_PreviewMeshType == PreviewMeshType::Cube ? 2.0f / 3.0f : 1.0f;
        return meshScale / m_Zoom;
    }

    glm::mat4 AssetPreviewPanel::GetPreviewTransform() const {
        return glm::translate(glm::mat4(1.0f), m_PanOffset)
             * glm::mat4_cast(GetPreviewRotation())
             * glm::scale(glm::mat4(1.0f), glm::vec3(GetPreviewScale()));
    }

    void AssetPreviewPanel::SpawnCurrentAssetIntoScene() {
        if (!m_Scene || !m_HasAsset) return;

        if (m_IsAssembly) {
            auto spawned = m_Scene->InstantiateAssembly(m_CurrentAssetPath, m_PhysicsScene, glm::vec3(0.0f, 1.0f, 0.0f), m_SerializerContext);
            AXLE_INFO("AssetPreviewPanel: Instantiated Assembly '{0}'", PathToUtf8(m_CurrentAssetPath.filename()));
            return;
        }

        if (m_CurrentPackage.Parts.empty()) return;

        glm::vec3 spawnPos(0.0f, m_CurrentPackage.HalfExtent.y, 0.0f);
        if (m_CurrentPackage.IsPackage) {
            std::string rootTag = m_CurrentPackage.Name.empty() ? PathToUtf8(m_CurrentAssetPath.stem()) : m_CurrentPackage.Name;
            auto rootEntity = m_Scene->CreateEntity(rootTag);
            rootEntity.AddComponent<TransformComponent>(spawnPos, glm::vec3(1.0f));
            auto& rootHierarchy = rootEntity.AddComponent<HierarchyComponent>();

            if (m_PhysicsScene) {
                BoxBodyProps bProps;
                bProps.Position = spawnPos;
                bProps.HalfExtent = m_CurrentPackage.HalfExtent;
                bProps.IsDynamic = true;
                bProps.Friction = 0.70f;
                bProps.Restitution = 0.15f;
                auto bodyId = m_PhysicsScene->CreateBox(bProps);
                rootEntity.AddComponent<RigidBody3D>(bodyId, true);
                rootEntity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, m_CurrentPackage.HalfExtent, 0.70f, 0.15f);
            }

            for (size_t i = 0; i < m_CurrentPackage.Parts.size(); ++i) {
                const auto& part = m_CurrentPackage.Parts[i];
                auto vao = i < m_PartVAOs.size() ? m_PartVAOs[i] : nullptr;
                auto childEntity = m_Scene->CreateEntity(part.Name);
                auto& ct = childEntity.AddComponent<TransformComponent>(part.LocalTranslation, part.LocalScale);
                ct.Rotation = part.LocalRotation;

                auto partFullPath = m_CurrentAssetPath.parent_path() / part.RelativePath;
                childEntity.AddComponent<MeshInstance3D>(
                    vao ? vao : m_FallbackMesh,
                    PathToUtf8(partFullPath),
                    part.LoadedMesh.SourceData.PatternMode,
                    part.LoadedMesh.SourceData.TileScale,
                    part.LoadedMesh.SourceData.ColorPrimary,
                    part.LoadedMesh.SourceData.ColorSecondary
                );

                childEntity.AddComponent<HierarchyComponent>(static_cast<entt::entity>(rootEntity));
                rootHierarchy.Children.push_back(static_cast<entt::entity>(childEntity));
            }
            AXLE_INFO("AssetPreviewPanel: Instantiated package '{0}' ({1} subparts)", rootTag, m_CurrentPackage.Parts.size());
        } else {
            const auto& part = m_CurrentPackage.Parts[0];
            auto vao = !m_PartVAOs.empty() ? m_PartVAOs[0] : nullptr;
            std::string tag = m_CurrentPackage.Name.empty() ? PathToUtf8(m_CurrentAssetPath.stem()) : m_CurrentPackage.Name;
            auto entity = m_Scene->CreateEntity(tag);
            entity.AddComponent<TransformComponent>(spawnPos, part.LocalScale);
            entity.AddComponent<MeshInstance3D>(
                vao ? vao : m_FallbackMesh,
                PathToUtf8(m_CurrentAssetPath),
                part.LoadedMesh.SourceData.PatternMode,
                part.LoadedMesh.SourceData.TileScale,
                part.LoadedMesh.SourceData.ColorPrimary,
                part.LoadedMesh.SourceData.ColorSecondary
            );

            if (m_PhysicsScene) {
                BoxBodyProps bProps;
                bProps.Position = spawnPos;
                bProps.HalfExtent = part.LoadedMesh.SourceData.HalfExtent;
                bProps.IsDynamic = true;
                auto bodyId = m_PhysicsScene->CreateBox(bProps);
                entity.AddComponent<RigidBody3D>(bodyId, true);
                entity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, part.LoadedMesh.SourceData.HalfExtent, 0.70f, 0.15f);
            }
            AXLE_INFO("AssetPreviewPanel: Instantiated single mesh '{0}'", tag);
        }
    }

    void AssetPreviewPanel::SetPreviewMeshType(PreviewMeshType type) {
        m_PreviewMeshType = type;
        m_SceneNeedsRebuild = true;
        if (IsMaterialPreview()) {
            m_ObjectYaw = type == PreviewMeshType::Plane ? 0.0f : 30.0f;
            m_ObjectPitch = type == PreviewMeshType::Plane ? 0.0f : -15.0f;
            m_Zoom = 1.0f;
            m_PanOffset = glm::vec3(0.0f);
        }
        if (!m_SerializerContext) return;
        std::shared_ptr<VertexArray> vao = m_SerializerContext->SphereMesh;
        if (type == PreviewMeshType::Cube) vao = m_FallbackMesh;
        else if (type == PreviewMeshType::Cylinder) vao = m_SerializerContext->CylinderMesh;
        else if (type == PreviewMeshType::Plane) vao = m_SerializerContext->PlaneMesh;
        else if (type == PreviewMeshType::Custom && m_CustomMeshVAO) vao = m_CustomMeshVAO;

        if (!m_PartVAOs.empty()) {
            m_PartVAOs[0] = vao;
        }
    }

    void AssetPreviewPanel::LoadCustomPreviewMesh(const std::filesystem::path& meshPath) {
        if (!std::filesystem::exists(meshPath)) return;
        m_CustomMeshPath = meshPath;
        AxleLoadedPackage pkg = AssetSerializer::LoadPackage(meshPath);
        if (pkg.Success && !pkg.Parts.empty()) {
            m_CustomMeshVAO = CreateVertexArrayFromMeshSource(pkg.Parts[0].LoadedMesh.SourceData);
            m_TotalVertices = pkg.Parts[0].LoadedMesh.SourceData.Vertices.size();
            m_TotalTriangles = pkg.Parts[0].LoadedMesh.SourceData.Indices.size() / 3;
            m_SceneNeedsRebuild = true;
            SetPreviewMeshType(PreviewMeshType::Custom);
        }
    }

    void AssetPreviewPanel::UpdateMaterialPreview(const MaterialComponent& mat) {
        m_ActiveMaterial = mat;
        if (m_PreviewEntity && m_PreviewScene && m_PreviewEntity.HasComponent<MaterialComponent>()) {
            auto& comp = m_PreviewEntity.GetComponent<MaterialComponent>();
            comp = mat;
            comp.SyncToRHI();
        } else {
            m_SceneNeedsRebuild = true;
        }

        if (!m_CurrentPackage.Parts.empty()) {
            m_CurrentPackage.Parts[0].LoadedMesh.SourceData.ColorPrimary = mat.AlbedoColor;
            m_CurrentPackage.Parts[0].LoadedMesh.SourceData.ColorSecondary = mat.AlbedoColor * 0.85f;
        }
    }

    void AssetPreviewPanel::SetSunAzimuth(float deg) {
        m_SunAzimuth = deg;
        if (m_PreviewLight && m_PreviewScene && m_PreviewLight.HasComponent<DirectionalLightComponent>()) {
            m_PreviewLight.GetComponent<DirectionalLightComponent>().Azimuth = m_SunAzimuth;
        }
    }

    void AssetPreviewPanel::SetSunElevation(float deg) {
        m_SunElevation = std::clamp(deg, 5.0f, 85.0f);
        if (m_PreviewLight && m_PreviewScene && m_PreviewLight.HasComponent<DirectionalLightComponent>()) {
            m_PreviewLight.GetComponent<DirectionalLightComponent>().Elevation = m_SunElevation;
        }
    }

    void AssetPreviewPanel::RebuildPreviewScene() {
        if (!m_PreviewScene) {
            m_PreviewScene = std::make_shared<Scene>();
        }
        m_PreviewScene->Clear();
        // Mesh parts share one transform. Lights remain outside this hierarchy.
        m_PreviewEntity = m_PreviewScene->CreateEntity("PreviewObject");

        bool isMaterial = (!m_CurrentAssetPath.empty() && 
                           m_CurrentAssetPath.extension() == ".axleasset" && 
                           AssetDSL::PeekAssetKindFromFile(PathToUtf8(m_CurrentAssetPath)) == AssetKind::Material);

        if (isMaterial) {
            auto& tc = m_PreviewEntity.GetComponent<TransformComponent>();
            tc.Translation = glm::vec3(0.0f);
            tc.Scale = glm::vec3(1.0f);

            auto& mc = m_PreviewEntity.AddComponent<MeshInstance3D>();
            switch (m_PreviewMeshType) {
                case PreviewMeshType::Sphere:
                    mc.AssetPath = "builtin://sphere";
                    mc.RHIMeshAsset = RHIMeshRegistry::Get().GetSphere();
                    mc.VAO = (m_SerializerContext && m_SerializerContext->SphereMesh) ? m_SerializerContext->SphereMesh : PrimitiveMesh::CreateSphere(64, 96);
                    break;
                case PreviewMeshType::Cube:
                    mc.AssetPath = "builtin://cube";
                    mc.RHIMeshAsset = RHIMeshRegistry::Get().GetCube();
                    mc.VAO = PrimitiveMesh::CreateCube();
                    break;
                case PreviewMeshType::Cylinder:
                    mc.AssetPath = "builtin://cylinder";
                    mc.RHIMeshAsset = RHIMeshRegistry::Get().GetCylinder();
                    mc.VAO = PrimitiveMesh::CreateCylinder(32);
                    break;
                case PreviewMeshType::Plane:
                    mc.AssetPath = "builtin://plane";
                    mc.RHIMeshAsset = RHIMeshRegistry::Get().GetPlane();
                    mc.VAO = PrimitiveMesh::CreatePlane(1, 1);
                    break;
                case PreviewMeshType::Custom:
                    if (!m_CustomMeshPath.empty()) {
                        mc.AssetPath = PathToUtf8(m_CustomMeshPath);
                        mc.RHIMeshAsset = RHIMeshRegistry::Get().GetOrCreateMesh(PathToUtf8(m_CustomMeshPath));
                        mc.VAO = m_CustomMeshVAO;
                    }
                    break;
            }

            auto& matComp = m_PreviewEntity.AddComponent<MaterialComponent>(m_ActiveMaterial);
            matComp.SyncToRHI();
        } else if (m_IsAssembly) {
            auto centeredRoot=m_PreviewScene->CreateEntity("Assembly origin");
            centeredRoot.SetParent(m_PreviewEntity,false);
            centeredRoot.GetComponent<TransformComponent>().Translation=-m_Center;
            std::function<void(const AssemblyNode&, Entity)> spawnNodes = [&](const AssemblyNode& node, Entity parent) {
                auto part=node.AuthoredUUID ? m_PreviewScene->CreateEntityWithUUID(UUID(node.AuthoredUUID), node.Tag.empty()?"Node":node.Tag)
                                            : m_PreviewScene->CreateEntity(node.Tag.empty()?"Node":node.Tag);
                part.SetParent(parent,false);
                part.GetComponent<TransformComponent>()=node.Transform;
                if (node.HasMesh) part.AddComponent<MeshInstance3D>()=node.Mesh;
                if (node.HasMaterial) part.AddComponent<MaterialComponent>()=node.Material;
                if (node.HasSkeletalMesh) part.AddComponent<SkeletalMeshComponent>()=node.SkeletalMesh;
                if (node.HasAnimator) part.AddComponent<AnimatorComponent>()=node.Animator;
                for (const auto& section : node.PassiveSections) m_PreviewScene->StorePassiveSection(part, section.SectionName, section.Lines);
                for (const auto& child : node.Children) spawnNodes(child,part);
            };
            spawnNodes(m_CurrentAssembly.RootNode,centeredRoot);
        } else {
            for (size_t i = 0; i < m_CurrentPackage.Parts.size(); ++i) {
                const auto& part = m_CurrentPackage.Parts[i];
                auto partEnt = m_PreviewScene->CreateEntity(part.Name.empty() ? "Part" : part.Name);
                partEnt.SetParent(m_PreviewEntity, false);
                auto& ptc = partEnt.GetComponent<TransformComponent>();
                ptc.Translation = part.LocalTranslation - m_Center;
                ptc.Rotation = part.LocalRotation;
                ptc.Scale = part.LocalScale;

                auto& pmc = partEnt.AddComponent<MeshInstance3D>();
                if (!m_CurrentAssetPath.empty()) {
                    auto partFullPath = m_CurrentAssetPath.parent_path() / part.RelativePath;
                    pmc.AssetPath = PathToUtf8(partFullPath);

                }
                pmc.VAO = i < m_PartVAOs.size() ? m_PartVAOs[i] : nullptr;
                pmc.Source=std::make_shared<AxleMeshSource>(part.LoadedMesh.SourceData);
                pmc.Color1 = part.LoadedMesh.SourceData.ColorPrimary;
                pmc.Color2 = part.LoadedMesh.SourceData.ColorSecondary;
                pmc.PatternMode = part.LoadedMesh.SourceData.PatternMode;
                pmc.TileScale = part.LoadedMesh.SourceData.TileScale;
            }
        }

        // Godot-style rig: a neutral light from above/front and a dimmer light
        // from below. Add fill first so the key is the primary directional sun.
        m_PreviewFillLight = m_PreviewScene->CreateEntity("PreviewFillLight");
        auto& flc = m_PreviewFillLight.AddComponent<DirectionalLightComponent>();
        flc.Azimuth = 0.0f;
        flc.Elevation = -90.0f;
        flc.Color = glm::vec3(0.7f);
        flc.Intensity = m_EnableSecondaryLight ? 1.0f : 0.0f;
        flc.AmbientIntensity = 0.65f;
        flc.CastShadows = false;

        m_PreviewLight = m_PreviewScene->CreateEntity("PreviewSunLight");
        auto& dlc = m_PreviewLight.AddComponent<DirectionalLightComponent>();
        dlc.Azimuth = m_SunAzimuth;
        dlc.Elevation = m_SunElevation;
        dlc.Color = glm::vec3(1.0f);
        dlc.Intensity = m_EnableKeyLight ? 1.0f : 0.0f;
        dlc.AmbientIntensity = m_EnableKeyLight ? 0.65f : 0.25f;

        dlc.CastShadows = false;

        m_SceneNeedsRebuild = false;
    }

    void AssetPreviewPanel::RenderPreview(float width, float height) {
        if (!m_HasAsset) return;

        if (m_SceneNeedsRebuild || !m_PreviewScene) {
            RebuildPreviewScene();
        }
        if (!m_PreviewScene) return;

        // Apply manual rotation even while automatic rotation is paused.
        if (m_PreviewEntity && m_PreviewEntity.HasComponent<TransformComponent>()) {
            auto& tc = m_PreviewEntity.GetComponent<TransformComponent>();
            tc.Translation = m_PanOffset;
            tc.Rotation = GetPreviewRotation();
            tc.Scale = glm::vec3(GetPreviewScale());
        }

        // Camera setup
        float pitchRad = glm::radians(m_Pitch);
        float yawRad = glm::radians(m_Yaw);
        bool isMaterial = IsMaterialPreview();
        float distance = m_MaxDim * (isMaterial ? 3.0f : 2.2f);

        glm::vec3 camPos = glm::vec3(
            distance * std::cos(pitchRad) * std::sin(yawRad),
            distance * std::sin(pitchRad),
            distance * std::cos(pitchRad) * std::cos(yawRad)
        );

        // Two fixed world-space lights, independent of all mouse gestures.
        if (m_PreviewLight && m_PreviewLight.HasComponent<DirectionalLightComponent>()) {
            auto& dlc = m_PreviewLight.GetComponent<DirectionalLightComponent>();
            dlc.Elevation = m_SunElevation;
            dlc.Azimuth = m_SunAzimuth;
            dlc.Intensity = m_EnableKeyLight ? 1.0f : 0.0f;
            dlc.AmbientIntensity = m_EnableKeyLight ? 0.65f : 0.25f;
        }
        if (m_PreviewFillLight && m_PreviewFillLight.HasComponent<DirectionalLightComponent>()) {
            auto& flc = m_PreviewFillLight.GetComponent<DirectionalLightComponent>();
            flc.Intensity = m_EnableSecondaryLight ? 1.0f : 0.0f;
        }

        float aspect = (height > 0.0f) ? (width / height) : 1.0f;
        PerspectiveCamera camera(isMaterial ? 20.0f : 45.0f, aspect, 0.05f, distance * 8.0f);
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        camera.SetViewMatrix(view, camPos);

        SceneRenderSettings settings;
        settings.EnableShadows = false;
        settings.EnableSkybox = false;
        settings.EnableStudioLightLobes = false;
        settings.EnableToneMapping = true;
        settings.EnableFXAA = true;
        settings.MSAASamples = 4;
        settings.ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // Transparent clear for Godot checkerboard

        ViewRequest request;
        request.Kind = RenderViewKind::AssetPreview;
        request.Width = m_ViewportWidth;
        request.Height = m_ViewportHeight;
        request.Camera = camera;
        request.Settings = settings;
        request.Name = "AssetPreview";

        if (RHI::RHIDevice::Get().IsNativeReady() && m_RHIViewport && m_RHIViewport->IsValid()) {
            if (!m_RHIViewport->SetRenderOptions(settings.MSAASamples, settings.EnableToneMapping, settings.EnableFXAA)) return;
            request.ColorTarget = m_RHIViewport->GetColorTexture();
            request.DepthTarget = m_RHIViewport->GetDepthTexture();
            request.ViewTargets = m_RHIViewport->GetTargets();
        } else if (m_Framebuffer) {
            request.GLTargetFBO = m_Framebuffer;
        }

        const auto result=RenderService::Get().RenderView(*m_PreviewScene, request);
        m_RenderDiagnostic=result.ErrorMessage;
        std::vector<std::string> readiness;
        if (!m_PreviewScene->ExtensionsReady(readiness)) {
            for (const auto& error : readiness) { if (!m_RenderDiagnostic.empty()) m_RenderDiagnostic += '\n'; m_RenderDiagnostic += error; }
        }
        if (!result.Success) AXLE_CORE_WARN("Asset preview failed: {0}",m_RenderDiagnostic);
    }

    void AssetPreviewPanel::RenderPreviewViewport() {
        if (!m_HasAsset) return;

        if (m_TurntableActive && !m_IsViewportHovered) {
            m_ObjectYaw += ImGui::GetIO().DeltaTime * m_TurntableSpeed * 30.0f;
            if (m_ObjectYaw > 360.0f) m_ObjectYaw -= 360.0f;
        }

        bool isMaterial = IsMaterialPreview();
        float uiScale = ImGui::GetFontSize() / 15.5f;
        float availW = ImGui::GetContentRegionAvail().x;
        float viewportH = std::clamp(availW * 0.54f, 150.0f, 240.0f);

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + availW, p0.y + viewportH);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 1. Godot-style Checkerboard background grid (Refined Neutral Mid-Gray Studio)
        const float tileSize = 32.0f * uiScale;
        for (float y = p0.y; y < p1.y; y += tileSize) {
            for (float x = p0.x; x < p1.x; x += tileSize) {
                int cx = static_cast<int>((x - p0.x) / tileSize);
                int cy = static_cast<int>((y - p0.y) / tileSize);
                ImU32 col = ((cx + cy) % 2 == 0) ? IM_COL32(128, 128, 128, 255) : IM_COL32(64, 64, 64, 255);
                ImVec2 tileMax(std::min(x + tileSize, p1.x), std::min(y + tileSize, p1.y));
                drawList->AddRectFilled(ImVec2(x, y), tileMax, col);
            }
        }

        // 2. Render 3D Scene into Viewport
        if (RHI::RHIDevice::Get().IsNativeReady()) {
            if (!m_RHIViewport && m_ImGuiLayer) {
                InitRHI(m_ImGuiLayer);
            }
            if (m_RHIViewport && m_RHIViewport->IsValid()) {
                if ((uint32_t)availW != m_ViewportWidth || (uint32_t)viewportH != m_ViewportHeight) {
                    m_ViewportWidth = (uint32_t)availW;
                    m_ViewportHeight = (uint32_t)viewportH;
                    m_RHIViewport->Resize(m_ViewportWidth, m_ViewportHeight);
                }
                RenderPreview(availW, viewportH);
                uint64_t rhiTexID = m_RHIViewport->GetImTextureID();
                if (rhiTexID != 0) {
                    glm::vec2 uv0 = m_RHIViewport->GetUV0();
                    glm::vec2 uv1 = m_RHIViewport->GetUV1();
                    ImGui::Image((ImTextureID)(intptr_t)rhiTexID, ImVec2(availW, viewportH), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
                }
            }
        } else if (Window::HasActiveContext() && m_Framebuffer) {
            if ((uint32_t)availW != m_ViewportWidth || (uint32_t)viewportH != m_ViewportHeight) {
                m_ViewportWidth = (uint32_t)availW;
                m_ViewportHeight = (uint32_t)viewportH;
                m_Framebuffer=Framebuffer::Create({m_ViewportWidth,m_ViewportHeight,1,false});
            }
            RenderPreview(availW, viewportH);
            u32 texID = m_Framebuffer->GetColorAttachmentRendererID();
            if(m_DeferredRelease)m_DeferredRelease([owner=m_Framebuffer]{});
            ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(availW, viewportH), ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::Dummy(ImVec2(availW, viewportH));
        }

        m_IsViewportHovered = ImGui::IsItemHovered();

        // Drag & Drop custom mesh into preview turntable
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const wchar_t* pathStr = (const wchar_t*)payload->Data;
                std::filesystem::path droppedPath(pathStr);
                std::string ext = PathToUtf8(droppedPath.extension());
                std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                if (ext == ".axlemesh" || ext == ".axle" || ext == ".obj" || ext == ".glb" || ext == ".gltf") {
                    LoadCustomPreviewMesh(droppedPath);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Flat, inset icon strips match the reference without covering the material.
        // Place every button explicitly: ImGui's normal line flow would put the
        // second light at the left edge instead of below the first one.
        const float inset = 2.0f * uiScale;
        const float buttonSize = 28.0f * uiScale;
        const float rowStep = 32.0f * uiScale;
        const float iconSize = 16.0f * uiScale;
        drawList->PushClipRect(p0, p1, true);
        auto DrawPreviewIcon = [&](const char* id, const char* icon, const char* fallback,
                                   ImVec2 position, bool enabled, const char* tooltip) {
            ImGui::SetCursorScreenPos(position);
            ImGui::PushID(id);
            bool clicked = ImGui::InvisibleButton("##button", ImVec2(buttonSize, buttonSize));
            bool hovered = ImGui::IsItemHovered();
            if (hovered || ImGui::IsItemActive()) m_IsViewportHovered = false;
            if (hovered) {
                drawList->AddRectFilled(position, ImVec2(position.x + buttonSize, position.y + buttonSize),
                                       IM_COL32(255, 255, 255, 28), 3.0f * uiScale);
                ImGui::SetTooltip("%s", tooltip);
            }
            uint64_t texture = GetOrCreateIconTexture(icon);
            ImU32 tint = enabled ? IM_COL32(235, 235, 235, 255) : IM_COL32(155, 155, 155, 210);
            ImVec2 iconPos(position.x + (buttonSize - iconSize) * 0.5f,
                           position.y + (buttonSize - iconSize) * 0.5f);
            if (texture) {
                drawList->AddImage((ImTextureID)(intptr_t)texture, iconPos,
                                  ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                  ImVec2(0, 0), ImVec2(1, 1), tint);
            } else {
                ImVec2 textSize = ImGui::CalcTextSize(fallback);
                drawList->AddText(ImVec2(position.x + (buttonSize - textSize.x) * 0.5f,
                                         position.y + (buttonSize - textSize.y) * 0.5f), tint, fallback);
            }
            ImGui::PopID();
            return clicked;
        };

        if (isMaterial) {
            float left = p0.x + inset;
            float right = p1.x - inset - buttonSize;
            if (DrawPreviewIcon("sphere", "icon_mesh_sphere.png", "O", ImVec2(left, p0.y + inset),
                                m_PreviewMeshType == PreviewMeshType::Sphere, "Preview on sphere"))
                SetPreviewMeshType(PreviewMeshType::Sphere);
            if (DrawPreviewIcon("cube", "icon_mesh_cube.png", "#", ImVec2(left, p0.y + inset + rowStep),
                                m_PreviewMeshType == PreviewMeshType::Cube, "Preview on cube"))
                SetPreviewMeshType(PreviewMeshType::Cube);
            if (DrawPreviewIcon("plane", "icon_mesh_plane.png", "-", ImVec2(left, p0.y + inset + rowStep * 2.0f),
                                m_PreviewMeshType == PreviewMeshType::Plane, "Preview on quad"))
                SetPreviewMeshType(PreviewMeshType::Plane);
            if (DrawPreviewIcon("light1", "icon_light_1.png", "1", ImVec2(right, p0.y + inset),
                                m_EnableKeyLight, "Toggle first light")) {
                m_EnableKeyLight = !m_EnableKeyLight;
                UpdatePreviewLighting();
            }
            if (DrawPreviewIcon("light2", "icon_light_2.png", "2", ImVec2(right, p0.y + inset + rowStep),
                                m_EnableSecondaryLight, "Toggle second light")) {
                m_EnableSecondaryLight = !m_EnableSecondaryLight;
                UpdatePreviewLighting();
            }
        }
        drawList->PopClipRect();

        // Keep extra preview actions available without adding toolbar clutter.
        if (m_IsViewportHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            ImGui::OpenPopup("PreviewOptions");
        if (ImGui::BeginPopup("PreviewOptions")) {
            ImGui::MenuItem("Auto-rotate", nullptr, &m_TurntableActive);
            if (ImGui::BeginMenu("Preview mesh")) {
                if (ImGui::MenuItem("Sphere")) SetPreviewMeshType(PreviewMeshType::Sphere);
                if (ImGui::MenuItem("Cube")) SetPreviewMeshType(PreviewMeshType::Cube);
                if (ImGui::MenuItem("Quad")) SetPreviewMeshType(PreviewMeshType::Plane);
                if (ImGui::MenuItem("Cylinder")) SetPreviewMeshType(PreviewMeshType::Cylinder);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reset preview")) {
                m_Pitch = isMaterial ? 0.0f : 20.0f;
                m_Yaw = isMaterial ? 0.0f : 35.0f;
                m_ObjectPitch = isMaterial && m_PreviewMeshType != PreviewMeshType::Plane ? -15.0f : 0.0f;
                m_ObjectYaw = isMaterial && m_PreviewMeshType != PreviewMeshType::Plane ? 30.0f : 0.0f;
                m_Zoom = 1.0f;
                m_PanOffset = glm::vec3(0.0f);
                m_SunAzimuth = 45.0f;
                m_SunElevation = 35.264f;
                m_EnableKeyLight = true;
                m_EnableSecondaryLight = true;
                m_SceneNeedsRebuild = true;
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsPopupOpen("PreviewOptions")) m_IsViewportHovered = false;

        // Full-width resource type row, like the BaseMaterial3D row in the reference.
        const float footerGap = 4.0f * uiScale;
        const float footerHeight = 32.0f * uiScale;
        ImVec2 footer0(p0.x, p1.y + footerGap);
        ImVec2 footer1(p1.x, footer0.y + footerHeight);
        drawList->AddRectFilled(footer0, footer1, IM_COL32(56, 56, 56, 255), 3.0f * uiScale);
        std::string badgeText = isMaterial ? "BaseMaterial3D" : (m_IsAssembly ? "Assembly3D" : "MeshInstance3D");
        ImVec2 textSize = ImGui::CalcTextSize(badgeText.c_str());
        float labelWidth = textSize.x + iconSize + 6.0f * uiScale;
        float labelX = p0.x + (availW - labelWidth) * 0.5f;
        uint64_t typeIcon = GetOrCreateIconTexture("icon_mesh_cube.png");
        if (typeIcon) {
            ImVec2 iconPos(labelX, footer0.y + (footerHeight - iconSize) * 0.5f);
            drawList->AddImage((ImTextureID)(intptr_t)typeIcon, iconPos,
                              ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                              ImVec2(0, 0), ImVec2(1, 1), IM_COL32(165, 165, 165, 255));
        }
        drawList->AddText(ImVec2(labelX + iconSize + 6.0f * uiScale, footer0.y + (footerHeight - textSize.y) * 0.5f),
                          IM_COL32(210, 210, 210, 255), badgeText.c_str());
        ImGui::SetCursorScreenPos(footer0);
        ImGui::Dummy(ImVec2(availW, footerHeight));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", PathToUtf8(m_CurrentAssetPath.filename()).c_str());

        if (m_IsViewportHovered) {
            // Zoom scales the object, leaving the camera and lights fixed.
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                m_Zoom -= wheel * 0.12f;
                m_Zoom = std::clamp(m_Zoom, 0.25f, 4.0f);
            }

            // Left drag rotates the object around its center, not the camera.
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                m_ObjectYaw += delta.x * 0.75f;
                m_ObjectPitch += delta.y * 0.75f;
                m_ObjectPitch = std::clamp(m_ObjectPitch, -85.0f, 85.0f);
            }

            // Pan moves the object along the fixed camera's screen axes.
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                float cameraYaw = glm::radians(m_Yaw);
                float cameraPitch = glm::radians(m_Pitch);
                glm::vec3 right(std::cos(cameraYaw), 0.0f, -std::sin(cameraYaw));
                glm::vec3 up(-std::sin(cameraPitch) * std::sin(cameraYaw), std::cos(cameraPitch),
                             -std::sin(cameraPitch) * std::cos(cameraYaw));
                m_PanOffset += (right * delta.x - up * delta.y) * 0.005f * m_MaxDim;
            }
        }
    }

    void AssetPreviewPanel::OnImGuiRender(bool* p_open) {
        if (p_open && !*p_open) return;

        ImGui::Begin("Asset Preview", p_open);

        if (!m_HasAsset) {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.45f, 0.70f, 0.95f, 1.0f), "  No 3D Asset Selected");
            ImGui::Spacing();
            ImGui::TextDisabled("  Click any 3D model asset (.axle, .glb, .obj)\n  in the Content Browser to inspect it in 3D.");
            ImGui::End();
            return;
        }

        RenderPreviewViewport();

        ImGui::Spacing();

        // Action Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.78f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.58f, 0.90f, 1.0f));
        if (ImGui::Button("+ Instantiate into Scene", ImVec2(-1.0f, 28.0f))) {
            SpawnCurrentAssetIntoScene();
        }
        ImGui::PopStyleColor(2);

        ImGui::End();
    }

}
