#include <Axle/AssetThumbnailManager.hpp>
#include <Axle/Application.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/RenderService.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/ViewRequest.hpp>
#include <Axle/Framebuffer.hpp>
#include <Extensions/NRIHelper.h>
#include <Axle/ImageSource.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/Log.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/FileSystem.hpp>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

namespace Axle {

    AssetThumbnailManager* AssetThumbnailManager::s_Instance = nullptr;

    uint64_t AssetThumbnailManager::GetPreview(const std::filesystem::path& path) {
        return s_Instance ? s_Instance->GetOrCreateThumbnail(path) : 0;
    }

    AssetThumbnailManager::AssetThumbnailManager() {
        s_Instance = this;
    }

    AssetThumbnailManager::~AssetThumbnailManager() {
        Clear();
        if (s_Instance == this) {
            s_Instance = nullptr;
        }
    }

    void AssetThumbnailManager::InvalidateThumbnail(const std::filesystem::path& path) {
        std::string key = path.lexically_normal().string();
        m_GuiThumbnails.erase(key);
        std::vector<uint64_t> handles;
        std::vector<std::shared_ptr<RHI::RHITexture>> textures;
        std::vector<std::shared_ptr<Texture2D>> loaded;
        std::vector<u32> generated;
        if (auto it = m_ThumbnailCache.find(key); it != m_ThumbnailCache.end()) {
            handles.push_back(it->second);
            if (auto id = std::find(m_GeneratedTextures.begin(), m_GeneratedTextures.end(), it->second);
                id != m_GeneratedTextures.end()) {
                generated.push_back(*id);
                m_GeneratedTextures.erase(id);
            }
            m_ThumbnailCache.erase(it);
        }
        if (auto it = m_RHIThumbnails.find(key); it != m_RHIThumbnails.end()) {
            textures.push_back(std::move(it->second));
            m_RHIThumbnails.erase(it);
        }
        if (auto it = m_LoadedTextures.find(key); it != m_LoadedTextures.end()) {
            loaded.push_back(std::move(it->second));
            m_LoadedTextures.erase(it);
        }
        m_ThumbnailDimensions.erase(key);
        m_MetaCache.erase(key);
        m_AssetRevisions.erase(key);

        auto qIt = std::find(m_PendingQueue.begin(), m_PendingQueue.end(), path);
        if (qIt != m_PendingQueue.end()) {
            m_PendingQueue.erase(qIt);
        }

        RetireTextures(std::move(handles), std::move(textures), std::move(loaded), std::move(generated));
    }

    void AssetThumbnailManager::Invalidate(const std::filesystem::path& path) {
        if (s_Instance) {
            s_Instance->InvalidateThumbnail(path);
        }
    }

    void AssetThumbnailManager::SetImGuiLayer(ImGuiLayer* layer) {
        m_ImGuiLayer = layer;
        m_TextureRegistrar = layer ? layer->GetTextureRegistrar() : GuiTextureRegistrar{};
        m_DeferredRelease = layer ? layer->GetDeferredReleaser() : GuiDeferredReleaser{};
    }

    void AssetThumbnailManager::RetireTextures(std::vector<uint64_t> handles,
            std::vector<std::shared_ptr<RHI::RHITexture>> textures,
            std::vector<std::shared_ptr<Texture2D>> loaded, std::vector<u32> generated) {
        // Native registrations retain complete payload generations themselves.
        if (loaded.empty() && generated.empty()) return;
        auto release = [loaded = std::move(loaded), generated = std::move(generated)] {
            if (Window::HasActiveContext()) for (auto id : generated) if (id) glDeleteTextures(1, &id);
        };
        if (m_DeferredRelease) m_DeferredRelease(std::move(release)); else release();
    }

    void AssetThumbnailManager::Clear() {
        m_GuiThumbnails.clear();
        std::vector<uint64_t> handles;
        std::vector<std::shared_ptr<RHI::RHITexture>> textures;
        std::vector<std::shared_ptr<Texture2D>> loaded;
        for (const auto& entry : m_ThumbnailCache) if (entry.second) handles.push_back(entry.second);
        for (auto& entry : m_RHIThumbnails) textures.push_back(std::move(entry.second));
        for (auto& entry : m_LoadedTextures) loaded.push_back(std::move(entry.second));
        RetireTextures(std::move(handles), std::move(textures), std::move(loaded), std::move(m_GeneratedTextures));
        m_GeneratedTextures.clear();
        m_ThumbnailCache.clear();
        m_ThumbnailDimensions.clear();
        m_RHIThumbnails.clear();
        m_MetaCache.clear();
        m_LoadedTextures.clear();
        m_AssetRevisions.clear();
        m_PendingQueue.clear();
    }

    glm::vec2 AssetThumbnailManager::GetThumbnailDimensions(const std::filesystem::path& path) const {
        std::string key = path.lexically_normal().string();
        auto it = m_ThumbnailDimensions.find(key);
        if (it != m_ThumbnailDimensions.end()) {
            return it->second;
        }
        return glm::vec2(1.0f, 1.0f);
    }

    const AssetCachedMeta* AssetThumbnailManager::GetCachedMeta(const std::filesystem::path& path) const {
        std::string key = path.lexically_normal().string();
        auto it = m_MetaCache.find(key);
        if (it != m_MetaCache.end()) {
            return &it->second;
        }
        return nullptr;
    }

    uint64_t AssetThumbnailManager::GetOrCreateThumbnail(const std::filesystem::path& path) {
        if (!m_ImGuiLayer) {
            SetImGuiLayer(dynamic_cast<ImGuiLayer*>(Application::Get().GetUIContext()));
        }

        std::string key = path.lexically_normal().string();
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

        // Resolve source image path if this is a Texture descriptor or raw media
        std::filesystem::path imagePath;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr") {
            imagePath = FileSystem::ResolveAssetPath(path);
        } else if (ext == ".axleasset" && AssetDSL::PeekAssetKindFromFile(path.string()) == AssetKind::Texture) {
            TextureDescriptor desc;
            if (TextureDescriptor::Load(path.string(), desc) && !desc.Source.empty()) {
                imagePath = FileSystem::ResolveAssetPath(desc.Source);
            }
        }

        if (!imagePath.empty()) {
            const auto source = LoadImageSource(path, {});
            if (RHI::RHIDevice::Get().IsNativeReady()) {
                auto texture = ImageTextureCache::GetNative(source);
                if (!texture) return 0;
                auto old = m_RHIThumbnails.find(key); auto registration = m_GuiThumbnails.find(key);
                if (old != m_RHIThumbnails.end() && old->second == texture && registration != m_GuiThumbnails.end() && registration->second.IsValid()) return registration->second.GetID();
                auto token = m_TextureRegistrar ? m_TextureRegistrar(*texture) : GuiTextureLease{};
                if (!token.IsValid()) return 0;
                const auto id = token.GetID();
                m_ThumbnailDimensions[key] = {texture->GetWidth(), texture->GetHeight()};
                m_GuiThumbnails[key] = std::move(token); m_RHIThumbnails[key] = std::move(texture); m_ThumbnailCache[key] = id; return id;
            }
            TextureSpecification spec; spec.FlipVertically = false; spec.GenerateMipmaps = false;
            spec.WrapS = spec.WrapT = TextureWrap::ClampToEdge;
            auto texture = ImageTextureCache::GetGL(source, spec);
            if (!texture) return 0;
            auto previous = m_LoadedTextures.find(key);
            if (previous != m_LoadedTextures.end() && previous->second != texture) {
                auto retired = std::move(previous->second);
                if (m_DeferredRelease) m_DeferredRelease([retired] {});
            }
            const auto id = texture->GetRendererID(); m_ThumbnailDimensions[key] = {texture->GetWidth(), texture->GetHeight()};
            m_LoadedTextures[key] = std::move(texture); m_ThumbnailCache[key] = id; return id;
        }
        if (auto it = m_ThumbnailCache.find(key); it != m_ThumbnailCache.end()) return it->second;

        // 2. Material Assets (.axleasset): High-Fidelity 3D PBR Sphere Thumbnail (Unreal Engine Parity)
        // Renders an offscreen Cook-Torrance PBR material sphere with 2x2 MSAA supersampling and studio lighting
        bool isMaterial = (ext == ".axleasset" && AssetDSL::PeekAssetKindFromFile(path.string()) == AssetKind::Material);
        if (isMaterial) {
            std::vector<uint8_t> pixels;
            uint32_t width = 0, height = 0;
            if (RenderMaterialThumbnail(path, pixels, width, height) && !pixels.empty()) {
                m_ThumbnailDimensions[key] = glm::vec2((float)width, (float)height);

                auto image = std::make_shared<DecodedImage>();
                image->Mips.push_back({width, height, std::move(pixels)});
                ImageSourceData source; source.SourceKey = "generated-material:" + std::to_string(FileSystem::GetSourceGeneration()) + ":" + path.lexically_normal().generic_string();
                source.RequestKey = source.SourceKey; source.Revision = ImageContentRevision(image->Mips.front().Pixels); source.Image = image;
                if (RHI::RHIDevice::Get().IsNativeReady()) {
                    auto texture = ImageTextureCache::GetNative(source);
                    auto token = texture && m_TextureRegistrar ? m_TextureRegistrar(*texture) : GuiTextureLease{};
                    if (token.IsValid()) {
                        const auto id = token.GetID(); m_GuiThumbnails[key] = std::move(token);
                        m_RHIThumbnails[key] = std::move(texture); m_ThumbnailCache[key] = id; return id;
                    }
                } else {
                    TextureSpecification spec; spec.GenerateMipmaps = false; spec.WrapS = spec.WrapT = TextureWrap::ClampToEdge;
                    auto texture = ImageTextureCache::GetGL(source, spec);
                    if (texture) { const auto id = texture->GetRendererID(); m_LoadedTextures[key] = std::move(texture); m_ThumbnailCache[key] = id; return id; }
                }
            }
            m_ThumbnailCache[key] = 0;
            return 0;
        }

        // 3. 3D Meshes & Assemblies
        if (!RHI::RHIDevice::Get().IsNativeReady() && !Window::HasActiveContext()) {
            return 0;
        }

        // Check if path is supported 3D format
        bool isSupported = (ext == ".axlemesh" || ext == ".axle" || ext == ".axlebin" || 
                            ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx");
        if (!isSupported) {
            return 0;
        }

        // Render synchronously if pending queue is empty or small
        if (m_PendingQueue.size() < 3) {
            uint64_t handle = 0;
            AssetCachedMeta meta;
            if (RenderThumbnail(path, handle, meta)) {
                m_ThumbnailCache[key] = handle;
                m_MetaCache[key] = meta;
                return handle;
            }
        }

        // Queue for background frame slicing
        if (std::find(m_PendingQueue.begin(), m_PendingQueue.end(), path) == m_PendingQueue.end()) {
            m_PendingQueue.push_back(path);
        }

        return 0;
    }

    void AssetThumbnailManager::OnUpdate() {
        if ((!RHI::RHIDevice::Get().IsNativeReady() && !Window::HasActiveContext()) || m_PendingQueue.empty()) return;

        size_t count = 0;
        while (!m_PendingQueue.empty() && count < m_MaxGenerationsPerFrame) {
            auto path = m_PendingQueue.front();
            m_PendingQueue.erase(m_PendingQueue.begin());

            std::string key = path.lexically_normal().string();
            if (m_ThumbnailCache.find(key) != m_ThumbnailCache.end()) {
                continue;
            }

            uint64_t handle = 0;
            AssetCachedMeta meta;
            if (RenderThumbnail(path, handle, meta)) {
                m_ThumbnailCache[key] = handle;
                m_MetaCache[key] = meta;
            } else {
                m_ThumbnailCache[key] = 0; // Mark failed to avoid infinite retry
            }
            count++;
        }
    }

    bool AssetThumbnailManager::RenderThumbnail(const std::filesystem::path& path, uint64_t& outTexHandle, AssetCachedMeta& outMeta) {
        if (!std::filesystem::exists(path)) return false;
        if (!RHI::RHIDevice::Get().IsNativeReady() && !Window::HasActiveContext()) return false;

        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

        AxleLoadedPackage pkg;

        // Case A: Material Asset (.axleasset) -> render on preview sphere
        if (ext == ".axleasset" && AssetDSL::PeekAssetKindFromFile(path.string()) == AssetKind::Material) {
            MaterialComponent mat;
            if (!SceneHierarchyPanel::LoadMaterialAsset(path, mat)) {
                return false;
            }
            if (!m_PreviewSphereMesh) {
                m_PreviewSphereMesh = PrimitiveMesh::CreateSphere(48, 64);
            }
            pkg.Name = path.stem().string();
            pkg.BoundingSize = glm::vec3(1.0f);
            pkg.HalfExtent = glm::vec3(0.5f);
            pkg.IsPackage = false;
            pkg.Success = true;

            AxlePackagePart spherePart;
            spherePart.Name = "MaterialSphere";
            spherePart.RelativePath = path.filename().string();
            spherePart.LoadedMesh.Success = true;
            spherePart.LoadedMesh.SourceData.Name = pkg.Name;
            spherePart.LoadedMesh.SourceData.ColorPrimary = mat.AlbedoColor;
            spherePart.LoadedMesh.SourceData.ColorSecondary = mat.AlbedoColor * 0.85f;
            spherePart.LoadedMesh.SourceData.PatternMode = 2; // Spherical PBR lighting pattern
            spherePart.LoadedMesh.SourceData.TileScale = 1.0f;
            spherePart.LoadedMesh.SourceData.Vertices = { glm::vec3(-0.5f), glm::vec3(0.5f) };
            spherePart.LoadedMesh.SourceData.Indices = { 0, 1, 0 };
            pkg.Parts.push_back(spherePart);
        }
        // Case B: Assembly Prefab (.axle) -> aggregate parts
        else if (ext == ".axle") {
            auto asmData = AssemblySerializer::LoadAssemblyData(path);
            if (!asmData.IsValid) return false;

            std::function<void(const AssemblyNode&, const glm::mat4&)> collectParts = 
                [&](const AssemblyNode& node, const glm::mat4& parentMat) {
                glm::mat4 currentMat = parentMat * node.Transform.GetTransform();
                if (node.HasMesh && !node.Mesh.AssetPath.empty()) {
                    auto meshPath = FileSystem::ResolveAssetPath(node.Mesh.AssetPath);
                    if (!std::filesystem::exists(meshPath)) {
                        if (meshPath.extension() == ".axle") {
                            auto alt = meshPath;
                            alt.replace_extension(".axlemesh");
                            if (std::filesystem::exists(alt)) meshPath = alt;
                        }
                    }
                    if (std::filesystem::exists(meshPath)) {
                        auto loaded = AssetSerializer::LoadMesh(meshPath);
                        if (loaded.Success) {
                            AxlePackagePart part;
                            part.Name = node.Tag;
                            part.RelativePath = node.Mesh.AssetPath;
                            part.LocalTranslation = glm::vec3(currentMat[3]);
                            part.LocalRotation = glm::quat_cast(currentMat);
                            part.LocalScale = glm::vec3(glm::length(currentMat[0]), glm::length(currentMat[1]), glm::length(currentMat[2]));
                            part.LoadedMesh = loaded;
                            if (node.Mesh.Color1 != glm::vec4(0.0f)) {
                                part.LoadedMesh.SourceData.ColorPrimary = node.Mesh.Color1;
                                part.LoadedMesh.SourceData.ColorSecondary = node.Mesh.Color2;
                                part.LoadedMesh.SourceData.PatternMode = node.Mesh.PatternMode;
                                part.LoadedMesh.SourceData.TileScale = node.Mesh.TileScale;
                            }
                            pkg.Parts.push_back(part);
                        }
                    }
                }
                for (const auto& child : node.Children) {
                    collectParts(child, currentMat);
                }
            };

            collectParts(asmData.RootNode, glm::mat4(1.0f));
            if (pkg.Parts.empty()) {
                return false;
            }
            pkg.Name = asmData.Name;
            pkg.Success = true;
        }
        // Case C: 3D Mesh (.axlemesh, .obj, .gltf, etc.)
        else {
            pkg = AssetSerializer::LoadPackage(path);
            if (!pkg.Success || pkg.Parts.empty()) {
                return false;
            }
        }

        // 1. Compute aggregate bounds across all loaded parts
        glm::vec3 minBound(1e9f);
        glm::vec3 maxBound(-1e9f);
        size_t totalVertices = 0;
        size_t totalIndices = 0;

        for (const auto& part : pkg.Parts) {
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

        glm::vec3 center = (minBound + maxBound) * 0.5f;
        glm::vec3 size = maxBound - minBound;
        float maxDim = std::max({ size.x, size.y, size.z, 0.05f });

        // Populate metadata
        outMeta.Name = pkg.Name.empty() ? path.stem().string() : pkg.Name;
        outMeta.VertexCount = totalVertices;
        outMeta.TriangleCount = totalIndices / 3;
        outMeta.BoundingSize = size;
        outMeta.Center = center;
        outMeta.MaxDimension = maxDim;
        outMeta.IsPackage = pkg.IsPackage;
        outMeta.SubPartCount = pkg.Parts.size();

        if (!pkg.Parts.empty()) {
            const auto& firstPart = pkg.Parts[0].LoadedMesh.SourceData;
            outMeta.ColorPrimary = firstPart.ColorPrimary;
            outMeta.ColorSecondary = firstPart.ColorSecondary;
            outMeta.PatternMode = firstPart.PatternMode;
            outMeta.TileScale = firstPart.TileScale;
        }

        // 2. Build RenderWorld
        RenderWorld world;
        world.TotalEntities = static_cast<uint32_t>(pkg.Parts.size());

        RenderDirectionalLight keyLight;
        keyLight.Direction = glm::normalize(glm::vec3(0.5f, 0.85f, 0.6f));
        keyLight.Color = glm::vec3(1.0f);
        keyLight.Intensity = 1.0f;
        keyLight.AmbientIntensity = 0.34f;
        world.DirectionalLight = keyLight;

        RenderDirectionalLight fillLight;
        fillLight.Direction = glm::normalize(glm::vec3(-0.4f, 0.5f, -0.6f));
        fillLight.Color = glm::vec3(0.85f, 0.90f, 1.0f);
        fillLight.Intensity = 0.35f;
        world.AdditionalDirectionalLights.push_back(fillLight);

        for (size_t i = 0; i < pkg.Parts.size(); ++i) {
            const auto& part = pkg.Parts[i];
            glm::mat4 localMat = glm::translate(glm::mat4(1.0f), part.LocalTranslation)
                               * glm::mat4_cast(part.LocalRotation)
                               * glm::scale(glm::mat4(1.0f), part.LocalScale);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), -center) * localMat;

            RenderMeshInstance rmi;
            rmi.EntityID = static_cast<uint32_t>(i + 1);
            rmi.WorldTransform = model;
            rmi.Material.AlbedoColor = part.LoadedMesh.SourceData.ColorPrimary;
            rmi.Material.SecondaryColor = part.LoadedMesh.SourceData.ColorSecondary;
            rmi.Material.PatternMode = part.LoadedMesh.SourceData.PatternMode;
            rmi.Material.TileScale = glm::vec2(part.LoadedMesh.SourceData.TileScale);
            rmi.Material.Roughness = 0.5f;
            rmi.Material.Metallic = 0.0f;

            if (part.Name == "MaterialSphere" && m_PreviewSphereMesh) {
                rmi.GLMesh = m_PreviewSphereMesh;
            } else if (!part.LoadedMesh.SourceData.Vertices.empty()) {
                rmi.GLMesh = CreateVertexArrayFromMeshSource(part.LoadedMesh.SourceData);
            }

#if defined(AXLE_BUILD_RHI)
            if (RHI::RHIDevice::Get().IsNativeReady()) {
                if (!part.LoadedMesh.SourceData.Vertices.empty()) {
                    rmi.NativeMesh = RHI::RHIMesh::CreateFromAxleMesh(part.LoadedMesh.SourceData.Vertices, part.LoadedMesh.SourceData.Indices);
                } else if (!part.RelativePath.empty()) {
                    rmi.NativeMesh = RHIMeshRegistry::Get().GetOrCreateMesh(part.RelativePath);
                }
            }
#endif
            world.Meshes.push_back(std::move(rmi));
        }

        // 3. Studio camera
        float pitchRad = glm::radians(24.0f);
        float yawRad = glm::radians(38.0f);
        float distance = maxDim * 1.85f;
        glm::vec3 camPos = glm::vec3(
            distance * std::cos(pitchRad) * std::sin(yawRad),
            distance * std::sin(pitchRad),
            distance * std::cos(pitchRad) * std::cos(yawRad)
        );
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        PerspectiveCamera cam(45.0f, 1.0f, 0.01f, distance * 6.0f);
        cam.SetViewMatrix(view, camPos);

        // 4. ViewRequest
        ViewRequest request;
        request.Kind = RenderViewKind::Thumbnail;
        request.Width = m_ThumbnailResolution;
        request.Height = m_ThumbnailResolution;
        request.Camera = cam;
        request.Settings.ClearColor = glm::vec4(0.11f, 0.13f, 0.17f, 1.0f);
        request.Settings.EnableShadows = false;
        request.Settings.EnableSkybox = false;
        request.Name = "Thumbnail_" + path.filename().string();

        std::string key = path.lexically_normal().string();

#if defined(AXLE_BUILD_RHI)
        if (RHI::RHIDevice::Get().IsNativeReady()) {
            auto rhiTex = std::make_shared<RHI::RHITexture>();
            if (rhiTex->Init(RHI::RHITextureDesc::RenderTarget2D(m_ThumbnailResolution, m_ThumbnailResolution, RHI::RHITextureFormat::RGBA8_UNORM, request.Name))) {
                request.ColorTarget = rhiTex.get();
                auto result = RenderService::Get().RenderView(world, request);
                if (result.Success) {
                    auto token = m_TextureRegistrar ? m_TextureRegistrar(*rhiTex) : GuiTextureLease{};
                    if (token.IsValid()) {
                        outTexHandle = token.GetID();
                        m_GuiThumbnails[key] = std::move(token);
                        m_RHIThumbnails[key] = std::move(rhiTex);
                        return true;
                    }
                }
            }
            return false;
        }
#endif

        if (Window::HasActiveContext()) {
            FramebufferSpecification spec;
            spec.Width = m_ThumbnailResolution;
            spec.Height = m_ThumbnailResolution;
            auto fb = Framebuffer::Create(spec);
            request.GLTargetFBO = fb;
            auto result = RenderService::Get().RenderView(world, request);
            if (result.Success) {
                outTexHandle = fb->GetColorAttachmentRendererID();
                m_GeneratedTextures.push_back(static_cast<u32>(outTexHandle));
                return true;
            }
        }
        return false;
    }

    bool AssetThumbnailManager::RenderMaterialThumbnail(const std::filesystem::path& path, std::vector<uint8_t>& outPixels, uint32_t& outWidth, uint32_t& outHeight) {
        if (!std::filesystem::exists(path)) return false;

        MaterialComponent mat;
        if (!SceneHierarchyPanel::LoadMaterialAsset(path, mat)) {
            return false;
        }

        const uint32_t res = m_ThumbnailResolution;
        outWidth = res;
        outHeight = res;
        outPixels.resize(res * res * 4);

        glm::vec3 albedo = glm::vec3(mat.AlbedoColor.r, mat.AlbedoColor.g, mat.AlbedoColor.b);
        float metallic = std::clamp(mat.Metallic, 0.0f, 1.0f);
        float roughness = std::clamp(mat.Roughness, 0.04f, 1.0f);
        float clearcoat = std::clamp(mat.ClearcoatStrength, 0.0f, 1.0f);
        float clearcoatRoughness = std::clamp(mat.ClearcoatRoughness, 0.01f, 1.0f);
        glm::vec3 emission = glm::vec3(mat.EmissionColor) * mat.EmissionIntensity;

        int texW = 0, texH = 0, texChannels = 0;
        const uint8_t* texPixels = nullptr;
        ImageSourceData albedoImage;
        if (mat.UseAlbedoMap && !mat.AlbedoPath.empty()) {
            albedoImage = LoadImageSource(mat.AlbedoPath, {});
            if (albedoImage.Image && !albedoImage.Image->HDR) {
                const auto& mip = albedoImage.Image->Mips.front(); texW = mip.Width; texH = mip.Height; texChannels = albedoImage.Image->SourceChannels;
                texPixels = mip.Pixels.data();
            }
        }

        const glm::vec3 keyDir = glm::normalize(glm::vec3(0.45f, 0.65f, 0.60f));
        const glm::vec3 fillDir = glm::normalize(glm::vec3(-0.55f, 0.50f, 0.50f));

        const float radiusPixels = 54.0f;
        const float centerOffset = 64.0f;

        for (uint32_t py = 0; py < res; ++py) {
            for (uint32_t px = 0; px < res; ++px) {
                float accR = 0.0f, accG = 0.0f, accB = 0.0f, accA = 0.0f;

                for (int sy = 0; sy < 2; ++sy) {
                    for (int sx = 0; sx < 2; ++sx) {
                        float x = (((float)px + 0.25f + (float)sx * 0.5f) - centerOffset) / radiusPixels;
                        float y = (((float)py + 0.25f + (float)sy * 0.5f) - centerOffset) / radiusPixels;
                        float r2 = x * x + y * y;

                        if (r2 > 1.0f) {
                            float bgY = 1.0f - ((float)py / (float)res);
                            float bg = 0.075f + 0.035f * bgY;

                            float shadowY = ((float)py - 100.0f) / 22.0f;
                            float shadowX = x * 1.25f;
                            float shadowR2 = shadowX * shadowX + shadowY * shadowY;
                            if (shadowR2 < 1.0f && py > 85) {
                                bg *= (0.40f + 0.60f * std::sqrt(shadowR2));
                            }

                            accR += bg * 0.95f;
                            accG += bg;
                            accB += bg * 1.15f;
                            accA += 1.0f;
                            continue;
                        }

                        float z = std::sqrt(std::max(0.0f, 1.0f - r2));
                        glm::vec3 N(x, -y, z);
                        glm::vec3 V(0.0f, 0.0f, 1.0f);

                        float NdotV = std::max(0.001f, N.z);

                        glm::vec3 pixelAlbedo = albedo;
                        if (texPixels && texW > 0 && texH > 0) {
                            float u = std::atan2(N.x, N.z) / (2.0f * 3.14159265f) + 0.5f;
                            float v = std::asin(std::clamp(N.y, -1.0f, 1.0f)) / 3.14159265f + 0.5f;
                            int tx = std::clamp((int)(u * (float)texW), 0, texW - 1);
                            int ty = std::clamp((int)(v * (float)texH), 0, texH - 1);
                            int idx = (ty * texW + tx) * 4;
                            glm::vec3 texColor = glm::vec3(
                                std::pow(texPixels[idx + 0] / 255.0f, 2.2f),
                                std::pow(texPixels[idx + 1] / 255.0f, 2.2f),
                                std::pow(texPixels[idx + 2] / 255.0f, 2.2f)
                            );
                            pixelAlbedo *= texColor;
                        }

                        glm::vec3 F0 = glm::mix(glm::vec3(0.04f), pixelAlbedo, metallic);

                        float NdotL = glm::dot(N, keyDir);
                        float NdotL_diff = std::clamp((NdotL + 0.22f) / 1.488f, 0.0f, 1.0f);
                        float NdotL_spec = std::max(0.0f, NdotL);

                        glm::vec3 H = glm::normalize(V + keyDir);
                        float NdotH = std::max(0.0f, glm::dot(N, H));
                        float VdotH = std::max(0.0f, glm::dot(V, H));

                        float a2 = roughness * roughness * roughness * roughness;
                        float dDenom = (NdotH * NdotH * (a2 - 1.0f) + 1.0f);
                        float ndf = a2 / (3.14159265f * dDenom * dDenom + 1e-5f);

                        float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
                        float g = (NdotV / (NdotV * (1.0f - k) + k)) * (NdotL_spec / (NdotL_spec * (1.0f - k) + k + 1e-4f));

                        glm::vec3 F = F0 + (glm::vec3(1.0f) - F0) * std::pow(1.0f - VdotH, 5.0f);
                        glm::vec3 spec = (ndf * g * F) / (4.0f * NdotV * std::max(NdotL_spec, 0.001f) + 1e-4f);

                        glm::vec3 kD = (glm::vec3(1.0f) - F) * (1.0f - metallic);
                        glm::vec3 cKey = (kD * pixelAlbedo / 3.14159265f * NdotL_diff + spec * NdotL_spec) * 2.4f;

                        float NdotL_fill = glm::dot(N, fillDir);
                        float NdotL_fill_diff = std::clamp((NdotL_fill + 0.25f) / 1.5625f, 0.0f, 1.0f);
                        glm::vec3 cFill = (1.0f - metallic) * pixelAlbedo / 3.14159265f * NdotL_fill_diff * 1.0f * glm::vec3(0.88f, 0.93f, 1.0f);

                        float upW = std::clamp(N.y * 0.5f + 0.5f, 0.0f, 1.0f);
                        glm::vec3 ambSky = glm::mix(glm::vec3(0.68f, 0.70f, 0.74f), glm::vec3(0.96f, 0.98f, 1.00f), upW);
                        glm::vec3 ambient = ambSky * pixelAlbedo * (1.0f - metallic) * 0.75f + F0 * (0.2f + 0.7f * upW) * (1.0f - roughness * 0.3f);

                        float rim = std::pow(1.0f - NdotV, 4.0f) * 0.20f;
                        glm::vec3 cRim = glm::vec3(0.92f, 0.95f, 1.0f) * rim;

                        glm::vec3 cClearcoat(0.0f);
                        if (clearcoat > 0.001f) {
                            float ccRough2 = clearcoatRoughness * clearcoatRoughness;
                            float ccA2 = ccRough2 * ccRough2;
                            float ccDenom = (NdotH * NdotH * (ccA2 - 1.0f) + 1.0f);
                            float ccNDF = ccA2 / (3.14159265f * ccDenom * ccDenom + 1e-5f);
                            float ccK = (clearcoatRoughness + 1.0f) * (clearcoatRoughness + 1.0f) / 8.0f;
                            float ccG = (NdotV / (NdotV * (1.0f - ccK) + ccK)) * (NdotL_spec / (NdotL_spec * (1.0f - ccK) + ccK + 1e-4f));
                            float ccF = 0.04f + 0.96f * std::pow(1.0f - VdotH, 5.0f);
                            float ccSpec = (ccNDF * ccG * ccF) / (4.0f * NdotV * std::max(NdotL_spec, 0.001f) + 1e-4f);
                            cClearcoat = glm::vec3(ccSpec * NdotL_spec * clearcoat * 0.5f);
                        }

                        glm::vec3 total = cKey + cFill + ambient + cRim + cClearcoat + emission;

                        const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
                        glm::vec3 tm = (total * (a * total + b)) / (total * (c * total + d) + e);

                        glm::vec3 sRGB = glm::pow(glm::clamp(tm, glm::vec3(0.0f), glm::vec3(1.0f)), glm::vec3(1.0f / 2.2f));

                        accR += sRGB.r;
                        accG += sRGB.g;
                        accB += sRGB.b;
                        accA += 1.0f;
                    }
                }

                size_t pixelIdx = (py * res + px) * 4;
                outPixels[pixelIdx + 0] = (uint8_t)std::clamp((int)std::round((accR / 4.0f) * 255.0f), 0, 255);
                outPixels[pixelIdx + 1] = (uint8_t)std::clamp((int)std::round((accG / 4.0f) * 255.0f), 0, 255);
                outPixels[pixelIdx + 2] = (uint8_t)std::clamp((int)std::round((accB / 4.0f) * 255.0f), 0, 255);
                outPixels[pixelIdx + 3] = (uint8_t)std::clamp((int)std::round((accA / 4.0f) * 255.0f), 0, 255);
            }
        }


        return true;
    }

}
